use crate::checkpoint::{self, Store};
use actor_core::population as core;
use std::{
    fs::{self, File},
    io::Read,
};

#[cxx::bridge(namespace = "eqemu_population")]
mod ffi {
    #[derive(Default)]
    struct Body {
        actor: u8,
        id: u16,
        incarnation: u64,
        epoch: u64,
        generation: u64,
        goal: u64,
        operation: u64,
        alive: bool,
        present: bool,
        drained: bool,
        x: f64,
        y: f64,
        z: f64,
        hp: i64,
        idle: bool,
        moving: bool,
        sitting: bool,
        engaged: bool,
        outcome: Outcome,
        contested: bool,
        target_viable: bool,
        pending_kill: bool,
        swings: u64,
        hits: u64,
        damage: u64,
        incoming_swings: u64,
        incoming_hits: u64,
        incoming_damage: u64,
        probe_id: u16,
        probe_incarnation: u64,
        probe_ready: bool,
        probe_ok: bool,
        error: bool,
        refused: bool,
    }
    struct Prey {
        id: u16,
        incarnation: u64,
        x: f64,
        y: f64,
        z: f64,
        type_id: u32,
        level: u8,
        name: String,
        safe: bool,
        idle: bool,
    }
    #[repr(u8)]
    enum Outcome {
        None,
        Victory,
        Lost,
        Defeated,
        Paused,
    }
    #[repr(u8)]
    enum Kind {
        Spawn,
        Probe,
        Engage,
        Release,
        Rest,
        Stand,
        Retire,
    }
    struct Action {
        actor: u8,
        kind: Kind,
        id: u16,
        incarnation: u64,
        epoch: u64,
        generation: u64,
        target_id: u16,
        target_incarnation: u64,
        goal: u64,
        x: f64,
        y: f64,
        z: f64,
        hp: i64,
    }
    extern "Rust" {
        type Population;
        fn population_open(directory: &str, session: &str) -> Result<Box<Population>>;
        fn needs_scan(self: &Population, now: u64) -> bool;
        fn step(
            self: &mut Population,
            now: u64,
            bodies: Vec<Body>,
            prey: Vec<Prey>,
            scanned: bool,
            overflow: bool,
        ) -> Vec<Action>;
        fn begin_shutdown(self: &mut Population);
        fn failed(self: &Population) -> bool;
        fn prey_profile(type_id: u32, level: u8, name: &str) -> bool;
    }
}
impl Default for ffi::Outcome {
    fn default() -> Self {
        Self::None
    }
}
fn key(id: u16, incarnation: u64) -> core::Key {
    core::Key { id, incarnation }
}
fn body(b: &ffi::Body) -> core::Body {
    core::Body {
        lifetime: core::Lifetime {
            key: key(b.id, b.incarnation),
            epoch: b.epoch,
            generation: b.generation,
        },
        runtime_alive: b.alive,
        present: b.present,
        drained: b.drained,
        point: core::Point {
            x: b.x,
            y: b.y,
            z: b.z,
        },
        hp: b.hp,
        goal: b.goal,
        operation: b.operation,
        idle: b.idle,
        moving: b.moving,
        sitting: b.sitting,
        engaged: b.engaged,
        outcome: match b.outcome {
            ffi::Outcome::Victory => core::Outcome::Victory,
            ffi::Outcome::Lost => core::Outcome::Lost,
            ffi::Outcome::Defeated => core::Outcome::Defeated,
            ffi::Outcome::Paused => core::Outcome::Paused,
            _ => core::Outcome::None,
        },
        contested: b.contested,
        target_viable: b.target_viable,
        pending_kill: b.pending_kill,
        swings: b.swings,
        hits: b.hits,
        damage: b.damage,
        incoming_swings: b.incoming_swings,
        incoming_hits: b.incoming_hits,
        incoming_damage: b.incoming_damage,
        probe: key(b.probe_id, b.probe_incarnation),
        probe_ready: b.probe_ready,
        probe_ok: b.probe_ok,
        error: b.error
            || !matches!(
                b.outcome,
                ffi::Outcome::None
                    | ffi::Outcome::Victory
                    | ffi::Outcome::Lost
                    | ffi::Outcome::Defeated
                    | ffi::Outcome::Paused
            ),
        refused: b.refused,
    }
}
fn action(a: core::Action) -> ffi::Action {
    ffi::Action {
        actor: a.actor as u8,
        kind: match a.kind {
            core::ActionKind::Spawn => ffi::Kind::Spawn,
            core::ActionKind::Probe => ffi::Kind::Probe,
            core::ActionKind::Engage => ffi::Kind::Engage,
            core::ActionKind::Release => ffi::Kind::Release,
            core::ActionKind::Rest => ffi::Kind::Rest,
            core::ActionKind::Stand => ffi::Kind::Stand,
            core::ActionKind::Retire => ffi::Kind::Retire,
        },
        id: a.lifetime.key.id,
        incarnation: a.lifetime.key.incarnation,
        epoch: a.lifetime.epoch,
        generation: a.lifetime.generation,
        target_id: a.target.id,
        target_incarnation: a.target.incarnation,
        goal: a.goal,
        x: a.point.x,
        y: a.point.y,
        z: a.point.z,
        hp: a.hp,
    }
}

pub struct Population {
    store: Store,
    core: core::Population,
    session: String,
    revision: u64,
    clean: bool,
    fault: String,
    last_request: u64,
    command_result: &'static str,
    io_at: u64,
    save_at: u64,
    latest: [core::Body; core::COUNT],
}
fn population_open(directory: &str, session: &str) -> Result<Box<Population>, String> {
    crate::boundary(|| population_open_inner(directory, session))
}
fn population_open_inner(directory: &str, session: &str) -> Result<Box<Population>, String> {
    let store = Store::open(directory)?;
    let loaded = store.load();
    let (saved, revision, clean, fault) = match loaded {
        Ok(l) => (
            l.saved,
            l.revision,
            l.clean,
            if l.clean {
                String::new()
            } else {
                "recovery_required".into()
            },
        ),
        Err(e) => (std::array::from_fn(core::Saved::initial), 0, false, e),
    };
    let old = store.directory.join("command");
    if let Err(e) = fs::remove_file(old) {
        if e.kind() != std::io::ErrorKind::NotFound {
            return Err(format!("command_cleanup: {e}"));
        }
    }
    Ok(Box::new(Population {
        store,
        core: core::Population::new(saved),
        session: session.into(),
        revision,
        clean,
        fault,
        last_request: 0,
        command_result: "ready",
        io_at: 0,
        save_at: 0,
        latest: [core::Body {
            drained: true,
            ..core::Body::default()
        }; core::COUNT],
    }))
}
fn prey_profile(type_id: u32, level: u8, name: &str) -> bool {
    core::prey_profile(type_id, level, name)
}
impl Population {
    fn failed(&self) -> bool {
        !self.fault.is_empty()
    }
    fn needs_scan(&self, now: u64) -> bool {
        !self.failed() && self.core.needs_scan(now)
    }
    fn begin_shutdown(&mut self) {
        self.core.drain();
        self.io_at = 0;
    }
    fn save(&mut self, clean: bool) -> bool {
        let Some(revision) = self.revision.checked_add(1) else {
            self.fault = "revision_exhausted".into();
            self.core.drain();
            return false;
        };
        match self.store.write(&self.core.saved(), revision, clean) {
            Ok(()) => {
                self.revision = revision;
                self.clean = clean;
                true
            }
            Err(e) => {
                self.fault = e;
                self.core.drain();
                false
            }
        }
    }
    fn command(&mut self) {
        let path = self.store.directory.join("command");
        let f = match File::open(&path) {
            Ok(f) => f,
            Err(e) if e.kind() == std::io::ErrorKind::NotFound => return,
            Err(e) => {
                self.fault = format!("command_read: {e}");
                self.core.drain();
                return;
            }
        };
        let mut bytes = Vec::new();
        let read = f.take(129).read_to_end(&mut bytes);
        if fs::remove_file(&path).is_err() || read.is_err() {
            self.fault = "command_consume_failed".into();
            self.core.drain();
            return;
        }
        let Some((verb, session, request, index)) = parse(&bytes) else {
            self.command_result = "invalid_command";
            return;
        };
        if session != self.session {
            self.command_result = "stale_session";
            return;
        }
        if request <= self.last_request {
            self.command_result = "replayed_request";
            return;
        }
        self.last_request = request;
        if self.failed() {
            self.command_result = "recovery_required";
            return;
        }
        match verb {
            "start" => {
                if self.core.quiescing
                    || self.core.actors.iter().enumerate().any(|(i, a)| {
                        index.is_none_or(|n| n == i)
                            && a.saved.desired != core::Desired::Enabled
                            && a.phase != core::Phase::Dormant
                    })
                {
                    self.command_result = "busy";
                    return;
                }
                self.command_result = if self.core.start(index) {
                    "start_accepted"
                } else {
                    "dead_actor"
                };
                if !self.save(false) {
                    self.command_result = "storage_failed";
                }
            }
            "stop" => {
                self.core.stop(index);
                self.command_result = "stop_queued";
                if !self.save(false) {
                    self.command_result = "storage_failed";
                }
            }
            "drain" => {
                self.core.drain();
                self.command_result = "drain_queued";
            }
            _ => unreachable!(),
        }
    }
    fn step(
        &mut self,
        now: u64,
        bodies: Vec<ffi::Body>,
        prey: Vec<ffi::Prey>,
        scanned: bool,
        overflow: bool,
    ) -> Vec<ffi::Action> {
        crate::boundary(|| self.step_inner(now, bodies, prey, scanned, overflow))
    }
    fn step_inner(
        &mut self,
        now: u64,
        bodies: Vec<ffi::Body>,
        prey: Vec<ffi::Prey>,
        scanned: bool,
        overflow: bool,
    ) -> Vec<ffi::Action> {
        if bodies.len() != core::COUNT
            || bodies
                .iter()
                .enumerate()
                .any(|(i, b)| b.actor as usize != i)
            || prey.len() > 512
        {
            self.fault = "invalid_native_frame".into();
            self.core.drain();
        } else {
            self.latest = std::array::from_fn(|i| body(&bodies[i]));
        }
        let io = now >= self.io_at;
        if io {
            self.io_at = now + 1000;
            self.command();
        }
        if self.failed() {
            self.core.drain();
        }
        // Overflow cannot silently change nearest selection by truncating facts.
        let candidates: Vec<_> = if overflow {
            Vec::new()
        } else {
            prey.into_iter()
                .map(|p| core::Candidate {
                    key: key(p.id, p.incarnation),
                    point: core::Point {
                        x: p.x,
                        y: p.y,
                        z: p.z,
                    },
                    type_id: p.type_id,
                    level: p.level,
                    name: p.name,
                    safe: p.safe,
                    idle: p.idle,
                })
                .collect()
        };
        let before = self.core.saved();
        let mut actions = self.core.tick(now, &self.latest, &candidates, scanned);
        if actions.iter().any(|a| a.kind == core::ActionKind::Spawn)
            && self.clean
            && !self.save(false)
        {
            actions.retain(|a| a.kind != core::ActionKind::Spawn);
        }
        let defeated = before.iter().zip(self.core.actors.iter()).any(|(old, a)| {
            old.desired != core::Desired::Dead && a.saved.desired == core::Desired::Dead
        });
        let was_clean = self.clean;
        if !self.failed() {
            let quiet = self.core.drained(&self.latest)
                && (self.core.quiescing
                    || self
                        .core
                        .actors
                        .iter()
                        .all(|a| a.saved.desired != core::Desired::Enabled));
            if quiet && (!self.clean || self.revision == 0) {
                self.save(true);
            } else if !quiet && (defeated || now >= self.save_at) {
                self.save_at = now + 5000;
                self.save(false);
            }
        }
        if self.failed() {
            actions.retain(|a| a.kind == core::ActionKind::Retire);
            // Disk failure inhibits all three immediately on this return,
            // without waiting for another scheduled policy tick.
            for i in 0..core::COUNT {
                if !self.latest[i].drained && !actions.iter().any(|a| a.actor == i) {
                    let a = &self.core.actors[i];
                    actions.push(core::Action {
                        actor: i,
                        kind: core::ActionKind::Retire,
                        lifetime: a.lifetime,
                        target: a.target,
                        goal: a.goal,
                        point: a.saved.point,
                        hp: a.saved.hp,
                    });
                }
            }
        }
        if io || (!was_clean && self.clean) {
            self.status(now, overflow);
        }
        actions.into_iter().map(action).collect()
    }
    fn status(&mut self, now: u64, overflow: bool) {
        let live = self.latest.iter().filter(|b| b.present).count();
        let actors:Vec<_>=self.core.actors.iter().enumerate().map(|(i,a)|{
            let b=self.latest[i];serde_json::json!({"id":format!("hunter-{}",i+1),"desired":a.saved.desired.name(),"phase":a.phase.name(),
                "reason":a.reason,"native_id":b.lifetime.key.id,"incarnation":b.lifetime.key.incarnation,"epoch":b.lifetime.epoch,"generation":b.lifetime.generation,
                "body_present":b.present,"runtime_alive":b.runtime_alive,"drained":b.drained,"native_hp":if b.present{Some(b.hp)}else{None},
                "goal":a.goal,"operation":b.operation,"target_id":a.target.id,"target_incarnation":a.target.incarnation,
                "position":[a.saved.point.x,a.saved.point.y,a.saved.point.z],"hp":a.saved.hp,"rest_remaining_ms":a.saved.rest_ms,
                "completed":a.saved.completed,"yielded":a.saved.yielded,"failed":a.saved.failed,"eligible":a.eligible,"claim_conflicts":a.claim_conflicts,
                "swings":a.outgoing[0],"hits":a.outgoing[1],"damage":a.outgoing[2],"incoming_swings":a.incoming[0],"incoming_hits":a.incoming[1],"incoming_damage":a.incoming[2],
                "minimum_hp":a.minimum_hp,"observed_rest_hp_gain":a.rest_hp_gain})
        }).collect();
        let claims:Vec<_>=self.core.claims.iter().map(|(k,c)|serde_json::json!({"target_id":k.id,"target_incarnation":k.incarnation,
            "owner":format!("hunter-{}",c.actor+1),"owner_native_id":c.lifetime.key.id,"owner_incarnation":c.lifetime.key.incarnation,
            "epoch":c.lifetime.epoch,"generation":c.lifetime.generation,"goal":c.goal})).collect();
        let data = serde_json::json!({"schema":1,"scenario":"population-hunt-v1","session":self.session,"last_request":self.last_request,"command_result":self.command_result,
            "phase":if self.failed(){"recovery_required"}else if self.core.quiescing{"draining"}else{"ready"},"error":self.fault,
            "checkpoint_revision":self.revision,"checkpoint_clean":self.clean,"last_sync_us":self.store.sync_us,"scan_overflow":overflow,
            "restart_safe":!self.failed() && self.clean && self.core.quiescing && self.core.drained(&self.latest),
            "live_owned":live,"custody_drained":self.core.drained(&self.latest),"actors":actors,"claims":claims,"updated_ms":now});
        let bytes = serde_json::to_vec(&data).expect("finite native status");
        // Status needs atomic visibility, not a disk barrier each second.
        let tmp = self.store.directory.join("status.json.tmp");
        if bytes.len() > 65_536
            || fs::write(&tmp, bytes)
                .and_then(|_| fs::rename(tmp, self.store.directory.join("status.json")))
                .is_err()
        {
            self.fault = "status_io_failed".into();
            self.core.drain();
        }
    }
}
fn parse(bytes: &[u8]) -> Option<(&str, &str, u64, Option<usize>)> {
    if bytes.len() > 128 {
        return None;
    }
    let text = std::str::from_utf8(bytes).ok()?;
    let text = text.strip_suffix('\n').unwrap_or(text);
    let text = text.strip_suffix('\r').unwrap_or(text);
    if text
        .bytes()
        .any(|b| !(b' '..=b'~').contains(&b) && b != b'\t')
    {
        return None;
    }
    let words: Vec<_> = text.split_ascii_whitespace().collect();
    if words.len() < 3 || !words[2].bytes().all(|b| b.is_ascii_digit()) {
        return None;
    }
    let request = words[2].parse::<u64>().ok()?;
    if request == 0 {
        return None;
    }
    let index = match words[0] {
        "drain" if words.len() == 3 => None,
        "start" | "stop" if words.len() == 4 => match words[3] {
            "all" => None,
            "hunter-1" => Some(0),
            "hunter-2" => Some(1),
            "hunter-3" => Some(2),
            _ => return None,
        },
        _ => return None,
    };
    Some((words[0], words[1], request, index))
}

#[cfg(test)]
mod tests {
    use super::*;
    fn dir() -> std::path::PathBuf {
        use std::os::unix::fs::PermissionsExt;
        let p = std::env::temp_dir().join(format!(
            "eqemu-pop-bridge-{}-{}",
            std::process::id(),
            std::time::SystemTime::now()
                .duration_since(std::time::UNIX_EPOCH)
                .unwrap()
                .as_nanos()
        ));
        fs::create_dir(&p).unwrap();
        fs::set_permissions(&p, fs::Permissions::from_mode(0o700)).unwrap();
        p
    }
    fn bodies() -> Vec<ffi::Body> {
        (0..3)
            .map(|i| ffi::Body {
                actor: i,
                drained: true,
                ..ffi::Body::default()
            })
            .collect()
    }
    #[test]
    fn dirty_is_durable_before_spawn_and_crash_refuses_replay() {
        let d = dir();
        let mut p = population_open(d.to_str().unwrap(), "session1").unwrap();
        fs::write(d.join("command"), "start session1 1 all\n").unwrap();
        let actions = p.step(1, bodies(), Vec::new(), false, false);
        assert_eq!(actions.len(), 3);
        assert!(!p.store.load().unwrap().clean);
        drop(p);
        fs::write(d.join("command"), "start session1 2 all\n").unwrap();
        let mut p = population_open(d.to_str().unwrap(), "session2").unwrap();
        assert!(!d.join("command").exists());
        fs::write(d.join("command"), "start session1 2 all\n").unwrap();
        assert!(p.step(2, bodies(), Vec::new(), false, false).is_empty());
        assert_eq!(p.command_result, "stale_session");
        assert!(p.failed());
        drop(p);
        fs::remove_dir_all(d).unwrap();
    }
    #[test]
    fn controls_are_bounded_fresh_and_stop_is_not_clean_drain() {
        assert!(parse(b"start\nsession 1 all").is_none());
        assert!(parse(b"start s +1 all").is_none());
        assert!(parse(b"start s 0 all").is_none());
        assert!(parse(b"drain s 18446744073709551615\r\n").is_some());
        assert!(parse(b"start s 2 hunter-4").is_none());
        let d = dir();
        let mut p = population_open(d.to_str().unwrap(), "s").unwrap();
        fs::write(d.join("command"), "stop s 1 all").unwrap();
        p.step(1, bodies(), Vec::new(), false, false);
        assert!(p.clean);
        assert!(!p.core.quiescing);
        fs::write(d.join("command"), "drain s 2").unwrap();
        p.step(1001, bodies(), Vec::new(), false, false);
        assert!(p.core.quiescing && p.clean);
        drop(p);
        fs::remove_dir_all(d).unwrap();
    }

    #[test]
    fn storage_failure_drains_live_actors_and_leaves_dirty_evidence() {
        let d = dir();
        let mut p = population_open(d.to_str().unwrap(), "s").unwrap();
        fs::write(d.join("command"), "start s 1 all").unwrap();
        p.step(1, bodies(), Vec::new(), false, false);
        let live = || {
            (0..3)
                .map(|i| ffi::Body {
                    actor: i,
                    id: 10 + i as u16,
                    incarnation: 100 + i as u64,
                    epoch: 1 + i as u64,
                    generation: 1,
                    alive: true,
                    present: true,
                    x: core::HOME.x,
                    y: core::HOME.y,
                    z: core::HOME.z,
                    hp: 47,
                    sitting: true,
                    ..ffi::Body::default()
                })
                .collect()
        };
        p.step(2, live(), Vec::new(), false, false);
        fs::create_dir(d.join("checkpoint.json.tmp")).unwrap();
        let actions = p.step(5001, live(), Vec::new(), false, false);
        assert!(p.failed());
        assert_eq!(actions.len(), 3);
        assert!(actions.iter().all(|a| a.kind == ffi::Kind::Retire));
        assert!(!p.store.load().unwrap().clean);
        drop(p);
        let mut restored = population_open(d.to_str().unwrap(), "next").unwrap();
        assert!(
            restored
                .step(1, bodies(), Vec::new(), false, false)
                .is_empty()
        );
        assert!(restored.failed());
        drop(restored);
        fs::remove_dir_all(d).unwrap();
    }

    #[test]
    fn corrupted_or_zero_hp_living_checkpoint_never_spawns() {
        let d = dir();
        let mut p = population_open(d.to_str().unwrap(), "s").unwrap();
        p.step(1, bodies(), Vec::new(), false, false);
        drop(p);
        let good = fs::read_to_string(d.join("checkpoint.json")).unwrap();
        fs::write(
            d.join("checkpoint.json"),
            good.replace("\"hp\":100", "\"hp\":0"),
        )
        .unwrap();
        let evidence = fs::read(d.join("checkpoint.json")).unwrap();
        let mut p = population_open(d.to_str().unwrap(), "next").unwrap();
        fs::write(d.join("command"), "start next 1 all").unwrap();
        assert!(p.step(1, bodies(), Vec::new(), false, false).is_empty());
        assert!(p.failed());
        assert_eq!(fs::read(d.join("checkpoint.json")).unwrap(), evidence);
        drop(p);
        fs::remove_dir_all(d).unwrap();
    }
}

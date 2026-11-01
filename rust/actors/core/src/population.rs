//! Bounded, zone-thread population policy. Native facts are values; actions are
//! requests, never acknowledgements. Checkpoint I/O belongs to the bridge.
use std::collections::BTreeMap;

pub const COUNT: usize = 3;
pub const HOME: Point = Point {
    x: -917.0,
    y: 1224.0,
    z: 3.75,
};
pub const SEARCH: f64 = 150.0;
pub const LEASH: f64 = 180.0;
pub const Z_RANGE: f64 = 6.0;
pub const REST_MS: u64 = 10_000;
pub const SCAN_MS: u64 = 2_000;
pub const PROFILE: &str = "west-qeynos-v1";

#[derive(Clone, Copy, Debug, Default, PartialEq)]
pub struct Point {
    pub x: f64,
    pub y: f64,
    pub z: f64,
}
impl Point {
    pub fn distance(self, p: Self) -> f64 {
        (self.x - p.x).powi(2) + (self.y - p.y).powi(2)
    }
    pub fn inside(self, radius: f64) -> bool {
        self.x.is_finite()
            && self.y.is_finite()
            && self.z.is_finite()
            && self.distance(HOME) <= radius * radius
            && (self.z - HOME.z).abs() <= Z_RANGE
    }
}
#[derive(Clone, Copy, Debug, Default, PartialEq, Eq, PartialOrd, Ord)]
pub struct Key {
    pub id: u16,
    pub incarnation: u64,
}
impl Key {
    pub fn valid(self) -> bool {
        self.id != 0 && self.incarnation != 0
    }
}
#[derive(Clone, Copy, Debug, Default, PartialEq, Eq)]
pub struct Lifetime {
    pub key: Key,
    pub epoch: u64,
    pub generation: u64,
}
impl Lifetime {
    pub fn valid(self) -> bool {
        self.key.valid() && self.epoch != 0 && self.generation != 0
    }
}

pub fn prey_profile(id: u32, level: u8, name: &str) -> bool {
    matches!(
        (id, level, name),
        (2011, 1, "a_large_rat")
            | (2104, 1, "a_decaying_skeleton")
            | (2000, 2, "a_decaying_skeleton")
            | (2002, 1, "a_gnoll_pup")
            | (2003, 1, "a_grass_snake")
            | (2017, 1, "a_brown_myotis_bat")
            | (2101, 1, "a_gopher_snake")
            | (2014, 2, "a_klicnik_worker")
            | (2021, 2, "a_klicnik_drone")
            | (2024, 2, "a_fire_beetle")
    )
}
pub fn prey_type(id: u32, level: u8) -> bool {
    matches!(
        (id, level),
        (2011 | 2104 | 2002 | 2003 | 2017 | 2101, 1) | (2000 | 2014 | 2021 | 2024, 2)
    )
}
#[derive(Clone, Debug)]
pub struct Candidate {
    pub key: Key,
    pub point: Point,
    pub type_id: u32,
    pub level: u8,
    pub name: String,
    pub safe: bool,
    pub idle: bool,
}
impl Candidate {
    pub fn eligible(&self, actor: Point) -> bool {
        self.key.valid()
            && self.safe
            && self.idle
            && self.point.inside(SEARCH)
            && (self.point.z - actor.z).abs() <= Z_RANGE
            && prey_profile(self.type_id, self.level, &self.name)
    }
}
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Desired {
    Paused,
    Enabled,
    Dead,
}
impl Desired {
    pub fn name(self) -> &'static str {
        match self {
            Self::Paused => "paused",
            Self::Enabled => "enabled",
            Self::Dead => "dead",
        }
    }
}
#[derive(Clone, Debug)]
pub struct Saved {
    pub desired: Desired,
    pub point: Point,
    pub hp: i64,
    pub rest_ms: u64,
    pub completed: u64,
    pub yielded: u64,
    pub failed: u64,
}
impl Saved {
    pub fn initial(index: usize) -> Self {
        Self {
            desired: Desired::Paused,
            point: Point {
                x: HOME.x + [0.0, 3.0, -3.0][index],
                ..HOME
            },
            hp: 100,
            rest_ms: REST_MS,
            completed: 0,
            yielded: 0,
            failed: 0,
        }
    }
    pub fn valid(&self) -> bool {
        self.point.inside(LEASH)
            && self.rest_ms <= REST_MS
            && match self.desired {
                Desired::Dead => self.hp == 0,
                _ => (1..=100).contains(&self.hp),
            }
    }
}
#[derive(Clone, Copy, Debug, Default, PartialEq, Eq)]
pub enum Outcome {
    #[default]
    None,
    Victory,
    Lost,
    Defeated,
    Paused,
}
#[derive(Clone, Copy, Debug, Default)]
pub struct Body {
    pub lifetime: Lifetime,
    pub runtime_alive: bool,
    pub present: bool,
    pub drained: bool,
    pub point: Point,
    pub hp: i64,
    pub goal: u64,
    pub operation: u64,
    pub idle: bool,
    pub moving: bool,
    pub sitting: bool,
    pub engaged: bool,
    pub outcome: Outcome,
    pub contested: bool,
    pub target_viable: bool,
    pub pending_kill: bool,
    pub swings: u64,
    pub hits: u64,
    pub damage: u64,
    pub incoming_swings: u64,
    pub incoming_hits: u64,
    pub incoming_damage: u64,
    pub probe: Key,
    pub probe_ready: bool,
    pub probe_ok: bool,
    pub error: bool,
    pub refused: bool,
}
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Phase {
    Dormant,
    Spawning,
    Resting,
    Searching,
    Probing,
    Fighting,
    Releasing,
    Retiring,
}
impl Phase {
    pub fn name(self) -> &'static str {
        match self {
            Self::Dormant => "dormant",
            Self::Spawning => "spawning",
            Self::Resting => "resting",
            Self::Searching => "searching",
            Self::Probing => "probing",
            Self::Fighting => "fighting",
            Self::Releasing => "releasing",
            Self::Retiring => "retiring",
        }
    }
}
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum ActionKind {
    Spawn,
    Probe,
    Engage,
    Release,
    Rest,
    Stand,
    Retire,
}
#[derive(Clone, Copy, Debug)]
pub struct Action {
    pub actor: usize,
    pub kind: ActionKind,
    pub lifetime: Lifetime,
    pub target: Key,
    pub goal: u64,
    pub point: Point,
    pub hp: i64,
}
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct Claim {
    pub actor: usize,
    pub lifetime: Lifetime,
    pub goal: u64,
}

pub struct Actor {
    pub saved: Saved,
    pub phase: Phase,
    pub lifetime: Lifetime,
    pub target: Key,
    pub goal: u64,
    pub reason: &'static str,
    pub eligible: usize,
    pub claim_conflicts: u64,
    pub outgoing: [u64; 3],
    pub incoming: [u64; 3],
    pub minimum_hp: i64,
    pub rest_hp_gain: u64,
    at: u64,
    progress: u64,
    due: u64,
    observed_at: u64,
    last_point: Point,
    last_native: [u64; 3],
    credited_goal: u64,
    rejected: Vec<Key>,
}
impl Actor {
    fn new(saved: Saved) -> Self {
        let hp = saved.hp;
        Self {
            saved,
            phase: Phase::Dormant,
            lifetime: Lifetime::default(),
            target: Key::default(),
            goal: 0,
            reason: "ready",
            eligible: 0,
            claim_conflicts: 0,
            outgoing: [0; 3],
            incoming: [0; 3],
            minimum_hp: hp,
            rest_hp_gain: 0,
            at: 0,
            progress: 0,
            due: 0,
            observed_at: 0,
            last_point: Point::default(),
            last_native: [0; 3],
            credited_goal: 0,
            rejected: Vec::new(),
        }
    }
    fn action(&self, actor: usize, kind: ActionKind) -> Action {
        Action {
            actor,
            kind,
            lifetime: self.lifetime,
            target: self.target,
            goal: self.goal,
            point: self.saved.point,
            hp: self.saved.hp,
        }
    }
}
pub struct Population {
    pub actors: [Actor; COUNT],
    pub claims: BTreeMap<Key, Claim>,
    pub quiescing: bool,
    scan_at: u64,
    probe_at: u64,
    turn: usize,
}
impl Population {
    pub fn new(saved: [Saved; COUNT]) -> Self {
        Self {
            actors: saved.map(Actor::new),
            claims: BTreeMap::new(),
            quiescing: false,
            scan_at: 0,
            probe_at: 0,
            turn: 0,
        }
    }
    pub fn needs_scan(&self, now: u64) -> bool {
        !self.quiescing
            && now >= self.scan_at
            && self
                .actors
                .iter()
                .any(|a| a.saved.desired == Desired::Enabled)
    }
    pub fn saved(&self) -> [Saved; COUNT] {
        std::array::from_fn(|i| self.actors[i].saved.clone())
    }
    pub fn start(&mut self, index: Option<usize>) -> bool {
        if self.quiescing {
            return false;
        }
        let mut changed = false;
        for i in 0..COUNT {
            if index.is_none_or(|v| v == i) && self.actors[i].saved.desired != Desired::Dead {
                self.actors[i].saved.desired = Desired::Enabled;
                changed = true;
            }
        }
        changed
    }
    pub fn stop(&mut self, index: Option<usize>) {
        for i in 0..COUNT {
            if index.is_none_or(|v| v == i) && self.actors[i].saved.desired != Desired::Dead {
                self.actors[i].saved.desired = Desired::Paused;
            }
        }
    }
    pub fn drain(&mut self) {
        self.quiescing = true;
    }
    pub fn drained(&self, bodies: &[Body; COUNT]) -> bool {
        self.claims.is_empty() && bodies.iter().all(|b| b.drained)
    }
    pub fn tick(
        &mut self,
        now: u64,
        bodies: &[Body; COUNT],
        prey: &[Candidate],
        scanned: bool,
    ) -> Vec<Action> {
        if scanned {
            self.scan_at = now.saturating_add(SCAN_MS);
            for a in &mut self.actors {
                a.rejected.clear();
            }
        }
        let mut actions = Vec::with_capacity(COUNT);
        for offset in 0..COUNT {
            let i = (self.turn + offset) % COUNT;
            let a = &mut self.actors[i];
            let b = bodies[i];
            if a.phase == Phase::Spawning
                && b.lifetime.valid()
                && (b.present || b.outcome == Outcome::Defeated)
            {
                a.lifetime = b.lifetime;
            }
            let bound = a.lifetime.valid() && a.lifetime == b.lifetime;
            if bound && b.present && b.point.inside(LEASH) && b.hp > 0 {
                if a.phase == Phase::Resting {
                    if b.sitting && !b.moving && !b.engaged {
                        a.saved.rest_ms = a
                            .saved
                            .rest_ms
                            .saturating_sub(now.saturating_sub(a.observed_at));
                    } else {
                        a.saved.rest_ms = REST_MS;
                    }
                    if b.sitting && !b.engaged && b.hp > a.saved.hp {
                        a.rest_hp_gain += (b.hp - a.saved.hp) as u64;
                    }
                }
                a.saved.point = b.point;
                a.saved.hp = b.hp;
                a.minimum_hp = a.minimum_hp.min(b.hp);
                a.incoming = [b.incoming_swings, b.incoming_hits, b.incoming_damage];
                if b.goal == a.goal {
                    let current = [b.swings, b.hits, b.damage];
                    if current[0] > a.last_native[0] {
                        a.progress = now;
                    }
                    if current.iter().zip(a.last_native).all(|(n, o)| *n >= o) {
                        for k in 0..3 {
                            a.outgoing[k] += current[k] - a.last_native[k];
                        }
                        a.last_native = current;
                    }
                }
            }
            a.observed_at = now;
            if bound
                && b.goal == a.goal
                && a.target.valid()
                && b.outcome == Outcome::Victory
                && a.credited_goal != a.goal
            {
                a.saved.completed += 1;
                a.credited_goal = a.goal;
                a.reason = "target_killed";
            }
            if bound && (b.outcome == Outcome::Defeated || (b.present && b.hp <= 0)) {
                a.saved.desired = Desired::Dead;
                a.saved.hp = 0;
                a.minimum_hp = 0;
                a.reason = "actor_defeated";
            }
            if self.quiescing || a.saved.desired != Desired::Enabled {
                if a.phase != Phase::Resting
                    && a.phase != Phase::Retiring
                    && a.phase != Phase::Dormant
                {
                    a.saved.rest_ms = REST_MS;
                }
                if b.drained {
                    self.claims.retain(|_, c| c.actor != i);
                    a.target = Key::default();
                    a.phase = Phase::Dormant;
                } else if a.phase != Phase::Retiring {
                    a.phase = Phase::Retiring;
                    a.at = now;
                    actions.push(a.action(i, ActionKind::Retire));
                } else if now.saturating_sub(a.at) >= 10_000 {
                    a.reason = "cleanup_stalled";
                }
                continue;
            }
            if a.phase != Phase::Dormant
                && a.phase != Phase::Spawning
                && (!bound || !b.present || b.error || !b.point.inside(LEASH))
            {
                a.saved.desired = Desired::Paused;
                a.saved.failed += 1;
                a.reason = "native_identity_or_bounds_lost";
                a.phase = Phase::Retiring;
                a.at = now;
                actions.push(a.action(i, ActionKind::Retire));
                continue;
            }
            match a.phase {
                Phase::Dormant => {
                    if b.drained {
                        a.phase = Phase::Spawning;
                        a.at = now;
                        actions.push(a.action(i, ActionKind::Spawn));
                    }
                }
                Phase::Spawning => {
                    if b.present && b.lifetime.valid() && b.hp > 0 {
                        a.lifetime = b.lifetime;
                        a.saved.hp = b.hp;
                        a.saved.point = b.point;
                        a.last_point = b.point;
                        a.phase = Phase::Resting;
                        a.at = now;
                        a.observed_at = now;
                        actions.push(a.action(i, ActionKind::Rest));
                    } else if b.error || now.saturating_sub(a.at) >= 10_000 {
                        a.saved.desired = Desired::Paused;
                        a.saved.failed += 1;
                        a.reason = "spawn_refused";
                        a.phase = Phase::Retiring;
                        a.at = now;
                        actions.push(a.action(i, ActionKind::Retire));
                    }
                }
                Phase::Resting => {
                    if b.contested {
                        a.saved.rest_ms = REST_MS;
                        a.reason = "interference";
                    }
                    if a.saved.rest_ms == 0 && b.hp >= 90 && b.sitting && !b.moving && !b.engaged {
                        a.phase = Phase::Searching;
                        a.due = now + (i as u64) * 200;
                        actions.push(a.action(i, ActionKind::Stand));
                    }
                }
                Phase::Searching => {
                    if b.contested {
                        a.saved.yielded += 1;
                        a.reason = "foreign_interference";
                        a.phase = Phase::Releasing;
                        a.at = now;
                        actions.push(a.action(i, ActionKind::Release));
                        continue;
                    }
                    if now < a.due || now < self.probe_at || b.moving || b.engaged {
                        continue;
                    }
                    let mut candidates: Vec<_> =
                        prey.iter().filter(|p| p.eligible(b.point)).collect();
                    candidates.sort_by(|x, y| {
                        x.point
                            .distance(b.point)
                            .total_cmp(&y.point.distance(b.point))
                            .then(x.key.cmp(&y.key))
                    });
                    a.eligible = candidates.len();
                    let mut selected = None;
                    for p in candidates {
                        if a.rejected.contains(&p.key) {
                            continue;
                        }
                        if self.claims.contains_key(&p.key) {
                            a.claim_conflicts += 1;
                            continue;
                        }
                        selected = Some(p);
                        break;
                    }
                    a.due = now + SCAN_MS;
                    if let Some(p) = selected {
                        let Some(goal) = b.goal.checked_add(1) else {
                            a.saved.desired = Desired::Paused;
                            a.reason = "goal_exhausted";
                            continue;
                        };
                        a.goal = goal;
                        a.target = p.key;
                        a.phase = Phase::Probing;
                        a.at = now;
                        self.claims.insert(
                            p.key,
                            Claim {
                                actor: i,
                                lifetime: a.lifetime,
                                goal,
                            },
                        );
                        self.probe_at = now + 200;
                        actions.push(a.action(i, ActionKind::Probe));
                    } else {
                        a.reason = if a.eligible == 0 {
                            "no_target"
                        } else {
                            "claimed_or_unreachable"
                        };
                    }
                }
                Phase::Probing => {
                    if b.contested || (b.probe_ready && !b.target_viable) {
                        a.saved.yielded += 1;
                        a.reason = if b.contested {
                            "target_contested"
                        } else {
                            "target_lost"
                        };
                        a.phase = Phase::Releasing;
                        a.at = now;
                        actions.push(a.action(i, ActionKind::Release));
                    } else if (b.probe_ready && b.probe == a.target && !b.probe_ok)
                        || now.saturating_sub(a.at) >= 5_000
                    {
                        self.claims.remove(&a.target);
                        a.rejected.push(a.target);
                        a.target = Key::default();
                        a.phase = Phase::Searching;
                        a.reason = "probe_refused";
                        a.due = now + 200;
                    } else if b.probe_ready && b.probe == a.target && b.probe_ok {
                        a.phase = Phase::Fighting;
                        a.at = now;
                        a.progress = now;
                        a.last_native = [0; 3];
                        a.last_point = b.point;
                        actions.push(a.action(i, ActionKind::Engage));
                    }
                }
                Phase::Fighting => {
                    if b.swings > a.last_native[0] || b.point.distance(a.last_point) >= 0.0625 {
                        a.progress = now;
                        a.last_point = b.point;
                    }
                    let victory = b.goal == a.goal && b.outcome == Outcome::Victory;
                    let lost = b.contested
                        || b.refused
                        || b.error
                        || (b.goal == a.goal
                            && matches!(b.outcome, Outcome::Lost | Outcome::Paused))
                        || (!b.target_viable && !b.pending_kill && !victory);
                    if victory
                        || lost
                        || now.saturating_sub(a.progress) >= 30_000
                        || now.saturating_sub(a.at) >= 60_000
                    {
                        if !victory {
                            a.saved.yielded += 1;
                            a.reason = if b.contested {
                                "target_contested"
                            } else {
                                "target_lost_or_stalled"
                            };
                        }
                        a.phase = Phase::Releasing;
                        a.at = now;
                        actions.push(a.action(i, ActionKind::Release));
                    }
                }
                Phase::Releasing => {
                    if b.idle && !b.moving {
                        self.claims.remove(&a.target);
                        a.target = Key::default();
                        a.phase = Phase::Resting;
                        a.saved.rest_ms = REST_MS;
                        a.observed_at = now;
                        actions.push(a.action(i, ActionKind::Rest));
                    } else if now.saturating_sub(a.at) >= 10_000 {
                        a.saved.desired = Desired::Paused;
                        a.saved.failed += 1;
                        a.reason = "release_stalled";
                        a.phase = Phase::Retiring;
                        a.at = now;
                        actions.push(a.action(i, ActionKind::Retire));
                    }
                }
                Phase::Retiring => {}
            }
        }
        self.turn = (self.turn + 1) % COUNT;
        actions
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    fn empty() -> [Body; COUNT] {
        [Body {
            drained: true,
            ..Body::default()
        }; COUNT]
    }
    fn setup() -> (Population, [Body; COUNT]) {
        let mut p = Population::new(std::array::from_fn(Saved::initial));
        p.start(None);
        let mut b = empty();
        assert_eq!(p.tick(1, &b, &[], true).len(), 3);
        for (i, x) in b.iter_mut().enumerate() {
            *x = Body {
                lifetime: Lifetime {
                    key: Key {
                        id: 10 + i as u16,
                        incarnation: 100 + i as u64,
                    },
                    epoch: i as u64 + 1,
                    generation: 1,
                },
                runtime_alive: true,
                present: true,
                point: HOME,
                hp: 100,
                idle: true,
                sitting: true,
                target_viable: true,
                ..Body::default()
            };
        }
        p.tick(2, &b, &[], false);
        p.tick(10_002, &b, &[], false);
        (p, b)
    }
    fn rat(id: u16) -> Candidate {
        Candidate {
            key: Key {
                id,
                incarnation: u64::from(id),
            },
            point: Point {
                x: HOME.x + 10.0,
                ..HOME
            },
            type_id: 2011,
            level: 1,
            name: "a_large_rat".into(),
            safe: true,
            idle: true,
        }
    }
    #[test]
    fn claims_are_before_approach_and_fair() {
        let (mut p, b) = setup();
        let prey = [rat(20), rat(21), rat(22)];
        for t in [11_000, 11_200, 11_400] {
            let a = p.tick(t, &b, &prey, false);
            assert!(a.iter().any(|x| x.kind == ActionKind::Probe));
        }
        assert_eq!(p.claims.len(), 3);
        assert!(p.actors.iter().all(|a| a.phase == Phase::Probing));
        let (mut p, b) = setup();
        for t in [11_000, 13_000, 15_000] {
            p.tick(t, &b, &[rat(20)], false);
        }
        assert_eq!(p.claims.len(), 1);
        assert!(p.actors.iter().any(|a| a.claim_conflicts > 0));
    }
    #[test]
    fn no_target_is_healthy_and_filters_are_explicit() {
        let (mut p, b) = setup();
        for t in [11_000, 650_000, 1_000_000] {
            p.tick(t, &b, &[], true);
        }
        assert!(
            p.actors
                .iter()
                .all(|a| a.phase == Phase::Searching && a.saved.failed == 0)
        );
        assert!(prey_profile(2002, 1, "a_gnoll_pup"));
        assert!(!prey_profile(2002, 1, "Fippy_Darkpaw"));
        assert!(!prey_profile(2030, 1, "a_large_rat"));
        let mut r = rat(20);
        r.idle = false;
        assert!(!r.eligible(HOME));
    }
    #[test]
    fn release_holds_claim_until_native_idle_and_reuse_is_distinct() {
        let (mut p, mut b) = setup();
        p.tick(11_000, &b, &[rat(20)], false);
        let i = p.claims.values().next().unwrap().actor;
        b[i].probe = p.actors[i].target;
        b[i].probe_ready = true;
        b[i].probe_ok = true;
        p.tick(11_001, &b, &[], false);
        b[i].goal = 1;
        b[i].idle = false;
        b[i].contested = true;
        assert!(
            p.tick(11_002, &b, &[], false)
                .iter()
                .any(|a| a.actor == i && a.kind == ActionKind::Release)
        );
        assert_eq!(p.claims.len(), 1);
        p.tick(11_003, &b, &[], false);
        assert_eq!(p.claims.len(), 1);
        b[i].idle = true;
        p.tick(11_004, &b, &[], false);
        assert!(p.claims.is_empty());
        let mut reused = rat(20);
        reused.key.incarnation += 1;
        assert_ne!(reused.key, rat(20).key);
    }
    #[test]
    fn clean_restore_keeps_wound_and_rest_and_dead_does_not_spawn() {
        let mut s = std::array::from_fn(Saved::initial);
        s[0].desired = Desired::Enabled;
        s[0].hp = 47;
        s[0].rest_ms = 6000;
        s[1].desired = Desired::Dead;
        s[1].hp = 0;
        let mut p = Population::new(s);
        let a = p.tick(1, &empty(), &[], false);
        assert_eq!(a.len(), 1);
        assert_eq!(a[0].hp, 47);
        assert_eq!(p.actors[0].saved.rest_ms, 6000);
        assert!(!p.start(Some(1)));
    }

    #[test]
    fn target_reuse_and_cancel_do_not_transfer_claim_authority() {
        let (mut p, mut b) = setup();
        p.tick(11_000, &b, &[rat(20)], false);
        let first = *p.claims.get(&rat(20).key).unwrap();
        let mut reused = rat(20);
        reused.key.incarnation += 100;
        p.tick(11_200, &b, &[reused.clone()], false);
        let second = *p.claims.get(&reused.key).unwrap();
        assert_ne!(first.actor, second.actor);
        assert_eq!(p.claims.len(), 2);
        p.stop(Some(first.actor));
        assert!(
            p.tick(11_201, &b, &[], false)
                .iter()
                .any(|a| a.actor == first.actor && a.kind == ActionKind::Retire)
        );
        assert!(p.claims.contains_key(&rat(20).key));
        b[first.actor] = Body {
            drained: true,
            ..Body::default()
        };
        p.tick(11_202, &b, &[], false);
        assert!(!p.claims.contains_key(&rat(20).key));
        assert!(p.claims.contains_key(&reused.key));
    }

    #[test]
    fn watchdogs_defeat_and_stale_lifetime_are_bounded() {
        let (mut p, mut b) = setup();
        p.tick(11_000, &b, &[rat(20)], false);
        let i = p.claims.values().next().unwrap().actor;
        b[i].probe = p.actors[i].target;
        b[i].probe_ready = true;
        b[i].probe_ok = true;
        p.tick(11_001, &b, &[], false);
        b[i].goal = p.actors[i].goal;
        b[i].idle = false;
        assert!(
            p.tick(41_001, &b, &[], false)
                .iter()
                .any(|a| a.actor == i && a.kind == ActionKind::Release)
        );
        assert_eq!(p.actors[i].saved.yielded, 1);
        assert!(p.claims.contains_key(&rat(20).key));
        b[i].outcome = Outcome::Defeated;
        b[i].hp = 0;
        p.tick(41_002, &b, &[], false);
        assert_eq!(p.actors[i].saved.desired, Desired::Dead);
        assert!(!p.start(Some(i)));
        let (mut p, mut b) = setup();
        b[0].lifetime.generation += 1;
        assert!(
            p.tick(11_000, &b, &[], false)
                .iter()
                .any(|a| a.actor == 0 && a.kind == ActionKind::Retire)
        );
        assert_eq!(p.actors[0].saved.desired, Desired::Paused);
    }

    #[test]
    fn rest_requires_sitting_and_has_no_offline_credit() {
        let (mut p, mut b) = setup();
        p.actors[0].phase = Phase::Resting;
        p.actors[0].saved.rest_ms = 6000;
        p.actors[0].saved.hp = 47;
        b[0].hp = 47;
        b[0].sitting = false;
        p.tick(15_000, &b, &[], false);
        assert_eq!(p.actors[0].saved.rest_ms, REST_MS);
        b[0].sitting = true;
        p.tick(20_000, &b, &[], false);
        assert_eq!(p.actors[0].saved.rest_ms, 5000);
        p.drain();
        p.tick(21_000, &b, &[], false);
        p.tick(22_000, &empty(), &[], false);
        p.tick(23_000, &empty(), &[], false);
        let saved = p.saved();
        assert_eq!(saved[0].hp, 47);
        assert_eq!(saved[0].rest_ms, 4000);
        let mut restored = Population::new(saved);
        restored.tick(900_000, &empty(), &[], false);
        let mut fresh = empty();
        fresh[0] = b[0];
        fresh[0].lifetime.epoch += 100;
        restored.tick(900_001, &fresh, &[], false);
        assert_eq!(restored.actors[0].saved.rest_ms, 4000);
        restored.tick(902_001, &fresh, &[], false);
        assert_eq!(restored.actors[0].saved.rest_ms, 2000);
        assert_eq!(restored.actors[0].saved.hp, 47);
    }

    #[test]
    fn native_encounter_refusal_yields_without_replacing_body() {
        let (mut p, mut b) = setup();
        p.tick(11_000, &b, &[rat(20)], false);
        let i = p.claims.values().next().unwrap().actor;
        let lifetime = p.actors[i].lifetime;
        b[i].probe = p.actors[i].target;
        b[i].probe_ready = true;
        b[i].probe_ok = true;
        p.tick(11_001, &b, &[], false);
        b[i].refused = true;
        assert!(
            p.tick(11_002, &b, &[], false)
                .iter()
                .any(|a| a.actor == i && a.kind == ActionKind::Release)
        );
        assert_eq!(p.actors[i].saved.desired, Desired::Enabled);
        assert_eq!(p.actors[i].lifetime, lifetime);
        b[i].refused = false;
        b[i].idle = true;
        assert!(
            p.tick(11_003, &b, &[], false)
                .iter()
                .any(|a| a.actor == i && a.kind == ActionKind::Rest)
        );
        assert_eq!(p.actors[i].saved.failed, 0);
        assert!(p.claims.is_empty());
    }
}

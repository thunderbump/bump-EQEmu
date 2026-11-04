//! Bounded, zone-thread population policy. Native facts are values; actions are
//! requests, never acknowledgements. Checkpoint I/O belongs to the bridge.
use serde::{Deserialize, Serialize};
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

#[derive(Clone, Copy, Debug, Default, PartialEq, Serialize, Deserialize)]
#[serde(deny_unknown_fields)]
pub struct Point {
    pub x: f64,
    pub y: f64,
    pub z: f64,
}
impl Point {
    pub fn distance_xy_squared(self, p: Self) -> f64 {
        (self.x - p.x).powi(2) + (self.y - p.y).powi(2)
    }
    pub fn valid(self) -> bool {
        [self.x, self.y, self.z]
            .iter()
            .all(|v| v.is_finite() && v.abs() <= 1_000_000.0)
    }
    pub fn allowed(self, level: u8) -> bool {
        if level == 1 {
            self.inside(LEASH)
        } else {
            self.valid()
        }
    }
    pub fn inside(self, radius: f64) -> bool {
        self.x.is_finite()
            && self.y.is_finite()
            && self.z.is_finite()
            && self.distance_xy_squared(HOME) <= radius * radius
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

const PREY: &[(u32, u8, &str)] = &[
    (2011, 1, "a_large_rat"),
    (2104, 1, "a_decaying_skeleton"),
    (2000, 2, "a_decaying_skeleton"),
    (2002, 1, "a_gnoll_pup"),
    (2003, 1, "a_grass_snake"),
    (2017, 1, "a_brown_myotis_bat"),
    (2101, 1, "a_gopher_snake"),
    (2014, 2, "a_klicnik_worker"),
    (2021, 2, "a_klicnik_drone"),
    (2024, 2, "a_fire_beetle"),
];
pub fn prey_profile(id: u32, level: u8, name: &str) -> bool {
    PREY.contains(&(id, level, name))
}
pub fn prey_type(id: u32, level: u8) -> bool {
    PREY.iter().any(|p| (p.0, p.1) == (id, level))
}
// The starter profile is an explicit exception to faction hostility. Beyond it,
// suitable prey is within two levels and has a native player-hostile reaction.
pub fn hunting_bounds(actor_level: u8) -> (u8, u8) {
    (
        actor_level.saturating_sub(2).max(1),
        actor_level.saturating_add(2),
    )
}
pub fn appropriate_hostile(actor_level: u8, target_level: u8, hostile: bool) -> bool {
    let (minimum, maximum) = hunting_bounds(actor_level);
    actor_level > 1 && hostile && target_level >= minimum && target_level <= maximum
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
    pub hostile_mask: u8,
}
impl Candidate {
    pub fn eligible(&self, actor: Point, level: u8, index: usize) -> bool {
        self.key.valid()
            && self.safe
            && self.idle
            && if level == 1 {
                self.point.inside(SEARCH)
            } else {
                self.point.valid() && self.point.distance_xy_squared(actor) <= SEARCH * SEARCH
            }
            && (self.point.z - actor.z).abs() <= Z_RANGE
            && if level == 1 {
                prey_profile(self.type_id, self.level, &self.name)
            } else {
                appropriate_hostile(level, self.level, self.hostile_mask & (1 << index) != 0)
            }
    }
}
#[derive(Clone, Copy, Debug, PartialEq, Eq, Serialize, Deserialize)]
#[serde(rename_all = "snake_case")]
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
#[derive(Clone, Copy, Debug, Default, PartialEq, Serialize, Deserialize)]
#[serde(deny_unknown_fields)]
pub struct Route {
    pub id: u32,
    pub zone: u32,
    pub exit: Point,
    pub arrival: Point,
    pub distance: f64,
}
impl Route {
    pub fn valid(self) -> bool {
        self.zone > 0
            && self.exit.valid()
            && self.arrival.valid()
            && self.distance.is_finite()
            && self.distance >= 0.0
    }
}

#[derive(Clone, Debug, Serialize, Deserialize)]
#[serde(deny_unknown_fields)]
pub struct Saved {
    pub desired: Desired,
    pub zone: u32,
    pub previous_zone: u32,
    pub owner_epoch: u64,
    pub transfer: Option<Route>,
    pub point: Point,
    pub hp: i64,
    pub max_hp: i64,
    pub rest_ms: u64,
    pub level: u8,
    pub experience: u64,
    pub reputation: BTreeMap<i32, i32>,
    pub inventory: Option<Vec<crate::inventory::Item>>,
    pub completed: u64,
    pub yielded: u64,
    pub failed: u64,
}
impl Saved {
    pub fn initial(index: usize) -> Self {
        Self {
            desired: Desired::Paused,
            zone: 2,
            previous_zone: 0,
            owner_epoch: 0,
            transfer: None,
            point: Point {
                x: HOME.x + [0.0, 3.0, -3.0][index],
                ..HOME
            },
            hp: 100,
            max_hp: 100,
            rest_ms: REST_MS,
            level: 1,
            experience: 0,
            reputation: BTreeMap::new(),
            inventory: None,
            completed: 0,
            yielded: 0,
            failed: 0,
        }
    }
    pub fn valid(&self) -> bool {
        self.zone > 0
            && self
                .transfer
                .is_none_or(|route| self.level > 1 && route.valid() && route.zone != self.zone)
            && self
                .inventory
                .as_ref()
                .is_none_or(|items| crate::inventory::valid(items))
            && self.reputation.len() <= 4096
            && self
                .reputation
                .iter()
                .all(|(id, value)| *id > 0 && i16::try_from(*value).is_ok())
            && self.max_hp > 0
            && (1..=127).contains(&self.level)
            && self.point.allowed(self.level)
            && (self.level != 1 || self.zone == 2)
            && self.rest_ms <= REST_MS
            && match self.desired {
                Desired::Dead => self.hp == 0,
                _ => self.max_hp > 0 && (1..=self.max_hp).contains(&self.hp),
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
pub struct FactionChange {
    pub id: i32,
    pub before: i32,
    pub after: i32,
}

#[derive(Clone, Debug, Default)]
pub struct Body {
    pub lifetime: Lifetime,
    pub runtime_alive: bool,
    pub present: bool,
    pub drained: bool,
    pub point: Point,
    pub hp: i64,
    pub max_hp: i64,
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
    pub kill_experience: u64,
    pub factions: [FactionChange; 20],
    pub level: u8,
    pub level_cap: u8,
    pub next_level_at: u64,
    pub experience_cap: u64,
    pub inventory: Option<Vec<crate::inventory::Item>>,
    pub routes: Vec<Route>,
    pub travel_complete: bool,
    pub travel_failed: bool,
    pub destination_ready: bool,
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
    Travelling,
    PreparingTransfer,
    TransferDrain,
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
            Self::Travelling => "travelling",
            Self::PreparingTransfer => "preparing_transfer",
            Self::TransferDrain => "transfer_drain",
        }
    }
}
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum ActionKind {
    Spawn,
    LevelUp,
    Travel,
    PrepareTransfer,
    BootDestination,
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
    pub level: u8,
    pub owner_epoch: u64,
    pub route: Option<Route>,
}
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct Claim {
    pub actor: usize,
    pub lifetime: Lifetime,
    pub goal: u64,
}

#[derive(Clone, Copy, Debug, Default, PartialEq, Eq)]
pub struct CombatCounters {
    pub swings: u64,
    pub hits: u64,
    pub damage: u64,
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
    pub outgoing: CombatCounters,
    pub incoming: CombatCounters,
    pub minimum_hp: i64,
    pub rest_hp_gain: u64,
    at: u64,
    progress: u64,
    due: u64,
    observed_at: u64,
    last_point: Point,
    last_native: CombatCounters,
    credited_goal: u64,
    admitted_body: bool,
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
            outgoing: CombatCounters::default(),
            incoming: CombatCounters::default(),
            minimum_hp: hp,
            rest_hp_gain: 0,
            at: 0,
            progress: 0,
            due: 0,
            observed_at: 0,
            last_point: Point::default(),
            last_native: CombatCounters::default(),
            credited_goal: 0,
            admitted_body: false,
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
            point: if kind == ActionKind::Travel {
                self.saved
                    .transfer
                    .map(|r| r.exit)
                    .unwrap_or(self.saved.point)
            } else {
                self.saved.point
            },
            hp: self.saved.hp,
            level: self.saved.level,
            owner_epoch: self.saved.owner_epoch,
            route: self.saved.transfer,
        }
    }
}
pub struct Population {
    pub actors: [Actor; COUNT],
    pub claims: BTreeMap<Key, Claim>,
    pub quiescing: bool,
    pub zone: u32,
    pub owned: u8,
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
            zone: 2,
            owned: 7,
            scan_at: 0,
            probe_at: 0,
            turn: 0,
        }
    }
    pub fn adopt(&mut self, index: usize, saved: Saved) -> bool {
        if index >= COUNT || self.actors[index].phase != Phase::Dormant {
            return false;
        }
        self.claims.retain(|_, claim| claim.actor != index);
        self.actors[index] = Actor::new(saved);
        true
    }
    pub fn needs_scan(&self, now: u64) -> bool {
        !self.quiescing
            && now >= self.scan_at
            && self.actors.iter().enumerate().any(|(i, a)| {
                self.owned & (1 << i) != 0
                    && a.saved.zone == self.zone
                    && a.saved.desired == Desired::Enabled
            })
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
            if self.owned & (1 << i) != 0
                && index.is_none_or(|v| v == i)
                && self.actors[i].saved.desired != Desired::Dead
            {
                self.actors[i].saved.desired = Desired::Enabled;
                changed = true;
            }
        }
        changed
    }
    pub fn stop(&mut self, index: Option<usize>) {
        for i in 0..COUNT {
            if self.owned & (1 << i) != 0
                && index.is_none_or(|v| v == i)
                && self.actors[i].saved.desired != Desired::Dead
            {
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
            if self.owned & (1 << i) == 0 || a.saved.zone != self.zone {
                continue;
            }
            let b = &bodies[i];
            if a.phase == Phase::Spawning
                && b.lifetime.valid()
                && (b.present || b.outcome == Outcome::Defeated)
            {
                a.lifetime = b.lifetime;
            }
            let bound = a.lifetime.valid() && a.lifetime == b.lifetime;
            // HP remains truthful even when bad geometry makes the body unsafe
            // to continue. Keep the last validated position, never its old HP.
            let max_hp = if b.max_hp > 0 { b.max_hp } else { 100 };
            if bound
                && b.present
                && b.hp > 0
                && b.hp <= max_hp
                && (a.admitted_body || (a.phase == Phase::Spawning && !b.error))
            {
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
                if b.point.allowed(a.saved.level) {
                    a.saved.point = b.point;
                }
                if !b.error
                    && let Some(items) = &b.inventory
                {
                    a.saved.inventory = Some(items.clone());
                }
                a.saved.hp = b.hp;
                a.saved.max_hp = max_hp;
                a.minimum_hp = a.minimum_hp.min(b.hp);
            }
            // Counters/terminals remain attributable after native body removal,
            // including the fatal incoming swing and a kill racing retirement.
            if bound {
                a.incoming = CombatCounters {
                    swings: b.incoming_swings,
                    hits: b.incoming_hits,
                    damage: b.incoming_damage,
                };
                if b.goal == a.goal {
                    let current = CombatCounters {
                        swings: b.swings,
                        hits: b.hits,
                        damage: b.damage,
                    };
                    if current.swings > a.last_native.swings {
                        a.progress = now;
                    }
                    if current.swings >= a.last_native.swings
                        && current.hits >= a.last_native.hits
                        && current.damage >= a.last_native.damage
                    {
                        a.outgoing.swings += current.swings - a.last_native.swings;
                        a.outgoing.hits += current.hits - a.last_native.hits;
                        a.outgoing.damage += current.damage - a.last_native.damage;
                        a.last_native = current;
                    }
                }
            }
            a.observed_at = now;
            if bound
                && b.goal == a.goal
                && a.target.valid()
                && b.outcome == Outcome::Victory
                && !b.error
                && a.credited_goal != a.goal
            {
                let mut reputation = a.saved.reputation.clone();
                let consistent = b.factions.iter().filter(|f| f.id != 0).all(|f| {
                    if f.id < 0
                        || i16::try_from(f.after).is_err()
                        || reputation.get(&f.id).copied().unwrap_or(0) != f.before
                    {
                        return false;
                    }
                    reputation.insert(f.id, f.after);
                    true
                });
                if !consistent || reputation.len() > 4096 {
                    a.saved.desired = Desired::Paused;
                    a.saved.failed += 1;
                    a.reason = "faction_credit_mismatch";
                    a.phase = Phase::Retiring;
                    actions.push(a.action(i, ActionKind::Retire));
                    continue;
                }
                let Some(experience) = a.saved.experience.checked_add(b.kill_experience) else {
                    a.saved.desired = Desired::Paused;
                    a.saved.failed += 1;
                    a.reason = "experience_exhausted";
                    a.phase = Phase::Retiring;
                    actions.push(a.action(i, ActionKind::Retire));
                    continue;
                };
                a.saved.experience = if b.experience_cap > 0 {
                    experience.min(b.experience_cap)
                } else {
                    experience
                };
                a.saved.reputation = reputation;
                a.saved.completed += 1;
                a.credited_goal = a.goal;
                a.reason = "target_killed";
            }
            if bound
                && (a.admitted_body || !b.error)
                && (b.outcome == Outcome::Defeated || (b.present && b.hp <= 0))
            {
                a.saved.desired = Desired::Dead;
                a.saved.hp = 0;
                a.minimum_hp = 0;
                a.reason = "actor_defeated";
            }
            if self.quiescing || a.saved.desired != Desired::Enabled {
                a.saved.transfer = None;
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
                && a.phase != Phase::TransferDrain
                && (!bound || !b.present || b.error || !b.point.allowed(a.saved.level))
            {
                a.saved.desired = Desired::Paused;
                a.saved.failed += 1;
                a.reason = "native_identity_or_bounds_lost";
                a.phase = Phase::Retiring;
                a.at = now;
                actions.push(a.action(i, ActionKind::Retire));
                continue;
            }
            if bound && b.present && b.level != 0 && b.level != a.saved.level {
                a.saved.desired = Desired::Paused;
                a.saved.failed += 1;
                a.reason = "native_level_mismatch";
                a.phase = Phase::Retiring;
                actions.push(a.action(i, ActionKind::Retire));
                continue;
            }
            // Native projects the configured threshold; Rust owns progression.
            // Wait for the acknowledged native level before requesting another.
            if bound
                && b.present
                && b.level == a.saved.level
                && b.level < b.level_cap
                && b.next_level_at > 0
                && a.saved.experience >= b.next_level_at
            {
                a.saved.level += 1;
                actions.push(a.action(i, ActionKind::LevelUp));
                continue;
            }
            match a.phase {
                Phase::Dormant => {
                    if b.drained {
                        a.phase = Phase::Spawning;
                        a.admitted_body = false;
                        a.at = now;
                        actions.push(a.action(i, ActionKind::Spawn));
                    }
                }
                Phase::Spawning => {
                    if b.present && b.lifetime.valid() && b.hp > 0 && !b.error {
                        a.admitted_body = true;
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
                    if a.saved.rest_ms == 0
                        && b.hp >= max_hp - max_hp / 10
                        && b.sitting
                        && !b.moving
                        && !b.engaged
                    {
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
                    let mut candidates: Vec<_> = prey
                        .iter()
                        .filter(|p| p.eligible(b.point, a.saved.level, i))
                        .collect();
                    candidates.sort_by(|x, y| {
                        x.point
                            .distance_xy_squared(b.point)
                            .total_cmp(&y.point.distance_xy_squared(b.point))
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
                        if a.saved.level > 1 {
                            let route = b
                                .routes
                                .iter()
                                .filter(|r| r.valid() && r.zone != self.zone)
                                .min_by(|x, y| {
                                    (x.zone == a.saved.previous_zone)
                                        .cmp(&(y.zone == a.saved.previous_zone))
                                        .then(x.distance.total_cmp(&y.distance))
                                        .then(x.zone.cmp(&y.zone))
                                        .then(x.id.cmp(&y.id))
                                })
                                .copied();
                            if let Some(route) = route {
                                a.saved.transfer = Some(route);
                                a.phase = Phase::Travelling;
                                a.at = now;
                                a.reason = "connected_zone_target";
                                actions.push(a.action(i, ActionKind::Travel));
                            }
                        }
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
                        a.last_native = CombatCounters::default();
                        a.last_point = b.point;
                        actions.push(a.action(i, ActionKind::Engage));
                    }
                }
                Phase::Fighting => {
                    if b.swings > a.last_native.swings
                        || b.point.distance_xy_squared(a.last_point) >= 0.0625
                    {
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
                    } else {
                        // Cancellation has a later native terminal. Repeat
                        // release until that terminal permits goal reset.
                        actions.push(a.action(i, ActionKind::Release));
                    }
                }
                Phase::Travelling => {
                    if b.travel_failed
                        || b.refused
                        || b.contested
                        || now.saturating_sub(a.at) > 120_000
                    {
                        a.saved.desired = Desired::Paused;
                        a.saved.failed += 1;
                        a.reason = "zone_travel_failed";
                        a.phase = Phase::Retiring;
                        actions.push(a.action(i, ActionKind::Retire));
                    } else if b.travel_complete {
                        if let Some(previous) = a.saved.transfer
                            && let Some(current) = b.routes.iter().find(|r| {
                                r.id == previous.id && r.zone == previous.zone && r.valid()
                            })
                        {
                            a.saved.transfer = Some(*current);
                        }
                        a.phase = Phase::PreparingTransfer;
                        a.at = now;
                        a.due = now + 2000;
                        actions.push(a.action(i, ActionKind::PrepareTransfer));
                    }
                }
                Phase::PreparingTransfer => {
                    if let Some(previous) = a.saved.transfer
                        && let Some(current) = b
                            .routes
                            .iter()
                            .find(|r| r.id == previous.id && r.zone == previous.zone && r.valid())
                    {
                        a.saved.transfer = Some(*current);
                    }
                    if b.refused || b.contested {
                        a.saved.desired = Desired::Paused;
                        a.saved.failed += 1;
                        a.reason = "zone_connection_changed";
                        a.phase = Phase::Retiring;
                        actions.push(a.action(i, ActionKind::Retire));
                    } else if b.destination_ready {
                        a.phase = Phase::TransferDrain;
                        a.at = now;
                        actions.push(a.action(i, ActionKind::Retire));
                    } else if now >= a.due {
                        a.due = now + 2000;
                        a.reason = "waiting_destination";
                        actions.push(a.action(i, ActionKind::PrepareTransfer));
                    }
                }
                Phase::TransferDrain => {
                    if b.drained {
                        if let Some(route) = a.saved.transfer.take() {
                            a.saved.previous_zone = self.zone;
                            a.saved.zone = route.zone;
                            a.saved.point = route.arrival;
                            a.saved.rest_ms = REST_MS;
                            a.phase = Phase::Dormant;
                            a.lifetime = Lifetime::default();
                            a.target = Key::default();
                            a.reason = "destination_published";
                            let mut action = a.action(i, ActionKind::BootDestination);
                            action.route = Some(route);
                            actions.push(action);
                        }
                    } else if now.saturating_sub(a.at) >= 10_000 {
                        a.reason = "transfer_drain_stalled";
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
        std::array::from_fn(|_| Body {
            drained: true,
            ..Body::default()
        })
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
            hostile_mask: 0,
        }
    }
    #[test]
    fn failed_new_body_cannot_clamp_saved_wound_before_equipment_restore() {
        let mut saved = std::array::from_fn(Saved::initial);
        saved[0].desired = Desired::Enabled;
        saved[0].hp = 120;
        saved[0].max_hp = 200;
        let mut p = Population::new(saved);
        p.tick(1, &empty(), &[], false);
        let mut b = empty();
        b[0] = Body {
            lifetime: Lifetime {
                key: Key {
                    id: 10,
                    incarnation: 10,
                },
                epoch: 1,
                generation: 1,
            },
            present: true,
            runtime_alive: true,
            point: HOME,
            hp: 100,
            max_hp: 100,
            error: true,
            ..Body::default()
        };
        p.tick(2, &b, &[], false);
        p.tick(3, &b, &[], false);
        assert_eq!(p.actors[0].saved.hp, 120);
        assert_eq!(p.actors[0].saved.max_hp, 200);
        assert_eq!(p.actors[0].saved.desired, Desired::Paused);
    }

    #[test]
    fn native_restore_error_cannot_replace_saved_inventory_with_partial_items() {
        let (mut p, mut b) = setup();
        let item = crate::inventory::Item {
            id: 1001,
            parent: -1,
            kind: 0,
            slot: 2,
            ..crate::inventory::Item::default()
        };
        p.actors[0].saved.inventory = Some(vec![item.clone()]);
        b[0].inventory = Some(Vec::new());
        b[0].error = true;
        p.tick(11_000, &b, &[], false);
        assert_eq!(p.actors[0].saved.inventory, Some(vec![item]));
        assert_eq!(p.actors[0].saved.desired, Desired::Paused);
    }

    #[test]
    fn rejected_local_prey_does_not_starve_a_reachable_connection() {
        let (mut p, mut b) = setup();
        p.actors[0].saved.level = 2;
        p.actors[0].saved.experience = 1000;
        for body in &mut b {
            body.level = 1;
            body.max_hp = 100;
            body.sitting = false; // Acknowledge setup's Stand actions.
        }
        b[0].level = 2;
        b[0].level_cap = 127;
        b[0].next_level_at = 8000;
        let mut target = rat(20);
        target.hostile_mask = 1;
        b[0].routes = vec![Route {
            id: 4,
            zone: 3,
            exit: HOME,
            arrival: HOME,
            distance: 10.0,
        }];
        p.tick(11_000, &b, &[target.clone()], false);
        assert_eq!(p.actors[0].phase, Phase::Probing);
        b[0].probe = target.key;
        b[0].probe_ready = true;
        b[0].probe_ok = false;
        p.tick(11_001, &b, &[], false);
        assert_eq!(p.actors[0].phase, Phase::Searching);
        assert!(p.actors[0].rejected.contains(&target.key));
        assert!(!p.claims.contains_key(&target.key));

        // Another starter hunter can probe this rat after actor 0 releases it.
        // That consumes the shared stagger, not actor 0's rejection history.
        let staggered = p.tick(11_202, &b, &[target.clone()], false);
        assert!(
            staggered
                .iter()
                .any(|a| a.actor != 0 && a.kind == ActionKind::Probe && a.target == target.key)
        );
        assert_eq!(p.actors[0].phase, Phase::Searching);
        assert!(p.actors[0].rejected.contains(&target.key));
        assert_eq!(p.probe_at, 11_402);
        let waiting = p.tick(11_401, &b, &[target.clone()], false);
        assert!(!waiting.iter().any(|a| a.actor == 0));
        assert_eq!(p.actors[0].phase, Phase::Searching);

        let actions = p.tick(11_402, &b, &[target], false);
        assert!(
            actions
                .iter()
                .any(|a| a.actor == 0 && a.kind == ActionKind::Travel)
        );
        assert_eq!(p.actors[0].phase, Phase::Travelling);
        assert_eq!(p.actors[0].eligible, 1);
        assert_eq!(p.actors[0].saved.transfer.unwrap().id, 4);
    }

    #[test]
    fn preserve_coordinate_arrival_is_refreshed_from_native_exit_position() {
        let (mut p, mut b) = setup();
        p.actors[0].saved.level = 2;
        let original = Route {
            id: 4,
            zone: 3,
            exit: HOME,
            arrival: HOME,
            distance: 10.0,
        };
        p.actors[0].saved.transfer = Some(original);
        p.actors[0].phase = Phase::Travelling;
        let actual = Route {
            arrival: Point {
                x: HOME.x + 100.0,
                ..HOME
            },
            ..original
        };
        b[0].routes = vec![actual];
        b[0].travel_complete = true;
        p.tick(11_000, &b, &[], false);
        assert_eq!(p.actors[0].saved.transfer.unwrap().arrival, actual.arrival);
        b[0].destination_ready = true;
        p.tick(11_100, &b, &[], false);
        b[0].present = false;
        b[0].drained = true;
        p.tick(11_200, &b, &[], false);
        assert_eq!(p.actors[0].saved.point, actual.arrival);
        assert_eq!(p.actors[0].saved.zone, 3);
    }

    #[test]
    fn transfer_waits_for_arrival_readiness_and_native_absence() {
        let (mut p, mut b) = setup();
        p.actors[0].saved.level = 2;
        p.actors[0].saved.experience = 12345;
        p.actors[0].saved.owner_epoch = 7;
        p.actors[0].saved.inventory = Some(Vec::new());
        b[0].routes = vec![Route {
            id: 4,
            zone: 3,
            exit: Point {
                x: HOME.x + 300.0,
                ..HOME
            },
            arrival: Point {
                x: 10.0,
                y: 20.0,
                z: 4.0,
            },
            distance: 310.0,
        }];
        let actions = p.tick(11_000, &b, &[], true);
        assert!(
            actions
                .iter()
                .any(|a| a.actor == 0 && a.kind == ActionKind::Travel)
        );
        assert_eq!(p.actors[0].saved.zone, 2);
        b[0].point.x += 300.0;
        b[0].travel_complete = true;
        assert!(
            p.tick(11_100, &b, &[], false)
                .iter()
                .any(|a| a.actor == 0 && a.kind == ActionKind::PrepareTransfer)
        );
        assert!(
            !p.tick(11_200, &b, &[], false)
                .iter()
                .any(|a| a.actor == 0 && a.kind == ActionKind::Retire)
        );
        b[0].destination_ready = true;
        assert!(
            p.tick(11_300, &b, &[], false)
                .iter()
                .any(|a| a.actor == 0 && a.kind == ActionKind::Retire)
        );
        assert_eq!(p.actors[0].saved.zone, 2);
        p.tick(11_400, &b, &[], false);
        assert_eq!(p.actors[0].saved.zone, 2);
        b[0].present = false;
        b[0].drained = true;
        let actions = p.tick(11_500, &b, &[], false);
        assert_eq!(p.actors[0].saved.zone, 3);
        assert_eq!(p.actors[0].saved.previous_zone, 2);
        assert_eq!(p.actors[0].saved.experience, 12345);
        assert_eq!(p.actors[0].saved.inventory, Some(Vec::new()));
        assert!(p.actors[0].saved.transfer.is_none());
        assert!(
            actions
                .iter()
                .any(|a| a.actor == 0 && a.kind == ActionKind::BootDestination)
        );
        assert!(
            !p.tick(12_000, &b, &[], false)
                .iter()
                .any(|a| a.actor == 0 && a.kind == ActionKind::Spawn)
        );
    }

    #[test]
    fn missing_destination_retries_without_retiring_or_moving_identity() {
        let (mut p, mut b) = setup();
        p.actors[0].saved.level = 2;
        p.actors[0].saved.transfer = Some(Route {
            id: 1,
            zone: 3,
            exit: HOME,
            arrival: HOME,
            distance: 1.0,
        });
        p.actors[0].phase = Phase::PreparingTransfer;
        for now in [11_000, 20_000, 200_000] {
            let actions = p.tick(now, &b, &[], false);
            assert!(
                actions
                    .iter()
                    .any(|a| a.actor == 0 && a.kind == ActionKind::PrepareTransfer)
            );
            assert!(
                !actions
                    .iter()
                    .any(|a| a.actor == 0 && a.kind == ActionKind::Retire)
            );
            assert_eq!(p.actors[0].saved.zone, 2);
        }
        p.stop(Some(0));
        assert!(
            p.tick(200_001, &b, &[], false)
                .iter()
                .any(|a| a.actor == 0 && a.kind == ActionKind::Retire)
        );
        b[0] = Body {
            drained: true,
            ..Body::default()
        };
        p.tick(200_002, &b, &[], false);
        assert_eq!(p.actors[0].saved.zone, 2);
        assert!(p.actors[0].saved.transfer.is_none());
    }

    #[test]
    fn progressed_hunters_require_actor_specific_hostility_and_deterministic_nearest() {
        let (mut p, b) = setup();
        for a in &mut p.actors {
            a.saved.level = 2;
        }
        let mut friendly = rat(20);
        friendly.type_id = 9999;
        friendly.point.x = HOME.x + 1.0;
        let mut hostile = friendly.clone();
        hostile.key = Key {
            id: 22,
            incarnation: 22,
        };
        hostile.point.x = HOME.x + 10.0;
        hostile.hostile_mask = 1;
        let mut tie = hostile.clone();
        tie.key = Key {
            id: 21,
            incarnation: 21,
        };
        let mut high = hostile.clone();
        high.key = Key {
            id: 23,
            incarnation: 23,
        };
        high.level = 5;
        let actions = p.tick(11_000, &b, &[hostile, friendly, high, tie], true);
        let probes: Vec<_> = actions
            .iter()
            .filter(|a| a.kind == ActionKind::Probe)
            .collect();
        assert_eq!(probes.len(), 1);
        assert_eq!(probes[0].actor, 0);
        assert_eq!(probes[0].target.id, 21);
        assert_eq!(p.claims.len(), 1);
        assert!(!appropriate_hostile(2, 1, false));
        assert!(appropriate_hostile(2, 4, true));
        assert!(!appropriate_hostile(2, 5, true));
        assert!(!appropriate_hostile(1, 1, true));
    }

    #[test]
    fn inconsistent_faction_credit_cannot_partially_award_xp_or_reputation() {
        let (mut p, mut b) = setup();
        p.tick(11_000, &b, &[rat(20)], false);
        let i = p.claims.values().next().unwrap().actor;
        b[i].goal = p.actors[i].goal;
        b[i].outcome = Outcome::Victory;
        b[i].kill_experience = 275;
        b[i].factions[0] = FactionChange {
            id: 203,
            before: 0,
            after: -10,
        };
        b[i].factions[1] = FactionChange {
            id: 204,
            before: 30,
            after: 20,
        };
        p.tick(11_001, &b, &[], false);
        assert_eq!(p.actors[i].saved.desired, Desired::Paused);
        assert_eq!(p.actors[i].saved.experience, 0);
        assert!(p.actors[i].saved.reputation.is_empty());
        assert_eq!(p.actors[i].saved.completed, 0);
    }

    #[test]
    fn levels_require_native_threshold_and_acknowledgement_and_respect_cap() {
        let (mut p, mut b) = setup();
        p.actors[0].saved.experience = 8000;
        b[0].level = 1;
        b[0].level_cap = 3;
        b[0].next_level_at = 1000;
        let actions = p.tick(11_000, &b, &[], false);
        assert!(
            actions
                .iter()
                .any(|a| a.actor == 0 && a.kind == ActionKind::LevelUp && a.level == 2)
        );
        b[0].level = 2;
        b[0].next_level_at = 8000;
        let actions = p.tick(11_001, &b, &[], false);
        assert!(
            actions
                .iter()
                .any(|a| a.actor == 0 && a.kind == ActionKind::LevelUp && a.level == 3)
        );
        b[0].level = 3;
        b[0].next_level_at = 27000;
        p.actors[0].saved.experience = 50000;
        assert!(
            !p.tick(11_002, &b, &[], false)
                .iter()
                .any(|a| a.kind == ActionKind::LevelUp)
        );
        assert_eq!(p.actors[0].saved.level, 3);
        assert_eq!(p.saved()[0].level, 3);
    }

    #[test]
    fn refused_native_level_cannot_start_another_hunt() {
        let (mut p, mut b) = setup();
        p.actors[0].saved.experience = 1000;
        b[0].level = 1;
        b[0].level_cap = 65;
        b[0].next_level_at = 1000;
        p.tick(11_000, &b, &[], false);
        let actions = p.tick(11_001, &b, &[rat(20)], false);
        assert_eq!(p.actors[0].saved.desired, Desired::Paused);
        assert!(
            actions
                .iter()
                .any(|a| a.actor == 0 && a.kind == ActionKind::Retire)
        );
        assert!(
            !actions
                .iter()
                .any(|a| a.actor == 0 && a.kind == ActionKind::Probe)
        );
    }

    #[test]
    fn actual_kill_credit_is_once_and_bound_to_lifetime_and_goal() {
        for stale in [0, 1, 2] {
            let (mut p, mut b) = setup();
            p.tick(11_000, &b, &[rat(20)], false);
            let i = p.claims.values().next().unwrap().actor;
            b[i].probe = p.actors[i].target;
            b[i].probe_ready = true;
            b[i].probe_ok = true;
            p.tick(11_001, &b, &[], false);
            b[i].goal = p.actors[i].goal;
            b[i].outcome = Outcome::Victory;
            b[i].kill_experience = 275;
            b[i].factions[0] = FactionChange {
                id: 203,
                before: 0,
                after: -10,
            };
            if stale == 1 {
                b[i].lifetime.generation += 1;
            }
            if stale == 2 {
                b[i].goal += 1;
            }
            p.tick(11_002, &b, &[], false);
            p.tick(11_003, &b, &[], false);
            assert_eq!(
                p.actors[i].saved.experience,
                if stale == 0 { 275 } else { 0 }
            );
            assert_eq!(p.actors[i].saved.completed, if stale == 0 { 1 } else { 0 });
            assert_eq!(
                p.actors[i].saved.reputation.get(&203).copied(),
                if stale == 0 { Some(-10) } else { None }
            );
        }
    }

    #[test]
    fn experience_overflow_retires_without_wrapping_or_crediting() {
        let (mut p, mut b) = setup();
        p.tick(11_000, &b, &[rat(20)], false);
        let i = p.claims.values().next().unwrap().actor;
        p.actors[i].saved.experience = u64::MAX;
        b[i].goal = p.actors[i].goal;
        b[i].outcome = Outcome::Victory;
        b[i].kill_experience = 1;
        let actions = p.tick(11_001, &b, &[], false);
        assert!(
            actions
                .iter()
                .any(|a| a.actor == i && a.kind == ActionKind::Retire)
        );
        assert_eq!(p.actors[i].saved.experience, u64::MAX);
        assert_eq!(p.actors[i].saved.completed, 0);
        assert_eq!(p.actors[i].saved.desired, Desired::Paused);
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
        assert!(!r.eligible(HOME, 1, 0));
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
        b[i].present = false;
        b[i].incoming_swings = 9;
        b[i].incoming_hits = 6;
        b[i].incoming_damage = 100;
        p.tick(41_002, &b, &[], false);
        assert_eq!(p.actors[i].saved.desired, Desired::Dead);
        assert_eq!(
            p.actors[i].incoming,
            CombatCounters {
                swings: 9,
                hits: 6,
                damage: 100
            }
        );
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
        fresh[0] = b[0].clone();
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
    #[test]
    fn invalid_geometry_cannot_restore_stale_higher_hp() {
        for x in [HOME.x + LEASH + 1.0, f64::NAN] {
            let (mut p, mut b) = setup();
            b[0].point.x = x;
            b[0].hp = 47;
            p.tick(11_000, &b, &[], false);
            assert_eq!(p.actors[0].saved.desired, Desired::Paused);
            p.tick(11_001, &empty(), &[], false);
            let saved = p.saved();
            assert_eq!(saved[0].hp, 47);
            assert_eq!(saved[0].point, HOME);
            let mut restored = Population::new(saved);
            restored.start(Some(0));
            let actions = restored.tick(1, &empty(), &[], false);
            assert!(
                actions
                    .iter()
                    .any(|a| a.actor == 0 && a.kind == ActionKind::Spawn && a.hp == 47)
            );
        }
    }

    #[test]
    fn watchdog_repeats_release_until_native_terminal_has_reset() {
        let (mut p, mut b) = setup();
        p.tick(11_000, &b, &[rat(20)], false);
        let i = p.claims.values().next().unwrap().actor;
        b[i].probe = p.actors[i].target;
        b[i].probe_ready = true;
        b[i].probe_ok = true;
        p.tick(11_001, &b, &[], false);
        b[i].idle = false;
        for now in [71_001, 71_002] {
            assert!(
                p.tick(now, &b, &[], false)
                    .iter()
                    .any(|a| a.actor == i && a.kind == ActionKind::Release)
            );
            assert!(p.claims.contains_key(&rat(20).key));
        }
        b[i].idle = true;
        p.tick(71_003, &b, &[], false);
        assert_eq!(p.actors[i].phase, Phase::Resting);
        assert_eq!(p.actors[i].saved.desired, Desired::Enabled);
        assert!(p.claims.is_empty());
    }
}

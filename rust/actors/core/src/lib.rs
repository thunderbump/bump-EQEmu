#![forbid(unsafe_code)]

//! One actor, one pending move, at most one input and one output per advance.
//! Body existence is acknowledged only from later native facts; never from requests.
//! ```
//! use actor_core::{Input, Kind, Runtime, Status};
//! let mut runtime = Runtime::new(7);
//! assert_eq!(runtime.advance(1, 1, &[Input::new(Kind::Create, 7, 0, 0)]).status,
//!            Status::Ok);
//! ```

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
#[repr(u8)]
pub enum Kind {
    Create,
    Move,
    Stop,
    Retire,
    Shutdown,
    Admitted,
    Completed,
    Rejected,
    Interrupted,
    CreateBody,
    BodyPresent,
    BodyRejected,
    BodyAbsent,
    StartPatrol,
    Pause,
    Resume,
    StartApproach,
}

#[derive(Clone, Copy, Debug)]
pub struct Input {
    pub kind: Kind,
    pub epoch: u64,
    pub generation: u64,
    pub operation: u64,
    pub point: [i32; 3],
}

impl Input {
    pub fn new(kind: Kind, epoch: u64, generation: u64, operation: u64) -> Self {
        Self {
            kind,
            epoch,
            generation,
            operation,
            point: [0; 3],
        }
    }
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
#[repr(u8)]
pub enum Status {
    Ok,
    ExcessInput,
    Order,
    Stale,
    Busy,
    Missing,
    InvalidResult,
    Closed,
    Exhausted,
    InvalidInput,
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
#[repr(u8)]
pub enum Event {
    None,
    Created,
    MoveRequested,
    StopRequested,
    Admitted,
    Completed,
    Rejected,
    Interrupted,
    Retired,
    Shutdown,
    BodyRequested,
    BodyPresent,
    BodyRejected,
    RemovalRequested,
    BodyAbsent,
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
#[repr(u8)]
pub enum Phase {
    Idle,
    Requested,
    Admitted,
    Stopping,
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
#[repr(u8)]
pub enum Body {
    Logical,
    Requested,
    Present,
    Removing,
    Absent,
}

#[derive(Clone, Copy, Debug)]
pub struct Report {
    pub status: Status,
    pub event: Event,
    pub epoch: u64,
    pub generation: u64,
    pub operation: u64,
    pub point: [i32; 3],
}

#[derive(Clone, Copy, Debug)]
pub struct Snapshot {
    pub epoch: u64,
    pub generation: u64,
    pub alive: bool,
    pub body: Body,
    pub closed: bool,
    pub phase: Phase,
    pub operation: u64,
    pub last_terminal: Event,
    pub last_operation: u64,
    pub sequence: u64,
    pub tick: u64,
    pub patrol: Patrol,
    pub approach: Approach,
    pub goal: u64,
    pub target: u64,
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
#[repr(u8)]
pub enum Patrol {
    Disabled,
    Running,
    Paused,
    Retiring,
}

/// One-shot state; Paused never retries without an explicit Resume.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
#[repr(u8)]
pub enum Approach {
    Disabled,
    Observing,
    Travelling,
    Reached,
    Paused,
    Retiring,
}

/// Complete owned native facts. Positions are millimetres; no native pointers.
#[derive(Clone, Copy, Debug)]
pub struct Candidate {
    pub incarnation: u64,
    pub point: [i32; 3],
    pub present: bool,
    pub alive: bool,
    pub targetable: bool,
    pub owned: bool,
    pub engaged: bool,
    pub actor: bool,
    pub client: bool,
    pub self_body: bool,
}

pub struct Observation<'a> {
    pub epoch: u64,
    pub generation: u64,
    pub goal: u64,
    pub actor: [i32; 3],
    pub candidates: &'a [Candidate],
}

pub struct Runtime {
    state: Snapshot,
    high_operation: u64,
    endpoints: [[i32; 3]; 2],
    next_endpoint: usize,
    target_point: [i32; 3],
    near_target: bool,
}

impl Runtime {
    pub fn new(epoch: u64) -> Self {
        Self {
            state: Snapshot {
                epoch,
                generation: 0,
                alive: false,
                body: Body::Logical,
                closed: false,
                phase: Phase::Idle,
                operation: 0,
                last_terminal: Event::None,
                last_operation: 0,
                sequence: 0,
                tick: 0,
                patrol: Patrol::Disabled,
                approach: Approach::Disabled,
                goal: 0,
                target: 0,
            },
            high_operation: 0,
            endpoints: [[0; 3]; 2],
            next_endpoint: 1,
            target_point: [0; 3],
            near_target: false,
        }
    }

    pub fn inspect(&self) -> Snapshot {
        self.state
    }

    /// Sequence must strictly increase; ticks may stay equal but cannot decrease.
    /// Refusals do not consume ordering inputs or mutate state. Empty advances
    /// may select one patrol effect. No queued outputs: caller owns each report.
    pub fn advance(&mut self, sequence: u64, tick: u64, inputs: &[Input]) -> Report {
        let mut report = Report {
            status: Status::Ok,
            event: Event::None,
            epoch: self.state.epoch,
            generation: self.state.generation,
            operation: 0,
            point: [0; 3],
        };
        report.status = if inputs.len() > 1 {
            Status::ExcessInput
        } else if self.state.closed {
            Status::Closed
        } else if sequence <= self.state.sequence || tick < self.state.tick {
            Status::Order
        } else if let Some(input) = inputs.first() {
            report.operation = input.operation;
            report.point = input.point;
            self.apply(*input, &mut report.event)
        } else {
            self.idle(&mut report)
        };
        if report.status == Status::Ok {
            self.state.sequence = sequence;
            self.state.tick = tick;
        }
        report.generation = self.state.generation;
        if report.event == Event::StopRequested {
            report.operation = self.state.operation;
        }
        report
    }

    /// A complete fresh scope (including absent members), at most four entries.
    /// Selection radius: XY 30, Z 6 units. Arrival: XY 5, Z 6. Drift from
    /// selection: XY 1, Z 1. Reached is latched, not a pursuit policy.
    pub fn observe(&mut self, sequence: u64, tick: u64, observation: Observation<'_>) -> Report {
        let s = self.state;
        let mut out = Report {
            status: Status::Ok,
            event: Event::None,
            epoch: s.epoch,
            generation: s.generation,
            operation: 0,
            point: [0; 3],
        };
        out.status = if s.closed {
            Status::Closed
        } else if sequence <= s.sequence || tick < s.tick {
            Status::Order
        } else if observation.epoch != s.epoch
            || observation.generation != s.generation
            || observation.goal != s.goal
        {
            Status::Stale
        } else if !s.alive || s.body != Body::Present {
            Status::Missing
        } else if observation.candidates.len() > 4
            || !valid_point(observation.actor)
            || observation.candidates.iter().enumerate().any(|(i, c)| {
                c.incarnation == 0
                    || !valid_point(c.point)
                    || observation.candidates[..i]
                        .iter()
                        .any(|p| p.incarnation == c.incarnation)
            })
        {
            Status::InvalidInput
        } else {
            Status::Ok
        };
        if out.status != Status::Ok {
            return out;
        }
        self.state.sequence = sequence;
        self.state.tick = tick;
        if s.approach == Approach::Travelling {
            let target = observation
                .candidates
                .iter()
                .find(|c| c.incarnation == s.target);
            if let Some(c) =
                target.filter(|c| eligible(c) && within(c.point, self.target_point, 1000, 1000))
            {
                self.near_target = within(observation.actor, c.point, 5000, 6000);
            } else {
                self.near_target = false;
                self.state.approach = Approach::Paused;
                if s.phase == Phase::Admitted {
                    self.state.phase = Phase::Stopping;
                    out.event = Event::StopRequested;
                    out.operation = s.operation;
                }
            }
        } else if s.approach == Approach::Observing && s.phase == Phase::Idle {
            let selected = observation
                .candidates
                .iter()
                .filter(|c| eligible(c) && within(observation.actor, c.point, 30000, 6000))
                .min_by_key(|c| (distance_xy(observation.actor, c.point), c.incarnation));
            if let Some(c) = selected {
                self.state.target = c.incarnation;
                self.target_point = c.point;
                self.near_target = within(observation.actor, c.point, 5000, 6000);
                if self.near_target {
                    self.state.approach = Approach::Reached;
                } else {
                    let Some(operation) = self.high_operation.checked_add(1) else {
                        self.state.approach = Approach::Paused;
                        out.status = Status::Exhausted;
                        return out;
                    };
                    // Stop two units short; integer native destination rounding plus
                    // native XY<=2 arrival tolerance fits the XY<=5 goal radius.
                    let dx = f64::from(c.point[0]) - f64::from(observation.actor[0]);
                    let dy = f64::from(c.point[1]) - f64::from(observation.actor[1]);
                    let length = dx.hypot(dy);
                    let scale = if length > 2000.0 {
                        2000.0 / length
                    } else {
                        0.0
                    };
                    out.point = [
                        ((f64::from(c.point[0]) - dx * scale) / 1000.0).round() as i32,
                        ((f64::from(c.point[1]) - dy * scale) / 1000.0).round() as i32,
                        (f64::from(c.point[2]) / 1000.0).round() as i32,
                    ];
                    self.high_operation = operation;
                    self.state.operation = operation;
                    self.state.phase = Phase::Requested;
                    self.state.approach = Approach::Travelling;
                    out.operation = operation;
                    out.event = Event::MoveRequested;
                }
            }
        }
        out
    }

    // Terminal facts are returned intact; only a later empty advance may
    // allocate a new operation. No queue or unbounded policy work.
    fn idle(&mut self, report: &mut Report) -> Status {
        let s = &mut self.state;
        if !s.alive || s.phase != Phase::Idle || s.body != Body::Present {
            return Status::Ok;
        }
        if s.patrol == Patrol::Retiring || s.approach == Approach::Retiring {
            s.body = Body::Removing;
            report.event = Event::RemovalRequested;
        } else if s.patrol == Patrol::Running {
            let Some(operation) = self.high_operation.checked_add(1) else {
                s.patrol = Patrol::Paused;
                return Status::Exhausted;
            };
            self.high_operation = operation;
            s.operation = operation;
            s.phase = Phase::Requested;
            report.operation = operation;
            report.point = self.endpoints[self.next_endpoint];
            report.event = Event::MoveRequested;
        }
        Status::Ok
    }

    fn apply(&mut self, input: Input, event: &mut Event) -> Status {
        let s = &mut self.state;
        if input.epoch != s.epoch || input.generation != s.generation {
            return Status::Stale;
        }
        match input.kind {
            Kind::Create | Kind::CreateBody => {
                if s.alive {
                    return Status::Busy;
                }
                let Some(next) = s.generation.checked_add(1) else {
                    return Status::Exhausted;
                };
                s.generation = next;
                s.patrol = Patrol::Disabled;
                s.approach = Approach::Disabled;
                s.target = 0;
                self.endpoints[0] = input.point;
                self.next_endpoint = 1;
                s.alive = true;
                s.body = if input.kind == Kind::CreateBody {
                    Body::Requested
                } else {
                    Body::Logical
                };
                *event = if input.kind == Kind::CreateBody {
                    Event::BodyRequested
                } else {
                    Event::Created
                };
            }
            Kind::Shutdown => {
                if s.alive {
                    return Status::Busy;
                }
                s.closed = true;
                *event = Event::Shutdown;
            }
            _ if !s.alive => return Status::Missing,
            Kind::BodyPresent if s.body == Body::Requested => {
                s.body = Body::Present;
                *event = Event::BodyPresent;
            }
            Kind::BodyRejected if s.body == Body::Requested => {
                s.body = Body::Absent;
                s.alive = false;
                *event = Event::BodyRejected;
            }
            Kind::BodyAbsent if matches!(s.body, Body::Present | Body::Removing) => {
                if s.phase != Phase::Idle {
                    return Status::Busy;
                }
                s.body = Body::Absent;
                s.alive = false;
                s.phase = Phase::Idle;
                s.operation = 0;
                *event = Event::BodyAbsent;
            }
            Kind::BodyPresent | Kind::BodyRejected | Kind::BodyAbsent => {
                return Status::InvalidResult;
            }
            Kind::StartPatrol => {
                if s.patrol != Patrol::Disabled
                    || s.approach != Approach::Disabled
                    || s.phase != Phase::Idle
                    || !matches!(s.body, Body::Requested | Body::Present)
                {
                    return Status::Busy;
                }
                if input.point == self.endpoints[0] {
                    return Status::InvalidInput;
                }
                self.endpoints[1] = input.point;
                s.patrol = Patrol::Running;
            }
            Kind::StartApproach => {
                if s.patrol != Patrol::Disabled
                    || s.approach != Approach::Disabled
                    || s.phase != Phase::Idle
                    || s.body != Body::Present
                {
                    return Status::Busy;
                }
                let Some(goal) = s.goal.checked_add(1) else {
                    return Status::Exhausted;
                };
                s.goal = goal;
                s.approach = Approach::Observing;
            }
            Kind::Resume if s.approach != Approach::Disabled => {
                if s.approach != Approach::Paused
                    || s.phase != Phase::Idle
                    || s.body != Body::Present
                {
                    return Status::Busy;
                }
                let Some(goal) = s.goal.checked_add(1) else {
                    return Status::Exhausted;
                };
                s.goal = goal;
                s.target = 0;
                self.near_target = false;
                s.approach = Approach::Observing;
            }
            Kind::Pause if s.approach != Approach::Disabled => {
                if !matches!(
                    s.approach,
                    Approach::Observing | Approach::Travelling | Approach::Paused
                ) {
                    return Status::Busy;
                }
                s.approach = Approach::Paused;
                if s.phase == Phase::Admitted {
                    s.phase = Phase::Stopping;
                    *event = Event::StopRequested;
                }
            }
            Kind::Resume => {
                if s.patrol != Patrol::Paused || s.phase != Phase::Idle || s.body != Body::Present {
                    // A draining or retired goal is never resumable.
                    return Status::Busy;
                }
                s.patrol = Patrol::Running;
            }
            Kind::Pause => {
                if !matches!(s.patrol, Patrol::Running | Patrol::Paused) {
                    return Status::Busy;
                }
                s.patrol = Patrol::Paused;
                if s.phase == Phase::Admitted {
                    s.phase = Phase::Stopping;
                    *event = Event::StopRequested;
                }
            }
            Kind::Move => {
                if s.patrol != Patrol::Disabled || s.approach != Approach::Disabled {
                    return Status::Busy;
                }
                // Native admission remains a later, correlated fact.
                if !matches!(s.body, Body::Logical | Body::Present) {
                    return Status::Busy;
                }
                if s.phase != Phase::Idle {
                    return Status::Busy;
                }
                if input.operation <= self.high_operation {
                    return Status::InvalidInput;
                }
                self.high_operation = input.operation;
                s.operation = input.operation;
                s.phase = Phase::Requested;
                *event = Event::MoveRequested;
            }
            Kind::Stop => {
                if input.operation != s.operation || s.phase != Phase::Admitted {
                    return Status::InvalidResult;
                }
                s.phase = Phase::Stopping;
                if s.approach == Approach::Travelling {
                    s.approach = Approach::Paused;
                }
                if s.patrol == Patrol::Running {
                    s.patrol = Patrol::Paused;
                }
                *event = Event::StopRequested;
            }
            Kind::Retire if s.approach != Approach::Disabled => {
                s.approach = Approach::Retiring;
                if s.phase == Phase::Admitted {
                    s.phase = Phase::Stopping;
                    *event = Event::StopRequested;
                }
            }
            Kind::Retire if s.patrol != Patrol::Disabled => {
                s.patrol = Patrol::Retiring;
                if s.phase == Phase::Admitted {
                    s.phase = Phase::Stopping;
                    *event = Event::StopRequested;
                }
            }
            Kind::Retire => {
                if s.phase != Phase::Idle {
                    return Status::Busy;
                }
                if s.body == Body::Present {
                    s.body = Body::Removing;
                    *event = Event::RemovalRequested;
                } else if s.body == Body::Logical {
                    s.alive = false;
                    *event = Event::Retired;
                } else {
                    return Status::Busy;
                }
            }
            kind => {
                if input.operation != s.operation || s.phase == Phase::Idle {
                    return Status::InvalidResult;
                }
                *event = match (kind, s.phase) {
                    (Kind::Admitted, Phase::Requested) => Event::Admitted,
                    (Kind::Rejected, Phase::Requested) => Event::Rejected,
                    // Cancellation can race a completion already queued by native.
                    // A stop request does not rewrite that terminal outcome.
                    (Kind::Completed, Phase::Admitted | Phase::Stopping) => Event::Completed,
                    (Kind::Interrupted, Phase::Admitted | Phase::Stopping) => Event::Interrupted,
                    _ => return Status::InvalidResult,
                };
                if *event == Event::Admitted {
                    s.phase = Phase::Admitted;
                } else {
                    if s.patrol != Patrol::Disabled {
                        if *event == Event::Completed {
                            self.next_endpoint ^= 1;
                        } else if s.patrol == Patrol::Running {
                            s.patrol = Patrol::Paused;
                        }
                    }
                    if s.approach == Approach::Travelling {
                        s.approach = if *event == Event::Completed && self.near_target {
                            Approach::Reached
                        } else {
                            Approach::Paused
                        };
                    }
                    s.last_terminal = *event;
                    s.last_operation = s.operation;
                    s.operation = 0;
                    s.phase = Phase::Idle;
                }
            }
        }
        Status::Ok
    }
}

fn valid_point(p: [i32; 3]) -> bool {
    p.iter().all(|v| v.unsigned_abs() <= 1_000_000_000)
}
fn distance_xy(a: [i32; 3], b: [i32; 3]) -> i128 {
    let x = i128::from(a[0]) - i128::from(b[0]);
    let y = i128::from(a[1]) - i128::from(b[1]);
    x * x + y * y
}
fn within(a: [i32; 3], b: [i32; 3], xy: i32, z: i32) -> bool {
    distance_xy(a, b) <= i128::from(xy) * i128::from(xy)
        && (i64::from(a[2]) - i64::from(b[2])).abs() <= i64::from(z)
}
fn eligible(c: &Candidate) -> bool {
    c.present
        && c.alive
        && c.targetable
        && !c.owned
        && !c.engaged
        && !c.actor
        && !c.client
        && !c.self_body
}

#[cfg(test)]
mod tests {
    use super::*;

    fn send(r: &mut Runtime, kind: Kind, operation: u64) -> Report {
        let s = r.inspect();
        r.advance(
            s.sequence + 1,
            s.tick + 1,
            &[Input::new(kind, s.epoch, s.generation, operation)],
        )
    }

    fn approach() -> Runtime {
        let mut r = Runtime::new(88);
        send(&mut r, Kind::CreateBody, 0);
        send(&mut r, Kind::BodyPresent, 0);
        assert_eq!(send(&mut r, Kind::StartApproach, 0).status, Status::Ok);
        r
    }

    fn candidate(incarnation: u64, x: i32) -> Candidate {
        Candidate {
            incarnation,
            point: [x, 0, 0],
            present: true,
            alive: true,
            targetable: true,
            owned: false,
            engaged: false,
            actor: false,
            client: false,
            self_body: false,
        }
    }

    fn observe(r: &mut Runtime, actor: [i32; 3], candidates: &[Candidate]) -> Report {
        let s = r.inspect();
        r.observe(
            s.sequence + 1,
            s.tick + 1,
            Observation {
                epoch: s.epoch,
                generation: s.generation,
                goal: s.goal,
                actor,
                candidates,
            },
        )
    }

    #[test]
    fn approach_idle_selection_ties_and_exclusions() {
        for reversed in [false, true] {
            let mut r = approach();
            for _ in 0..8 {
                assert_eq!(observe(&mut r, [0; 3], &[]).event, Event::None);
                assert_eq!(idle(&mut r).event, Event::None);
            }
            let mut scope = [
                candidate(9, 18000),
                candidate(7, -18000),
                candidate(3, 25000),
            ];
            if reversed {
                scope.reverse();
            }
            let request = observe(&mut r, [0; 3], &scope);
            assert_eq!(request.event, Event::MoveRequested);
            assert_eq!(r.inspect().target, 7);
            assert_eq!(request.point, [-16, 0, 0]);
            assert_eq!(send(&mut r, Kind::Move, 100).status, Status::Busy);
            assert_eq!(send(&mut r, Kind::StartPatrol, 0).status, Status::Busy);
            assert_eq!(send(&mut r, Kind::StartApproach, 0).status, Status::Busy);
        }
        for exclusion in 0..11 {
            let mut r = approach();
            let mut c = candidate(1, 10000);
            match exclusion {
                0 => c.present = false,
                1 => c.alive = false,
                2 => c.targetable = false,
                3 => c.owned = true,
                4 => c.engaged = true,
                5 => c.actor = true,
                6 => c.client = true,
                7 => c.self_body = true,
                8 => c.point[0] = 30001,
                9 => c.point[2] = 6001,
                _ => c.point[0] = -30001,
            }
            assert_eq!(observe(&mut r, [0; 3], &[c]).event, Event::None);
            let eligible = candidate(1, 10000);
            assert_eq!(
                observe(&mut r, [0; 3], &[eligible]).event,
                Event::MoveRequested
            );
        }
    }

    #[test]
    fn approach_arrival_is_native_correlated_and_quiet() {
        let mut r = approach();
        let c = candidate(10, 20000);
        let request = observe(&mut r, [0; 3], &[c]);
        assert_eq!(
            send(&mut r, Kind::Completed, request.operation).status,
            Status::InvalidResult
        );
        send(&mut r, Kind::Admitted, request.operation);
        assert_eq!(observe(&mut r, [18000, 0, 0], &[c]).event, Event::None);
        assert_eq!(r.inspect().approach, Approach::Travelling);
        send(&mut r, Kind::Completed, request.operation);
        assert_eq!(r.inspect().approach, Approach::Reached);
        for _ in 0..8 {
            assert_eq!(observe(&mut r, [18000, 0, 0], &[]).event, Event::None);
            assert_eq!(idle(&mut r).event, Event::None);
        }
        assert_eq!(send(&mut r, Kind::Resume, 0).status, Status::Busy);
        // Native completion at a position outside the goal radius is not success.
        let mut r = approach();
        observe(&mut r, [0; 3], &[c]);
        send(&mut r, Kind::Admitted, 1);
        send(&mut r, Kind::Completed, 1);
        assert_eq!(r.inspect().last_terminal, Event::Completed);
        assert_eq!(r.inspect().approach, Approach::Paused);
    }

    #[test]
    fn approach_invalidation_drains_and_resume_requires_fresh_facts() {
        for reason in 0..9 {
            for terminal in [Kind::Completed, Kind::Interrupted] {
                let mut r = approach();
                let c = candidate(10, 20000);
                observe(&mut r, [0; 3], &[c]);
                send(&mut r, Kind::Admitted, 1);
                let mut changed = c;
                match reason {
                    0 => changed.present = false,
                    1 => changed.alive = false,
                    2 => changed.owned = true,
                    3 => changed.engaged = true,
                    4 => changed.targetable = false,
                    5 => changed.point[0] += 1001,
                    6 => changed.point[2] += 1001,
                    7 => changed.incarnation += 1,
                    _ => (),
                }
                let facts = if reason == 8 {
                    &[][..]
                } else {
                    std::slice::from_ref(&changed)
                };
                assert_eq!(
                    observe(&mut r, [18000, 0, 0], facts).event,
                    Event::StopRequested
                );
                assert_eq!(send(&mut r, Kind::Resume, 0).status, Status::Busy);
                assert_eq!(send(&mut r, terminal, 1).status, Status::Ok);
                assert_eq!(r.inspect().approach, Approach::Paused);
                for _ in 0..8 {
                    assert_eq!(observe(&mut r, [0; 3], &[c]).event, Event::None);
                    assert_eq!(idle(&mut r).event, Event::None);
                }
                let goal = r.inspect().goal;
                assert_eq!(send(&mut r, Kind::Resume, 0).status, Status::Ok);
                assert!(r.inspect().goal > goal);
                assert_eq!(idle(&mut r).event, Event::None);
                assert_eq!(observe(&mut r, [0; 3], &[]).event, Event::None);
                assert_eq!(observe(&mut r, [0; 3], &[c]).operation, 2);
                assert_eq!(
                    send(&mut r, Kind::Completed, 1).status,
                    Status::InvalidResult
                );
            }
        }
    }

    #[test]
    fn approach_protocol_bounds_refusal_pause_and_retirement() {
        let mut r = approach();
        let c = candidate(1, 20000);
        for candidates in [
            &[c; 5][..],
            &[c; 2][..],
            &[candidate(0, 0)][..],
            &[candidate(1, i32::MAX)][..],
        ] {
            let sequence = r.inspect().sequence;
            assert_eq!(
                observe(&mut r, [0; 3], candidates).status,
                Status::InvalidInput
            );
            assert_eq!(r.inspect().sequence, sequence);
        }
        let s = r.inspect();
        for (epoch, generation, goal) in [
            (s.epoch + 1, s.generation, s.goal),
            (s.epoch, s.generation + 1, s.goal),
            (s.epoch, s.generation, s.goal + 1),
        ] {
            assert_eq!(
                r.observe(
                    s.sequence + 1,
                    s.tick,
                    Observation {
                        epoch,
                        generation,
                        goal,
                        actor: [0; 3],
                        candidates: &[c],
                    }
                )
                .status,
                Status::Stale
            );
        }
        assert_eq!(
            r.observe(
                s.sequence,
                s.tick,
                Observation {
                    epoch: s.epoch,
                    generation: s.generation,
                    goal: s.goal,
                    actor: [0; 3],
                    candidates: &[c],
                }
            )
            .status,
            Status::Order
        );
        observe(&mut r, [0; 3], &[c]);
        send(&mut r, Kind::Pause, 0);
        send(&mut r, Kind::Rejected, 1);
        assert_eq!(r.inspect().approach, Approach::Paused);
        send(&mut r, Kind::Resume, 0);
        observe(&mut r, [0; 3], &[c]);
        send(&mut r, Kind::Rejected, 2);
        assert_eq!(r.inspect().approach, Approach::Paused);
        send(&mut r, Kind::Resume, 0);
        observe(&mut r, [0; 3], &[c]);
        send(&mut r, Kind::Admitted, 3);
        assert_eq!(send(&mut r, Kind::Retire, 0).event, Event::StopRequested);
        send(&mut r, Kind::Completed, 3);
        assert_eq!(r.inspect().approach, Approach::Retiring);
        assert_eq!(send(&mut r, Kind::Resume, 0).status, Status::Busy);
        assert_eq!(idle(&mut r).event, Event::RemovalRequested);
        send(&mut r, Kind::BodyAbsent, 0);
        send(&mut r, Kind::Shutdown, 0);
        assert_eq!(observe(&mut r, [0; 3], &[c]).status, Status::Closed);
    }

    #[test]
    fn native_body_facts_are_not_logical_lifetime_events() {
        let mut r = Runtime::new(31);
        assert_eq!(
            send(&mut r, Kind::CreateBody, 0).event,
            Event::BodyRequested
        );
        assert_eq!(r.inspect().body, Body::Requested);
        assert_eq!(send(&mut r, Kind::Retire, 0).status, Status::Busy);
        assert_eq!(send(&mut r, Kind::Shutdown, 0).status, Status::Busy);
        assert_eq!(
            send(&mut r, Kind::BodyAbsent, 0).status,
            Status::InvalidResult
        );
        assert_eq!(send(&mut r, Kind::BodyPresent, 0).event, Event::BodyPresent);
        assert_eq!(
            send(&mut r, Kind::BodyPresent, 0).status,
            Status::InvalidResult
        );
        assert_eq!(send(&mut r, Kind::Move, 1).event, Event::MoveRequested);
        assert_eq!(send(&mut r, Kind::Rejected, 1).event, Event::Rejected);
        assert_eq!(send(&mut r, Kind::Retire, 0).event, Event::RemovalRequested);
        assert!(r.inspect().alive);
        assert_eq!(r.inspect().body, Body::Removing);
        assert_eq!(send(&mut r, Kind::CreateBody, 0).status, Status::Busy);
        assert_eq!(
            send(&mut r, Kind::BodyPresent, 0).status,
            Status::InvalidResult
        );
        assert_eq!(send(&mut r, Kind::BodyAbsent, 0).event, Event::BodyAbsent);
        assert!(!r.inspect().alive);
        assert_eq!(send(&mut r, Kind::BodyAbsent, 0).status, Status::Missing);
        assert_eq!(send(&mut r, Kind::Shutdown, 0).event, Event::Shutdown);
    }

    #[test]
    fn native_travel_preserves_correlation_and_terminal_before_absence() {
        let mut r = Runtime::new(41);
        send(&mut r, Kind::CreateBody, 0);
        assert_eq!(send(&mut r, Kind::Move, 1).status, Status::Busy);
        send(&mut r, Kind::BodyPresent, 0);
        for terminal in [Kind::Completed, Kind::Interrupted] {
            let operation = r.high_operation + 1;
            assert_eq!(
                send(&mut r, Kind::Move, operation).event,
                Event::MoveRequested
            );
            assert_eq!(
                send(&mut r, Kind::Completed, operation).status,
                Status::InvalidResult
            );
            assert_eq!(
                send(&mut r, Kind::Admitted, operation + 1).status,
                Status::InvalidResult
            );
            send(&mut r, Kind::Admitted, operation);
            assert_eq!(send(&mut r, Kind::BodyAbsent, 0).status, Status::Busy);
            assert_eq!(send(&mut r, Kind::Retire, 0).status, Status::Busy);
            send(&mut r, Kind::Stop, operation);
            assert_eq!(send(&mut r, terminal, operation).status, Status::Ok);
            assert_eq!(r.inspect().last_operation, operation);
            assert_eq!(
                send(&mut r, terminal, operation).status,
                Status::InvalidResult
            );
            assert_eq!(
                send(&mut r, Kind::Move, operation).status,
                Status::InvalidInput
            );
        }
        send(&mut r, Kind::BodyAbsent, 0);
        assert!(!r.inspect().alive);
        assert_eq!(r.inspect().last_terminal, Event::Interrupted);
        let retired = r.inspect();
        send(&mut r, Kind::CreateBody, 0);
        send(&mut r, Kind::BodyPresent, 0);
        for kind in [Kind::Move, Kind::Stop, Kind::Completed, Kind::Interrupted] {
            let current = r.inspect();
            let report = r.advance(
                current.sequence + 1,
                current.tick + 1,
                &[Input::new(kind, retired.epoch, retired.generation, 3)],
            );
            assert_eq!(report.status, Status::Stale);
            assert_eq!(r.inspect().phase, Phase::Idle);
            assert_eq!(r.inspect().sequence, current.sequence);
        }
    }

    #[test]
    fn admission_failure_external_removal_and_delayed_native_facts() {
        let mut r = Runtime::new(32);
        send(&mut r, Kind::CreateBody, 0);
        assert_eq!(
            send(&mut r, Kind::BodyRejected, 0).event,
            Event::BodyRejected
        );
        assert!(!r.inspect().alive);
        send(&mut r, Kind::CreateBody, 0);
        let old = r.inspect();
        send(&mut r, Kind::BodyPresent, 0);
        // No retirement request: external native absence is an independent fact.
        assert_eq!(send(&mut r, Kind::BodyAbsent, 0).event, Event::BodyAbsent);
        send(&mut r, Kind::CreateBody, 0);
        let current = r.inspect();
        for kind in [
            Kind::BodyPresent,
            Kind::BodyRejected,
            Kind::BodyAbsent,
            Kind::Retire,
        ] {
            for (epoch, generation) in [(old.epoch, old.generation), (33, current.generation)] {
                let report = r.advance(
                    current.sequence + 1,
                    current.tick + 1,
                    &[Input::new(kind, epoch, generation, 0)],
                );
                assert_eq!(report.status, Status::Stale);
                assert_eq!(report.event, Event::None);
                assert_eq!(r.inspect().body, Body::Requested);
                assert_eq!(r.inspect().sequence, current.sequence);
            }
        }
        assert_eq!(send(&mut r, Kind::BodyPresent, 0).event, Event::BodyPresent);
        send(&mut r, Kind::Retire, 0);
        send(&mut r, Kind::BodyAbsent, 0);
        send(&mut r, Kind::Shutdown, 0);
        assert!(r.inspect().closed);
    }

    #[test]
    fn lifecycle_and_native_trace() {
        let mut r = Runtime::new(9);
        assert_eq!(send(&mut r, Kind::Create, 0).event, Event::Created);
        assert_eq!(send(&mut r, Kind::Move, 1).event, Event::MoveRequested);
        assert_eq!(send(&mut r, Kind::Shutdown, 0).status, Status::Busy);
        assert_eq!(send(&mut r, Kind::Stop, 1).status, Status::InvalidResult);
        assert_eq!(
            send(&mut r, Kind::Completed, 1).status,
            Status::InvalidResult
        );
        assert_eq!(send(&mut r, Kind::Admitted, 1).event, Event::Admitted);
        assert_eq!(send(&mut r, Kind::Completed, 1).event, Event::Completed);
        assert_eq!(r.inspect().last_operation, 1);
        assert_eq!(send(&mut r, Kind::Create, 0).status, Status::Busy);
        assert_eq!(send(&mut r, Kind::Move, 1).status, Status::InvalidInput);
        assert_eq!(send(&mut r, Kind::Move, 2).status, Status::Ok);
        assert_eq!(send(&mut r, Kind::Retire, 0).status, Status::Busy);
        assert_eq!(send(&mut r, Kind::Admitted, 2).status, Status::Ok);
        assert_eq!(send(&mut r, Kind::Stop, 2).event, Event::StopRequested);
        assert_eq!(send(&mut r, Kind::Interrupted, 2).event, Event::Interrupted);
        assert_eq!(send(&mut r, Kind::Retire, 0).event, Event::Retired);
        assert!(!r.inspect().alive);
        assert_eq!(send(&mut r, Kind::Interrupted, 2).status, Status::Missing);
        assert_eq!(send(&mut r, Kind::Shutdown, 0).event, Event::Shutdown);
        assert_eq!(send(&mut r, Kind::Create, 0).status, Status::Closed);
    }

    #[test]
    fn refusal_stale_replacement_and_bounds() {
        let mut r = Runtime::new(5);
        assert_eq!(send(&mut r, Kind::Move, 1).status, Status::Missing);
        send(&mut r, Kind::Create, 0);
        send(&mut r, Kind::Move, 1);
        assert_eq!(send(&mut r, Kind::Rejected, 1).event, Event::Rejected);
        send(&mut r, Kind::Retire, 0);
        send(&mut r, Kind::Create, 0);
        let s = r.inspect();
        for (epoch, generation) in [(4, s.generation), (5, 1)] {
            let input = Input::new(Kind::Interrupted, epoch, generation, 1);
            assert_eq!(r.advance(20, 20, &[input]).status, Status::Stale);
        }
        let input = Input::new(Kind::Move, 5, s.generation, 2);
        assert_eq!(r.advance(20, 20, &[input; 2]).status, Status::ExcessInput);
        assert_eq!(r.inspect().sequence, s.sequence);
        assert_eq!(r.inspect().last_terminal, Event::Rejected);
        assert_eq!(r.advance(s.sequence, 20, &[]).status, Status::Order);
        assert_eq!(r.advance(20, s.tick - 1, &[]).status, Status::Order);
        assert_eq!(r.advance(20, 20, &[input]).event, Event::MoveRequested);
        assert_eq!(send(&mut r, Kind::Move, 3).status, Status::Busy);
        assert_eq!(
            send(&mut r, Kind::Admitted, 1).status,
            Status::InvalidResult
        );
    }

    #[test]
    fn completion_queued_before_stop_remains_truthful() {
        let mut r = Runtime::new(9);
        send(&mut r, Kind::Create, 0);
        send(&mut r, Kind::Move, 1);
        send(&mut r, Kind::Admitted, 1);
        // Native finishes and retains this result before receiving StopRequested.
        let admitted = r.inspect();
        let completed = Input::new(Kind::Completed, 9, admitted.generation, 1);
        assert_eq!(send(&mut r, Kind::Stop, 1).event, Event::StopRequested);
        let stopping = r.inspect();
        assert_eq!(stopping.phase, Phase::Stopping);
        assert_eq!(send(&mut r, Kind::Retire, 0).status, Status::Busy);
        assert_eq!(send(&mut r, Kind::Shutdown, 0).status, Status::Busy);
        assert_eq!(
            send(&mut r, Kind::Completed, 2).status,
            Status::InvalidResult
        );
        let sequence = stopping.sequence + 1;
        let tick = stopping.tick + 1;
        for (epoch, generation) in [(8, stopping.generation), (9, 0)] {
            let stale = Input::new(Kind::Completed, epoch, generation, 1);
            assert_eq!(r.advance(sequence, tick, &[stale]).status, Status::Stale);
        }
        assert_eq!(
            r.advance(sequence, tick, &[completed; 2]).status,
            Status::ExcessInput
        );
        assert_eq!(r.inspect().sequence, stopping.sequence);
        assert_eq!(r.inspect().phase, Phase::Stopping);
        assert_eq!(r.inspect().operation, 1);
        // Retry the retained result alone with the unconsumed ordering inputs.
        let report = r.advance(sequence, tick, &[completed]);
        assert_eq!(report.status, Status::Ok);
        assert_eq!(report.event, Event::Completed);
        assert_eq!(report.operation, 1);
        assert_eq!(r.inspect().phase, Phase::Idle);
        assert_eq!(r.inspect().operation, 0);
        assert_eq!(r.inspect().last_terminal, Event::Completed);
        assert_eq!(r.inspect().last_operation, 1);
        assert_eq!(
            send(&mut r, Kind::Interrupted, 1).status,
            Status::InvalidResult
        );
        assert_eq!(
            send(&mut r, Kind::Completed, 1).status,
            Status::InvalidResult
        );
        assert_eq!(send(&mut r, Kind::Move, 2).event, Event::MoveRequested);
        assert_eq!(
            send(&mut r, Kind::Interrupted, 1).status,
            Status::InvalidResult
        );
        assert_eq!(send(&mut r, Kind::Admitted, 2).event, Event::Admitted);
        assert_eq!(send(&mut r, Kind::Completed, 2).event, Event::Completed);
        assert_eq!(send(&mut r, Kind::Retire, 0).event, Event::Retired);
        assert_eq!(send(&mut r, Kind::Create, 0).event, Event::Created);
        let replacement = r.inspect();
        assert_eq!(
            r.advance(replacement.sequence + 1, replacement.tick, &[completed])
                .status,
            Status::Stale
        );
        assert_eq!(r.inspect().last_terminal, Event::Completed);
        assert_eq!(r.inspect().last_operation, 2);
        assert_eq!(send(&mut r, Kind::Retire, 0).event, Event::Retired);
        assert_eq!(send(&mut r, Kind::Shutdown, 0).event, Event::Shutdown);
        assert!(r.inspect().closed);
    }

    fn idle(r: &mut Runtime) -> Report {
        let s = r.inspect();
        r.advance(s.sequence + 1, s.tick + 1, &[])
    }

    fn patrol() -> Runtime {
        let mut r = Runtime::new(77);
        send(&mut r, Kind::CreateBody, 0);
        let s = r.inspect();
        let mut start = Input::new(Kind::StartPatrol, s.epoch, s.generation, 0);
        start.point = [40, 0, 0];
        assert_eq!(
            r.advance(s.sequence + 1, s.tick, &[start]).status,
            Status::Ok
        );
        assert_eq!(idle(&mut r).event, Event::None);
        send(&mut r, Kind::BodyPresent, 0);
        r
    }

    #[test]
    fn patrol_alternates_only_after_correlated_native_completion() {
        let mut r = patrol();
        for operation in 1..=4 {
            let request = idle(&mut r);
            assert_eq!(request.event, Event::MoveRequested);
            assert_eq!(request.operation, operation);
            assert_eq!(
                request.point,
                if operation % 2 == 1 {
                    [40, 0, 0]
                } else {
                    [0; 3]
                }
            );
            assert_eq!(idle(&mut r).event, Event::None);
            assert_eq!(send(&mut r, Kind::Move, 90).status, Status::Busy);
            assert_eq!(
                send(&mut r, Kind::Completed, operation).status,
                Status::InvalidResult
            );
            assert_eq!(
                send(&mut r, Kind::Admitted, operation + 1).status,
                Status::InvalidResult
            );
            send(&mut r, Kind::Admitted, operation);
            let terminal = send(&mut r, Kind::Completed, operation);
            assert_eq!(terminal.event, Event::Completed);
            assert_eq!(terminal.operation, operation);
            assert_eq!(r.inspect().phase, Phase::Idle);
            assert_eq!(r.inspect().last_operation, operation);
        }
    }

    #[test]
    fn patrol_pause_refusal_and_late_completion_are_quiet() {
        let mut r = patrol();
        idle(&mut r);
        send(&mut r, Kind::Pause, 0);
        // Requested cancellation is acknowledged as refusal, never admission.
        send(&mut r, Kind::Rejected, 1);
        for _ in 0..20 {
            assert_eq!(idle(&mut r).event, Event::None);
        }
        assert_eq!(send(&mut r, Kind::Resume, 0).status, Status::Ok);
        assert_eq!(idle(&mut r).operation, 2);
        send(&mut r, Kind::Rejected, 2);
        assert_eq!(r.inspect().patrol, Patrol::Paused);
        for _ in 0..20 {
            assert_eq!(idle(&mut r).event, Event::None);
        }
        send(&mut r, Kind::Resume, 0);
        idle(&mut r);
        send(&mut r, Kind::Admitted, 3);
        assert_eq!(send(&mut r, Kind::Pause, 0).event, Event::StopRequested);
        assert_eq!(send(&mut r, Kind::Completed, 3).event, Event::Completed);
        assert_eq!(idle(&mut r).event, Event::None);
        send(&mut r, Kind::Resume, 0);
        assert_eq!(idle(&mut r).point, [0; 3]);
        send(&mut r, Kind::Admitted, 4);
        send(&mut r, Kind::Interrupted, 4);
        assert_eq!(r.inspect().patrol, Patrol::Paused);
        for _ in 0..20 {
            assert_eq!(idle(&mut r).event, Event::None);
        }
    }

    #[test]
    fn patrol_retirement_and_external_absence_cannot_resume() {
        for admitted in [false, true] {
            let mut r = patrol();
            idle(&mut r);
            if admitted {
                send(&mut r, Kind::Admitted, 1);
            }
            assert_eq!(send(&mut r, Kind::Retire, 0).status, Status::Ok);
            assert_eq!(send(&mut r, Kind::Resume, 0).status, Status::Busy);
            let terminal = if admitted {
                Kind::Completed
            } else {
                Kind::Rejected
            };
            assert_eq!(send(&mut r, terminal, 1).status, Status::Ok);
            assert_eq!(r.inspect().last_operation, 1);
            assert_eq!(idle(&mut r).event, Event::RemovalRequested);
            assert!(r.inspect().alive);
            send(&mut r, Kind::BodyAbsent, 0);
            assert_eq!(send(&mut r, Kind::Resume, 0).status, Status::Missing);
            send(&mut r, Kind::Shutdown, 0);
            assert_eq!(idle(&mut r).status, Status::Closed);
        }
        let mut r = patrol();
        let old = r.inspect();
        send(&mut r, Kind::BodyAbsent, 0);
        for _ in 0..20 {
            assert_eq!(idle(&mut r).event, Event::None);
        }
        send(&mut r, Kind::CreateBody, 0);
        send(&mut r, Kind::BodyPresent, 0);
        let s = r.inspect();
        assert_eq!(
            r.advance(
                s.sequence + 1,
                s.tick,
                &[Input::new(Kind::Resume, old.epoch, old.generation, 0)]
            )
            .status,
            Status::Stale
        );
        assert_eq!(r.inspect().patrol, Patrol::Disabled);
        assert_eq!(idle(&mut r).event, Event::None);
    }

    #[test]
    fn generation_exhaustion_preserves_state() {
        let mut r = Runtime::new(1);
        r.state.generation = u64::MAX;
        assert_eq!(send(&mut r, Kind::Create, 0).status, Status::Exhausted);
        assert!(!r.inspect().alive);
    }
}

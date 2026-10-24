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
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
#[repr(u8)]
pub enum Patrol {
    Disabled,
    Running,
    Paused,
    Retiring,
}

pub struct Runtime {
    state: Snapshot,
    high_operation: u64,
    endpoints: [[i32; 3]; 2],
    next_endpoint: usize,
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
            },
            high_operation: 0,
            endpoints: [[0; 3]; 2],
            next_endpoint: 1,
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

    // Terminal facts are returned intact; only a later empty advance may
    // allocate a new operation. No queue or unbounded policy work.
    fn idle(&mut self, report: &mut Report) -> Status {
        let s = &mut self.state;
        if !s.alive || s.phase != Phase::Idle || s.body != Body::Present {
            return Status::Ok;
        }
        if s.patrol == Patrol::Retiring {
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
                if s.patrol != Patrol::Disabled {
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
                if s.patrol == Patrol::Running {
                    s.patrol = Patrol::Paused;
                }
                *event = Event::StopRequested;
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

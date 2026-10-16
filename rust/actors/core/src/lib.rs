#![forbid(unsafe_code)]

//! One logical actor, one pending move, at most one input and one output per advance.
//! Outputs describe protocol facts, never native position or body existence.
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
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
#[repr(u8)]
pub enum Phase {
    Idle,
    Requested,
    Admitted,
    Stopping,
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
    pub closed: bool,
    pub phase: Phase,
    pub operation: u64,
    pub last_terminal: Event,
    pub last_operation: u64,
    pub sequence: u64,
    pub tick: u64,
}

pub struct Runtime {
    state: Snapshot,
    high_operation: u64,
}

impl Runtime {
    pub fn new(epoch: u64) -> Self {
        Self {
            state: Snapshot {
                epoch,
                generation: 0,
                alive: false,
                closed: false,
                phase: Phase::Idle,
                operation: 0,
                last_terminal: Event::None,
                last_operation: 0,
                sequence: 0,
                tick: 0,
            },
            high_operation: 0,
        }
    }

    pub fn inspect(&self) -> Snapshot {
        self.state
    }

    /// Sequence must strictly increase; ticks may stay equal but cannot decrease.
    /// Refusals do not consume ordering inputs or mutate state. Empty advances
    /// consume ordering inputs only. No queued outputs: caller owns each report.
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
            Status::Ok
        };
        if report.status == Status::Ok {
            self.state.sequence = sequence;
            self.state.tick = tick;
        }
        report.generation = self.state.generation;
        report
    }

    fn apply(&mut self, input: Input, event: &mut Event) -> Status {
        let s = &mut self.state;
        if input.epoch != s.epoch || input.generation != s.generation {
            return Status::Stale;
        }
        match input.kind {
            Kind::Create => {
                if s.alive {
                    return Status::Busy;
                }
                let Some(next) = s.generation.checked_add(1) else {
                    return Status::Exhausted;
                };
                s.generation = next;
                s.alive = true;
                *event = Event::Created;
            }
            Kind::Shutdown => {
                if s.alive {
                    return Status::Busy;
                }
                s.closed = true;
                *event = Event::Shutdown;
            }
            _ if !s.alive => return Status::Missing,
            Kind::Move => {
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
                *event = Event::StopRequested;
            }
            Kind::Retire => {
                if s.phase != Phase::Idle {
                    return Status::Busy;
                }
                s.alive = false;
                *event = Event::Retired;
            }
            kind => {
                if input.operation != s.operation || s.phase == Phase::Idle {
                    return Status::InvalidResult;
                }
                *event = match (kind, s.phase) {
                    (Kind::Admitted, Phase::Requested) => Event::Admitted,
                    (Kind::Rejected, Phase::Requested) => Event::Rejected,
                    (Kind::Completed, Phase::Admitted) => Event::Completed,
                    (Kind::Interrupted, Phase::Admitted | Phase::Stopping) => Event::Interrupted,
                    _ => return Status::InvalidResult,
                };
                if *event == Event::Admitted {
                    s.phase = Phase::Admitted;
                } else {
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
    fn lifecycle_and_native_trace() {
        let mut r = Runtime::new(9);
        assert_eq!(send(&mut r, Kind::Create, 0).event, Event::Created);
        assert_eq!(send(&mut r, Kind::Move, 1).event, Event::MoveRequested);
        assert_eq!(send(&mut r, Kind::Shutdown, 0).status, Status::Busy);
        assert_eq!(send(&mut r, Kind::Stop, 1).status, Status::InvalidResult);
        assert_eq!(send(&mut r, Kind::Completed, 1).status, Status::InvalidResult);
        assert_eq!(send(&mut r, Kind::Admitted, 1).event, Event::Admitted);
        assert_eq!(send(&mut r, Kind::Completed, 1).event, Event::Completed);
        assert_eq!(r.inspect().last_operation, 1);
        assert_eq!(send(&mut r, Kind::Create, 0).status, Status::Busy);
        assert_eq!(send(&mut r, Kind::Move, 1).status, Status::InvalidInput);
        assert_eq!(send(&mut r, Kind::Move, 2).status, Status::Ok);
        assert_eq!(send(&mut r, Kind::Retire, 0).status, Status::Busy);
        assert_eq!(send(&mut r, Kind::Admitted, 2).status, Status::Ok);
        assert_eq!(send(&mut r, Kind::Stop, 2).event, Event::StopRequested);
        assert_eq!(send(&mut r, Kind::Completed, 2).status, Status::InvalidResult);
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
        assert_eq!(send(&mut r, Kind::Admitted, 1).status, Status::InvalidResult);
    }

    #[test]
    fn generation_exhaustion_preserves_state() {
        let mut r = Runtime::new(1);
        r.state.generation = u64::MAX;
        assert_eq!(send(&mut r, Kind::Create, 0).status, Status::Exhausted);
        assert!(!r.inspect().alive);
    }
}

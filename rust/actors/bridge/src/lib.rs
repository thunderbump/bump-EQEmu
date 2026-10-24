#![deny(unsafe_op_in_unsafe_fn)]

#[cxx::bridge(namespace = "eqemu_proof")]
mod ffi {
    #[repr(u8)]
    enum Kind {
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
    #[repr(u8)]
    enum Status {
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
    #[repr(u8)]
    enum Event {
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
    #[repr(u8)]
    enum Phase {
        Idle,
        Requested,
        Admitted,
        Stopping,
    }
    #[repr(u8)]
    enum Body {
        Logical,
        Requested,
        Present,
        Removing,
        Absent,
    }
    struct Input {
        kind: Kind,
        epoch: u64,
        generation: u64,
        operation: u64,
        x: i32,
        y: i32,
        z: i32,
    }
    struct Report {
        status: Status,
        event: Event,
        epoch: u64,
        generation: u64,
        operation: u64,
        x: i32,
        y: i32,
        z: i32,
    }
    #[repr(u8)]
    enum Patrol {
        Disabled,
        Running,
        Paused,
        Retiring,
    }
    #[repr(u8)]
    enum Approach {
        Disabled,
        Observing,
        Travelling,
        Reached,
        Paused,
        Retiring,
    }
    struct Candidate {
        incarnation: u64,
        x: i32,
        y: i32,
        z: i32,
        present: bool,
        alive: bool,
        targetable: bool,
        owned: bool,
        engaged: bool,
        actor: bool,
        client: bool,
        self_body: bool,
    }
    struct Observation {
        epoch: u64,
        generation: u64,
        goal: u64,
        x: i32,
        y: i32,
        z: i32,
        candidates: Vec<Candidate>,
    }
    struct Snapshot {
        epoch: u64,
        generation: u64,
        alive: bool,
        body: Body,
        closed: bool,
        phase: Phase,
        operation: u64,
        last_terminal: Event,
        last_operation: u64,
        sequence: u64,
        tick: u64,
        patrol: Patrol,
        approach: Approach,
        goal: u64,
        target: u64,
    }
    extern "Rust" {
        type Runtime;
        fn construct(epoch: u64) -> Box<Runtime>;
        fn advance(self: &mut Runtime, sequence: u64, tick: u64, inputs: Vec<Input>) -> Report;
        fn inspect(self: &Runtime) -> Snapshot;
        fn observe(
            self: &mut Runtime,
            sequence: u64,
            tick: u64,
            observation: Observation,
        ) -> Report;
    }
}

pub struct Runtime(actor_core::Runtime);

// Fail-stop policy: never resume with possibly partially mutated state. No Rust
// panic escapes to C++; no native callbacks or exceptions enter Rust.
fn boundary<T>(f: impl FnOnce() -> T) -> T {
    match std::panic::catch_unwind(std::panic::AssertUnwindSafe(f)) {
        Ok(value) => value,
        Err(_) => std::process::abort(),
    }
}

fn construct(epoch: u64) -> Box<Runtime> {
    boundary(|| Box::new(Runtime(actor_core::Runtime::new(epoch))))
}

fn map_status(value: actor_core::Status) -> ffi::Status {
    match value {
        actor_core::Status::Ok => ffi::Status::Ok,
        actor_core::Status::ExcessInput => ffi::Status::ExcessInput,
        actor_core::Status::Order => ffi::Status::Order,
        actor_core::Status::Stale => ffi::Status::Stale,
        actor_core::Status::Busy => ffi::Status::Busy,
        actor_core::Status::Missing => ffi::Status::Missing,
        actor_core::Status::InvalidResult => ffi::Status::InvalidResult,
        actor_core::Status::Closed => ffi::Status::Closed,
        actor_core::Status::Exhausted => ffi::Status::Exhausted,
        actor_core::Status::InvalidInput => ffi::Status::InvalidInput,
    }
}

fn map_event(value: actor_core::Event) -> ffi::Event {
    match value {
        actor_core::Event::None => ffi::Event::None,
        actor_core::Event::Created => ffi::Event::Created,
        actor_core::Event::MoveRequested => ffi::Event::MoveRequested,
        actor_core::Event::StopRequested => ffi::Event::StopRequested,
        actor_core::Event::Admitted => ffi::Event::Admitted,
        actor_core::Event::Completed => ffi::Event::Completed,
        actor_core::Event::Rejected => ffi::Event::Rejected,
        actor_core::Event::Interrupted => ffi::Event::Interrupted,
        actor_core::Event::Retired => ffi::Event::Retired,
        actor_core::Event::Shutdown => ffi::Event::Shutdown,
        actor_core::Event::BodyRequested => ffi::Event::BodyRequested,
        actor_core::Event::BodyPresent => ffi::Event::BodyPresent,
        actor_core::Event::BodyRejected => ffi::Event::BodyRejected,
        actor_core::Event::RemovalRequested => ffi::Event::RemovalRequested,
        actor_core::Event::BodyAbsent => ffi::Event::BodyAbsent,
    }
}

fn map_phase(value: actor_core::Phase) -> ffi::Phase {
    match value {
        actor_core::Phase::Idle => ffi::Phase::Idle,
        actor_core::Phase::Requested => ffi::Phase::Requested,
        actor_core::Phase::Admitted => ffi::Phase::Admitted,
        actor_core::Phase::Stopping => ffi::Phase::Stopping,
    }
}

impl Runtime {
    fn advance(&mut self, sequence: u64, tick: u64, inputs: Vec<ffi::Input>) -> ffi::Report {
        boundary(|| {
            // Check length before conversion: fixed work, no truncation of facts.
            let report = if inputs.len() > 1 {
                let dummy = actor_core::Input::new(actor_core::Kind::Create, 0, 0, 0);
                self.0.advance(sequence, tick, &[dummy; 2])
            } else if let Some(input) = inputs.first() {
                let kind = match input.kind {
                    ffi::Kind::Create => actor_core::Kind::Create,
                    ffi::Kind::Move => actor_core::Kind::Move,
                    ffi::Kind::Stop => actor_core::Kind::Stop,
                    ffi::Kind::Retire => actor_core::Kind::Retire,
                    ffi::Kind::Shutdown => actor_core::Kind::Shutdown,
                    ffi::Kind::Admitted => actor_core::Kind::Admitted,
                    ffi::Kind::Completed => actor_core::Kind::Completed,
                    ffi::Kind::Rejected => actor_core::Kind::Rejected,
                    ffi::Kind::Interrupted => actor_core::Kind::Interrupted,
                    ffi::Kind::CreateBody => actor_core::Kind::CreateBody,
                    ffi::Kind::BodyPresent => actor_core::Kind::BodyPresent,
                    ffi::Kind::BodyRejected => actor_core::Kind::BodyRejected,
                    ffi::Kind::BodyAbsent => actor_core::Kind::BodyAbsent,
                    ffi::Kind::StartPatrol => actor_core::Kind::StartPatrol,
                    ffi::Kind::Pause => actor_core::Kind::Pause,
                    ffi::Kind::Resume => actor_core::Kind::Resume,
                    ffi::Kind::StartApproach => actor_core::Kind::StartApproach,
                    _ => {
                        let s = self.0.inspect();
                        return ffi::Report {
                            status: ffi::Status::InvalidInput,
                            event: ffi::Event::None,
                            epoch: s.epoch,
                            generation: s.generation,
                            operation: input.operation,
                            x: 0,
                            y: 0,
                            z: 0,
                        };
                    }
                };
                self.0.advance(
                    sequence,
                    tick,
                    &[actor_core::Input {
                        kind,
                        epoch: input.epoch,
                        generation: input.generation,
                        operation: input.operation,
                        point: [input.x, input.y, input.z],
                    }],
                )
            } else {
                self.0.advance(sequence, tick, &[])
            };
            ffi::Report {
                status: map_status(report.status),
                event: map_event(report.event),
                epoch: report.epoch,
                generation: report.generation,
                operation: report.operation,
                x: report.point[0],
                y: report.point[1],
                z: report.point[2],
            }
        })
    }

    fn observe(&mut self, sequence: u64, tick: u64, observation: ffi::Observation) -> ffi::Report {
        boundary(|| {
            let mut candidates = Vec::new();
            // Oversize input is rejected whole, never silently truncated.
            if observation.candidates.len() <= 4 {
                for c in observation.candidates {
                    candidates.push(actor_core::Candidate {
                        incarnation: c.incarnation,
                        point: [c.x, c.y, c.z],
                        present: c.present,
                        alive: c.alive,
                        targetable: c.targetable,
                        owned: c.owned,
                        engaged: c.engaged,
                        actor: c.actor,
                        client: c.client,
                        self_body: c.self_body,
                    });
                }
            } else {
                let s = self.0.inspect();
                return ffi::Report {
                    status: ffi::Status::InvalidInput,
                    event: ffi::Event::None,
                    epoch: s.epoch,
                    generation: s.generation,
                    operation: 0,
                    x: 0,
                    y: 0,
                    z: 0,
                };
            }
            let r = self.0.observe(
                sequence,
                tick,
                actor_core::Observation {
                    epoch: observation.epoch,
                    generation: observation.generation,
                    goal: observation.goal,
                    actor: [observation.x, observation.y, observation.z],
                    candidates: &candidates,
                },
            );
            ffi::Report {
                status: map_status(r.status),
                event: map_event(r.event),
                epoch: r.epoch,
                generation: r.generation,
                operation: r.operation,
                x: r.point[0],
                y: r.point[1],
                z: r.point[2],
            }
        })
    }

    fn inspect(&self) -> ffi::Snapshot {
        boundary(|| {
            let s = self.0.inspect();
            ffi::Snapshot {
                epoch: s.epoch,
                generation: s.generation,
                alive: s.alive,
                body: match s.body {
                    actor_core::Body::Logical => ffi::Body::Logical,
                    actor_core::Body::Requested => ffi::Body::Requested,
                    actor_core::Body::Present => ffi::Body::Present,
                    actor_core::Body::Removing => ffi::Body::Removing,
                    actor_core::Body::Absent => ffi::Body::Absent,
                },
                closed: s.closed,
                phase: map_phase(s.phase),
                operation: s.operation,
                last_terminal: map_event(s.last_terminal),
                last_operation: s.last_operation,
                sequence: s.sequence,
                tick: s.tick,
                approach: match s.approach {
                    actor_core::Approach::Disabled => ffi::Approach::Disabled,
                    actor_core::Approach::Observing => ffi::Approach::Observing,
                    actor_core::Approach::Travelling => ffi::Approach::Travelling,
                    actor_core::Approach::Reached => ffi::Approach::Reached,
                    actor_core::Approach::Paused => ffi::Approach::Paused,
                    actor_core::Approach::Retiring => ffi::Approach::Retiring,
                },
                goal: s.goal,
                target: s.target,
                patrol: match s.patrol {
                    actor_core::Patrol::Disabled => ffi::Patrol::Disabled,
                    actor_core::Patrol::Running => ffi::Patrol::Running,
                    actor_core::Patrol::Paused => ffi::Patrol::Paused,
                    actor_core::Patrol::Retiring => ffi::Patrol::Retiring,
                },
            }
        })
    }
}

// SAFETY: unique versioned, pointer-free C symbol retained for zone validation.
#[unsafe(no_mangle)]
pub extern "C" fn eqemu_rust_bridge_v1() -> u32 {
    42
}

#[cfg(test)]
mod tests {
    #[test]
    fn fault_child() {
        if std::env::var_os("ACTOR_FAULT_CHILD").is_some() {
            super::boundary(|| panic!("isolated boundary fault"));
        }
    }

    #[test]
    fn internal_fault_is_fail_stop() {
        let status = std::process::Command::new(std::env::current_exe().unwrap())
            .args(["--exact", "tests::fault_child", "--nocapture"])
            .env("ACTOR_FAULT_CHILD", "1")
            .status()
            .unwrap();
        #[cfg(unix)]
        {
            use std::os::unix::process::ExitStatusExt;
            assert_eq!(status.signal(), Some(6)); // SIGABRT, not an unwound test failure
        }
        #[cfg(not(unix))]
        assert!(!status.success());
    }
}

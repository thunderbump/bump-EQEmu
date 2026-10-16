# Bounded logical actor proof

`core` is deterministic, dependency-free safe Rust (`forbid(unsafe_code)`).
`bridge` exposes that policy as CXX-owned values and an opaque `rust::Box`.
This is **not** native NPC creation, movement, position inspection or gameplay.
The zone startup caller constructs, creates, inspects, retires and shuts down a
logical actor, then releases the box. The registered `ActorRuntimeTest` uses the
same API, including a fake native move-result trace. The pointer-free
`eqemu_rust_bridge_v1` marker remains 42 for existing zone-link validation.

## Protocol and bounds

- One runtime holds at most one logical actor and one outstanding move. No
  broker, scheduler, callbacks, I/O, native pointers or background work.
- `advance(sequence, tick, inputs)` accepts **zero or one** owned input and
  returns exactly one owned report, containing **at most one** event/effect.
  Excess input is rejected atomically with `ExcessInput`, not truncated. Work
  and retained state are constant; there are no output queues or histories.
  C++ must bound its own vector construction to one input (including allocation
  before entering Rust); oversized vectors are refused before conversion.
- `sequence` strictly increases and `tick` is nondecreasing. There is no wall
  clock or implicit timeout. Empty advances only update these ordering fields.
  Refusals leave all state, including ordering, unchanged. No counter wraps.
- Caller supplies a zone epoch unique across runtime reconstruction/zone
  lifetimes. Identity is `(epoch, generation)`; the single slot needs no index.
  Create uses the current tombstone generation (initially zero), and returns the
  incremented generation. All subsequent inputs must match it. Epoch/generation
  mismatch is `Stale`, including native results for a retired/replaced actor.
  Generation exhaustion refuses creation without modifying the tombstone.
- Moves require a positive, strictly increasing operation ID across this runtime,
  even across actor replacement. One pending move includes its operation and
  phase. Coordinates are owned integer request values, not an observed position.
  Results must carry the exact identity and pending operation. Report coordinates
  are meaningful only for `MoveRequested`; other reports do not observe position.
  A refusal has no event/effect, regardless of its echoed correlation fields.

| Input | Preconditions | Output / new state |
| --- | --- | --- |
| Create | no live actor | Created / live, next generation |
| Move | live, idle, fresh operation | MoveRequested / Requested |
| Admitted (native input) | Requested | Admitted / Admitted |
| Rejected (native input) | Requested | Rejected / idle |
| Completed (native input) | Admitted or Stopping | Completed / idle |
| Stop | Admitted, matching operation | StopRequested / Stopping |
| Interrupted (native input) | Admitted or Stopping | Interrupted / idle |
| Retire | live and idle | Retired / logical tombstone |
| Shutdown | no live actor | Shutdown / closed |

Admission is not completion. Stop is a requested cancellation, not an assertion
that a native body stopped. In this minimal protocol, stop before admission is
refused; the adapter must first admit or reject the request. Once stop is
requested, a correlated completion may still arrive: native may have finished
and queued it before receiving the stop request. Both Completed and Interrupted
are valid terminal outcomes while Stopping; the adapter reports what actually
happened. Either clears pending work and preserves the truthful terminal fact.
Late duplicate completion/interruption is refused, never rewritten as a second
terminal event. Duplicate, wrong-operation and out-of-phase results are
`InvalidResult`. Unknown CXX enum values are `InvalidInput`.
Missing/live/busy/closed conditions are typed refusals.

Retire and shutdown **refuse** active work instead of generating a burst of
terminal/removal outputs. Finish/reject/interrupt the move, retire, then shut
down. There is no output capacity to overflow and no silent eviction: each
accepted fact is returned by value to its caller before another call is possible.
`inspect` exposes the live/tombstone generation, pending phase/operation, closure,
ordering and most recent move terminal event/operation. The latter remains
inspectable across retirement and recreation until a subsequent move terminates.
Older returned reports belong to the caller; retaining a full audit history is
not this runtime's responsibility. A Retired event means logical retirement only,
not proof that any native body has disappeared.

## FFI ownership and faults

CXX generates the glue and compiles its C++ exactly once via `build.rs`. Native
CMake targets only include generated headers and link Cargo's static archive.
The existing Linux x86-64/non-cross guard and external Cargo target-directory
ownership are retained; unsupported platforms get no bridge references.

C++ owns the box and serializes all calls (including inspect/destruction); it
must not alias a mutable call or reenter Rust. Input vectors transfer ownership;
reports/snapshots copy fixed-width values without borrowed strings or pointers.
Native entity pointers remain exclusively in C++. Native results enter only on a
later advance, never via callbacks. Drop releases Rust memory and does not
synthesize policy events: consumers must explicitly finish, retire and shut down
before dropping a live runtime if they require those facts.

Expected refusals are values, never exceptions. All callable Rust operations
catch Rust panics and **abort the process** rather than resume partially mutated
state or unwind into C++. Allocation failure is also fail-stop. The opaque type
has no custom destructor or fallible drop logic. The scalar marker cannot panic.
No C++ function is called from Rust; C++ allocation/argument-construction
exceptions stay on the native side and must never be manually thrown through
Rust frames. The small hand-written unsafe surface is the unique, versioned,
no-pointer `unsafe(no_mangle)` symbol; CXX owns all generated unsafe glue.

## Test meaning and execution

- Core unit tests and the doctest cover logical creation, admission, completion,
  rejection, stop/interruption and stop racing queued completion (including
  excess-input refusal/retry and late results), retire/shutdown, ordinary refusals,
  stale epochs and replacement identities, wrong operation/order, exhausted generations and
  excess inputs while preserving inspectable terminal state. Busy refusals bound
  effects instead of silently dropping terminal/removal facts.
- The bridge's isolated subprocess panic test expects process abort, not a Rust
  test failure unwinding through the boundary.
- The registered native utility round trip constructs and releases a real CXX
  box, exercises the move/result protocol including completion after stop,
  inspects it, and checks stale/overflow refusals. It proves ABI/ownership and
  logical policy, not native mechanics.
- Zone startup uses the actual runtime API in addition to the retained marker.
  It is bounded plumbing only, with no world-connected gameplay claim.

Do not run Cargo, rustc, build scripts, macros, tests, native binaries or a zone
on the implementation host. The ordinary isolated fixture owns offline locked
format/Clippy/unit/doctest checks, native build, final-zone symbol/hash checks and
registered native utilities. No dependencies or locked versions were added.
Candidate validation is pending until that fixture runs; no incremental-build,
fresh-consumer, body/movement or gameplay qualification is asserted here.

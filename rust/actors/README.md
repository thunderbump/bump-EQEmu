# Bounded actor runtime and native body slice

`core` is deterministic, dependency-free safe Rust (`forbid(unsafe_code)`).
`bridge` exposes that policy as CXX-owned values and an opaque `rust::Box`.
The original logical protocol below remains available. `CreateBody` additionally
requests one native NPC through the zone-owned adapter described below; no movement,
position inspection or gameplay qualification is implied.
The zone startup caller constructs, creates, inspects, retires and shuts down a
logical actor, then releases the box. The registered `ActorRuntimeTest` uses the
same API, including a fake native move-result trace. The pointer-free
`eqemu_rust_bridge_v1` marker remains 42 for existing zone-link validation.

## Native body lifetime slice (candidate; VM validation pending)

`zone/actor_body.*` consumes `BodyRequested`/`RemovalRequested` effects after the
owned Rust call returns. C++ alone allocates/registers NPCs, owns their type data,
looks up entities, depops them and observes removal. The next serialized normal
zone iteration supplies one owned native fact to Rust. There are no callbacks,
mutable Rust reentry, retained entity pointers, result queues or alternate CLI.
The normal zone loop drives both the adapter and the existing source registry.

| Input | Body state / output |
| --- | --- |
| CreateBody | next generation, Requested / BodyRequested |
| BodyPresent | Requested -> Present / BodyPresent |
| BodyRejected | Requested -> Absent, logical tombstone / BodyRejected |
| Retire | Present -> Removing / RemovalRequested (not Retired) |
| BodyAbsent | Present or Removing -> Absent, logical tombstone / BodyAbsent |

Presence is checked against **both** native NPC and mob registries. Absence is
not inferred from `Depop`, logical retirement or a missing NPC index alone.
Creation, retirement and shutdown refuse while admission/removal is pending.
Travel is deliberately refused for native bodies in this slice. Native facts
cannot be submitted as adapter commands. All facts/commands carry Rust epoch and
generation, and every native lookup also checks the stamp stored in the NPC.
Reused entity IDs are not capabilities. Adapter reconstruction allocates a fresh
nonzero, process-local epoch without wrapping; the zone thread owns allocation.
The legacy startup-only logical runtime reserves epoch zero and never owns native
bodies.

Shutdown closes adapter admission before zone shutdown work begins. The zone
destructor clears the entity list, then observes native absence and closes Rust
before releasing its box. A partial native registry or surviving owned body is
not converted into a successful absence acknowledgement. Ordering exhaustion is
fail-stop, not wraparound. Adapter teardown can also drain through ordinary
entity processing (used by the registered test without unloading its fixture).

### Explicit decision control

NPC actor stamps enable an actor-only mode, independent of `AI_Stop` and
`IsAIControlled`. Native `NPC::Process` remains active. Actor bodies cannot enter
`Mob::AI_Process`, voluntary AI movement/waypoint setup, AI spell selection, or
engaged/disengaged AI event decisions. Guard-facing and outgoing/incoming assists
are gated outside AI processing; damage-triggered flee checks and tick fleeing
are gated too. Ordinary unmarked NPCs take their existing paths. This body-only
mode admits **no voluntary action**, including in unknown charm/fear/forced-control
states; it does not guess that these states permit Rust actions. Native forced
movement/status handling remains native, not reimplemented by the adapter.

The candidate in-server case independently checks registration, native process
counts, a real native stun timer expiring, ordinary-neighbor stun expiry and AI
decision-loop entry, retire/absence, external depop/absence, forced native-ID
reuse, generation refusal, and epoch refusal after runtime reconstruction with
the same native ID **and generation**. It checks admission closure and actual
native absence during teardown drain. A test-only free-ID reorder preserves the
allocator's IDs and refuses registered IDs; it is compiled only with
`EQEMU_BUILD_TESTS`. In an idle empty zone, the opt-in scenario temporarily uses
the existing quest idle override so the **ordinary** entity loop processes both
subjects, restoring the original setting on success, failure and deadline.
Owned subjects are depopped on assertions/deadlines; the normal loop and fixture
shutdown own destruction. The bounded complete-result interface is unchanged.

Unproved: movement/routes and stop/stun/root travel effects, combat, spells/buffs,
regen, charm/fear behavior, persistence, client delivery and all other gameplay.
The stun test proves only native status/timer processing, not all mechanics.
The registered drain test invokes production adapter teardown methods but does
not itself unload the world-connected zone. The real zone shutdown hook asserts
absence after entity-list clearing; actual fixture shutdown observation and
exact-head VM checks/review are still required. No candidate builds or tests have
been run on the implementation host. No additional environment service or Stack
scenario selection is needed.

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
  are meaningful only for `MoveRequested` or `BodyRequested`; other reports do not observe position.
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

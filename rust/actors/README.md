# Bounded actor runtime and native body slice

`core` is deterministic, dependency-free safe Rust (`forbid(unsafe_code)`).
`bridge` exposes that policy as CXX-owned values and an opaque `rust::Box`.
The original logical protocol below remains available. `CreateBody` additionally
requests one native NPC through the zone-owned adapter described below. The R2
candidate adds bounded native travel; qualification remains pending (see below).
The zone startup caller constructs, creates, inspects, retires and shuts down a
logical actor, then releases the box. The registered `ActorRuntimeTest` uses the
same API, including a fake native move-result trace. The pointer-free
`eqemu_rust_bridge_v1` marker remains 42 for existing zone-link validation.

## Native body lifetime slice (R1 accepted; retain regressions)

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
Travel now requests later native admission for Present bodies only. Native facts
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
mode bypasses native voluntary AI. The R2 movement lane admits only explicitly
checked free-control bodies; charm/fear/other suspended states do not permit
Rust movement. Native forced
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

R1 alone did not prove movement/routes or stop/stun/root travel effects.
Still unproved by this candidate until VM qualification: travel, combat, spells/buffs,
regen, charm/fear behavior, persistence, client delivery and all other gameplay.
The stun test proves only native status/timer processing, not all mechanics.
The accepted R1 run exercised actual zone teardown/replacement as well as the
registered drain test. Retain those regressions; R1 acceptance does not qualify
this movement candidate. No candidate builds or tests have been run on the
implementation host. No additional environment service or Stack scenario
selection is needed.

## Native movement candidate (R2; qualification pending)

`Adapter::Send` accepts Move/Stop requests, not native results. Rust allows one
fresh operation for a Present body, returns MoveRequested, and later consumes a
correlated Admitted or Rejected fact. Coordinates remain requests, not evidence.
The adapter resolves the stamped body on every access; the movement manager owns
native commands and erases its entry on native destruction. No entity pointer is
retained in Rust or in the adapter. External absence must first reject requested
work or interrupt admitted work, then acknowledge BodyAbsent; absence cannot
silently erase a pending operation. Teardown likewise drains terminal/absence
facts before releasing Rust.

The source-owned movement-manager lane validates a short, dry, direct route
against native maps and navmesh. It refuses missing map/nav/water data, partial
or stuck routes, teleport/portal nodes, water, long detours and out-of-scope
routes. It queues existing native MoveTo/StopMoving commands, never generic stuck
handling, Teleport or logical-coordinate position writes. Generic voluntary
NavigateTo is barred for actor bodies; ordinary NPC behavior is unchanged.
A forced native Teleport cancels this lane rather than counting as completion.
Movement remains native interpolation, speed, packets, position updates and
normal world processing, not Rust mechanics or a second AI.

Admission and every movement-manager frame require an AI-controlled, living,
standing, unowned/unengaged actor without stun, root (including pseudo/permanent
root), mez, fear, blindness, charm or casting. Unsupported/control-blocked states
reject new travel or interrupt queued travel; there is no automatic resume or
fallback action. Stop clears queued displacement through native StopNavigation.
The adapter waits for native nonmoving state and the latched movement result;
Completed additionally requires observed native arrival. A completion already
latched before Stop is not rewritten as cancellation. Retire still refuses active
work; the normal loop observes actual absence after depop.

### Source coverage and candidate bounds

The existing complete registry retains R1 lifetime, reused-ID, generation/epoch,
ordinary-neighbor and real zone-replacement cases. Seven additional normal-world
cases cover arrival, deliberate stop, native timed stun, native Root spell effect,
a real local map obstruction, external depop during travel, and rejected admission
under native mez/control suspension. Each uses one body,
one operation and a neighboring ordinary NPC (native stun expiry and AI decision
entry). Cases independently sample real coordinates/moving flags/AI counters,
check plausible per-frame displacement, no displacement after stop/control
action and post-terminal stability, and require
native retirement/absence. Root uses the fixture spell data's spell 230 and fails
if it does not really root. Missing route/status/outcome is failure, not skip.
The blocked case must find a nearby real obstruction; an out-of-range request is
not used as a substitute. Core/CXX tests cover out-of-phase/wrong-operation facts,
duplicate results, pending disappearance, and truthful completion after Stop.
The adapter refuses caller-injected native facts and stale move identities.

Current constants are **provisional safety/test bounds, not measured evidence**:

| Bound | Candidate value |
| --- | --- |
| Locality | 4–80 units, vertical difference <= 6, <= 32 nav nodes |
| Detour | native route length <= 1.5 times direct distance |
| Test clear route | rounded fixture safe point to +40 X; native ground Z + body offset |
| Arrival | XY <= 2, Z <= 6 |
| Stop/interruption terminal | <= 1000 ms from action |
| Post-terminal drift | <= 0.05 units over >= 400 ms |
| Case deadline | 7500 ms (existing registry 10000 ms outer gate unchanged) |
| Sample jump limit | native speed * elapsed seconds * 0.4 * 1.45 + 2 units; <= 8 units absolute |
| Observability | <= 250 ms sample gap; arrival requires >= 3 displaced samples |

Before teardown, stderr records native route endpoints, candidate bounds, elapsed
and action/terminal times, native arrival error/position, displaced sample count,
sampled distance, maximum step, phase and terminal correlation.
The bounded suite diagnostic also retains final native measurements, including
failures. No readiness, completion, cleanup or host-process preservation gate was
relaxed. Scoped idle override restoration and normal-loop owned depop cleanup
remain in place.

### Required qualification and locality decision

**Not yet accepted.** Run the existing ordinary isolated VM/GitHub pipeline at the
exact repaired head, retaining native diagnostics before teardown. Operations
must first measure the short clear route, its native speed/frame behavior,
arrival error, stop/root/stun latency and drift on the fixture baseline; record
those measurements and finalize the above bounds before accepting scenario
results. The safe-point route and nearby obstruction are candidate probes, not a
claim of a measured known-clear route. If either is unsuitable, select a measured
local route in EQEmu, not by changing Stack or the installed validator. Require
all ordinary checks, complete native suite, native/database/VM cleanup and review;
a crash, missing terminal or cleanup failure is non-pass. No host Cargo/native
execution or fixture run was performed for this implementation.

Decision: **keep locality narrow; do not expand mechanics or population** pending
that evidence. Combat, followers, persistence, inventory, dialogue, cross-zone
movement and unqualified forced-control mechanics remain deferred. No new
pipeline lifecycle, client dependency, external service or dependency was added.

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

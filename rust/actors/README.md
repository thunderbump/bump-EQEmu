# Bounded actor runtime and native body slice

`core` is deterministic, dependency-free safe Rust (`forbid(unsafe_code)`).
`bridge` exposes that policy as CXX-owned values and an opaque `rust::Box`.
The original logical protocol below remains available. `CreateBody` additionally
requests one native NPC through the zone-owned adapter described below. The R2
implementation adds bounded native travel with a limited measured fixture-route
calibration (see below).
The zone startup caller constructs, creates, inspects, retires and shuts down a
logical actor, then releases the box. The registered `ActorRuntimeTest` uses the
same API, including a fake native move-result trace. The pointer-free
`eqemu_rust_bridge_v1` marker remains 42 for existing zone-link validation.

## Opt-in one-shot native approach (candidate; native qualification pending)

Create a body with the existing `Send(CreateBody)` interface, wait for actual
`Body::Present`, then call `Adapter::StartApproach(std::vector<uint16_t>{...})`.
The vector is an authored scope of **zero to four distinct registered NPC
instances**, not a winner, position, eligibility flag or movement command.
Unknown IDs, clients, duplicate IDs and oversize scopes are rejected whole.
Ordinary zones do not opt in automatically. There is no new service or command.
Manual Move and patrol start refuse while approach is enabled (and vice versa).
The scope is immutable for this body's one-shot goal: it cannot acquire an NPC
merely because that NPC later reuses a scoped entity ID. To author a different
scope, retire the body and explicitly create/start a new one.

On each ordinary adapter Process while Observing or Travelling, C++ resolves
only these four-or-fewer identities against both native registries. It copies a
complete owned snapshot including explicit absence, actor/subject positions,
HP/presence, targetability, ownership (including swarm owner), engagement and
Rust-body identity. No pointers cross CXX. Native facts and StartApproach cannot
be supplied through Send. NPC construction assigns a process-local monotonic
nonzero 64-bit incarnation, independent of entity ID, address, spawn timestamp
and tick. It never wraps (exhaustion is fail-stop), survives adapter reconstruction,
and needs no retained pointer/map or destruction callback. Process restart cannot
retain old in-memory actors or messages. All candidate references use ID **and**
incarnation; actor epochs/generations, goal counters and fresh operation counters
independently protect replacements and resumes.

Safe Rust owns eligibility and selection: exclude self, other Rust actor bodies,
clients, absent/dead/untargetable/owned/engaged candidates; choose nearest by XY
within **30 XY units and 6 Z units**, then smaller incarnation on ties, regardless
of input order. Observations use millimetre integer coordinates (rounded from
finite native coordinates, bounded to +/-1,000,000 units), at most four entries,
with duplicate/zero incarnations rejected. Snapshot sequence strictly increases,
tick cannot decrease, and epoch/generation/goal must match. A rejected snapshot
cannot advance policy or preserve new authority; production coordinate failure
explicitly pauses/drains instead of using partial observations. Scope absence is
never truncation or a cached eligible fact.

Rust retains the selected incarnation and original observed position. It chooses
an integer destination two XY units short of the target along the observed line.
The existing native short dry local lane still owns geometry/admission, speed,
ordinary movement and actual arrival (XY <=2, Z <=6 from that destination).
The approach success check additionally requires fresh actual body-to-target
**XY <=5, Z <=6**. The XY radius includes two-unit standoff, integer rounding
and native arrival tolerance; the Z check is independently strict, so a native
completion outside the target radius pauses rather than inventing success.
A body initially inside this radius can latch Reached from native observations
without unnecessary travel. There is no pursuit or fallback route/target retry.

Transitions:

* Observing without an eligible subject is quiet; bounded fresh snapshots can
  discover an initially ineligible scoped NPC becoming eligible.
* Travelling retains one operation and one target. Before consuming movement
  facts, refresh perception. Absence/ineligibility or drift from the selected
  position beyond **1 XY or 1 Z unit** pauses and stops/drains native travel.
  Requested travel is rejected before admission. Native rejection/interruption
  also pauses, with no timer-based restart or automatic alternative target.
* Completed remains a truthful native terminal even after invalidation/pause;
  only a still-valid target and fresh actual arrival can latch Reached. Reached
  stays observable and quiet even if the subject later changes; no attack,
  aggro, casting, looting or automatic second selection occurs.
* Explicit `Send(Resume)` only accepts Paused after the operation drains. It
  increments the goal identity, clears selection, and waits for a new complete
  observation before reselecting. Reached/Retiring/absent/closed cannot resume.
* Pause/Stop disable approach scheduling. Retire and zone teardown disable it
  before native drain, retain the terminal and require actual absence before
  shutdown. Process does at most one bounded perception advance plus one native
  terminal/presence/absence or idle advance; travel cannot starve perception and
  perception cannot starve a terminal. There are no queues, workers or I/O.

Deterministic Rust tests cover idle/new eligibility, ordering-independent nearest
and tie selection, exclusions, native-correlated quiet arrival, invalidation,
incarnation replacement, drift, explicit fresh resume, refusal/pause, bounds,
stale actor/goal/snapshot identities, terminal races and retirement/shutdown.
The CXX utility contract exercises owned observation conversion and resumed
operation/goal identities. Existing manual/patrol tests remain intact.

`rust_native_actor_approach` adds an EQEmu-owned public PoK scenario: three ordinary
owned NPC subjects, initially no target, two newly eligible alternatives and a
closer untargetable exclusion; production scope opt-in; independently sampled
ordinary native travel and arrival; >=400ms reached quiet; actual untargetability
and disappearance during subsequent travel; <=1000ms stop and >=400ms quiet
with <=0.05 drift; fresh explicit resume; forced native ID reuse without scope
authority; active-mode teardown. It never supplies perception, chooses a target
command, steps movement or manufactures a terminal. It retains native XY<=2/Z<=6
arrival, <=250ms sample gaps and displacement bounds. Subjects are constructed
at independently queried native ground heights plus their race/size Z offset,
not at the safe point's Z; invalid ground or elevations outside the existing
six-unit local Z bound fail setup. Arrival checks both the rounded destination
Z and actual subject Z. This also applies to the ID-reuse subject spawn.
Its 9500ms internal deadline
fits the unchanged 10000ms per-case gate: short <=26-unit local legs plus bounded
100ms idle and 400ms quiet windows. Diagnostics remain <=256 bytes and the whole
suite retains its existing 4096-byte fail-closed report limit. The format/name,
patrol, lifetime, ordinary-neighbor, movement and cleanup regressions are unchanged.

**Reported baseline fixture result (`35bdd733`): failed.** Rust checks, native
build/link and 87 utilities completed successfully; 14 of 15 in-server cases
passed. Approach failed at subject-ground setup before any travel samples,
so approach/invalidation/reuse behavior was not qualified. That run reported
VM, normal-world and database cleanup complete. Setup incorrectly required
subject ground plus offset to match safe-point Z within one unit; the repair
constructs grounded subjects instead, without widening movement/arrival gates.
An advisory repair-snapshot check completed Rust format/dependency/Clippy/tests
and cleanup; native compilation was unavailable for the changed header/bridge
scope. No repaired native approach measurement is claimed. Exact-current-head
full native fixtures and independent review remain caller-owned acceptance
gates. This is a bounded noncombat
observation/movement slice, not population, hunting, combat, general pursuit or
navigation, persistence, or a performance guarantee. PoK combat policy is unchanged.

## Opt-in two-point patrol (current candidate; not yet native-qualified)

`ActorBody::Adapter::StartPatrol(ax, ay, az, bx, by, bz)` creates one body
at A and configures B once. No ordinary zone starts a patrol automatically.
The owned-value equivalent is `CreateBody(A)` followed by `StartPatrol(B)`;
all controls carry the current epoch/generation. Endpoints must be distinct,
nearby and admissible to the existing native lane. Native admission remains a
later fact, not a successful start guarantee.

Rust stores two endpoints, the next endpoint, and `Patrol` state. Only an empty
advance selects one fresh operation. Presence precedes the first B request;
correlated Completed flips the endpoint, and a **later** empty advance requests
the opposite leg. Terminal reports and `last_terminal/last_operation` remain
visible before the next request. Every adapter advance (commands, facts and idle
policy steps) consumes effects through the same native handler.

`Send(Pause)` disables scheduling and stops admitted travel through the ordinary
native StopNavigation lane. Requested-but-not-admitted travel is rejected on the
next normal advance without navigation. Interrupted or Rejected also enters
Paused. No timer or control expiry resumes policy: `Send(Resume)` is required,
and a subsequent refusal pauses again without retries. Stop on admitted patrol
travel also pauses. Manual Move is Busy while a patrol is enabled; manual-only
actors retain the original protocol.

`Send(Retire)` latches Retiring even during active travel, stops/drains the
operation, preserves its native terminal, then requests removal on a later idle
advance. Actual absence is required before shutdown. Resume cannot reopen a
retiring, absent or closed actor. Teardown disables scheduling before draining;
external absence never respawns. Identity checks protect replacement bodies.

Source coverage adds deterministic core tests, a CXX contract test, and
`rust_native_actor_patrol` in the complete native registry. The native case
configures the route once, samples A-to-B and B-to-A independently, interrupts
the third leg with a real timed stun, observes a quiet window through expiry,
explicitly resumes real travel, retires to actual absence, then exercises active
patrol adapter teardown with a fresh epoch. Existing zone-shutdown, native-ID
reuse and ordinary-neighbor regressions remain selected. Per-leg operation,
elapsed time, native position and sampled distance are retained with bounded
latency/drift/sample diagnostics. Existing gates and deadlines are unchanged.

**Measured loop result: pending caller-owned current-head native fixtures.**
The advisory development check ran Rust tests and Clippy successfully on an
intermediate snapshot; it could not build native objects for the changed bridge
and header scope. This is not native acceptance. No A-to-B-to-A travel result,
complete-report budget observation or cleanup qualification is claimed yet.
Qualification remains one actor on the short public PoK route, not population,
combat, followers, general navigation, persistence or performance.

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
R2's limited travel calibration is recorded below. Combat, general spells/buffs,
regen, charm/fear behavior, persistence, client delivery and other gameplay remain
unqualified.
The stun test proves only native status/timer processing, not all mechanics.
The accepted R1 run exercised actual zone teardown/replacement as well as the
registered drain test. Retain those regressions; R1 acceptance does not qualify
this movement candidate. No candidate builds or tests have been run on the
implementation host. No additional environment service or Stack scenario
selection is needed.

## Native movement (R2; limited fixture-route calibration)

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
The native stop command preserves the actor's last position instead of applying
an extra FixZ height snap after cancellation; ordinary NPC stop correction is
unchanged. This also applies to native control interruption and teardown stops.
The adapter waits for native nonmoving state and the latched movement result;
Completed additionally requires observed native arrival. A completion already
latched before Stop is not rewritten as cancellation. Manual-only Retire still
refuses active work; enabled patrol retirement drains it as described above.
The normal loop observes actual absence after depop.

### Source coverage and retained bounds

The existing complete registry retains R1 lifetime, reused-ID, generation/epoch,
ordinary-neighbor and real zone-replacement cases. Eight additional normal-world
cases cover arrival, deliberate stop (both moving and admitted before the first
movement tick), native timed stun, native Root spell effect,
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

Two additional native regressions cover the adapter boundary rather than injected
Rust facts. `rust_native_actor_latched_stop` interposes a real Stop after the
normal movement manager latches Completed but before the adapter consumes it;
it requires StopRequested/Stopping, an unchanged native latch and position,
then exactly the correlated Completed outcome and post-terminal stability.
The test-only pre-fact hook never drives movement or injects results.
`rust_native_actor_reused_travel` externally depops a moving body, waits for
Interrupted and actual absence, reuses its native ID, and verifies that the new
body has no retained navigation. It rejects old-generation Move/Stop and wrong-
epoch commands while replacement travel is pending, then requires real arrival
for the replacement's fresh operation. Both retain ordinary-neighbor and cleanup
assertions. The reused-ID case samples the replacement's native position
immediately after creation and requires it to remain idle within 0.05 units for
at least 400 ms before fresh travel. It does not compare that position with the
old body's float origin: CreateBody uses integer coordinates, so reconstructing
from a native observation can change the spawn position through truncation.
Terminal identity/outcome must remain stable through the settle window.

The following **conservative safety/test bounds are retained unchanged** for the
limited fixture-route calibration recorded below. Bounds are acceptance limits,
not measurements or guarantees for other routes.

| Bound | Retained value |
| --- | --- |
| Locality | 4–80 units, vertical difference <= 6, <= 32 nav nodes |
| Detour | native route length <= 1.5 times direct distance |
| Test clear route | public PoK fixture: (-285, -148, -158.90) to requested (-245, -148, -156); native ground Z + body offset |
| Arrival | XY <= 2, Z <= 6 |
| Stop/interruption terminal | <= 1000 ms from action |
| Post-terminal drift | <= 0.05 units over >= 400 ms |
| Attempt deadline | 7500 ms; all repetitions share the unchanged 10000 ms registry deadline |
| Sample jump limit | native speed * elapsed seconds * 0.4 * 1.45 + 2 units; <= 8 units absolute |
| Observability | <= 250 ms sample gap; arrival requires >= 3 displaced samples |

The source-owned suite collects three observations each for arrival, deliberate
Stop, stun and root, and one for the other movement scenarios. Each repetition
runs the same real route, verifies normal-loop retirement and actual absence,
restores the idle override, then reconstructs the runtime and creates a fresh
body. All repetitions share the existing 10-second case deadline; a failed
attempt fails that scenario immediately. The current suite selects 13 scenarios including the opt-in patrol candidate.
No native movement processing is called by the test.

Successful movement diagnostics use this compact `m1` notation within the
existing 256-byte diagnostic and 4096-byte suite report limits:

```text
m1 r=start_x,start_y,start_z>target_x,target_y,target_z v=native_runspeed p=settled_x,settled_y,settled_z m=sample;sample;sample
sample = total_ms,arrival_xy_error,arrival_z_error,action_terminal_ms,max_drift,settle_ms,distance,max_step,max_gap_ms,max_action_drift
```

`r` retains the observed native spawn position and requested map-derived endpoint.
`v` is the effective native run-speed value at the first route observation,
before the move request. It is not a measured travel velocity.
Repeats must have identical native origins and requested endpoints. `p` is the
last repetition's sampled settled native position. Arrival errors are measured
at settlement for arrival, latched completion and replacement arrival; other
cases use `-`. Action latency is `-` when no action-to-terminal interval was
measured. `max_drift` is the largest sampled displacement from the terminal
settlement position across the full `settle_ms` window. It is meaningful only
when that window contains a native body; external depop has no post-terminal
body to sample. Distance and maximum
step come from native position samples; `max_gap_ms` is the largest sampling
interval while a body exists. `max_action_drift` separately retains the largest
observed displacement from the action position before terminal settlement for
Stop, queued Stop, stun and root. Total time includes the settle and retirement
checks. Coordinates, speed, distance and step display two decimal places;
arrival errors and drift display three. Assertions retain native float precision,
so a displayed zero drift means less than 0.0005 units, not mathematical zero.
The complete report supplies the native run ID; the caller's existing report
and private evidence envelope supply candidate and worker-run attribution.

Failures retain the reason, stage, repeat number, terminal correlation, route
and current measurement before cleanup. Before terminal settlement, the latency
field shows elapsed action age. If unusually large coordinates exceed the
budget, the diagnostic explicitly marks the route `omitted-size` and keeps the
current measurements and terminal correlation. Stderr also retains per-attempt measurements and route traces.
A successful diagnostic that cannot fit fails explicitly rather than silently
truncating measurements. No report budget, readiness, completion, cleanup or
host-process preservation gate was relaxed. Scoped idle override restoration
and normal-loop owned depop cleanup remain in place.

### Accepted limited fixture-route calibration

Calibration attribution (not validation of subsequent edits):

- Candidate: `093a3b3128d3a02a12be060c20fbfc24bd87308f`.
- Fixture job: `45184ffa0e974a90`, finished 2026-10-23 at 04:00:17 UTC.
- Worker run: `291c45bb3feb4fafb16212afe651aed2`;
  native suite run: `725e1b407f049388b90fa7ae91b4d22c`.
- The PR reports all 10 checks and 85 utilities passing. The inspected private
  normal-world success artifact records exit 0, all 12 selected native scenarios
  completed with zero failures, finalized suite success, and normal-world,
  database and VM cleanup complete. Macroscope was skipped, not passed.

The PR's calibration follow-through records independent Spec/Standards acceptance
of this baseline for the short public Plane of Knowledge (`poknowledge`) fixture
route. The native observations below support that limited calibration, not a
blanket approval of the PR or later heads.

The repeated clear route starts at observed native position
**(-285.00, -148.00, -158.90)** and requests **(-245, -148, -156)**.
All three arrival repetitions settled at displayed errors XY **0.000**, Z
**0.250** units; the last settled position was **(-245.00, -148.00, -156.25)**.
The effective native run-speed value was **50.00**, not a measured velocity.
Sampled arrival distance was **40.18** units, maximum steps **1.50–1.69** units,
and maximum sample gap **34 ms**. No teleport fallback counts as travel.

| Scenario | Repetitions | Total times (ms) | Action-to-terminal (ms) | Max post-terminal drift (units) | Settle windows (ms) |
| --- | --- | --- | --- | --- | --- |
| Arrival | 3 | 1837, 1850, 1847 | n/a | 0.000 each | 419, 422, 419 |
| Deliberate Stop | 3 | 713, 716, 708 | 32, 33, 33 | 0.000 each | 420, 422, 418 |
| Native stun | 3 | 709, 713, 712 | 32, 33, 32 | 0.000 each | 420, 423, 423 |
| Native root | 3 | 709, 710, 710 | 33, 32, 32 | 0.000 each | 420, 419, 420 |
| Stop before first tick | 1 | 549 | 32 | 0.000 | 420 |

Stop, stun and root also each recorded **0.000** maximum action-to-terminal
drift in all repetitions; queued Stop recorded the same and zero sampled travel.
Displayed drift/errors use three decimal places: zero means less than 0.0005
units, not mathematical zero. Total times include settlement and retirement,
not just travel. The maximum sample gaps across movement scenarios were
33–34 ms.

The single latched-completion-before-Stop case took 1843 ms, with XY/Z errors
0.000/0.250 and 0.000 drift over 417 ms; its truthful Completed outcome was
preserved. Reused-ID travel took 2522 ms, including 64 ms to interruption of the
old body, idle replacement observation and fresh arrival; final XY/Z errors
were 0.000/0.250, with 0.000 drift over 422 ms. External depop took 65 ms to
terminal/absence; its reported zero drift is not post-terminal body evidence.
The blocked probe requested **(-230, -125, -156)** from the same origin and was
rejected with zero sampled travel, as was control-suspended admission on the
clear route. These single cases are not three-repetition baselines.

The existing arrival XY <= 2 / Z <= 6, action-to-terminal <= 1000 ms and drift
<= 0.05 over >= 400 ms limits remain unchanged. The observations lie comfortably
inside them; three fresh-body repetitions within **one VM** do not justify
fitting tighter thresholds or general timing/reliability guarantees. The locality,
route, jump, sampling and deadline gates above remain conservative safeguards,
not qualification of the entire 4–80-unit envelope or arbitrary navmesh paths.

Earlier runs remain historical and distinct: `06485b651fda4efb` passed the older
10-case suite at `9a29de1fa692fee5fca6ddc07e025529b44fd972`, while same-head
`ac69f13d2a314aac` failed responsiveness before suite completion. At
`15ca92c249ad044cd3a1b9d332d94133d1623d40`, `ae29a2eee3864010` failed the
reused-ID coordinate-baseline assertion; `9fc6d93884154e9b` later passed all
12 cases at `51079559b044d60873ec7946b1d8b71800771819` after its repair, without
this repeated calibration. Operator-stopped `83d884ca808a42ca` is not a scenario
failure. `734b3050208e4b61` reported a native UDP timeout at
`9b6e63a93e8716f9265a4d45130e45547fea4b25`. None substitutes for the attributed
calibration run or validates a subsequent edited head.

### Locality and validation decision

**Accept the measured baseline only for this short public PoK fixture route;
keep locality narrow and do not expand mechanics or population.** Three
fresh-body repetitions in one VM are not independent VM runs, general route
qualification or a reliability guarantee. Combat, followers, persistence,
inventory, dialogue, cross-zone movement and unqualified forced-control mechanics
remain deferred. No new pipeline lifecycle, client dependency, external service
or dependency was added.

The caller must validate and review the resulting documentation head through the
existing ordinary isolated VM/GitHub pipeline. Require all ordinary checks, a
complete native suite and native/database/VM cleanup; crashes, missing terminals
or cleanup failures remain non-pass. No host Cargo/native execution or fixture
run was performed for this documentation repair.

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
The attributed fixture result above validates its exact candidate only; later
edits still require caller-owned exact-head validation and review. No general
incremental-build, fresh-consumer or broader gameplay qualification is asserted.

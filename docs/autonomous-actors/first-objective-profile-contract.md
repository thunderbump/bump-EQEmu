# First Actor Objective And Profile Contract

Date: 2026-07-14

Wayfinder ticket: **Choose the first objective hierarchy and actor-profile contract**

## Decision

The first contract uses an immutable, versioned **Actor Strategy Registry**, a flat set of bounded **Actor Objective
Templates**, and an authored **Actor Routine** that selects one objective at a time. Objectives never contain, create,
or queue other objectives.

The **Objective Runner** owns deterministic state transitions. The activation scheduler decides when an objective has
execution capacity. The zone-owned executor applies bounded actions through ordinary gameplay and produces correlated
outcomes. Economy, chatter, activation, and materialization policies remain separate strategy or scheduling concerns.

## Registry and persistent state

The Actor Strategy Registry is operator-controlled code/config. Published Actor Routines, Actor Objective Templates,
Actor Profile Presets, and Party Danger evaluators are immutable; behavior changes publish new versions.

Persistent actors and objective instances reference exact registry versions. An active instance retains its original
versions until an explicit, observable migration or restart policy changes them. The first contract does not permit
database-authored phase graphs, live editing, planner-declared capabilities, per-actor trait overrides, or
self-modifying strategy promotion.

An objective instance persists at least:

- actor, objective, routine, template, profile-preset, danger-evaluator, and strategy versions;
- state (`active`, `recovering`, `replanning`, `completed`, or `abandoned`), phase, concrete payload, and terminal reason;
- pending action identity, attempt, action generation, idempotency/expiry data, and correlated event watermark;
- retry consumption, viability remaining, Actor Replan Request, recovery action/resume state, and replay watermarks;
- Objective Progress Lease, optional active-age cap state, deterministic seed, and decision sequence.

## Declared templates and availability

The first registry declares seven flat templates:

| Template | Desired state | First availability |
| --- | --- | --- |
| `DwellAtAnchor` | Party remains visibly or coarsely at an authored safe anchor for a bounded dwell | First promotion target after dwell outcomes are proven |
| `TravelTo` | Party reaches one configured checkpoint or destination through repeated bounded movement/handoff actions | Disabled until nav reachability and cross-zone handoff are proven |
| `HuntOneAllowlistedTarget` | The selected, zone-owned allowlisted target has an authoritatively correlated death | First consequential promotion target in materialized Misty |
| `RecoverToThreshold` | Party reaches Party Readiness through ordinary recovery behavior | Live-authoritative first; coarse elapsed recovery requires separate proof |
| `AcquireOneItem` | One observed corpse item changes conserved custody | Disabled until the custody/loot seam is proven |
| `SellOneItem` | One conserved item-for-currency merchant transaction completes | Disabled until custody, wallet, and merchant authority are proven |
| `ResupplyOneItem` | One conserved currency-for-item purchase completes | Disabled until the shared economy authority is proven |

Declaration does not imply availability. An Actor Routine may instantiate a template only when every required action,
authority class, progress event, terminal result, and postcondition in the template has authoritative runtime proof.
Optional unavailable objectives are omitted before instantiation; they are never entered and allowed to stall.

The full-route authored routine is:

```text
TravelTo -> HuntOneAllowlistedTarget -> RecoverToThreshold -> TravelTo(home) -> DwellAtAnchor
```

`AcquireOneItem`, `SellOneItem`, and `ResupplyOneItem` join the routine only after their conserved seams are enabled.
This routine remains unavailable until `TravelTo`, including its cross-zone handoff, is proven.

The separately versioned first-promotion routine is intentionally local:

```text
MistyLocalPresenceV1: DwellAtAnchor -> HuntOneAllowlistedTarget ->
  RecoverToThreshold(when unready) -> DwellAtAnchor
```

It begins with an already-materialized Actor-led Party at an authored anchor in the bounded Misty hunting area and does
not instantiate or skip `TravelTo`. Before this routine can promote `HuntOneAllowlistedTarget`, authoritative
interruption recovery and correlated Party Readiness must be proven as supporting capabilities. This is required even
when a run does not select scheduled `RecoverToThreshold`: danger, interruption, or death can enter runner-owned
recovery from any active hunt. Travel and economy remain gated.

## Profile presets

The initial four-party roster uses one cautious, two steady, and one tenacious Actor Profile Preset. The preset values
are versioned tuning seeds for instrumentation, not permanent balance:

| Preset | Additional action retries | Party Danger tolerance | Party Readiness threshold |
| --- | ---: | ---: | ---: |
| `cautious` | 0 | 35 | 90% |
| `steady` | 1 | 60 | 80% |
| `tenacious` | 2 | 85 | 70% |

Each concrete plan receives its initial attempt plus the listed retries. Every objective begins with an Objective
Viability Allowance of `3`, independent of preset. Temporary deferral spends neither retry nor viability.

Party Danger is a normalized `0-100`, versioned, zone-owned signal from authoritative live party/combat state. The
Actor Profile cannot supply or override it. The first evaluator is intentionally reactive and naive; it does not
predict zone depth, route difficulty, or threats beyond current live evidence. Exact inputs and weights remain
instrumented strategy tuning.

## Outcomes, replanning, and abandonment

Every template uses the same transition contract:

| Outcome | Runner behavior |
| --- | --- |
| `succeeded` | Advance or complete only after the correlated postcondition is observed |
| `deferred` | Clear the concrete plan, create a correlated Actor Replan Request, and spend no retry or viability |
| `blocked`, `failed`, `expired` | Spend an action attempt; when retries are exhausted, spend one viability and require a fresh correlated plan |
| `interrupted`, `death` | Spend one viability and enter recovery while preserving active-versus-replanning resume state |
| stale, duplicate, or mismatched input | Leave objective execution state, the outstanding request, and budgets unchanged; an audit decision receipt or sequence may still advance |
| viability exhausted | Abandon the objective and return conserved state to the Actor Routine |

Templates define concrete payload schemas, postconditions, meaningful progress events, and deferred-versus-structural
classifications. The runner alone owns the common transitions. A fresh plan must match the outstanding replan request
identity, its phase, its current action-generation fence, and the exact concrete payload shape. If interruption occurs
while replanning, the runner advances the action generation and atomically refreshes the persisted request identity and
generation; a response correlated to the pre-interruption request remains stale.

Abandonment never deletes, teleports, silently dematerializes, or fabricates a successful return. The routine selects
`RecoverToThreshold` when the party is unready and recovery is possible, otherwise `DwellAtAnchor` at a configured safe
anchor, or an enabled `TravelTo` toward the nearest safe/home anchor. If none is executable, the scheduler defers at the
last conserved safe checkpoint and exposes the blocker. Capacity dematerialization remains a separate safe-boundary
decision.

## Recovery and committed combat

Recovery has two entry paths that share one authoritative capability:

- Runner-owned interruption recovery pauses the current objective, preserves whether it should resume active or
  replanning, and resumes only after correlated Party Readiness.
- Routine-owned scheduled recovery instantiates `RecoverToThreshold` as an ordinary next objective after another
  objective completes.

Party Readiness includes the Actor leader and every spawned Bot follower. Each must be present, alive, out of combat,
and above the preset threshold for HP and applicable mana. Endurance is deliberately ignored because its recovery is
not decision-relevant; pets are outside the first readiness contract. Dead or missing members yield explicit
blocked/interrupted results rather than being silently excluded.

An accepted engage action creates a **Committed Engagement**. Party Danger may prevent engagement or schedule recovery,
but the first contract does not invent mid-combat retreat. Ordinary Bot combat remains authoritative until observed
combat end, target loss, or death. A retreat/disengage action requires a later ordinary-gameplay seam.

Only the correlated death of the selected target satisfies `HuntOneAllowlistedTarget`. Player contention observed
before engage is `deferred`. Target loss or combat ending with the selected target still alive is `failed` unless the
terminal event names a separately recognized actor-level interruption, in which case it is `interrupted`. Party death
is `death`. None of these non-success outcomes satisfies the hunt postcondition.

Committed Engagement also fences replacement behavior. Action expiry or an Objective Progress Lease stall may record
stuck evidence while combat continues, but it cannot emit another consequential action, create a replacement replan,
or dematerialize the party before an authoritative combat terminal is observed. The runner classifies that terminal
outcome afterward, then applies the ordinary retry, recovery, replanning, or abandonment transition.

## Time and execution authority

Each template declares an **Objective Progress Lease** refreshed only by correlated meaningful progress. Movement
checkpoint arrival and active combat progress while fighting toward the objective may refresh it. Capacity deferral
pauses the lease.

Templates may also declare an optional longer active-age cap. Prototype caps may be short to expose stuck states;
instantiated live objectives may begin around six hours and tune upward or disable the cap where evidence supports it.
Individual actions retain shorter freshness expiry. `DwellAtAnchor` uses elapsed dwell as its intended postcondition.

Execution authority inherits the hybrid world decision:

- dwell and explicitly proven authored checkpoint progress may use coarse, reversible adapters;
- combat, loot, death, currency, merchant transactions, and first-pass recovery require live authoritative execution;
- travel handoff requires its separately proven conserved transition;
- a planner or template payload cannot self-declare an execution class or gameplay success.

## Consequences and next proof

The contract directly unblocks **Prototype an allowlisted Actor-led Party hunt action chain** and deterministic replay
strategy comparison. The first hunt proof uses an already-materialized level 1-3 Rivervale-aligned Actor-led Party in
Misty, the existing target allowlist, normal attack/visibility eligibility, and player-contention deferral.

Automated testing reuses the same deterministic state, correlated action/outcome evidence, fencing, and replay controls.
Those controls exist first to make persistent world Actors safe and understandable; test coverage is a consequence.

Prospective zone-depth or route-threat prediction, per-actor trait overrides, retreat, coarse recovery, generalized
faction reasoning, custody/economy authority, and live strategy mutation are not part of this first contract.

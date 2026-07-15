# Actor Behavior Evidence And Strategy Experiment Vocabulary

Date: 2026-07-14

Wayfinder ticket: **Define actor behavior evidence and strategy experiment vocabulary** (`central-dcq7.15`)

## Decision

Actor evidence exists first to make a populated world understandable and tunable. Deterministic replay and automated
tests reuse that evidence; they do not define its shape.

Use five deliberately different evidence classes:

1. immutable strategy and experiment provenance;
2. correlated **Actor Decision Records**, action lifecycle records, and **Actor Events**;
3. full-fidelity **Actor Economy Evidence** for conserved assets;
4. sampled or window-aggregated runtime measurements; and
5. derived metrics recomputed from the first four classes.

Do not collapse these into undifferentiated logs. A strategy choice is not a gameplay outcome, an accepted action is
not success, an aggregate is not conservation proof, and a sampled performance observation is not replay input.

The present repository already points toward this separation. Objective instances retain exact strategy versions,
action generations, event watermarks, replay watermarks, seeds, and decision sequence
([objective contract](https://github.com/thunderbump/bump-EQEmu/blob/60edb352d33101c22e24447cd6d5fddda032157e/docs/autonomous-actors/first-objective-profile-contract.md#L17-L33)).
The durable action queue already distinguishes source metadata, action payload, idempotency key, state, expiry, result,
and failure reason
([queue repository](https://github.com/thunderbump/bump-EQEmu/blob/60edb352d33101c22e24447cd6d5fddda032157e/common/repositories/actor_action_queue_repository.h#L15-L52)).
Actor events are already actor-keyed, cursor-readable records rather than player audit rows
([event schema](https://github.com/thunderbump/bump-EQEmu/blob/60edb352d33101c22e24447cd6d5fddda032157e/common/repositories/base/base_actor_events_repository.h#L20-L69),
[bounded cursor](https://github.com/thunderbump/bump-EQEmu/blob/60edb352d33101c22e24447cd6d5fddda032157e/common/repositories/actor_events_repository.h#L49-L105)).

This document defines a logical vocabulary and evidence contract, not a storage schema or runtime implementation.

### Current capability boundary

On pushed `master`, the in-memory harness recorder implements `spell_cast_started`, `target_changed`, and
`speech_emitted`, while only `speech_emitted` has a durable Actor persistence sink. The source is newer than the
expansion spike's opening inventory, which still says spell start is the only event. Treat current source as authority:
do not cite the proposed movement, damage, heal, death, handoff, economy, performance-percentile, or experiment records
below as implemented capabilities. The durable `actor_events` shape also lacks objective/action correlation columns;
those fields currently belong in the logical contract until a later schema decision.

There is no pushed Actor cross-zone handoff, Actor wallet/holdings settlement, tick-percentile sampler, evidence-drop
counter, or Actor Experiment Manifest schema. This research names the evidence each later proof needs; it does not
declare those gameplay capabilities available.

## Evidence classes

### Immutable provenance

Provenance answers, “Which behavior did this Actor run?” It is referenced by records rather than copied as a mutable
blob into every event.

An immutable strategy publication identifies:

- Actor Routine, Actor Objective Template, Actor Profile Preset, Party Danger evaluator, economy policy, chatter
  policy, and any materialization or scheduling policy by stable name and version;
- a canonical content digest of each selected definition;
- server repository commit, ruleset/config revision, content/data revision, and schema version;
- deterministic algorithm version and seed derivation version; and
- publication time and operator-approved status.

An active objective keeps the versions with which it began until an explicit observable migration. That follows the
existing **Actor Strategy Registry** rule that published definitions are immutable and live editing or self-promotion
is unavailable
([registry boundary](https://github.com/thunderbump/bump-EQEmu/blob/60edb352d33101c22e24447cd6d5fddda032157e/docs/autonomous-actors/first-objective-profile-contract.md#L17-L25)).

### Correlated decisions, actions, and outcomes

An **Actor Decision Record** explains one transition evaluation. It records the selected action or explicit no-action,
reason code, relevant bounded inputs or their stable digests, input watermarks, before/after objective-state digests,
and exact strategy provenance. It never claims the action occurred.

An action lifecycle record explains execution authority:

```text
requested -> accepted | rejected
accepted  -> started | terminal
started   -> progress* -> terminal
```

`accepted` means only that a bounded request passed ingress. The existing harness already requires a later correlated
Actor Event before treating an accepted target or speech request as complete
([request/observation loop](https://github.com/thunderbump/bump-EQEmu/blob/60edb352d33101c22e24447cd6d5fddda032157e/docs/autonomous-actors/actor-action-event-control-plane-spike.md#L29-L45)).

An **Actor Event** records authoritative or actor-observable world change. Events should arise at consequential
gameplay boundaries, not from every internal callback. The current recorder demonstrates a sparse envelope and a
bounded 512-record ring
([event shape and bound](https://github.com/thunderbump/bump-EQEmu/blob/60edb352d33101c22e24447cd6d5fddda032157e/zone/harness/actor_event_recorder.h#L26-L105));
its speech path truncates text to 160 bytes and persists after the gameplay-facing emission boundary
([speech recorder](https://github.com/thunderbump/bump-EQEmu/blob/60edb352d33101c22e24447cd6d5fddda032157e/zone/harness/actor_event_recorder.cpp#L344-L376)).

### Conserved asset evidence

**Actor Economy Evidence** is a ledger-like account of every attempted and completed item or currency custody change.
It records exact before/after custody, item fingerprint or copper delta, source, destination, authority, settlement key,
quote provenance when applicable, postcondition, and terminal outcome. It remains separate from recommendation
evidence such as `balanced-v1` disposition or Actor Offer Intent.

Asset evidence is never inferred from wallet balance, holdings value, or a success-shaped log message. The accepted
economy policy already requires full-fidelity mutation evidence and unsampled conservation failures
([economy evidence](https://github.com/thunderbump/bump-EQEmu/blob/60edb352d33101c22e24447cd6d5fddda032157e/docs/autonomous-actors/first-conserved-actor-economy-policy.md#L148-L162)).

### Performance measurements

Performance observations describe cost, not behavior truth. They may be sampled or aggregated into fixed windows:

- zone loop wall time and overrun count;
- zone process CPU, RSS/PSS, and cgroup memory;
- world process CPU and memory when actor routing adds work;
- database statement count, rows, bytes, batch size, and persistence latency;
- evidence queue depth, high-water mark, drops, overwrites, and serialized bytes;
- materialized Actor-led Parties, Bot followers, booted zones, and active resolution slots; and
- helper-process CPU, memory, planning duration, and action submission rate.

The main zone loop processes entity families, Mob processing, scheduled content, and `Zone::Process()` in one
gameplay-critical cycle
([zone main loop](https://github.com/thunderbump/bump-EQEmu/blob/60edb352d33101c22e24447cd6d5fddda032157e/zone/main.cpp#L595-L619)).
High-level planning therefore stays outside zone ticks, with the zone retaining only fresh validation and ordinary
gameplay execution
([planner boundary](https://github.com/thunderbump/bump-EQEmu/blob/60edb352d33101c22e24447cd6d5fddda032157e/docs/autonomous-actors/actor-planner-deployment-spike.md#L15-L43)).

### Derived metrics

Derived metrics are named formulas over retained evidence. Store the formula version and source-watermark range with a
materialized aggregate; do not present an aggregate as raw evidence. Recompute when a formula changes.

## Common evidence envelope

Every immutable record has these fields, regardless of physical storage:

| Field | Meaning |
| --- | --- |
| `evidence_schema_version` | Version of the envelope and payload contract. |
| `record_id` | Globally unique immutable record identity. |
| `record_kind` | Bounded vocabulary such as `decision`, `action`, `event`, `asset`, `performance`, or `manifest`. |
| `record_type` | Versioned type within the kind, such as `objective.terminal` or `chatter.delivered`. |
| `observed_at_utc` | Wall time for operator correlation; never the sole ordering authority. |
| `source` | Bounded producer name and source version. |
| `actor_id` | Durable Autonomous Actor identity when actor-scoped. |
| `actor_sequence` | Monotonic per-Actor evidence sequence when actor-scoped. |
| `authority` | `live`, `coarse`, or `derived`; payload cannot promote its own authority. |
| `provenance_ref` | Immutable strategy/config/content provenance identity and digest. |
| `payload` | Bounded, schema-versioned type-specific content. |

Add the following correlation and fencing fields whenever that boundary exists:

| Field | Required for |
| --- | --- |
| `objective_id`, `objective_generation`, `phase` | Objective decisions, progress, and terminals. |
| `decision_sequence` | Every Actor Decision Record. |
| `action_id`, `action_generation`, `attempt`, `idempotency_key` | Requested actions and their results. |
| `causation_record_id`, `correlation_id` | Linking a trigger to the records it caused without relying on timestamps. |
| `input_event_watermark`, `input_status_revision` | Decisions based on bounded Actor Events or actor status. |
| `zone_id`, `instance_id`, `zone_process_epoch` | Live-zone authority and entity references. |
| `materialization_generation` | Materialization, dematerialization, and cross-zone handoff. |
| `entity_ref`, `target_ref`, `target_generation` | Live targets whose numeric entity IDs can be reused. |
| `settlement_key`, `custody_version`, `item_fingerprint` | Item/currency mutations. |
| `experiment_id`, `manifest_version`, `manifest_digest` | Controlled comparisons and every execution of an Actor Experiment Manifest. |
| `experiment_run_id`, `run_iteration` | Every record produced by one **Actor Experiment Run**; the opaque run identity and manifest-scoped iteration distinguish repetitions of the same manifest. |
| `cohort_id`, `assignment_digest` | Cohort-scoped records and controlled comparisons. |

Names and free-form text belong in bounded payloads, not correlation keys. IDs are opaque. Timestamps help operators
but never replace actor sequence, action generation, materialization generation, transaction fence, or event watermark.

## Terminal vocabulary

Use the objective contract's existing terminal outcomes everywhere an Actor Action feeds an Actor Objective:

| Terminal | Meaning |
| --- | --- |
| `succeeded` | The correlated authoritative postcondition was observed. |
| `deferred` | The current concrete plan cannot proceed now; replan without spending retry or viability. |
| `blocked` | Structural or authoritative rejection that consumes an action attempt. |
| `failed` | Accepted/started work reached a non-success terminal that consumes an action attempt. |
| `expired` | The bounded action became stale before its postcondition. |
| `interrupted` | A distinct actor/world interruption entered recovery and spent viability. |
| `death` | Party death entered Actor Death Recovery and spent viability. |

These meanings and budget effects already form the common runner contract
([outcome table](https://github.com/thunderbump/bump-EQEmu/blob/60edb352d33101c22e24447cd6d5fddda032157e/docs/autonomous-actors/first-objective-profile-contract.md#L94-L111)).

Keep delivery classifications separate:

- `stale`, `duplicate`, `mismatched`, and `fenced` mean an input was ignored without changing objective budgets;
- `accepted` and `started` are nonterminal action lifecycle states;
- `indeterminate` is an asset-operation observation requiring its existing settlement fence, not objective success;
- `cancelled` is an execution fact and must map explicitly to one canonical objective terminal; and
- `dropped_evidence` describes telemetry loss and can never be an Actor Action terminal.

Every terminal records a stable reason code plus bounded human-readable detail. Metrics group by reason code, never by
free-form detail.

## Domain event vocabulary

### Objectives and action failures

Record in full:

- `objective.instantiated`, `objective.phase_changed`, and `objective.terminal`;
- `decision.made`, including emitted action or explicit no-action;
- `action.requested`, `action.accepted`, `action.rejected`, and `action.terminal`;
- `objective.progress_lease_refreshed`, paused, resumed, and stalled only when the lease state changes;
- `replan.requested`, `replan.accepted`, and rejected replacement classification; and
- retry and Objective Viability consumption as before/after values on the decision that consumed them.

Do not record a heartbeat decision every tick. If the state, selected action, budgets, lease, and meaningful reason are
unchanged, advance neither durable decision history nor derived counters.

### Travel, handoff, and materialization

Use bounded checkpoint progress rather than coordinate spam:

- `travel.requested` and `travel.checkpoint_reached` for authored checkpoint identity;
- `travel.terminal` with the common terminal vocabulary;
- `handoff.prepared`, `handoff.source_released`, `handoff.destination_claimed`, and `handoff.terminal` sharing one
  handoff identity and materialization generation;
- `materialization.requested`, `materialized`, `dematerialized`, `reconciliation_selected`, and terminal deferral;
- source/destination zone process epochs and conserved Actor/party state digests at handoff boundaries.

Never persist every path node, heading correction, movement packet, or raw coordinate tick. The existing event design
already recommends one high-level movement request and one terminal completion while suppressing internal path-node
details
([movement bounds](https://github.com/thunderbump/bump-EQEmu/blob/60edb352d33101c22e24447cd6d5fddda032157e/docs/autonomous-actors/actor-event-perception-expansion-spike.md#L69-L127)).

### Combat, death, and recovery

Record in full:

- `combat.engagement_committed` with selected target generation and participating party snapshot digest;
- `combat.target_lost`, `combat.terminal`, and the authoritative selected-target `death` event;
- `party.death`, `recovery.started`, `party.readiness_observed`, `recovery.rematerialized`, and `recovery.terminal`;
- bind/home anchor identity and possessions digest across Actor Death Recovery; and
- every Party Danger value that actually participates in a decision, with evaluator version and input digest.

Do not durably emit every hit, heal, hate update, or combat AI tick. Maintain bounded in-memory encounter accumulators
and emit a terminal `combat.summary` containing duration, damage/healing totals, party minimum HP/mana, target changes,
and progress-lease refresh count. A decision-triggering damage, readiness, danger, target-loss, or death boundary remains
full fidelity even if the high-rate combat detail that produced it is aggregated.

Only correlated selected-target death is hunt success; pre-engagement player contention is deferred, target loss after
Committed Engagement is failed, and party death is death
([hunt terminals](https://github.com/thunderbump/bump-EQEmu/blob/60edb352d33101c22e24447cd6d5fddda032157e/docs/autonomous-actors/first-objective-profile-contract.md#L133-L146)).

### Inventory, currency, pricing, and offers

Record every asset mutation as Actor Economy Evidence. Also record non-mutating policy decisions:

- `economy.disposition_recommended` with complete-party evaluation digest and economy strategy version;
- `economy.offer_intent_created`, expired, or cleared;
- `economy.quote_observed` with the Actor Merchant Principal, merchant, validity, authoritative price, pricing-calculation
  version, ruleset/config revision, and an immutable bounded quote-input snapshot and digest. The snapshot includes the
  principal's quote-time faction standing, Charisma, race, class, and deity; merchant identity, faction, and bounded
  pricing context; and item fingerprint, quantity, buy/sell direction;
- `economy.settlement_attempted` and terminal receipt/indeterminate result;
- `economy.asset_transfer` with equal and opposite custody/currency sides where conservation requires them; and
- `economy.capacity_deferred` with Actor Holdings Capacity before/required values.

Never sample asset mutations, settlement fences, custody conflicts, balance mismatches, or conservation violations.
Aggregated wallet and holdings metrics are useful but cannot replace those records.

### Chatter

Keep generated text separate from gameplay decisions:

- `chatter.opportunity` records the bounded trigger category and audience only when policy evaluates it;
- `chatter.decision` records selected authored/fallback strategy, suppression reason, cooldown/budget before/after, and
  template/provider/prompt version references;
- `chatter.requested` and `chatter.generation_terminal` cover asynchronous generation without treating generation as
  delivery;
- `chatter.delivered` records speaker, channel, audience scope, final bounded text or text digest, and delivery count;
- `chatter.stale_suppressed` records failed Current Interaction or other delivery-time fencing.

The current runtime already records final local say/emote emission and bounds text to 160 bytes
([speech event implementation](https://github.com/thunderbump/bump-EQEmu/blob/60edb352d33101c22e24447cd6d5fddda032157e/zone/harness/actor_event_recorder.cpp#L344-L376)).
Do not persist prompts, model internals, private context, or unrestricted generated blobs. Actor, Bot, and NPC chatter
must share trigger/delivery vocabulary even if only an Autonomous Actor has an objective correlation.

### Player contention

Contention needs no special priority policy, but evidence must explain when ordinary player presence changed an Actor
result:

- record `contention.observed` only when it changes selection, authority, or a terminal outcome;
- identify bounded resource kind (`target`, `corpse`, `merchant_stock`, `interaction`, or `zone_capacity`) and whether
  contention occurred before or after action acceptance;
- record only opaque player-presence correlation and counts, never player chat, account data, inventory, or identity
  details not needed for gameplay authority; and
- map the effect to the common terminal vocabulary. Ordinary merchant state may simply cause the existing transaction
  terminal; it does not require a special winner event.

### Runtime and zone cost

Performance records use fixed windows and bounded dimensions:

- `runtime.zone_window`: loop duration histogram, overruns, active players, materialized Actors, followers, entities,
  and evidence queue counters;
- `runtime.process_window`: CPU and memory for zone, world, planner/helper, and host/cgroup;
- `runtime.database_window`: actor statements, rows, bytes, batch count, failures, and latency histogram;
- `runtime.materialization`: boot/request/ready/defer duration and resolution capacity;
- `runtime.evidence_window`: records and serialized bytes by evidence class, enqueue/persist latency, queue depth,
  drops, and overwrites.

Measure matched actor-off and actor-on windows. Label workload shape (`idle`, `city_downtime`, `travel`, `combat`,
`economy`, or `chatter`) from the experiment manifest, not by guessing from CPU observations.

## Metrics and formulas

Every report includes numerator, denominator, observation duration, cohort size, formula version, source-watermark
range, and missing/dropped evidence count.

| Metric | Definition |
| --- | --- |
| materialized presence share | materialized Actor seconds / enabled Actor seconds |
| visible activity share | seconds in materialized travel, combat, economy, chatter, or city-downtime activity / materialized Actor seconds |
| objective success rate | `succeeded` objective terminals / all objective terminals |
| objective deferral rate | `deferred` action terminals / all action terminals |
| action terminal outcome share | action terminals in one canonical outcome / all action terminals; report every `succeeded`, `deferred`, `blocked`, `failed`, `expired`, `interrupted`, and `death` outcome separately |
| retry-consuming action failure rate | `blocked` + `failed` + `expired` action terminals / all action terminals; `interrupted` and `death` remain separate viability outcomes rather than action failures |
| meaningful progress rate | Objective Progress Lease refreshes / active objective hour |
| retry consumption | action attempts consumed / objective terminal; report distribution by template and strategy |
| active objective duration | active elapsed time excluding explicitly paused capacity deferral |
| travel completion rate | `succeeded` travel terminals / all travel terminals; report the remaining canonical terminal outcomes separately |
| travel completion duration | correlated logical elapsed time from `travel.requested` to `travel.terminal`; report distribution by terminal outcome, authored route/checkpoint set, and strategy version |
| handoff success rate | succeeded handoffs / handoff terminals |
| double-materialization violations | handoffs with simultaneous authoritative source and destination claim; target is zero |
| engagement success rate | selected-target succeeded combat terminals / Committed Engagements |
| party death rate | party deaths / materialized party hour and / Committed Engagement |
| recovery completion time | readiness/rematerialization terminal time minus recovery start |
| wallet net flow | authoritative Actor Wallet credits minus debits in copper |
| vendor proceeds | authoritative merchant-sale credits in copper |
| authoritative quote price | authoritative quoted copper / quoted item quantity for valid `economy.quote_observed` records; report distributions by quote-input digest, pricing-calculation version, ruleset/config revision, and strategy version |
| paired quote price difference | candidate authoritative quote copper per item minus baseline authoritative quote copper per item for valid quotes matched by quote-input digest, pricing-calculation version, ruleset/config revision, and validity boundary; also report `(candidate - baseline) / baseline` when baseline is nonzero |
| holdings concentration | top-N Actors' holdings value / total Actor holdings value, with valuation formula version |
| upgrade distribution | positive Bot Gear Value gained by Actor, party member, slot, and strategy |
| conservation violations | unmatched or imbalanced authoritative asset deltas; never estimated or sampled |
| chatter delivery rate | delivered messages / materialized Actor hour, split by Actor/Bot/NPC speaker kind |
| chatter suppression rate | suppressed chatter decisions / chatter opportunities |
| repetition rate | repeated normalized text digests within the manifest's repetition window / deliveries |
| contention impact rate | contention-caused deferred/failed terminals / actions for which contention was observable |
| zone loop p50/p95/p99 | quantiles of loop wall time from the same fixed sampling windows |
| loop overrun rate | loops above the configured budget / measured loops |
| marginal CPU | matched actor-on CPU seconds minus actor-off CPU seconds / materialized Actor hour |
| marginal memory | matched actor-on RSS/PSS minus actor-off RSS/PSS at equivalent workload |
| evidence volume | records and serialized bytes / Actor hour, by evidence class and record type |
| evidence loss rate | dropped or overwritten eligible records / attempted eligible records; required evidence loss is a violation |

“World feels populated” still requires human observation. Each controlled **Actor Experiment Run** therefore accepts a
bounded operator rating and note set for perceived presence, plausibility, noisiness, visible discontinuities, and
interference. Store the rubric version and blinded run label; do not convert subjective notes into fabricated gameplay
events.

## Actor Experiment Manifest

An **Actor Experiment Manifest** is an immutable experiment definition once an **Actor Experiment Run** begins. Its
digest is the experiment identity referenced by every **Actor Experiment Run** and every record it produces. It contains:

```text
manifest schema/version and immutable digest
hypothesis and primary/guardrail metrics
repository commit, schema, ruleset/config, content/data, and zone/nav revisions
all strategy names, exact versions, and content digests
baseline and candidate labels
Actor roster/profile/party definitions
cohort assignment algorithm, salt identity (not secret), assignment digest, and exclusions
initial snapshots, deterministic seed list, and replay ordering rules
zones, anchors, objective templates, workload, observation window, and stop rules
authority mode: live, coarse, hybrid, offline replay, or controlled zone
sampling, aggregation, retention, and evidence-loss policy
human-observation rubric and blinding, when applicable
```

An **Actor Experiment Run** is an observable execution of that definition, not a mutation of it. Each execution receives
a new durable `experiment_run_id` and the next manifest-scoped `run_iteration`, including retries and exact
repetitions. Record both boundaries at full fidelity:

- `experiment.run_started` binds the **Actor Experiment Run** identity to the manifest identity/digest, actual
  repository/config/content/schema and strategy provenance digests, authority and workload mode, initial snapshot and
  ordered-input/replay-bundle digests, seed-set digest, actual cohort roster/exclusions and assignment digest, planned
  observation window and stop rules, start wall time and logical-time origin, and starting source/Actor watermarks. When
  human observation is used, it also references the rubric version, blinded run label, and opaque observation-session
  identity.
- `experiment.run_terminal` links to the start record and records one stable **Actor Experiment Run** terminal
  (`completed`, `stopped`, `failed`, or `invalidated`) plus reason code; actual wall/logical timing and observation
  window; terminal watermarks by source, Actor, and cohort; attempted/accepted/dropped/overwritten/rejected evidence
  counts by class; required-evidence completeness; final state, output, replay-result, metric/report, and other produced
  artifact digests; and the bounded human-observation artifact/session reference when used. Any required-evidence loss
  or provenance/input mismatch makes the **Actor Experiment Run** `invalidated`, never a complete-looking comparison.

**Actor Experiment Run** records report observed execution facts. They cannot rewrite the manifest, assignment,
strategy publication, input bundle, or expected stop rules. Artifact references are immutable identities and content
digests, not mutable paths.

Use paired replay over the same initial snapshot, ordered inputs, and seeds when comparing pure decisions. Use matched
live cohorts or crossover windows when ordinary gameplay timing, players, pathing, or zone cost is part of the question.
Assignment is deterministic from the manifest and stable Actor identity; no Actor self-selects or self-promotes.

Comparison reports must:

- name exact baseline and candidate manifest digests and every included `experiment_run_id`;
- show cohort balance and exclusions before outcomes;
- compare identical metric formulas and authority classes;
- report denominators, missing evidence, effect size, and interval/dispersion rather than winner-only totals;
- separate replay, controlled-zone, production-like, and human-observation results; and
- require explicit reviewed promotion of a new Actor Strategy Registry version.

This follows the useful AFK pipeline convention that verification evidence belongs to an exact committed candidate:
the pushed pipeline source binds implementation evidence to the checkout HEAD
([exact-head normalization](https://github.com/thunderbump/afk-composable-pipeline/blob/87578dc0f2c49bb1f7f054335b36749f188c6a61/src/afk/review.py#L257-L315))
and refuses review before required validation evidence passes
([evidence gate](https://github.com/thunderbump/afk-composable-pipeline/blob/87578dc0f2c49bb1f7f054335b36749f188c6a61/src/afk/review.py#L52-L78)).
Actor experiments apply the same principle to exact strategy/config/content provenance, without importing AFK workflow
state into the game domain.

## Deterministic replay contract

The minimum replay bundle is:

- Actor Experiment Manifest and immutable strategy publications;
- the `experiment.run_started` record that binds this replay execution to its exact inputs and provenance;
- initial Actor Profile/objective/party/holdings snapshots and their digests;
- ordered decision-bearing observations and authoritative action outcomes;
- per-Actor event sequence plus objective/action generations and watermarks;
- deterministic seed and time inputs represented as logical elapsed values;
- expected per-step Actor Decision Record, emitted action identity, terminal classification, and next-state digest; and
- the resulting `experiment.run_terminal` record with terminal watermarks, completeness, and replay-result digest.

Replay consumes observations/outcomes, not derived metrics or performance samples. At each step it verifies input
watermarks, before-state digest, decision reason, zero-or-one emitted action, action generation, after-state digest, and
unconsumed stale/duplicate input behavior. A mismatch stops the replay at the first divergent decision sequence.

Wall-clock timestamps, entity pointer identity, database auto-increment order across Actors, CPU timing, asynchronous
delivery order not admitted by the recorded actor sequence, and performance samples are nondeterministic context. They
must not decide replay equality.

Economy replay can compare recommendations from conserved snapshots, but it cannot recreate a live asset mutation.
Asset correctness is checked by settlement keys, custody versions, receipts, and ledger reconciliation. Likewise,
combat replay may replay objective decisions from recorded authoritative outcomes; it does not pretend to reproduce
the complete server combat simulation from a terminal summary.

## Cardinality, retention, and sampling

### Never sample or silently drop

- Actor Experiment Manifests and strategy publications used by an **Actor Experiment Run**;
- every observation that participates in an Actor Decision Record;
- objective/action decisions, generations, fences, and terminals;
- travel handoff and materialization authority transitions;
- party death and Actor Death Recovery terminals;
- every Actor Economy Evidence mutation, settlement fence, receipt, and indeterminate result;
- every conservation, duplicate-execution, double-materialization, or required-evidence-loss violation;
- experiment cohort assignment, every `experiment.run_started` and `experiment.run_terminal` record, and their run
  correlation fields; and
- counters that disclose sampled-record drops or bounded-buffer overwrites.

If the required-evidence path has no capacity, a consequential Actor capability whose safety depends on that evidence
must defer or remain unavailable. It must not mutate first and silently lose proof.

### Aggregate or sample

- per-hit combat detail becomes one bounded encounter summary unless it triggered a decision;
- unchanged Actor status and perception are not emitted;
- path nodes, coordinate ticks, headings, and packet updates are not evidence records;
- zone loop duration is accumulated in-memory and flushed as a fixed-window histogram;
- process and host CPU/memory may begin at one sample per second in a controlled cost experiment and one per ten
  seconds in production-like observation, aggregated into one-minute windows; and
- low-value diagnostic detail may use deterministic sampling declared by the manifest, never ad hoc random omission.

### Cardinality rules

Live metric dimensions are limited to bounded categories: record type, common terminal/reason category, strategy
version, authority mode, workload class, speaker kind, and zone. Actor, objective, action, transaction, target, item,
free text, exact price, and experiment IDs belong in event records or offline grouping, not live time-series labels.

Every string payload has a type-specific byte cap. Every repeated collection has a cap. Evidence producers count
attempted records, accepted records, serialized bytes, rejected oversize records, queue high-water marks, persistence
failures, drops, and overwrites by evidence class.

The present durable `actor_events.event_json` limit is 16 KiB and its indexes support Actor cursor and zone/time reads
([manifest schema](https://github.com/thunderbump/bump-EQEmu/blob/60edb352d33101c22e24447cd6d5fddda032157e/common/database/database_update_manifest.h#L7274-L7300)).
The in-memory recorder overwrites its oldest entries at 512 records but exposes no overwrite counter
([bounded overwrite](https://github.com/thunderbump/bump-EQEmu/blob/60edb352d33101c22e24447cd6d5fddda032157e/zone/harness/actor_event_recorder.cpp#L304-L340)).
Therefore `events_dropped_total`, payload-byte accounting, and cursor-gap accounting are prerequisites for claiming a
measured evidence-loss rate.

### Retention phases

During prototypes, tests, deterministic replay work, and the first production-like soak, retain the complete required
evidence bundle with its manifest across test runs. Do not choose production sampling from intuition. First measure
records/second, bytes/second, database cost, and query use by evidence class.

After measurement, a later policy may shorten high-rate diagnostic retention or retain only fixed-window performance
aggregates. Immutable provenance, decision/action terminals needed to explain current Actor state, experiment bundles
under active comparison, and conserved-asset evidence remain protected by explicit retention rules. Deletion itself
records the source-watermark range and retention policy version; it never leaves an aggregate that looks complete when
its source evidence was partial.

## Zone-tick overhead boundary

The zone-side evidence path must be bounded and cheaper than the gameplay boundary it observes:

- no planner inference, comparative analysis, report building, database query, network call, or synchronous JSON/file
  persistence inside Mob or zone tick loops;
- capture compact typed values only at consequential state transitions;
- maintain per-engagement counters and performance histograms in memory rather than emitting per-tick records;
- enqueue into a bounded zone-owned buffer in constant time, then batch serialization and persistence outside the
  gameplay loop;
- cap batches, flush work, payloads, and queue memory; publish queue saturation and persistence latency;
- shed sampled performance/diagnostic detail before decision, action-terminal, handoff, death, or asset evidence; and
- defer evidence-dependent consequential actions when required evidence cannot be accepted.

The existing harness precedent is bounded rather than streaming: cursor reads are clamped and callers poll once per
completed observation cycle rather than busy-looping
([harness event limits](https://github.com/thunderbump/bump-EQEmu/blob/60edb352d33101c22e24447cd6d5fddda032157e/docs/autonomous-actors/actor-action-event-control-plane-spike.md#L47-L82)).
The expanded event design likewise recommends pull-based perception, bounded buffers, filtered cursor reads, and no
per-tick snapshots
([cost controls](https://github.com/thunderbump/bump-EQEmu/blob/60edb352d33101c22e24447cd6d5fddda032157e/docs/autonomous-actors/actor-event-perception-expansion-spike.md#L287-L299)).

## Implementation sequence implied by the research

1. Publish logical schemas for Actor Experiment Manifest, Actor Decision Record, action/event correlation, and runtime
   windows before expanding durable tables.
2. Prototype a bounded asynchronous evidence sink with explicit required-versus-sampled classes, overload injection,
   byte/rate accounting, and zone-loop overhead measurements.
3. Extend only the event types needed by the first hunt and chatter prototypes; do not implement the entire vocabulary
   at once.
4. Build deterministic replay from exact manifests, initial snapshots, ordered inputs, seeds, and per-step digests.
5. Derive automated gameplay assertions from the same authoritative terminals and conservation records.
6. Run a production-like evidence-volume soak before selecting long-term diagnostic retention or sampling.

The sharp newly surfaced follow-on is: **Prototype a bounded Actor evidence sink and measure overload behavior**. Its
question should compare an in-memory batch/outbox boundary and the existing direct persistence precedent under normal,
burst, and persistence-failure loads; prove required evidence cannot be silently lost; and measure marginal zone-loop,
CPU, memory, database, and byte volume. It should block production retention decisions, but not the already-charted
offline deterministic replay prototype.

## Primary source boundary

This research used only pushed first-party sources:

- bump-EQEmu commit [`60edb352`](https://github.com/thunderbump/bump-EQEmu/tree/60edb352d33101c22e24447cd6d5fddda032157e):
  current source, schema repositories, tests, ADRs, and merged Autonomous Actor artifacts;
- afk-composable-pipeline commit
  [`87578dc0`](https://github.com/thunderbump/afk-composable-pipeline/tree/87578dc0f2c49bb1f7f054335b36749f188c6a61):
  exact-commit evidence and verification-gate conventions only.

No secondary web sources, deployment environment, AkkStack mutation, or Graphify corpus was used.

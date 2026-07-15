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

There is no pushed Actor cross-zone handoff, Actor wallet/holdings settlement, durable **Actor Evidence Sequencer**,
tick-percentile sampler, evidence-drop counter, or Actor Experiment Manifest schema. This research names the evidence
each later proof needs; it does not declare those gameplay capabilities available.

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

`action.started` is emitted exactly once, immediately before an accepted action first crosses from queued/validated
work into its ordinary gameplay execution boundary. Claiming a queue row, validating authority, polling a retry, or
recording later progress does not start the action. The start record cites the requested and accepted record IDs; keeps
the same action identity, generation, attempt, idempotency key, and payload digest; records the current execution
authority, Actor Evidence Sequencer authority generation, and zone/process epoch; and carries a monotonic logical start
value from the action's time origin.
The requested, accepted, and terminal records carry comparable logical values so durations never depend on wall time.

An accepted action may reach a canonical terminal before it starts, for example when authority is interrupted or its
request expires while queued. That terminal has no `started_record_id` and retains the ordinary canonical outcome and
reason; it never fabricates a start. A started action terminal cites `started_record_id`. No tick, progress callback, or
repeated execution attempt emits another start record.

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
| `actor_sequence` | Unique monotonic position assigned by the Actor Evidence Sequencer when actor-scoped. |
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
| `manifest_name`, `manifest_version`, `manifest_schema_version`, `manifest_digest` | Every Actor Experiment Run and every record it produces; the digest is the manifest's content identity. |
| `experiment_run_id`, `run_iteration` | Every record produced by one **Actor Experiment Run**; the opaque run identity and manifest-scoped iteration distinguish repetitions of the same manifest. |
| `variant_id`, `cohort_id`, `assignment_window_id`, `assignment_digest` | Variant-, cohort-, or crossover-window-scoped records within one controlled comparison. |

Names and free-form text belong in bounded payloads, not correlation keys. IDs are opaque. Timestamps help operators
but never replace actor sequence, action generation, materialization generation, transaction fence, or event watermark.

### Actor evidence sequencing authority

One durable **Actor Evidence Sequencer** owns the next sequence and current evidence-authority generation for each
Actor. Zone, world/coarse, and helper processes are evidence producers, not independent sequence authorities. The
sequencer grants one exclusive append lease for an Actor to the producer that currently owns that Actor's execution
authority: a materialized zone process for live execution or the world/coarse controller while dematerialized. A
planner or other helper returns a source-local proposal to the current lease holder; accepting, rejecting, or ignoring
that proposal becomes evidence only when the holder submits it through the sequencer.

The durable sequencer state contains at least the Actor, last committed `actor_sequence`, evidence-authority
generation, leased producer kind and process epoch, and lease state. It assigns consecutive sequences transactionally
when a producer appends an ordered batch outside the zone tick. Each submitted record already has a globally unique
`record_id`; retrying the same immutable `record_id` returns its original sequence, while reusing that identity with
different content is rejected. The sequencer rejects a sequence supplied by a producer, a second record for an
occupied Actor/sequence pair, any value at or below the durable watermark, and every append from a stale authority
generation or process epoch. Producers may preserve source-local order before append, but neither their counters nor
database auto-increment order are Actor replay order.

An exclusive append lease avoids cross-producer races without a coordination call on every zone tick. Producers
capture only consequential records into their bounded local queue and append bounded batches outside gameplay loops.
No second producer may append for the Actor concurrently; world routing and helper results enter through the current
holder until an explicit authority transfer. Batch persistence and capacity still obey the required-versus-sampled
evidence rules below; the sequencer does not make a dropped record complete merely by advancing a counter.

Cross-zone or live/coarse handoff uses this ordering fence:

1. the source stops accepting new consequential work, appends through its final required-evidence watermark, and
   appends `handoff.source_released` in its current evidence-authority generation;
2. the sequencer durably closes that lease and records its terminal sequence before advancing the evidence-authority
   generation and leasing the destination; and
3. the destination appends `handoff.destination_claimed` as the next Actor sequence before it may emit consequential
   work.

If the source cannot prove its release and final watermark, the destination lease is not granted. On process restart,
the sequencer reloads the durable Actor watermark and fences the old process epoch before granting a recovery lease.
An append whose acknowledgement was lost is retried with the same `record_id` and recovers the same sequence; an
uncommitted record is never reconstructed with a guessed order. A lost required record invalidates the affected
experiment run and keeps any capability whose safety depends on that evidence deferred. Late batches from the old
generation are fenced without consuming a sequence. Thus gaps cannot masquerade as committed evidence, sequences
cannot regress or be assigned twice, and replay sorts one Actor's committed records by `actor_sequence` while using
explicit causation/correlation for relationships between Actors.

## Terminal vocabulary

Objective lifecycle terminals remain distinct from the action outcomes that led to them. An `objective.terminal` record
uses exactly one of these classifications:

| Objective terminal | Meaning |
| --- | --- |
| `completed` | The objective's desired postcondition was authoritatively observed. |
| `abandoned` | The objective became non-viable and returned conserved state to the Actor Routine. |

Every objective terminal also records its bounded terminal reason. Action deferral, retry exhaustion, interruption, or
death can contribute to that reason, but none of them is itself an objective lifecycle terminal.

Use the objective contract's existing action outcomes everywhere an Actor Action feeds an Actor Objective:

| Action outcome | Meaning |
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
- `cancelled` is an execution fact and must map explicitly to one canonical action outcome; and
- `dropped_evidence` describes telemetry loss and can never be an Actor Action terminal.

Every objective lifecycle terminal and every action terminal records a stable reason code plus bounded human-readable
detail. Metrics group by reason code, never by free-form detail.

## Domain event vocabulary

### Objectives and action failures

Record in full:

- `objective.instantiated`, `objective.phase_changed`, and `objective.terminal`;
- `decision.made`, including emitted action or explicit no-action;
- `action.requested`, `action.accepted`, `action.rejected`, `action.started`, and `action.terminal`;
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
  handoff identity, materialization generation, and the source/destination Actor Evidence Sequencer generations;
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

Contention needs no special priority policy, but evidence must measure both affected and unaffected opportunities. A
contestable opportunity is one bounded Actor decision or action evaluation against `target`, `corpse`,
`merchant_stock`, `interaction`, or `zone_capacity` that concludes with an explicit selection/no-action decision,
ingress rejection, or one canonical action terminal.

Emit exactly one `contention.opportunity` when each unique contestable evaluation concludes, including when no player
contender was observed or an observed contender had no effect. The record contains:

- a stable opportunity identity bound to the Actor Decision Record and bounded resource kind, plus the action
  identity/generation/attempt when an action exists;
- whether a contender was authoritatively observed, a capped opaque contender count, and whether observation occurred
  before acceptance, after acceptance, or both;
- a bounded effect set containing zero or more of `selection_changed`, `authority_changed`, and `terminal_changed`;
- the resulting canonical action terminal, the separate `rejected` ingress classification, or `no_action` for a
  selection-only decision; and
- the linked action and decision records that establish causation rather than inferring it from nearby timestamps.

An opportunity with an empty effect set is zero-impact even when a contender was present. If ordinary merchant state
changed but the Actor cannot authoritatively attribute that change to player contention, record no contention effect;
the merchant action still retains its ordinary rejection or terminal. Actions still active at the report watermark are
reported as censored and do not enter a terminal-opportunity denominator.

Retain these bounded opportunity records in full during controlled runs; do not emit proximity scans or per-tick
player-presence records. Record only opaque player-presence correlation and capped counts, never player chat, account
data, inventory, or identity details not needed for gameplay authority. Live dimensions are limited to resource kind,
observation phase, effect category, and canonical outcome; Actor, action, opportunity, and player correlations remain
event fields or offline grouping keys.

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

Economy metrics are derived only from complete, unsampled Actor Economy Evidence within that watermark range plus
authoritative opening and closing custody snapshots. A missing mutation, opening/closing snapshot, or valuation input
makes the affected metric incomplete; a report cannot infer it from a success result or silently treat missing value as
zero. Actor, item, action, and transaction identities remain event fields and offline report keys, never live
time-series labels.

An economy-scoped action is one whose bounded operation requests a change to Actor item custody, equipment custody,
wallet currency, or merchant settlement. Operation and reason are bounded schema vocabulary for offline grouping.

Marginal CPU uses matched actor-on/actor-off window pairs with identical process or cgroup scope, duration, workload,
and sampling method. For pair `i`, `cpu_delta_i = actor_on_cpu_seconds_i - actor_off_cpu_seconds_i` and
`actor_hours_i` is materialized Actor seconds in its actor-on window divided by 3,600. Aggregate matched pairs as
`sum(cpu_delta_i) / sum(actor_hours_i)`, not as an unweighted mean of pair ratios. The unit is CPU seconds per
materialized Actor hour. A zero or missing summed denominator makes the result undefined and reported with its invalid
denominator; negative deltas remain observable and are never clamped to zero.

| Metric | Definition |
| --- | --- |
| materialized presence share | materialized Actor seconds / enabled Actor seconds |
| visible activity share | seconds in materialized travel, combat, economy, chatter, or city-downtime activity / materialized Actor seconds |
| objective completion rate | `completed` objective lifecycle terminals / all objective lifecycle terminals |
| objective abandonment rate | `abandoned` objective lifecycle terminals / all objective lifecycle terminals; report by bounded terminal reason |
| action deferral rate | `deferred` action terminals / all action terminals |
| action terminal outcome share | action terminals in one canonical outcome / all action terminals; report every `succeeded`, `deferred`, `blocked`, `failed`, `expired`, `interrupted`, and `death` outcome separately |
| retry-consuming action failure rate | `blocked` + `failed` + `expired` action terminals / all action terminals; `interrupted` and `death` remain separate viability outcomes rather than action failures |
| action request-to-start duration | `action.started` logical start minus its correlated `action.requested` logical value; report only started actions and count accepted actions that terminal before start separately |
| action accepted-queue duration | `action.started` logical start minus its correlated `action.accepted` logical value; report only started actions |
| action runtime duration | `action.terminal` logical value minus its correlated `action.started` logical start; report only started actions and split by canonical terminal outcome |
| accepted-before-start terminal share | accepted actions reaching `action.terminal` without `started_record_id` / all accepted actions reaching a terminal; report every canonical outcome and bounded reason separately |
| meaningful progress rate | Objective Progress Lease refreshes / active objective hour |
| retry consumption | action attempts consumed / objective lifecycle terminal; report distribution by objective terminal, template, and strategy |
| active objective duration | active elapsed time excluding explicitly paused capacity deferral |
| travel completion rate | `succeeded` travel terminals / all travel terminals; report the remaining canonical terminal outcomes separately |
| travel completion duration | correlated logical elapsed time from `travel.requested` to `travel.terminal`; report distribution by terminal outcome, authored route/checkpoint set, and strategy version |
| handoff success rate | succeeded handoffs / handoff terminals |
| double-materialization violations | handoffs with simultaneous authoritative source and destination claim; target is zero |
| engagement success rate | selected-target succeeded combat terminals / Committed Engagements |
| party death rate | party deaths / materialized party hour and / Committed Engagement |
| recovery completion time | readiness/rematerialization terminal time minus recovery start |
| Actor Wallet balance | authoritative closing Actor Wallet copper for each Actor at the terminal watermark; report cohort total and per-Actor distribution, and reconcile opening balance + gross inflow - gross outflow = closing balance |
| Actor Holdings value | sum of the manifest-declared valuation formula over every item in each Actor's authoritative closing Holdings snapshot; report cohort total and per-Actor distribution with valuation formula/input versions, and mark the metric incomplete if any held item lacks a value |
| holdings concentration | top-N Actors' Actor Holdings value / total Actor Holdings value from the same closing snapshot and valuation formula; N is declared by the report formula, and a zero total produces an undefined result with its zero denominator reported |
| gross Actor coin inflow | sum of positive authoritative copper credits whose destination is an Actor Wallet; count each completed asset side once by settlement key or custody version and report bounded source category offline |
| gross Actor coin outflow | sum of the absolute value of authoritative copper debits whose source is an Actor Wallet; count each completed asset side once by settlement key or custody version and report bounded sink category offline |
| Actor wallet net flow | gross Actor coin inflow minus gross Actor coin outflow; report the two gross components beside the net value |
| vendor proceeds | subset of gross Actor coin inflow credited by completed ordinary merchant-sale settlements with a terminal receipt; deduplicate by settlement key |
| economy deferred count | count of unique economy-scoped action identity/generation/attempt tuples whose `action.terminal` outcome is `deferred`; report by bounded operation and reason offline |
| economy deferral rate | economy deferred count / all unique economy-scoped action terminals in the same watermark range |
| economy rejected count | count of unique economy-scoped action identity/generation/attempt tuples whose request ended at `action.rejected`; report by bounded operation and reason offline |
| economy rejection rate | economy rejected count / all unique economy-scoped `action.requested` records in the same watermark range |
| authoritative quote price | authoritative quoted copper / quoted item quantity for valid `economy.quote_observed` records; report distributions by quote-input digest, pricing-calculation version, ruleset/config revision, and strategy version |
| paired quote price difference | candidate authoritative quote copper per item minus baseline authoritative quote copper per item for valid quotes matched by quote-input digest, pricing-calculation version, ruleset/config revision, and validity boundary; also report `(candidate - baseline) / baseline` when baseline is nonzero |
| upgrade distribution | positive Bot Gear Value gained by Actor, party member, slot, and strategy |
| conservation violations | unmatched or imbalanced authoritative asset deltas; never estimated or sampled |
| chatter delivery rate | delivered messages / materialized Actor hour, split by Actor/Bot/NPC speaker kind |
| chatter suppression rate | suppressed chatter decisions / chatter opportunities |
| repetition rate | repeated normalized text digests within the manifest's repetition window / deliveries |
| contention exposure rate | emitted `contention.opportunity` records with an authoritatively observed contender / all emitted `contention.opportunity` records |
| contention impact rate | emitted `contention.opportunity` records with a non-empty effect set / all emitted `contention.opportunity` records |
| contention conditional impact rate | emitted `contention.opportunity` records with a non-empty effect set / emitted opportunities with an observed contender; a zero denominator is undefined and reported |
| contention-affected action outcome share | contention-affected opportunities ending in one canonical action outcome / all contention-affected opportunities with an action terminal; report `succeeded`, `deferred`, `blocked`, `failed`, `expired`, `interrupted`, and `death` separately |
| contention-affected rejection count | contention-affected opportunities ending at the separate `rejected` ingress classification; never fold rejection into a canonical action outcome |
| contention-affected selection-only count | contention-affected opportunities ending in `no_action`; report separately from rejected requests and canonical action outcomes |
| zone loop p50/p95/p99 | quantiles of loop wall time from the same fixed sampling windows |
| loop overrun rate | loops above the configured budget / measured loops |
| marginal CPU | `(sum(matched actor-on CPU seconds - matched actor-off CPU seconds)) / sum(materialized Actor hours in actor-on windows)`, in CPU seconds per materialized Actor hour |
| marginal memory | matched actor-on RSS/PSS minus actor-off RSS/PSS at equivalent workload |
| evidence volume | records and serialized bytes / Actor hour, by evidence class and record type |
| evidence loss rate | dropped or overwritten eligible records / attempted eligible records; required evidence loss is a violation |

“World feels populated” still requires human observation. Each controlled **Actor Experiment Run** therefore accepts a
bounded operator rating and note set for perceived presence, plausibility, noisiness, visible discontinuities, and
interference. Store the rubric version and blinded run label; do not convert subjective notes into fabricated gameplay
events.

## Actor Experiment Manifest

An **Actor Experiment Manifest** is one published, immutable definition of a complete controlled comparison. It contains
the baseline and candidate variants together; an experiment arm is never represented as a second manifest. Every
**Actor Experiment Run** and every record produced by that run references the same manifest content identity. The
manifest contains:

```text
manifest schema version, manifest name/version, and canonicalization/digest algorithm version
hypothesis and primary/guardrail metrics
repository commit, schema, ruleset/config, content/data, and zone/nav revisions
baseline and candidate variant IDs, roles, and exact immutable strategy-publication references/digests
Actor roster/profile/party definitions
assignment mode, immutable fixed-cohort mapping or Actor-by-window crossover schedule, assignment digest, and exclusions
initial snapshots, deterministic seed list, and replay ordering rules
zones, anchors, objective templates, workload, observation window, and stop rules
authority mode: live, coarse, hybrid, offline replay, or controlled zone
sampling, aggregation, retention, and evidence-loss policy
human-observation rubric and blinding, when applicable
```

`manifest_digest` is SHA-256 over the canonical UTF-8 JSON serialization of every logical manifest field listed above.
The declared canonicalization version fixes object-key ordering, array ordering, integer encoding, and treatment of
optional fields. The serialized input excludes `manifest_digest` itself, signatures, storage paths, publication
timestamps, run identities/state/timestamps, results, and produced artifacts. The digest is computed and attached only
after the manifest content is frozen; changing any definition field publishes a new manifest version and digest.

Each `variant_id` is unique only within that manifest and selects an exact set of immutable Actor Strategy Registry
publications. `baseline` and `candidate` are roles assigned to variants, not independent manifest identities. Assignment
binds Actors before outcomes are observed. The manifest selects exactly one assignment mode:

- `fixed_cohort` assigns every included Actor one `variant_id` for the complete run using the declared mapping or
  deterministic algorithm and non-secret salt; or
- `crossover_schedule` contains an ordered immutable list of windows. Each window has a unique
  `assignment_window_id`, planned half-open logical-time bounds from the run origin, an explicit Actor-to-variant table,
  the safe activation rule, maximum transition delay, washout duration, and observation-exclusion rule. Windows cannot
  overlap, and every included Actor has exactly one assignment in every window.

For crossover mode, a planned boundary stops the Actor from beginning a new objective under the old variant. An active
objective retains its published strategy versions until its next declared safe objective boundary, then
`experiment.actor_variant_activated` records the Actor, old/new window and variant, planned and actual logical boundary,
Actor Evidence Sequencer watermark, and exact strategy-publication digests. Washout begins at that actual activation;
carryover and washout evidence remains attributable but is excluded from outcome comparison. Missing the manifest's
maximum transition delay follows its predeclared Actor-exclusion or run-invalidation rule.

The fixed mapping or complete crossover schedule and its boundary/washout rules are covered by `manifest_digest` and
`assignment_digest`. Every decision, action, and outcome in crossover mode carries its actual `assignment_window_id`,
`variant_id`, and assignment digest. An Actor cannot choose to select, skip, reorder, or extend a window, and the
schedule cannot promote a strategy outside this experiment.

An **Actor Experiment Run** is an observable execution of that definition, not a mutation of it. Each execution receives
a new durable `experiment_run_id` and the next manifest-scoped `run_iteration`, including retries and exact
repetitions. Record both boundaries at full fidelity:

- `experiment.run_started` binds the **Actor Experiment Run** identity to the manifest name, version, schema version,
  and digest; assignment mode and digest; exact assigned window/variant IDs and strategy-publication digests; actual
  repository/config/content/schema and strategy provenance digests, authority and workload mode, initial snapshot and
  ordered-input/replay-bundle digests, seed-set digest, actual Actor roster/exclusions and assignment digest, planned
  observation window and stop rules, start wall time and logical-time origin, and starting source/Actor watermarks. When
  human observation is used, it also references the rubric version, blinded run label, and opaque observation-session
  identity.
- `experiment.run_terminal` links to the start record and records one stable **Actor Experiment Run** terminal
  (`completed`, `stopped`, `failed`, or `invalidated`) plus reason code; actual wall/logical timing and observation
  window; terminal watermarks by source, Actor, cohort, and assignment window; actual crossover transitions, delays,
  washouts, and exclusions when applicable; attempted/accepted/dropped/overwritten/rejected evidence counts by class;
  required-evidence completeness; final state, output, replay-result, metric/report, and other produced artifact digests;
  and the bounded human-observation artifact/session reference when used. Any required-evidence loss or
  provenance/input mismatch makes the **Actor Experiment Run** `invalidated`, never a complete-looking comparison.

**Actor Experiment Run** records report observed execution facts. They cannot rewrite the manifest, assignment,
strategy publication, input bundle, or expected stop rules. Artifact references are immutable identities and content
digests, not mutable paths. A run whose manifest bytes do not reproduce `manifest_digest`, or whose assigned strategy
publication does not match its variant, is invalidated.

Use paired replay over the same initial snapshot, ordered inputs, and seeds when comparing pure decisions. Use matched
live cohorts or crossover windows when ordinary gameplay timing, players, pathing, or zone cost is part of the question.
Fixed assignment is deterministic from the manifest and stable Actor identity. Crossover assignment follows only the
manifest's Actor-by-window table and recorded safe-boundary activation rule. No Actor self-selects or self-promotes.

Comparison reports must:

- name the one exact manifest name/version/digest, every included `experiment_run_id`, and the compared baseline and
  candidate `variant_id` plus their exact strategy-publication digests;
- include only runs that reference that same manifest digest; comparing a different strategy or comparison definition
  requires a newly published manifest rather than treating two manifests as experiment arms;
- show cohort/window balance and exclusions before outcomes;
- for crossover runs, reconstruct each Actor's assignment from the immutable schedule and
  `experiment.actor_variant_activated` records, show planned/actual boundaries and transition delays, and exclude the
  declared carryover/washout intervals before grouping evidence by window and variant;
- compare identical metric formulas and authority classes;
- report denominators, missing evidence, effect size, and interval/dispersion rather than winner-only totals;
- separate replay, controlled-zone, production-like, and human-observation results; and
- require explicit reviewed promotion of a new Actor Strategy Registry version.

Actor experiments require verification evidence to name the exact committed candidate to which it applies. That is an
explicit Actor experiment contract here, not an enforcement claim about the cited AFK review function. The pinned AFK
source more narrowly normalizes or infers implementation metadata and packages it into review evidence
([implementation evidence normalization](https://github.com/thunderbump/afk-composable-pipeline/blob/87578dc0f2c49bb1f7f054335b36749f188c6a61/src/afk/review.py#L257-L315)),
and refuses review before required validation evidence passes
([evidence gate](https://github.com/thunderbump/afk-composable-pipeline/blob/87578dc0f2c49bb1f7f054335b36749f188c6a61/src/afk/review.py#L52-L78)).
Those cited lines do not independently prove that caller-supplied implementation metadata equals checkout `HEAD`.
Actor experiments therefore make exact strategy/config/content/candidate binding independently verifiable without
importing AFK workflow state into the game domain.

## Deterministic replay contract

The minimum replay bundle is:

- Actor Experiment Manifest and immutable strategy publications;
- the `experiment.run_started` record that binds this replay execution to its exact inputs and provenance;
- initial Actor Profile/objective/party/holdings snapshots and their digests;
- ordered decision-bearing observations and authoritative action outcomes;
- the Actor Evidence Sequencer watermark and evidence-authority generation, plus objective/action generations and
  input watermarks;
- deterministic seed and time inputs represented as logical elapsed values;
- expected per-step Actor Decision Record, emitted action identity, action-outcome classification when an action
  terminates, objective lifecycle terminal when the objective terminates, and next-state digest; and
- the resulting `experiment.run_terminal` record with terminal watermarks, completeness, and replay-result digest.

Replay consumes observations/outcomes, not derived metrics or performance samples. At each step it verifies input
watermarks, before-state digest, decision reason, zero-or-one emitted action, action generation, after-state digest, and
unconsumed stale/duplicate input behavior. A mismatch stops the replay at the first divergent decision sequence.

Wall-clock timestamps, entity pointer identity, database auto-increment order across Actors, producer-local sequence,
CPU timing, asynchronous delivery order not admitted by the sequencer-assigned Actor sequence, and performance samples
are nondeterministic context. They must not decide replay equality.

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
- experiment cohort/window assignment, every `experiment.actor_variant_activated`, `experiment.run_started`, and
  `experiment.run_terminal` record, and their run correlation fields; and
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

1. Publish logical schemas for Actor Experiment Manifest, Actor Decision Record, action/event correlation, Actor
   Evidence Sequencer state/leases, and runtime windows before expanding durable tables.
2. Prototype a bounded asynchronous evidence sink and durable Actor Evidence Sequencer with explicit
   required-versus-sampled classes, authority-transfer and crash injection, duplicate/fenced append checks,
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

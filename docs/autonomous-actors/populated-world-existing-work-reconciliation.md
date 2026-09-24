# Populated-World Existing Work Reconciliation

Date: 2026-07-12

Wayfinder ticket: `central-dcq7.1`, **Reconcile the existing actor execution Beads with the populated-world destination**

Snapshot notice: this document records the decision state on 2026-07-12. The queue executor subsequently merged in
[PR #51](https://github.com/thunderbump/bump-EQEmu/pull/51) at
[`b070a6c6f502e010ec11bf8af66cc2800629ae1e`](https://github.com/thunderbump/bump-EQEmu/commit/b070a6c6f502e010ec11bf8af66cc2800629ae1e),
so current Beads status—not the historical sequencing below—governs implementation order. The retained substrate and
scope boundaries remain the result of this reconciliation.

## Decision

Keep the existing bot-backed actor substrate and finish its queue executor before designing populated-world behavior.
The old work established the right execution boundary: one stable `actor_id` maps to a bot substrate and reserved
owner, a zone applies bounded actions through ordinary gameplay, durable events report outcomes, and planning stays
outside zone ticks. The populated-world map should extend that boundary with objectives, travel, economy, chatter,
activation, and experimentation; it should not replace it.

Three legacy Autonomous Actor Beads remain open. Retain all three, but amend two before implementation:

1. **Claim and execute queued actor actions in zone runtime** remains the immediate prerequisite. Tighten its
   acceptance criteria around disabled actors, expiry races, gameplay-event watermarks, rule-independent speech
   outcomes, idempotent terminal transitions, and actor ownership/status freshness.
2. **Add bump-EQEmu actor queue Tier 3 integration profile** remains the target-owned integration proof, but should be
   expressed as a repository-owned Validation Contract profile and should validate the exact executor behavior rather
   than contain AFK orchestration knowledge.
3. **Build helper-process planner MVP for low-cadence actor goals** remains useful only as a transport/discovery/cursor
   tracer. Narrow its “goal” to one deterministic bounded decision; the populated-world objective hierarchy belongs to
   the new map's objective research and prototype tickets.

## Closed foundation to treat as authoritative

| Closed work | Capability that remains valid | Boundary carried forward |
| --- | --- | --- |
| **Prototype: bot-backed Autonomous Actor harness action loop** and **Prototype: async Actor Action and Event control plane** | Zone Harness proves queued `target`/`say`, bounded normal processing, cursor events, and an opt-in local transport. | Harness setup is test-only; production behavior still uses ordinary gameplay actions. Do not promote harness HTTP into the world architecture by default. |
| **Spike: resolve the Bot owner seam for Autonomous Actors** and **Implement reserved owner-character seam for production bot-backed actors** | Production actors retain normal bot ownership through an explicitly reserved character; synthetic `Client` owners remain harness-only. | Preserve `actor_id`, `bot_id`, and `owner_character_id` as distinct identities. Do not reopen ownerless Bot or headless-Client work for the first living-world slice. |
| **Spike: prove actor-led Bot party with follower bots in Zone Harness**, **Extract ActorCommandSource seam for bot target and leash intent**, and **Extend ActorCommandSource to assist intent** | One bot-backed Autonomous Actor can source target, leash, and assist intent for ordinary Bot followers while owner authority and defaults remain intact. | This is the first `Actor-led Party`; it does not require bot group leadership or independently planning followers. |
| **Add ActorProfile and ActorStatus schema MVP** | Durable actor identity, enablement, substrate binding, current zone/entity state, and heartbeat exist with repository coverage. | Status is bounded current state, not a full perception dump or durable behavioral history. |
| **Persist ActorEvents for bot-backed actor observations** | Actor-shaped durable events, cursor reads, action correlation, and harness persistence exist separately from player events. | Keep gameplay observations distinct from queue lifecycle events when enforcing planner perception watermarks. |
| **Add durable ActorActionQueue for bounded actor intent** | Durable enqueue, actor-scoped claim, idempotency, `not_before`, expiry, bounded JSON, completion/failure, and stale-transition controls exist. | The queue holds one fresh gameplay-native step, not a durable goal tree, speculative plan, or unbounded history. |
| **Spike: sidecar planner container for low-cadence Actor goals** | Planning belongs in a disabled-by-default same-container helper first, with a dedicated sidecar deferred until isolation or multi-zone scale requires it. | Zone ticks own legality and execution; planner outage must degrade actors safely rather than stop zone behavior. |
| **Spike: test headless Client viability for player-equivalent actors** and its Tier 3 proof | Headless Client remains bounded evidence that player-equivalent paths are costly and lifecycle-heavy. | It is not the first-slice substrate and does not block the populated-world route. |

These conclusions are present in the actor docs and implementations at pushed commit
[`d18273b1da3f60b0fac25845931863f6be13ad54`](https://github.com/thunderbump/bump-EQEmu/tree/d18273b1da3f60b0fac25845931863f6be13ad54): the schema decision keeps
the three identities separate and adds actor-shaped profile/status/queue/event storage; the queue controls define
fresh, idempotent, bounded intent; the command-source seam preserves owner authority; and the party proof establishes
the one-actor/multiple-Bot shape.[^schema][^queue][^command][^party] The authoritative branch also contains the four
actor repositories, their schema manifests and CLI coverage, the event persistence sink, reserved-owner helpers, and
the actor-party harness scenario.[^code]

## Disposition of every relevant open legacy Actor Bead

### Retain and amend: Claim and execute queued actor actions in zone runtime

This is not duplicated by the populated-world map. It is the missing bridge between the already-merged durable queue
and ordinary zone gameplay, and therefore blocks every live objective prototype.

Preserve the current intent—claim only work for a live actor owned by the zone, execute a tiny action vocabulary, and
emit correlated success/rejection events—but add these acceptance criteria before another implementation attempt:

- Never claim work for a disabled profile, missing profile, stale heartbeat, mismatched zone/instance, or entity that
  no longer represents the profile's bot substrate.
- Treat expiry-aware queue transitions as authoritative. If completion/failure loses a race to expiry, do not emit a
  contradictory `action_completed` or `action_rejected` event.
- Separate the planner's last-observed *gameplay* event watermark from executor-generated queue lifecycle events, so
  completing one action does not falsely stale the next action planned from the same perception.
- Make `say` completion observable regardless of `Chat:QuestDialogueUsesDialogueWindow`, or use a first verb whose
  normal outcome is rule-independent; cover both relevant rule settings if `say` remains supported.
- Prove duplicate claim/execution attempts are idempotent and cannot perform the gameplay effect twice.
- Keep the first vocabulary narrow (`target` plus one safe observable verb); movement, combat, loot, merchant, and
  cross-zone actions graduate only from their populated-world investigations.

This amendment directly incorporates the unresolved findings recorded on the Bead after validated but unpublished AFK
attempts: disabled profiles were claimable, expiry outcomes could contradict emitted events, the speech proof depended
on a rule setting, and lifecycle events contaminated the stale-event watermark.[^beads]

### Retain and reframe: Add bump-EQEmu actor queue Tier 3 integration profile

This remains valid target-owned validation work and should continue to depend on **Claim and execute queued actor
actions in zone runtime**. It overlaps with no behavior-design ticket; it supplies evidence used by those tickets.

Reframe it behind **Implement the AFK Validation Contract adapter in Bump EQEmu** and **Align the repository AFK
implementation, review, and AkkStack instructions**:

- expose a named repository-owned profile through `afk.toml`/the repository wrapper;
- keep database, Zone Harness, AkkStack, cleanup, timeout, and evidence details inside Bump EQEmu;
- prove enqueue -> eligible ownership claim -> ordinary execution -> correlated observation, plus disabled/stale,
  expired, and watermark rejection cases;
- emit machine-readable evidence suitable for both AFK validation and later actor strategy experiments;
- do not encode actor semantics or deployment topology in the external AFK pipeline.

The Bead's audit correction already says it remains Bump EQEmu work after the AFK reset. Its old “Tier 3 profile” name
can remain for continuity, but its contract must follow the repository-owned validation architecture.[^beads]

### Retain but narrow: Build helper-process planner MVP for low-cadence actor goals

Keep this blocked by **Claim and execute queued actor actions in zone runtime**. It is the smallest end-to-end proof of
profile discovery, heartbeat filtering, event cursor resume, durable enqueue, and correlated observation without putting
planning in zone ticks.

Amend the scope so “goal” means a deterministic tracer decision such as “for this enabled fresh actor, enqueue one
safe action after this gameplay-event cursor.” Require:

- an explicit persisted or otherwise restart-safe cursor policy, including cursor gaps and retained-event loss;
- one in-flight/idempotency policy per actor and bounded backoff when no actor is eligible;
- disabled, stale, moved-zone, and helper-restart coverage;
- versioned decision metadata sufficient to reproduce why the action was submitted;
- disabled-by-default local deployment with no dedicated sidecar commitment.

Do **not** add hunt/acquire/sell/travel goal hierarchy, personality selection, pricing, chatter policy, or off-zone
simulation to this Bead. Those decisions belong respectively to **Map bounded actor objectives onto ordinary EQEmu
gameplay seams**, **Prototype the objective and persistent actor-profile decision model**, the economy/chatter lines,
and **Compare live-zone, coarse off-zone, and hybrid actor execution models**. The helper becomes their replaceable
execution adapter after those decisions are made.

## Relationship to the populated-world map

The legacy execution chain should be represented as a prerequisite lane:

`durable substrate (closed)` -> **Claim and execute queued actor actions in zone runtime** ->
**Add bump-EQEmu actor queue Tier 3 integration profile** and **Build helper-process planner MVP for low-cadence actor goals**

The new Wayfinder lines then answer decisions the legacy chain intentionally did not answer:

- runtime cost and live/coarse/hybrid execution decide activation and believable downtime;
- objective mapping and prototypes decide the first authored objective hierarchy and actor profile;
- economy research/prototypes decide conserved inventory disposition and availability-based pricing;
- chatter research/prototypes decide the shared actor/Bot/NPC event-driven system;
- evidence/replay work decides strategy comparison and deliberate promotion;
- route selection and the final roadmap combine these into the existing two-zone living-world slice.

There is one important dependency correction for future tracker wiring: behavior prototypes that need a live actor action
round-trip should depend on the queue executor (and normally its target-owned integration proof), while purely local
decision-model prototypes may proceed against recorded/synthetic bounded inputs. This prevents execution work from
being duplicated inside objective, economy, or chatter prototypes without serializing all research behind runtime code.

## Recommended next sequence

1. Amend and complete **Claim and execute queued actor actions in zone runtime** from the clean pushed baseline, using the
   recorded failed/unpublished work only as evidence—not as an assumed candidate.
2. Complete **Add bump-EQEmu actor queue Tier 3 integration profile** through the repository-owned Validation Contract.
3. Complete the narrowed **Build helper-process planner MVP for low-cadence actor goals** as a deterministic transport
   tracer.
4. In parallel with steps 1–3, continue read-only route, runtime-cost, economy, chatter, and evidence investigations.
5. Let later Wayfinder prototypes reuse the proven queue/helper boundary rather than designing another actor runtime.

No existing open legacy Actor Bead should be closed as duplicate. No new implementation Bead should be created for
queue execution, queue integration validation, or the first helper loop.

## Sources

[^schema]: [`actor-persistence-event-schema-spike.md`](https://github.com/thunderbump/bump-EQEmu/blob/d18273b1da3f60b0fac25845931863f6be13ad54/docs/autonomous-actors/actor-persistence-event-schema-spike.md); merged profile/status PR #28 (`fd48cbb3d`) and actor-events PR #29 (`7884dba15`).
[^queue]: [`actor-action-queue-controls.md`](https://github.com/thunderbump/bump-EQEmu/blob/d18273b1da3f60b0fac25845931863f6be13ad54/docs/autonomous-actors/actor-action-queue-controls.md); [`actor_action_queue_repository.h`](https://github.com/thunderbump/bump-EQEmu/blob/d18273b1da3f60b0fac25845931863f6be13ad54/common/repositories/actor_action_queue_repository.h); [`cli_test_actor_action_queue.cpp`](https://github.com/thunderbump/bump-EQEmu/blob/d18273b1da3f60b0fac25845931863f6be13ad54/world/cli/cli_test_actor_action_queue.cpp).
[^command]: [`actor-command-source-seam.md`](https://github.com/thunderbump/bump-EQEmu/blob/d18273b1da3f60b0fac25845931863f6be13ad54/docs/autonomous-actors/actor-command-source-seam.md); merged PR #24 (`319a0c218`) and PR #33 (`458aa61e6`).
[^party]: [`actor-led-bot-party-spike.md`](https://github.com/thunderbump/bump-EQEmu/blob/d18273b1da3f60b0fac25845931863f6be13ad54/docs/autonomous-actors/actor-led-bot-party-spike.md); merged PR #23 (`122ae985a`).
[^code]: [`database_update_manifest.h`](https://github.com/thunderbump/bump-EQEmu/blob/d18273b1da3f60b0fac25845931863f6be13ad54/common/database/database_update_manifest.h); [`actor_reserved_owners.h`](https://github.com/thunderbump/bump-EQEmu/blob/d18273b1da3f60b0fac25845931863f6be13ad54/common/actor_reserved_owners.h); the [`actor_profiles`](https://github.com/thunderbump/bump-EQEmu/blob/d18273b1da3f60b0fac25845931863f6be13ad54/common/repositories/actor_profiles_repository.h), [`actor_status`](https://github.com/thunderbump/bump-EQEmu/blob/d18273b1da3f60b0fac25845931863f6be13ad54/common/repositories/actor_status_repository.h), [`actor_action_queue`](https://github.com/thunderbump/bump-EQEmu/blob/d18273b1da3f60b0fac25845931863f6be13ad54/common/repositories/actor_action_queue_repository.h), and [`actor_events`](https://github.com/thunderbump/bump-EQEmu/blob/d18273b1da3f60b0fac25845931863f6be13ad54/common/repositories/actor_events_repository.h) repositories; [`actor_event_persistence_sink.cpp`](https://github.com/thunderbump/bump-EQEmu/blob/d18273b1da3f60b0fac25845931863f6be13ad54/zone/harness/actor_event_persistence_sink.cpp); [`reserved-owner-character-seam.md`](https://github.com/thunderbump/bump-EQEmu/blob/d18273b1da3f60b0fac25845931863f6be13ad54/docs/autonomous-actors/reserved-owner-character-seam.md); reserved-owner merge PR #32 (`241486e7d`).
[^beads]: Central Beads workspace records and comments read 2026-07-12 for `central-lhy`, `central-lhy.1`–`.17`, and `central-umi2.6`, including the recorded AFK attempts and audit correction. Commands were run read-only from `/home/bump/Projects/beads` with the documented per-invocation authentication method.

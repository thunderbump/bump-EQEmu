# Bounded Actor Objectives And Ordinary Gameplay Seams

Date: 2026-07-13

Wayfinder ticket: `central-dcq7.6`, **Map bounded actor objectives onto ordinary EQEmu gameplay seams**

Snapshot notice: this document records the decision state on 2026-07-13. The durable queue executor that was then a
prerequisite subsequently merged in
[`b070a6c6f502e010ec11bf8af66cc2800629ae1e`](https://github.com/thunderbump/bump-EQEmu/commit/b070a6c6f502e010ec11bf8af66cc2800629ae1e).
The objective/action boundaries remain the research result; current Beads status governs prerequisite sequencing.

## Source state

The checked-out branch was older than the merged Autonomous Actor substrate. Code findings therefore use pushed
commit [`d18273b1da3f60b0fac25845931863f6be13ad54`](https://github.com/thunderbump/bump-EQEmu/tree/d18273b1da3f60b0fac25845931863f6be13ad54).
The Rivervale–Misty constraints came from the same investigation's dated route decision.

No production behavior or stack configuration was changed for this investigation.

## Answer

An **Actor Objective** is a persistent desired state, not an **Actor Action**. The objective runner should advance one
small step at a time:

1. read bounded **Actor Perception** and the last relevant **Actor Events**;
2. enqueue one fresh, expiring, idempotent gameplay intent;
3. let the owning live zone execute it through ordinary server behavior;
4. observe a correlated gameplay outcome or explicit postcondition;
5. advance, retry with a new attempt identity, abandon, or recover.

The queue already has the right storage controls—bounded JSON, actor-scoped idempotency, `not_before`, expiry, claim,
and terminal states—but no live zone consumer currently joins that durable queue to gameplay. The separate Zone
Harness executor accepts only `target` and `say`.[^queue][^executor] That missing executor remains a prerequisite; this
research does not duplicate it.

Queue acceptance must never mean objective success. A consequential step completes only from a correlated gameplay
event or a checked postcondition. One queue row must never contain a multi-step travel, hunt, acquisition, merchant, or
recovery script.

## Objective-to-action map

| Objective | Bounded live actions | Ordinary seam to retain | Missing evidence or prototype |
| --- | --- | --- | --- |
| **Travel** | Repeated `move_to(checkpoint)`; then a distinct `cross_zone_line(route_edge)` handoff | `Mob::NavigateTo` and `MobMovementManager::NavigateTo` already drive ordinary pathing. Followers can retain their Client owner while sourcing follow, target, leash, and assist intent from the Actor leader.[^movement][^command-source] | Add `move_to` plus requested, arrived, stuck/no-path, cancelled, and party-lag outcomes. Existing Bot zoning is initiated by a live Client and saves/depops Bots; it does not transfer a Bot-led party itself.[^zoning] Use the existing nav-reachability and cross-zone-handoff prototypes rather than treating travel as one action. |
| **Hunt** | `select_allowlisted_target(area)` → `target(entity_ref)` → `engage(entity_ref)`; wait for one bounded kill or interruption | `target` is proven. Bot AI already applies normal target validity, attack eligibility, hate, positioning, casting, and attacks; Actor command-source seams can drive followers.[^executor][^bot-combat] | Actor Perception currently exposes nearby identity, HP, distance, group/owner/current-target, and existing-aggro tags, but not a safe target-eligibility decision.[^perception] Keep candidate selection zone-owned: enforce the route's NPC-type allowlist, hunting bounds, alive/attackable checks, ordinary `IsAttackAllowed`, visibility, and player contention. Add engage accepted/blocked, damage, death, and target-lost events. General faction reasoning remains later work. |
| **Acquire** | After an observed death: `inspect_corpse(corpse_ref)` → `loot_item(corpse_ref,item_ref)`; complete only after conserved custody changes | Ordinary corpse loot already enforces range, locks, ownership, concurrent looters, lore, quest hooks, inventory capacity, corpse removal, and events.[^loot] Bots have persistent equipment and normal Client-to-Bot trade/equip paths.[^bot-inventory] | Corpse loot is `Client` and packet/session shaped; the Bot substrate has no normal corpse-loot path. Compare only two approaches later: a real reserved-owner Client looter followed by ordinary Bot trade, or a server-side loot transaction seam shared by Client and Actor callers. Direct item insertion is not objective completion. |
| **Sell** | After live travel and eligibility: `sell_item(merchant_ref,item_ref,quantity,min_price)` for exactly one conserved transaction | Client merchant sell validates merchant identity, range, item/charges, price rules, temporary stock, quest/player events, inventory removal, money credit, and save.[^merchant-sell] Existing Bot Gear Value can inform disposition but is not transaction authority.[^gear-value] | Merchant sale and currency are Client-owned. Prototype a sessionless transaction seam with explicit possession and currency authority that reuses the ordinary validators, mutations, and events. Direct DB deletion/credit or `SaveTempItem` alone would bypass conservation. Detailed disposition and pricing remain in the economy research/prototype line. |
| **Recover** | `recover_until(thresholds,deadline)` enters a safe stop/guard/rest policy and waits; it does not directly heal or restore resources | Out-of-combat Bot processing already performs idle casts, recovery healing, meditation, sitting/standing, HP/mana/endurance regeneration, and rest-timer rules.[^recovery] | Add self and party HP/mana/endurance readiness plus healed, interrupted, dead, and deadline outcomes. Never implement recovery by `SetHP`, `SetMana`, `SetEndurance`, or “mark full.” Coarse downtime should preserve deficits until elapsed-recovery semantics are separately proven. |
| **Resupply** | `buy_item(merchant_ref,item_ref,quantity,max_price)` for one live purchase, followed by a separate conserved transfer/equip action if needed | Client merchant buy checks merchant/range, lore, stock, price, carried money, inventory, temporary stock, and events. Bot equipment transfer has its own legality and persistence checks.[^merchant-buy][^bot-inventory] | The Bot Actor has no ordinary carried inventory, consumable custody, or wallet. Solve the same possession/currency authority required by sell before selecting the first resupply item. Start with one concrete item such as ammo only after custody is real; never call Bot item-save helpers as a substitute for purchase. |
| **Idle** | `idle_at(anchor,minimum_dwell,interrupt_policy)` after ordinary arrival; use guard/hold/rest behavior until dwell or interruption | Bots already support guard/hold, follow/guard positioning, idle casts, meditation, and ordinary processing.[^idle] | Define visible idle start/end and interruption outcomes. `Camp()` saves and depops the Bot, so it is a dematerialization checkpoint—not the visible idle verb. Do not randomize or teleport between downtime states. |

## Cross-cutting action contract

Every new live action should carry:

- stable `actor_id`, objective ID, phase, attempt, and action type;
- compact typed payload and a logical-window idempotency key;
- `expires_at` matched to gameplay freshness;
- the Actor gameplay-event watermark used to make the decision;
- an explicit terminal result: observed success, blocked/rejected, expired, interrupted, or failed;
- action correlation in the Actor Event, or the conclusive event cursor and checked postcondition in `result_json`.

Recommended key shape: `objective_id:phase:attempt:checkpoint-or-entity`. A retry within the same decision window reuses
the key. Selecting a genuinely new checkpoint, target, item, or attempt advances it.

The current in-memory event vocabulary proves only `target_changed`, `speech_emitted`, and `spell_cast_started`; durable
persistence currently proves speech.[^events] Objectives need event families rather than forced outcomes:

- movement requested/terminal;
- engagement accepted/blocked and target lost;
- damage, healing, death, and combat ended;
- corpse inspected, loot blocked, and item custody changed;
- merchant offer/blocked/purchase/sale with item, stock, and currency deltas;
- rest/idle entered, readiness reached, interrupted, and timed out.

Actor Perception also needs eligibility hardening. Its current nearby scan is capped and distance-sorted, but it walks
the zone mob list without a visibility filter. It must not become an omniscient merchant or hunting query.[^perception]

## Prototype boundaries

1. **Existing prerequisite — durable live executor:** finish the already-tracked queue consumer for one materialized
   Actor, initially with `target` and one rule-independent observable verb. Correlate the outcome and terminalize safely
   across expiry races.
2. **Route reachability:** prototype `move_to` arrival/stuck/no-path evidence at the fixed Rivervale, Misty zone-line,
   merchant, and hunting checkpoints. Do not edit topology, paths, spawns, or world data.
3. **Objective state model:** simulate objectives as persisted state machines emitting one action at a time. Include
   progress, abandonment, bounded retry, interruption, death recovery, and deterministic replay without requiring live
   gameplay for every transition.
4. **Allowlisted hunt:** after objective policy is selected, prove zone-owned candidate eligibility, one engage, one
   observed kill, and player-contention deferral. Do not solve generalized faction reasoning here.
5. **Cross-zone handoff:** separately prove save/depop/materialize/rebind across the existing Rivervale–Misty zone line
   in the validation AkkStack. Two independent Zone Harness boots cannot prove transfer.
6. **Conserved custody:** let the economy seam research choose the acquisition/merchant authority, then prototype one
   corpse item receipt and one merchant transaction. Selling and resupply share this blocker and should not invent
   separate wallets or inventories.
7. **Recovery and idle:** prove threshold-based completion and dwell interruption using ordinary live Bot processing;
   keep later coarse elapsed-time recovery separate.

## Consequences for the first slice

- The initial objective hierarchy may include travel, hunt, acquire, sell, recover, resupply, and idle, but an
  implementation contract must enable only objectives whose live actions and observable completion are proven.
- The smallest credible loop is not a monolithic “travel/hunt/acquire” command. It is a persistent objective runner over
  bounded actions, with movement, handoff, combat, custody, and merchant transactions remaining separate seams.
- Testing follows naturally: each action has deterministic fixtures, bounded processing, explicit outcomes, and
  correlation. Those controls exist to make actors trustworthy and observable; automated gameplay tests reuse them.

## Sources

[^queue]: [`actor-action-queue-controls.md`](https://github.com/thunderbump/bump-EQEmu/blob/d18273b1da3f60b0fac25845931863f6be13ad54/docs/autonomous-actors/actor-action-queue-controls.md#L5-L45); [`actor_action_queue_repository.h`](https://github.com/thunderbump/bump-EQEmu/blob/d18273b1da3f60b0fac25845931863f6be13ad54/common/repositories/actor_action_queue_repository.h#L15-L52).
[^executor]: [`zone_harness_runtime.cpp`](https://github.com/thunderbump/bump-EQEmu/blob/d18273b1da3f60b0fac25845931863f6be13ad54/zone/harness/zone_harness_runtime.cpp#L239-L260).
[^movement]: [`waypoints.cpp`](https://github.com/thunderbump/bump-EQEmu/blob/d18273b1da3f60b0fac25845931863f6be13ad54/zone/waypoints.cpp#L612-L630); [`mob_movement_manager.cpp`](https://github.com/thunderbump/bump-EQEmu/blob/d18273b1da3f60b0fac25845931863f6be13ad54/zone/mob_movement_manager.cpp#L768-L802).
[^command-source]: [`actor-command-source-seam.md`](https://github.com/thunderbump/bump-EQEmu/blob/d18273b1da3f60b0fac25845931863f6be13ad54/docs/autonomous-actors/actor-command-source-seam.md#L9-L50).
[^zoning]: [`zoning.cpp`](https://github.com/thunderbump/bump-EQEmu/blob/d18273b1da3f60b0fac25845931863f6be13ad54/zone/zoning.cpp#L38-L47); [`bot.cpp`](https://github.com/thunderbump/bump-EQEmu/blob/d18273b1da3f60b0fac25845931863f6be13ad54/zone/bot.cpp#L7335-L7367).
[^bot-combat]: [`bot.cpp`](https://github.com/thunderbump/bump-EQEmu/blob/d18273b1da3f60b0fac25845931863f6be13ad54/zone/bot.cpp#L2229-L2286).
[^perception]: [`harness_snapshot_service.cpp`](https://github.com/thunderbump/bump-EQEmu/blob/d18273b1da3f60b0fac25845931863f6be13ad54/zone/harness/harness_snapshot_service.cpp#L122-L188).
[^loot]: [`corpse.cpp`](https://github.com/thunderbump/bump-EQEmu/blob/d18273b1da3f60b0fac25845931863f6be13ad54/zone/corpse.cpp#L1219-L1310).
[^bot-inventory]: [`bot.cpp`](https://github.com/thunderbump/bump-EQEmu/blob/d18273b1da3f60b0fac25845931863f6be13ad54/zone/bot.cpp#L4072-L4176); [`bot.h`](https://github.com/thunderbump/bump-EQEmu/blob/d18273b1da3f60b0fac25845931863f6be13ad54/zone/bot.h#L703-L714).
[^merchant-sell]: [`client_packet.cpp`](https://github.com/thunderbump/bump-EQEmu/blob/d18273b1da3f60b0fac25845931863f6be13ad54/zone/client_packet.cpp#L14374-L14568).
[^gear-value]: [`bot_loot_request.cpp`](https://github.com/thunderbump/bump-EQEmu/blob/d18273b1da3f60b0fac25845931863f6be13ad54/common/bot_loot_request.cpp#L720-L837).
[^recovery]: [`bot.cpp`](https://github.com/thunderbump/bump-EQEmu/blob/d18273b1da3f60b0fac25845931863f6be13ad54/zone/bot.cpp#L2560-L2626).
[^merchant-buy]: [`client_packet.cpp`](https://github.com/thunderbump/bump-EQEmu/blob/d18273b1da3f60b0fac25845931863f6be13ad54/zone/client_packet.cpp#L14126-L14372).
[^idle]: [`bot.cpp`](https://github.com/thunderbump/bump-EQEmu/blob/d18273b1da3f60b0fac25845931863f6be13ad54/zone/bot.cpp#L2131-L2141).
[^events]: [`actor_event_recorder.cpp`](https://github.com/thunderbump/bump-EQEmu/blob/d18273b1da3f60b0fac25845931863f6be13ad54/zone/harness/actor_event_recorder.cpp#L319-L416); [`actor_event_persistence_sink.cpp`](https://github.com/thunderbump/bump-EQEmu/blob/d18273b1da3f60b0fac25845931863f6be13ad54/zone/harness/actor_event_persistence_sink.cpp#L25-L55).

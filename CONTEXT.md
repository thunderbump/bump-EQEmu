# Bump EQEmu

This context describes gameplay-facing language for the Bump EQEmu server work. It captures terms that need consistent meaning across quests, server behavior, and AI-assisted in-game responses.

## Language

**Authored Dialogue**:
An existing NPC, bot, mercenary, quest, task, or emote response that the server already provides for a player interaction.
_Avoid_: Built-in response, scripted response

**Fallback Dialogue**:
A generated in-game response used only when no **Authored Dialogue** applies to the interaction.
_Avoid_: AI dialogue, dynamic dialogue

**Targeted Say**:
A player speech interaction directed at the player’s current target while both are close enough for ordinary in-game say range.
_Avoid_: Area chat, ambient listening

**Live Context**:
The current in-memory state available in the zone process when a player interaction occurs.
_Avoid_: Full world context, lore database context

**Unavailable Reply**:
A short in-character placeholder response shown when **Fallback Dialogue** cannot be generated.
_Avoid_: Error message, outage notice

**Dialogue Rules**:
Server rule values that control whether **Fallback Dialogue** is enabled and how it reaches the remote response service.
_Avoid_: Hardcoded AI settings, process-only configuration

**Fallback Dialogue Settings**:
A runtime snapshot of **Dialogue Rules** used by **Fallback Dialogue** code so processing, planning, and provider behavior can be tested without reading server rules directly.
_Avoid_: New rule source, cached configuration authority

**Delayed Dialogue**:
A response emitted after the original player interaction has already completed.
_Avoid_: Blocking dialogue, inline generation

**Current Interaction**:
The original speaker and target still being present, nearby, and intentionally connected when **Delayed Dialogue** is ready.
_Avoid_: Stale interaction, old target

**Public Gameplay Context**:
The subset of **Live Context** that is visible or inferable through normal play and safe to send to the remote response service.
_Avoid_: Private account context, operator context

**Dialogue Cooldown**:
A separate rate limit for starting remote **Fallback Dialogue** generation.
_Avoid_: Chat anti-spam, global chat limit

**Dialogue Response**:
One generated in-character response to a **Targeted Say**. A **Dialogue Response** may contain one or more ordered **Dialogue Fragments**.
_Avoid_: AI answer, generated blob

**Dialogue Fragment**:
One ordered part of a **Dialogue Response**, delivered as target speech or target emote.
_Avoid_: Raw model chunk, unstructured text

**Delivered Dialogue Message**:
One client-visible say or emote message emitted from a **Dialogue Fragment**. A long speech **Dialogue Fragment** may be split into multiple **Delivered Dialogue Messages**.
_Avoid_: Dialogue Fragment, player chat message

**Dialogue Response Processing**:
Turning raw **Natural Dialogue Format** text into safe ordered **Dialogue Fragments**.
_Avoid_: Delivery formatting, queue polling

**Dialogue Delivery Planning**:
Turning safe ordered **Dialogue Fragments** into **Delivered Dialogue Messages**.
_Avoid_: Model parsing, prompt handling

**Unsafe Dialogue Fragment**:
A **Dialogue Fragment** that looks like a command, metadata, JSON, technical error, provider failure, or out-of-character model refusal instead of ordinary in-character dialogue.
_Avoid_: Bad vibes, disliked answer

**Natural Dialogue Format**:
Plain generated response text that can include speech and simple stage-direction markers such as `*looks around*`.
_Avoid_: Required JSON dialogue, rigid response schema

**Stage Direction Marker**:
A simple **Natural Dialogue Format** marker that identifies an emote **Dialogue Fragment**. Asterisk markers can appear inside mixed speech; parenthesized markers are only treated as emotes when the whole **Dialogue Response** is parenthesized.
_Avoid_: Free-form parser magic, markdown command

**Bot Loot Request**:
A deterministic bot interest event where the server decides that a bot should ask for an item looted by a player.
_Avoid_: AI loot decision, bot loot automation

**Bot Gear Value**:
A deterministic estimate of how much a looted item improves a specific bot, based on server-applied equipment effects and bot class behavior.
_Avoid_: Item score, raw stat score, AI gear rating

**Bot Gear Role**:
A class-specific valuation profile used by **Bot Gear Value** when one bot class benefits from item effects differently than another.
_Avoid_: Archetype, generic role, loot role

**Loot Request Dialogue**:
Generated in-character phrasing for an already-decided **Bot Loot Request**.
_Avoid_: AI loot request, generated loot decision

**Engaged Hostile**:
An alive, attackable NPC that is currently part of the owner's group or raid combat situation and can be considered by bot combat-maintenance behavior. An **Engaged Hostile** is not merely nearby; it must be actively relevant to the fight and still pass ordinary spell validity constraints such as range, line of sight, immunity, crowd-control safety, and spell-specific cast checks.
_Avoid_: Nearby mob, active combatant, add

**Incoming Damage Pressure**:
A short-lived estimate of how quickly a bot heal target is losing HP during combat.
_Avoid_: Damage rate, HP slope, heal urgency score

**Healing Danger Window**:
The short projected time before a bot heal target reaches a dangerous HP threshold under current **Incoming Damage Pressure**.
_Avoid_: Raw DPS, urgency score, time-to-death

**Recovery Healing**:
Out-of-combat bot healing that returns players, bots, and pets to a ready state after active danger has passed.
_Avoid_: Pressure healing, downtime topping-off

**Bot Heal Selection**:
Choosing the concrete heal spell a bot should cast for an already-selected heal target and requested bot heal spell type, including pressure-aware escalation, Recovery Healing HoT preference, and efficient regular-heal choice when those behaviors are enabled.
_Avoid_: Regular heal efficiency service, heal optimizer, pressure healing module

**Bot-Aided Tracking**:
An owned bot's ability to report nearby trackable spawns to its owner when the bot has an appropriate tracking-capable class and level.
_Avoid_: Group tracking, shared Track skill, borrowed tracking

**Bot-Aided Tracking Report**:
A player-facing list of eligible nearby trackable NPC spawns produced through **Bot-Aided Tracking**.
_Avoid_: Track window, spawn window, tracking packet, raw mob list

**Rare Spawn**:
A gameplay-notable NPC spawn marked by the server's rare-spawn data flag.
_Avoid_: Named Spawn, name-prefix match, spawn-probability guess, ordinary spawn

**Autonomous Actor**:
A server-controlled character or creature that can perceive ordinary gameplay state, choose bounded actions toward a goal, and act through normal gameplay paths without direct player input.
_Avoid_: AI actor, test actor, entity, mob

**Actor-led Party**:
A party with one **Autonomous Actor** as its gameplay-intent source and ordinary owned bots filling the remaining group roles.
_Avoid_: Actor group, autonomous group, multi-actor party

**Actor Live Slot**:
Configurable permission for one **Actor-led Party** to run at live-zone fidelity; this is separate from permission to boot or retain a zone process.
_Avoid_: Active zone slot, permanent actor process

**Actor Objective**:
A persistent desired state advanced by observing **Actor Perception** and **Actor Events**, requesting one bounded **Actor Action** at a time, and evaluating its postcondition before choosing the next step.
_Avoid_: Goal command, queued plan, action script

**Actor Objective Template**:
A versioned, operator-approved definition of one bounded desired state that an **Actor Routine** may instantiate. Templates remain unavailable until their required gameplay capabilities have authoritative execution and observable completion.
_Avoid_: Composite quest, action script, behavior tree

**Actor Routine**:
A versioned, operator-approved strategy that selects one flat **Actor Objective** at a time to create a believable larger activity cycle. It does not nest objectives or decide that gameplay actions succeeded.
_Avoid_: Composite objective, queued plan, objective hierarchy

**Actor Strategy Registry**:
The operator-controlled collection of immutable, versioned **Actor Routines**, **Actor Objective Templates**, **Actor Profile Presets**, and **Party Danger** evaluators. Persistent Actors reference published versions rather than accepting live-edited or planner-authored strategy definitions.
_Avoid_: Strategy database, live behavior editor, planner configuration

**Actor Profile**:
Stable persistent identity, replay metadata, and a small set of objective-mechanics traits for an **Autonomous Actor**. The first objective contract uses persistence, risk tolerance, and recovery threshold; current **Actor Objective** execution state is stored separately.
_Avoid_: Objective state, personality blob, complete behavior configuration

**Actor Profile Preset**:
A versioned, operator-approved bundle of persistence, risk tolerance, and recovery threshold assigned to an **Actor Profile**. The first contract uses fixed cautious, steady, and tenacious presets without per-actor overrides.
_Avoid_: Personality class, mutable strategy, arbitrary trait values

**Party Readiness**:
The Actor leader and its spawned Bot followers being present, alive, out of combat, and above the configured HP and applicable mana recovery thresholds. Endurance and pets are not part of the first readiness contract.
_Avoid_: Actor health, full resources, recovery timer

**Party Danger**:
A versioned, normalized live estimate of immediate risk to an Actor-led Party, computed from authoritative party and combat state independently of its **Actor Profile Preset**. The first contract is reactive and does not predict zone depth or route difficulty.
_Avoid_: Zone difficulty, planner risk guess, objective importance

**Committed Engagement**:
An accepted Actor-led Party combat engagement that remains under ordinary Bot combat authority until combat ends, the target is lost, or the party dies. The first contract does not treat recovery as an unproven mid-combat retreat.
_Avoid_: Uncancellable fight, forced combat result, recovery action

**Action Retry Budget**:
The bounded number of retries allowed for one concrete **Actor Action** plan, such as a selected checkpoint or target, before the **Actor Objective** requires a fresh replacement plan.
_Avoid_: Objective lifetime, death limit, infinite retry

**Actor Replan Request**:
The persistent request for a fresh concrete phase plan after an **Actor Action** exhausts its retry budget or is temporarily deferred. It identifies the action, phase, and action generation being replaced so stale or wrong-phase plans cannot resume the objective.
_Avoid_: Uncorrelated retry, planner suggestion, queued plan

**Objective Progress Lease**:
A template-specific stall bound refreshed by correlated meaningful progress, including movement checkpoints and active combat progress toward the objective. Capacity deferral pauses the lease; an optional longer active-age cap provides a separate safety ceiling.
_Avoid_: Universal objective timeout, wall-clock failure, action expiry

**Objective Viability Allowance**:
The bounded allowance for replanning, danger interruption, and death recovery before an **Actor Objective** is abandoned as no longer worthwhile.
_Avoid_: Action retry count, actor health, global failure limit

**Believable Downtime**:
Persistent, lower-cost actor activity that maintains a plausible authored cycle while an **Actor-led Party** is not live, such as city recovery, resupply, trading, or coarse route progress.
_Avoid_: Despawn, random teleport, frozen actor

**Actor Resolution Zone Reserve**:
A configurable portion of the active-zone budget, initially one zone, available to resolve one otherwise-deferred consequential actor action without displacing a player-visible zone.
_Avoid_: Actor-only zone, permanently booted actor zone, guaranteed spare capacity

**Materialization Reconciliation**:
Choosing a plausible live placement and state for a coarsely progressing **Actor-led Party** when its zone becomes observable independently, such as when a player zones in.
_Avoid_: Teleport catch-up, spawn guess, forced arrival

**Actor Death Recovery**:
The conserved rematerialization of a defeated **Actor-led Party** at its configured bind or home anchor, preserving possessions without a lootable Actor corpse or death penalty.
_Avoid_: Corpse run, item recovery, resurrection penalty

**Actor Holdings**:
The persistent conserved unequipped items and currency owned by one **Autonomous Actor**. Items equipped by the Actor gameplay body or a Bot follower remain in authoritative equipment-slot custody rather than **Actor Holdings**. Ownership remains with the Actor across party changes and **Actor Death Recovery**.
_Avoid_: Party treasury, reserved-owner inventory, Bot bank

**Actor Merchant Principal**:
The gameplay identity whose authoritative faction standing, race, class, deity, and Charisma determine ordinary merchant access and pricing for an **Autonomous Actor**.
_Avoid_: Reserved-owner quote, neutral Actor price, strategy price

**Actor Offer Intent**:
A versioned recommendation to retain an item in **Actor Holdings** for possible future market listing. It is not a listing, reservation, escrow, or settlement authority.
_Avoid_: Actor listing, market order, reserved item

**Actor Holdings Capacity**:
The bounded amount of unequipped item custody available to one **Autonomous Actor**. Full capacity blocks acquisition or equipment replacement rather than deleting, overflowing, or automatically selling assets.
_Avoid_: Unlimited Actor inventory, overflow storage, Bot bags

**Actor Perception**:
The gameplay state visible or inferable to an **Autonomous Actor** at a decision point.
_Avoid_: Actor context, AI context, full world state

**Actor Action**:
A bounded gameplay request made by an **Autonomous Actor** through ordinary server behavior.
_Avoid_: Direct mutation, forced outcome, command injection

**Actor Event**:
An observable gameplay result used to understand what changed after **Actor Actions** and world processing.
_Avoid_: Log line, debug trace, internal callback

**Actor Decision Record**:
An attributable explanation of one **Autonomous Actor** decision, including the exact strategy provenance and observed-state boundary used to choose an **Actor Action** or deliberately choose no action. It does not assert that a requested action succeeded.
_Avoid_: Actor Event, planner log, action result

**Actor Evidence Sequencer**:
The durable ordering authority that assigns each actor-scoped evidence record its unique monotonic position while fencing stale producers across execution-authority changes.
_Avoid_: Event timestamp, database row order, producer-local counter

**Actor Experiment Manifest**:
An immutable, versioned definition of one controlled Actor strategy comparison, including its hypothesis, strategy variants, fixed or windowed Actor assignment, replay inputs, world/configuration provenance, and observation window.
_Avoid_: Test config, mutable experiment settings, strategy version

**Actor Experiment Run**:
An observable execution of one **Actor Experiment Manifest**, kept distinct across retries and exact repetitions so its start, terminal, provenance, evidence completeness, and produced artifacts remain attributable. It does not mutate the manifest.
_Avoid_: Manifest revision, experiment result, anonymous repetition

**Actor Economy Evidence**:
An attributable account of item and currency custody changes and economy outcomes used to assess conservation and strategy behavior.
_Avoid_: Economy debug log, aggregate-only metric, inferred transaction

**Zone Harness**:
Test infrastructure that controls and observes a zone runtime so gameplay behavior can be validated without relying on a manual client for every scenario.
_Avoid_: Autonomous Actor runtime, test actor system, production sidecar

## Relationships

- **Fallback Dialogue** must not replace **Authored Dialogue**.
- **Actor Death Recovery** is separate from an **Autonomous Actor** acquiring an item from an ordinary NPC corpse.
- **Actor Death Recovery** preserves equipped and carried possessions; death is not an item or currency sink.
- The Actor leader and Bot followers keep equipped items in their authoritative equipment slots; Bot followers own only their equipped items, while unequipped Actor-owned items and currency belong to **Actor Holdings**.
- A reserved owner character does not own or store **Actor Holdings**.
- A vendor or resupply recommendation does not authorize settlement until the **Actor Merchant Principal** and conserved item/currency transfer have authoritative proof.
- Merchant settlement requires the **Actor-led Party** to be materialized in the merchant's live zone and within ordinary interaction range; coarse progress may prepare intent but cannot move assets.
- Merchant settlement requires a durable unique key claimed by the ordinary transaction boundary before asset mutation; an interrupted attempt cannot execute again or receive a replacement key for the same concrete custody version, and without that fence selling and resupply remain unavailable.
- An **Actor Offer Intent** leaves the item in **Actor Holdings** and cannot make it visible or available to another buyer.
- Expiring an **Actor Offer Intent** clears that recommendation and requires fresh evaluation; it does not sell the item or extend stale market assumptions.
- Corpse-coin acquisition remains independently unavailable until a live guarded operation proves correlated combat participation and loot entitlement, exact corpse coin debit, Actor Wallet credit, authoritative postcondition and receipt, and **Actor Economy Evidence**; it cannot be coupled to item acquisition or executed through coarse progress.
- The directing Actor Wallet receives the first Actor-led Party's authorized coin; Bot followers do not own wallets or separate coin shares.
- The **Actor Merchant Principal** is the Autonomous Actor's own gameplay identity; reserved owner characters and Bot followers cannot supply or modify its merchant access or price.
- A first-slice Actor begins with an empty Actor Wallet and explicitly sourced equipped gear; replaced provisioned gear becomes ordinary **Actor Holdings**, and no asset replenishes automatically.
- The first **Actor Holdings Capacity** is eight flat item-stack slots; bags, nested containers, weight, and encumbrance require later evidence.
- Initial Actor item acquisition covers ordinary single-instance equipment; corpse coin and broader item classes remain unavailable until their independent custody and authority rules are proven.
- The first world-population slice should use one **Autonomous Actor** per **Actor-led Party** rather than requiring every party member to plan independently.
- Independently planning **Autonomous Actors** may form temporary parties later, after runtime evidence justifies the added coordination cost.
- The first slice should use a configurable roster of four persistent **Actor-led Parties**, with one **Actor Live Slot** per player-present zone and an initial configurable global soft limit of two **Actor Live Slots**.
- Player-visible population takes activation priority over off-screen consequential objectives; combat, loot, trade, and death must defer until authoritative execution is available rather than being invented by coarse progress.
- Under capacity pressure, reclaim **Actor Live Slots** from unobserved city downtime first, then unobserved travel or hunting at a safe checkpoint; never dematerialize a player-visible party or one resolving a consequential action, and age deferred work to prevent starvation.
- An unobserved party has an initial configurable two-minute live grace period, but capacity pressure may reclaim it sooner at a safe boundary.
- **Believable Downtime** should follow a persistent city recovery/resupply, route travel, objective, and return cycle with configurable minimum dwell times. Begin with a tunable city-heavy weighting near half the roster rather than a hard city quota, then compare balanced and objective-heavy strategies using feel and runtime telemetry.
- Active-zone capacity and **Actor Live Slots** are separate controls. Begin with an **Actor Resolution Zone Reserve** of one, do not keep its zone booted unnecessarily, never displace a player zone, and defer the actor action when the reserve cannot be used.
- A nearby player implies that the player's zone is already live; an **Actor-led Party** becoming visible there does not require booting another zone process.
- **Materialization Reconciliation** must preserve already-conserved actor state and must not invent combat, loot, death, trade, or currency outcomes merely to place the party in the live zone.
- A player interaction can produce zero or more **Authored Dialogue** responses.
- A player interaction can produce at most one **Fallback Dialogue** response.
- The first **Fallback Dialogue** iteration applies only to **Targeted Say**.
- The first **Fallback Dialogue** iteration applies to NPCs and bots, not mercenaries.
- The first **Fallback Dialogue** iteration skips engaged NPC targets but allows engaged bot targets.
- The first **Fallback Dialogue** prompt uses **Live Context** only.
- If **Fallback Dialogue** cannot be generated, the first version shows an **Unavailable Reply** instead of a technical error.
- Successful **Fallback Dialogue** is presented as ordinary target speech.
- An **Unavailable Reply** is presented as a runtime emote from the target.
- **Dialogue Rules** configure the first **Fallback Dialogue** implementation.
- **Fallback Dialogue Settings** capture **Dialogue Rules** for code paths that should not read server rules directly.
- **Fallback Dialogue Settings** may include ordinary game rules such as say range when **Current Interaction** validation needs a testable runtime snapshot; those values do not become **Dialogue Rules**.
- **Fallback Dialogue Settings** are stable for the lifetime of the zone process unless an explicit reload path is added later.
- Zone runtime owns loading **Fallback Dialogue Settings** from server rules; common **Fallback Dialogue** logic receives settings rather than reading rules directly.
- **Fallback Dialogue Settings** should keep responsibility boundaries visible so eligibility, public context construction, delivery planning, current-interaction validation, and provider calls do not all depend on every setting.
- Common **Fallback Dialogue** APIs should prefer explicit **Fallback Dialogue Settings** over convenience wrappers that read server rules directly.
- **Fallback Dialogue** feature eligibility is separate from remote provider availability; invalid provider settings should lead to an **Unavailable Reply** path, not redefine whether **Fallback Dialogue** is enabled.
- Remote **Fallback Dialogue** generation produces **Delayed Dialogue** and must not block normal zone chat handling.
- **Delayed Dialogue** is emitted only when the **Current Interaction** still holds.
- The first **Fallback Dialogue** prompt sends **Public Gameplay Context** only.
- **Public Gameplay Context** excludes operator-only status such as GM status; generated interactions should not change based on operator privileges.
- A **Dialogue Cooldown** limits how often one speaker can trigger **Fallback Dialogue** for the same target.
- The first **Fallback Dialogue** implementation accepts a single generated **Dialogue Response**.
- A **Dialogue Response** can produce multiple ordered **Dialogue Fragments**.
- Long speech splitting is a **Delivered Dialogue Message** concern, not a change to the **Dialogue Response** structure.
- **Dialogue Response Processing** and **Dialogue Delivery Planning** are separate concerns even when implemented near each other.
- Long emote **Dialogue Fragments** are rejected rather than split unless runtime evidence shows long generated emotes need a different policy.
- **Dialogue Responses** use **Natural Dialogue Format** because the first remote response service target is a smaller local model optimized for speed, not strict structured output reliability.
- Mixed speech uses asterisk **Stage Direction Markers** for emote **Dialogue Fragments**; parenthesized **Stage Direction Markers** are reserved for whole-response emotes to avoid treating ordinary parenthetical speech as emotes.
- **Fallback Dialogue** prompts should allow multiple ordered **Dialogue Fragments** without explicitly encouraging verbose or multi-message responses.
- New or refactored **Fallback Dialogue** implementation surfaces should use **Dialogue Response**, **Dialogue Fragment**, and **Delivered Dialogue Message** language; older internal `Dialogue Line` names can be retired incrementally when touched.
- If any part of a **Dialogue Response** is an **Unsafe Dialogue Fragment**, reject the whole **Dialogue Response** and show an **Unavailable Reply**.
- **Bot Loot Request** is separate from **Fallback Dialogue** because it is triggered by loot events rather than missing **Authored Dialogue**.
- **Loot Request Dialogue** may use remote generation to phrase a **Bot Loot Request**, but remote generation must not decide whether a bot wants the item.
- The first **Bot Loot Request** implementation triggers only after a player successfully loots an item.
- A **Bot Loot Request** is advisory in the first implementation; it does not reserve, prevent, redirect, or automatically assign loot.
- The first **Bot Loot Request** implementation considers only spawned bots in the looter's current group.
- The first **Bot Loot Request** implementation considers only equippable gear, not consumables, tradeskill items, spell scrolls, bag clickies, or quest-like items.
- **Bot Loot Request** eligibility may include No Drop gear because bot equipment commands can equip bots outside ordinary player trade restrictions.
- **Bot Loot Request** eligibility rejects items that would create a lore conflict for the requesting bot.
- The first **Bot Loot Request** upgrade score is a simple deterministic comparison between the looted item and the bot's currently equipped item for an eligible slot.
- Future **Bot Loot Request** scoring should use **Bot Gear Value** rather than raw item stat totals when the server-applied item effect differs by bot class or equipment slot.
- **Bot Gear Value** should be based on item instances when available so augments and recommended-level scaling are represented in the deterministic comparison.
- A **Bot Loot Request** should not be produced for an item above the requesting bot's required level, and **Bot Gear Value** should scale recommended-level item effects when the bot is below the recommended level.
- **Bot Gear Role** should split Warrior, Paladin, and Shadow Knight rather than treating them as one tank role because their mana-stat benefits differ.
- **Bot Gear Role** should split hybrids when item effects differ by class, including Paladin, Shadow Knight, Ranger, Beastlord, and Bard.
- Weapon **Bot Gear Value** should use weapon-specific comparisons such as damage, delay, hand constraints, dual-wield eligibility, two-hander replacement cost, proc signals, and class-specific weapon behavior rather than treating weapons as ordinary stat items.
- Ranged **Bot Gear Value** should be conservative and depend on effective ranged context such as ranged bot mode, compatible ammunition, and class behavior rather than treating range and ammo slots as ordinary stat gear.
- Effect-based **Bot Gear Value** should include only server-applied item effects with clear low-noise valuation, and should exclude click effects until bots routinely use clicked item effects.
- **Bot Gear Value** may produce an internal category breakdown for testing and reason summaries, but **Loot Request Dialogue** should receive only compact public intent rather than scoring weights, raw stat dumps, spell IDs, or full inventory details.
- Richer **Bot Gear Value** may change which bot wins a **Bot Loot Request** when class-specific server-applied value differs from the first raw-stat scoring model.
- When an item can equip in multiple slots, **Bot Loot Request** scoring uses the best valid replacement slot for each bot.
- A **Bot Loot Request** requires a minimum positive upgrade score before dialogue generation is allowed.
- If multiple bots are eligible for the same looted item, the first **Bot Loot Request** implementation selects one requesting bot by highest deterministic upgrade score.
- Tied **Bot Loot Request** scores are resolved by current group order.
- The first **Bot Loot Request** implementation emits at most one visible request for a single loot event.
- **Bot Loot Request** spam control is keyed by looter and requesting bot rather than by the whole group.
- The first **Loot Request Dialogue** implementation is delivered to group chat rather than local say.
- Remote **Loot Request Dialogue** generation produces delayed phrasing and must not block normal looting.
- Delayed **Loot Request Dialogue** is emitted only if the looter and requesting bot still exist and are still in the same group.
- **Loot Request Dialogue** prompts include only compact request intent, not raw item stat dumps, full inventories, account data, corpse details, or scoring weights.
- **Loot Request Dialogue** prompts may include the requesting bot, looter, item name, target equipment slot, and a deterministic reason summary.
- The first **Loot Request Dialogue** implementation accepts only one short speech line, not multiple fragments or emotes.
- **Loot Request Dialogue** delivery uses server-built item links; remote generation must not create or alter item link markup.
- If **Loot Request Dialogue** cannot be generated, the bot still sends a deterministic template request for the already-decided **Bot Loot Request**.
- **Loot Request Dialogue** generation failure does not use **Unavailable Reply** because the gameplay request itself is still valid.
- The first **Bot Loot Request** implementation should be disabled by default behind server settings.
- The first **Bot Loot Request** implementation does not transfer items or mutate bot inventory.
- **Bot Loot Request** decision behavior should be testable without remote **Loot Request Dialogue** generation.
- Bot combat-maintenance behavior may consider **Engaged Hostiles** beyond the bot's current target, but should not treat every nearby NPC as part of the fight.
- Bot slow maintenance should remain eventual rather than bursty: a bot may choose an unslowed **Engaged Hostile** beyond its current target during a normal slow opportunity, but should not cast multiple slow spells in one AI pass.
- Bot slow maintenance should prefer the bot's current target first, then other unslowed **Engaged Hostiles** threatening the owner, group, or raid, then hostiles threatening group or raid pets, with proximity as a tie-breaker.
- Bot slow maintenance should treat a hostile as needing slow when a candidate slow spell passes ordinary spell validity and stacking checks; it should not maintain a separate domain notion of slowed state unless runtime evidence shows existing spell checks are insufficient.
- The first bot slow-maintenance improvement should apply to single-target `Slow` behavior only; `AESlow` should keep its existing area-target-count and area-safety behavior until a separate need is proven.
- Bot slow maintenance should be the default behavior for bots that already have single-target `Slow` enabled, unless code exploration shows hostile enumeration is risky enough to require an opt-in rule.
- The first bot slow-maintenance improvement should skip mezzed **Engaged Hostiles**. Slowing mezzed hostiles may be acceptable later, but should wait until the implementation can make spell-break safety explicit.
- Bot slow maintenance should continue searching other **Engaged Hostiles** during the same slow opportunity when the current target is already slowed or otherwise does not need a slow. The slow opportunity should count as successful only when the bot begins casting a slow.
- Bot slow maintenance may bound how many candidate **Engaged Hostiles** it examines in one slow opportunity; bounded search should delay maintenance in unusually large fights rather than change ordinary group-fight behavior.
- Bot slow maintenance is intended to cover the owner's group or raid combat situation. A first implementation may fall back to group-only if raid-wide hostile discovery is not practical without broader combat-tracking changes.
- The first bot slow-maintenance improvement should rely on existing spell casting and stacking checks to avoid duplicate slow casts from multiple bots. Explicit cross-bot slow coordination should wait for runtime evidence that duplicate casts are a real problem.
- The first bot slow-maintenance implementation should stay specific to single-target `Slow`; a broader hostile-maintenance primitive should emerge only if the implementation naturally supports it without pulling in snare, debuff, dispel, mez, or root semantics.
- **Incoming Damage Pressure** may influence bot heal type selection, but should not replace ordinary heal thresholds, recast checks, mana limits, holds, aggro checks, or spell validity rules.
- **Incoming Damage Pressure** applies to healable player, bot, and pet targets during combat, and should expire quickly when recent damage stops.
- **Incoming Damage Pressure** belongs to the heal target's recent combat state, not to the bot healer choosing a spell.
- **Incoming Damage Pressure** should measure recent damage taken only; healing received should affect current HP but should not reduce the pressure signal directly.
- **Incoming Damage Pressure** should use final positive HP loss from combat damage sources, excluding obvious self-inflicted or non-combat environmental damage when the server can identify those cases cheaply.
- **Incoming Damage Pressure** should be represented as lightweight rolling aggregate state rather than a detailed combat event history.
- Bot heal type selection should use **Healing Danger Window** rather than raw **Incoming Damage Pressure** when comparing targets with different maximum HP or survivability.
- Pressure-aware bot healing should apply to healable targets already considered by existing bot healing behavior, not introduce a new healing target-selection model.
- **Healing Danger Window** should account for whether a candidate heal can land before the target crosses dangerous HP thresholds, while recast availability remains an ordinary spell validity check.
- Pressure-aware bot healing should use existing bot heal HP thresholds as the dangerous HP thresholds, and add only time-window configuration for conservative projection.
- **Bot Heal Selection** owns choosing the concrete spell for single-target bot heal families before the cast begins; it should not replace target selection, recast timers, mana checks, holds, aggro checks, or ordinary spell validity rules.
- **Bot Heal Selection** produces both the selected bot heal spell type and the concrete heal spell; the selected spell type may differ from the requested spell type when pressure-aware escalation or **Recovery Healing** applies.
- **Bot Heal Selection** owns **Recovery Healing** HoT substitution only when starting from a single-target direct-heal request; primary HoT requests, group heals, group HoTs, and complete heals remain separate behavior until runtime evidence supports broadening the module.
- Heal rotation behavior should remain outside the first **Bot Heal Selection** pass because heal rotations have separate cadence and fast-heal behavior.
- Efficient regular-heal choice is a **Bot Heal Selection** policy, not a separate gameplay-facing concept.
- The first pressure-aware bot healing settings should include feature enablement, the **Incoming Damage Pressure** sample window, and the **Healing Danger Window** projection only, not a separate HoT sustain window, per-class weights, or per-spell coefficients.
- HoT-based bot healing should be treated as low-pressure sustain; **Incoming Damage Pressure** should suppress HoT selection when the target is in active danger.
- Low-pressure HoT sustain means the target is not projected to cross a direct-heal threshold soon; low pressure may allow HoT selection, but should not force HoTs ahead of ordinary heal settings.
- Missing or expired **Incoming Damage Pressure** should prefer HoT-based sustain when an existing valid HoT option is available, while falling back to ordinary direct-heal behavior when HoTs are unavailable, held, invalid, unsafe to cast, or the target is already inside an emergency direct-heal threshold.
- No-pressure HoT preference should be part of the first pressure-aware healing feature flag rather than a separate setting.
- The first no-pressure HoT preference should apply only to single-target HoT healing, not group HoTs.
- The first pressure-aware bot healing implementation should not suppress valid direct heals under low pressure; direct-heal conservation belongs to existing settings and separate efficient spell-selection work.
- Complete-heal bot behavior should remain unchanged in the first pressure-aware healing implementation; conflicts between **Healing Danger Window** and complete-heal timing should be investigated separately if runtime evidence shows a problem.
- **Recovery Healing** should not use stale **Incoming Damage Pressure** from a previous fight; existing out-of-combat heal settings should control how bots recover the group after active danger has passed.
- **Incoming Damage Pressure** should gate or escalate bot heal type selection inside the existing bot heal settings model rather than replacing user-visible spell type priorities.
- Pressure-aware bot healing should keep the pressure decision in a small testable helper, with bot AI integration limited to gathering runtime context and applying the helper's decision.
- The first pressure-aware bot healing implementation should be server-operator opt-in until runtime evidence shows it preserves survivability and improves heal choice quality.
- Pressure-aware direct-heal escalation should choose the least urgent direct heal type that is projected to land safely, not always jump to the fastest heal.
- Pressure-aware bot healing should optimize for target survival before mana efficiency, and for mana efficiency before subjective healing feel.
- The first pressure-aware bot healing implementation should rely on existing per-target recast timers and heal rotations rather than adding cross-healer coordination.
- Raid-scale bot healing coordination should be investigated separately from first-pass **Incoming Damage Pressure** behavior.
- Pressure-aware bot healing should not produce player-visible explanations for pressure-based choices; diagnostics should use tests, logs, or developer instrumentation instead.
- Pressure-aware bot healing should be proven with deterministic decision tests before live smoke testing against real bot spell lists and zone combat timing.
- The first pressure-aware bot healing implementation should document where **Incoming Damage Pressure** is updated and where bot heal selection reads it.
- **Incoming Damage Pressure** should never make emergency bot healing less safe; it may escalate to faster or stronger heal types under pressure, but should not delay `VeryFastHeals` or `FastHeals` when a target is already inside emergency thresholds.
- Smarter `RegularHeal` spell choice should stay inside the existing bot spell settings model rather than becoming a new stance or domain concept.
- When enabled, smarter `RegularHeal` spell choice should apply across bot stances; stance should continue to control thresholds, delays, priorities, holds, and mana limits.
- Smarter `RegularHeal` spell choice should prefer the smallest valid direct heal that covers the target's missing HP plus a conservative margin, and should fall back to the current priority-first spell choice when heal amount cannot be estimated cleanly or no sufficient candidate exists.
- Smarter `RegularHeal` spell choice may choose across existing bot spell entry priority order once the server operator has opted in; ordinary cast validity and bot spell settings remain authoritative, while priority/list order becomes a stability tie-breaker after sufficient heal amount, overheal, and mana cost.
- Smarter `RegularHeal` spell choice should be controlled by a global server rule defaulting off in the first implementation, not by a new player command or per-bot setting. A narrow safety-margin rule may be added if implementation needs operator-tunable conservatism.
- The first smarter `RegularHeal` spell-choice implementation should not change `PetRegularHeals`; pet regular-heal selection can reuse the same approach later if ordinary pet-healing waste becomes important.
- **Bot-Aided Tracking** should work through the requesting player's owned spawned bots, not through any capable bot in the player's group.
- **Bot-Aided Tracking** should not grant the player the Tracking skill or bypass native class tracking rules.
- The first **Bot-Aided Tracking** implementation should use a bot-produced tracking report popup rather than the native client Track window.
- A **Bot-Aided Tracking Report** is the selected report content before any popup, private message, or bot chat presentation is applied.
- **Bot-Aided Tracking** should use the owned bot's class and level to determine whether a report is available, but center the reported search area on the requesting player.
- The first **Bot-Aided Tracking** report should list trackable NPC spawns only, not clients, bots, pets, familiars, mercenaries, or corpses.
- **Bot-Aided Tracking** should respect whether a reported spawn is visible to the requesting player; bot tracking should not reveal spawns hidden from that player through ordinary invisibility checks.
- A **Bot-Aided Tracking Report** should be selected from spawns already visible to the requesting player; hidden-spawn handling belongs to ordinary runtime visibility rules, not report selection.
- **Bot-Aided Tracking** rare filtering should use **Rare Spawn** classification from the existing rare-spawn data flag.
- Spawn chance, spawn limits, killed-named logging, raid conventions, and name-shape heuristics should not expand **Rare Spawn** classification until real data gaps are found and reviewed.
- The first **Bot-Aided Tracking** report should include approximate distance in the popup, but should not provide live direction updates or set the player's tracking target.
- **Bot-Aided Tracking Report** distance should be a rounded horizontal distance from the requesting player to the spawn, not a direction, pathing route, or vertical-distance-aware navigation hint.
- **Bot-Aided Tracking** reports should sort trackable NPC spawns nearest-first, with con color as presentation rather than the primary grouping.
- **Bot-Aided Tracking Report** entries at the same approximate distance should sort by clean player-facing spawn name rather than by entity ID or spawn metadata.
- The first **Bot-Aided Tracking** implementation should preserve the current bot class, level, and range tiers rather than introducing bot Tracking skill progression.
- **Bot-Aided Tracking** should not interrupt the reporting bot's current spell cast or combat action in the first implementation.
- **Bot-Aided Tracking** may be used in combat as a passive report, but should have a small reuse cooldown to prevent popup spam.
- The **Bot-Aided Tracking** popup should be sent only to the requesting player; any bot chat announcement should remain short and non-essential.
- **Bot-Aided Tracking** should show clean player-facing spawn names, not raw NPC names, spawn IDs, entity IDs, or database metadata.
- **Bot-Aided Tracking** should use a popup when eligible spawns are found and a brief private message when no eligible spawns are found.
- **Bot-Aided Tracking** reports should have an explicit result cap, with truncation communicated as part of the report when more eligible spawns exist.
- The first **Bot-Aided Tracking Report** result cap should be 50 entries and should be part of report selection rather than popup formatting.
- The first **Bot-Aided Tracking** report should be informational only, with no clickable target selection, no tracking target assignment, and no automatic navigation.
- The first **Bot-Aided Tracking** cleanup should not add a new server rule; existing bot command access controls are sufficient for an explicit tracking command.
- **Bot-Aided Tracking** should be validated with deterministic report-selection tests before live smoke testing Ranger, Druid, and Bard bot tracking commands in a real zone.
- An **Autonomous Actor** may be represented by an NPC, bot, mercenary, or future player-like server agent, but existing feature names such as **Bot-Aided Tracking**, bot slow maintenance, and **Fallback Dialogue** remain the right terms for those specific behaviors.
- An **Autonomous Actor** is not the same as a player because players provide direct intent; an **Autonomous Actor** acts without direct player input.
- **Fallback Dialogue** may be one action available to an **Autonomous Actor**, but **Fallback Dialogue** alone does not make a target an **Autonomous Actor**.
- Test-only direct state mutation may support setup and reset, but **Autonomous Actor** behavior should act through normal gameplay paths where practical.
- The first **Autonomous Actor** work should use owned spawned bots as the actor boundary; NPCs, mercenaries, and player-like server agents should wait for later actor-specific design.
- A test-spawned NPC controlled by the **Zone Harness** is a fixture unless it is explicitly running **Autonomous Actor** behavior.
- The first **Autonomous Actor** work should be reactive and bounded, responding to existing gameplay opportunities such as combat ticks, damage events, tracking commands, loot events, and **Targeted Say** rather than introducing long-running goal planning.
- Long-running goals such as exploring, farming, travelling, pulling, or maintaining cross-session conversation memory should wait for separate actor design after the **Zone Harness** proves the perception-action-event loop.
- The first **Autonomous Actor** behavior for owned bots should stay inside existing bot controls such as stance, spell holds, spell priorities, mana thresholds, owner and group membership, class, level, spell lists, equipment, and reuse cooldowns.
- New actor-specific settings should be added only when existing bot controls cannot express an important safety boundary.
- **Actor Perception** can include tactical state available to server-controlled gameplay behavior; **Public Gameplay Context** is the dialogue-safe subset suitable for remote response generation.
- **Actor Perception** should represent what the **Autonomous Actor** can legitimately know through gameplay rules, not the omniscient state a **Zone Harness** can inspect for diagnostics.
- **Actor Perception** should not include hidden spawn metadata, database identifiers, future spawns, GM-only state, or invisible entities unless ordinary gameplay rules make that information available to the actor.
- The first **Actor Perception** and **Actor Event** uses should support deterministic server logic, tests, and diagnostics; they should not be treated as remote model payloads.
- Remote dialogue generation should continue using **Public Gameplay Context** or a separately designed safe export format rather than raw **Actor Perception**.
- **Zone Harness** diagnostic snapshots may include omniscient fields for assertions, but those snapshots should not be treated as **Actor Perception**.
- An **Actor Action** expresses intent such as saying, targeting, moving, assisting, casting, attacking, tracking, or looting; it should not describe a forced outcome such as directly applying a buff, setting HP, forcing spell success, or marking an NPC as tracked.
- **Actor Actions** should be intent requests when ordinary server rules exist for the behavior, so spell validity, range, line of sight, cooldowns, mana checks, aggro checks, holds, priorities, and resist outcomes remain authoritative.
- One **Actor Objective** transition consumes one bounded observation or outcome and emits zero or one **Actor Action**; it must not emit a phase action without a concrete phase payload.
- An **Actor Routine** selects another **Actor Objective** only when no objective is active; an **Actor Objective** never creates or contains another objective.
- An **Actor Objective Template** is available to an **Actor Routine** only when every required action and postcondition in that template is supported by authoritative runtime capabilities.
- Exhausting an **Action Retry Budget** clears the concrete phase payload and requires a fresh replacement plan before another phase action is emitted.
- An **Actor Replan Request** must match the replacement observation by request identity, phase, action generation, exact phase payload shape, and a concrete non-empty phase value before another phase action is emitted.
- An abandoned **Actor Objective** has no live **Actor Replan Request** or recovery state.
- A temporarily deferred **Actor Action** creates an **Actor Replan Request** without spending **Action Retry Budget** or **Objective Viability Allowance**; a structurally blocked action remains a retry failure.
- Actor interruption preserves objective phase and active-versus-replanning resume state, spends **Objective Viability Allowance** rather than **Action Retry Budget**, and requires correlated recovery readiness before resuming.
- Actor interruption observations use a persistent ordered watermark for snapshot-idempotent deduplication and advance an action-generation fence before recovery is emitted, so late attempts and outcomes from older generations cannot execute or advance the objective.
- An **Actor Event** may be captured by tests or diagnostics, but not every **Actor Event** needs to be player-visible.
- The first **Zone Harness** use of **Actor Events** should be ephemeral test observation, not durable world history, replay, analytics, learning memory, or cross-session actor state.
- Durable memory for future **Autonomous Actors** should be designed separately rather than inferred from the first **Zone Harness** event drain.
- The **Zone Harness** is a proving tool for gameplay behavior; it may exercise **Autonomous Actor** behavior but is not itself part of the **Autonomous Actor** domain.
- **Zone Harness** shortcuts for setup and reset should remain visibly separate from ordinary gameplay actions that future **Autonomous Actors** might use.
- The first **Zone Harness** scenarios should run in a separate one-off zone process by default, not attach to an already-running live zone.
- Persistent dev runtime and real-client checks should remain separate smoke tiers for world, login, zoning, packet, UI, and client-visible behavior.
- HTTP may be used as a **Zone Harness** transport for scripts and agents, but harness behavior should live behind typed in-process scenario, perception, action, and event interfaces rather than inside route handlers.
- **Zone Harness** scenarios should remain callable without HTTP when practical, so CLI checks and HTTP-driven checks can share behavior.
- The first **Zone Harness** tests should prefer synthetic in-process owners for server-observable behavior and reserve real connected clients for packet, UI, login, zoning, and final visual validation.
- A **Zone Harness** scenario should say explicitly when the behavior under test requires a real connected client instead of silently replacing client-visible behavior with a test shortcut.
- **Zone Harness** scenarios should prefer in-memory fixtures and read-only content/database access; persistent database mutation should be explicit, risk-classified, cleaned up or backup-gated, and avoided in the default bot slow maintenance scenario.
- **Zone Harness** scenarios may use bounded normal world processing and event polling, but should not directly force AI timers, spell cooldowns, recast readiness, or spell outcomes unless a lower-level helper is explicitly under test.
- Broad decision matrices should stay in fast unit or helper tests; **Zone Harness** scenarios should prove representative runtime wiring and cadence through normal zone and bot paths.
- Live client smoke should remain the validation layer for UI, packet, login, zoning, and subjective player-visible behavior that the **Zone Harness** cannot observe.
- The first full **Zone Harness** scenario should prove bot slow maintenance against **Engaged Hostiles**, because it exercises owned bots, combat state, spell selection, buff observation, and asynchronous world processing without requiring client UI.
- The first **Zone Harness** bot slow maintenance scenario should create **Engaged Hostiles** through existing combat or hate paths where practical; directly tagging an NPC as engaged is only acceptable as setup fallback and should not be the behavior being proven.
- Fully organic pulling, pathing, and client-driven combat can remain a later live-runtime smoke after the harness proves bot target selection against combat-relevant NPCs.
- The first **Zone Harness** bot slow maintenance pass condition should be that the bot begins a normal single-target `Slow` cast at the expected **Engaged Hostile**; slow buff application may be a secondary observation when spell landing can be made deterministic enough.
- Bot slow maintenance harness assertions should allow bounded eventual timing and should avoid depending on a specific slow spell ID unless the bot setup makes the spell list stable.

## Example dialogue

> **Dev:** "If a player hails an NPC with a quest script, should the generated response also run?"
> **Domain expert:** "No. The quest script is **Authored Dialogue**; generated text is only **Fallback Dialogue** when the game has nothing else to say."
>
> **Dev:** "Should nearby NPCs also react when the player speaks?"
> **Domain expert:** "Not in the first version. Start with **Targeted Say** so a player can intentionally test one target at a time."
>
> **Dev:** "Should generated responses inspect quest state, database lore, or historical faction records?"
> **Domain expert:** "No. The first version should use **Live Context** from the current zone moment and add deeper sources only after the basic behavior feels right."
>
> **Dev:** "What should the player see if the remote response service is unavailable?"
> **Domain expert:** "For testing, show an **Unavailable Reply** such as the NPC appearing distracted; do not expose a technical error in chat."
>
> **Dev:** "Should generated text and unavailable placeholders appear the same way?"
> **Domain expert:** "No. Generated **Fallback Dialogue** should be target speech, while an **Unavailable Reply** should be a target emote so failures are visible but not technical."
>
> **Dev:** "Should the remote response endpoint be hardcoded or configured in process config?"
> **Domain expert:** "Use **Dialogue Rules** for the first version so the feature, endpoint, model, timeout, and local context limits can be adjusted like other chat behavior."
>
> **Dev:** "Should the zone wait for the remote response before finishing chat handling?"
> **Domain expert:** "No. Remote generation can take a while, so generated responses are **Delayed Dialogue** and normal chat handling should return immediately."
>
> **Dev:** "What if the target despawns, the player moves away, or the player targets something else before the generated response is ready?"
> **Domain expert:** "Then the **Current Interaction** no longer holds; drop the **Delayed Dialogue** silently for players and log the reason for testing."
>
> **Dev:** "Can the prompt include account data, IDs, private chat, inventory, or quest globals?"
> **Domain expert:** "No. Send only **Public Gameplay Context** such as the current message, public character and target summaries, zone identity, and nearby entity summaries."
>
> **Dev:** "Should mercenaries participate in generated responses?"
> **Domain expert:** "Not in the first version. Start with NPCs and bots because they are the primary target types for **Fallback Dialogue**."
>
> **Dev:** "Should generated replies run while the target is in combat?"
> **Domain expert:** "Skip engaged NPCs for the first version, but allow bots to answer because they are player companions."
>
> **Dev:** "Are normal chat anti-spam rules enough to protect remote generation?"
> **Domain expert:** "No. Add a **Dialogue Cooldown** for each speaker and target so normal speech can continue without flooding the remote response queue."
>
> **Dev:** "Can generated responses include multiple commands, mechanics, or out-of-character explanations?"
> **Domain expert:** "No. A **Dialogue Response** can contain multiple ordered **Dialogue Fragments**, but every fragment must stay in-character and safe to deliver."

## Flagged ambiguities

- "NPC response" may refer to quest scripts, task hails, NPC emotes, bot scripts, mercenary scripts, or generated text. Resolved: existing game-provided responses are **Authored Dialogue**; generated responses are **Fallback Dialogue**.
- Generated emotes may become a future output form, but the first version uses speech for successful **Fallback Dialogue** and reserves emotes for **Unavailable Reply**.

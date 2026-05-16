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

## Relationships

- **Fallback Dialogue** must not replace **Authored Dialogue**.
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

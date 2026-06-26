# Actor Event And Perception Expansion Spike

This spike covers `central-lhy.6`: expand **Actor Event** and **Actor Perception** coverage for the **Zone Harness** safely, without turning the harness into a full-zone export or durable telemetry system.

## Current coverage inventory

Current `ActorEventRecorder` coverage is intentionally narrow.

- `zone/harness/actor_event_recorder.{h,cpp}` stores an in-memory ring buffer with `max_events = 512`.
- Only `spell_cast_started` is recorded today.
- The only runtime hook is `EQ::ZoneHarness::ActorEventRecorder::ObserveSpellCastStarted(...)` from `zone/spells.cpp`.
- `GET /api/v1/harness/events` already supports cursor-style reads with `since` and `limit`, with `limit` clamped to `1..1000`.
- `POST /api/v1/harness/events/drain` clears the in-memory buffer.
- `GET /api/v1/harness/entities` is an omniscient harness snapshot, not **Actor Perception**. It returns global counts plus a sample of arbitrary zone entities.

That means the current harness can prove a bounded cast-start event, but it cannot yet explain actor-visible chat, target changes, movement outcomes, combat outcomes, or the actor-scoped view that `central-lhy` needs.

## Recommended event expansion

The next slice should keep the recorder ephemeral and add only low-cardinality, gameplay-facing event types that are already visible in ordinary play.

### 1. `speech_emitted`

Purpose:
Capture actor-visible `say` and `emote` output without replaying channel traffic or private tells.

Primary hook boundary:

- final local `say`/`emote` broadcast branches such as `entity_list.MessageCloseString(...)` in `zone/mob.cpp`

Payload additions:

- `channel`: `say` or `emote`
- `text`: final emitted text, truncated
- `audible_radius`: current local radius when known

Bounds:

- Store only the final rendered local speech/emote text.
- Do not emit `speech_emitted` from dialogue-window delivery branches that return before local speech broadcast.
- Capture text after saylink or other ordinary broadcast-time transformations, not raw method-entry text.
- Truncate `text` to 160 bytes.
- Do not capture shouts, tells, guild, raid, auction, or OOC as part of this slice.

### 2. `target_changed`

Purpose:
Explain when an actor changed or cleared its target.

Primary hook point:

- `Mob::SetTarget(...)` in `zone/mob.cpp`

Payload additions:

- `previous_target`
- `target`
- `reason`: initially omit unless a later caller can supply one cheaply

Bounds:

- Record only when the target pointer actually changes.
- Allow `target = null` for clear-target events.

### 3. `movement_requested`

Purpose:
Capture bounded movement intent without per-frame path spam.

Primary hook boundary:

- high-level navigation request entry points such as `NavigateTo(...)` and path-update callers before they expand into queued path nodes
- optional later explicit teleport request sources, not low-level teleport/move queue commands

Payload additions:

- `movement.kind`: `move_to`, `swim_to`, optional later `teleport`
- `movement.mode`: walking/running when available
- `movement.destination`: `{x,y,z}` rounded to coarse precision

Bounds:

- Emit one event per actor movement intent, before the request is expanded into internal path nodes.
- Do not hook raw `PushMoveTo(...)` / `PushSwimTo(...)` queue insertion without source-level deduping; those calls are used for path-node construction and replans.
- Do not emit path node details, heading corrections, rotate/stop commands, or every packet update.
- Ignore repeated path recalculations that point to the same coarse destination.

### 4. `movement_completed`

Purpose:
Capture that bounded movement intent finished.

Primary hook boundary:

- the high-level navigation request lifecycle, when the actor-level request reaches its coarse destination, is cancelled, or is abandoned

Notes:

- Queue-command completion alone is too low-level because one navigation request may produce many `MoveToCommand` or `SwimToCommand` path-node completions.
- It is safer than polling raw coordinates from the harness.

Payload additions:

- `movement.kind`
- `movement.destination`
- `movement.terminal_status`: `completed`, `cancelled`, or `abandoned`

Bounds:

- Emit once per terminal high-level actor movement request.
- Suppress internal path-node completion events.
- Do not attempt sub-step progress percentages.

### 5. `damage_taken`

Purpose:
Capture combat outcomes that matter to actor reasoning.

Primary hook points:

- `Mob::CommonDamage(...)` for shared post-mitigation values, if a stable observer can be inserted once
- Fallback: concrete overrides `Client::Damage`, `NPC::Damage`, `Bot::Damage`, `Merc::Damage`

Payload additions:

- `target`: damaged entity
- `source`: attacker if present
- `amount`
- `spell.id`
- `attack_skill`
- `damage_kind`: melee, spell, dot, other

Bounds:

- Record only positive applied damage.
- Skip zero, absorbed, or purely attempted hits in the first slice.
- Prefer a single shared post-mitigation hook over duplicating logic in each subclass.

### 6. `healed`

Purpose:
Capture successful healing outcomes.

Primary hook point:

- `Mob::HealDamage(...)` in `zone/attack.cpp`

Payload additions:

- `target`
- `source`
- `amount_requested`
- `amount_applied`
- `spell.id`

Bounds:

- Record only when `acthealed > 0`.
- Use applied heal amount, not the pre-cap request amount, as the main signal.

### 7. `death`

Purpose:
Capture combat terminal outcomes for actor logic and harness assertions.

Primary hook points:

- `Client::Death(...)`
- `NPC::Death(...)`
- `Bot::Death(...)`
- `Merc::Death(...)`

Payload additions:

- `target`: dead entity
- `source`: killer if present
- `amount`
- `spell.id`
- `attack_skill`

Bounds:

- Emit once per successful death path.
- Prefer the earliest shared point that runs after the death is accepted, not after corpse cleanup or depop side effects.

### 8. `action_blocked`

Purpose:
Capture bounded, actor-relevant failures where the actor attempted a normal action and the server rejected it.

Feasible first slice:

- blocked spell casts caused by `zone->IsSpellBlocked(...)`
- existing bot attack rejection paths that already surface `"I cannot attack [...]"` to the owner

Non-goals for the first slice:

- every failure-to-act branch in combat, pathing, or quest logic
- omniscient reasons such as hidden internal validation state
- blocked bot spell-cast internals that only log and return `false` without an owner-visible/public reason

Payload additions:

- `action.kind`: `cast_spell`, `attack`, optional later `move`
- `reason_code`: stable server-side enum/string such as `zone_blocked_spell` or `cannot_attack`
- `message`: short truncated public-facing text when one already exists
- `spell.id` or `target` when applicable

Bounds:

- Start with a small allowlist of well-understood blocked-action reasons.
- Do not try to normalize every `false` return from `CastSpell()` in this spike.
- Require a public or owner-visible rejection message before recording bot spell-block events.

## Recommended payload shape changes

`ActorEvent` needs to stop assuming every event is a spell-cast event. The simplest next shape is:

- keep the common envelope: `id`, `time_ms`, `type`, `message`
- rename `caster` to `actor` over time, or add `actor` and keep `caster` temporarily for compatibility
- add optional `source` and `subject`/`target` entities
- keep `spell` and `cast` optional
- add optional `movement`, `speech`, `combat`, and `blocked_action` sub-objects

That keeps event JSON sparse and bounded while allowing one recorder instead of separate per-type feeds.

## Actor Perception recommendation

Do not reuse `GET /api/v1/harness/entities` as **Actor Perception**. It is a harness diagnostic snapshot with omniscient zone visibility.

Add a new actor-scoped surface instead, backed by one explicit subject entity.

Suggested runtime API:

- `ZoneHarnessRuntime::PerceptionFor(uint16_t actor_id, const PerceptionQuery &query)`

Suggested HTTP surface:

- `GET /api/v1/harness/perception?actor_id=<id>`

Suggested bounded perception shape:

- `self`
  - `entity_id`, `name`, `kind`, `level`
  - coarse position
  - hp/mana/endurance percent
  - combat state
- `current_target`
  - entity summary or `null`
  - hp percent if target is visible
  - coarse distance bucket
- `nearby_entities`
  - capped list sorted by distance
  - only entities the actor could ordinarily know about
  - include coarse distance bucket and basic relation tags such as `group`, `owner`, `hostile`, `corpse`
- `group_state`
  - capped to the actor's current group or raid subgroup
  - member summary with hp/mana percent, alive/dead, role-like classification if cheaply inferable
- `inventory_summary`
  - summary only, not slot-by-slot export
  - equipped primary/secondary/range item names when relevant
  - aggregate counts for consumable categories or key resources if a caller needs them later
- `current_cast`
  - optional current spell id/name/remaining cast time when the actor is already casting

## Perception limits

The first **Actor Perception** slice should stay intentionally incomplete.

- Nearby entity cap: default `12`, hard max `25`
- Nearby entity ordering: nearest first
- Distance precision: use coarse buckets such as `melee`, `near`, `far` instead of raw float distances where possible
- Group scope: current group only, not whole raid, unless the actor is explicitly raid-coordinated later
- Inventory scope: summary only
- Target scope: current target only
- Hidden information exclusions:
  - no spawn timers
  - no database ids
  - no full hate lists
  - no invisible entities unless the actor can see invis
  - no full-zone entity dump
  - no quest/global/script internals
  - no GM, admin, guide, or operator-only flags
  - no account identifiers, account status, or account-level metadata
  - no ownership metadata beyond ordinary gameplay-visible relations such as pet owner or group member

## Cost controls

The event and perception work should keep the current bounded-harness posture.

- Keep the recorder in-memory and per-runtime only.
- Increase `max_events` modestly if needed, but stay bounded; `1024` is a safer ceiling than an unbounded vector.
- Keep `since` cursor reads and clamp limits server-side.
- Add a `types` filter for `GET /api/v1/harness/events` so callers can avoid draining irrelevant traffic.
- Keep `POST /api/v1/harness/events/drain` for scenario-style assertions.
- Perception should be pull-based only. Do not auto-stream perception snapshots.
- Add optional `sample_ms` or scenario-owned polling cadence guidance rather than recording every world tick.
- Round or bucket positions/distances before serialization.
- Default to no full-zone export and no slot-by-slot inventory export.

## Validation impact

The current `tier3-harness` suite proves only `spell_cast_started`.

Recommended follow-up harness scenarios after implementation:

- targeted say/emote scenario proving one `speech_emitted`
- target swap scenario proving `target_changed`
- bounded move scenario proving `movement_requested` then `movement_completed`
- direct damage/heal scenario proving `damage_taken` and `healed`
- kill scenario proving `death`
- blocked zone spell scenario proving `action_blocked`
- actor perception scenario proving caps, ordering, and hidden-field exclusions

## Summary

`central-lhy.6` should expand the harness from one spell-start event to a small actor-facing event vocabulary plus a new actor-scoped perception endpoint. The safest implementation path is to hook broad gameplay methods that already represent normal server behavior, keep all outputs bounded and ephemeral, and avoid turning diagnostic harness snapshots into actor-visible truth.

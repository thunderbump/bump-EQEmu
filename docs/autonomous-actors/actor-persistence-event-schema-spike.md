# Actor Persistence and Event Schema Spike

Date: 2026-06-26

Beads item: `central-lhy.3`

## Question

Which existing persistence tables can safely support **Autonomous Actor** identity, inventory, social context, goals,
status, action queue, and event history without pretending that bot-originated actions are ordinary player actions?

## Decision

Use existing bot and social tables only where the actor is explicitly bot-backed. Add actor-specific tables for actor
profiles, live/persisted status, action queue, and actor events before production actor reporting or planning depends
on persistence.

The first production shape should keep three identities distinct:

- `bot_id`: the existing gameplay substrate and inventory owner when the actor is bot-backed.
- `owner_character_id`: the reserved or real owner character required by current bot ownership and authority paths.
- `actor_id`: the actor-level identity used by planners, operators, action queues, and actor event history.

Do not use `character_data` as the actor identity store unless a later headless-`Client` branch proves that actors
should become real player characters. Do not force actor events into `player_event_logs`.

## Existing Table Reuse Boundaries

| Table or construct | Actor use | Boundary |
| --- | --- | --- |
| `bot_data` | Reuse for the bot-backed actor's spawn/save shell: name, appearance, class, level, stats, and `bot_id`. | It is owner-bound through `owner_id` and has no actor goal, queue, status, planner, or event fields. Treat it as substrate identity, not actor identity. |
| `bot_inventories` | Reuse for bot-backed actor equipment and inventory slots. | It is keyed by `bot_id`, so it cannot represent non-bot actors and should not store actor policy or inventory intent. |
| `group_id` | Reuse for current mixed group membership because it already has `character_id`, `bot_id`, and `merc_id`. | It has no actor identity column and should remain a gameplay social-state table, not an actor roster. |
| `raid_members` | Reuse for current raid membership when a bot-backed actor is represented as a bot. | It stores `charid` and `bot_id`, plus raid role flags, but no actor-level role, goal, or planner state. |
| `bot_guild_members` | Do not reuse for the first actor schema. | The bot schema manifest drops this table, while the generated repository still exists and is stale. Treat bot guild membership as a later rebuild if actors need it. |
| `character_data` | Keep for owner/reserved owner characters and real player characters. | It implies account/session/player lifecycle and has broad character state such as account, login, position, XP, leadership, hunger, and GM flags. Using it as actor identity would reintroduce the headless-`Client` risk. |
| Task tables | Reuse task content/templates as possible inspiration for actor goals. | Runtime task state is character-shaped: `character_tasks`, `character_activities`, `completed_tasks`, `character_task_timers`, and shared-task membership should not become actor state. |
| `player_event_logs` | Use as a precedent for async JSON event persistence, settings, retention, and indexing. | It is account/character-shaped and records through `Client::GetPlayerEvent()`; bot-originated actor events have no natural `account_id`/`character_id` without lying. |

## Failing Proof: Bot-Originated Actor Events Do Not Fit Player Events

The existing player event path records a `PlayerEvent::PlayerEvent` with `account_id`, `character_id`,
`character_name`, guild, zone, and position metadata. The `player_event_logs` repository stores `account_id` and
`character_id` as first-class columns. Event macros call `GetPlayerEvent()` or `RecordPlayerEventLogWithClient(c, ...)`,
which ultimately requires a `Client` identity.

Concrete event example: a future actor action queue asks bot-backed actor `actor_id = 12`, `bot_id = 44` to say a
report line or cast a spell. The observable result should be an **Actor Event** sourced by actor 12. Writing that to
`player_event_logs` has only bad options:

- use the owner client's `character_id`, which makes the event look player-originated;
- use a reserved owner character, which hides which actor acted;
- use zero or null `character_id`, which bypasses the indexed character-shaped query model and still lacks `actor_id`;
- add `actor_id` to `player_event_logs`, which changes a player audit table into a mixed actor/player event table.

That is the first unavoidable schema extension point. Actor events need their own actor-shaped event table while
player events stay player-shaped.

Concrete state example: bot loot requests already produce bot-originated gameplay intent. The looting path builds a
successful-loot event, the bot loot request runtime plans a request, and cooldown/dedup state currently lives in
in-memory containers. A zone restart loses pending request state and cooldown history, while `player_event_logs`,
character task tables, and bot state tables still have no natural actor-shaped row that says which bot/actor requested
what and why.

Concrete task example: shared task membership resolves character rows from `group_id.character_id` and
`raid_members.charid`, while `shared_task_members` stores `character_id`. Existing mixed social tables can carry bots
in group/raid state, but shared task runtime state has no natural bot/actor member identity.

## Minimal Actor Tables

These are schema shapes, not an implementation commitment for this bead.

```sql
CREATE TABLE actor_profiles (
  actor_id BIGINT UNSIGNED NOT NULL AUTO_INCREMENT,
  actor_type VARCHAR(32) NOT NULL,
  bot_id INT UNSIGNED DEFAULT NULL,
  owner_character_id INT UNSIGNED DEFAULT NULL,
  display_name VARCHAR(64) NOT NULL,
  enabled TINYINT(1) UNSIGNED NOT NULL DEFAULT 1,
  created_at DATETIME NOT NULL,
  updated_at DATETIME NOT NULL,
  PRIMARY KEY (actor_id),
  UNIQUE KEY actor_profiles_bot_id (bot_id),
  KEY actor_profiles_owner_character_id (owner_character_id)
);

CREATE TABLE actor_status (
  actor_id BIGINT UNSIGNED NOT NULL,
  zone_id INT UNSIGNED DEFAULT NULL,
  instance_id INT UNSIGNED DEFAULT NULL,
  entity_id INT UNSIGNED DEFAULT NULL,
  state VARCHAR(32) NOT NULL,
  status_json LONGTEXT CHARACTER SET utf8mb4 COLLATE utf8mb4_bin DEFAULT NULL CHECK (json_valid(status_json)),
  heartbeat_at DATETIME DEFAULT NULL,
  updated_at DATETIME NOT NULL,
  PRIMARY KEY (actor_id)
);

CREATE TABLE actor_action_queue (
  action_id BIGINT UNSIGNED NOT NULL AUTO_INCREMENT,
  actor_id BIGINT UNSIGNED NOT NULL,
  source VARCHAR(32) NOT NULL,
  action_type VARCHAR(64) NOT NULL,
  action_json LONGTEXT CHARACTER SET utf8mb4 COLLATE utf8mb4_bin NOT NULL CHECK (json_valid(action_json)),
  state VARCHAR(32) NOT NULL,
  not_before DATETIME DEFAULT NULL,
  expires_at DATETIME DEFAULT NULL,
  claimed_by VARCHAR(128) DEFAULT NULL,
  claimed_at DATETIME DEFAULT NULL,
  created_at DATETIME NOT NULL,
  updated_at DATETIME NOT NULL,
  PRIMARY KEY (action_id),
  KEY actor_action_queue_actor_state (actor_id, state, not_before)
);

CREATE TABLE actor_events (
  event_id BIGINT UNSIGNED NOT NULL AUTO_INCREMENT,
  actor_id BIGINT UNSIGNED NOT NULL,
  bot_id INT UNSIGNED DEFAULT NULL,
  owner_character_id INT UNSIGNED DEFAULT NULL,
  zone_id INT UNSIGNED DEFAULT NULL,
  instance_id INT UNSIGNED DEFAULT NULL,
  entity_id INT UNSIGNED DEFAULT NULL,
  event_type VARCHAR(64) NOT NULL,
  event_json LONGTEXT CHARACTER SET utf8mb4 COLLATE utf8mb4_bin NOT NULL CHECK (json_valid(event_json)),
  action_id BIGINT UNSIGNED DEFAULT NULL,
  created_at DATETIME NOT NULL,
  PRIMARY KEY (event_id),
  KEY actor_events_actor_created (actor_id, created_at),
  KEY actor_events_action_id (action_id),
  KEY actor_events_zone_created (zone_id, instance_id, created_at)
);
```

### Table Notes

- `actor_profiles` maps an actor to a substrate such as `bot_id` while preserving a stable actor-facing identity.
- `actor_status` is a small current-state row for operator views and planner discovery. Rich perception snapshots
  should remain bounded and generated by zone runtime, not dumped wholesale to the database every tick.
- `actor_action_queue` is a durable low-cadence command queue for sidecar/planner work. Zone-local harness actions can
  stay in memory until live ingress requires persistence.
- `actor_events` is the reporting/event history table. It intentionally records `actor_id` first and only mirrors
  `bot_id` / `owner_character_id` as correlation fields.

## Migration and Schema Drift Risks

- Generated base repositories are marked "DO NOT MODIFY"; actor tables should be added through the normal schema
  manifest path and then repository generation or hand-written extension repositories as appropriate.
- Current schema authority is the native database manifests, not old SQL dump folders. Main/runtime schema changes live
  in `common/database/database_update_manifest.h`; bot schema changes live in
  `common/database/database_update_manifest_bots.h`.
- Existing bot bootstrap SQL, bot manifests, and generated repositories do not always describe the same live shape.
  Validate `SHOW CREATE TABLE` in the target validation database before depending on bot table shape.
- Some generated repositories are unsafe for composite-key tables. For example, `group_id` has mixed member identity
  columns, but its base repository models only `group_id` as the primary key; new code should prefer explicit
  `GetWhere` / `DeleteWhere` / table-specific helpers over generic single-key helpers on those tables.
- `player_event_logs` already has follow-up manifest changes for compression/indexing. Actor event tables should copy
  the useful retention/indexing lessons, not share that table.
- JSON columns should follow the existing `LONGTEXT ... CHECK (json_valid(...))` pattern used by player events so old
  MariaDB compatibility stays visible.
- Foreign keys need caution. Bot bootstrap inventory has a `bot_id` foreign key, but many EQEmu tables rely on
  repository cleanup rather than full FK coverage. For an MVP, indexes plus explicit cleanup may be safer than adding
  cross-table FKs that surprise older installs.

## Recommendation

Keep the next runtime work bot-backed:

1. Continue using `bot_data` and `bot_inventories` for the actor's live gameplay body.
2. Use `group_id` and `raid_members` only for current social gameplay state.
3. Add actor-specific schema before persistent planner/reporting work: `actor_profiles`, `actor_status`,
   `actor_action_queue`, and `actor_events`.
4. Keep player audit/event history separate from actor event history.
5. Defer actual migration SQL until the next implementation bead, after deciding whether the first actor persistence
   slice is profile/status only or profile/status/events together.

## Source References

- `common/repositories/base/base_bot_data_repository.h`
- `zone/bot_database.cpp`
- `common/repositories/base/base_bot_inventories_repository.h`
- `common/repositories/base/base_group_id_repository.h`
- `common/repositories/base/base_raid_members_repository.h`
- `common/database/database_update_manifest_bots.h`
- `common/repositories/base/base_character_data_repository.h`
- `zone/zonedb.cpp`
- `common/repositories/base/base_player_event_logs_repository.h`
- `common/events/player_events.h`
- `zone/client.cpp`
- `zone/task_manager.cpp`
- `zone/task_client_state.cpp`
- `common/shared_tasks.cpp`
- `world/shared_task_manager.cpp`
- `zone/bot_loot_request_runtime.cpp`
- `common/bot_loot_request.cpp`

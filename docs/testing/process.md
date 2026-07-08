# Testing Process

This repo is validated through local AkkStack environments. Automated validation should run against a validation stack, while client-facing play and live smoke checks should use a separate gameplay stack.

ADR 0006 defines the portable automation contract: automation should call `scripts/validation-worker.sh run --request <request.json>` with a fetchable repo/ref or commit and an evidence directory. For local diagnostics, the same worker also accepts a local-checkout request path instead of a fetch source. Fetch requests clone into worker-owned storage, local-checkout requests validate the named checkout in place, and both paths acquire an exclusive validation slot, delegate to the project validation profile, and write mechanical evidence (`request.json`, `result.json`, and logs). Requests may include `stack.role: "validation"` and `stack.path` to select a validation AkkStack checkout; when present, the worker binds that stack's `code` symlink to the active checkout under the same exclusive lock and writes `stack-binding.json`. Direct path-based wrapper usage such as `scripts/validate.sh` remains supported for local diagnostics and narrowing failures, but it is not the portable automation contract because it assumes the caller can see the local checkout, AkkStack path, and Docker host.

Bootstrap from zero is a separate setup task. Do not fold `make install`, environment generation, data downloads, or first-time database setup into every validation pass.

Use `scripts/validation-worker.sh profiles --json` to discover the portable AFK-facing profiles and their rough mutation, timeout, and locking guidance. Today that discovery surface exposes `preflight`, `safe`, `tier3-harness`, and `tier1-tier3-harness`.

Submodule expectations are part of the worker contract. Fetch requests run `git submodule update --init --recursive` before validation. Local-checkout requests continue to work for diagnostics, but the worker treats missing or drifting submodules as request failures instead of mutating the caller-owned checkout.

## Environment Contract

- Use a validation AkkStack checkout for automated validation. The intended local path is
  `../bump-akk-stack-validation`.
- Use a separate gameplay AkkStack checkout for client-facing runtime and manual play. The current local gameplay
  path is `../bump-akk-stack`.
- The validation stack and gameplay stack should have separate `.env`, `server/eqemu_config.json`, Docker Compose
  project state, volumes, and `data/mariadb` directories.
- The validation stack `code` path should point at this checkout, preferably as a symlink to
  `/home/bump/Projects/bump-eqemu/bump-EQEmu`.
- Use local AkkStack `make` commands where possible.
- Do not change the gameplay stack unless a live/client smoke check specifically requires it.
- Treat the validation database as a persistent developer database, not a disposable test schema.
- Keep the validation `mariadb` service available through the canonical validation stack Compose config. Do not
  apply test-specific port-remap overlays to `mariadb`; those can cause Compose to recreate a long-running
  database container.
- Run automated `eqemu-server` validation through one-off validation containers when possible. A one-off validation
  container is a short-lived `docker-compose run --rm --no-deps --entrypoint bash eqemu-server ...` process that
  does not own the persistent gameplay server and does not publish host gameplay ports.

Role-aware wrappers select the stack with `--stack validation` or `--stack gameplay`. Validation-oriented
wrappers default to `--stack validation`; runtime-proof helpers default to `--stack gameplay`. `AKKSTACK_DIR`
remains an explicit custom-path override for diagnostics, and wrappers print the selected role and resolved path
before acting. Use `--dry-run` to preview the selected stack, resolved path, Compose files, and high-level action
without invoking Docker.

Validation wrappers print a warning when they are intentionally routed to `--stack gameplay` or to a custom path
that resolves to the gameplay default, because those commands may read or mutate persistent gameplay data.

Before running container validation, confirm the stack is initialized:

```sh
./scripts/check-akkstack-contract.sh --stack validation
./scripts/check-akkstack-contract.sh --stack validation --dry-run
```

If a task needs a custom stack path, keep the role explicit and use `AKKSTACK_DIR` only for the path override:

```sh
AKKSTACK_DIR=/path/to/custom-validation-stack ./scripts/check-akkstack-contract.sh --stack validation
```

Then keep the validation database running from the validation stack:

```sh
cd ../bump-akk-stack-validation
docker-compose -f docker-compose.yml -f docker-compose.dev.yml up -d --no-recreate mariadb
```

The preflight intentionally checks for the selected AkkStack `.env` without printing it. If `.env` is missing,
initialize the persistent AkkStack environment before running validation; do not generate or paste secrets as
part of an ordinary test pass.

The preflight also identifies whether the selected AkkStack `code` path is a symlink, a directory checkout, or an
invalid path. The default contract is that it resolves to `/home/bump/Projects/bump-eqemu/bump-EQEmu`. If it
points somewhere else, fix the AkkStack `code` mount to point at this checkout before validating changes from
this repo. A symlink is preferred when `code` is missing:

```sh
cd ../bump-akk-stack-validation
ln -s /home/bump/Projects/bump-eqemu/bump-EQEmu code
```

If an alternate checkout is intentionally accepted for a specific validation pass, make that explicit instead of relying on the default:

```sh
EXPECTED_EQEMU_CHECKOUT=/path/to/accepted/bump-EQEmu ./scripts/check-akkstack-contract.sh
```

## Tier 0: Static Sanity

Use this for every change before running heavier checks.

- Review the touched files and choose the lowest tier that covers the risk.
- Check for accidental generated-file churn.
- Check that no secrets were written into tracked files.
- For schema-sensitive work, identify whether the backup gate applies before running commands.

### Graphify Deterministic Update

When the local knowledge graph already exists, code-only graph refreshes can run without semantic extraction:

```sh
./scripts/graphify-deterministic-update.sh
```

This wrapper calls `graphify update <repo> --no-cluster`, so it is suitable for a local pre-commit hook or manual refresh after code changes. It does not run LLM semantic extraction, community labeling, full report generation, or HTML regeneration. If `graphify` or `graphify-out/graph.json` is not present, the wrapper skips cleanly.

## Tier 1: Container Build And Unit Tests

Use this as the default automated validation tier for code changes.

Run the full configured build inside the AkkStack dev container, with tests enabled, then run the CMake test binary:

```sh
git submodule update --init --recursive
cd ../bump-akk-stack-validation
docker-compose -f docker-compose.yml -f docker-compose.dev.yml run --rm --no-deps --entrypoint bash eqemu-server -lc 'cd ~/code && cmake --preset linux-debug && cmake --build build --parallel && ./build/bin/tests'
```

Use `docker-compose run` for Tier 1 instead of `make up` plus `exec`. The build and unit test pass does not need
server ports or a running database, and avoiding published service ports keeps this tier usable when local web or
game ports are already occupied. The `--entrypoint bash` override bypasses the image's normal server startup loop
so the validation command runs directly.

Host-native CMake is not the primary path because this codebase has specific runtime and dependency version expectations. The container is the closer match for local server development.

The repo-local wrapper for this tier is:

```sh
./scripts/validate.sh --stack validation tier1
./scripts/validate.sh --stack validation --dry-run tier1
```

The wrapper runs the AkkStack contract preflight first, then runs the raw Tier 1 command shown above from the
selected validation stack. Keep the raw command visible here so failures can still be reproduced or narrowed
manually.

Pressure-aware bot healing has deterministic unit coverage in `tests/pressure_aware_healing_test.h`. Live runtime
validation for real bot spell lists and combat timing is documented separately in
`docs/testing/pressure-aware-healing-smoke.md`.

Efficient `RegularHeal` selection has deterministic unit coverage in `tests/regular_heal_efficiency_test.h`, with
pressure ordering covered in `tests/pressure_aware_healing_test.h`. Live runtime validation for rule-off/rule-on
real bot spell-list behavior is documented separately in `docs/testing/efficient-regular-heal-smoke.md`.

Complete-heal parent-bucket fallback gating has deterministic coverage in `tests/bot_heal_selection_test.h`.
If live CompleteHeal fallback remains too eager before a fix is deployed, lower the CompleteHeal max threshold
(e.g. `^spellmaxthresholds completeheals 50 byname Wumpermup`) or hold CompleteHeal for that bot.

## Tier 2: DB-Backed CLI Tests

Use this when the change touches code that is exercised by existing `zone` or `world` command hooks.

Run targeted tests rather than the whole live server whenever possible. Read-mostly DB-backed validation should
run in a one-off validation container against the validation database, not by `exec` into the persistent gameplay
server:

```sh
cd ../bump-akk-stack-validation
docker-compose -f docker-compose.yml -f docker-compose.dev.yml up -d --no-recreate mariadb
docker-compose -f docker-compose.yml -f docker-compose.dev.yml run --rm --no-deps --entrypoint bash eqemu-server -lc 'set -euo pipefail
cd ~/server
~/code/build/bin/zone tests:npc-handins
~/code/build/bin/zone tests:npc-handins-multiquest'
```

`tests:databuckets`, `tests:zone-state`, and `tests:reserved-actor-owner` are intentionally omitted from the
default command block because they mutate persistent validation data. Use the raw `zone tests:*` commands for
those checks only after applying the backup gate and confirming the validation database is the intended target.

The repo-local wrapper covers only the read-mostly targeted zone checks:

```sh
./scripts/validate.sh --stack validation tier2-readonly
./scripts/validate.sh --stack validation --dry-run tier2-readonly
```

That command runs the preflight, starts or verifies validation MariaDB through the canonical Compose files with
`--no-recreate`, then runs `tests:npc-handins` and `tests:npc-handins-multiquest` as separate `zone` processes
inside a single one-off validation `eqemu-server` container. It intentionally does not run `tests:databuckets`,
`tests:zone-state`, or `tests:reserved-actor-owner`; use the raw commands after applying the backup gate when a
change specifically needs those caution-tier checks. The wrapper should not require the persistent gameplay
`eqemu-server` container to be running and should not `exec` into it.

Risk classification:

- Default: `tests:npc-handins` is read-mostly DB-backed validation. It boots `qrg`, loads NPC type
  `754008`, creates an in-memory `Client` and `NPC`, creates test item instances, and exercises hand-in
  matching/return logic. Observed implementation side effects are limited to in-memory entity/hand-in state;
  no persistent cleanup is required.
- Default: `tests:npc-handins-multiquest` is read-mostly DB-backed validation. It boots `qrg`, loads NPC type
  `754008`, enables multiquest on an in-memory NPC, creates test item instances, and resets hand-in state after
  each check. Observed implementation side effects are limited to in-memory entity/hand-in state; no persistent
  cleanup is required.
- Caution: `tests:databuckets` mutates persistent `data_buckets` rows. It starts by deleting the exact keys
  listed below without a scope filter, so matching rows for any character/account/NPC/bot/zone scope are removed.
  The test client then uses `character_id = 1` for normal `SetBucket`, `GetBucket`, and `DeleteBucket` calls,
  clears the process-local bucket cache several times, expires and deletes `expiring_key`, and directly inserts
  `scoped_db_only_key` for `character_id = 1`. It does not perform final cleanup, so expect scoped test rows to
  remain unless you delete them after the run.
- Caution: `tests:zone-state` mutates persistent `soldungb` state. It deletes and recreates
  `zone_state_spawns` for `zone_id = 32` and `instance_id = 0`, repeatedly saves/restores NPC, corpse, loot,
  buff, location, entity-variable, and zone-variable state, and leaves final `soldungb` state rows behind. It also
  creates the `zone_state_test` loottable/lootdrop rows with item `11621` if missing, does not remove them, and
  may replace and then delete `respawn_times` rows whose IDs match `soldungb` `spawn2` IDs. Use it only for
  zone-state or spawn persistence changes, preferably after the backup gate if the local database contains
  important `soldungb` state.
- Caution: `tests:reserved-actor-owner` provisions a reserved non-playable `character_data` shell with the
  `Actorowner` prefix, non-secret `last_name = ReservedActorOwner` marker, and zeroed `level/class/race`, saves a
  bot through that owner ID, inserts an `actor_profiles` row, reloads and respawns the bot through a harness-only
  synthetic owner client, then deletes the bot, actor profile, and reserved owner row again. The test also proves
  that a plain `Actorowner*` character row without the marker is not silently adopted, that a marker-bearing
  playable `Actorowner*` row is still rejected, and that a non-bot `actor_profiles` association does not activate
  reserved-owner lookup. The test is intended to be self-cleaning,
  but it still mutates persistent `character_data`, `bot_data`, and `actor_profiles` during the run, so keep it
  behind the backup gate when using a shared validation database.

`tests:databuckets` exact startup cleanup keys:

```text
basic_key, expiring_key, cache_key, json_key, non_existent_key, simple_key,
nested, nested.test1, nested.test2, nested.test1.a, nested.test2.a,
exp_test, cache_test, full_json, full_json.key2, complex, complex.nested.obj1,
complex.nested.obj2, plain_string, json_array, nested_partial,
nested_override, empty_json, json_string, deep_nested, nested_expire,
scoped_miss_test, scoped_nested_miss.key, cache_miss_overwrite,
missed_nested_set, account_client_test, ac_nested.test, scoped_db_only_key
```

Expected `tests:databuckets` leftover scoped rows after a successful run are top-level keys for
`character_id = 1`: `json_key`, `simple_key`, `exp_test`, `full_json`, `complex`, `plain_string`,
`json_array`, `nested_partial`, `nested_override`, `empty_json`, `json_string`, `deep_nested`,
`nested_expire`, `cache_miss_overwrite`, `missed_nested_set`, and `scoped_db_only_key`.

Runtime observation: on 2026-05-04, after the `tests:*` CLI exit path was changed to avoid process-wide OpenSSL
atexit cleanup, all four targeted `zone tests:*` commands had successful runtime checks. `tests:npc-handins` and
`tests:npc-handins-multiquest` exited cleanly with read-mostly side effects. With the backup gate in place,
`tests:databuckets` exited cleanly and left the 16 expected `character_id = 1` rows listed above; the dev database
was restored afterward. With a refreshed backup gate in place, `tests:zone-state` exited cleanly and left 115
`zone_state_spawns` rows for `zone_id = 32`, plus one `zone_state_test` loottable, lootdrop, loottable entry, and
lootdrop entry; the dev database was restored afterward.

Reserved-owner actor setup can be exercised explicitly with:

```sh
cd ../bump-akk-stack-validation
docker-compose -f docker-compose.yml -f docker-compose.dev.yml up -d --no-recreate mariadb
docker-compose -f docker-compose.yml -f docker-compose.dev.yml run --rm --no-deps --entrypoint bash eqemu-server -lc 'cd ~/server && ~/code/build/bin/zone tests:reserved-actor-owner'
```

World CLI checks can also be targeted:

```sh
cd ../bump-akk-stack-validation
docker-compose -f docker-compose.yml -f docker-compose.dev.yml run --rm --no-deps --entrypoint bash eqemu-server -lc 'cd ~/code/build && ./bin/world database:version'
docker-compose -f docker-compose.yml -f docker-compose.dev.yml run --rm --no-deps --entrypoint bash eqemu-server -lc 'cd ~/code/build && ./bin/world database:schema'
```

## Backup Gate

Before any validation that may mutate schema or run migrations, back up the persistent dev database:

```sh
cd ../bump-akk-stack-validation
make mysql-backup
```

The backup artifact is written under the AkkStack checkout at:

```text
../bump-akk-stack-validation/backup/database/<database-name>-MM-DD-YYYY.tar.gz
```

For the default local database name, an observed artifact was:

```text
../bump-akk-stack-validation/backup/database/peq-05-03-2026.tar.gz
```

The archive contains the SQL dump named with the same base name, such as `peq-05-03-2026.sql`.

This gate is mandatory before:

- `bin/world database:updates`
- bot or merc enable-disable flows
- schema migrations or changes under database update manifests
- repository-generation/schema work, including generated base repositories or repository generation code
- any future test command classified as schema-mutating

Do not require a database backup for ordinary compile checks or read-mostly targeted CLI tests.
Do not print, paste, or store database passwords while proving this gate. If the command is run from an
automation transcript or investigation log, suppress or redact command output and record only the exit status,
artifact path, file size, and archive member names.

## Local-Backup Database Prepopulation

Use local-backup prepopulation only when a DB-backed validation task needs real PEQ content and the persistent
dev database is empty or known to be unsuitable. This is a manual recovery/setup flow, not part of the default
validation path.

Check AkkStack restore support first. As of the last local check, top-level `make help` exposed `mysql-backup`
but no local restore target, and `assets/scripts/Makefile` exposed `init-peq-database`, which downloads a fresh
PEQ dump instead of restoring `backup/database/*.tar.gz`. Prefer a future AkkStack restore target if one is
added; use the direct import below only while no restore command exists.

Select the newest local backup deterministically from the old AkkStack backup directory, then preserve that
selected source before taking the required safety backup in the active dev stack. The preservation step matters
because `make mysql-backup` names artifacts only by date and can overwrite a same-day source archive if the
source and destination backup directories are the same.

```sh
old_backup_dir="../../eqemu/akk-stack/backup/database"
selected_backup="$(find "$old_backup_dir" -maxdepth 1 -type f -name '*.tar.gz' -printf '%T@ %p\n' | sort -k1,1n -k2,2 | tail -n 1 | cut -d ' ' -f2-)"
test -n "$selected_backup"
restore_source="/tmp/eqemu-restore-$(basename "$selected_backup")"
cp -p "$selected_backup" "$restore_source"
sql_member="$(tar -tzf "$restore_source" | sed -n '1p')"
cd ../bump-akk-stack-validation
make -s mysql-backup
tar -xOzf "$restore_source" "$sql_member" \
  | docker-compose -f docker-compose.yml -f docker-compose.dev.yml exec -T mariadb bash -lc 'mysql -uroot -p"$MYSQL_ROOT_PASSWORD" -h localhost "$MYSQL_DATABASE"'
```

After import, verify that the database actually has content before relying on it for Tier 2 checks:

```sh
cd ../bump-akk-stack-validation
docker-compose -f docker-compose.yml -f docker-compose.dev.yml exec -T mariadb bash -lc 'mysql -uroot -p"$MYSQL_ROOT_PASSWORD" -h localhost "$MYSQL_DATABASE" -Nse "select count(*) from information_schema.tables where table_schema = database()"'
```

Observed local retry on 2026-05-03 selected
`../../eqemu/akk-stack/backup/database/peq-05-02-2026.tar.gz`, preserved it under `/tmp`, ran the active stack's
`make -s mysql-backup` gate first, and imported `peq-05-02-2026.sql` successfully. After import, the local `peq`
database had 267 tables, including 67,530 `npc_types` rows, 165,711 `spawn2` rows, 618 `zone` rows, and existing
`db_version` and `variables` tables. A one-off `world database:version` run from the repo build binary still
failed because that container path did not have an initialized server config/database connection; that is separate
from the SQL import result.

## Tier 3: Zone Harness Validation

Use this when the change affects runtime gameplay behavior that can be observed inside one booted zone without a
real connected client. The Zone Harness is the right tier for server-observable actor behavior, combat state,
spell cast-start events, bounded zone processing, entity snapshots, and scenario fixtures that can run in a
one-off zone process.

Do not move broad decision matrices into harness scenarios. Unit and helper tests remain the right place for
large combinations of target selection, eligibility, ordering, and edge-case rules. The harness should prove that
the runtime wiring, ordinary processing loop, and event observations work for a small number of representative
scenarios.

Do not use the harness as a replacement for persistent runtime or manual client validation when the behavior
depends on UI, login, zoning between zones, client protocol packets, final visible gameplay, or real client
timing. Those still belong in the live server and manual tiers below.

The one-off harness process command is:

```sh
zone tests:serve-http --zone qrg --port 9099 --max-runtime-seconds 30
```

In normal repo validation, prefer the wrapper instead of hand-assembling the AkkStack container, temporary runtime
directory, server asset links, and `./bin/zone` invocation:

```sh
./scripts/smoke-zone-harness.sh --stack validation
./scripts/smoke-zone-harness.sh --stack validation --dry-run
```

The top-level opt-in Tier 3 validation command delegates to the same canonical smoke implementation:

```sh
./scripts/validate.sh --stack validation tier3-harness
./scripts/validate.sh --stack validation --dry-run tier3-harness
```

The wrapper first starts or verifies validation `mariadb` with canonical Compose and `--no-recreate`:

```sh
cd ../bump-akk-stack-validation
docker-compose -f docker-compose.yml -f docker-compose.dev.yml up -d --no-recreate mariadb
```

It then applies a temporary Compose override only to the one-off validation `eqemu-server` service so host
gameplay ports are disabled for that short-lived server container. Do not apply that temporary port override to
`mariadb`; the database should stay on the canonical validation stack Compose definition so Compose does not
recreate the long-running database container.

Inside the one-off `eqemu-server` container, the wrapper waits for the validation MariaDB service through Docker
service DNS, builds a temporary runtime directory that links the repo build binaries and initialized server
assets, runs `zone tests:serve-http --zone qrg --port 9099 --max-runtime-seconds 30`, curls localhost from inside
the same container, and checks:

- `GET /api/v1/harness/health` returns healthy JSON.
- `GET /api/v1/harness/zone` reports `short_name` `qrg`.
- `GET /api/v1/harness/entities` returns entity counts.
- `POST /api/v1/harness/process` with `{"ticks":2}` reports two processed ticks.
- `GET /api/v1/harness/events` initially returns an empty event list.
- `POST /api/v1/harness/scenarios/spell-cast-start` can produce a drained `spell_cast_started` Actor Event.
- `POST /api/v1/harness/scenarios/headless-client/target` proves a synthetic headless `Client()` can set and
  clear one NPC target without an `EQStream`, reports both `target_set` and `target_cleared` actor events in the
  same scenario payload, and leaves no additional actor events after its reported final cursor.
- `POST /api/v1/harness/scenarios/bot-slow-maintenance/current-target` proves the current target case.
- `POST /api/v1/harness/scenarios/bot-slow-maintenance/fallback` proves fallback to another unslowed
  **Engaged Hostile** after the current target is already slowed.
- `POST /api/v1/harness/scenarios/bot-slow-maintenance/mezzed` proves the bot skips a mezzed hostile and slows
  another eligible hostile.
- `POST /api/v1/harness/scenarios/autonomous-actor-loop` proves one owned spawned bot can act as a bounded
  **Autonomous Actor** harness primitive by enqueueing target and say actions, processing a small tick budget,
  observing actor-scoped perception, and verifying cursor-based `target_changed` and `speech_emitted`
  **Actor Events** without default persistent DB mutation.
- `POST /api/v1/harness/scenarios/actor-led-bot-party` proves one owned bot can act as an actor-leader
  candidate with 1-4 follower bots while the current owner-client target and leash defaults remain intact and
  follower bots can instead source target, assist, and leash intent from a narrow `ActorCommandSource` seam.
- `POST /api/v1/harness/shutdown` requests clean shutdown.

Expected validation result: the wrapper exits `0` with no scenario payload printed. On failure it prints either
the failed HTTP response, a compact scenario error payload, or `logs/zone_harness.out` from the temporary runtime.

Fixture and database expectations:

- Default harness scenarios should use in-memory fixtures and read-only database/content access.
- Any scenario that mutates persistent database rows is not a default harness check. Classify it as read-mostly,
  persistent-data-mutating, or schema-mutating in this document before using it in routine validation.
- Persistent-data-mutating scenarios must either clean up exactly after themselves or require a documented backup
  gate. Schema-mutating scenarios always require the backup gate.
- The default bot slow maintenance scenarios report `database_mutation` beginning with `none:` and should not
  leave persistent database changes.
- The default headless-client target scenario reports `database_mutation` beginning with `none:` and should leave
  no unreported `target_changed` cleanup events after its returned `event_cursor_end`.

Processing expectations:

- Harness scenarios may request bounded normal processing, such as a fixed number of process ticks or a bounded
  wait for an event.
- Do not force AI timers, directly complete spell outcomes, or mark a spell as landed to make a scenario pass.
  The primary pass condition should be observable behavior produced by ordinary zone processing.
- Setup shortcuts are acceptable only as fixtures. They must stay separate from the behavior under test and from
  actor actions that should flow through normal server intent paths.

The bot slow maintenance harness scenarios prove the first runtime slice for single-target `Slow` maintenance.
Their primary pass condition is that a normal `spell_cast_started` Actor Event is observed for a single-target
`Slow` spell at the expected **Engaged Hostile**. The current-target scenario expects the bot to begin slowing its
current target. The fallback scenario expects another unslowed engaged hostile after the current target is already
slowed. The mezzed scenario expects the mezzed hostile to remain untargeted while a different eligible hostile is
slowed.

The autonomous actor loop harness scenario is intentionally narrower. It uses the same non-persistent owned-bot
fixture to prove a first perception-action-event loop: enqueue bounded target and say actions for one owned bot,
process a small tick budget through normal zone processing, read cursor-bounded `Actor Events`, and observe the
actor's current target and nearby entities through actor-scoped perception. The failure payload should identify
the actor, owner, pending action, tick budget, observed event count, and persistence/database-mutation
classification.

The actor-led bot party harness scenario is bounded by a normal six-member EQ group: one synthetic owner client,
one actor leader bot, and 1-4 follower bots. Tier 3 smoke should prove the minimum (`1`) and maximum (`4`)
follower counts, and the leash phase should include an owner-nearby control so the target clear is tied to the
owner moving outside leash range instead of any unrelated combat-target loss.

For AFK agents, use `./scripts/smoke-zone-harness.sh` after Tier 1 when a task touches harness-covered runtime
gameplay. If a validation wrapper is requested, wire it to this smoke script rather than duplicating the long
Docker command.

Observed runtime validation on the bot slow maintenance slices used:

```sh
./scripts/validate.sh --stack validation tier1
./scripts/validate.sh --stack validation tier3-harness
git diff --check
```

## Tier 4: Optional Live Server Smoke

Use this when the change affects runtime wiring, startup behavior, config loading, database connectivity, process orchestration, or server command surfaces.

Start the gameplay stack and run a lightweight server command:

```sh
cd ../bump-akk-stack
make up
docker-compose -f docker-compose.yml -f docker-compose.dev.yml exec -T eqemu-server bash -lc 'cd ~/server && ~/code/build/bin/world database:version'
docker-compose -f docker-compose.yml -f docker-compose.dev.yml logs --tail=120 eqemu-server
```

The goal is to confirm the container is alive, the configured server binaries can run, and the server can talk to the database. This is not a full manual gameplay pass.

Observed live-smoke blockers on 2026-05-04:

- `make up` can fail before `eqemu-server` starts if host port `8080` is already bound.
- Local remediation for a host `8080` conflict is to use an out-of-git compose overlay in the AkkStack checkout
  and invoke `make` with an explicit local compose chain. The observed working overlay used Compose `!override`
  for `eqemu-server.ports` and remapped container `8080` to host `18080`, then started the stack with:

```sh
cd ../bump-akk-stack
make DOCKER='docker-compose -f docker-compose.yml -f docker-compose.dev.yml -f docker-compose.local.yml' up
```

- The current local AkkStack `server/bin` directory was empty, so `cd ~/server && ./bin/world database:version`
  failed with `./bin/world: No such file or directory`. Running the repo build binary from the server working
  directory, `cd ~/server && ~/code/build/bin/world database:version`, loaded `~/server/eqemu_config.json` and
  returned the database version successfully in a one-off diagnostic container.
- A bounded world startup after copying the reference worldserver loginserver account/password fields into the
  local out-of-git `../bump-akk-stack/server/eqemu_config.json` connected to MariaDB, connected to the remote
  legacy login server, and sent server info without the previous invalid account/password fatal appearing before
  the timeout. No explicit registered/accepted line was observed in the bounded run. It still warned that Docker
  `server.world.localaddress`/`server.world.address` values did not match the detected container/public addresses
  and reported missing patch opcode files under `~/server/assets/patches`.

If world telnet is enabled in `eqemu_config.json`, localhost console commands provide another smoke surface, including `ping`, `version`, `who`, `zonestatus`, `zonebootup`, and `zoneshutdown`.

## Tier 5: Manual Client Test

Use manual client testing only when behavior cannot be observed through build output, unit tests, CLI hooks, database checks, sidecar HTTP, or logs.

Manual testing is appropriate for:

- login flow
- character select and zoning
- client protocol packet behavior
- visible gameplay, combat, UI, movement, and interaction behavior
- GM command behavior that depends on an actual connected client

When manual testing is required, write down the exact client, character, zone, command sequence, observed result, and logs checked.

## Sidecar HTTP Hooks

Use sidecar HTTP checks when the touched code is reachable through the zone
sidecar and the behavior can be observed without an EQ client. Prefer extending
sidecar hooks in this repo over adding AkkStack-specific test behavior.

Run the sidecar from a one-off validation `eqemu-server` container so Docker service DNS
for validation `mariadb` is available, but execute the built checkout binaries through a
temporary runtime directory that points at the initialized server config,
plugins, Lua modules, and shared-memory files:

```sh
cd ../bump-akk-stack-validation
docker-compose -f docker-compose.yml -f docker-compose.dev.yml run --rm --no-deps --entrypoint bash eqemu-server -lc '
set -eu
runtime=/tmp/sidecar-validation-runtime
rm -rf "$runtime"
mkdir -p "$runtime/bin" "$runtime/logs" "$runtime/maps" "$runtime/quests"
ln -s ~/code/build/bin/zone "$runtime/bin/zone"
ln -s ~/code/build/bin/shared_memory "$runtime/bin/shared_memory"
ln -s ~/server/eqemu_config.json "$runtime/eqemu_config.json"
ln -s ~/server/plugins "$runtime/plugins"
ln -s ~/server/lua_modules "$runtime/lua_modules"
ln -s ~/server/shared "$runtime/shared"
cd "$runtime"
./bin/zone sidecar:serve-http --port 9099
'
```

Existing endpoints include:

- `GET /api/v1/test-controller` returns HTTP 200 with `{"data":{"test":"test"}}`.
- `GET /api/v1/loot-simulate` returns HTTP 200 with loot simulation data for
  `loottable_id=4027` and `npc_id=32040` by default.

Because the sidecar binds to `localhost` inside the container, run `curl` from
the same container or add explicit port publishing for an intentional host-side
check. The current sidecar process handles `SIGTERM` by logging the signal but
may need the one-off container to be stopped after validation.

## Choosing A Tier

- Common utility or isolated logic: Tier 1.
- Database-backed game logic with an existing CLI hook: Tier 1 plus targeted Tier 2.
- Schema or migration work: backup gate, then Tier 1 plus targeted Tier 2.
- Runtime gameplay behavior covered by a harness scenario: Tier 1 plus Tier 3.
- Runtime process, config, startup, or integration behavior: Tier 1 plus Tier 4.
- Client-visible behavior or packet flow: Tier 1 plus Tier 4 and Tier 5.

Escalate only as far as the touched area requires.

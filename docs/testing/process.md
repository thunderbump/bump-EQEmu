# Testing Process

This repo is validated through a persistent local AkkStack development environment. The normal path assumes `../bump-akk-stack` is already initialized and can run the EQEmu server containers locally.

Bootstrap from zero is a separate setup task. Do not fold `make install`, environment generation, data downloads, or first-time database setup into every validation pass.

## Environment Contract

- Use `../bump-akk-stack` for local containerized validation.
- `../bump-akk-stack/code` should point at this checkout, preferably as a symlink to `/home/bump/Projects/bump-eqemu/bump-EQEmu`.
- Use local AkkStack `make` commands where possible.
- Do not change `../bump-akk-stack` unless existing hooks are not enough.
- Treat the AkkStack database as a persistent developer database, not a disposable test schema.

Before running container validation, confirm the stack is initialized:

```sh
./scripts/check-akkstack-contract.sh
```

Then start the stack from AkkStack:

```sh
cd ../bump-akk-stack
make up
```

The preflight intentionally checks for `../bump-akk-stack/.env` without printing it. If `.env` is missing,
initialize the persistent AkkStack environment before running validation; do not generate or paste secrets as
part of an ordinary test pass.

The preflight also identifies whether `../bump-akk-stack/code` is a symlink, a directory checkout, or an
invalid path. The default contract is that it resolves to `/home/bump/Projects/bump-eqemu/bump-EQEmu`. If it
points somewhere else, fix the AkkStack `code` mount to point at this checkout before validating changes from
this repo. A symlink is preferred when `code` is missing:

```sh
cd ../bump-akk-stack
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

## Tier 1: Container Build And Unit Tests

Use this as the default automated validation tier for code changes.

Run the full configured build inside the AkkStack dev container, with tests enabled, then run the CMake test binary:

```sh
git submodule update --init --recursive
cd ../bump-akk-stack
docker-compose -f docker-compose.yml -f docker-compose.dev.yml run --rm --no-deps --entrypoint bash eqemu-server -lc 'cd ~/code && cmake --preset linux-debug && cmake --build build --parallel && ./build/bin/tests'
```

Use `docker-compose run` for Tier 1 instead of `make up` plus `exec`. The build and unit test pass does not need
server ports or a running database, and avoiding published service ports keeps this tier usable when local web or
game ports are already occupied. The `--entrypoint bash` override bypasses the image's normal server startup loop
so the validation command runs directly.

Host-native CMake is not the primary path because this codebase has specific runtime and dependency version expectations. The container is the closer match for local server development.

The repo-local wrapper for this tier is:

```sh
./scripts/validate.sh tier1
```

The wrapper runs the AkkStack contract preflight first, then runs the raw Tier 1 command shown above from
`../bump-akk-stack`. Keep the raw command visible here so failures can still be reproduced or narrowed manually.

## Tier 2: DB-Backed CLI Tests

Use this when the change touches code that is exercised by existing `zone` or `world` command hooks.

Run targeted tests rather than the whole live server whenever possible:

```sh
cd ../bump-akk-stack
docker-compose -f docker-compose.yml -f docker-compose.dev.yml exec -T eqemu-server bash -lc 'cd ~/server && ~/code/build/bin/zone tests:npc-handins'
docker-compose -f docker-compose.yml -f docker-compose.dev.yml exec -T eqemu-server bash -lc 'cd ~/server && ~/code/build/bin/zone tests:npc-handins-multiquest'
docker-compose -f docker-compose.yml -f docker-compose.dev.yml exec -T eqemu-server bash -lc 'cd ~/server && ~/code/build/bin/zone tests:databuckets'
docker-compose -f docker-compose.yml -f docker-compose.dev.yml exec -T eqemu-server bash -lc 'cd ~/server && ~/code/build/bin/zone tests:zone-state'
```

The repo-local wrapper covers only the read-mostly targeted zone checks:

```sh
./scripts/validate.sh tier2-readonly
```

That command runs the preflight, then `tests:npc-handins` and `tests:npc-handins-multiquest`. It intentionally
does not run `tests:databuckets` or `tests:zone-state`; use the raw commands above after applying the backup gate
when a change specifically needs those caution-tier checks. The read-mostly Tier 2 wrapper assumes the AkkStack
`eqemu-server` container is already running.

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

World CLI checks can also be targeted:

```sh
cd ../bump-akk-stack
docker-compose -f docker-compose.yml -f docker-compose.dev.yml exec -T eqemu-server bash -lc 'cd ~/code/build && ./bin/world database:version'
docker-compose -f docker-compose.yml -f docker-compose.dev.yml exec -T eqemu-server bash -lc 'cd ~/code/build && ./bin/world database:schema'
```

## Backup Gate

Before any validation that may mutate schema or run migrations, back up the persistent dev database:

```sh
cd ../bump-akk-stack
make mysql-backup
```

The backup artifact is written under the AkkStack checkout at:

```text
../bump-akk-stack/backup/database/<database-name>-MM-DD-YYYY.tar.gz
```

For the default local database name, an observed artifact was:

```text
../bump-akk-stack/backup/database/peq-05-03-2026.tar.gz
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
cd ../bump-akk-stack
make -s mysql-backup
tar -xOzf "$restore_source" "$sql_member" \
  | docker-compose -f docker-compose.yml -f docker-compose.dev.yml exec -T mariadb bash -lc 'mysql -uroot -p"$MYSQL_ROOT_PASSWORD" -h localhost "$MYSQL_DATABASE"'
```

After import, verify that the database actually has content before relying on it for Tier 2 checks:

```sh
cd ../bump-akk-stack
docker-compose -f docker-compose.yml -f docker-compose.dev.yml exec -T mariadb bash -lc 'mysql -uroot -p"$MYSQL_ROOT_PASSWORD" -h localhost "$MYSQL_DATABASE" -Nse "select count(*) from information_schema.tables where table_schema = database()"'
```

Observed local retry on 2026-05-03 selected
`../../eqemu/akk-stack/backup/database/peq-05-02-2026.tar.gz`, preserved it under `/tmp`, ran the active stack's
`make -s mysql-backup` gate first, and imported `peq-05-02-2026.sql` successfully. After import, the local `peq`
database had 267 tables, including 67,530 `npc_types` rows, 165,711 `spawn2` rows, 618 `zone` rows, and existing
`db_version` and `variables` tables. A one-off `world database:version` run from the repo build binary still
failed because that container path did not have an initialized server config/database connection; that is separate
from the SQL import result.

## Tier 3: Optional Live Server Smoke

Use this when the change affects runtime wiring, startup behavior, config loading, database connectivity, process orchestration, or server command surfaces.

Start the stack and run a lightweight server command:

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

## Tier 4: Manual Client Test

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

Run the sidecar from a one-off `eqemu-server` container so Docker service DNS
for `mariadb` is available, but execute the built checkout binaries through a
temporary runtime directory that points at the initialized server config,
plugins, Lua modules, and shared-memory files:

```sh
cd ../bump-akk-stack
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
- Runtime process, config, startup, or integration behavior: Tier 1 plus Tier 3.
- Client-visible behavior or packet flow: Tier 1 plus Tier 3 and Tier 4.

Escalate only as far as the touched area requires.

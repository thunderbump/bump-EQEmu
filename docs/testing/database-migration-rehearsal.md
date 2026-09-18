# Database migration rehearsal and maintenance rollback

`./scripts/validate.sh --stack validation migration-rehearsal` creates a fresh MariaDB container, volume and internal Docker network for each run. Candidate and archived-world processes join only that network, have no published ports, and use generated configuration with no login-server entries or automatic startup updates. No existing database credentials, database volumes or server configuration are loaded. Following the repository's zone-CLI routing convention, the validation stack supplies only its read-only generated shared-memory directory plus the selected `plugins` and `lua_modules` directories. This database-focused scenario does not boot a zone, so it uses empty temporary maps and quests directories rather than requiring or mounting the stack's full asset trees. The Candidate build and archived old build are read-only inputs. Runtime files live in container tmpfs. The run owns and cleans up its database container, runtime container, volume and network, retaining private logs and result evidence outside them. There is no shared-database compatibility mode.

The dump and archived old binaries are operational artifacts and must remain outside Git. Prepare a captured baseline in place, inspect the generated SQL, and then explicitly enable it:

```sh
./scripts/prepare-migration-rehearsal-fixture.sh --baseline-dir /path/to/captured-baseline
# If capture metadata omitted MariaDB's version, add the version verified on the
# source and validation servers: --mariadb-version X.Y.Z
# Review the generated manifest, SQL and extracted binary hash.
# For a worker-managed/relocated validation stack, mark that inspected stack once:
mkdir -p "${VALIDATION_WORKER_HOME:-.validation-worker}"
install -m 600 /path/to/captured-baseline/migration-rehearsal.manifest-path \
  "${VALIDATION_WORKER_HOME:-.validation-worker}/migration-rehearsal-manifest"
./scripts/validate-afk
```

Preparation requires `manifest.json`, one `database.sql` snapshot (optionally `.gz` or `.zst`), and `installed-binaries.tar.gz`. It compares each artifact with its exact declared checksum field, safely extracts exactly one executable named `world`, and writes the rehearsal manifest, fixture SQL and environment selection beneath the baseline. It neither starts Docker nor configures the gate. After review, installing the generated one-line selection file makes the ordinary no-argument AFK command load that absolute manifest path; an explicit `MIGRATION_REHEARSAL_MANIFEST` environment value takes precedence. If capture metadata uses an unrecognized shape, `--help` lists explicit metadata overrides; if the archive contains multiple `world` binaries, select one with `--world-member`. Keep all generated files with the access-controlled baseline and out of Git.

For a focused run after inspection without installing the persistent worker selection, source the generated `migration-rehearsal.env` file and run `./scripts/validate.sh --stack validation migration-rehearsal`. `MIGRATION_REHEARSAL_TIMEOUT_SECONDS` sets the positive-integer whole-run deadline (5400 seconds by default), including image setup, snapshot import, updates, scenarios, and recovery. The portable worker inherits `MIGRATION_REHEARSAL_MANIFEST` and writes `migration-rehearsal/result.json` under exact-Candidate evidence. Snapshot identity and checksum, source build, source server/bot/custom database versions, MariaDB version, Candidate source commit, outcome, failure phase, isolated database name, and measured restore milliseconds are recorded. Candidate `world`/`zone` hashes and worktree cleanliness are recorded, while source-to-artifact identity remains conservatively unattested; Tier 1's immediately preceding exact-checkout build and the exercised runtime provide operational evidence without inventing embedded build provenance. Logs must not contain credentials. Missing fixture infrastructure exits nonzero; it is not a skipped pass.

## Fixture contract

The manifest has this shape:

```json
{
  "snapshot": {
    "id": "validation-deployed-2026-09-01",
    "file": "deployed.sql.zst",
    "sha256": "<64 lowercase hex characters>"
  },
  "source": {
    "build": "<captured source checkout or other metadata>",
    "build_identity_attested": false,
    "mariadb_version": "10.11.8",
    "database_versions": {"server": 9334, "bots": 9055, "custom": 0}
  },
  "old_build": {
    "world_binary_container_path": "/opt/eqemu-old/9334/world",
    "world_binary_sha256": "<64 lowercase hex characters>",
    "host_directory": "migration-rehearsal-fixture/old-build"
  },
  "fixtures": {
    "seed_sql": "seed-old-format.sql",
    "upgraded_assert_sql": "assert-upgraded.sql",
    "restored_assert_sql": "assert-restored.sql"
  },
  "candidate_scenarios": [
    "/home/eqemu/code/build/bin/zone tests:npc-handins",
    "/home/eqemu/code/build/bin/zone tests:npc-handins-multiquest"
  ]
}
```

Snapshot, SQL fixture and old-build host directory paths are relative to and confined beneath the manifest directory. `old_build.world_binary_container_path` must be beneath `/opt/eqemu-old/`, the fixed read-only archive mount. Fixture-provided Compose overrides are no longer used. The database image is `mariadb:<captured-version>` and the runtime image is `eqemulator/eqemu-server:v16-dev`; images are cached/pulled on the host before isolated execution. Database readiness is bounded at 90 seconds, candidate scenarios at 300 seconds by default, and old-world startup at 30 seconds. `MIGRATION_REHEARSAL_SCENARIO_TIMEOUT_SECONDS` may set another positive-integer scenario deadline. Timeout uses TERM followed by KILL and labelled-container cleanup, while `MIGRATION_REHEARSAL_TIMEOUT_SECONDS` bounds the entire focused run (and the worker has its own outer deadline). TERM/INT triggers cleanup; an uncatchable host/process failure can leave UUID-named, labelled resources for operator cleanup.

`source.build` is capture metadata, not an attestation that the installed binary was produced from that checkout. `source.build_identity_attested` remains false for this capture. The separately checked world-binary hash and successful database-backed startup establish exactly which archived binary recovered, without fabricating source-to-binary provenance.

`seed-old-format.sql` must add or record representative rows for every changed saved-data shape. Use reserved fixture identities and include owners and balances on both sides of any transfer. The repository preparer records aggregate ownership/currency state from real snapshot rows, collision-checks reserved names, and inserts two named owners, balances, and owned bots into the production `character_data`, `character_currency`, and `bot_data` tables. Production auto-increment allocates their IDs, which the fixture records for its assertions; the fixture does not advance production sequences to artificial near-maximum IDs. The actor migration is additive and has no predecessor actor-row format to transform. `assert-upgraded.sql` verifies the actor tables' critical columns, indexes, and all five JSON constraint semantics (independent of MariaDB-generated names and tolerant of equivalent duplicate constraints), preserves non-fixture snapshot aggregates, and checks the real fixture ownership and currency total. The `tests:actor-events` scenario then creates and cleans production actor records against that schema. `assert-restored.sql` validates the original snapshot after rollback and proves the reserved production-table records are absent. Each assertion file must return exactly one scalar row containing `ok`; any other output fails the profile. Candidate scenarios must exercise relevant production command/gameplay paths against the upgraded isolated database and must be bounded and self-cleaning.

The profile verifies the dump checksum and all source versions, resolves required read-only runtime inputs before creating Docker resources or restoring data, imports and seeds the snapshot, runs the Candidate updater, checks upgraded meaning, runs the listed scenarios, and records the database-version/schema fingerprint. It then runs the updater again, requires that fingerprint to remain unchanged, and reruns the upgraded-data assertions so row-level non-idempotence cannot pass on stable schema metadata alone. Finally it drops and recreates the isolated database, reimports the pristine snapshot, checks restored data, verifies the old binary checksum, and starts that binary for a bounded 30-second window. Before old-world startup, the generated runtime config contains no `loginserver`/`loginserverN` entries and sets `server.auto_database_updates` to false. The recovery check requires the old world to remain alive until termination and to reach its TCP listener after database-backed loading, then reruns both the restored assertion and the captured ownership/currency conservation fingerprint. Cleanup removes only the UUID-named, ownership-labelled runtime/database containers, volume, and internal network; cleanup failure forces a nonzero result.

`./scripts/validate-afk` currently has no trusted base commit or schema-change classifier. It therefore conservatively selects `tier1-migration-tier3` for every Candidate. Operators can explicitly request `migration-rehearsal` or `tier1-migration-tier3` through the worker profile contract. A future trusted selector may use the lighter `tier1-tier3-harness` only after it can reliably prove that schema and saved data are unaffected.

## Maintenance-window release

Only perform this procedure after deployment is separately authorized:

1. Announce maintenance, close access, stop world/zone/login writers, and verify no other database writers remain.
2. Record the new and previous build/config identities. Create a backup name containing UTC date, time, release, and a unique token; do not use AkkStack's date-only name without copying it to a unique name. Record its SHA-256, DB versions, MariaDB version, size, and member names without recording secrets.
3. Rehearse that exact backup with this profile and the matching previous binary. Do not proceed unless upgrade, assertions, second updater run, scenarios, rollback import, and old-build recovery all pass.
4. With writers still stopped, take the final uniquely named backup. Run the tested Candidate `world database:updates` explicitly. Verify resulting versions and the same data assertions, then start the matching new build for a bounded smoke check before reopening access. Account for automatic startup updates.
5. On failure, keep writers stopped. Drop/recreate the affected database, restore the matching final backup, start the recorded previous build/config, and rerun the restored-data/startup checks. Reopen only after recovery succeeds. If access had already reopened, record the exact progress-loss window.

## Current evidence and limitations

A host-local baseline has been captured and private inputs have been prepared. The latest refreshed-input run restored and seeded the snapshot, upgraded 9328 to 9335, and passed `tests:actor-events`, but failed the upgraded assertion; it did not reach idempotence or recovery and is not acceptance evidence. Generated SQL is not refreshed automatically when the repository preparer changes: the operator must rerun preparation for the exact Candidate, review the regenerated inputs, and install its selection file. Result evidence records the fixture-preparer commit plus exact snapshot and generated-SQL hashes so stale inputs remain distinguishable. The capture's source checkout is metadata, not proof that the archived installed binaries match. Compatibility, restore timing, updater idempotence, scenario behavior, and old-build recovery therefore remain unproven until fresh inputs are selected and the complete fixture passes.

The profile uses a fresh disposable MariaDB container and volume for every run; it never connects to or mounts a shared validation/gameplay database. It does not prove zero-downtime migration, universal reverse migrations, client rendering, cross-zone handoff, or production performance. Restore duration on local validation hardware is evidence for maintenance planning, not a production SLA. Snapshot acquisition and secret handling remain trusted fixture-setup responsibilities. The preparation command supplies the old-binary directory, and an operator must review the generated manifest and read-only inputs before enabling the fixture.

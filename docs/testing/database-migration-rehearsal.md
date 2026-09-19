# Database migration rehearsal and maintenance rollback

`./scripts/validate.sh --stack validation migration-rehearsal` creates a fresh MariaDB container, volume and internal Docker network for each run. Candidate and archived-world processes join only that network, have no published ports, and use generated configuration with no login-server entries or automatic startup updates. No existing database credentials, database volumes or server configuration are loaded. Following the repository's zone-CLI routing convention, the validation stack supplies only its read-only generated shared-memory directory plus the selected `plugins` and `lua_modules` directories. This database-focused scenario does not boot a zone, so it uses empty temporary maps and quests directories rather than requiring or mounting the stack's full asset trees. The Candidate build and archived old build are read-only inputs. Runtime files live in container tmpfs. The run owns and cleans up its database container, runtime container, volume and network, retaining private logs and result evidence outside them. There is no shared-database compatibility mode.

The dump and archived old binaries remain outside Git. Select the captured baseline once in host-local configuration:

```sh
mkdir -p "${VALIDATION_WORKER_HOME:-.validation-worker}"
# Save this JSON as $VALIDATION_WORKER_HOME/migration-rehearsal-baseline.json:
# {"directory":"/absolute/path/to/captured-baseline","mariadb_version":"10.5.4"}
./scripts/validate-afk
```

`directory` is required and absolute. Optional `mariadb_version` supplies a verified version when the capture omitted it; otherwise the capture supplies the version. No other keys are accepted. `MIGRATION_REHEARSAL_BASELINE_CONFIG` can select another config file. This replaces the old `migration-rehearsal-manifest` selection file for the no-argument AFK gate. That gate also ignores an ambient `MIGRATION_REHEARSAL_MANIFEST` and always prepares its own inputs.

After acquiring the exact committed checkout and worker lock, the worker runs that checkout's preparer before stack binding and Tier 1. Preparation consumes the same validation time budget. It writes under `<evidence_dir>/prepared-fixture/`, passes the generated manifest directly to the checks, and retains `logs/fixture-preparation.log`. Preparation failure writes a nonzero `fixture_preparation_failed` result and runs no validation checks. No model waits for preparation or tests.

Preparation requires `manifest.json`, one `database.sql` snapshot, optionally compressed as `.gz` or `.zst`, and `installed-binaries.tar.gz`. It verifies declared artifact checksums, copies the snapshot using copy-on-write where available, safely extracts the archived binaries, and generates SQL and a manifest. The baseline stays unchanged. Each output directory must be new and must not overlap the baseline. Outputs are private and stay with run evidence; no shared selection or environment files are written. The generated manifest records snapshot, capture-manifest, binary-archive and SQL hashes plus the preparer commit. Regeneration on every run deliberately avoids caching.

For focused manual diagnosis, prepare into a fresh private directory and pass the manifest explicitly:

```sh
./scripts/prepare-migration-rehearsal-fixture.sh \
  --baseline-dir /path/to/captured-baseline --output-dir /private/new-run/prepared-fixture
MIGRATION_REHEARSAL_MANIFEST=/private/new-run/prepared-fixture/migration-rehearsal-manifest.json \
  ./scripts/validate.sh --stack validation migration-rehearsal
```

`--help` lists metadata overrides for unusual capture formats, including `--world-member` for archives with multiple world binaries. Standalone worker profiles still accept an explicitly prepared manifest; automatic preparation belongs to the no-argument AFK gate.

`MIGRATION_REHEARSAL_TIMEOUT_SECONDS` sets the positive-integer whole-run deadline (5400 seconds by default), including image setup, snapshot import, updates, scenarios, and recovery. The portable worker inherits `MIGRATION_REHEARSAL_MANIFEST` and writes `migration-rehearsal/result.json` under exact-Candidate evidence. Snapshot identity and checksum, source build, source server/bot/custom database versions, MariaDB version, Candidate source commit, outcome, failure phase, isolated database name, and measured restore milliseconds are recorded. Candidate `world`/`zone` hashes and worktree cleanliness are recorded, while source-to-artifact identity remains conservatively unattested; Tier 1's immediately preceding exact-checkout build and the exercised runtime provide operational evidence without inventing embedded build provenance. Logs must not contain credentials. Missing fixture infrastructure exits nonzero; it is not a skipped pass.

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
    "fixture_preparer_commit": "<40-character Candidate commit>",
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

Snapshot, SQL fixture and old-build host directory paths are relative to and confined beneath the manifest directory. `old_build.world_binary_container_path` must be beneath `/opt/eqemu-old/`, the fixed read-only archive mount, and may not contain `.` or `..` path components. The fixture preparer commit must exactly equal the Candidate commit, so the AFK gate prepares from each committed checkout; manual focused runs must regenerate after repository changes. Fixture-provided Compose overrides are no longer used. The database image is `mariadb:<captured-version>` and the runtime image is `eqemulator/eqemu-server:v16-dev`; images are cached/pulled on the host before isolated execution. Database readiness is bounded at 90 seconds, candidate scenarios at 300 seconds by default, and old-world startup at 30 seconds. `MIGRATION_REHEARSAL_SCENARIO_TIMEOUT_SECONDS` may set another positive-integer scenario deadline. Timeout uses TERM followed by KILL and labelled-container cleanup, while `MIGRATION_REHEARSAL_TIMEOUT_SECONDS` bounds the entire focused run (and the worker has its own outer deadline). TERM/INT triggers cleanup; an uncatchable host/process failure can leave UUID-named, labelled resources for operator cleanup.

`source.build` is capture metadata, not an attestation that the installed binary was produced from that checkout. `source.build_identity_attested` remains false for this capture. The separately checked world-binary hash and successful database-backed startup establish exactly which archived binary recovered, without fabricating source-to-binary provenance.

`seed-old-format.sql` must add or record representative rows for every changed saved-data shape. Use reserved fixture identities and include owners and balances on both sides of any transfer. The repository preparer records aggregate ownership/currency state from real snapshot rows, collision-checks reserved names, and inserts two named owners, balances, and owned bots into the production `character_data`, `character_currency`, and `bot_data` tables. Production auto-increment allocates their IDs, which the fixture records for its assertions; the fixture does not advance production sequences to artificial near-maximum IDs. The actor migration is additive and has no predecessor actor-row format to transform. `assert-upgraded.sql` verifies the actor tables' critical columns and indexes, then behaviorally checks that invalid and oversized values are rejected by each of the five JSON constraints. This avoids MariaDB 10.5's truncated `information_schema.check_constraints.check_clause` while remaining independent of generated constraint names and equivalent duplicates. It also preserves non-fixture snapshot aggregates and checks the real fixture ownership and currency total. On failure it returns only bounded per-predicate labels (for example, `actor_events.event_json_constraint` or `fixture_currency`), never captured values. The `tests:actor-events` scenario then creates and cleans production actor records against that schema. `assert-restored.sql` validates the original snapshot after rollback and proves the reserved production-table records are absent. Each assertion file must return exactly one scalar row containing `ok`; any other output fails the profile. Candidate scenarios must exercise relevant production command/gameplay paths against the upgraded isolated database and must be bounded and self-cleaning.

The profile verifies the dump checksum and all source versions, resolves required read-only runtime inputs before creating Docker resources or restoring data, imports and seeds the snapshot, runs the Candidate updater, checks upgraded meaning, runs the listed scenarios, and records the database-version/schema fingerprint. It requires the resulting server, bot and custom versions to equal the numeric targets in the Candidate’s `common/version.h`; this rehearsal requires the full bot schema target even if runtime bot updates are disabled. JSON checks require valid values at each exact size limit and rejection of malformed JSON and values one character over the limit. It then runs the updater again, requires that fingerprint to remain unchanged, and reruns the upgraded-data assertions so row-level non-idempotence cannot pass on stable schema metadata alone. Finally it drops and recreates the isolated database, reimports the pristine snapshot, checks restored data, verifies the old binary checksum, and starts that binary for a bounded 30-second window. Before old-world startup, the generated runtime config contains no `loginserver`/`loginserverN` entries and sets `server.auto_database_updates` to false. The recovery check requires the old world to remain alive until termination and to reach its TCP listener after database-backed loading, then reruns both the restored assertion and the captured ownership/currency conservation fingerprint. Cleanup removes only the UUID-named, ownership-labelled runtime/database containers, volume, and internal network; cleanup failure forces a nonzero result.

`./scripts/validate-afk` currently has no trusted base commit or schema-change classifier. It therefore conservatively selects `tier1-migration-tier3` for every Candidate. Operators can explicitly request `migration-rehearsal` or `tier1-migration-tier3` through the worker profile contract. A future trusted selector may use the lighter `tier1-tier3-harness` only after it can reliably prove that schema and saved data are unaffected.

## Maintenance-window release

Only perform this procedure after deployment is separately authorized:

1. Announce maintenance, close access, stop world/zone/login writers, and verify no other database writers remain.
2. Record the new and previous build/config identities. Create a backup name containing UTC date, time, release, and a unique token; do not use AkkStack's date-only name without copying it to a unique name. Record its SHA-256, DB versions, MariaDB version, size, and member names without recording secrets.
3. Rehearse that exact backup with this profile and the matching previous binary. Do not proceed unless upgrade, assertions, second updater run, scenarios, rollback import, and old-build recovery all pass.
4. With writers still stopped, take the final uniquely named backup. Run the tested Candidate `world database:updates` explicitly. Verify resulting versions and the same data assertions, then start the matching new build for a bounded smoke check before reopening access. Account for automatic startup updates.
5. On failure, keep writers stopped. Drop/recreate the affected database, restore the matching final backup, start the recorded previous build/config, and rerun the restored-data/startup checks. Reopen only after recovery succeeds. If access had already reopened, record the exact progress-loss window.

## Current evidence and limitations

The refreshed-input run for `614ab776ea1895ea70e9c66aab369e157cf489e7` passed the complete rehearsal, including upgrade 9328 to 9335, actor scenarios, idempotence, snapshot restoration and archived-world recovery. Snapshot restoration took 21,127 ms. That evidence predates the explicit target-version and valid-boundary checks added here; these additions require a new exact-Candidate run. Generated SQL must be regenerated after each Candidate change. The capture's source checkout and Candidate binary hashes remain metadata, not source-to-binary attestations. No gameplay deployment is implied by this validation.

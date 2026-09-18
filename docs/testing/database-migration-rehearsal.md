# Database migration rehearsal and maintenance rollback

`./scripts/validate.sh --stack validation migration-rehearsal` is the repository-owned migration profile. It uses the existing EQEmu `world database:updates` updater and the default validation AkkStack MariaDB process, but creates a uniquely named `afk_migration_*` database and a unique MariaDB account whose grants are confined to that database. Snapshot import, fixtures, assertions, Candidate scenarios, and old-build recovery use that restricted account. Candidate and old-build runtime configuration routes `database`, `qsdatabase`, and `content_database` to the isolated database on the validation MariaDB service. The wrapper accepts the default `../bump-akk-stack-validation` path or a relocated stack carrying an operator-installed `.afk-validation-stack` marker; it does not trust a relocatable role label or `AKKSTACK_DIR` alone. It also rejects an isolated name that does not have the reserved prefix. SQL screening is defense in depth rather than the database security boundary.

The dump and archived old binaries are operational artifacts and must remain outside Git. Prepare a captured baseline in place, inspect the generated SQL and read-only Compose mount, and then explicitly enable it:

```sh
./scripts/prepare-migration-rehearsal-fixture.sh --baseline-dir /path/to/captured-baseline
# If capture metadata omitted MariaDB's version, add the version verified on the
# source and validation servers: --mariadb-version X.Y.Z
# Review the generated manifest, SQL, extracted binary hash, and Compose override.
# For a worker-managed/relocated validation stack, mark that inspected stack once:
printf '%s\n' afk-validation-stack-v1 > /path/to/bump-akk-stack-validation/.afk-validation-stack
mkdir -p "${VALIDATION_WORKER_HOME:-.validation-worker}"
install -m 600 /path/to/captured-baseline/migration-rehearsal.manifest-path \
  "${VALIDATION_WORKER_HOME:-.validation-worker}/migration-rehearsal-manifest"
./scripts/validate-afk
```

Preparation requires `manifest.json`, one `database.sql` snapshot (optionally `.gz` or `.zst`), and `installed-binaries.tar.gz`. It compares each artifact with its exact declared checksum field, safely extracts exactly one executable named `world`, and writes the rehearsal manifest, fixture SQL, Compose override, and environment selection beneath the baseline. It neither starts Docker nor configures the gate. After review, installing the generated one-line selection file makes the ordinary no-argument AFK command load that absolute manifest path; an explicit `MIGRATION_REHEARSAL_MANIFEST` environment value takes precedence. If capture metadata uses an unrecognized shape, `--help` lists explicit metadata overrides; if the archive contains multiple `world` binaries, select one with `--world-member`. Keep all generated files with the access-controlled baseline and out of Git.

For a focused run after inspection without installing the persistent worker selection, source the generated `migration-rehearsal.env` file and run `./scripts/validate.sh --stack validation migration-rehearsal`. The portable worker inherits `MIGRATION_REHEARSAL_MANIFEST` and writes `migration-rehearsal/result.json` under exact-Candidate evidence. Snapshot identity and checksum, source build, source server/bot/custom database versions, MariaDB version, Candidate commit, outcome, failure phase, isolated database name, and measured restore milliseconds are recorded. Logs must not contain credentials. Missing fixture infrastructure exits nonzero; it is not a skipped pass.

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
    "host_directory": "migration-rehearsal-fixture/old-build",
    "compose_file": "migration-rehearsal-fixture/docker-compose.migration-rehearsal.yml"
  },
  "fixtures": {
    "seed_sql": "seed-old-format.sql",
    "upgraded_assert_sql": "assert-upgraded.sql",
    "restored_assert_sql": "assert-restored.sql"
  },
  "candidate_scenarios": [
    "~/code/build/bin/zone tests:npc-handins",
    "~/code/build/bin/zone tests:npc-handins-multiquest"
  ]
}
```

Snapshot, SQL fixture, old-build host directory, and optional Compose override paths are relative to and confined beneath the manifest directory. The override is accepted only when it exactly defines the generated read-only old-build mount; arbitrary fixture-provided Compose settings are rejected. `old_build.world_binary_container_path` is different: it must be an absolute path inside the one-off `eqemu-server` container. The preparation command generates a read-only mount for that path.

`source.build` is capture metadata, not an attestation that the installed binary was produced from that checkout. `source.build_identity_attested` remains false for this capture. The separately checked world-binary hash and successful database-backed startup establish exactly which archived binary recovered, without fabricating source-to-binary provenance.

`seed-old-format.sql` must add or record representative rows for every changed saved-data shape. Use reserved fixture identities and include owners and balances on both sides of any transfer. The repository preparer records aggregate ownership/currency state from real snapshot rows, collision-checks reserved IDs and names, and inserts two deterministic owners, balances, and owned bots into the production `character_data`, `character_currency`, and `bot_data` tables. The actor migration is additive and has no predecessor actor-row format to transform. `assert-upgraded.sql` verifies the actor tables' critical columns, indexes, and named JSON constraints, preserves non-fixture snapshot aggregates, and checks the real fixture ownership and currency total. The `tests:actor-events` scenario then creates and cleans production actor records against that schema. `assert-restored.sql` validates the original snapshot after rollback and proves the reserved production-table records are absent. Each assertion file must return exactly one scalar row containing `ok`; any other output fails the profile. Candidate scenarios must exercise relevant production command/gameplay paths against the upgraded isolated database and must be bounded and self-cleaning.

The profile verifies the dump checksum and all source versions, imports and seeds it, runs the Candidate updater, checks upgraded meaning, runs the listed scenarios, and records the database-version/schema fingerprint. It then runs the updater again, requires that fingerprint to remain unchanged, and reruns the upgraded-data assertions so row-level non-idempotence cannot pass on stable schema metadata alone. Finally it drops and recreates the isolated database, reimports the pristine snapshot, checks restored data, verifies the old binary checksum, and starts that binary for a bounded 20-second window. Before old-world startup, the generated runtime config removes every `loginserver`/`loginserverN` entry and sets `server.auto_database_updates` to false; a local assertion rejects the startup unless both safeguards are present. The recovery check requires the old world to remain alive until termination and to reach its TCP listener after database-backed loading, then reruns the restored-data assertion. Cleanup drops both the temporary account and isolated database after success or failure; partial setup is tracked so later setup failures are also cleaned, and cleanup failure forces a nonzero result.

`./scripts/validate-afk` currently has no trusted base commit or schema-change classifier. It therefore conservatively selects `tier1-migration-tier3` for every Candidate. Operators can explicitly request `migration-rehearsal` or `tier1-migration-tier3` through the worker profile contract. A future trusted selector may use the lighter `tier1-tier3-harness` only after it can reliably prove that schema and saved data are unaffected.

## Maintenance-window release

Only perform this procedure after deployment is separately authorized:

1. Announce maintenance, close access, stop world/zone/login writers, and verify no other database writers remain.
2. Record the new and previous build/config identities. Create a backup name containing UTC date, time, release, and a unique token; do not use AkkStack's date-only name without copying it to a unique name. Record its SHA-256, DB versions, MariaDB version, size, and member names without recording secrets.
3. Rehearse that exact backup with this profile and the matching previous binary. Do not proceed unless upgrade, assertions, second updater run, scenarios, rollback import, and old-build recovery all pass.
4. With writers still stopped, take the final uniquely named backup. Run the tested Candidate `world database:updates` explicitly. Verify resulting versions and the same data assertions, then start the matching new build for a bounded smoke check before reopening access. Account for automatic startup updates.
5. On failure, keep writers stopped. Drop/recreate the affected database, restore the matching final backup, start the recorded previous build/config, and rerun the restored-data/startup checks. Reopen only after recovery succeeds. If access had already reopened, record the exact progress-loss window.

## Current evidence and limitations

A host-local baseline has been captured and an earlier preparation attempt generated private inputs, but selection remains deliberately disabled. The capture's `source` field is a container label while its checkout is in `source_checkout_sha`; the preparer now handles that shape. The capture omitted MariaDB version metadata, so the operator must rerun preparation with `--mariadb-version` set to the version independently checked on both database services, review the regenerated isolation inputs, and install the generated selection file. Its source checkout is not proof that the archived installed binaries match, and no isolated upgrade/rollback rehearsal has passed yet. Compatibility, restore timing, updater idempotence, scenario behavior, and old-build recovery therefore remain unproven until the fixture is reviewed, selected, and run.

The profile reuses one MariaDB process and isolates by a restricted temporary account and database name rather than by a disposable server volume. It does not prove zero-downtime migration, universal reverse migrations, client rendering, cross-zone handoff, or production performance. Restore duration on local validation hardware is evidence for maintenance planning, not a production SLA. Snapshot acquisition and secret handling remain trusted fixture-setup responsibilities. The preparation command supplies the old-binary mount, but an operator must review that generated read-only override before enabling the fixture.

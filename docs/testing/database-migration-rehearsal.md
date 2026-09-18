# Database migration rehearsal and maintenance rollback

`./scripts/validate.sh --stack validation migration-rehearsal` is the repository-owned migration profile. It uses the existing EQEmu `world database:updates` updater and the validation AkkStack MariaDB process, but creates a uniquely named `afk_migration_*` database and a unique MariaDB account whose grants are confined to that database. Snapshot import, fixtures, assertions, Candidate scenarios, and old-build recovery use that restricted account. Candidate and old-build runtime configuration routes `database`, `qsdatabase`, and `content_database` to the isolated database on the validation MariaDB service. The wrapper also rejects the gameplay role, the gameplay default path, and an isolated name that does not have the reserved prefix; its SQL screening is defense in depth rather than the database security boundary.

The dump and matching old binary are operational artifacts and must remain outside Git. The non-secret manifest and SQL fixture files can be retained with the snapshot in an access-controlled fixture location. Select one explicitly:

```sh
MIGRATION_REHEARSAL_MANIFEST=/fixtures/eqemu-deployed-9334/manifest.json \
  ./scripts/validate.sh --stack validation migration-rehearsal
```

The portable worker inherits `MIGRATION_REHEARSAL_MANIFEST` and writes `migration-rehearsal/result.json` under exact-Candidate evidence. Snapshot identity and checksum, source build, source server/bot/custom database versions, MariaDB version, Candidate commit, outcome, failure phase, isolated database name, and measured restore milliseconds are recorded. Logs must not contain credentials. Missing fixture infrastructure exits nonzero; it is not a skipped pass.

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
    "build": "<old build commit or immutable release id>",
    "mariadb_version": "10.11.8",
    "database_versions": {"server": 9334, "bots": 9055, "custom": 0}
  },
  "old_build": {
    "world_binary_container_path": "/opt/eqemu-old/9334/world",
    "world_binary_sha256": "<64 lowercase hex characters>"
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

All file paths are relative to and confined beneath the manifest directory. The old binary path is inside the one-off `eqemu-server` container and must be mounted by fixture setup. Fixture setup is separate from a validation run.

`seed-old-format.sql` must add representative rows for every changed saved-data shape. Use reserved fixture identities and include owners and balances on both sides of any transfer. `assert-upgraded.sql` must validate schema **and meaning**, including identity/ownership and item or currency conservation as applicable. `assert-restored.sql` validates the original snapshot after rollback. Each assertion file must return exactly one scalar row containing `ok`; any other output fails the profile. Candidate scenarios must exercise relevant production command/gameplay paths against the upgraded isolated database and must be bounded and self-cleaning.

The profile verifies the dump checksum and all source versions, imports and seeds it, runs the Candidate updater, checks upgraded meaning, runs the listed scenarios, and records the database-version/schema fingerprint. It then runs the updater again and requires that fingerprint to remain unchanged. Finally it drops and recreates the isolated database, reimports the pristine snapshot, checks restored data, verifies the old binary checksum, and runs that binary's `database:version` startup path. Cleanup drops both the temporary account and isolated database after success or failure; partial setup is tracked so later setup failures are also cleaned, and cleanup failure forces a nonzero result.

`./scripts/validate-afk` currently has no trusted base commit or schema-change classifier. It therefore conservatively selects `tier1-migration-tier3` for every Candidate. Operators can explicitly request `migration-rehearsal` or `tier1-migration-tier3` through the worker profile contract. A future trusted selector may use the lighter `tier1-tier3-harness` only after it can reliably prove that schema and saved data are unaffected.

## Maintenance-window release

Only perform this procedure after deployment is separately authorized:

1. Announce maintenance, close access, stop world/zone/login writers, and verify no other database writers remain.
2. Record the new and previous build/config identities. Create a backup name containing UTC date, time, release, and a unique token; do not use AkkStack's date-only name without copying it to a unique name. Record its SHA-256, DB versions, MariaDB version, size, and member names without recording secrets.
3. Rehearse that exact backup with this profile and the matching previous binary. Do not proceed unless upgrade, assertions, second updater run, scenarios, rollback import, and old-build recovery all pass.
4. With writers still stopped, take the final uniquely named backup. Run the tested Candidate `world database:updates` explicitly. Verify resulting versions and the same data assertions, then start the matching new build for a bounded smoke check before reopening access. Account for automatic startup updates.
5. On failure, keep writers stopped. Drop/recreate the affected database, restore the matching final backup, start the recorded previous build/config, and rerun the restored-data/startup checks. Reopen only after recovery succeeds. If access had already reopened, record the exact progress-loss window.

## Current evidence and limitations

A host-local baseline has been captured, but it remains deliberately unconfigured for this implementation repair. Its source checkout is not proof that the archived installed binaries match, and no isolated upgrade/rollback rehearsal has passed yet. Compatibility, restore timing, updater idempotence, scenario behavior, and old-build recovery therefore remain unproven until the fixture is reviewed, selected, and run.

The profile reuses one MariaDB process and isolates by a restricted temporary account and database name rather than by a disposable server volume. It does not prove zero-downtime migration, universal reverse migrations, client rendering, cross-zone handoff, or production performance. Restore duration on local validation hardware is evidence for maintenance planning, not a production SLA. Snapshot acquisition, secret handling, and mounting the old binary remain trusted fixture-setup responsibilities.

# Database changes and release procedure

These are required delivery rules. Snapshot-based migration validation is planned, not an existing command. Until it lands, follow the [current backup gate and worker contract](../testing/process.md#backup-gate) and report missing upgrade evidence explicitly. Never assume the shared validation database is disposable.

## When implementing schema or saved-data changes

1. Use the existing EQEmu versioned updater in `common/database/database_update.cpp` and its manifests. Add a new migration and update the corresponding binary database version. Do not rewrite deployed migrations or invent a second migration system.
2. Prefer additive changes with explicit defaults/backfills. Preserve affected identities, ownership, items and currency. Defer destructive cleanup until the replacement works and rollback compatibility is understood.
3. Rehearse the actual Candidate updater against an isolated restore of a known deployed-version snapshot, with targeted old-format fixtures. Assert data meaning as well as schema, rerun the updater without changes, then run the affected gameplay scenarios on the upgraded database. A fresh-schema test alone is insufficient.
4. Retain snapshot identity/checksum, source build and database versions, resulting versions, Candidate commit and assertions. Keep dumps and secrets outside Git and published evidence. Prove safe recovery from failure; do not assume a multi-statement migration rolls back atomically.
5. Use repository-owned profiles for final evidence. If isolation or rehearsal support is missing, identify the prerequisite rather than testing destructive changes on the gameplay database. Routine scenarios may continue using the persistent validation DB with exact fixture cleanup.

## When explicitly deploying a release

Feature implementation or an AFK PR does not itself request gameplay deployment. Once deployment is authorized:

1. Stop gameplay processes and all other writers. Retain the previous build/config identity and take a uniquely named backup of all affected databases. Verify restore usability through the rehearsal procedure.
2. Apply the tested updater as an explicit release step, verify version and data postconditions, then start the matching new build for a short smoke check before reopening access. Account for world startup's automatic-update setting so startup does not introduce unreviewed migrations.
3. On failure, keep writers stopped and restore the matching backup and previous build/config together. Rolling back binaries alone is allowed only with demonstrated schema compatibility. Restoring after reopening may lose subsequent progress; record that loss window.

No zero-downtime migration system or universal reverse migrations are required. The initial policy accepts short maintenance and small progress rollbacks. Existing date-only AkkStack backup names can overwrite same-day backups; preserve uniquely named artifacts. A backup file alone is not proof that restore works.

#!/usr/bin/env bash
set -euo pipefail

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
repo_root="$(cd "$script_dir/.." && pwd)"

usage() {
  cat <<'EOF'
Usage: scripts/prepare-migration-rehearsal-fixture.sh --baseline-dir DIR [metadata options]

Prepare a captured baseline for scripts/validate-afk without copying its private
artifacts into Git. DIR must contain manifest.json, database.sql.gz (or .zst or
.sql), and installed-binaries.tar.gz. The capture manifest must contain the
actual SHA-256 values of both archives.

Metadata is read from common capture-manifest fields. Use these options only
when the capture uses another shape:
  --snapshot-id ID
  --source-build ID
  --mariadb-version VERSION
  --server-version NUMBER
  --bots-version NUMBER
  --custom-version NUMBER
  --world-member ARCHIVE_PATH

The command writes migration-rehearsal-manifest.json, fixture SQL and explicit selection files beneath DIR. It never configures
or runs validation by itself.
EOF
}

baseline_dir=""
snapshot_id=""
source_build=""
mariadb_version=""
server_version=""
bots_version=""
custom_version=""
world_member=""
while [[ "$#" -gt 0 ]]; do
  case "$1" in
    --baseline-dir) baseline_dir="${2:-}"; shift 2 ;;
    --snapshot-id) snapshot_id="${2:-}"; shift 2 ;;
    --source-build) source_build="${2:-}"; shift 2 ;;
    --mariadb-version) mariadb_version="${2:-}"; shift 2 ;;
    --server-version) server_version="${2:-}"; shift 2 ;;
    --bots-version) bots_version="${2:-}"; shift 2 ;;
    --custom-version) custom_version="${2:-}"; shift 2 ;;
    --world-member) world_member="${2:-}"; shift 2 ;;
    -h|--help) usage; exit 0 ;;
    *) printf 'error: unknown argument: %s\n' "$1" >&2; usage >&2; exit 2 ;;
  esac
done

[[ -n "$baseline_dir" && -d "$baseline_dir" ]] || { printf 'error: --baseline-dir must name the captured baseline directory\n' >&2; exit 2; }
command -v jq >/dev/null || { printf 'error: jq is required\n' >&2; exit 125; }
command -v python3 >/dev/null || { printf 'error: python3 is required\n' >&2; exit 125; }
command -v sha256sum >/dev/null || { printf 'error: sha256sum is required\n' >&2; exit 125; }
baseline_dir="$(realpath "$baseline_dir")"
capture_manifest="$baseline_dir/manifest.json"
[[ -f "$capture_manifest" ]] || { printf 'error: captured baseline is missing manifest.json\n' >&2; exit 2; }

snapshot=""
for candidate in database.sql.gz database.sql.zst database.sql; do
  if [[ -f "$baseline_dir/$candidate" ]]; then
    [[ -z "$snapshot" ]] || { printf 'error: captured baseline contains multiple database snapshots\n' >&2; exit 2; }
    snapshot="$baseline_dir/$candidate"
  fi
done
[[ -n "$snapshot" ]] || { printf 'error: captured baseline is missing database.sql[.gz|.zst]\n' >&2; exit 2; }
if [[ "$snapshot" == *.zst ]]; then
  command -v zstd >/dev/null || { printf 'error: zstd is required for database.sql.zst snapshots\n' >&2; exit 125; }
fi
binary_archive="$baseline_dir/installed-binaries.tar.gz"
[[ -f "$binary_archive" ]] || { printf 'error: captured baseline is missing installed-binaries.tar.gz\n' >&2; exit 2; }

snapshot_sha="$(sha256sum "$snapshot" | awk '{print $1}')"
archive_sha="$(sha256sum "$binary_archive" | awk '{print $1}')"
read_capture() {
  local expression="$1"
  jq -er "$expression | select(. != null and . != \"\")" "$capture_manifest" 2>/dev/null || true
}
# Compare the digests only with their declared artifact fields. An unscoped text
# search could accidentally accept a digest copied into an unrelated note.
snapshot_name="$(basename "$snapshot")"
recorded_snapshot_sha="$(jq -er --arg name "$snapshot_name" '
  .files[$name].sha256 // .artifacts.database.sha256 // .snapshot.sha256
  | select(type == "string") | ascii_downcase
' "$capture_manifest" 2>/dev/null || true)"
recorded_archive_sha="$(jq -er '
  .files["installed-binaries.tar.gz"].sha256 // .artifacts.binaries.sha256 // .old_build.archive_sha256
  | select(type == "string") | ascii_downcase
' "$capture_manifest" 2>/dev/null || true)"
[[ "$recorded_snapshot_sha" == "$snapshot_sha" ]] || { printf 'error: manifest.json database snapshot checksum does not match the captured artifact\n' >&2; exit 1; }
[[ "$recorded_archive_sha" == "$archive_sha" ]] || { printf 'error: manifest.json installed-binaries checksum does not match the captured artifact\n' >&2; exit 1; }

[[ -n "$snapshot_id" ]] || snapshot_id="$(read_capture '.snapshot.id // .baseline_id // .id')"
[[ -n "$snapshot_id" ]] || snapshot_id="$(basename "$baseline_dir")"
[[ -n "$source_build" ]] || source_build="$(read_capture '(.source | objects | .checkout_commit // .commit // .build) // .source_checkout // .source_checkout_sha')"
[[ -n "$mariadb_version" ]] || mariadb_version="$(read_capture '(.source | objects | .mariadb_version) // .database.mariadb_version // .mariadb_version')"
[[ -n "$server_version" ]] || server_version="$(read_capture '(.source | objects | .database_versions.server) // .database_versions.server // .database.version')"
[[ -n "$bots_version" ]] || bots_version="$(read_capture '(.source | objects | .database_versions.bots) // .database_versions.bots // .database.bots_version')"
[[ -n "$custom_version" ]] || custom_version="$(read_capture '(.source | objects | .database_versions.custom) // .database_versions.custom // .database.custom_version')"
if [[ -z "$server_version" || -z "$bots_version" || -z "$custom_version" ]]; then
  database_version_row="$(read_capture '.database_version_row')"
  if [[ -n "$database_version_row" ]]; then
    IFS=$'\t' read -r captured_server captured_bots captured_custom extra <<<"$database_version_row"
    [[ -z "${extra:-}" ]] || { printf 'error: database_version_row must contain exactly three tab-separated values\n' >&2; exit 2; }
    [[ -n "$server_version" ]] || server_version="$captured_server"
    [[ -n "$bots_version" ]] || bots_version="$captured_bots"
    [[ -n "$custom_version" ]] || custom_version="$captured_custom"
  fi
fi
[[ -n "$source_build" && -n "$mariadb_version" && -n "$server_version" && -n "$bots_version" && -n "$custom_version" ]] || {
  printf 'error: capture metadata is incomplete; provide the documented metadata options\n' >&2
  exit 2
}
[[ "$mariadb_version" =~ ^[0-9]+([.][0-9]+)+$ ]] || {
  printf 'error: MariaDB version must be a dotted numeric version\n' >&2
  exit 2
}
[[ "$server_version" =~ ^[0-9]+$ && "$bots_version" =~ ^[0-9]+$ && "$custom_version" =~ ^[0-9]+$ ]] || {
  printf 'error: database versions must be nonnegative integers\n' >&2
  exit 2
}

fixture_dir="$baseline_dir/migration-rehearsal-fixture"
extract_dir="$fixture_dir/old-build"
rm -rf "$fixture_dir"
mkdir -p "$extract_dir"
python3 - "$binary_archive" "$extract_dir" <<'PY'
from pathlib import Path
import sys, tarfile

archive, destination = sys.argv[1], Path(sys.argv[2]).resolve()
with tarfile.open(archive, "r:gz") as tf:
    for member in tf.getmembers():
        target = (destination / member.name).resolve()
        if destination != target and destination not in target.parents:
            raise SystemExit("error: installed-binaries archive contains an escaping path")
        if member.issym():
            link = (target.parent / member.linkname).resolve()
        elif member.islnk():
            # tar hard-link names are relative to the archive root, unlike
            # symbolic-link targets, which are relative to the link's parent.
            link = (destination / member.linkname).resolve()
        else:
            continue
        if destination != link and destination not in link.parents:
            raise SystemExit("error: installed-binaries archive contains an escaping link")
    tf.extractall(destination)
PY

if [[ -n "$world_member" ]]; then
  [[ "$world_member" != /* && "$world_member" != ../* && "$world_member" != *'/../'* ]] || { printf 'error: --world-member must be a safe archive-relative path\n' >&2; exit 2; }
  old_world="$extract_dir/$world_member"
  [[ -f "$old_world" && -x "$old_world" ]] || { printf 'error: selected old world binary is missing or not executable\n' >&2; exit 2; }
else
  mapfile -t worlds < <(find "$extract_dir" -type f -name world -perm /111 -print)
  [[ "${#worlds[@]}" -eq 1 ]] || { printf 'error: expected exactly one executable named world; use --world-member to select it\n' >&2; exit 2; }
  old_world="${worlds[0]}"
fi
old_world="$(realpath "$old_world")"
[[ "$old_world" == "$extract_dir/"* ]] || { printf 'error: old world binary escapes the extracted archive\n' >&2; exit 2; }
old_world_relative="${old_world#"$extract_dir/"}"
old_world_sha="$(sha256sum "$old_world" | awk '{print $1}')"
capture_manifest_sha="$(sha256sum "$capture_manifest" | awk '{print $1}')"
preparer_commit="$(git -C "$repo_root" rev-parse HEAD)"
[[ "$preparer_commit" =~ ^[0-9a-f]{40}$ ]] || { printf 'error: could not identify fixture preparer commit\n' >&2; exit 2; }

cat >"$fixture_dir/seed-old-format.sql" <<'SQL'
DROP TABLE IF EXISTS `afk_migration_fixture_baseline`;
CREATE TABLE `afk_migration_fixture_baseline` (
  `character_count` BIGINT UNSIGNED NOT NULL,
  `character_owner_checksum` BIGINT UNSIGNED NOT NULL,
  `currency_copper_total` DECIMAL(65,0) NOT NULL,
  `bot_count` BIGINT UNSIGNED NOT NULL,
  `bot_owner_id_total` DECIMAL(65,0) NOT NULL,
  `bot_owner_checksum` BIGINT UNSIGNED NOT NULL,
  `actor_table_count` BIGINT UNSIGNED NOT NULL,
  `fixture_character_1_id` INT UNSIGNED NULL,
  `fixture_character_2_id` INT UNSIGNED NULL,
  `fixture_bot_1_id` INT UNSIGNED NULL,
  `fixture_bot_2_id` INT UNSIGNED NULL
) ENGINE=InnoDB;
INSERT INTO `afk_migration_fixture_baseline`
SELECT
  (SELECT COUNT(*) FROM `character_data`),
  (SELECT COALESCE(BIT_XOR(CRC32(CONCAT(`id`, ':', `account_id`))), 0) FROM `character_data`),
  (SELECT COALESCE(SUM(`copper` + 10 * `silver` + 100 * `gold` + 1000 * `platinum` +
      `copper_bank` + 10 * `silver_bank` + 100 * `gold_bank` + 1000 * `platinum_bank` +
      `copper_cursor` + 10 * `silver_cursor` + 100 * `gold_cursor` + 1000 * `platinum_cursor`), 0)
    FROM `character_currency`),
  (SELECT COUNT(*) FROM `bot_data`),
  (SELECT COALESCE(SUM(`owner_id`), 0) FROM `bot_data`),
  (SELECT COALESCE(BIT_XOR(CRC32(CONCAT(`bot_id`, ':', `owner_id`))), 0) FROM `bot_data`),
  (SELECT COUNT(*) FROM information_schema.tables
    WHERE table_schema = DATABASE() AND table_name IN
      ('actor_profiles', 'actor_status', 'actor_events', 'actor_action_queue')),
  NULL, NULL, NULL, NULL;

-- Seed representative records in production ownership/currency tables. Let
-- each production table allocate ordinary IDs: forcing IDs near UINT32_MAX
-- advances AUTO_INCREMENT and can make unrelated runtime scenario inserts fail.
-- The allocated IDs are recorded so assertions can distinguish fixture rows.
-- Abort instead of overwriting if any reserved name is already present.
DELIMITER //
CREATE PROCEDURE `afk_assert_fixture_identities_available`()
BEGIN
  IF EXISTS (SELECT 1 FROM `character_data`
      WHERE `name` IN ('AfkMigSender', 'AfkMigReceiver'))
    OR EXISTS (SELECT 1 FROM `bot_data`
      WHERE `name` IN ('AfkMigSenderBot', 'AfkMigReceiverBot')) THEN
    SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT = 'reserved migration fixture identity collides with snapshot data';
  END IF;
END//
DELIMITER ;
CALL `afk_assert_fixture_identities_available`();
DROP PROCEDURE `afk_assert_fixture_identities_available`;

INSERT INTO `character_data`
  (`account_id`, `name`, `last_name`, `level`, `class`, `race`)
VALUES
  (0, 'AfkMigSender', 'MigrationFixture', 1, 1, 1),
  (0, 'AfkMigReceiver', 'MigrationFixture', 1, 1, 1);
UPDATE `afk_migration_fixture_baseline` SET
  `fixture_character_1_id` = (SELECT `id` FROM `character_data` WHERE `name` = 'AfkMigSender'),
  `fixture_character_2_id` = (SELECT `id` FROM `character_data` WHERE `name` = 'AfkMigReceiver');
INSERT INTO `character_currency`
  (`id`, `platinum`, `gold`, `silver`, `copper`,
   `platinum_bank`, `gold_bank`, `silver_bank`, `copper_bank`)
SELECT `fixture_character_1_id`, 7, 0, 0, 0, 3, 0, 0, 0 FROM `afk_migration_fixture_baseline`
UNION ALL
SELECT `fixture_character_2_id`, 2, 0, 0, 0, 8, 0, 0, 0 FROM `afk_migration_fixture_baseline`;
INSERT INTO `bot_data` (`owner_id`, `name`, `level`, `class`, `race`)
SELECT `fixture_character_1_id`, 'AfkMigSenderBot', 1, 1, 1 FROM `afk_migration_fixture_baseline`
UNION ALL
SELECT `fixture_character_2_id`, 'AfkMigReceiverBot', 1, 1, 1 FROM `afk_migration_fixture_baseline`;
UPDATE `afk_migration_fixture_baseline` SET
  `fixture_bot_1_id` = (SELECT `bot_id` FROM `bot_data` WHERE `name` = 'AfkMigSenderBot'),
  `fixture_bot_2_id` = (SELECT `bot_id` FROM `bot_data` WHERE `name` = 'AfkMigReceiverBot');
SQL

cat >"$fixture_dir/assert-upgraded.sql" <<'SQL'
-- Keep each predicate separately named so a failed private rehearsal reports
-- only bounded assertion labels, never captured row values.
SET @afk_actor_tables_absent_before =
  (SELECT `actor_table_count` FROM `afk_migration_fixture_baseline`) = 0;
SET @afk_critical_columns = (SELECT COUNT(*) FROM information_schema.columns
  WHERE table_schema = DATABASE() AND
    (table_name, column_name, column_type, is_nullable) IN (
      ('actor_profiles', 'actor_id', 'int(10) unsigned', 'NO'),
      ('actor_profiles', 'bot_id', 'int(10) unsigned', 'YES'),
      ('actor_status', 'status_json', 'longtext', 'YES'),
      ('actor_events', 'event_id', 'bigint(20) unsigned', 'NO'),
      ('actor_events', 'event_json', 'longtext', 'NO'),
      ('actor_action_queue', 'action_id', 'bigint(20) unsigned', 'NO'),
      ('actor_action_queue', 'idempotency_key', 'varchar(128)', 'NO'),
      ('actor_action_queue', 'result_json', 'longtext', 'YES'))) = 8;
SET @afk_column_counts = (SELECT CONCAT_WS(':',
    SUM(table_name = 'actor_profiles'), SUM(table_name = 'actor_status'),
    SUM(table_name = 'actor_events'), SUM(table_name = 'actor_action_queue'))
  FROM information_schema.columns WHERE table_schema = DATABASE()
    AND table_name IN ('actor_profiles', 'actor_status', 'actor_events', 'actor_action_queue')) = '8:8:10:17';
SET @afk_indexes = (SELECT COUNT(DISTINCT CONCAT(table_name, ':', index_name))
  FROM information_schema.statistics WHERE table_schema = DATABASE()
    AND index_name IN ('idx_actor_profiles_bot_id', 'idx_actor_profiles_owner_character_id',
      'idx_actor_status_zone_binding', 'idx_actor_status_state_heartbeat',
      'idx_actor_events_actor_cursor', 'idx_actor_events_zone_created',
      'idx_actor_action_queue_actor_idempotency', 'idx_actor_action_queue_claim_path',
      'idx_actor_action_queue_actor_state')) = 9;
SET @afk_json_constraints = (SELECT COUNT(DISTINCT CASE
      WHEN tc.table_name = 'actor_status'
        AND REPLACE(REPLACE(REPLACE(REPLACE(REPLACE(LOWER(cc.check_clause), '`', ''), ' ', ''), CHAR(9), ''), CHAR(10), ''), CHAR(13), '') LIKE '%status_jsonisnull%'
        AND REPLACE(REPLACE(REPLACE(REPLACE(REPLACE(LOWER(cc.check_clause), '`', ''), ' ', ''), CHAR(9), ''), CHAR(10), ''), CHAR(13), '') LIKE '%json_valid(status_json)%'
        AND REPLACE(REPLACE(REPLACE(REPLACE(REPLACE(LOWER(cc.check_clause), '`', ''), ' ', ''), CHAR(9), ''), CHAR(10), ''), CHAR(13), '') LIKE '%char_length(status_json)<=4096%'
        THEN 'actor_status.status_json'
      WHEN tc.table_name = 'actor_events'
        AND REPLACE(REPLACE(REPLACE(REPLACE(REPLACE(LOWER(cc.check_clause), '`', ''), ' ', ''), CHAR(9), ''), CHAR(10), ''), CHAR(13), '') LIKE '%json_valid(event_json)%'
        AND REPLACE(REPLACE(REPLACE(REPLACE(REPLACE(LOWER(cc.check_clause), '`', ''), ' ', ''), CHAR(9), ''), CHAR(10), ''), CHAR(13), '') LIKE '%char_length(event_json)<=16384%'
        THEN 'actor_events.event_json'
      WHEN tc.table_name = 'actor_action_queue'
        AND REPLACE(REPLACE(REPLACE(REPLACE(REPLACE(LOWER(cc.check_clause), '`', ''), ' ', ''), CHAR(9), ''), CHAR(10), ''), CHAR(13), '') LIKE '%source_metadata_jsonisnull%'
        AND REPLACE(REPLACE(REPLACE(REPLACE(REPLACE(LOWER(cc.check_clause), '`', ''), ' ', ''), CHAR(9), ''), CHAR(10), ''), CHAR(13), '') LIKE '%json_valid(source_metadata_json)%'
        AND REPLACE(REPLACE(REPLACE(REPLACE(REPLACE(LOWER(cc.check_clause), '`', ''), ' ', ''), CHAR(9), ''), CHAR(10), ''), CHAR(13), '') LIKE '%char_length(source_metadata_json)<=4096%'
        THEN 'actor_action_queue.source_metadata_json'
      WHEN tc.table_name = 'actor_action_queue'
        AND REPLACE(REPLACE(REPLACE(REPLACE(REPLACE(LOWER(cc.check_clause), '`', ''), ' ', ''), CHAR(9), ''), CHAR(10), ''), CHAR(13), '') LIKE '%json_valid(action_json)%'
        AND REPLACE(REPLACE(REPLACE(REPLACE(REPLACE(LOWER(cc.check_clause), '`', ''), ' ', ''), CHAR(9), ''), CHAR(10), ''), CHAR(13), '') LIKE '%char_length(action_json)<=16384%'
        THEN 'actor_action_queue.action_json'
      WHEN tc.table_name = 'actor_action_queue'
        AND REPLACE(REPLACE(REPLACE(REPLACE(REPLACE(LOWER(cc.check_clause), '`', ''), ' ', ''), CHAR(9), ''), CHAR(10), ''), CHAR(13), '') LIKE '%result_jsonisnull%'
        AND REPLACE(REPLACE(REPLACE(REPLACE(REPLACE(LOWER(cc.check_clause), '`', ''), ' ', ''), CHAR(9), ''), CHAR(10), ''), CHAR(13), '') LIKE '%json_valid(result_json)%'
        AND REPLACE(REPLACE(REPLACE(REPLACE(REPLACE(LOWER(cc.check_clause), '`', ''), ' ', ''), CHAR(9), ''), CHAR(10), ''), CHAR(13), '') LIKE '%char_length(result_json)<=16384%'
        THEN 'actor_action_queue.result_json'
    END)
  FROM information_schema.table_constraints tc
  JOIN information_schema.check_constraints cc
    ON cc.constraint_schema = tc.constraint_schema AND cc.constraint_name = tc.constraint_name
  WHERE tc.table_schema = DATABASE() AND tc.constraint_type = 'CHECK') = 5;
SET @afk_character_count = (SELECT COUNT(*) FROM `character_data`
    WHERE `id` NOT IN (SELECT `fixture_character_1_id` FROM `afk_migration_fixture_baseline`
                       UNION SELECT `fixture_character_2_id` FROM `afk_migration_fixture_baseline`)) =
  (SELECT `character_count` FROM `afk_migration_fixture_baseline`);
SET @afk_character_owners = (SELECT COALESCE(BIT_XOR(CRC32(CONCAT(`id`, ':', `account_id`))), 0)
    FROM `character_data`
    WHERE `id` NOT IN (SELECT `fixture_character_1_id` FROM `afk_migration_fixture_baseline`
                       UNION SELECT `fixture_character_2_id` FROM `afk_migration_fixture_baseline`)) =
  (SELECT `character_owner_checksum` FROM `afk_migration_fixture_baseline`);
SET @afk_currency = (SELECT COALESCE(SUM(`copper` + 10 * `silver` + 100 * `gold` + 1000 * `platinum` +
    `copper_bank` + 10 * `silver_bank` + 100 * `gold_bank` + 1000 * `platinum_bank` +
    `copper_cursor` + 10 * `silver_cursor` + 100 * `gold_cursor` + 1000 * `platinum_cursor`), 0)
  FROM `character_currency`
  WHERE `id` NOT IN (SELECT `fixture_character_1_id` FROM `afk_migration_fixture_baseline`
                     UNION SELECT `fixture_character_2_id` FROM `afk_migration_fixture_baseline`)) =
  (SELECT `currency_copper_total` FROM `afk_migration_fixture_baseline`);
SET @afk_bot_count = (SELECT COUNT(*) FROM `bot_data`
    WHERE `bot_id` NOT IN (SELECT `fixture_bot_1_id` FROM `afk_migration_fixture_baseline`
                           UNION SELECT `fixture_bot_2_id` FROM `afk_migration_fixture_baseline`)) =
  (SELECT `bot_count` FROM `afk_migration_fixture_baseline`);
SET @afk_bot_owner_total = (SELECT COALESCE(SUM(`owner_id`), 0) FROM `bot_data`
    WHERE `bot_id` NOT IN (SELECT `fixture_bot_1_id` FROM `afk_migration_fixture_baseline`
                           UNION SELECT `fixture_bot_2_id` FROM `afk_migration_fixture_baseline`)) =
  (SELECT `bot_owner_id_total` FROM `afk_migration_fixture_baseline`);
SET @afk_bot_owners = (SELECT COALESCE(BIT_XOR(CRC32(CONCAT(`bot_id`, ':', `owner_id`))), 0) FROM `bot_data`
    WHERE `bot_id` NOT IN (SELECT `fixture_bot_1_id` FROM `afk_migration_fixture_baseline`
                           UNION SELECT `fixture_bot_2_id` FROM `afk_migration_fixture_baseline`)) =
  (SELECT `bot_owner_checksum` FROM `afk_migration_fixture_baseline`);
SET @afk_fixture_characters = (SELECT COUNT(*) FROM `character_data` c JOIN `afk_migration_fixture_baseline` f
    ON (c.`id` = f.`fixture_character_1_id` AND c.`name` = 'AfkMigSender')
    OR (c.`id` = f.`fixture_character_2_id` AND c.`name` = 'AfkMigReceiver')) = 2;
SET @afk_fixture_bots = (SELECT COUNT(*) FROM `bot_data` b JOIN `afk_migration_fixture_baseline` f
    ON (b.`bot_id` = f.`fixture_bot_1_id` AND b.`owner_id` = f.`fixture_character_1_id`)
    OR (b.`bot_id` = f.`fixture_bot_2_id` AND b.`owner_id` = f.`fixture_character_2_id`)) = 2;
SET @afk_fixture_currency = (SELECT SUM(c.`copper` + 10 * c.`silver` + 100 * c.`gold` + 1000 * c.`platinum` +
    c.`copper_bank` + 10 * c.`silver_bank` + 100 * c.`gold_bank` + 1000 * c.`platinum_bank` +
    c.`copper_cursor` + 10 * c.`silver_cursor` + 100 * c.`gold_cursor` + 1000 * c.`platinum_cursor`)
  FROM `character_currency` c JOIN `afk_migration_fixture_baseline` f
    ON c.`id` IN (f.`fixture_character_1_id`, f.`fixture_character_2_id`)) = 20000;

SET @afk_failures = CONCAT_WS(',',
  IF(@afk_actor_tables_absent_before, NULL, 'preexisting_actor_tables'),
  IF(@afk_critical_columns, NULL, 'critical_columns'),
  IF(@afk_column_counts, NULL, 'column_counts'),
  IF(@afk_indexes, NULL, 'indexes'),
  IF(@afk_json_constraints, NULL, 'json_constraints'),
  IF(@afk_character_count, NULL, 'character_count'),
  IF(@afk_character_owners, NULL, 'character_owners'),
  IF(@afk_currency, NULL, 'currency'),
  IF(@afk_bot_count, NULL, 'bot_count'),
  IF(@afk_bot_owner_total, NULL, 'bot_owner_total'),
  IF(@afk_bot_owners, NULL, 'bot_owners'),
  IF(@afk_fixture_characters, NULL, 'fixture_characters'),
  IF(@afk_fixture_bots, NULL, 'fixture_bots'),
  IF(@afk_fixture_currency, NULL, 'fixture_currency'));
SELECT IF(@afk_failures = '', 'ok', CONCAT('failed:', @afk_failures));
SQL

cat >"$fixture_dir/assert-restored.sql" <<'SQL'
SELECT IF(
  (SELECT CONCAT(version, ':', bots_version, ':', custom_version) FROM db_version LIMIT 1) =
    '@SOURCE_DATABASE_VERSIONS@'
  AND (SELECT COUNT(*) FROM information_schema.tables
    WHERE table_schema = DATABASE() AND table_name IN
      ('actor_profiles', 'actor_status', 'actor_events', 'actor_action_queue')) = 0
  AND (SELECT COUNT(*) FROM `character_data`
    WHERE `name` IN ('AfkMigSender', 'AfkMigReceiver')) = 0
  AND (SELECT COUNT(*) FROM `bot_data`
    WHERE `name` IN ('AfkMigSenderBot', 'AfkMigReceiverBot')) = 0,
  'ok', 'failed:restored_version_schema_or_fixture_rows');
SQL
sed -i "s/@SOURCE_DATABASE_VERSIONS@/$server_version:$bots_version:$custom_version/" \
  "$fixture_dir/assert-restored.sql"

manifest_path="$baseline_dir/migration-rehearsal-manifest.json"
jq -n \
  --arg snapshot_id "$snapshot_id" --arg snapshot_file "$(basename "$snapshot")" --arg snapshot_sha "$snapshot_sha" \
  --arg source_build "$source_build" --arg mariadb_version "$mariadb_version" \
  --argjson server_version "$server_version" --argjson bots_version "$bots_version" --argjson custom_version "$custom_version" \
  --arg world_path "/opt/eqemu-old/$old_world_relative" --arg world_sha "$old_world_sha" \
  --arg capture_manifest_sha "$capture_manifest_sha" --arg preparer_commit "$preparer_commit" \
  '{snapshot:{id:$snapshot_id,file:$snapshot_file,sha256:$snapshot_sha},source:{build:$source_build,build_identity_attested:false,mariadb_version:$mariadb_version,database_versions:{server:$server_version,bots:$bots_version,custom:$custom_version},capture_manifest_sha256:$capture_manifest_sha,fixture_preparer_commit:$preparer_commit},old_build:{world_binary_container_path:$world_path,world_binary_sha256:$world_sha,host_directory:"migration-rehearsal-fixture/old-build"},fixtures:{seed_sql:"migration-rehearsal-fixture/seed-old-format.sql",upgraded_assert_sql:"migration-rehearsal-fixture/assert-upgraded.sql",restored_assert_sql:"migration-rehearsal-fixture/assert-restored.sql"},candidate_scenarios:["/home/eqemu/code/build/bin/zone tests:actor-events"]}' >"$manifest_path"

env_path="$baseline_dir/migration-rehearsal.env"
selection_path="$baseline_dir/migration-rehearsal.manifest-path"
printf 'export MIGRATION_REHEARSAL_MANIFEST=%q\n' "$manifest_path" >"$env_path"
printf '%s\n' "$manifest_path" >"$selection_path"
printf 'Prepared migration rehearsal fixture. Review it before enabling the generated selection file.\n'

#!/usr/bin/env bash
set -euo pipefail

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

The command writes migration-rehearsal-manifest.json, fixture SQL, a Compose
mount override, and explicit selection files beneath DIR. It never configures
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
binary_archive="$baseline_dir/installed-binaries.tar.gz"
[[ -f "$binary_archive" ]] || { printf 'error: captured baseline is missing installed-binaries.tar.gz\n' >&2; exit 2; }

snapshot_sha="$(sha256sum "$snapshot" | awk '{print $1}')"
archive_sha="$(sha256sum "$binary_archive" | awk '{print $1}')"
# The capture manifest is untrusted input, but requiring its recorded digests to
# match both artifacts detects a stale or accidentally mixed capture.
grep -Fqi -- "$snapshot_sha" "$capture_manifest" || { printf 'error: manifest.json does not record the database snapshot checksum\n' >&2; exit 1; }
grep -Fqi -- "$archive_sha" "$capture_manifest" || { printf 'error: manifest.json does not record the installed-binaries checksum\n' >&2; exit 1; }

read_capture() {
  local expression="$1"
  jq -er "$expression | select(. != null and . != \"\")" "$capture_manifest" 2>/dev/null || true
}
[[ -n "$snapshot_id" ]] || snapshot_id="$(read_capture '.snapshot.id // .baseline_id // .id')"
[[ -n "$snapshot_id" ]] || snapshot_id="$(basename "$baseline_dir")"
[[ -n "$source_build" ]] || source_build="$(read_capture '.source.checkout_commit // .source.commit // .source.build // .source_checkout')"
[[ -n "$mariadb_version" ]] || mariadb_version="$(read_capture '.source.mariadb_version // .database.mariadb_version // .mariadb_version')"
[[ -n "$server_version" ]] || server_version="$(read_capture '.source.database_versions.server // .database_versions.server // .database.version')"
[[ -n "$bots_version" ]] || bots_version="$(read_capture '.source.database_versions.bots // .database_versions.bots // .database.bots_version')"
[[ -n "$custom_version" ]] || custom_version="$(read_capture '.source.database_versions.custom // .database_versions.custom // .database.custom_version')"
[[ -n "$source_build" && -n "$mariadb_version" && -n "$server_version" && -n "$bots_version" && -n "$custom_version" ]] || {
  printf 'error: capture metadata is incomplete; provide the documented metadata options\n' >&2
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
        if member.issym() or member.islnk():
            link = (target.parent / member.linkname).resolve()
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

cat >"$fixture_dir/seed-old-format.sql" <<'SQL'
DROP TABLE IF EXISTS `afk_migration_fixture_baseline`;
CREATE TABLE `afk_migration_fixture_baseline` (
  `character_count` BIGINT UNSIGNED NOT NULL,
  `character_owner_checksum` BIGINT UNSIGNED NOT NULL,
  `currency_copper_total` DECIMAL(65,0) NOT NULL,
  `bot_count` BIGINT UNSIGNED NOT NULL,
  `bot_owner_id_total` DECIMAL(65,0) NOT NULL,
  `bot_owner_checksum` BIGINT UNSIGNED NOT NULL,
  `actor_table_count` BIGINT UNSIGNED NOT NULL
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
      ('actor_profiles', 'actor_status', 'actor_events', 'actor_action_queue'));
SQL

cat >"$fixture_dir/assert-upgraded.sql" <<'SQL'
SELECT IF(
  (SELECT `actor_table_count` FROM `afk_migration_fixture_baseline`) = 0
  AND (SELECT COUNT(*) FROM information_schema.tables
    WHERE table_schema = DATABASE() AND table_name IN
      ('actor_profiles', 'actor_status', 'actor_events', 'actor_action_queue')) = 4
  AND (SELECT COUNT(*) FROM `character_data`) =
      (SELECT `character_count` FROM `afk_migration_fixture_baseline`)
  AND (SELECT COALESCE(BIT_XOR(CRC32(CONCAT(`id`, ':', `account_id`))), 0) FROM `character_data`) =
      (SELECT `character_owner_checksum` FROM `afk_migration_fixture_baseline`)
  AND (SELECT COALESCE(SUM(`copper` + 10 * `silver` + 100 * `gold` + 1000 * `platinum` +
      `copper_bank` + 10 * `silver_bank` + 100 * `gold_bank` + 1000 * `platinum_bank` +
      `copper_cursor` + 10 * `silver_cursor` + 100 * `gold_cursor` + 1000 * `platinum_cursor`), 0)
    FROM `character_currency`) =
      (SELECT `currency_copper_total` FROM `afk_migration_fixture_baseline`)
  AND (SELECT COUNT(*) FROM `bot_data`) =
      (SELECT `bot_count` FROM `afk_migration_fixture_baseline`)
  AND (SELECT COALESCE(SUM(`owner_id`), 0) FROM `bot_data`) =
      (SELECT `bot_owner_id_total` FROM `afk_migration_fixture_baseline`)
  AND (SELECT COALESCE(BIT_XOR(CRC32(CONCAT(`bot_id`, ':', `owner_id`))), 0) FROM `bot_data`) =
      (SELECT `bot_owner_checksum` FROM `afk_migration_fixture_baseline`),
  'ok', 'failed');
SQL

cat >"$fixture_dir/assert-restored.sql" <<SQL
SELECT IF(
  (SELECT CONCAT(version, ':', bots_version, ':', custom_version) FROM db_version LIMIT 1) =
    '$server_version:$bots_version:$custom_version'
  AND (SELECT COUNT(*) FROM information_schema.tables
    WHERE table_schema = DATABASE() AND table_name IN
      ('actor_profiles', 'actor_status', 'actor_events', 'actor_action_queue')) = 0,
  'ok', 'failed');
SQL

compose_override="$fixture_dir/docker-compose.migration-rehearsal.yml"
printf 'services:\n  eqemu-server:\n    volumes:\n      - %s\n' "$(jq -Rn --arg mount "$extract_dir:/opt/eqemu-old:ro" '$mount')" >"$compose_override"
manifest_path="$baseline_dir/migration-rehearsal-manifest.json"
jq -n \
  --arg snapshot_id "$snapshot_id" --arg snapshot_file "$(basename "$snapshot")" --arg snapshot_sha "$snapshot_sha" \
  --arg source_build "$source_build" --arg mariadb_version "$mariadb_version" \
  --argjson server_version "$server_version" --argjson bots_version "$bots_version" --argjson custom_version "$custom_version" \
  --arg world_path "/opt/eqemu-old/$old_world_relative" --arg world_sha "$old_world_sha" \
  --arg capture_manifest_sha "$capture_manifest_sha" \
  '{snapshot:{id:$snapshot_id,file:$snapshot_file,sha256:$snapshot_sha},source:{build:$source_build,build_identity_attested:false,mariadb_version:$mariadb_version,database_versions:{server:$server_version,bots:$bots_version,custom:$custom_version},capture_manifest_sha256:$capture_manifest_sha},old_build:{world_binary_container_path:$world_path,world_binary_sha256:$world_sha,host_directory:"migration-rehearsal-fixture/old-build",compose_file:"migration-rehearsal-fixture/docker-compose.migration-rehearsal.yml"},fixtures:{seed_sql:"migration-rehearsal-fixture/seed-old-format.sql",upgraded_assert_sql:"migration-rehearsal-fixture/assert-upgraded.sql",restored_assert_sql:"migration-rehearsal-fixture/assert-restored.sql"},candidate_scenarios:["~/code/build/bin/zone tests:actor-events"]}' >"$manifest_path"

env_path="$baseline_dir/migration-rehearsal.env"
selection_path="$baseline_dir/migration-rehearsal.manifest-path"
printf 'export MIGRATION_REHEARSAL_MANIFEST=%q\n' "$manifest_path" >"$env_path"
printf '%s\n' "$manifest_path" >"$selection_path"
printf 'Prepared migration rehearsal fixture. Review it before enabling the generated selection file.\n'

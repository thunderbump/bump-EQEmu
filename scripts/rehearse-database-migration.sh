#!/usr/bin/env bash
set -euo pipefail

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
repo_root="$(cd "$script_dir/.." && pwd)"
source "$script_dir/lib/akkstack-routing.sh"

usage() {
  cat <<'EOF'
Usage: scripts/rehearse-database-migration.sh [--stack validation] [--dry-run]

Rehearses the Candidate updater and rollback in a uniquely named database in the
validation MariaDB service. MIGRATION_REHEARSAL_MANIFEST must name a JSON
manifest; snapshot and SQL paths are relative to that manifest. See
 docs/testing/database-migration-rehearsal.md for the manifest contract.
EOF
}

akkstack_init_routing "$repo_root" validation "$@"
if [[ "$AKKSTACK_HELP" -eq 1 ]]; then usage; exit 0; fi
if [[ "${#AKKSTACK_REMAINING_ARGS[@]}" -ne 0 ]]; then usage >&2; exit 2; fi
if [[ "$AKKSTACK_STACK_ROLE" != validation ]]; then
  printf 'error: migration rehearsal only accepts --stack validation\n' >&2
  exit 2
fi

stack_dir="$AKKSTACK_STACK_DIR"
gameplay_resolved="$AKKSTACK_GAMEPLAY_DEFAULT_RESOLVED"
if [[ -e "$AKKSTACK_GAMEPLAY_DEFAULT_DIR" && "$stack_dir" == "$gameplay_resolved" ]]; then
  printf 'error: migration rehearsal refuses the gameplay AkkStack destination\n' >&2
  exit 2
fi

manifest="${MIGRATION_REHEARSAL_MANIFEST:-}"
evidence_dir="${MIGRATION_REHEARSAL_EVIDENCE_DIR:-}"
if [[ "$AKKSTACK_DRY_RUN" -eq 1 ]]; then
  akkstack_print_dry_run "would verify a checksummed deployed snapshot and matching old build, create a unique isolated database, seed old-format fixtures, run the Candidate updater twice, run manifest scenarios, restore the snapshot, prove old-build recovery, record restore cost, and drop the isolated database" docker-compose.yml docker-compose.dev.yml
  printf '  destination guard: validation role; gameplay and shared validation databases rejected\n'
  exit 0
fi

[[ -n "$manifest" && -f "$manifest" ]] || { printf 'error: MIGRATION_REHEARSAL_MANIFEST must name a readable manifest\n' >&2; exit 125; }
command -v jq >/dev/null || { printf 'error: jq is required\n' >&2; exit 125; }
command -v sha256sum >/dev/null || { printf 'error: sha256sum is required\n' >&2; exit 125; }
manifest="$(realpath "$manifest")"
manifest_dir="$(dirname "$manifest")"

required_string() {
  local expression="$1" label="$2" value
  value="$(jq -er "$expression | select(type == \"string\" and length > 0)" "$manifest" 2>/dev/null)" || {
    printf 'error: manifest requires %s\n' "$label" >&2; exit 2;
  }
  printf '%s' "$value"
}
resolve_member() {
  local member="$1" resolved
  [[ "$member" != /* && "$member" != ../* && "$member" != *'/../'* ]] || { printf 'error: manifest paths must be relative and may not escape their directory\n' >&2; exit 2; }
  resolved="$(realpath -e "$manifest_dir/$member")" || { printf 'error: manifest member is missing: %s\n' "$member" >&2; exit 125; }
  [[ "$resolved" == "$manifest_dir/"* ]] || { printf 'error: manifest member escapes its directory: %s\n' "$member" >&2; exit 2; }
  printf '%s' "$resolved"
}

snapshot_id="$(required_string '.snapshot.id' snapshot.id)"
snapshot_sha="$(required_string '.snapshot.sha256' snapshot.sha256)"
source_build="$(required_string '.source.build' source.build)"
source_mariadb="$(required_string '.source.mariadb_version' source.mariadb_version)"
source_server_version="$(jq -er '.source.database_versions.server | numbers' "$manifest")" || { printf 'error: manifest requires numeric source.database_versions.server\n' >&2; exit 2; }
source_bots_version="$(jq -er '.source.database_versions.bots | numbers' "$manifest")" || { printf 'error: manifest requires numeric source.database_versions.bots\n' >&2; exit 2; }
source_custom_version="$(jq -er '.source.database_versions.custom | numbers' "$manifest")" || { printf 'error: manifest requires numeric source.database_versions.custom\n' >&2; exit 2; }
scenarios_json="$(jq -cer '.candidate_scenarios | select(type == "array" and length > 0 and all(.[]; type == "string" and length > 0))' "$manifest")" || { printf 'error: manifest requires at least one candidate_scenarios command\n' >&2; exit 2; }
snapshot="$(resolve_member "$(required_string '.snapshot.file' snapshot.file)")"
seed_sql="$(resolve_member "$(required_string '.fixtures.seed_sql' fixtures.seed_sql)")"
upgraded_assert_sql="$(resolve_member "$(required_string '.fixtures.upgraded_assert_sql' fixtures.upgraded_assert_sql)")"
restored_assert_sql="$(resolve_member "$(required_string '.fixtures.restored_assert_sql' fixtures.restored_assert_sql)")"
seed_sha="$(sha256sum "$seed_sql" | awk '{print $1}')"
upgraded_assert_sha="$(sha256sum "$upgraded_assert_sql" | awk '{print $1}')"
restored_assert_sha="$(sha256sum "$restored_assert_sql" | awk '{print $1}')"
old_world_path="$(required_string '.old_build.world_binary_container_path' old_build.world_binary_container_path)"
old_world_sha="$(required_string '.old_build.world_binary_sha256' old_build.world_binary_sha256)"
[[ "$snapshot_sha" =~ ^[0-9a-f]{64}$ && "$old_world_sha" =~ ^[0-9a-f]{64}$ ]] || { printf 'error: manifest checksums must be lowercase SHA-256 values\n' >&2; exit 2; }
actual_snapshot_sha="$(sha256sum "$snapshot" | awk '{print $1}')"
[[ "$actual_snapshot_sha" == "$snapshot_sha" ]] || { printf 'error: snapshot checksum mismatch for %s\n' "$snapshot_id" >&2; exit 1; }

candidate_commit="$(git -C "$repo_root" rev-parse HEAD)"
run_token="$(date -u +%Y%m%dT%H%M%SZ)-$$-$RANDOM"
target_suffix="${run_token//[^A-Za-z0-9]/_}"
target_db="afk_migration_$target_suffix"
target_user="afk_mig_${$}_${RANDOM}"
target_password="$(printf '%s' "$run_token-$RANDOM-$candidate_commit" | sha256sum | awk '{print $1}')"
[[ "$target_db" =~ ^afk_migration_[A-Za-z0-9_]+$ && "$target_user" =~ ^afk_mig_[A-Za-z0-9_]+$ ]] || { printf 'error: failed to construct safe isolated database identity\n' >&2; exit 2; }
if [[ -z "$evidence_dir" ]]; then evidence_dir="$(mktemp -d "${TMPDIR:-/tmp}/eqemu-migration-evidence.XXXXXX")"; fi
if [[ -e "$evidence_dir" && ! -d "$evidence_dir" ]]; then printf 'error: evidence destination is not a directory\n' >&2; exit 2; fi
mkdir -p "$evidence_dir"
evidence_dir="$(realpath "$evidence_dir")"
result="$evidence_dir/result.json"
log="$evidence_dir/rehearsal.log"
compose=(docker-compose -f docker-compose.yml -f docker-compose.dev.yml)
database_created=0
user_created=0
status=failed
failure_step=setup
restore_ms=null
candidate_versions=""

root_sql() {
  local statement="$1"
  printf '%s\n' "$statement" | "${compose[@]}" exec -T mariadb bash -lc 'MYSQL_PWD="$MYSQL_ROOT_PASSWORD" mysql -uroot'
}
target_mysql() {
  "${compose[@]}" exec -T mariadb bash -lc 'MYSQL_PWD="$2" mysql -N -B -u"$1" "$3"' _ "$target_user" "$target_password" "$target_db"
}
target_query() {
  local statement="$1"
  "${compose[@]}" exec -T mariadb bash -lc 'MYSQL_PWD="$2" mysql -N -B -u"$1" "$3" -e "$4"' _ "$target_user" "$target_password" "$target_db" "$statement"
}

write_result() {
  jq -n --arg status "$status" --arg failure_step "$failure_step" --arg candidate_commit "$candidate_commit" \
    --arg snapshot_id "$snapshot_id" --arg snapshot_sha256 "$snapshot_sha" --arg source_build "$source_build" --arg old_world_sha "$old_world_sha" \
    --arg source_mariadb_version "$source_mariadb" --argjson source_server_version "$source_server_version" \
    --argjson source_bots_version "$source_bots_version" --argjson source_custom_version "$source_custom_version" \
    --arg target_database "$target_db" --arg candidate_versions "$candidate_versions" --arg seed_sha "$seed_sha" \
    --arg upgraded_assert_sha "$upgraded_assert_sha" --arg restored_assert_sha "$restored_assert_sha" \
    --argjson scenarios "$scenarios_json" --arg completed_at "$(date -u +%Y-%m-%dT%H:%M:%SZ)" --argjson restore_ms "$restore_ms" \
    '{schema_version:1,status:$status,failure_step:(if $failure_step=="" then null else $failure_step end),candidate_commit:$candidate_commit,snapshot:{id:$snapshot_id,sha256:$snapshot_sha256},source:{build:$source_build,world_binary_sha256:$old_world_sha,mariadb_version:$source_mariadb,database_versions:{server:$source_server_version,bots:$source_bots_version,custom:$source_custom_version}},resulting_database_versions:(if $candidate_versions=="" then null else ($candidate_versions|split(":")|map(tonumber)|{server:.[0],bots:.[1],custom:.[2]}) end),fixtures:{seed_sha256:$seed_sha,upgraded_assert_sha256:$upgraded_assert_sha,restored_assert_sha256:$restored_assert_sha},candidate_scenarios:$scenarios,target:{class:"isolated-disposable",database:$target_database},restore_elapsed_ms:$restore_ms,completed_at:$completed_at}' >"$result"
}
cleanup() {
  local main_status=$? cleanup_status=0 result_status=0
  trap - EXIT
  set +e
  cd "$stack_dir"
  if [[ "$user_created" -eq 1 ]]; then
    root_sql "DROP USER IF EXISTS '$target_user'@'%'" >>"$log" 2>&1 || cleanup_status=1
  fi
  if [[ "$database_created" -eq 1 ]]; then
    root_sql "DROP DATABASE IF EXISTS \`$target_db\`" >>"$log" 2>&1 || cleanup_status=1
  fi
  if [[ "$cleanup_status" -ne 0 ]]; then
    status=failed
    failure_step=cleanup
  fi
  write_result || result_status=$?
  printf 'Migration rehearsal evidence: %s\n' "$evidence_dir"
  if [[ "$cleanup_status" -ne 0 || "$result_status" -ne 0 ]]; then
    exit 1
  fi
  exit "$main_status"
}
trap cleanup EXIT

cd "$stack_dir"
"${compose[@]}" up -d --no-recreate mariadb >>"$log" 2>&1
shared_db="$("${compose[@]}" exec -T mariadb bash -lc 'printf %s "$MYSQL_DATABASE"')"
[[ -n "$shared_db" && "$target_db" != "$shared_db" ]] || { printf 'error: isolated database unexpectedly matches the shared validation database\n' >&2; exit 1; }
actual_mariadb="$("${compose[@]}" exec -T mariadb mariadb --version)"
[[ "$actual_mariadb" == *"$source_mariadb"* ]] || { printf 'error: MariaDB version does not match snapshot metadata (expected %s)\n' "$source_mariadb" >&2; exit 1; }
root_sql "CREATE DATABASE \`$target_db\`" >>"$log" 2>&1
database_created=1
root_sql "CREATE USER '$target_user'@'%' IDENTIFIED BY '$target_password'" >>"$log" 2>&1
user_created=1
root_sql "GRANT ALL PRIVILEGES ON \`$target_db\`.* TO '$target_user'@'%'" >>"$log" 2>&1

snapshot_stream() {
  case "$snapshot" in
    *.gz) gzip -dc -- "$snapshot" ;;
    *.zst) zstd -dc -- "$snapshot" ;;
    *.sql) cat -- "$snapshot" ;;
    *) printf 'error: snapshot must end in .sql, .sql.gz, or .sql.zst\n' >&2; return 2 ;;
  esac
}
if snapshot_stream | grep -Eiq '(^|[^A-Za-z_])(USE[[:space:]]+`?[A-Za-z0-9_]+`?[[:space:]]*;|CREATE[[:space:]]+(DATABASE|SCHEMA)|DROP[[:space:]]+(DATABASE|SCHEMA))'; then
  printf 'error: snapshot contains database-selection DDL and is unsafe for an isolated target\n' >&2
  exit 1
fi
import_snapshot() {
  snapshot_stream | target_mysql
}
query_file_expect_ok() {
  local path="$1" output
  output="$(target_mysql <"$path")"
  [[ "$output" == ok ]] || { printf 'error: assertion %s must return exactly one scalar value: ok\n' "$(basename "$path")" >&2; return 1; }
}
run_candidate() {
  local mode="$1"
  "${compose[@]}" run --rm --no-deps -T \
    -e "MIGRATION_TARGET_DB=$target_db" -e "MIGRATION_TARGET_USER=$target_user" \
    -e "MIGRATION_TARGET_PASSWORD=$target_password" -e "MIGRATION_SCENARIOS_JSON=$scenarios_json" \
    --entrypoint bash eqemu-server -lc '
set -euo pipefail
runtime=/tmp/migration-rehearsal-runtime
~/code/scripts/lib/prepare-zone-cli-runtime.sh "$runtime"
jq --arg db "$MIGRATION_TARGET_DB" --arg user "$MIGRATION_TARGET_USER" --arg password "$MIGRATION_TARGET_PASSWORD" \
  "def isolated: .host = \"mariadb\" | .port = \"3306\" | .db = \$db | .username = \$user | .password = \$password; .server.database |= isolated | .server.qsdatabase |= isolated | .server.content_database |= isolated" \
  "$runtime/eqemu_config.json" >"$runtime/config.tmp"
mv "$runtime/config.tmp" "$runtime/eqemu_config.json"
unset MIGRATION_TARGET_PASSWORD
cd "$runtime"
~/code/build/bin/world database:updates --skip-backup --force
if [[ "'"$mode"'" == scenarios ]]; then
  while IFS= read -r command; do
    bash -lc "$command"
  done < <(jq -r ".[]" <<<"$MIGRATION_SCENARIOS_JSON")
fi' >>"$log" 2>&1
}

failure_step=restore_snapshot
import_snapshot >>"$log" 2>&1
snapshot_versions="$(target_query 'SELECT CONCAT(version,CHAR(58),bots_version,CHAR(58),custom_version) FROM db_version LIMIT 1')"
expected_versions="$source_server_version:$source_bots_version:$source_custom_version"
[[ "$snapshot_versions" == "$expected_versions" ]] || { printf 'error: restored database versions %s do not match manifest %s\n' "$snapshot_versions" "$expected_versions" >&2; exit 1; }
failure_step=seed_old_format
target_mysql <"$seed_sql" >>"$log" 2>&1
failure_step=candidate_update
run_candidate scenarios
failure_step=upgraded_assertions
query_file_expect_ok "$upgraded_assert_sql"
first_state="$(target_query 'SELECT CONCAT(version,CHAR(58),bots_version,CHAR(58),custom_version) FROM db_version LIMIT 1; SELECT SHA2(GROUP_CONCAT(CONCAT(table_name,CHAR(58),column_name,CHAR(58),column_type,CHAR(58),is_nullable) ORDER BY table_name,ordinal_position SEPARATOR CHAR(10)),256) FROM information_schema.columns WHERE table_schema=DATABASE()')"
candidate_versions="${first_state%%$'\n'*}"
[[ "$candidate_versions" =~ ^[0-9]+:[0-9]+:[0-9]+$ ]] || { printf 'error: Candidate updater left invalid database versions\n' >&2; exit 1; }
failure_step=idempotent_update
run_candidate no-scenarios
second_state="$(target_query 'SELECT CONCAT(version,CHAR(58),bots_version,CHAR(58),custom_version) FROM db_version LIMIT 1; SELECT SHA2(GROUP_CONCAT(CONCAT(table_name,CHAR(58),column_name,CHAR(58),column_type,CHAR(58),is_nullable) ORDER BY table_name,ordinal_position SEPARATOR CHAR(10)),256) FROM information_schema.columns WHERE table_schema=DATABASE()')"
[[ "$first_state" == "$second_state" ]] || { printf 'error: second updater run changed database version or schema\n' >&2; exit 1; }

failure_step=rollback_restore
start_ms="$(date +%s%3N)"
root_sql "DROP DATABASE \`$target_db\`; CREATE DATABASE \`$target_db\`" >>"$log" 2>&1
import_snapshot >>"$log" 2>&1
restore_ms=$(( $(date +%s%3N) - start_ms ))
failure_step=restored_assertions
query_file_expect_ok "$restored_assert_sql"
failure_step=old_build_recovery
"${compose[@]}" run --rm --no-deps -T \
  -e "MIGRATION_TARGET_DB=$target_db" -e "MIGRATION_TARGET_USER=$target_user" \
  -e "MIGRATION_TARGET_PASSWORD=$target_password" -e "OLD_WORLD=$old_world_path" -e "OLD_WORLD_SHA=$old_world_sha" \
  --entrypoint bash eqemu-server -lc '
set -euo pipefail
[[ -x "$OLD_WORLD" ]] || { printf "error: matching old world binary is unavailable: %s\n" "$OLD_WORLD" >&2; exit 125; }
[[ "$(sha256sum "$OLD_WORLD" | awk "{print \$1}")" == "$OLD_WORLD_SHA" ]] || { printf "error: old world binary checksum mismatch\n" >&2; exit 1; }
runtime=/tmp/migration-old-build-runtime
~/code/scripts/lib/prepare-zone-cli-runtime.sh "$runtime"
jq --arg db "$MIGRATION_TARGET_DB" --arg user "$MIGRATION_TARGET_USER" --arg password "$MIGRATION_TARGET_PASSWORD" \
  "def isolated: .host = \"mariadb\" | .port = \"3306\" | .db = \$db | .username = \$user | .password = \$password; .server.database |= isolated | .server.qsdatabase |= isolated | .server.content_database |= isolated" \
  "$runtime/eqemu_config.json" >"$runtime/config.tmp"
mv "$runtime/config.tmp" "$runtime/eqemu_config.json"
unset MIGRATION_TARGET_PASSWORD
cd "$runtime"
"$OLD_WORLD" database:version' >>"$log" 2>&1

failure_step=
status=passed
printf 'Migration rehearsal passed for Candidate %s; restore took %sms.\n' "$candidate_commit" "$restore_ms"

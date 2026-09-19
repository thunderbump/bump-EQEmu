#!/usr/bin/env bash
set -euo pipefail

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
repo_root="$(cd "$script_dir/.." && pwd)"
source "$script_dir/lib/akkstack-routing.sh"

usage() {
  cat <<'EOF'
Usage: scripts/rehearse-database-migration.sh [--stack validation] [--dry-run]

Rehearses the Candidate updater and rollback in a disposable MariaDB container,
volume and internal network. The validation stack supplies read-only test assets. MIGRATION_REHEARSAL_MANIFEST must name a JSON
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
manifest="${MIGRATION_REHEARSAL_MANIFEST:-}"
evidence_dir="${MIGRATION_REHEARSAL_EVIDENCE_DIR:-}"
if [[ "$AKKSTACK_DRY_RUN" -eq 1 ]]; then
  printf '%s\n' 'would create a disposable database container, volume and internal network; restore the snapshot, run updater and recovery assertions, record restore cost, and remove only this run resources'
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
fixture_preparer_commit="$(jq -er '.source.fixture_preparer_commit | select(type == "string" and test("^[0-9a-f]{40}$"))' "$manifest" 2>/dev/null)" || { printf 'error: source.fixture_preparer_commit must be the exact 40-character Candidate commit\n' >&2; exit 2; }
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
[[ "$old_world_path" == /* ]] || { printf 'error: old_build.world_binary_container_path must be an absolute container path\n' >&2; exit 2; }
case "/$old_world_path/" in
  */./*|*/../*) printf 'error: old world path may not contain . or .. components\n' >&2; exit 2 ;;
esac
old_build_host_dir="$(resolve_member "$(required_string '.old_build.host_directory' old_build.host_directory)")"
[[ -d "$old_build_host_dir" && "$old_world_path" == /opt/eqemu-old/* ]] || { printf 'error: old build must be mounted beneath /opt/eqemu-old\n' >&2; exit 2; }
build_identity_attested="$(jq -r '.source.build_identity_attested // false | if type == "boolean" then tostring else error("not a boolean") end' "$manifest" 2>/dev/null)" || { printf 'error: source.build_identity_attested must be boolean\n' >&2; exit 2; }
[[ "$source_mariadb" =~ ^[0-9]+([.][0-9]+){1,3}$ ]] || { printf 'error: source.mariadb_version must be a numeric dotted version\n' >&2; exit 2; }
[[ "$snapshot_sha" =~ ^[0-9a-f]{64}$ && "$old_world_sha" =~ ^[0-9a-f]{64}$ ]] || { printf 'error: manifest checksums must be lowercase SHA-256 values\n' >&2; exit 2; }
actual_snapshot_sha="$(sha256sum "$snapshot" | awk '{print $1}')"
[[ "$actual_snapshot_sha" == "$snapshot_sha" ]] || { printf 'error: snapshot checksum mismatch for %s\n' "$snapshot_id" >&2; exit 1; }

candidate_commit="$(git -C "$repo_root" rev-parse HEAD)"
[[ "$fixture_preparer_commit" == "$candidate_commit" ]] || {
  printf 'error: fixture preparer commit %s does not match Candidate %s; regenerate the fixture\n' \
    "$fixture_preparer_commit" "$candidate_commit" >&2
  exit 125
}
candidate_worktree_clean=false
if git -C "$repo_root" diff --quiet --ignore-submodules -- &&
   git -C "$repo_root" diff --cached --quiet --ignore-submodules --; then
  candidate_worktree_clean=true
fi
candidate_world_sha=unavailable
candidate_zone_sha=unavailable
if [[ -f "$repo_root/build/bin/world" ]]; then
  candidate_world_sha="$(sha256sum "$repo_root/build/bin/world" | awk '{print $1}')"
fi
if [[ -f "$repo_root/build/bin/zone" ]]; then
  candidate_zone_sha="$(sha256sum "$repo_root/build/bin/zone" | awk '{print $1}')"
fi
run_token="eqemu-rehearsal-$(cat /proc/sys/kernel/random/uuid)"
target_db=peq
target_password="$(cat /proc/sys/kernel/random/uuid)"
db_image="mariadb:$source_mariadb"
runtime_image=eqemulator/eqemu-server:v16-dev
db_image_id=unresolved
runtime_image_id=unresolved
if [[ -z "$evidence_dir" ]]; then evidence_dir="$(mktemp -d "${TMPDIR:-/tmp}/eqemu-migration-evidence.XXXXXX")"; fi
if [[ -e "$evidence_dir" && ! -d "$evidence_dir" ]]; then printf 'error: evidence destination is not a directory\n' >&2; exit 2; fi
umask 077
mkdir -p "$evidence_dir"
evidence_dir="$(realpath "$evidence_dir")"
result="$evidence_dir/result.json"
log="$evidence_dir/rehearsal.log"
db_container="$run_token-db"
runtime_container="$run_token-runtime"
network="$run_token"
volume="$run_token-data"
owner_label="org.eqemu.rehearsal=$run_token"
worker_label=()
worker_timeout_args=()
if [[ -n "${VALIDATION_WORKER_LIFETIME_TOKEN:-}" ]]; then
  worker_timeout_args=(--foreground)
  worker_label=(--label "org.eqemu.validation=$VALIDATION_WORKER_LIFETIME_TOKEN")
fi

network_created=0
volume_created=0
status=failed
failure_step=setup
assertion_result=""
restore_ms=null
candidate_versions=""
snapshot_data_state=""
shared_dir=""
plugins_dir=""
lua_modules_dir=""

root_sql() { printf '%s\n' "$1" | target_mysql; }
target_mysql() {
  docker exec -i "$db_container" sh -c 'MYSQL_PWD="$MYSQL_ROOT_PASSWORD" exec mariadb -uroot -N -B peq'
}
target_query() { printf '%s\n' "$1" | target_mysql; }
# Hash the complete schema stream, including indexes/constraints, without SQL aggregation limits.
schema_fingerprint() {
  docker exec "$db_container" sh -c 'MYSQL_PWD="$MYSQL_ROOT_PASSWORD" exec mariadb-dump -uroot --no-data --skip-comments --skip-add-drop-table peq' | sha256sum | cut -d ' ' -f1
}
representative_data_state() {
  target_query 'SELECT CONCAT((SELECT COUNT(*) FROM character_data),CHAR(58),(SELECT COALESCE(BIT_XOR(CRC32(CONCAT(id,CHAR(58),account_id))),0) FROM character_data),CHAR(58),(SELECT COALESCE(SUM(copper + 10 * silver + 100 * gold + 1000 * platinum + copper_bank + 10 * silver_bank + 100 * gold_bank + 1000 * platinum_bank + copper_cursor + 10 * silver_cursor + 100 * gold_cursor + 1000 * platinum_cursor),0) FROM character_currency),CHAR(58),(SELECT COUNT(*) FROM bot_data),CHAR(58),(SELECT COALESCE(SUM(owner_id),0) FROM bot_data),CHAR(58),(SELECT COALESCE(BIT_XOR(CRC32(CONCAT(bot_id,CHAR(58),owner_id))),0) FROM bot_data))'
}
select_runtime_dir() {
  local label="$1"
  shift
  local candidate
  for candidate in "$@"; do
    if [[ -d "$candidate" ]]; then
      realpath -e "$candidate"
      return
    fi
  done
  printf 'error: migration rehearsal requires the validation stack %s directory; checked:' "$label" >&2
  printf ' %s' "$@" >&2
  printf '\n' >&2
  return 125
}
run_runtime() {
  local mode="$1"
  local -a deadline=()
  if [[ "$mode" == scenarios ]]; then
    # Bound both the scenario process and the Docker client. The EXIT trap uses
    # the ownership label to force-remove the container if timeout's TERM/KILL
    # cannot complete Docker's five-second stop sequence.
    deadline=(timeout "${worker_timeout_args[@]}" --signal=TERM --kill-after=10s "${MIGRATION_REHEARSAL_SCENARIO_TIMEOUT_SECONDS:-300}s")
  fi
  "${deadline[@]}" docker run --rm --name "$runtime_container" --label "$owner_label" "${worker_label[@]}" \
    --network "$network" --read-only --user 0:0 --init --ulimit core=0 --stop-timeout 5 \
    --tmpfs /tmp:rw,nosuid,size=256m --tmpfs /runtime:rw,nosuid,size=1g \
    --mount "type=bind,src=$repo_root,dst=/home/eqemu/code,readonly" \
    --mount "type=bind,src=$old_build_host_dir,dst=/opt/eqemu-old,readonly" \
    --mount "type=bind,src=$shared_dir,dst=/inputs/shared,readonly" \
    --mount "type=bind,src=$plugins_dir,dst=/inputs/plugins,readonly" \
    --mount "type=bind,src=$lua_modules_dir,dst=/inputs/lua_modules,readonly" \
    -e "REHEARSAL_PASSWORD=$target_password" -e "MIGRATION_SCENARIOS_JSON=$scenarios_json" \
    -e "OLD_WORLD=$old_world_path" -e "OLD_WORLD_SHA=$old_world_sha" \
    --entrypoint bash "$runtime_image" /home/eqemu/code/scripts/lib/migration-runtime.sh "$mode"
}

write_result() {
  jq -n --arg status "$status" --arg failure_step "$failure_step" --arg assertion_result "$assertion_result" --arg candidate_commit "$candidate_commit" \
    --arg run_id "$run_token" --arg database_image "$db_image" --arg runtime_image "$runtime_image" \
    --arg database_image_id "$db_image_id" --arg runtime_image_id "$runtime_image_id" \
    --arg candidate_world_sha "$candidate_world_sha" --arg candidate_zone_sha "$candidate_zone_sha" \
    --argjson candidate_worktree_clean "$candidate_worktree_clean" \
    --arg snapshot_id "$snapshot_id" --arg snapshot_sha256 "$snapshot_sha" --arg source_build "$source_build" --arg old_world_sha "$old_world_sha" \
    --arg fixture_preparer_commit "$fixture_preparer_commit" --arg source_mariadb_version "$source_mariadb" --argjson source_server_version "$source_server_version" \
    --argjson source_bots_version "$source_bots_version" --argjson source_custom_version "$source_custom_version" \
    --argjson build_identity_attested "$build_identity_attested" \
    --arg target_database "$target_db" --arg candidate_versions "$candidate_versions" --arg seed_sha "$seed_sha" \
    --arg upgraded_assert_sha "$upgraded_assert_sha" --arg restored_assert_sha "$restored_assert_sha" \
    --argjson scenarios "$scenarios_json" --arg completed_at "$(date -u +%Y-%m-%dT%H:%M:%SZ)" --argjson restore_ms "$restore_ms" \
    '{schema_version:1,assertion_result:$assertion_result,run_id:$run_id,images:{database:$database_image,runtime:$runtime_image,database_id:$database_image_id,runtime_id:$runtime_image_id},status:$status,failure_step:(if $failure_step=="" then null else $failure_step end),candidate_commit:$candidate_commit,candidate:{source_commit:$candidate_commit,worktree_clean:$candidate_worktree_clean,artifact_identity_attested:false,world_sha256:$candidate_world_sha,zone_sha256:$candidate_zone_sha},snapshot:{id:$snapshot_id,sha256:$snapshot_sha256},source:{build:$source_build,build_identity_attested:$build_identity_attested,fixture_preparer_commit:$fixture_preparer_commit,world_binary_sha256:$old_world_sha,mariadb_version:$source_mariadb_version,database_versions:{server:$source_server_version,bots:$source_bots_version,custom:$source_custom_version}},resulting_database_versions:(if $candidate_versions=="" then null else ($candidate_versions|split(":")|map(tonumber)|{server:.[0],bots:.[1],custom:.[2]}) end),fixtures:{seed_sha256:$seed_sha,upgraded_assert_sha256:$upgraded_assert_sha,restored_assert_sha256:$restored_assert_sha},candidate_scenarios:$scenarios,target:{class:"isolated-disposable",database:$target_database},restore_elapsed_ms:$restore_ms,completed_at:$completed_at}' >"$result"
}
cleanup() {
  local main_status=$? cleanup_status=0 result_status=0
  trap - EXIT
  set +e
  # Names contain an unpredictable run UUID; label checks prevent deleting foreign resources.
  # Before resource creation there is nothing to inspect; this keeps prerequisite
  # failures free of Docker operations as well as database restoration work.
  if [[ "$network_created" == 1 || "$volume_created" == 1 ]]; then
    for container in "$runtime_container" "$db_container"; do
      if [[ "$(docker inspect --format '{{ index .Config.Labels "org.eqemu.rehearsal" }}' "$container" 2>/dev/null)" == "$run_token" ]]; then
        docker rm -f "$container" >>"$log" 2>&1 || cleanup_status=1
      fi
    done
  fi
  if [[ "$volume_created" == 1 ]]; then docker volume rm "$volume" >>"$log" 2>&1 || cleanup_status=1; fi
  if [[ "$network_created" == 1 ]]; then docker network rm "$network" >>"$log" 2>&1 || cleanup_status=1; fi
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
trap 'exit 143' TERM
trap 'exit 130' INT

# Resolve the same minimal runtime inputs used by the zone CLI profiles before
# pulling images or restoring the snapshot. Database updates and the selected
# actor command do not boot a zone, so map and full quest trees are not inputs.
failure_step=prerequisites
command -v docker >/dev/null || { printf 'error: docker is required\n' >&2; exit 125; }
command -v timeout >/dev/null || { printf 'error: timeout is required\n' >&2; exit 125; }
[[ "${MIGRATION_REHEARSAL_SCENARIO_TIMEOUT_SECONDS:-300}" =~ ^[1-9][0-9]*$ ]] || { printf 'error: MIGRATION_REHEARSAL_SCENARIO_TIMEOUT_SECONDS must be a positive integer\n' >&2; exit 2; }
case "$snapshot" in
  *.gz) command -v gzip >/dev/null || { printf 'error: gzip is required for the selected snapshot\n' >&2; exit 125; } ;;
  *.zst) command -v zstd >/dev/null || { printf 'error: zstd is required for the selected snapshot\n' >&2; exit 125; } ;;
esac
shared_dir="$(select_runtime_dir shared-memory "$stack_dir/server/shared")" || exit $?
plugins_dir="$(select_runtime_dir plugins "$stack_dir/server/quests/plugins" "$stack_dir/server/plugins")" || exit $?
lua_modules_dir="$(select_runtime_dir lua-modules "$stack_dir/server/quests/lua_modules" "$stack_dir/server/lua_modules")" || exit $?
failure_step=setup

# Pull/cache images on the host; containers themselves have no external network.
for image in "$db_image" "$runtime_image"; do
  docker image inspect "$image" >/dev/null 2>&1 || docker pull "$image" >>"$log" 2>&1
done
db_image_id="$(docker image inspect --format '{{.Id}}' "$db_image")"
runtime_image_id="$(docker image inspect --format '{{.Id}}' "$runtime_image")"
[[ -z "${VALIDATION_WORKER_DOCKER_MARKER:-}" ]] || : >"$VALIDATION_WORKER_DOCKER_MARKER"
docker network create --internal --label "$owner_label" "${worker_label[@]}" "$network" >>"$log"
network_created=1
docker volume create --label "$owner_label" "${worker_label[@]}" "$volume" >>"$log"
volume_created=1
docker run -d --name "$db_container" --label "$owner_label" "${worker_label[@]}" --network "$network" --network-alias mariadb \
  --mount "type=volume,src=$volume,dst=/var/lib/mysql" \
  -e "MYSQL_ROOT_PASSWORD=$target_password" -e MYSQL_ROOT_HOST=% -e MYSQL_DATABASE=peq "$db_image" >>"$log" 2>&1
ready=0
for attempt in {1..90}; do
  if target_query 'SELECT 1' >/dev/null 2>&1; then ready=1; break; fi
  sleep 1
done
[[ "$ready" == 1 ]] || { printf 'error: disposable database did not become ready\n' >&2; exit 1; }
actual_mariadb="$(target_query 'SELECT VERSION()')"
# A major/minor image tag intentionally floats within that release line; a
# manifest with a patch (or fuller) version requires that exact version token.
if [[ "$source_mariadb" =~ ^[0-9]+[.][0-9]+$ ]]; then
  [[ "$actual_mariadb" == "$source_mariadb".* ]] || { printf 'error: disposable MariaDB version differs from capture\n' >&2; exit 1; }
else
  [[ "$actual_mariadb" == "$source_mariadb"-* ]] || { printf 'error: disposable MariaDB version differs from capture\n' >&2; exit 1; }
fi

snapshot_stream() {
  case "$snapshot" in
    *.gz) gzip -dc -- "$snapshot" ;;
    *.zst) zstd -dc -- "$snapshot" ;;
    *.sql) cat -- "$snapshot" ;;
    *) printf 'error: snapshot must end in .sql, .sql.gz, or .sql.zst\n' >&2; return 2 ;;
  esac
}
import_snapshot() {
  snapshot_stream | target_mysql
}
query_file_expect_ok() {
  local path="$1" output
  output="$(target_mysql <"$path")"
  if [[ "$output" != ok ]]; then
    assertion_result="${output:0:1024}"
    # Assertion files contain only bounded, repository-authored labels. Include
    # their scalar result so failures are actionable without exposing row data.
    printf 'error: assertion %s failed (%s); expected exactly one scalar value: ok\n' \
      "$(basename "$path")" "${output:-no output}" >&2
    return 1
  fi
}
run_candidate() { run_runtime "$1" >>"$log" 2>&1; }

failure_step=restore_snapshot
import_snapshot >>"$log" 2>&1
snapshot_versions="$(target_query 'SELECT CONCAT(version,CHAR(58),bots_version,CHAR(58),custom_version) FROM db_version LIMIT 1')"
expected_versions="$source_server_version:$source_bots_version:$source_custom_version"
[[ "$snapshot_versions" == "$expected_versions" ]] || { printf 'error: restored database versions %s do not match manifest %s\n' "$snapshot_versions" "$expected_versions" >&2; exit 1; }
snapshot_data_state="$(representative_data_state)"
failure_step=seed_old_format
target_mysql <"$seed_sql" >>"$log" 2>&1
failure_step=candidate_update
run_candidate update
failure_step=candidate_scenarios
run_candidate scenarios
failure_step=upgraded_assertions
query_file_expect_ok "$upgraded_assert_sql"
first_state="$(schema_fingerprint)"
candidate_versions="$(target_query 'SELECT CONCAT(version,CHAR(58),bots_version,CHAR(58),custom_version) FROM db_version LIMIT 1')"
[[ "$candidate_versions" =~ ^[0-9]+:[0-9]+:[0-9]+$ ]] || { printf 'error: invalid Candidate versions\n' >&2; exit 1; }
# This fixture requires the Candidate's full server/bot/custom schema targets.
# Read the exact checkout rather than accepting any stable numeric version.
failure_step=candidate_target_versions
expected_candidate_versions="$(python3 "$script_dir/lib/migration-target-versions.py" "$repo_root/common/version.h")"
[[ "$candidate_versions" == "$expected_candidate_versions" ]] || {
  printf 'error: upgraded versions %s do not match Candidate targets %s\n' "$candidate_versions" "$expected_candidate_versions" >&2
  exit 1
}
failure_step=idempotent_update
run_candidate update
second_state="$(schema_fingerprint)"
second_candidate_versions="$(target_query 'SELECT CONCAT(version,CHAR(58),bots_version,CHAR(58),custom_version) FROM db_version LIMIT 1')"
[[ "$first_state" == "$second_state" && "$candidate_versions" == "$second_candidate_versions" ]] || {
  printf 'error: second updater run changed database version or schema\n' >&2
  exit 1
}
failure_step=idempotent_data_assertions
query_file_expect_ok "$upgraded_assert_sql"

failure_step=rollback_restore
start_ms="$(date +%s%3N)"
root_sql "DROP DATABASE \`$target_db\`; CREATE DATABASE \`$target_db\`" >>"$log" 2>&1
import_snapshot >>"$log" 2>&1
restore_ms=$(( $(date +%s%3N) - start_ms ))
failure_step=restored_assertions
query_file_expect_ok "$restored_assert_sql"
restored_data_state="$(representative_data_state)"
[[ "$restored_data_state" == "$snapshot_data_state" ]] || { printf 'error: rollback restore changed representative ownership or currency state\n' >&2; exit 1; }
failure_step=old_build_recovery
run_runtime recovery >>"$log" 2>&1

failure_step=post_startup_restored_assertions
query_file_expect_ok "$restored_assert_sql"
post_startup_data_state="$(representative_data_state)"
[[ "$post_startup_data_state" == "$snapshot_data_state" ]] || { printf 'error: archived world startup changed representative ownership or currency state\n' >&2; exit 1; }
failure_step=
status=passed
printf 'Migration rehearsal passed for Candidate %s; restore took %sms.\n' "$candidate_commit" "$restore_ms"

#!/usr/bin/env bash
set -euo pipefail

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
repo_root="$(cd "$script_dir/.." && pwd)"
worker_home="${VALIDATION_WORKER_HOME:-$repo_root/.validation-worker}"
lock_name="validation-slot"
RUN_REQUEST_CLEANUP_ACTIVE=0
RUN_REQUEST_CLEANUP_EVIDENCE_DIR=
RUN_REQUEST_CLEANUP_LOCK_DIR=
RUN_REQUEST_CLEANUP_STACK_LOCK_DIR=
RUN_REQUEST_CHILD_PID=
RUN_REQUEST_CHILD_PGID=
RUN_REQUEST_CHECKOUT_DIR=
RUN_REQUEST_HEAD_COMMIT=
RUN_REQUEST_STACK_PATH_SOURCE=
RUN_REQUEST_AFK_LOG_PREFIX=logs
RUN_REQUEST_AFK_ACTIVE_INDEX=-1
RUN_REQUEST_DEADLINE_NS=
RUN_REQUEST_COMMAND_DEADLINE_NS=
RUN_REQUEST_ACTOR_CLEANUP_REQUIRED=0
termination_grace_seconds="${VALIDATION_WORKER_TERMINATION_GRACE_SECONDS:-5}"
actor_cleanup_reserve_seconds="${VALIDATION_WORKER_ACTOR_CLEANUP_RESERVE_SECONDS:-120}"
if [[ ! "$termination_grace_seconds" =~ ^[0-9]+$ || "$termination_grace_seconds" -eq 0 ]]; then
  printf 'error: VALIDATION_WORKER_TERMINATION_GRACE_SECONDS must be a positive integer\n' >&2
  exit 2
fi
if [[ ! "$actor_cleanup_reserve_seconds" =~ ^[0-9]+$ || "$actor_cleanup_reserve_seconds" -eq 0 ]]; then
  printf 'error: VALIDATION_WORKER_ACTOR_CLEANUP_RESERVE_SECONDS must be a positive integer\n' >&2
  exit 2
fi

usage() {
  cat <<'USAGE'
Usage: scripts/validation-worker.sh <command> [options]

Commands:
  profiles --json        List portable validation worker profiles.
  run --request <path>   Execute a validation worker request JSON.
  self-test              Run validation worker shell self-tests.
  -h, --help             Show this help.

Request JSON fields:
  project            Required project name, must be "bump-eqemu" or "bump-EQEmu".
  repo               Fetch source as a string URL/path, or repo.url/repo.path in an object.
  ref                Required for fetch requests. May also be repo.ref.
  commit             Optional expected commit SHA. May also be repo.commit.
  checkout.path      Optional local-checkout request path. Also accepts local_checkout.path,
                     target_worktree_checkout, target_checkout_path, or repo.path without a ref.
  profile            Required validation profile: preflight, tier1, tier2-readonly,
                     tier3-harness, actor-queue-tier3, tier1-tier3-harness, or safe.
  run_id             Required stable run identifier used for worker-owned checkout storage.
  evidence_dir       Required directory where request.json, result.json, and logs are written.
  timeout_seconds    Optional validation timeout in seconds. Defaults to 3600.
  lock_wait_seconds  Optional bounded wait for the exclusive worker-local validation slot. Defaults to 0.
  stack.role         Optional stack role. If present, must be "validation".
  stack.path         Optional validation AkkStack path to bind while validation runs.

Fetch requests clone into VALIDATION_WORKER_HOME (default: .validation-worker),
run git submodule update --init --recursive, verify any expected commit, and
delegate to scripts/validate.sh in the fetched checkout. Local-checkout requests
skip fetch, require an existing checkout, and verify submodules are already
initialized and pinned to recorded commits instead of mutating the checkout.
Set VALIDATION_WORKER_VALIDATE_DRY_RUN=1 to delegate with --dry-run for local
contract tests. If stack.path is omitted, AKKSTACK_DIR remains a path override.
VALIDATION_WORKER_ACTOR_CLEANUP_RESERVE_SECONDS controls the portion of the
shared timeout reserved for actor database cleanup (default: 120 seconds).
USAGE
}

json_get() {
  local path="$1" file="$2"
  jq -er "$path // empty" "$file" 2>/dev/null || true
}

now_utc() {
  date -u +%Y-%m-%dT%H:%M:%SZ
}

VALIDATION_PROFILES='[
  {"name":"preflight","portable":true,"description":"Verify the validation stack contract only.","mutation_classification":"read-only","timeout_guidance":"Short. Roughly 5-10 minutes is usually enough.","lock_guidance":"Takes the exclusive worker slot. Also takes the stack binding lock when stack.path is used."},
  {"name":"safe","portable":true,"description":"Run preflight, Tier 1, and read-mostly Tier 2 checks.","mutation_classification":"read-mostly","timeout_guidance":"Medium. Roughly 15-30 minutes depending on build speed.","lock_guidance":"Takes the exclusive worker slot for the whole run. Also takes the stack binding lock when stack.path is used."},
  {"name":"tier1","portable":false},
  {"name":"tier2-readonly","portable":false},
  {"name":"tier3-harness","portable":true,"description":"Run the canonical Tier 3 Zone Harness smoke.","mutation_classification":"read-mostly/runtime-fixture","timeout_guidance":"Medium. Roughly 10-20 minutes including harness startup.","lock_guidance":"Takes the exclusive worker slot for the whole run. Also takes the stack binding lock when stack.path is used."},
  {"name":"actor-queue-tier3","portable":true,"steps":["tier1","actor-queue-tier3"],"description":"Build Tier 1, then run the durable Autonomous Actor queue executor integration proof.","mutation_classification":"database-mutating/runtime-fixture","timeout_guidance":"Longer. Give one shared budget that covers Tier 1 and the actor queue runtime.","lock_guidance":"Takes the exclusive worker and stack binding locks; mutates the validation database and cleans scenario-owned rows."},
  {"name":"tier1-tier3-harness","portable":true,"steps":["tier1","tier3-harness","actor-queue-tier3"],"description":"Run Tier 1 once, then the canonical Tier 3 harness and durable actor queue proof under one timeout budget.","mutation_classification":"database-mutating/runtime-fixture","timeout_guidance":"Longer. Give one shared budget that covers Tier 1 and both required runtime scenarios.","lock_guidance":"Takes the exclusive worker and stack binding locks for the full combined run; the actor scenario cleans its database fixture."}
]'
# Every required runtime proof has a stable scenario identifier, a dispatch profile,
# an exact completion marker emitted by the scenario itself, and its own log. Add
# future actor proofs here; the worker requires the marker after a zero exit so an
# accidentally skipped or no-op scenario cannot produce a pass. Tier 1 is a build
# step rather than a runtime scenario and therefore has no completion marker.
AFK_CHECK_PLAN='[
  {"name":"tier1-build-and-unit-tests","scenario":"tier1-build-and-unit-tests","profile":"tier1","log_file":"tier1-build-and-unit-tests.log","failure_status":"rejected","failure_message":"Tier 1 validation failed","inconclusive_message":"Tier 1 validation was inconclusive"},
  {"name":"tier3-zone-harness","scenario":"canonical-zone-harness","profile":"tier3-harness","log_file":"tier3-zone-harness.log","completion_marker":"[PASS] canonical-zone-harness","failure_status":"rejected","failure_message":"Tier 3 Zone Harness validation failed","inconclusive_message":"Tier 3 Zone Harness validation was inconclusive"},
  {"name":"actor-queue-runtime","scenario":"actor-events-runtime","profile":"actor-queue-tier3","log_file":"actor-queue-runtime.log","completion_marker":"[PASS] actor-events-runtime","failure_status":"rejected","failure_message":"Durable actor queue validation failed","inconclusive_message":"Durable actor queue validation was inconclusive"}
]'

emit_profiles_json() {
  jq '{profiles:[.[] | select(.portable) | del(.portable, .steps)]}' <<<"$VALIDATION_PROFILES"
}

ensure_log_files() {
  local evidence_dir="$1"
  mkdir -p "$evidence_dir/logs"
  : >"$evidence_dir/logs/request.log"
  : >"$evidence_dir/logs/fetch.log"
  : >"$evidence_dir/logs/lock.log"
  : >"$evidence_dir/logs/stack.log"
  : >"$evidence_dir/logs/submodule.log"
  : >"$evidence_dir/logs/validation.log"
}

write_result() {
  local evidence_dir="$1" status="$2" category="$3" exit_code="$4" message="$5" checkout_dir="${6:-}" head_commit="${7:-}"
  local stack_path_source="${8:-}"
  local result_json

  mkdir -p "$evidence_dir"
  result_json="$evidence_dir/result.json"

  jq -n \
    --arg status "$status" \
    --arg category "$category" \
    --arg message "$message" \
    --arg profile "$profile" \
    --arg checkout_dir "$checkout_dir" \
    --arg expected_commit "$commit" \
    --arg actual_checkout_commit "$head_commit" \
    --arg head_commit "$head_commit" \
    --arg evidence_dir "$evidence_dir" \
    --arg stack_role "$stack_role" \
    --arg stack_path "$stack_path" \
    --arg stack_path_source "$stack_path_source" \
    --arg request_source_type "$request_source_type" \
    --arg request_source_repo "$request_source_repo" \
    --arg request_source_ref "$request_source_ref" \
    --arg request_source_commit "$request_source_commit" \
    --arg request_source_checkout_path "$request_source_checkout_path" \
    --arg project "$project" \
    --arg run_id "$run_id" \
    --arg completed_at "$(now_utc)" \
    --argjson exit_code "$exit_code" \
    --argjson timeout_seconds "$timeout_seconds" \
    --argjson lock_wait_seconds "$lock_wait_seconds" \
    '{
      status:$status,
      category:$category,
      exit_code:$exit_code,
      message:$message,
      profile:$profile,
      checkout_dir:$checkout_dir,
      expected_commit:$expected_commit,
      actual_checkout_commit:$actual_checkout_commit,
      head_commit:$head_commit,
      evidence_dir:$evidence_dir,
      stack:{
        role:$stack_role,
        path:$stack_path,
        path_source:$stack_path_source
      },
      request_metadata:{
        project:$project,
        run_id:$run_id,
        profile:$profile,
        timeout_seconds:$timeout_seconds,
        lock_wait_seconds:$lock_wait_seconds,
        source:{
          type:$request_source_type,
          repo:$request_source_repo,
          ref:$request_source_ref,
          commit:$request_source_commit,
          checkout_path:$request_source_checkout_path
        }
      },
      completed_at:$completed_at
    }' \
    >"$result_json"

  if [[ -n "${RESULT_CHECKS_PATH:-}" && -f "$RESULT_CHECKS_PATH" ]]; then
    jq --slurpfile registered "$RESULT_CHECKS_PATH" \
      '. + {checks:$registered[0].checks}' "$result_json" >"$result_json.tmp"
    mv "$result_json.tmp" "$result_json"
  fi
  cp "$result_json" "$evidence_dir/worker-output.json"
}

write_afk_checks() {
  local path="$1" overall_status="$2" log_prefix="${3:-logs}" statuses_json
  statuses_json="$(jq -cn '$ARGS.positional' --args "${afk_check_statuses[@]}")"
  jq -n \
    --arg overall_status "$overall_status" \
    --arg log_prefix "$log_prefix" \
    --argjson plan "$AFK_CHECK_PLAN" \
    --argjson statuses "$statuses_json" \
    '{
      status:$overall_status,
      checks:[$plan | to_entries[] | {
        name:.value.name,
        scenario:.value.scenario,
        profile:.value.profile,
        status:$statuses[.key],
        completion_marker:(.value.completion_marker // null),
        candidate_commit:($ENV.AFK_CHECK_CANDIDATE_COMMIT // null),
        log_path:($log_prefix + "/" + .value.log_file)
      }]
    }' >"$path"
}

initialize_afk_check_statuses() {
  local status="${1:-not_run}" count
  count="$(jq 'length' <<<"$AFK_CHECK_PLAN")"
  afk_check_statuses=()
  while [[ "${#afk_check_statuses[@]}" -lt "$count" ]]; do
    afk_check_statuses+=("$status")
  done
}

ensure_afk_check_logs() {
  local evidence_dir="$1" truncate="${2:-0}" log_file
  mkdir -p "$evidence_dir/logs"
  while IFS= read -r log_file; do
    if [[ "$truncate" == "1" ]]; then
      : >"$evidence_dir/logs/$log_file"
    else
      : >>"$evidence_dir/logs/$log_file"
    fi
  done < <(jq -r '.[].log_file' <<<"$AFK_CHECK_PLAN")
}

write_inconclusive_afk_checks() {
  local source="$1" destination="$2"
  jq '
    (.checks | [to_entries[] | select(.value.status != "not_run") | .key] | last) as $last
    | .status = "inconclusive"
    | if $last == null then . else .checks[$last].status = "inconclusive" end
  ' "$source" >"$destination"
}

write_afk_result() {
  local evidence_dir="$1" candidate_sha="$2" status="$3" summary="$4" checks_path="$5"
  jq -n \
    --arg candidate_sha "$candidate_sha" \
    --arg status "$status" \
    --arg summary "$summary" \
    --slurpfile checks "$checks_path" \
    '{
      schema_version:1,
      candidate_sha:$candidate_sha,
      status:$status,
      summary:$summary,
      checks:$checks[0].checks
    }' >"$evidence_dir/result.json"
}

is_afk_request() {
  local request_path="$1"
  jq -e '
    type == "object"
    and (.schema_version == 1)
    and (.candidate_sha | type == "string")
    and (.evidence_dir | type == "string")
    and (.run_id | type == "string")
  ' "$request_path" >/dev/null 2>&1
}

run_afk_request() {
  local request_path="$1" candidate_sha evidence_dir afk_run_id worker_evidence worker_request checks_path stack_path
  local worker_status expected_worker_status overall_status summary result_checks_path source_repo

  candidate_sha="$(json_get '.candidate_sha | strings' "$request_path")"
  evidence_dir="$(json_get '.evidence_dir | strings' "$request_path")"
  afk_run_id="$(json_get '.run_id | strings' "$request_path")"
  if [[ ! "$candidate_sha" =~ ^[0-9a-fA-F]{40}$ ]] \
    || [[ -z "$evidence_dir" || ! -d "$evidence_dir" ]] \
    || [[ -z "$afk_run_id" ]]; then
    printf 'invalid AFK validation request\n' >&2
    return 2
  fi

  worker_evidence="$evidence_dir/worker"
  worker_request="$evidence_dir/worker-request.json"
  checks_path="$worker_evidence/afk-checks.json"
  source_repo="$HOME/Projects/bump-eqemu/bump-EQEmu"
  stack_path="$HOME/Projects/bump-eqemu/bump-akk-stack-validation"
  mkdir "$worker_evidence"
  jq -n \
    --arg project bump-eqemu \
    --arg repo "$source_repo" \
    --arg commit "$candidate_sha" \
    --arg run_id "afk-${candidate_sha:0:16}" \
    --arg evidence_dir "$worker_evidence" \
    --arg stack_path "$stack_path" \
    '{
      project:$project,
      repo:$repo,
      ref:$commit,
      commit:$commit,
      profile:"tier1-tier3-harness",
      run_id:$run_id,
      evidence_dir:$evidence_dir,
      timeout_seconds:2600,
      lock_wait_seconds:0,
      stack:{role:"validation", path:$stack_path}
    }' >"$worker_request"

  set +e
  VALIDATION_WORKER_AFK_MODE=1 \
    "$script_dir/validation-worker.sh" run --request "$worker_request"
  worker_status=$?
  set -e

  ensure_afk_check_logs "$worker_evidence"
  if [[ ! -f "$checks_path" ]]; then
    initialize_afk_check_statuses
    afk_check_statuses[0]=inconclusive
    write_afk_checks "$checks_path" inconclusive worker/logs
  fi
  overall_status="$(json_get '.status | strings' "$checks_path")"
  case "$overall_status" in
    passed) expected_worker_status=0 ;;
    rejected) expected_worker_status=1 ;;
    inconclusive) expected_worker_status=2 ;;
    *) expected_worker_status=-1 ;;
  esac
  result_checks_path="$checks_path"
  if [[ "$worker_status" -ne "$expected_worker_status" ]]; then
    overall_status=inconclusive
    summary="Required repository validation could not reach a trustworthy verdict."
    worker_status=2
    result_checks_path="$evidence_dir/inconclusive-checks.json"
    write_inconclusive_afk_checks "$checks_path" "$result_checks_path"
  else
    case "$overall_status" in
      passed) summary="Required repository validation passed." ;;
      rejected) summary="Required repository validation found a deterministic failure." ;;
      inconclusive) summary="Required repository validation could not reach a trustworthy verdict." ;;
    esac
  fi
  write_afk_result "$evidence_dir" "$candidate_sha" "$overall_status" "$summary" "$result_checks_path"
  return "$worker_status"
}

copy_request_evidence() {
  local request_path="$1" evidence_dir="$2"
  mkdir -p "$evidence_dir"
  if jq -S . "$request_path" >"$evidence_dir/request.json" 2>"$evidence_dir/logs/request.log"; then
    return 0
  fi
  cp "$request_path" "$evidence_dir/request.json" 2>/dev/null || true
  return 1
}

sanitize_run_id() {
  local run_id="$1"
  [[ "$run_id" =~ ^[A-Za-z0-9._-]+$ ]]
}

resolve_request_source() {
  local request_path="$1"
  local repo_string repo_url repo_path top_ref repo_ref top_commit repo_commit checkout_path local_checkout_path

  repo_string="$(json_get '.repo | strings' "$request_path")"
  repo_url="$(json_get '.repo.url | strings' "$request_path")"
  repo_path="$(json_get '.repo.path | strings' "$request_path")"
  top_ref="$(json_get '.ref | strings' "$request_path")"
  repo_ref="$(json_get '.repo.ref | strings' "$request_path")"
  top_commit="$(json_get '.commit | strings' "$request_path")"
  repo_commit="$(json_get '.repo.commit | strings' "$request_path")"
  checkout_path="$(json_get '.checkout.path | strings' "$request_path")"
  local_checkout_path="$(json_get '.local_checkout.path | strings' "$request_path")"
  [[ -n "$local_checkout_path" ]] || local_checkout_path="$(json_get '.target_worktree_checkout | strings' "$request_path")"
  [[ -n "$local_checkout_path" ]] || local_checkout_path="$(json_get '.target_checkout_path | strings' "$request_path")"
  [[ -n "$local_checkout_path" ]] || local_checkout_path="$(json_get '.checkout_path | strings' "$request_path")"
  [[ -n "$local_checkout_path" ]] || local_checkout_path="$checkout_path"

  if [[ -n "$top_ref" && -n "$repo_ref" && "$top_ref" != "$repo_ref" ]]; then
    printf 'conflicting ref and repo.ref\n'
    return 1
  fi
  if [[ -n "$top_commit" && -n "$repo_commit" && "$top_commit" != "$repo_commit" ]]; then
    printf 'conflicting commit and repo.commit\n'
    return 1
  fi

  ref="${top_ref:-$repo_ref}"
  commit="${top_commit:-$repo_commit}"

  if [[ -n "$repo_string" ]]; then
    request_source_type=fetch
    request_source_repo="$repo_string"
    request_source_ref="$ref"
    request_source_commit="$commit"
    request_source_checkout_path=""
  elif [[ -n "$repo_url" ]]; then
    request_source_type=fetch
    request_source_repo="$repo_url"
    request_source_ref="$ref"
    request_source_commit="$commit"
    request_source_checkout_path=""
  elif [[ -n "$repo_path" && -n "$ref" ]]; then
    request_source_type=fetch
    request_source_repo="$repo_path"
    request_source_ref="$ref"
    request_source_commit="$commit"
    request_source_checkout_path=""
  elif [[ -n "$local_checkout_path" ]]; then
    request_source_type=local-checkout
    request_source_repo=""
    request_source_ref=""
    request_source_commit="$commit"
    request_source_checkout_path="$local_checkout_path"
  elif [[ -n "$repo_path" ]]; then
    request_source_type=local-checkout
    request_source_repo=""
    request_source_ref=""
    request_source_commit="$commit"
    request_source_checkout_path="$repo_path"
  else
    printf 'missing repo/ref fetch source or local checkout path\n'
    return 1
  fi

  if [[ "$request_source_type" == "fetch" ]]; then
    repo="$request_source_repo"
    [[ -n "$request_source_ref" ]] || { printf 'missing ref\n'; return 1; }
  else
    repo=""
    if [[ -n "$ref" ]]; then
      printf 'local-checkout requests may not set ref; use commit for pinned verification\n'
      return 1
    fi
  fi
}

validate_request() {
  local request_path="$1"
  if [[ ! -f "$request_path" ]]; then
    printf 'request file not found: %s\n' "$request_path"
    return 1
  fi
  if ! jq -e type "$request_path" >/dev/null 2>&1; then
    printf 'request is not valid JSON\n'
    return 1
  fi

  project="$(json_get '.project | strings' "$request_path")"
  profile="$(json_get '.profile | strings' "$request_path")"
  run_id="$(json_get '.run_id | strings' "$request_path")"
  evidence_dir="$(json_get '.evidence_dir | strings' "$request_path")"
  timeout_seconds="$(json_get '.timeout_seconds // 3600 | numbers' "$request_path")"
  lock_wait_seconds="$(json_get '.lock_wait_seconds // 0 | numbers' "$request_path")"
  stack_role="$(json_get '.stack.role // "validation" | strings' "$request_path")"
  stack_path="$(json_get '.stack.path | strings' "$request_path")"
  repo=
  ref=
  commit=
  request_source_type=
  request_source_repo=
  request_source_ref=
  request_source_commit=
  request_source_checkout_path=

  [[ "$project" == "bump-eqemu" || "$project" == "bump-EQEmu" ]] || { printf 'invalid or missing project\n'; return 1; }
  resolve_request_source "$request_path" || return 1
  [[ -z "$commit" || "$commit" =~ ^[0-9a-fA-F]{7,40}$ ]] || { printf 'invalid commit\n'; return 1; }
  jq -e --arg profile "$profile" 'any(.[]; .name == $profile)' <<<"$VALIDATION_PROFILES" >/dev/null \
    || { printf 'invalid or missing profile\n'; return 1; }
  [[ -n "$run_id" ]] || { printf 'missing run_id\n'; return 1; }
  sanitize_run_id "$run_id" || { printf 'run_id may contain only letters, digits, dot, underscore, and dash\n'; return 1; }
  [[ -n "$evidence_dir" ]] || { printf 'missing evidence_dir\n'; return 1; }
  [[ "$timeout_seconds" =~ ^[0-9]+$ && "$timeout_seconds" -gt 0 ]] || { printf 'invalid timeout_seconds\n'; return 1; }
  [[ "$lock_wait_seconds" =~ ^[0-9]+$ ]] || { printf 'invalid lock_wait_seconds\n'; return 1; }
  [[ "$stack_role" == "validation" ]] || { printf 'validation worker stack.role must be validation\n'; return 1; }
  [[ -z "$stack_path" || -d "$stack_path" ]] || { printf 'stack.path is not a directory\n'; return 1; }
  if [[ "$request_source_type" == "local-checkout" ]]; then
    [[ -d "$request_source_checkout_path" ]] || { printf 'local checkout path is not a directory\n'; return 1; }
    git -C "$request_source_checkout_path" rev-parse --is-inside-work-tree >/dev/null 2>&1 || { printf 'local checkout path is not a git work tree\n'; return 1; }
  fi
}

acquire_lock() {
  local evidence_dir="$1" wait_seconds="$2" lock_dir="$worker_home/locks/$lock_name.lock" start now
  mkdir -p "$worker_home/locks"
  start="$(date +%s)"
  while true; do
    if mkdir "$lock_dir" 2>/dev/null; then
      printf '%s acquired %s\n' "$(now_utc)" "$lock_dir" >>"$evidence_dir/logs/lock.log"
      printf '%s' "$lock_dir"
      return 0
    fi
    now="$(date +%s)"
    if (( now - start >= wait_seconds )); then
      printf '%s busy after %ss waiting for %s\n' "$(now_utc)" "$wait_seconds" "$lock_dir" >>"$evidence_dir/logs/lock.log"
      return 1
    fi
    sleep 1
  done
}

acquire_named_lock() {
  local evidence_dir="$1" wait_seconds="$2" lock_dir="$3" label="$4" start now
  start="$(date +%s)"
  while true; do
    if mkdir "$lock_dir" 2>/dev/null; then
      printf '%s acquired %s lock %s\n' "$(now_utc)" "$label" "$lock_dir" >>"$evidence_dir/logs/lock.log"
      printf '%s' "$lock_dir"
      return 0
    fi
    now="$(date +%s)"
    if (( now - start >= wait_seconds )); then
      printf '%s busy after %ss waiting for %s lock %s\n' "$(now_utc)" "$wait_seconds" "$label" "$lock_dir" >>"$evidence_dir/logs/lock.log"
      return 1
    fi
    sleep 1
  done
}

release_lock() {
  local lock_dir="${1:-}"
  if [[ -n "$lock_dir" ]]; then
    rm -rf "$lock_dir"
  fi
}

resolve_path() {
  local path="$1"
  if command -v realpath >/dev/null 2>&1; then
    realpath "$path"
  elif command -v readlink >/dev/null 2>&1 && [[ -e "$path" ]]; then
    readlink -f "$path"
  else
    (
      cd "$(dirname "$path")"
      printf '%s/%s\n' "$(pwd -P)" "$(basename "$path")"
    )
  fi
}

write_stack_binding() {
  local evidence_dir="$1" status="$2" role="$3" source="$4" stack_dir="$5" code_path="$6" target="$7" previous_kind="$8" previous_target="$9" restore_status="${10}" message="${11}"
  jq -n \
    --arg status "$status" \
    --arg role "$role" \
    --arg source "$source" \
    --arg stack_dir "$stack_dir" \
    --arg code_path "$code_path" \
    --arg target "$target" \
    --arg previous_kind "$previous_kind" \
    --arg previous_target "$previous_target" \
    --arg restore_status "$restore_status" \
    --arg message "$message" \
    --arg completed_at "$(now_utc)" \
    '{status:$status, role:$role, source:$source, stack_dir:$stack_dir, code_path:$code_path, target:$target, previous_kind:$previous_kind, previous_target:$previous_target, restore_status:$restore_status, message:$message, completed_at:$completed_at}' \
    >"$evidence_dir/stack-binding.json"
}

bind_validation_stack() {
  local evidence_dir="$1" stack_dir="$2" checkout_dir="$3" source="$4" code_path resolved_target current_target previous_kind previous_target

  [[ -n "$stack_dir" ]] || return 0

  code_path="$stack_dir/code"
  resolved_target="$(resolve_path "$checkout_dir")"
  printf '%s binding validation stack %s code to %s\n' "$(now_utc)" "$stack_dir" "$resolved_target" >>"$evidence_dir/logs/stack.log"

  if [[ ! -f "$stack_dir/.env" ]]; then
    write_stack_binding "$evidence_dir" failed validation "$source" "$stack_dir" "$code_path" "$resolved_target" "" "" not-needed "stack .env is missing"
    printf 'stack .env is missing: %s/.env\n' "$stack_dir" >>"$evidence_dir/logs/stack.log"
    return 1
  fi

  if [[ -L "$code_path" ]]; then
    previous_kind=symlink
    previous_target="$(readlink "$code_path")"
  elif [[ ! -e "$code_path" ]]; then
    previous_kind=missing
    previous_target=""
  else
    if [[ -d "$code_path" ]]; then
      write_stack_binding "$evidence_dir" failed validation "$source" "$stack_dir" "$code_path" "$resolved_target" directory "$code_path" not-needed "stack code path is a real directory; refusing to replace it"
      printf 'refusing to replace real directory: %s\n' "$code_path" >>"$evidence_dir/logs/stack.log"
      return 1
    fi
    write_stack_binding "$evidence_dir" failed validation "$source" "$stack_dir" "$code_path" "$resolved_target" other "$code_path" not-needed "stack code path is not a symlink or directory"
    printf 'refusing to replace non-symlink code path: %s\n' "$code_path" >>"$evidence_dir/logs/stack.log"
    return 1
  fi

  current_target="$(resolve_path "$code_path" 2>/dev/null || true)"
  if [[ -L "$code_path" && "$current_target" == "$resolved_target" ]]; then
    STACK_BINDING_STATUS=already-bound
    STACK_BINDING_SOURCE="$source"
    STACK_BINDING_STACK_DIR="$stack_dir"
    STACK_BINDING_CODE_PATH="$code_path"
    STACK_BINDING_TARGET="$resolved_target"
    STACK_BINDING_PREVIOUS_KIND="$previous_kind"
    STACK_BINDING_PREVIOUS_TARGET="$previous_target"
    STACK_BINDING_RESTORE_NEEDED=0
    write_stack_binding "$evidence_dir" already-bound validation "$source" "$stack_dir" "$code_path" "$resolved_target" "$previous_kind" "$previous_target" not-needed "stack code already pointed at worker checkout"
    printf 'stack code already pointed at worker checkout\n' >>"$evidence_dir/logs/stack.log"
    return 0
  fi

  ln -sfn "$resolved_target" "$code_path"
  STACK_BINDING_STATUS=rebound
  STACK_BINDING_SOURCE="$source"
  STACK_BINDING_STACK_DIR="$stack_dir"
  STACK_BINDING_CODE_PATH="$code_path"
  STACK_BINDING_TARGET="$resolved_target"
  STACK_BINDING_PREVIOUS_KIND="$previous_kind"
  STACK_BINDING_PREVIOUS_TARGET="$previous_target"
  STACK_BINDING_RESTORE_NEEDED=1
  write_stack_binding "$evidence_dir" rebound validation "$source" "$stack_dir" "$code_path" "$resolved_target" "$previous_kind" "$previous_target" pending "stack code symlink rebound to worker checkout"
}

restore_validation_stack() {
  local evidence_dir="$1" restore_status=not-needed cleanup_status=0

  [[ -n "${STACK_BINDING_STATUS:-}" ]] || return 0

  if [[ "${STACK_BINDING_RESTORE_NEEDED:-0}" == "1" ]]; then
    case "$STACK_BINDING_PREVIOUS_KIND" in
      symlink)
        if ln -sfn "$STACK_BINDING_PREVIOUS_TARGET" "$STACK_BINDING_CODE_PATH"; then
          restore_status=restored
        else
          restore_status=failed
          cleanup_status=1
        fi
        ;;
      missing)
        if rm -f "$STACK_BINDING_CODE_PATH"; then
          restore_status=removed
        else
          restore_status=failed
          cleanup_status=1
        fi
        ;;
      *)
        restore_status=not-needed
        ;;
    esac
  fi

  STACK_BINDING_RESTORE_NEEDED=0
  write_stack_binding "$evidence_dir" "$STACK_BINDING_STATUS" validation "$STACK_BINDING_SOURCE" "$STACK_BINDING_STACK_DIR" "$STACK_BINDING_CODE_PATH" "$STACK_BINDING_TARGET" "$STACK_BINDING_PREVIOUS_KIND" "$STACK_BINDING_PREVIOUS_TARGET" "$restore_status" "stack code binding cleanup complete" || cleanup_status=1
  return "$cleanup_status"
}

mark_active_afk_check_inconclusive() {
  local index="${RUN_REQUEST_AFK_ACTIVE_INDEX:--1}" last_index=-1
  [[ "$profile" == "tier1-tier3-harness" && -n "${RESULT_CHECKS_PATH:-}" ]] || return 0
  if [[ "$index" -lt 0 ]]; then
    for index in "${!afk_check_statuses[@]}"; do
      [[ "${afk_check_statuses[$index]}" == "not_run" ]] || last_index="$index"
    done
    index="$last_index"
  fi
  if [[ "$index" -ge 0 ]]; then
    afk_check_statuses[$index]=inconclusive
  fi
  write_afk_checks "$RESULT_CHECKS_PATH" inconclusive "$RUN_REQUEST_AFK_LOG_PREFIX"
}

write_terminal_run_failure() {
  local category="$1" exit_code="$2" message="$3"
  mark_active_afk_check_inconclusive || true
  write_result "$RUN_REQUEST_CLEANUP_EVIDENCE_DIR" failed "$category" "$exit_code" "$message" \
    "$RUN_REQUEST_CHECKOUT_DIR" "$RUN_REQUEST_HEAD_COMMIT" "$RUN_REQUEST_STACK_PATH_SOURCE"
}

run_tracked_command() {
  local status

  # Give every potentially long-running child its own process group. Keep the
  # pgid independently of the leader: a command may return after daemonizing a
  # descendant, and the reaped leader can no longer be queried with ps.
  set +e
  setsid "$@" &
  RUN_REQUEST_CHILD_PID=$!
  RUN_REQUEST_CHILD_PGID=$RUN_REQUEST_CHILD_PID
  wait "$RUN_REQUEST_CHILD_PID"
  status=$?
  if process_group_has_live_members "$RUN_REQUEST_CHILD_PGID"; then
    # Normal leader completion is not command-tree completion. Terminate and
    # prove the leaked descendants dead before shared locks can be released.
    terminate_request_child
    if [[ "$?" -ne 0 ]]; then
      set -e
      write_terminal_run_failure child_termination_failed 1 \
        "validation command descendants remained active after KILL grace period; stack cleanup withheld" || true
      trap - RETURN EXIT
      exit 1
    fi
    # Preserve timeout classification after proving the remaining command tree
    # dead. A successful command that leaked descendants is still an internal
    # validation failure rather than a success.
    if [[ "$status" -ne 124 && "$status" -ne 137 ]]; then
      status=125
    fi
  else
    RUN_REQUEST_CHILD_PID=
    RUN_REQUEST_CHILD_PGID=
  fi
  set -e
  return "$status"
}

run_tracked_timeout_command() {
  local remaining_ns remaining_ms remaining_duration
  remaining_ns=$(( ${RUN_REQUEST_COMMAND_DEADLINE_NS:-$RUN_REQUEST_DEADLINE_NS} - $(date +%s%N) ))
  remaining_ms=$(( remaining_ns / 1000000 ))
  if [[ "$remaining_ms" -le 0 ]]; then
    return 124
  fi
  printf -v remaining_duration '%d.%03ds' "$(( remaining_ms / 1000 ))" "$(( remaining_ms % 1000 ))"
  # --foreground keeps the managed command in the process group created by
  # run_tracked_command, so killing that group cannot leave it behind.
  run_tracked_command timeout --foreground --signal=TERM --kill-after="${termination_grace_seconds}s" "$remaining_duration" "$@"
}

process_group_has_live_members() {
  local pgid="$1"

  # The group leader remains as a zombie until this shell waits for it. Ignore
  # zombies while deciding whether any command descendant can still retain
  # validation-stack resources.
  ps -eo pgid=,stat= 2>/dev/null | awk -v target="$pgid" '
    $1 == target && $2 !~ /^Z/ { found = 1 }
    END { exit(found ? 0 : 1) }
  '
}

terminate_request_child() {
  local pid="${RUN_REQUEST_CHILD_PID:-}" deadline pgid="${RUN_REQUEST_CHILD_PGID:-}" observed_pgid group_tracked=0
  [[ -n "$pid" ]] || return 0

  # run_tracked_command records the setsid group before waiting. The lookup is
  # useful while the leader remains live, but the recorded value stays
  # authoritative after normal completion has reaped that leader.
  observed_pgid="$(ps -o pgid= -p "$pid" 2>/dev/null | tr -d ' ' || true)"
  if [[ -z "$pgid" ]]; then
    pgid="$observed_pgid"
  fi
  if [[ -n "$pgid" && "$pgid" == "$pid" ]]; then
    group_tracked=1
    kill -TERM -- "-$pgid" 2>/dev/null || true
  else
    kill -TERM "$pid" 2>/dev/null || true
  fi
  deadline=$(( $(date +%s) + termination_grace_seconds ))
  while :; do
    if [[ "$group_tracked" -eq 1 ]]; then
      process_group_has_live_members "$pgid" || break
    else
      kill -0 "$pid" 2>/dev/null || break
    fi
    if [[ "$(date +%s)" -ge "$deadline" ]]; then
      if [[ "$group_tracked" -eq 1 ]]; then
        kill -KILL -- "-$pgid" 2>/dev/null || true
      else
        kill -KILL "$pid" 2>/dev/null || true
      fi

      # Signal delivery is asynchronous. Do not restore the shared stack or
      # release its locks merely because KILL was sent: a descendant can still
      # be completing exit (for example after uninterruptible I/O). Give exit
      # one final bounded grace period and report failure rather than allowing
      # cleanup to overlap a surviving command tree.
      deadline=$(( $(date +%s) + termination_grace_seconds ))
      while :; do
        if [[ "$group_tracked" -eq 1 ]]; then
          process_group_has_live_members "$pgid" || break
        else
          kill -0 "$pid" 2>/dev/null || break
        fi
        if [[ "$(date +%s)" -ge "$deadline" ]]; then
          return 1
        fi
        sleep 0.1
      done
      break
    fi
    sleep 0.1
  done
  wait "$pid" 2>/dev/null || true
  RUN_REQUEST_CHILD_PID=
  RUN_REQUEST_CHILD_PGID=
}

run_request_cleanup() {
  local cleanup_status=0 cleanup_evidence
  [[ "${RUN_REQUEST_CLEANUP_ACTIVE:-0}" == "1" ]] || return 0
  RUN_REQUEST_CLEANUP_ACTIVE=0
  trap - RETURN EXIT INT TERM
  cleanup_evidence="$RUN_REQUEST_CLEANUP_EVIDENCE_DIR"

  # Restoration and both lock removals are independent best-effort operations:
  # one cleanup failure must never strand either lock, but still makes the run
  # fail rather than hiding incomplete cleanup.
  restore_validation_stack "$cleanup_evidence" || cleanup_status=1
  release_lock "$RUN_REQUEST_CLEANUP_STACK_LOCK_DIR" || cleanup_status=1
  release_lock "$RUN_REQUEST_CLEANUP_LOCK_DIR" || cleanup_status=1
  if [[ "$cleanup_status" -ne 0 ]]; then
    RUN_REQUEST_CLEANUP_EVIDENCE_DIR="$cleanup_evidence"
    write_terminal_run_failure cleanup_failed 1 "validation cleanup failed; inspect stack and lock evidence" || true
  fi
  RUN_REQUEST_CLEANUP_EVIDENCE_DIR=
  RUN_REQUEST_CLEANUP_STACK_LOCK_DIR=
  RUN_REQUEST_CLEANUP_LOCK_DIR=
  return "$cleanup_status"
}

run_request_signal() {
  local exit_code="$1" cleanup_status=0
  trap - INT TERM
  if ! terminate_request_child; then
    # Keep the stack binding and locks intact if the command tree could not be
    # proven dead. Releasing shared resources in this state would permit a new
    # request to race the surviving descendant.
    write_terminal_run_failure child_termination_failed 1 \
      "validation command descendants remained active after KILL grace period; stack cleanup withheld" || true
    # run_request installed cleanup as both RETURN and EXIT protection. This
    # failure intentionally retains the binding and locks, so prevent exit from
    # invoking the cleanup that this branch must withhold.
    trap - EXIT
    exit 1
  fi
  if [[ "${RUN_REQUEST_ACTOR_CLEANUP_REQUIRED:-0}" == "1" ]]; then
    RUN_REQUEST_COMMAND_DEADLINE_NS=$RUN_REQUEST_DEADLINE_NS
    if ! perform_actor_queue_cleanup; then
      write_terminal_run_failure cleanup_failed 1 \
        "actor queue database cleanup failed after interruption; inspect actor-queue-cleanup.log" || true
      run_request_cleanup || true
      exit 1
    fi
  fi
  write_terminal_run_failure interrupted "$exit_code" "validation interrupted by signal" || true
  run_request_cleanup || cleanup_status=1
  if [[ "$cleanup_status" -ne 0 ]]; then
    exit 1
  fi
  exit "$exit_code"
}

verify_checkout_submodules() {
  local checkout_dir="$1" evidence_dir="$2" mode="$3" output status

  local output_path="$evidence_dir/logs/submodule-status.tmp"

  set +e
  run_tracked_timeout_command git -C "$checkout_dir" submodule status --recursive >"$output_path" 2>&1
  status=$?
  set -e
  output="$(cat "$output_path")"
  printf '%s\n' "$output" >>"$evidence_dir/logs/submodule.log"
  rm -f "$output_path"

  if [[ "$status" -eq 124 ]]; then
    SUBMODULE_ERROR_CATEGORY=timeout
    SUBMODULE_ERROR_MESSAGE="submodule verification timed out"
    return 1
  fi
  if [[ "$status" -ne 0 ]]; then
    SUBMODULE_ERROR_CATEGORY=submodule_failed
    SUBMODULE_ERROR_MESSAGE="failed to inspect checkout submodules"
    return 1
  fi
  if [[ "$mode" == "local-checkout" ]] && printf '%s\n' "$output" | grep -Eq '^[-+U]'; then
    SUBMODULE_ERROR_CATEGORY=submodule_failed
    SUBMODULE_ERROR_MESSAGE="local checkout submodules are not initialized and pinned to recorded commits"
    return 1
  fi

  return 0
}

validate_submodule_config() {
  local config_path="$1" allow_production="$2"
  local key name path url entry_count=0 test_path=
  local -a keys

  [[ -f "$config_path" ]] || return 0
  mapfile -t keys < <(git config -f "$config_path" --name-only --list)
  for key in "${keys[@]}"; do
    case "$key" in
      submodule.*.path|submodule.*.url) ;;
      *)
        printf 'unapproved submodule config key: %s\n' "$key"
        return 1
        ;;
    esac
  done
  mapfile -t keys < <(git config -f "$config_path" --name-only --get-regexp '^submodule\..*\.path$')
  for key in "${keys[@]}"; do
    name="${key#submodule.}"
    name="${name%.path}"
    path="$(git config -f "$config_path" --get "$key")"
    url="$(git config -f "$config_path" --get "submodule.$name.url")"
    [[ -n "$path" && -n "$url" && "$path" != /* \
      && "$path" != ".." && "$path" != ../* && "$path" != */../* && "$path" != */.. ]] || return 1
    entry_count=$((entry_count + 1))
    if [[ "$allow_production" == "1" ]]; then
      case "$path|$url" in
        "submodules/websocketpp|https://github.com/zaphoyd/websocketpp.git") continue ;;
        "submodules/vcpkg|https://github.com/microsoft/vcpkg.git") continue ;;
      esac
    fi
    if [[ "${VALIDATION_WORKER_VALIDATE_DRY_RUN:-0}" == "1" \
      && -n "${VALIDATION_WORKER_TEST_FILE_SUBMODULE_ROOT:-}" ]]; then
      case "$url" in
        file://*) test_path="${url#file://}" ;;
        /*) test_path="$url" ;;
        *) test_path= ;;
      esac
      if [[ -n "$test_path" \
        && "$(resolve_path "$test_path")" == "$(resolve_path "$VALIDATION_WORKER_TEST_FILE_SUBMODULE_ROOT")"/* ]]; then
        continue
      fi
    fi
    printf 'unapproved submodule path or URL: %s -> %s\n' "$path" "$url"
    return 1
  done
  [[ "${#keys[@]}" -eq "$entry_count" ]] || return 1
  [[ "$(git config -f "$config_path" --name-only --get-regexp '^submodule\..*\.url$' | wc -l)" -eq "$entry_count" ]]
}

run_isolated_submodule_update() {
  local checkout_dir="$1" evidence_dir="$2" recursive="$3" status
  local -a transport_config=(-c protocol.allow=never -c protocol.https.allow=always)
  local -a update_args=(submodule update --init)

  case "$recursive" in
    yes) update_args+=(--recursive) ;;
    no) ;;
    *) return 2 ;;
  esac
  if [[ "${VALIDATION_WORKER_VALIDATE_DRY_RUN:-0}" == "1" \
    && -n "${VALIDATION_WORKER_TEST_FILE_SUBMODULE_ROOT:-}" ]]; then
    transport_config+=(-c protocol.file.allow=always)
  fi

  set +e
  run_tracked_timeout_command env -u GIT_CONFIG_PARAMETERS -u GIT_CONFIG_COUNT \
    GIT_CONFIG_GLOBAL=/dev/null GIT_CONFIG_SYSTEM=/dev/null GIT_CONFIG_NOSYSTEM=1 \
    GIT_TERMINAL_PROMPT=0 GIT_ASKPASS= SSH_ASKPASS= \
    git -C "$checkout_dir" "${transport_config[@]}" "${update_args[@]}" \
    >>"$evidence_dir/logs/submodule.log" 2>&1
  status=$?
  set -e
  if [[ "$status" -ne 0 ]]; then
    if [[ "$status" -eq 124 ]]; then
      PREPARE_ERROR_CATEGORY=timeout
      PREPARE_ERROR_MESSAGE="submodule initialization timed out"
    else
      PREPARE_ERROR_CATEGORY=submodule_failed
      PREPARE_ERROR_MESSAGE="failed to initialize checkout submodules"
    fi
    return 1
  fi
}

initialize_checkout_submodules() {
  local checkout_dir="$1" evidence_dir="$2" submodule_key submodule_path nested_config
  local index_entry index_metadata indexed_path submodule_dir nested_toplevel

  if ! validate_submodule_config "$checkout_dir/.gitmodules" 1 >>"$evidence_dir/logs/submodule.log" 2>&1; then
    PREPARE_ERROR_CATEGORY=submodule_failed
    PREPARE_ERROR_MESSAGE="checkout contains an unapproved submodule configuration"
    return 1
  fi
  run_isolated_submodule_update "$checkout_dir" "$evidence_dir" no || return 1

  while read -r submodule_key submodule_path; do
    [[ -n "$submodule_key" && -n "$submodule_path" ]] || continue
    index_entry="$(git -C "$checkout_dir" ls-files --stage -- "$submodule_path")" || continue
    [[ "$index_entry" != *$'\n'* && "$index_entry" == *$'\t'* ]] || continue
    index_metadata="${index_entry%%$'\t'*}"
    indexed_path="${index_entry#*$'\t'}"
    [[ "${index_metadata%% *}" == "160000" && "$indexed_path" == "$submodule_path" ]] || continue
    submodule_dir="$(resolve_path "$checkout_dir/$submodule_path")" || continue
    nested_toplevel="$(git -C "$submodule_dir" rev-parse --show-toplevel 2>/dev/null)" || continue
    [[ "$(resolve_path "$nested_toplevel")" == "$submodule_dir" ]] || continue
    nested_config="$submodule_dir/.gitmodules"
    if ! validate_submodule_config "$nested_config" 0 >>"$evidence_dir/logs/submodule.log" 2>&1; then
      PREPARE_ERROR_CATEGORY=submodule_failed
      PREPARE_ERROR_MESSAGE="checkout contains an unapproved recursive submodule configuration"
      return 1
    fi
  done < <(git config -f "$checkout_dir/.gitmodules" --get-regexp '^submodule\..*\.path$' 2>/dev/null || true)

  run_isolated_submodule_update "$checkout_dir" "$evidence_dir" yes
}

set_checkout_preparation_error() {
  local status="$1" message="$2"
  if [[ "$status" -eq 124 || "$status" -eq 137 ]]; then
    PREPARE_ERROR_CATEGORY=timeout
    PREPARE_ERROR_MESSAGE="checkout preparation timed out"
  else
    PREPARE_ERROR_CATEGORY=fetch_failed
    PREPARE_ERROR_MESSAGE="$message"
  fi
}

reset_checkout_directory() {
  local checkout_dir="$1" evidence_dir="$2" status

  set +e
  run_tracked_timeout_command rm -rf -- "$checkout_dir" >>"$evidence_dir/logs/fetch.log" 2>&1
  status=$?
  set -e
  if [[ "$status" -ne 0 ]]; then
    set_checkout_preparation_error "$status" "failed to remove previous checkout"
    return 1
  fi

  set +e
  run_tracked_timeout_command mkdir -p -- "$checkout_dir" >>"$evidence_dir/logs/fetch.log" 2>&1
  status=$?
  set -e
  if [[ "$status" -ne 0 ]]; then
    set_checkout_preparation_error "$status" "failed to create checkout directory"
    return 1
  fi
}

run_checkout_git_step() {
  local checkout_dir="$1" evidence_dir="$2" status
  shift 2

  set +e
  run_tracked_timeout_command git -C "$checkout_dir" "$@" >>"$evidence_dir/logs/fetch.log" 2>&1
  status=$?
  set -e
  if [[ "$status" -eq 0 ]]; then
    return 0
  fi
  set_checkout_preparation_error "$status" "failed to fetch or checkout requested ref"
  return 1
}

prepare_checkout() {
  local checkout_dir="$1" evidence_dir="$2"

  if [[ "$request_source_type" == "fetch" ]]; then
    run_checkout_git_step "$checkout_dir" "$evidence_dir" init || return 1
    run_checkout_git_step "$checkout_dir" "$evidence_dir" remote add origin "$repo" || return 1
    run_checkout_git_step "$checkout_dir" "$evidence_dir" fetch --depth=1 origin "$request_source_ref" || return 1
    run_checkout_git_step "$checkout_dir" "$evidence_dir" checkout --detach FETCH_HEAD || return 1

    if ! initialize_checkout_submodules "$checkout_dir" "$evidence_dir"; then
      return 1
    fi
  fi

  if ! verify_checkout_submodules "$checkout_dir" "$evidence_dir" "$request_source_type"; then
    PREPARE_ERROR_CATEGORY="$SUBMODULE_ERROR_CATEGORY"
    PREPARE_ERROR_MESSAGE="$SUBMODULE_ERROR_MESSAGE"
    return 1
  fi

  return 0
}

run_request() {
  local request_path="$1" validation_status validation_step lock_dir stack_lock_dir stack_lock checkout_dir head_commit stack_path_source
  local local_checks_path afk_log_prefix=logs
  local -a validation_steps=()
  project= repo= ref= commit= profile= run_id= evidence_dir= timeout_seconds= lock_wait_seconds= stack_role= stack_path=
  request_source_type= request_source_repo= request_source_ref= request_source_commit= request_source_checkout_path=
  stack_path_source=
  RESULT_CHECKS_PATH=
  STACK_BINDING_STATUS= STACK_BINDING_SOURCE= STACK_BINDING_STACK_DIR= STACK_BINDING_CODE_PATH= STACK_BINDING_TARGET= STACK_BINDING_PREVIOUS_KIND= STACK_BINDING_PREVIOUS_TARGET= STACK_BINDING_RESTORE_NEEDED=0
  RUN_REQUEST_CHECKOUT_DIR= RUN_REQUEST_HEAD_COMMIT= RUN_REQUEST_STACK_PATH_SOURCE= RUN_REQUEST_AFK_LOG_PREFIX=logs RUN_REQUEST_AFK_ACTIVE_INDEX=-1 RUN_REQUEST_DEADLINE_NS= RUN_REQUEST_COMMAND_DEADLINE_NS= RUN_REQUEST_CHILD_PID= RUN_REQUEST_CHILD_PGID= RUN_REQUEST_ACTOR_CLEANUP_REQUIRED=0

  # Discover and register a requested combined gate before validating fields that
  # can fail (notably stack.path). This is evidence initialization only; request
  # validation below remains authoritative and prevents any dispatch.
  profile="$(json_get '.profile | strings' "$request_path")"
  evidence_dir="$(json_get '.evidence_dir | strings' "$request_path")"
  commit="$(json_get '.commit | strings' "$request_path")"
  if [[ -z "$commit" ]]; then
    commit="$(json_get '.repo.commit | strings' "$request_path")"
  fi
  if [[ "$profile" == "tier1-tier3-harness" && -n "$evidence_dir" ]]; then
    local_checks_path="$evidence_dir/afk-checks.json"
    RESULT_CHECKS_PATH="$local_checks_path"
    afk_log_prefix=logs
    if [[ "${VALIDATION_WORKER_AFK_MODE:-0}" == "1" ]]; then
      afk_log_prefix=worker/logs
    fi
    initialize_afk_check_statuses
    ensure_afk_check_logs "$evidence_dir" 1
    export AFK_CHECK_CANDIDATE_COMMIT="$commit"
    write_afk_checks "$local_checks_path" inconclusive "$afk_log_prefix"
  fi

  if ! validate_request "$request_path" >/tmp/validation-worker-request-error.$$ 2>&1; then
    evidence_dir="$(json_get '.evidence_dir | strings' "$request_path")"
    evidence_dir="${evidence_dir:-$worker_home/evidence/invalid-$(date +%s)}"
    ensure_log_files "$evidence_dir"
    copy_request_evidence "$request_path" "$evidence_dir" || true
    cat /tmp/validation-worker-request-error.$$ >>"$evidence_dir/logs/request.log"
    rm -f /tmp/validation-worker-request-error.$$
    write_result "$evidence_dir" failed invalid_request 2 "invalid request"
    return 2
  fi
  rm -f /tmp/validation-worker-request-error.$$ 2>/dev/null || true

  ensure_log_files "$evidence_dir"
  copy_request_evidence "$request_path" "$evidence_dir" || true

  if [[ -n "$stack_path" ]]; then
    stack_path_source=request.stack.path
  elif [[ -n "${AKKSTACK_DIR:-}" ]]; then
    stack_path="$AKKSTACK_DIR"
    stack_path_source=AKKSTACK_DIR
    if [[ ! -d "$stack_path" ]]; then
      write_result "$evidence_dir" failed invalid_request 2 "AKKSTACK_DIR is not a directory" "" "" "$stack_path_source"
      return 2
    fi
  fi

  checkout_dir="$worker_home/checkouts/$run_id"
  if [[ "$request_source_type" == "local-checkout" ]]; then
    checkout_dir="$(resolve_path "$request_source_checkout_path")"
  fi

  if ! lock_dir="$(acquire_lock "$evidence_dir" "$lock_wait_seconds")"; then
    write_result "$evidence_dir" failed worker_busy 1 "exclusive validation slot is busy" "$checkout_dir" "" "$stack_path_source"
    return 1
  fi
  RUN_REQUEST_CLEANUP_ACTIVE=1
  RUN_REQUEST_CLEANUP_EVIDENCE_DIR="$evidence_dir"
  RUN_REQUEST_CLEANUP_LOCK_DIR="$lock_dir"
  RUN_REQUEST_CLEANUP_STACK_LOCK_DIR=
  RUN_REQUEST_CHECKOUT_DIR="$checkout_dir"
  RUN_REQUEST_STACK_PATH_SOURCE="$stack_path_source"
  RUN_REQUEST_AFK_LOG_PREFIX="$afk_log_prefix"
  trap run_request_cleanup RETURN EXIT
  trap 'run_request_signal 130' INT
  trap 'run_request_signal 143' TERM
  RUN_REQUEST_DEADLINE_NS=$(( $(date +%s%N) + (timeout_seconds * 1000000000) ))

  if [[ "$request_source_type" == "fetch" ]]; then
    if ! reset_checkout_directory "$checkout_dir" "$evidence_dir"; then
      write_result "$evidence_dir" failed "$PREPARE_ERROR_CATEGORY" 1 "$PREPARE_ERROR_MESSAGE" "$checkout_dir" "" "$stack_path_source"
      return 1
    fi
  fi

  if ! prepare_checkout "$checkout_dir" "$evidence_dir"; then
    write_result "$evidence_dir" failed "$PREPARE_ERROR_CATEGORY" 1 "$PREPARE_ERROR_MESSAGE" "$checkout_dir" "" "$stack_path_source"
    return 1
  fi

  head_commit="$(git -C "$checkout_dir" rev-parse HEAD)"
  RUN_REQUEST_HEAD_COMMIT="$head_commit"
  if [[ -n "$RESULT_CHECKS_PATH" && -z "$commit" ]]; then
    export AFK_CHECK_CANDIDATE_COMMIT="$head_commit"
    write_afk_checks "$RESULT_CHECKS_PATH" inconclusive "$afk_log_prefix"
  fi
  if [[ -n "$commit" && "$head_commit" != "$commit" ]]; then
    write_result "$evidence_dir" failed commit_mismatch 1 "checked out HEAD does not match requested commit" "$checkout_dir" "$head_commit" "$stack_path_source"
    return 1
  fi

  if [[ -n "$stack_path" ]]; then
    stack_lock_dir="$stack_path/.validation-worker-code.lock"
    if ! stack_lock="$(acquire_named_lock "$evidence_dir" "$lock_wait_seconds" "$stack_lock_dir" stack)"; then
      write_result "$evidence_dir" failed stack_busy 1 "validation stack is busy" "$checkout_dir" "$head_commit" "$stack_path_source"
      return 1
    fi
    RUN_REQUEST_CLEANUP_STACK_LOCK_DIR="$stack_lock"
  fi

  if ! bind_validation_stack "$evidence_dir" "$stack_path" "$checkout_dir" "$stack_path_source"; then
    write_result "$evidence_dir" failed stack_binding_failed 1 "failed to bind validation stack to worker checkout" "$checkout_dir" "$head_commit" "$stack_path_source"
    return 1
  fi

  validation_cmd=("$checkout_dir/scripts/validate.sh" --stack validation)
  if [[ "${VALIDATION_WORKER_VALIDATE_DRY_RUN:-0}" == "1" ]]; then
    validation_cmd+=(--dry-run)
  fi

  actor_validation_token="${head_commit:0:16}-${run_id}"
  run_validation() {
    local profile_name="$1" log_path="${2:-$evidence_dir/logs/validation.log}"
    local exit_code

    set +e
    if [[ -n "$stack_path" && -n "${STACK_BINDING_STATUS:-}" ]]; then
      run_tracked_timeout_command env AKKSTACK_DIR="$stack_path" EXPECTED_EQEMU_CHECKOUT="$checkout_dir" \
        ACTOR_QUEUE_VALIDATION_TOKEN="$actor_validation_token" "${validation_cmd[@]}" "$profile_name" >>"$log_path" 2>&1
    elif [[ -n "$stack_path" ]]; then
      run_tracked_timeout_command env AKKSTACK_DIR="$stack_path" ACTOR_QUEUE_VALIDATION_TOKEN="$actor_validation_token" \
        "${validation_cmd[@]}" "$profile_name" >>"$log_path" 2>&1
    else
      run_tracked_timeout_command env ACTOR_QUEUE_VALIDATION_TOKEN="$actor_validation_token" \
        "${validation_cmd[@]}" "$profile_name" >>"$log_path" 2>&1
    fi
    exit_code=$?
    set -e
    return "$exit_code"
  }

  perform_actor_queue_cleanup() {
    local cleanup_status
    set +e
    run_validation actor-queue-cleanup "$evidence_dir/logs/actor-queue-cleanup.log"
    cleanup_status=$?
    set -e
    if [[ "$cleanup_status" -eq 0 ]]; then
      RUN_REQUEST_ACTOR_CLEANUP_REQUIRED=0
    fi
    return "$cleanup_status"
  }

  run_validation_with_actor_cleanup() {
    local profile_name="$1" log_path="${2:-$evidence_dir/logs/validation.log}" status cleanup_status
    local now_ns remaining_ns reserve_ns
    VALIDATION_ACTOR_CLEANUP_FAILED=0
    if [[ "$profile_name" != "actor-queue-tier3" ]]; then
      run_validation "$profile_name" "$log_path"
      return $?
    fi

    # Reserve independent portions of the request's existing deadline for
    # terminating a timed-out actor process and for the out-of-process cleanup
    # pass. Cleanup runs while both locks and the Candidate stack binding are
    # still held. Do not dispatch the mutating scenario when the remaining
    # request budget cannot provide both reserves.
    now_ns=$(date +%s%N)
    remaining_ns=$(( RUN_REQUEST_DEADLINE_NS - now_ns ))
    reserve_ns=$(( (termination_grace_seconds + actor_cleanup_reserve_seconds) * 1000000000 ))
    if [[ "$remaining_ns" -le "$reserve_ns" ]]; then
      return 124
    fi
    RUN_REQUEST_COMMAND_DEADLINE_NS=$(( RUN_REQUEST_DEADLINE_NS - reserve_ns ))
    RUN_REQUEST_ACTOR_CLEANUP_REQUIRED=1
    set +e
    run_validation "$profile_name" "$log_path"
    status=$?
    RUN_REQUEST_COMMAND_DEADLINE_NS=$RUN_REQUEST_DEADLINE_NS
    perform_actor_queue_cleanup
    cleanup_status=$?
    RUN_REQUEST_COMMAND_DEADLINE_NS=
    set -e
    if [[ "$cleanup_status" -ne 0 ]]; then
      VALIDATION_ACTOR_CLEANUP_FAILED=1
      return 125
    fi
    return "$status"
  }

  if [[ "$profile" == "tier1-tier3-harness" ]]; then
    export AFK_CHECK_CANDIDATE_COMMIT="$head_commit"
    mapfile -t afk_plan_rows < <(
      jq -r '.[] | [.profile, .log_file, (.completion_marker // "-"), .failure_status, .failure_message, .inconclusive_message] | @tsv' <<<"$AFK_CHECK_PLAN"
    )
    for afk_check_index in "${!afk_plan_rows[@]}"; do
      RUN_REQUEST_AFK_ACTIVE_INDEX="$afk_check_index"
      IFS=$'\t' read -r afk_profile afk_log_file afk_completion_marker afk_failure_status afk_failure_message afk_inconclusive_message <<<"${afk_plan_rows[$afk_check_index]}"
      if run_validation_with_actor_cleanup "$afk_profile" "$evidence_dir/logs/$afk_log_file"; then
        if [[ "$afk_completion_marker" != "-" ]] \
          && ! grep -Fqx -- "$afk_completion_marker" "$evidence_dir/logs/$afk_log_file"; then
          printf 'required completion evidence missing: %s\n' "$afk_completion_marker" >>"$evidence_dir/logs/$afk_log_file"
          afk_check_statuses[$afk_check_index]=rejected
          write_afk_checks "$local_checks_path" rejected "$afk_log_prefix"
          write_result "$evidence_dir" failed validation_failed 1 "Required scenario did not emit completion evidence: $afk_completion_marker" "$checkout_dir" "$head_commit" "$stack_path_source"
          return 1
        fi
        afk_check_statuses[$afk_check_index]=passed
        write_afk_checks "$local_checks_path" inconclusive "$afk_log_prefix"
        continue
      else
        validation_status=$?
      fi
      if [[ "${VALIDATION_ACTOR_CLEANUP_FAILED:-0}" == "1" ]]; then
        afk_check_statuses[$afk_check_index]=inconclusive
        write_afk_checks "$local_checks_path" inconclusive "$afk_log_prefix"
        write_result "$evidence_dir" failed cleanup_failed 1 "actor queue database cleanup failed; inspect actor-queue-cleanup.log" "$checkout_dir" "$head_commit" "$stack_path_source"
        return 1
      fi
      if [[ ( "$validation_status" -eq 124 || "$validation_status" -eq 137 ) && "${VALIDATION_WORKER_AFK_MODE:-0}" != "1" ]]; then
        afk_check_statuses[$afk_check_index]=inconclusive
        write_afk_checks "$local_checks_path" rejected "$afk_log_prefix"
        write_result "$evidence_dir" failed timeout 1 "validation timed out" "$checkout_dir" "$head_commit" "$stack_path_source"
        return 1
      fi
      if [[ "$validation_status" -eq 124 || "$validation_status" -eq 125 || "$validation_status" -eq 127 || "$validation_status" -eq 137 ]]; then
        afk_failure_status=inconclusive
        afk_failure_message="$afk_inconclusive_message"
      fi
      afk_check_statuses[$afk_check_index]="$afk_failure_status"
      if [[ "$afk_failure_status" == "inconclusive" ]]; then
        write_afk_checks "$local_checks_path" inconclusive "$afk_log_prefix"
        write_result "$evidence_dir" failed prerequisite_unavailable 2 "$afk_failure_message" "$checkout_dir" "$head_commit" "$stack_path_source"
        return 2
      fi
      write_afk_checks "$local_checks_path" rejected "$afk_log_prefix"
      write_result "$evidence_dir" failed validation_failed 1 "$afk_failure_message" "$checkout_dir" "$head_commit" "$stack_path_source"
      return 1
    done
    RUN_REQUEST_AFK_ACTIVE_INDEX=-1
    write_afk_checks "$local_checks_path" passed "$afk_log_prefix"
    write_result "$evidence_dir" passed ok 0 "validation passed" "$checkout_dir" "$head_commit" "$stack_path_source"
    return 0
  fi

  mapfile -t validation_steps < <(
    jq -r --arg profile "$profile" '.[] | select(.name == $profile) | .steps[]?' <<<"$VALIDATION_PROFILES"
  )
  if [[ "${#validation_steps[@]}" -eq 0 ]]; then
    validation_steps=("$profile")
  fi

  for validation_step in "${validation_steps[@]}"; do
    if run_validation_with_actor_cleanup "$validation_step"; then
      continue
    else
      validation_status=$?
    fi
    if [[ "${VALIDATION_ACTOR_CLEANUP_FAILED:-0}" == "1" ]]; then
      write_result "$evidence_dir" failed cleanup_failed 1 "actor queue database cleanup failed; inspect actor-queue-cleanup.log" "$checkout_dir" "$head_commit" "$stack_path_source"
      return 1
    fi
    if [[ "$validation_status" -eq 124 || "$validation_status" -eq 137 ]]; then
      write_result "$evidence_dir" failed timeout 1 "validation timed out" "$checkout_dir" "$head_commit" "$stack_path_source"
      return 1
    fi
    write_result "$evidence_dir" failed validation_failed 1 "validation profile failed" "$checkout_dir" "$head_commit" "$stack_path_source"
    return 1
  done

  write_result "$evidence_dir" passed ok 0 "validation passed" "$checkout_dir" "$head_commit" "$stack_path_source"
  return 0
}

if [[ "$#" -eq 0 ]]; then
  usage >&2
  exit 2
fi

case "$1" in
  -h|--help)
    usage
    exit 0
    ;;
  profiles)
    shift
    if [[ "$#" -eq 1 && "$1" == "--json" ]]; then
      emit_profiles_json
      exit 0
    fi
    usage >&2
    exit 2
    ;;
  self-test)
    shift
    exec "$repo_root/tests/shell/validation-worker-test.sh" "$@"
    ;;
  run)
    shift
    request_path=""
    while [[ "$#" -gt 0 ]]; do
      case "$1" in
        --request)
          [[ "$#" -ge 2 ]] || { usage >&2; exit 2; }
          request_path="$2"
          shift 2
          ;;
        -h|--help)
          usage
          exit 0
          ;;
        *)
          usage >&2
          exit 2
          ;;
      esac
    done
    [[ -n "$request_path" ]] || { usage >&2; exit 2; }
    if is_afk_request "$request_path"; then
      run_afk_request "$request_path"
    else
      run_request "$request_path"
    fi
    ;;
  *)
    usage >&2
    exit 2
    ;;
esac

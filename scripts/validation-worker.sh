#!/usr/bin/env bash
set -euo pipefail

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
repo_root="$(cd "$script_dir/.." && pwd)"
worker_home="${VALIDATION_WORKER_HOME:-$repo_root/.validation-worker}"
lock_name="validation-slot"

usage() {
  cat <<'USAGE'
Usage: scripts/validation-worker.sh <command> [options]

Commands:
  run --request <path>  Execute a validation worker request JSON.
  self-test             Run validation worker shell self-tests.
  -h, --help            Show this help.

Request JSON fields:
  project            Required project name, must be "bump-eqemu" or "bump-EQEmu".
  repo               Required fetchable Git repository URL or path.
  ref                Required fetchable Git ref, branch, tag, or commit.
  commit             Optional expected commit SHA. HEAD must match after checkout.
  profile            Required validation profile: preflight, tier1, tier2-readonly, tier3-harness, or safe.
  run_id             Required stable run identifier used for worker-owned checkout storage.
  evidence_dir       Required directory where request.json, result.json, and logs are written.
  timeout_seconds    Optional validation timeout in seconds. Defaults to 3600.
  lock_wait_seconds  Optional bounded wait for the exclusive worker-local validation slot. Defaults to 0.

The worker fetches into VALIDATION_WORKER_HOME (default: .validation-worker),
acquires one local validation slot, delegates to scripts/validate.sh in the
fetched checkout, and returns a normal process exit code. Set
VALIDATION_WORKER_VALIDATE_DRY_RUN=1 to delegate with --dry-run for local
contract tests.
USAGE
}

json_get() {
  local path="$1" file="$2"
  jq -er "$path // empty" "$file" 2>/dev/null || true
}

json_string() {
  jq -Rn --arg v "$1" '$v'
}

now_utc() {
  date -u +%Y-%m-%dT%H:%M:%SZ
}

ensure_log_files() {
  local evidence_dir="$1"
  mkdir -p "$evidence_dir/logs"
  : >"$evidence_dir/logs/request.log"
  : >"$evidence_dir/logs/fetch.log"
  : >"$evidence_dir/logs/lock.log"
  : >"$evidence_dir/logs/validation.log"
}

write_result() {
  local evidence_dir="$1" status="$2" category="$3" exit_code="$4" message="$5" checkout_dir="${6:-}" head_commit="${7:-}"
  mkdir -p "$evidence_dir"
  jq -n \
    --arg status "$status" \
    --arg category "$category" \
    --arg message "$message" \
    --arg checkout_dir "$checkout_dir" \
    --arg head_commit "$head_commit" \
    --arg completed_at "$(now_utc)" \
    --argjson exit_code "$exit_code" \
    '{status:$status, category:$category, exit_code:$exit_code, message:$message, checkout_dir:$checkout_dir, head_commit:$head_commit, completed_at:$completed_at}' \
    >"$evidence_dir/result.json"
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
  repo="$(json_get '.repo | strings' "$request_path")"
  ref="$(json_get '.ref | strings' "$request_path")"
  commit="$(json_get '.commit | strings' "$request_path")"
  profile="$(json_get '.profile | strings' "$request_path")"
  run_id="$(json_get '.run_id | strings' "$request_path")"
  evidence_dir="$(json_get '.evidence_dir | strings' "$request_path")"
  timeout_seconds="$(json_get '.timeout_seconds // 3600 | numbers' "$request_path")"
  lock_wait_seconds="$(json_get '.lock_wait_seconds // 0 | numbers' "$request_path")"

  [[ "$project" == "bump-eqemu" || "$project" == "bump-EQEmu" ]] || { printf 'invalid or missing project\n'; return 1; }
  [[ -n "$repo" ]] || { printf 'missing repo\n'; return 1; }
  [[ -n "$ref" ]] || { printf 'missing ref\n'; return 1; }
  [[ -z "$commit" || "$commit" =~ ^[0-9a-fA-F]{7,40}$ ]] || { printf 'invalid commit\n'; return 1; }
  case "$profile" in preflight|tier1|tier2-readonly|tier3-harness|safe) ;; *) printf 'invalid or missing profile\n'; return 1 ;; esac
  [[ -n "$run_id" ]] || { printf 'missing run_id\n'; return 1; }
  sanitize_run_id "$run_id" || { printf 'run_id may contain only letters, digits, dot, underscore, and dash\n'; return 1; }
  [[ -n "$evidence_dir" ]] || { printf 'missing evidence_dir\n'; return 1; }
  [[ "$timeout_seconds" =~ ^[0-9]+$ && "$timeout_seconds" -gt 0 ]] || { printf 'invalid timeout_seconds\n'; return 1; }
  [[ "$lock_wait_seconds" =~ ^[0-9]+$ ]] || { printf 'invalid lock_wait_seconds\n'; return 1; }
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

release_lock() {
  local lock_dir="${1:-}"
  [[ -n "$lock_dir" ]] && rm -rf "$lock_dir"
}

run_request() {
  local request_path="$1" validation_status lock_dir checkout_dir head_commit
  project= repo= ref= commit= profile= run_id= evidence_dir= timeout_seconds= lock_wait_seconds=

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

  checkout_dir="$worker_home/checkouts/$run_id"
  rm -rf "$checkout_dir"
  mkdir -p "$checkout_dir"

  if ! git -C "$checkout_dir" init >>"$evidence_dir/logs/fetch.log" 2>&1 \
    || ! git -C "$checkout_dir" remote add origin "$repo" >>"$evidence_dir/logs/fetch.log" 2>&1 \
    || ! git -C "$checkout_dir" fetch --depth=1 origin "$ref" >>"$evidence_dir/logs/fetch.log" 2>&1 \
    || ! git -C "$checkout_dir" checkout --detach FETCH_HEAD >>"$evidence_dir/logs/fetch.log" 2>&1; then
    write_result "$evidence_dir" failed fetch_failed 1 "failed to fetch or checkout requested ref" "$checkout_dir"
    return 1
  fi

  head_commit="$(git -C "$checkout_dir" rev-parse HEAD)"
  if [[ -n "$commit" && "$head_commit" != "$commit" ]]; then
    write_result "$evidence_dir" failed commit_mismatch 1 "checked out HEAD does not match requested commit" "$checkout_dir" "$head_commit"
    return 1
  fi

  if ! lock_dir="$(acquire_lock "$evidence_dir" "$lock_wait_seconds")"; then
    write_result "$evidence_dir" failed worker_busy 1 "exclusive validation slot is busy" "$checkout_dir" "$head_commit"
    return 1
  fi
  trap 'release_lock "${lock_dir:-}"' RETURN

  validation_cmd=("$checkout_dir/scripts/validate.sh" --stack validation)
  if [[ "${VALIDATION_WORKER_VALIDATE_DRY_RUN:-0}" == "1" ]]; then
    validation_cmd+=(--dry-run)
  fi
  validation_cmd+=("$profile")

  set +e
  timeout "$timeout_seconds" "${validation_cmd[@]}" >>"$evidence_dir/logs/validation.log" 2>&1
  validation_status=$?
  set -e

  if [[ "$validation_status" -eq 124 ]]; then
    write_result "$evidence_dir" failed timeout 1 "validation timed out" "$checkout_dir" "$head_commit"
    return 1
  fi
  if [[ "$validation_status" -ne 0 ]]; then
    write_result "$evidence_dir" failed validation_failed 1 "validation profile failed" "$checkout_dir" "$head_commit"
    return 1
  fi

  write_result "$evidence_dir" passed ok 0 "validation passed" "$checkout_dir" "$head_commit"
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
    run_request "$request_path"
    ;;
  *)
    usage >&2
    exit 2
    ;;
esac

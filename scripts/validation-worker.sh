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
  profile            Required validation profile: preflight, tier1, tier2-readonly, tier3-harness, tier1-tier3-harness, or safe.
  run_id             Required stable run identifier used for worker-owned checkout storage.
  evidence_dir       Required directory where request.json, result.json, and logs are written.
  timeout_seconds    Optional validation timeout in seconds. Defaults to 3600.
  lock_wait_seconds  Optional bounded wait for the exclusive worker-local validation slot. Defaults to 0.
  stack.role         Optional stack role. If present, must be "validation".
  stack.path         Optional validation AkkStack path to bind while validation runs.

The worker fetches into VALIDATION_WORKER_HOME (default: .validation-worker),
acquires one local validation slot, delegates to scripts/validate.sh in the
fetched checkout, and returns a normal process exit code. Set
VALIDATION_WORKER_VALIDATE_DRY_RUN=1 to delegate with --dry-run for local
contract tests. If stack.path is omitted, AKKSTACK_DIR remains a path override.
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
  : >"$evidence_dir/logs/stack.log"
  : >"$evidence_dir/logs/submodule.log"
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
  stack_role="$(json_get '.stack.role // "validation" | strings' "$request_path")"
  stack_path="$(json_get '.stack.path | strings' "$request_path")"

  [[ "$project" == "bump-eqemu" || "$project" == "bump-EQEmu" ]] || { printf 'invalid or missing project\n'; return 1; }
  [[ -n "$repo" ]] || { printf 'missing repo\n'; return 1; }
  [[ -n "$ref" ]] || { printf 'missing ref\n'; return 1; }
  [[ -z "$commit" || "$commit" =~ ^[0-9a-fA-F]{7,40}$ ]] || { printf 'invalid commit\n'; return 1; }
  case "$profile" in preflight|tier1|tier2-readonly|tier3-harness|tier1-tier3-harness|safe) ;; *) printf 'invalid or missing profile\n'; return 1 ;; esac
  [[ -n "$run_id" ]] || { printf 'missing run_id\n'; return 1; }
  sanitize_run_id "$run_id" || { printf 'run_id may contain only letters, digits, dot, underscore, and dash\n'; return 1; }
  [[ -n "$evidence_dir" ]] || { printf 'missing evidence_dir\n'; return 1; }
  [[ "$timeout_seconds" =~ ^[0-9]+$ && "$timeout_seconds" -gt 0 ]] || { printf 'invalid timeout_seconds\n'; return 1; }
  [[ "$lock_wait_seconds" =~ ^[0-9]+$ ]] || { printf 'invalid lock_wait_seconds\n'; return 1; }
  [[ "$stack_role" == "validation" ]] || { printf 'validation worker stack.role must be validation\n'; return 1; }
  [[ -z "$stack_path" || -d "$stack_path" ]] || { printf 'stack.path is not a directory\n'; return 1; }
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
  [[ -n "$lock_dir" ]] && rm -rf "$lock_dir"
  return 0
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
  local evidence_dir="$1" restore_status=not-needed

  [[ -n "${STACK_BINDING_STATUS:-}" ]] || return 0

  if [[ "${STACK_BINDING_RESTORE_NEEDED:-0}" == "1" ]]; then
    case "$STACK_BINDING_PREVIOUS_KIND" in
      symlink)
        ln -sfn "$STACK_BINDING_PREVIOUS_TARGET" "$STACK_BINDING_CODE_PATH"
        restore_status=restored
        ;;
      missing)
        rm -f "$STACK_BINDING_CODE_PATH"
        restore_status=removed
        ;;
      *)
        restore_status=not-needed
        ;;
    esac
  fi

  write_stack_binding "$evidence_dir" "$STACK_BINDING_STATUS" validation "$STACK_BINDING_SOURCE" "$STACK_BINDING_STACK_DIR" "$STACK_BINDING_CODE_PATH" "$STACK_BINDING_TARGET" "$STACK_BINDING_PREVIOUS_KIND" "$STACK_BINDING_PREVIOUS_TARGET" "$restore_status" "stack code binding cleanup complete"
}

run_request() {
  local request_path="$1" validation_status lock_dir stack_lock_dir stack_lock checkout_dir head_commit
  project= repo= ref= commit= profile= run_id= evidence_dir= timeout_seconds= lock_wait_seconds= stack_role= stack_path= stack_source=
  STACK_BINDING_STATUS= STACK_BINDING_SOURCE= STACK_BINDING_STACK_DIR= STACK_BINDING_CODE_PATH= STACK_BINDING_TARGET= STACK_BINDING_PREVIOUS_KIND= STACK_BINDING_PREVIOUS_TARGET= STACK_BINDING_RESTORE_NEEDED=0

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
    stack_source=request.stack.path
  elif [[ -n "${AKKSTACK_DIR:-}" ]]; then
    stack_path="$AKKSTACK_DIR"
    stack_source=AKKSTACK_DIR
    if [[ ! -d "$stack_path" ]]; then
      write_result "$evidence_dir" failed invalid_request 2 "AKKSTACK_DIR is not a directory"
      return 2
    fi
  fi

  checkout_dir="$worker_home/checkouts/$run_id"

  if ! lock_dir="$(acquire_lock "$evidence_dir" "$lock_wait_seconds")"; then
    write_result "$evidence_dir" failed worker_busy 1 "exclusive validation slot is busy" "$checkout_dir"
    return 1
  fi
  trap 'restore_validation_stack "$evidence_dir"; release_lock "${stack_lock:-}"; release_lock "${lock_dir:-}"' RETURN

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

  set +e
  env GIT_TERMINAL_PROMPT=0 GIT_ASKPASS= SSH_ASKPASS= timeout "$timeout_seconds" git -C "$checkout_dir" -c protocol.file.allow=always submodule update --init --recursive >>"$evidence_dir/logs/submodule.log" 2>&1
  submodule_status=$?
  set -e

  if [[ "$submodule_status" -eq 124 ]]; then
    write_result "$evidence_dir" failed timeout 1 "submodule initialization timed out" "$checkout_dir" "$head_commit"
    return 1
  fi
  if [[ "$submodule_status" -ne 0 ]]; then
    write_result "$evidence_dir" failed submodule_failed 1 "failed to initialize checkout submodules" "$checkout_dir" "$head_commit"
    return 1
  fi

  if [[ -n "$stack_path" ]]; then
    stack_lock_dir="$stack_path/.validation-worker-code.lock"
    if ! stack_lock="$(acquire_named_lock "$evidence_dir" "$lock_wait_seconds" "$stack_lock_dir" stack)"; then
      write_result "$evidence_dir" failed stack_busy 1 "validation stack is busy" "$checkout_dir" "$head_commit"
      return 1
    fi
  fi

  if ! bind_validation_stack "$evidence_dir" "$stack_path" "$checkout_dir" "$stack_source"; then
    write_result "$evidence_dir" failed stack_binding_failed 1 "failed to bind validation stack to worker checkout" "$checkout_dir" "$head_commit"
    return 1
  fi

  validation_cmd=("$checkout_dir/scripts/validate.sh" --stack validation)
  if [[ "${VALIDATION_WORKER_VALIDATE_DRY_RUN:-0}" == "1" ]]; then
    validation_cmd+=(--dry-run)
  fi

  validation_started_at_ns="$(date +%s%N)"
  run_validation() {
    local profile_name="$1" exit_code elapsed_ns remaining_ns remaining_ms remaining_duration

    elapsed_ns=$(( $(date +%s%N) - validation_started_at_ns ))
    remaining_ns=$(( (timeout_seconds * 1000000000) - elapsed_ns ))
    remaining_ms=$(( remaining_ns / 1000000 ))
    if [[ "$remaining_ms" -le 0 ]]; then
      return 124
    fi
    printf -v remaining_duration '%d.%03ds' "$(( remaining_ms / 1000 ))" "$(( remaining_ms % 1000 ))"

    set +e
    if [[ -n "$stack_path" && -n "${STACK_BINDING_STATUS:-}" ]]; then
      AKKSTACK_DIR="$stack_path" EXPECTED_EQEMU_CHECKOUT="$checkout_dir" timeout "$remaining_duration" "${validation_cmd[@]}" "$profile_name" >>"$evidence_dir/logs/validation.log" 2>&1
    elif [[ -n "$stack_path" ]]; then
      AKKSTACK_DIR="$stack_path" timeout "$remaining_duration" "${validation_cmd[@]}" "$profile_name" >>"$evidence_dir/logs/validation.log" 2>&1
    else
      timeout "$remaining_duration" "${validation_cmd[@]}" "$profile_name" >>"$evidence_dir/logs/validation.log" 2>&1
    fi
    exit_code=$?
    set -e

    return "$exit_code"
  }

  case "$profile" in
    tier1-tier3-harness)
      if run_validation tier1; then
        :
      else
        validation_status=$?
        if [[ "$validation_status" -eq 124 ]]; then
          write_result "$evidence_dir" failed timeout 1 "validation timed out" "$checkout_dir" "$head_commit"
          return 1
        fi
        write_result "$evidence_dir" failed validation_failed 1 "validation profile failed" "$checkout_dir" "$head_commit"
        return 1
      fi
      if run_validation tier3-harness; then
        :
      else
        validation_status=$?
        if [[ "$validation_status" -eq 124 ]]; then
          write_result "$evidence_dir" failed timeout 1 "validation timed out" "$checkout_dir" "$head_commit"
          return 1
        fi
        write_result "$evidence_dir" failed validation_failed 1 "validation profile failed" "$checkout_dir" "$head_commit"
        return 1
      fi
      ;;
    *)
      if run_validation "$profile"; then
        :
      else
        validation_status=$?
        if [[ "$validation_status" -eq 124 ]]; then
          write_result "$evidence_dir" failed timeout 1 "validation timed out" "$checkout_dir" "$head_commit"
          return 1
        fi
        write_result "$evidence_dir" failed validation_failed 1 "validation profile failed" "$checkout_dir" "$head_commit"
        return 1
      fi
      ;;
  esac

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

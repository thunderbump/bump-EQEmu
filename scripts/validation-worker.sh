#!/usr/bin/env bash
set -euo pipefail

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
repo_default="$(cd "$script_dir/.." && pwd)"
source "$script_dir/lib/akkstack-routing.sh"
source "$script_dir/lib/validation-worker-json.sh"

usage() {
  cat <<'USAGE'
Usage: scripts/validation-worker.sh run --request <request.json>

Runs a local Validation Worker request and writes structured evidence. The first
implemented profile is preflight, which checks whether the selected validation
AkkStack can safely run automation.
USAGE
}

iso_now() {
  date -u +%Y-%m-%dT%H:%M:%SZ
}

profile_database_behavior() {
  case "$1" in
    preflight) printf 'no mutation expected\n' ;;
    safe) printf 'read-mostly\n' ;;
    tier3-harness) printf 'read-mostly/runtime fixture use\n' ;;
    *) printf 'unknown\n' ;;
  esac
}

request_file=""
if [[ "${1:-}" == "-h" || "${1:-}" == "--help" || "${1:-}" == "help" ]]; then
  usage
  exit 0
fi

[[ "${1:-}" == "run" ]] || { usage >&2; exit 2; }
shift
while [[ "$#" -gt 0 ]]; do
  case "$1" in
    --request)
      [[ "$#" -ge 2 ]] || { printf 'error: --request requires a JSON file\n' >&2; exit 2; }
      request_file="$2"
      shift 2
      ;;
    --request=*)
      request_file="${1#--request=}"
      shift
      ;;
    -h|--help)
      usage
      exit 0
      ;;
    *)
      printf 'error: unknown option %s\n' "$1" >&2
      usage >&2
      exit 2
      ;;
  esac
done

[[ -n "$request_file" ]] || { printf 'error: --request is required\n' >&2; exit 2; }
[[ -f "$request_file" ]] || { printf 'error: request file is missing: %s\n' "$request_file" >&2; exit 2; }

profile="$(json_get "$request_file" profile)"
case "$profile" in
  preflight|safe|tier3-harness)
    ;;
  *)
    printf 'error: unsupported validation profile: %s\n' "${profile:-<empty>}" >&2
    exit 2
    ;;
esac

request_dir="$(cd "$(dirname "$request_file")" && pwd)"
repo_root="$(json_get "$request_file" repo.path "$repo_default")"
[[ "$repo_root" == /* ]] || repo_root="$request_dir/$repo_root"
repo_root="$(akkstack_resolve_path "$repo_root")"
role="$(json_get "$request_file" stack.role validation)"
stack_path="$(json_get "$request_file" stack.path)"
evidence_dir="$(json_get "$request_file" evidenceDir "$repo_root/.case/validation-worker/preflight")"
[[ "$evidence_dir" == /* ]] || evidence_dir="$request_dir/$evidence_dir"
evidence_dir="$(akkstack_resolve_path "$evidence_dir")"
expected_commit="$(json_get "$request_file" repo.commit)"
docker_required="$(json_bool "$(json_get "$request_file" checks.dockerRequired)" true)"
database_required="$(json_bool "$(json_get "$request_file" checks.databaseRequired)" false)"
assets_required="$(json_bool "$(json_get "$request_file" checks.assetsRequired)" false)"
database_content_required="$(json_bool "$(json_get "$request_file" checks.databaseContentRequired)" "$database_required")"
docker_command="$(json_get "$request_file" tools.dockerCommand docker)"
if [[ "$docker_command" == */* && "$docker_command" != /* ]]; then
  docker_command="$request_dir/$docker_command"
fi
docker_cmd=("$docker_command")
dry_run="$(json_bool "$(json_get "$request_file" dryRun)" false)"
timeout_seconds="$(json_int "$(json_get "$request_file" timeoutSeconds)" 0)"
lock_wait_seconds="$(json_int "$(json_get "$request_file" lockWaitSeconds)" 30)"

mkdir -p "$evidence_dir/steps"
steps_tsv="$evidence_dir/steps.tsv"
: >"$steps_tsv"
failures=0

exec 9>"$evidence_dir/worker.lock"
if ! flock -w "$lock_wait_seconds" 9; then
  printf 'error: timed out waiting for validation worker lock: %s\n' "$evidence_dir/worker.lock" >&2
  exit 75
fi

if [[ -n "$stack_path" ]]; then
  [[ "$stack_path" == /* ]] || stack_path="$request_dir/$stack_path"
  AKKSTACK_DIR="$stack_path" akkstack_init_routing "$repo_root" "$role"
else
  akkstack_init_routing "$repo_root" "$role"
fi

write_result() {
  python3 - "$steps_tsv" "$evidence_dir/result.json" "$profile" "$repo_root" "$AKKSTACK_STACK_DIR" "$failures" "$(profile_database_behavior "$profile")" <<'PY'
import json, sys
steps_path, result_path, profile, repo, stack, failures, database_behavior = sys.argv[1:8]
steps = []
with open(steps_path, encoding="utf-8") as f:
    for line in f:
        name, status, category, reason, log_file, command, started_at, ended_at, exit_code = line.rstrip("\n").split("\t", 8)
        steps.append({
            "name": name,
            "status": status,
            "category": category,
            "reason": reason,
            "command": command,
            "startedAt": started_at,
            "endedAt": ended_at,
            "exitCode": int(exit_code),
            "log": log_file,
        })
result = {
    "profile": profile,
    "status": "pass" if int(failures) == 0 else "fail",
    "failureCount": int(failures),
    "repo": repo,
    "stackPath": stack,
    "metadata": {"databaseBehavior": database_behavior},
    "steps": steps,
}
with open(result_path, "w", encoding="utf-8") as f:
    json.dump(result, f, indent=2)
    f.write("\n")
PY
}

record_step() {
  local name="$1" status="$2" category="$3" reason="$4"
  local log_file="$evidence_dir/steps/$name.log"
  local command="internal:$name" exit_code=0 started_at ended_at
  shift 4 || true
  [[ "$status" != "fail" ]] || exit_code=1
  started_at="$(iso_now)"
  ended_at="$started_at"
  {
    printf 'step: %s\nstatus: %s\ncategory: %s\nreason: %s\ncommand: %s\nexitCode: %s\nstartedAt: %s\nendedAt: %s\n' \
      "$name" "$status" "$category" "$reason" "$command" "$exit_code" "$started_at" "$ended_at"
    if [[ "$#" -gt 0 ]]; then
      printf '%s\n' "$@"
    fi
  } >"$log_file"
  printf '%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\n' \
    "$name" "$status" "$category" "$reason" "$log_file" "$command" "$started_at" "$ended_at" "$exit_code" >>"$steps_tsv"
  printf '%s: %s [%s] - %s\n' "$status" "$name" "$category" "$reason"
  [[ "$status" != "fail" ]] || failures=$((failures + 1))
}

record_command_step() {
  local name="$1"
  shift
  local log_file="$evidence_dir/steps/$name.log"
  local started_at ended_at status category reason exit_code command
  printf -v command '%q ' "$@"
  command="${command% }"
  started_at="$(iso_now)"
  set +e
  if [[ "$timeout_seconds" -gt 0 ]]; then
    timeout "$timeout_seconds" "$@" >"$log_file" 2>&1
  else
    "$@" >"$log_file" 2>&1
  fi
  exit_code=$?
  set -e
  ended_at="$(iso_now)"
  if [[ "$exit_code" -eq 0 ]]; then
    status=pass
    category=ok
    reason="command completed successfully"
  elif [[ "$exit_code" -eq 124 ]]; then
    status=fail
    category=timeout
    reason="command timed out after ${timeout_seconds}s"
  else
    status=fail
    category=validation_failed
    reason="command exited with status $exit_code"
  fi
  {
    printf '\nstep: %s\nstatus: %s\ncategory: %s\nreason: %s\ncommand: %s\nexitCode: %s\nstartedAt: %s\nendedAt: %s\n' \
      "$name" "$status" "$category" "$reason" "$command" "$exit_code" "$started_at" "$ended_at"
  } >>"$log_file"
  printf '%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\n' \
    "$name" "$status" "$category" "$reason" "$log_file" "$command" "$started_at" "$ended_at" "$exit_code" >>"$steps_tsv"
  printf '%s: %s [%s] - %s\n' "$status" "$name" "$category" "$reason"
  [[ "$status" != "fail" ]] || failures=$((failures + 1))
  return "$exit_code"
}

compose_args() {
  printf -- '-f\n%s/docker-compose.yml\n-f\n%s/docker-compose.dev.yml\n' "$AKKSTACK_STACK_DIR" "$AKKSTACK_STACK_DIR"
}

docker_command_available() {
  command -v -- "${docker_cmd[0]}" >/dev/null 2>&1
}

run_docker() {
  "${docker_cmd[@]}" "$@"
}

port_is_listening() {
  local port="$1"
  python3 - "$port" <<'PY'
import socket, sys

port = int(sys.argv[1])
sock = socket.socket()
sock.settimeout(0.25)
try:
    sys.exit(0 if sock.connect_ex(("127.0.0.1", port)) == 0 else 1)
finally:
    sock.close()
PY
}

compose_port_matches() {
  local service="$1" container_port="$2" expected_port="$3" line actual_port

  [[ -n "$service" && -n "$container_port" ]] || return 1
  docker_command_available || return 1

  mapfile -t compose_flags < <(compose_args)
  while IFS= read -r line; do
    actual_port="${line##*:}"
    if [[ "$actual_port" == "$expected_port" ]]; then
      return 0
    fi
  done < <(run_docker compose "${compose_flags[@]}" port "$service" "$container_port" 2>/dev/null || true)

  return 1
}

port_in_gameplay_list() {
  local expected_port="$1" line _name port _service _container_port

  while IFS=$'\t' read -r _name port _service _container_port; do
    [[ -n "$port" ]] || continue
    if [[ "$port" == "$expected_port" ]]; then
      return 0
    fi
  done < <(json_host_ports_tsv "$request_file" gameplay)

  return 1
}

if [[ "$profile" != "preflight" ]]; then
  validate_args=("$repo_root/scripts/validate.sh" --stack "$AKKSTACK_STACK_ROLE")
  [[ "$dry_run" == "false" ]] || validate_args+=(--dry-run)
  case "$profile" in
    safe)
      validate_args+=(safe)
      record_command_step safe "${validate_args[@]}" || true
      ;;
    tier3-harness)
      validate_args+=(tier3-harness)
      record_command_step tier3_harness "${validate_args[@]}" || true
      ;;
  esac
  write_result
  printf 'result: %s\n' "$evidence_dir/result.json"
  if [[ "$failures" -gt 0 ]]; then
    printf '%s failed with %s issue(s).\n' "$profile" "$failures"
    exit 1
  fi
  printf '%s passed.\n' "$profile"
  exit 0
fi

selected_path="$AKKSTACK_STACK_DIR"
record_step stack_selection pass ok "selected $AKKSTACK_STACK_ROLE stack" \
  "stack role: $AKKSTACK_STACK_ROLE" \
  "stack path: $selected_path" \
  "path source: $AKKSTACK_PATH_SOURCE"

if [[ "$AKKSTACK_STACK_ROLE" == "validation" && -e "$AKKSTACK_GAMEPLAY_DEFAULT_DIR" && "$selected_path" == "$AKKSTACK_GAMEPLAY_DEFAULT_RESOLVED" ]]; then
  record_step stack_separation fail unsafe_gameplay_target "validation profile selected the gameplay stack"
else
  record_step stack_separation pass ok "validation and gameplay stack paths are distinct or gameplay stack is absent" \
    "validation default: $AKKSTACK_VALIDATION_DEFAULT_RESOLVED" \
    "gameplay default: $AKKSTACK_GAMEPLAY_DEFAULT_RESOLVED"
fi

if [[ -d "$selected_path" ]]; then
  record_step stack_directory pass ok "AkkStack directory exists"
else
  record_step stack_directory fail missing_validation_stack "AkkStack directory is missing: $selected_path"
fi

missing_compose=()
for compose in docker-compose.yml docker-compose.dev.yml; do
  [[ -f "$selected_path/$compose" ]] || missing_compose+=("$compose")
done
if [[ "${#missing_compose[@]}" -eq 0 ]]; then
  record_step compose_files pass ok "canonical Compose files are present" \
    "docker-compose.yml" "docker-compose.dev.yml"
else
  record_step compose_files fail missing_compose_files "missing Compose files: ${missing_compose[*]}"
fi

if [[ -f "$selected_path/.env" ]]; then
  record_step env_file pass ok ".env exists (contents intentionally not printed)"
else
  record_step env_file fail missing_env "AkkStack .env is missing"
fi

code_path="$selected_path/code"
if [[ -e "$code_path" ]]; then
  resolved_code="$(akkstack_resolve_path "$code_path")"
  if [[ "$resolved_code" == "$repo_root" ]]; then
    record_step checkout pass ok "AkkStack code points at requested checkout" "resolved code: $resolved_code"
  else
    record_step checkout fail wrong_checkout "AkkStack code points at a different checkout" \
      "resolved code: $resolved_code" "expected: $repo_root"
  fi
else
  record_step checkout fail wrong_checkout "AkkStack code path is missing"
fi

if [[ -n "$expected_commit" ]]; then
  actual_commit="$(git -C "$repo_root" rev-parse HEAD 2>/dev/null || true)"
  if [[ "$actual_commit" == "$expected_commit" ]]; then
    record_step repo_commit pass ok "requested commit is checked out" "commit: $actual_commit"
  else
    record_step repo_commit fail wrong_commit "requested commit is not checked out" \
      "actual: ${actual_commit:-unknown}" "expected: $expected_commit"
  fi
else
  record_step repo_commit skip not_requested "request did not pin a commit"
fi

if [[ "$docker_required" == "true" ]]; then
  if docker_command_available; then
    if run_docker info >/dev/null 2>&1; then
      record_step docker pass ok "Docker CLI and daemon are available" "docker command: ${docker_cmd[0]}"
    else
      record_step docker fail missing_docker "Docker CLI exists but daemon is unavailable"
    fi
  else
    record_step docker fail missing_docker "Docker CLI is not available"
  fi
else
  record_step docker skip not_required "request marked Docker availability as not required"
fi

host_port_details=()
host_port_conflicts=()
while IFS=$'\t' read -r port_name host_port service container_port; do
  [[ -n "$host_port" ]] || continue
  if port_in_gameplay_list "$host_port"; then
    host_port_conflicts+=("$port_name port $host_port conflicts with gameplay port $host_port")
    continue
  fi

  if port_is_listening "$host_port"; then
    if compose_port_matches "$service" "$container_port" "$host_port"; then
      host_port_details+=("$port_name port $host_port is already owned by selected validation Compose service $service")
    else
      host_port_conflicts+=("$port_name port $host_port already has a listener outside the selected validation Compose service")
    fi
  else
    host_port_details+=("$port_name port $host_port has no conflicting listener")
  fi
done < <(json_host_ports_tsv "$request_file" validation)

if [[ "${#host_port_conflicts[@]}" -gt 0 ]]; then
  record_step host_ports fail host_port_conflict "${host_port_conflicts[*]}" "${host_port_details[@]}"
elif [[ "${#host_port_details[@]}" -gt 0 ]]; then
  record_step host_ports pass ok "expected validation host ports have no conflicts" "${host_port_details[@]}"
else
  record_step host_ports skip not_requested "request did not define expected validation host ports"
fi

if [[ "$database_required" == "true" ]]; then
  if docker_command_available && [[ -f "$selected_path/docker-compose.yml" && -f "$selected_path/docker-compose.dev.yml" ]]; then
    mapfile -t compose_flags < <(compose_args)
    if (cd "$selected_path" && run_docker compose "${compose_flags[@]}" exec -T mariadb sh -lc 'MYSQL_PWD="${MYSQL_PASSWORD:-${MYSQL_ROOT_PASSWORD:-}}" mysqladmin -u"${MYSQL_USER:-root}" ping' >/dev/null 2>&1); then
      record_step mariadb pass ok "MariaDB responds to mysqladmin ping"
    else
      record_step mariadb fail db_unreachable "MariaDB is not reachable through the validation Compose project"
    fi
  else
    record_step mariadb fail db_unreachable "Docker or Compose files are unavailable for MariaDB check"
  fi
else
  record_step mariadb skip follow_up_profile "DB-backed readiness is deferred until a live validation stack is available"
fi

if [[ "$database_content_required" == "true" ]]; then
  if docker_command_available && [[ -f "$selected_path/docker-compose.yml" && -f "$selected_path/docker-compose.dev.yml" ]]; then
    mapfile -t compose_flags < <(compose_args)
    if (cd "$selected_path" && run_docker compose "${compose_flags[@]}" exec -T mariadb sh -lc 'counts="$(MYSQL_PWD="${MYSQL_PASSWORD:-${MYSQL_ROOT_PASSWORD:-}}" mysql -u"${MYSQL_USER:-root}" "${MYSQL_DATABASE:-peq}" --batch --skip-column-names -e "SELECT (SELECT COUNT(*) FROM zone), (SELECT COUNT(*) FROM npc_types);")" && set -- $counts && [ "${1:-0}" -gt 0 ] && [ "${2:-0}" -gt 0 ]' >/dev/null 2>&1); then
      record_step db_content pass ok "PEQ zone and npc_types tables contain rows"
    else
      record_step db_content fail missing_db_content "PEQ/content readiness query failed or returned empty core tables"
    fi
  else
    record_step db_content fail missing_db_content "Docker or Compose files are unavailable for DB content check"
  fi
else
  record_step db_content skip follow_up_profile "DB content readiness is deferred until a live validation stack is available"
fi

if [[ "$assets_required" == "true" ]]; then
  if [[ -x "$repo_root/build/bin/world" || -x "$repo_root/build/world" || -x "$selected_path/server/bin/world" ]]; then
    record_step runtime_assets pass ok "required runtime binaries are present"
  else
    record_step runtime_assets fail missing_runtime_assets "runtime binaries are missing; run the build profile before DB-backed validation"
  fi
else
  record_step runtime_assets skip follow_up_profile "build/runtime asset check is deferred to the build validation profile"
fi

write_result

printf 'result: %s\n' "$evidence_dir/result.json"
if [[ "$failures" -gt 0 ]]; then
  printf 'preflight failed with %s issue(s).\n' "$failures"
  exit 1
fi
printf 'preflight passed.\n'

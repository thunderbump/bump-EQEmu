#!/usr/bin/env bash
set -euo pipefail

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
repo_default="$(cd "$script_dir/.." && pwd)"
source "$script_dir/lib/akkstack-routing.sh"

usage() {
  cat <<'USAGE'
Usage: scripts/validation-worker.sh run --request <request.json>

Runs a local Validation Worker request and writes structured evidence. The first
implemented profile is preflight, which checks whether the selected validation
AkkStack can safely run automation.
USAGE
}

json_get() {
  local request="$1" expr="$2" default_value="${3:-}"
  python3 - "$request" "$expr" "$default_value" <<'PY'
import json, sys
path, expr, default = sys.argv[1:4]
with open(path, encoding="utf-8") as f:
    data = json.load(f)
cur = data
for part in expr.split('.'):
    if not isinstance(cur, dict) or part not in cur:
        print(default)
        sys.exit(0)
    cur = cur[part]
if cur is None:
    print(default)
elif isinstance(cur, bool):
    print("true" if cur else "false")
else:
    print(cur)
PY
}

json_bool() {
  local value="$1" default_value="$2"
  case "$value" in
    true|1|yes) printf 'true\n' ;;
    false|0|no) printf 'false\n' ;;
    "") printf '%s\n' "$default_value" ;;
    *) printf 'error: expected boolean value, got %s\n' "$value" >&2; exit 2 ;;
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
[[ "$profile" == "preflight" ]] || { printf 'error: unsupported validation profile: %s\n' "${profile:-<empty>}" >&2; exit 2; }

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

mkdir -p "$evidence_dir/steps"
steps_tsv="$evidence_dir/steps.tsv"
: >"$steps_tsv"
failures=0

if [[ -n "$stack_path" ]]; then
  [[ "$stack_path" == /* ]] || stack_path="$request_dir/$stack_path"
  AKKSTACK_DIR="$stack_path" akkstack_init_routing "$repo_root" "$role"
else
  akkstack_init_routing "$repo_root" "$role"
fi

record_step() {
  local name="$1" status="$2" category="$3" reason="$4"
  local log_file="$evidence_dir/steps/$name.log"
  shift 4 || true
  {
    printf 'step: %s\nstatus: %s\ncategory: %s\nreason: %s\n' "$name" "$status" "$category" "$reason"
    if [[ "$#" -gt 0 ]]; then
      printf '%s\n' "$@"
    fi
  } >"$log_file"
  printf '%s\t%s\t%s\t%s\t%s\n' "$name" "$status" "$category" "$reason" "$log_file" >>"$steps_tsv"
  printf '%s: %s [%s] - %s\n' "$status" "$name" "$category" "$reason"
  [[ "$status" != "fail" ]] || failures=$((failures + 1))
}

compose_args() {
  printf -- '-f\n%s/docker-compose.yml\n-f\n%s/docker-compose.dev.yml\n' "$AKKSTACK_STACK_DIR" "$AKKSTACK_STACK_DIR"
}

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
  if command -v docker >/dev/null 2>&1; then
    if docker info >/dev/null 2>&1; then
      record_step docker pass ok "Docker CLI and daemon are available"
    else
      record_step docker fail missing_docker "Docker CLI exists but daemon is unavailable"
    fi
  else
    record_step docker fail missing_docker "Docker CLI is not available"
  fi
else
  record_step docker skip not_required "request marked Docker availability as not required"
fi

if [[ "$database_required" == "true" ]]; then
  if command -v docker >/dev/null 2>&1 && [[ -f "$selected_path/docker-compose.yml" && -f "$selected_path/docker-compose.dev.yml" ]]; then
    mapfile -t compose_flags < <(compose_args)
    if (cd "$selected_path" && docker compose "${compose_flags[@]}" exec -T mariadb mysqladmin ping >/dev/null 2>&1); then
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

if [[ "$assets_required" == "true" ]]; then
  if [[ -x "$repo_root/build/bin/world" || -x "$repo_root/build/world" ]]; then
    record_step runtime_assets pass ok "required runtime binaries are present"
  else
    record_step runtime_assets fail missing_runtime_assets "runtime binaries are missing; run the build profile before DB-backed validation"
  fi
else
  record_step runtime_assets skip follow_up_profile "build/runtime asset check is deferred to the build validation profile"
fi

python3 - "$steps_tsv" "$evidence_dir/result.json" "$profile" "$repo_root" "$selected_path" "$failures" <<'PY'
import json, sys
steps_path, result_path, profile, repo, stack, failures = sys.argv[1:7]
steps = []
with open(steps_path, encoding="utf-8") as f:
    for line in f:
        name, status, category, reason, log_file = line.rstrip("\n").split("\t", 4)
        steps.append({"name": name, "status": status, "category": category, "reason": reason, "log": log_file})
result = {
    "profile": profile,
    "status": "pass" if int(failures) == 0 else "fail",
    "failureCount": int(failures),
    "repo": repo,
    "stackPath": stack,
    "steps": steps,
}
with open(result_path, "w", encoding="utf-8") as f:
    json.dump(result, f, indent=2)
    f.write("\n")
PY

printf 'result: %s\n' "$evidence_dir/result.json"
if [[ "$failures" -gt 0 ]]; then
  printf 'preflight failed with %s issue(s).\n' "$failures"
  exit 1
fi
printf 'preflight passed.\n'

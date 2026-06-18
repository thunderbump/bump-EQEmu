#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
tmp_root="$(mktemp -d)"

cleanup() {
  rm -rf "$tmp_root"
}
trap cleanup EXIT

failures=0
assertion_failed=0

fail() {
  printf 'FAIL: %s\n' "$*" >&2
  failures=$((failures + 1))
}

assert_contains() {
  local haystack="$1"
  local needle="$2"

  if [[ "$haystack" != *"$needle"* ]]; then
    printf 'Expected output to contain:\n%s\n\nActual output:\n%s\n' "$needle" "$haystack" >&2
    assertion_failed=1
  fi
}

assert_not_contains() {
  local haystack="$1"
  local needle="$2"

  if [[ "$haystack" == *"$needle"* ]]; then
    printf 'Expected output not to contain:\n%s\n\nActual output:\n%s\n' "$needle" "$haystack" >&2
    assertion_failed=1
  fi
}

capture_run() {
  local -n status_ref="$1"
  local -n output_ref="$2"
  shift 2

  set +e
  output_ref="$("$@" 2>&1)"
  status_ref=$?
  set -e
}

make_stack() {
  local stack_dir="$1"
  local checkout_dir="$2"

  mkdir -p "$stack_dir"
  printf 'ENV=development\n' >"$stack_dir/.env"
  ln -s "$checkout_dir" "$stack_dir/code"
  touch "$stack_dir/docker-compose.yml"
  touch "$stack_dir/docker-compose.dev.yml"
}

make_fixture() {
  local -n fixture_repo_ref="$1"
  local -n fixture_parent_ref="$2"

  fixture_parent_ref="$(mktemp -d "$tmp_root/fixture.XXXXXX")"
  fixture_repo_ref="$fixture_parent_ref/bump-EQEmu"
  mkdir -p "$fixture_repo_ref"
  cp -R "$repo_root/scripts" "$fixture_repo_ref/"

  make_stack "$fixture_parent_ref/bump-akk-stack-validation" "$fixture_repo_ref"
  make_stack "$fixture_parent_ref/bump-akk-stack" "$fixture_repo_ref"
}

run_test() {
  local name="$1"
  shift

  assertion_failed=0
  if "$@" && [[ "$assertion_failed" -eq 0 ]]; then
    printf 'ok - %s\n' "$name"
  else
    fail "$name"
  fi
}

test_preflight_defaults_to_validation() {
  local fixture_repo fixture_parent status output
  make_fixture fixture_repo fixture_parent

  capture_run status output "$fixture_repo/scripts/check-akkstack-contract.sh"

  [[ "$status" -eq 0 ]] || return 1
  assert_contains "$output" "stack role: validation"
  assert_contains "$output" "stack path: $fixture_parent/bump-akk-stack-validation"
  assert_contains "$output" "Preflight passed."
}

test_runtime_proof_defaults_to_gameplay() {
  local fixture_repo fixture_parent status output
  make_fixture fixture_repo fixture_parent

  capture_run status output "$fixture_repo/scripts/start-akkstack-runtime-proof.sh" --dry-run

  [[ "$status" -eq 0 ]] || return 1
  assert_contains "$output" "stack role: gameplay"
  assert_contains "$output" "stack path: $fixture_parent/bump-akk-stack"
  assert_contains "$output" "compose files:"
  assert_contains "$output" "docker-compose.yml"
  assert_contains "$output" "docker-compose.dev.yml"
  assert_contains "$output" "services:"
  assert_contains "$output" "mariadb"
  assert_contains "$output" "eqemu-server"
  assert_contains "$output" "launcher/runtime actions:"
  assert_contains "$output" "restart Spire launcher-managed runtime"
  assert_contains "$output" "fallback supervised runtime"
  assert_contains "$output" "wait for stable zone capacity"
  assert_contains "$output" "action:"
}

test_explicit_roles_are_accepted_by_wrappers() {
  local fixture_repo fixture_parent status output
  make_fixture fixture_repo fixture_parent

  capture_run status output "$fixture_repo/scripts/check-akkstack-contract.sh" --stack gameplay --dry-run
  [[ "$status" -eq 0 ]] || return 1
  assert_contains "$output" "stack role: gameplay"

  capture_run status output "$fixture_repo/scripts/validate.sh" --stack validation --dry-run tier1
  [[ "$status" -eq 0 ]] || return 1
  assert_contains "$output" "stack role: validation"

  capture_run status output "$fixture_repo/scripts/validate.sh" --stack gameplay --dry-run tier1
  [[ "$status" -eq 0 ]] || return 1
  assert_contains "$output" "stack role: gameplay"

  capture_run status output "$fixture_repo/scripts/smoke-zone-harness.sh" --stack validation --dry-run
  [[ "$status" -eq 0 ]] || return 1
  assert_contains "$output" "stack role: validation"

  capture_run status output "$fixture_repo/scripts/smoke-zone-harness.sh" --stack gameplay --dry-run
  [[ "$status" -eq 0 ]] || return 1
  assert_contains "$output" "stack role: gameplay"

  capture_run status output "$fixture_repo/scripts/start-akkstack-runtime-proof.sh" --stack validation --dry-run
  [[ "$status" -eq 0 ]] || return 1
  assert_contains "$output" "stack role: validation"
  assert_contains "$output" "runtime-proof selection: validation (non-default; default is gameplay)"

  capture_run status output "$fixture_repo/scripts/start-akkstack-runtime-proof.sh" --stack gameplay --dry-run
  [[ "$status" -eq 0 ]] || return 1
  assert_contains "$output" "stack role: gameplay"
  assert_contains "$output" "runtime-proof selection: gameplay (default)"
}

test_invalid_role_fails() {
  local fixture_repo fixture_parent status output
  make_fixture fixture_repo fixture_parent

  capture_run status output "$fixture_repo/scripts/validate.sh" --stack staging --dry-run tier1

  [[ "$status" -eq 2 ]] || return 1
  assert_contains "$output" "invalid --stack role"
}

test_custom_path_override() {
  local fixture_repo fixture_parent custom_stack status output
  make_fixture fixture_repo fixture_parent
  custom_stack="$fixture_parent/custom-akk-stack"
  make_stack "$custom_stack" "$fixture_repo"

  capture_run status output env AKKSTACK_DIR="$custom_stack" "$fixture_repo/scripts/check-akkstack-contract.sh" --stack gameplay

  [[ "$status" -eq 0 ]] || return 1
  assert_contains "$output" "stack role: gameplay"
  assert_contains "$output" "stack path: $custom_stack"
  assert_contains "$output" "path source: AKKSTACK_DIR"
  assert_contains "$output" "Preflight passed."
}

test_missing_default_validation_fails_clearly() {
  local fixture_repo fixture_parent status output
  make_fixture fixture_repo fixture_parent
  rm -rf "$fixture_parent/bump-akk-stack-validation"

  capture_run status output "$fixture_repo/scripts/check-akkstack-contract.sh"

  [[ "$status" -eq 1 ]] || return 1
  assert_contains "$output" "default validation AkkStack directory is missing"
  assert_not_contains "$output" "$fixture_parent/bump-akk-stack "
}

test_default_roles_must_not_resolve_to_same_directory() {
  local fixture_repo fixture_parent status output
  make_fixture fixture_repo fixture_parent
  rm -rf "$fixture_parent/bump-akk-stack-validation"
  ln -s "$fixture_parent/bump-akk-stack" "$fixture_parent/bump-akk-stack-validation"

  capture_run status output "$fixture_repo/scripts/check-akkstack-contract.sh" --dry-run

  [[ "$status" -eq 1 ]] || return 1
  assert_contains "$output" "default validation and gameplay AkkStack paths must not resolve to the same directory"
}

test_validation_commands_warn_on_gameplay_stack() {
  local fixture_repo fixture_parent status output
  make_fixture fixture_repo fixture_parent

  capture_run status output "$fixture_repo/scripts/validate.sh" --stack gameplay --dry-run tier1
  [[ "$status" -eq 0 ]] || return 1
  assert_contains "$output" "WARNING:"
  assert_contains "$output" "persistent gameplay data"

  capture_run status output "$fixture_repo/scripts/smoke-zone-harness.sh" --stack gameplay --dry-run
  [[ "$status" -eq 0 ]] || return 1
  assert_contains "$output" "WARNING:"
  assert_contains "$output" "persistent gameplay data"
}

test_zone_harness_dry_run_describes_stable_db_and_portless_server() {
  local fixture_repo fixture_parent fake_bin marker status output
  make_fixture fixture_repo fixture_parent
  fake_bin="$(mktemp -d "$tmp_root/fake-bin.XXXXXX")"
  marker="$fake_bin/docker-compose-called"

  printf '#!/usr/bin/env bash\n: >"%s"\nexit 99\n' "$marker" >"$fake_bin/docker-compose"
  chmod +x "$fake_bin/docker-compose"

  capture_run status output env PATH="$fake_bin:$PATH" "$fixture_repo/scripts/smoke-zone-harness.sh" --stack validation --dry-run

  [[ "$status" -eq 0 ]] || return 1
  [[ ! -e "$marker" ]] || return 1
  assert_contains "$output" "stack role: validation"
  assert_contains "$output" "docker-compose.yml"
  assert_contains "$output" "docker-compose.dev.yml"
  assert_contains "$output" "<generated eqemu-server portless override>"
  assert_contains "$output" "canonical Compose"
  assert_contains "$output" "--no-recreate"
  assert_contains "$output" "one-off eqemu-server container"
  assert_contains "$output" "only eqemu-server host ports disabled"
}

test_validate_tier3_harness_delegates_to_smoke_script() {
  local fixture_repo fixture_parent calls status output
  make_fixture fixture_repo fixture_parent
  calls="$tmp_root/smoke-zone-harness.args"

  printf '#!/usr/bin/env bash\nprintf "%%s\\n" "$*" >"$SMOKE_ZONE_HARNESS_ARGS"\nprintf "smoke delegated\\n"\n' >"$fixture_repo/scripts/smoke-zone-harness.sh"
  chmod +x "$fixture_repo/scripts/smoke-zone-harness.sh"

  capture_run status output env SMOKE_ZONE_HARNESS_ARGS="$calls" "$fixture_repo/scripts/validate.sh" --stack gameplay --dry-run tier3-harness

  [[ "$status" -eq 0 ]] || return 1
  assert_contains "$output" "smoke delegated"
  [[ -f "$calls" ]] || return 1
  [[ "$(cat "$calls")" == "--stack gameplay --dry-run" ]] || return 1
}

test_help_mentions_stack_and_dry_run() {
  local fixture_repo fixture_parent status output script
  make_fixture fixture_repo fixture_parent

  for script in check-akkstack-contract.sh validate.sh smoke-zone-harness.sh start-akkstack-runtime-proof.sh; do
    capture_run status output "$fixture_repo/scripts/$script" --help
    [[ "$status" -eq 0 ]] || return 1
    assert_contains "$output" "--stack <validation|gameplay>"
    assert_contains "$output" "--dry-run"
  done
}

test_dry_run_prints_route_and_skips_docker() {
  local fixture_repo fixture_parent fake_bin marker status output
  make_fixture fixture_repo fixture_parent
  fake_bin="$(mktemp -d "$tmp_root/fake-bin.XXXXXX")"
  marker="$fake_bin/docker-compose-called"

  printf '#!/usr/bin/env bash\n: >"%s"\nexit 99\n' "$marker" >"$fake_bin/docker-compose"
  chmod +x "$fake_bin/docker-compose"

  capture_run status output env PATH="$fake_bin:$PATH" "$fixture_repo/scripts/validate.sh" --dry-run tier1

  [[ "$status" -eq 0 ]] || return 1
  [[ ! -e "$marker" ]] || return 1
  assert_contains "$output" "stack role: validation"
  assert_contains "$output" "stack path: $fixture_parent/bump-akk-stack-validation"
  assert_contains "$output" "compose files:"
  assert_contains "$output" "action:"

  rm -f "$marker"
  capture_run status output env PATH="$fake_bin:$PATH" "$fixture_repo/scripts/start-akkstack-runtime-proof.sh" --stack validation --dry-run

  [[ "$status" -eq 0 ]] || return 1
  [[ ! -e "$marker" ]] || return 1
  assert_contains "$output" "stack role: validation"
  assert_contains "$output" "stack path: $fixture_parent/bump-akk-stack-validation"
  assert_contains "$output" "runtime-proof selection: validation (non-default; default is gameplay)"
  assert_contains "$output" "services:"
  assert_contains "$output" "launcher/runtime actions:"
}

test_tier2_readonly_dry_run_describes_single_one_off_container() {
  local fixture_repo fixture_parent fake_bin marker status output
  make_fixture fixture_repo fixture_parent
  fake_bin="$(mktemp -d "$tmp_root/fake-bin.XXXXXX")"
  marker="$fake_bin/docker-compose-called"

  printf '#!/usr/bin/env bash\n: >"%s"\nexit 99\n' "$marker" >"$fake_bin/docker-compose"
  chmod +x "$fake_bin/docker-compose"

  capture_run status output env PATH="$fake_bin:$PATH" "$fixture_repo/scripts/validate.sh" --dry-run tier2-readonly

  [[ "$status" -eq 0 ]] || return 1
  [[ ! -e "$marker" ]] || return 1
  assert_contains "$output" "start or verify MariaDB with canonical Compose (--no-recreate)"
  assert_contains "$output" "single one-off eqemu-server container"
  assert_contains "$output" "tests:npc-handins and tests:npc-handins-multiquest as separate zone CLI processes"
}

test_safe_dry_run_keeps_readonly_composition() {
  local fixture_repo fixture_parent status output
  make_fixture fixture_repo fixture_parent

  capture_run status output "$fixture_repo/scripts/validate.sh" --dry-run safe

  [[ "$status" -eq 0 ]] || return 1
  assert_contains "$output" "would run preflight, tier1, and tier2-readonly"
  assert_not_contains "$output" "Zone Harness"
  assert_not_contains "$output" "tests:databuckets"
  assert_not_contains "$output" "tests:zone-state"
}

write_validation_worker_request() {
  local request_file="$1" repo_path="$2" stack_path="$3" evidence_dir="$4"
  local checks_json="${5:-}"
  local tools_json="${6:-}"

  if [[ -z "$checks_json" ]]; then
    checks_json='{
    "dockerRequired": false,
    "databaseRequired": false,
    "assetsRequired": false
  }'
  fi
  if [[ -z "$tools_json" ]]; then
    tools_json='{}'
  fi

  cat >"$request_file" <<EOF
{
  "profile": "preflight",
  "repo": { "path": "$repo_path" },
  "stack": { "role": "validation", "path": "$stack_path" },
  "evidenceDir": "$evidence_dir",
  "tools": $tools_json,
  "checks": $checks_json
}
EOF
}

make_fake_docker() {
  local fake_bin="$1"

  mkdir -p "$fake_bin"
  cat >"$fake_bin/docker" <<'EOF'
#!/usr/bin/env bash
set -euo pipefail

printf '%s\n' "$*" >>"${FAKE_DOCKER_LOG:?}"

if [[ "${1:-}" == "info" ]]; then
  exit 0
fi

if [[ "${1:-}" == "compose" ]]; then
  shift
  args=" $* "
  case "$args" in
    *" exec -T mariadb "*mysqladmin*" ping"*)
      printf 'mysqld is alive\n'
      exit 0
      ;;
    *" exec -T mariadb "*mysql*"SELECT COUNT(*) FROM zone"*)
      printf 'ready\n'
      exit 0
      ;;
    *" port mariadb 3306 "*|*" port mariadb 3306")
      printf '0.0.0.0:13306\n'
      exit 0
      ;;
  esac
fi

printf 'unexpected fake docker invocation: %s\n' "$*" >&2
exit 99
EOF
  chmod +x "$fake_bin/docker"
}

make_runtime_assets() {
  local checkout_dir="$1"

  mkdir -p "$checkout_dir/build/bin"
  printf '#!/usr/bin/env bash\nexit 0\n' >"$checkout_dir/build/bin/world"
  chmod +x "$checkout_dir/build/bin/world"
}

test_validation_worker_preflight_writes_structured_evidence() {
  local fixture_repo fixture_parent request evidence status output
  make_fixture fixture_repo fixture_parent
  printf 'SUPER_SECRET_SHOULD_NOT_PRINT=1\n' >>"$fixture_parent/bump-akk-stack-validation/.env"
  request="$fixture_parent/preflight-request.json"
  evidence="$fixture_parent/evidence/preflight"
  write_validation_worker_request "$request" "$fixture_repo" "$fixture_parent/bump-akk-stack-validation" "$evidence"

  capture_run status output "$fixture_repo/scripts/validation-worker.sh" run --request "$request"

  [[ "$status" -eq 0 ]] || return 1
  [[ -f "$evidence/result.json" ]] || return 1
  assert_contains "$output" "pass: stack_selection"
  assert_contains "$output" "skip: mariadb"
  assert_contains "$(cat "$evidence/result.json")" '"status": "pass"'
  assert_contains "$(cat "$evidence/result.json")" '"name": "env_file"'
  assert_not_contains "$output" "SUPER_SECRET_SHOULD_NOT_PRINT"
  assert_not_contains "$(cat "$evidence/steps/env_file.log")" "SUPER_SECRET_SHOULD_NOT_PRINT"
}

test_validation_worker_preflight_actively_checks_docker_db_ports_and_assets() {
  local fixture_repo fixture_parent fake_bin docker_log request evidence status output result checks tools
  make_fixture fixture_repo fixture_parent
  make_runtime_assets "$fixture_repo"
  fake_bin="$fixture_parent/fake-bin"
  docker_log="$fixture_parent/fake-docker.log"
  make_fake_docker "$fake_bin"
  request="$fixture_parent/preflight-request.json"
  evidence="$fixture_parent/evidence/preflight"
  checks='{
    "dockerRequired": true,
    "databaseRequired": true,
    "databaseContentRequired": true,
    "assetsRequired": true,
    "hostPorts": {
      "gameplay": [3306],
      "validation": [
        { "name": "validation_mariadb", "port": 13306, "service": "mariadb", "containerPort": 3306 }
      ]
    }
  }'
  tools="{ \"dockerCommand\": \"$fake_bin/docker\" }"
  write_validation_worker_request "$request" "$fixture_repo" "$fixture_parent/bump-akk-stack-validation" "$evidence" "$checks" "$tools"

  capture_run status output env FAKE_DOCKER_LOG="$docker_log" "$fixture_repo/scripts/validation-worker.sh" run --request "$request"

  [[ "$status" -eq 0 ]] || return 1
  result="$(cat "$evidence/result.json")"
  assert_contains "$output" "pass: docker"
  assert_contains "$output" "pass: host_ports"
  assert_contains "$output" "pass: mariadb"
  assert_contains "$output" "pass: db_content"
  assert_contains "$output" "pass: runtime_assets"
  assert_contains "$result" '"name": "host_ports"'
  assert_contains "$result" '"name": "db_content"'
  assert_contains "$(cat "$docker_log")" "info"
  assert_contains "$(cat "$docker_log")" "mysqladmin"
  assert_contains "$(cat "$docker_log")" "SELECT COUNT(*) FROM zone"
}

test_validation_worker_preflight_reports_host_port_conflict_category() {
  local fixture_repo fixture_parent request evidence status output result checks
  make_fixture fixture_repo fixture_parent
  request="$fixture_parent/preflight-request.json"
  evidence="$fixture_parent/evidence/preflight"
  checks='{
    "dockerRequired": false,
    "databaseRequired": false,
    "assetsRequired": false,
    "hostPorts": {
      "gameplay": [3306],
      "validation": [
        { "name": "validation_mariadb", "port": 3306, "service": "mariadb", "containerPort": 3306 }
      ]
    }
  }'
  write_validation_worker_request "$request" "$fixture_repo" "$fixture_parent/bump-akk-stack-validation" "$evidence" "$checks"

  capture_run status output "$fixture_repo/scripts/validation-worker.sh" run --request "$request"

  [[ "$status" -eq 1 ]] || return 1
  result="$(cat "$evidence/result.json")"
  assert_contains "$output" "host_port_conflict"
  assert_contains "$result" '"category": "host_port_conflict"'
  assert_contains "$result" "conflicts with gameplay port"
}

test_validation_worker_preflight_reports_listener_port_conflict_category() {
  local fixture_repo fixture_parent request evidence status output result checks port_file listener_pid port
  make_fixture fixture_repo fixture_parent
  port_file="$fixture_parent/listener.port"
  python3 - "$port_file" <<'PY' &
import socket
import sys
import time

sock = socket.socket()
sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
sock.bind(("127.0.0.1", 0))
sock.listen(1)
with open(sys.argv[1], "w", encoding="utf-8") as f:
    f.write(str(sock.getsockname()[1]))
    f.flush()
try:
    time.sleep(60)
finally:
    sock.close()
PY
  listener_pid=$!
  for _ in {1..50}; do
    [[ -s "$port_file" ]] && break
    sleep 0.1
  done
  [[ -s "$port_file" ]] || return 1
  port="$(cat "$port_file")"
  request="$fixture_parent/preflight-request.json"
  evidence="$fixture_parent/evidence/preflight"
  checks="{
    \"dockerRequired\": false,
    \"databaseRequired\": false,
    \"assetsRequired\": false,
    \"hostPorts\": {
      \"validation\": [
        { \"name\": \"occupied_validation_port\", \"port\": $port }
      ]
    }
  }"
  write_validation_worker_request "$request" "$fixture_repo" "$fixture_parent/bump-akk-stack-validation" "$evidence" "$checks"

  capture_run status output "$fixture_repo/scripts/validation-worker.sh" run --request "$request"
  kill "$listener_pid" 2>/dev/null || true
  wait "$listener_pid" 2>/dev/null || true

  [[ "$status" -eq 1 ]] || return 1
  result="$(cat "$evidence/result.json")"
  assert_contains "$output" "host_port_conflict"
  assert_contains "$result" '"category": "host_port_conflict"'
  assert_contains "$result" "already has a listener"
}

test_validation_worker_preflight_reports_missing_docker_category() {
  local fixture_repo fixture_parent request evidence status output result checks tools
  make_fixture fixture_repo fixture_parent
  request="$fixture_parent/preflight-request.json"
  evidence="$fixture_parent/evidence/preflight"
  checks='{
    "dockerRequired": true,
    "databaseRequired": false,
    "assetsRequired": false
  }'
  tools="{ \"dockerCommand\": \"$fixture_parent/missing-docker\" }"
  write_validation_worker_request "$request" "$fixture_repo" "$fixture_parent/bump-akk-stack-validation" "$evidence" "$checks" "$tools"

  capture_run status output "$fixture_repo/scripts/validation-worker.sh" run --request "$request"

  [[ "$status" -eq 1 ]] || return 1
  result="$(cat "$evidence/result.json")"
  assert_contains "$output" "missing_docker"
  assert_contains "$result" '"category": "missing_docker"'
}

test_validation_worker_preflight_reports_missing_stack_category() {
  local fixture_repo fixture_parent request evidence status output result
  make_fixture fixture_repo fixture_parent
  rm -rf "$fixture_parent/bump-akk-stack-validation"
  request="$fixture_parent/preflight-request.json"
  evidence="$fixture_parent/evidence/preflight"
  write_validation_worker_request "$request" "$fixture_repo" "$fixture_parent/bump-akk-stack-validation" "$evidence"

  capture_run status output "$fixture_repo/scripts/validation-worker.sh" run --request "$request"

  [[ "$status" -eq 1 ]] || return 1
  result="$(cat "$evidence/result.json")"
  assert_contains "$output" "missing_validation_stack"
  assert_contains "$result" '"status": "fail"'
  assert_contains "$result" '"category": "missing_validation_stack"'
}

test_validation_worker_preflight_reports_wrong_checkout_category() {
  local fixture_repo fixture_parent other_repo request evidence status output result
  make_fixture fixture_repo fixture_parent
  other_repo="$fixture_parent/other-EQEmu"
  mkdir -p "$other_repo"
  rm "$fixture_parent/bump-akk-stack-validation/code"
  ln -s "$other_repo" "$fixture_parent/bump-akk-stack-validation/code"
  request="$fixture_parent/preflight-request.json"
  evidence="$fixture_parent/evidence/preflight"
  write_validation_worker_request "$request" "$fixture_repo" "$fixture_parent/bump-akk-stack-validation" "$evidence"

  capture_run status output "$fixture_repo/scripts/validation-worker.sh" run --request "$request"

  [[ "$status" -eq 1 ]] || return 1
  result="$(cat "$evidence/result.json")"
  assert_contains "$output" "wrong_checkout"
  assert_contains "$result" '"category": "wrong_checkout"'
}

run_test "preflight defaults to validation" test_preflight_defaults_to_validation
run_test "runtime proof defaults to gameplay" test_runtime_proof_defaults_to_gameplay
run_test "explicit roles are accepted by wrappers" test_explicit_roles_are_accepted_by_wrappers
run_test "invalid role fails" test_invalid_role_fails
run_test "custom path override is explicit" test_custom_path_override
run_test "missing default validation fails clearly" test_missing_default_validation_fails_clearly
run_test "default role paths are distinct" test_default_roles_must_not_resolve_to_same_directory
run_test "validation commands warn on gameplay stack" test_validation_commands_warn_on_gameplay_stack
run_test "zone harness dry-run describes stable DB and portless server" test_zone_harness_dry_run_describes_stable_db_and_portless_server
run_test "validate tier3-harness delegates to smoke script" test_validate_tier3_harness_delegates_to_smoke_script
run_test "help mentions stack and dry-run" test_help_mentions_stack_and_dry_run
run_test "dry-run prints route and skips Docker" test_dry_run_prints_route_and_skips_docker
run_test "tier2-readonly dry-run describes one-off container" test_tier2_readonly_dry_run_describes_single_one_off_container
run_test "safe dry-run keeps readonly composition" test_safe_dry_run_keeps_readonly_composition
run_test "validation worker preflight writes structured evidence" test_validation_worker_preflight_writes_structured_evidence
run_test "validation worker preflight actively checks Docker DB ports and assets" test_validation_worker_preflight_actively_checks_docker_db_ports_and_assets
run_test "validation worker preflight reports host port conflict category" test_validation_worker_preflight_reports_host_port_conflict_category
run_test "validation worker preflight reports listener port conflict category" test_validation_worker_preflight_reports_listener_port_conflict_category
run_test "validation worker preflight reports missing Docker category" test_validation_worker_preflight_reports_missing_docker_category
run_test "validation worker preflight reports missing stack category" test_validation_worker_preflight_reports_missing_stack_category
run_test "validation worker preflight reports wrong checkout category" test_validation_worker_preflight_reports_wrong_checkout_category

if [[ "$failures" -gt 0 ]]; then
  exit 1
fi

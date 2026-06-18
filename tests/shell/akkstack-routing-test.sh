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

test_tier1_uses_checkout_build_ccache_dir() {
  local fixture_repo fixture_parent fake_bin calls status output args
  make_fixture fixture_repo fixture_parent
  fake_bin="$(mktemp -d "$tmp_root/fake-bin.XXXXXX")"
  calls="$fake_bin/docker-compose.args"

  printf '#!/usr/bin/env bash\nprintf "%%s\\n" "$@" >"%s"\n' "$calls" >"$fake_bin/docker-compose"
  chmod +x "$fake_bin/docker-compose"

  capture_run status output env PATH="$fake_bin:$PATH" "$fixture_repo/scripts/validate.sh" tier1

  [[ "$status" -eq 0 ]] || return 1
  [[ -f "$calls" ]] || return 1
  args="$(cat "$calls")"
  assert_contains "$args" "run"
  assert_contains "$args" "--rm"
  assert_contains "$args" "--no-deps"
  assert_contains "$args" "eqemu-server"
  assert_contains "$args" 'export CCACHE_DIR="$HOME/code/build/.ccache"'
  assert_contains "$args" 'mkdir -p "$CCACHE_DIR"'
  assert_contains "$args" "cmake --preset linux-debug"
  assert_contains "$args" "./build/bin/tests"
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

test_validation_worker_preflight_accepts_automation_schema_commit_and_evidence_dir() {
  local fixture_repo fixture_parent request evidence status output result commit
  make_fixture fixture_repo fixture_parent
  git -C "$fixture_repo" init -q
  git -C "$fixture_repo" config user.email "validation-worker@example.invalid"
  git -C "$fixture_repo" config user.name "Validation Worker Test"
  git -C "$fixture_repo" add scripts
  git -C "$fixture_repo" commit -q -m "fixture checkout"
  commit="$(git -C "$fixture_repo" rev-parse HEAD)"
  request="$fixture_parent/automation-preflight-request.json"
  evidence="$fixture_parent/evidence/automation-preflight"
  cat >"$request" <<EOF
{
  "profile": "preflight",
  "target_checkout_path": "$fixture_repo",
  "stack": { "role": "validation", "path": "$fixture_parent/bump-akk-stack-validation" },
  "evidence_dir": "$evidence",
  "commit": "$commit",
  "checks": {
    "dockerRequired": false,
    "databaseRequired": false,
    "assetsRequired": false
  }
}
EOF

  capture_run status output "$fixture_repo/scripts/validation-worker.sh" run --request "$request"

  [[ "$status" -eq 0 ]] || return 1
  [[ -f "$evidence/result.json" ]] || return 1
  result="$(cat "$evidence/result.json")"
  assert_contains "$output" "pass: repo_commit"
  assert_contains "$result" '"repo": "'"$fixture_repo"'"'
  assert_contains "$result" '"name": "repo_commit"'
  assert_contains "$result" '"category": "ok"'
}

write_validation_worker_profile_request() {
  local request_file="$1" profile="$2" repo_path="$3" stack_path="$4" evidence_dir="$5" timeout_seconds="${6:-30}"
  local lock_wait_seconds="${7:-30}"

  cat >"$request_file" <<EOF
{
  "profile": "$profile",
  "repo": { "path": "$repo_path" },
  "stack": { "role": "validation", "path": "$stack_path" },
  "evidenceDir": "$evidence_dir",
  "dryRun": true,
  "timeoutSeconds": $timeout_seconds,
  "lockWaitSeconds": $lock_wait_seconds
}
EOF
}

test_validation_worker_safe_profile_records_command_evidence() {
  local fixture_repo fixture_parent request evidence status output result
  make_fixture fixture_repo fixture_parent
  request="$fixture_parent/safe-request.json"
  evidence="$fixture_parent/evidence/safe"
  write_validation_worker_profile_request "$request" safe "$fixture_repo" "$fixture_parent/bump-akk-stack-validation" "$evidence"

  capture_run status output "$fixture_repo/scripts/validation-worker.sh" run --request "$request"

  [[ "$status" -eq 0 ]] || return 1
  result="$(cat "$evidence/result.json")"
  assert_contains "$output" "pass: safe"
  assert_contains "$result" '"profile": "safe"'
  assert_contains "$result" '"databaseBehavior": "read-mostly"'
  assert_contains "$result" '"command":'
  assert_contains "$result" '"startedAt":'
  assert_contains "$result" '"endedAt":'
  assert_contains "$result" '"exitCode": 0'
  assert_contains "$(cat "$evidence/steps/safe.log")" "would run preflight, tier1, and tier2-readonly"
  assert_not_contains "$(cat "$evidence/steps/safe.log")" "tests:databuckets"
  assert_not_contains "$(cat "$evidence/steps/safe.log")" "tests:zone-state"
}

test_validation_worker_safe_profile_binds_requested_checkout_and_restores_stack_code() {
  local fixture_repo fixture_parent alternate_repo stack_dir request evidence status output validation_log
  make_fixture fixture_repo fixture_parent
  alternate_repo="$fixture_parent/alternate-EQEmu"
  stack_dir="$fixture_parent/bump-akk-stack-validation"
  validation_log="$fixture_parent/validated-checkout.log"
  mkdir -p "$alternate_repo"
  cp -R "$fixture_repo/scripts" "$alternate_repo/"
  cat >"$alternate_repo/scripts/validate.sh" <<'EOF'
#!/usr/bin/env bash
set -euo pipefail

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
repo_dir="$(cd "$script_dir/.." && pwd)"
resolved_code="$(realpath -m "$VALIDATION_STACK_DIR/code")"
{
  printf 'repo=%s\n' "$repo_dir"
  printf 'akkstack_dir=%s\n' "${AKKSTACK_DIR:-}"
  printf 'resolved_code=%s\n' "$resolved_code"
  printf 'args=%s\n' "$*"
} >"$VALIDATION_BINDING_LOG"
[[ "${AKKSTACK_DIR:-}" == "$VALIDATION_STACK_DIR" ]]
[[ "$resolved_code" == "$repo_dir" ]]
EOF
  chmod +x "$alternate_repo/scripts/validate.sh"
  request="$fixture_parent/alternate-safe-request.json"
  evidence="$fixture_parent/evidence/alternate-safe"
  write_validation_worker_profile_request "$request" safe "$alternate_repo" "$stack_dir" "$evidence"

  capture_run status output env VALIDATION_STACK_DIR="$stack_dir" VALIDATION_BINDING_LOG="$validation_log" "$fixture_repo/scripts/validation-worker.sh" run --request "$request"

  [[ "$status" -eq 0 ]] || return 1
  assert_contains "$output" "pass: safe"
  assert_contains "$(cat "$validation_log")" "repo=$alternate_repo"
  assert_contains "$(cat "$validation_log")" "akkstack_dir=$stack_dir"
  assert_contains "$(cat "$validation_log")" "resolved_code=$alternate_repo"
  assert_contains "$(cat "$validation_log")" "args=--stack validation --dry-run safe"
  [[ "$(readlink "$stack_dir/code")" == "$fixture_repo" ]] || return 1
}

test_validation_worker_safe_profile_accepts_automation_schema_and_binds_requested_checkout() {
  local fixture_repo fixture_parent alternate_repo stack_dir request evidence status output result validation_log
  make_fixture fixture_repo fixture_parent
  alternate_repo="$fixture_parent/automation-EQEmu"
  stack_dir="$fixture_parent/bump-akk-stack-validation"
  validation_log="$fixture_parent/automation-validated-checkout.log"
  mkdir -p "$alternate_repo"
  cp -R "$fixture_repo/scripts" "$alternate_repo/"
  cat >"$alternate_repo/scripts/validate.sh" <<'EOF'
#!/usr/bin/env bash
set -euo pipefail

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
repo_dir="$(cd "$script_dir/.." && pwd)"
resolved_code="$(realpath -m "$VALIDATION_STACK_DIR/code")"
{
  printf 'repo=%s\n' "$repo_dir"
  printf 'akkstack_dir=%s\n' "${AKKSTACK_DIR:-}"
  printf 'resolved_code=%s\n' "$resolved_code"
  printf 'args=%s\n' "$*"
} >"$VALIDATION_BINDING_LOG"
sleep 5
EOF
  chmod +x "$alternate_repo/scripts/validate.sh"
  request="$fixture_parent/automation-safe-request.json"
  evidence="$fixture_parent/evidence/automation-safe"
  cat >"$request" <<EOF
{
  "profile": "safe",
  "target_worktree_checkout": "$alternate_repo",
  "stack": { "role": "validation", "path": "$stack_dir" },
  "evidence_dir": "$evidence",
  "dryRun": true,
  "timeout_seconds": 1,
  "lockWaitSeconds": 30
}
EOF

  capture_run status output env VALIDATION_STACK_DIR="$stack_dir" VALIDATION_BINDING_LOG="$validation_log" "$fixture_repo/scripts/validation-worker.sh" run --request "$request"

  [[ "$status" -eq 1 ]] || return 1
  [[ -f "$evidence/result.json" ]] || return 1
  result="$(cat "$evidence/result.json")"
  assert_contains "$output" "timeout"
  assert_contains "$result" '"profile": "safe"'
  assert_contains "$result" '"category": "timeout"'
  assert_contains "$(cat "$validation_log")" "repo=$alternate_repo"
  assert_contains "$(cat "$validation_log")" "akkstack_dir=$stack_dir"
  assert_contains "$(cat "$validation_log")" "resolved_code=$alternate_repo"
  assert_contains "$(cat "$validation_log")" "args=--stack validation --dry-run safe"
  [[ "$(readlink "$stack_dir/code")" == "$fixture_repo" ]] || return 1
}

test_validation_worker_profile_uses_akkstack_dir_for_automation_worktree() {
  local fixture_repo fixture_parent alternate_parent alternate_repo stack_dir request evidence status output result validation_log
  make_fixture fixture_repo fixture_parent
  alternate_parent="$(mktemp -d "$tmp_root/automation-worktree.XXXXXX")"
  alternate_repo="$alternate_parent/agent-central-6lt8"
  stack_dir="$fixture_parent/bump-akk-stack-validation"
  validation_log="$fixture_parent/akkstack-dir-validated-checkout.log"
  mkdir -p "$alternate_repo"
  cp -R "$fixture_repo/scripts" "$alternate_repo/"
  cat >"$alternate_repo/scripts/validate.sh" <<'EOF'
#!/usr/bin/env bash
set -euo pipefail

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
repo_dir="$(cd "$script_dir/.." && pwd)"
resolved_code="$(realpath -m "$VALIDATION_STACK_DIR/code")"
{
  printf 'repo=%s\n' "$repo_dir"
  printf 'akkstack_dir=%s\n' "${AKKSTACK_DIR:-}"
  printf 'resolved_code=%s\n' "$resolved_code"
  printf 'args=%s\n' "$*"
} >"$VALIDATION_BINDING_LOG"
[[ "${AKKSTACK_DIR:-}" == "$VALIDATION_STACK_DIR" ]]
[[ "$resolved_code" == "$repo_dir" ]]
EOF
  chmod +x "$alternate_repo/scripts/validate.sh"
  request="$fixture_parent/automation-akkstack-dir-request.json"
  evidence="$fixture_parent/evidence/automation-akkstack-dir"
  cat >"$request" <<EOF
{
  "profile": "safe",
  "target_worktree_checkout": "$alternate_repo",
  "evidence_dir": "$evidence",
  "dryRun": true,
  "timeout_seconds": 30,
  "lockWaitSeconds": 30
}
EOF

  capture_run status output env AKKSTACK_DIR="$stack_dir" VALIDATION_STACK_DIR="$stack_dir" VALIDATION_BINDING_LOG="$validation_log" "$fixture_repo/scripts/validation-worker.sh" run --request "$request"

  [[ "$status" -eq 0 ]] || return 1
  result="$(cat "$evidence/result.json")"
  assert_contains "$output" "pass: safe"
  assert_contains "$result" '"stackPath": "'"$stack_dir"'"'
  assert_contains "$(cat "$validation_log")" "repo=$alternate_repo"
  assert_contains "$(cat "$validation_log")" "akkstack_dir=$stack_dir"
  assert_contains "$(cat "$validation_log")" "resolved_code=$alternate_repo"
  assert_contains "$(cat "$validation_log")" "args=--stack validation --dry-run safe"
  [[ "$(readlink "$stack_dir/code")" == "$fixture_repo" ]] || return 1
}

test_validation_worker_profile_requests_use_stack_global_lock() {
  local fixture_repo fixture_parent stack_dir first_request second_request first_evidence second_evidence state_dir first_output first_pid first_status status output
  make_fixture fixture_repo fixture_parent
  stack_dir="$fixture_parent/bump-akk-stack-validation"
  state_dir="$fixture_parent/lock-state"
  mkdir -p "$state_dir"
  cat >"$fixture_repo/scripts/validate.sh" <<'EOF'
#!/usr/bin/env bash
set -euo pipefail

if mkdir "$VALIDATION_LOCK_STATE/first-entered" 2>/dev/null; then
  touch "$VALIDATION_LOCK_STATE/first-started"
  while [[ ! -e "$VALIDATION_LOCK_STATE/release" ]]; do
    sleep 0.05
  done
else
  touch "$VALIDATION_LOCK_STATE/second-entered"
fi
EOF
  chmod +x "$fixture_repo/scripts/validate.sh"
  first_request="$fixture_parent/first-safe-request.json"
  second_request="$fixture_parent/second-safe-request.json"
  first_evidence="$fixture_parent/evidence/first-safe"
  second_evidence="$fixture_parent/evidence/second-safe"
  first_output="$fixture_parent/first-worker.out"
  write_validation_worker_profile_request "$first_request" safe "$fixture_repo" "$stack_dir" "$first_evidence" 10 10
  write_validation_worker_profile_request "$second_request" safe "$fixture_repo" "$stack_dir" "$second_evidence" 10 1

  env VALIDATION_LOCK_STATE="$state_dir" "$fixture_repo/scripts/validation-worker.sh" run --request "$first_request" >"$first_output" 2>&1 &
  first_pid=$!
  for _ in {1..100}; do
    [[ -e "$state_dir/first-started" ]] && break
    kill -0 "$first_pid" 2>/dev/null || break
    sleep 0.05
  done
  if [[ ! -e "$state_dir/first-started" ]]; then
    touch "$state_dir/release"
    set +e
    wait "$first_pid"
    set -e
    return 1
  fi

  capture_run status output env VALIDATION_LOCK_STATE="$state_dir" "$fixture_repo/scripts/validation-worker.sh" run --request "$second_request"
  touch "$state_dir/release"
  set +e
  wait "$first_pid"
  first_status=$?
  set -e

  [[ "$first_status" -eq 0 ]] || return 1
  [[ "$status" -eq 75 ]] || return 1
  assert_contains "$output" "timed out waiting for validation worker lock"
  assert_contains "$output" "bump-eqemu-validation-worker-locks"
  assert_not_contains "$output" "$second_evidence/worker.lock"
  [[ ! -e "$state_dir/second-entered" ]] || return 1
}

test_validation_worker_profile_refuses_non_symlink_stack_code() {
  local fixture_repo fixture_parent alternate_repo stack_dir request evidence marker status output result
  make_fixture fixture_repo fixture_parent
  alternate_repo="$fixture_parent/alternate-EQEmu"
  stack_dir="$fixture_parent/bump-akk-stack-validation"
  marker="$fixture_parent/validate-ran"
  mkdir -p "$alternate_repo"
  cp -R "$fixture_repo/scripts" "$alternate_repo/"
  cat >"$alternate_repo/scripts/validate.sh" <<EOF
#!/usr/bin/env bash
touch "$marker"
EOF
  chmod +x "$alternate_repo/scripts/validate.sh"
  rm "$stack_dir/code"
  mkdir "$stack_dir/code"
  request="$fixture_parent/unsafe-code-safe-request.json"
  evidence="$fixture_parent/evidence/unsafe-code-safe"
  write_validation_worker_profile_request "$request" safe "$alternate_repo" "$stack_dir" "$evidence"

  capture_run status output "$fixture_repo/scripts/validation-worker.sh" run --request "$request"

  [[ "$status" -eq 1 ]] || return 1
  result="$(cat "$evidence/result.json")"
  assert_contains "$output" "unsafe_code_path"
  assert_contains "$result" '"category": "unsafe_code_path"'
  [[ ! -e "$marker" ]] || return 1
  [[ -d "$stack_dir/code" && ! -L "$stack_dir/code" ]] || return 1
}

test_validation_worker_tier3_profile_records_harness_command() {
  local fixture_repo fixture_parent request evidence status output result
  make_fixture fixture_repo fixture_parent
  request="$fixture_parent/tier3-request.json"
  evidence="$fixture_parent/evidence/tier3"
  write_validation_worker_profile_request "$request" tier3-harness "$fixture_repo" "$fixture_parent/bump-akk-stack-validation" "$evidence"

  capture_run status output "$fixture_repo/scripts/validation-worker.sh" run --request "$request"

  [[ "$status" -eq 0 ]] || return 1
  result="$(cat "$evidence/result.json")"
  assert_contains "$output" "pass: tier3_harness"
  assert_contains "$result" '"profile": "tier3-harness"'
  assert_contains "$result" '"databaseBehavior": "read-mostly/runtime fixture use"'
  assert_contains "$(cat "$evidence/steps/tier3_harness.log")" "canonical Compose"
  assert_contains "$(cat "$evidence/steps/tier3_harness.log")" "only eqemu-server host ports disabled"
}

test_validation_worker_tier1_tier3_profile_records_ordered_dry_run_commands() {
  local fixture_repo fixture_parent request evidence status output result command_steps
  make_fixture fixture_repo fixture_parent
  request="$fixture_parent/tier1-tier3-request.json"
  evidence="$fixture_parent/evidence/tier1-tier3"
  write_validation_worker_profile_request "$request" tier1-tier3-harness "$fixture_repo" "$fixture_parent/bump-akk-stack-validation" "$evidence"

  capture_run status output "$fixture_repo/scripts/validation-worker.sh" run --request "$request"

  [[ "$status" -eq 0 ]] || return 1
  result="$(cat "$evidence/result.json")"
  command_steps="$(python3 - "$evidence/result.json" <<'PY'
import json
import sys

with open(sys.argv[1], encoding="utf-8") as f:
    result = json.load(f)

print(" ".join(step["name"] for step in result["steps"] if step["name"] in {"tier1", "tier3_harness"}))
PY
)"
  [[ "$command_steps" == "tier1 tier3_harness" ]] || return 1
  assert_contains "$output" "pass: tier1"
  assert_contains "$output" "pass: tier3_harness"
  assert_contains "$result" '"profile": "tier1-tier3-harness"'
  assert_contains "$result" '"databaseBehavior": "read-mostly/runtime fixture use"'
  assert_contains "$(cat "$evidence/steps/tier1.log")" "would run preflight, container build, and unit tests"
  assert_contains "$(cat "$evidence/steps/tier3_harness.log")" "canonical Compose"
}

test_validation_worker_tier1_tier3_profile_skips_harness_after_tier1_failure() {
  local fixture_repo fixture_parent request evidence calls status output result
  make_fixture fixture_repo fixture_parent
  request="$fixture_parent/tier1-tier3-failure-request.json"
  evidence="$fixture_parent/evidence/tier1-tier3-failure"
  calls="$fixture_parent/validate.calls"
  write_validation_worker_profile_request "$request" tier1-tier3-harness "$fixture_repo" "$fixture_parent/bump-akk-stack-validation" "$evidence"
  cat >"$fixture_repo/scripts/validate.sh" <<'EOF'
#!/usr/bin/env bash
set -euo pipefail
printf '%s\n' "$*" >>"${VALIDATE_CALLS:?}"
case "${@: -1}" in
  tier1) exit 42 ;;
  tier3-harness) exit 0 ;;
esac
exit 0
EOF
  chmod +x "$fixture_repo/scripts/validate.sh"

  capture_run status output env VALIDATE_CALLS="$calls" "$fixture_repo/scripts/validation-worker.sh" run --request "$request"

  [[ "$status" -eq 1 ]] || return 1
  result="$(cat "$evidence/result.json")"
  assert_contains "$output" "fail: tier1"
  assert_contains "$output" "skip: tier3_harness [prerequisite_failed]"
  assert_contains "$(cat "$calls")" "tier1"
  assert_not_contains "$(cat "$calls")" "tier3-harness"
  assert_contains "$result" '"status": "fail"'
  assert_contains "$result" '"name": "tier3_harness"'
  assert_contains "$result" '"status": "skip"'
  assert_contains "$result" '"category": "prerequisite_failed"'
}

test_validation_worker_profile_failure_and_timeout_categories() {
  local fixture_repo fixture_parent request evidence status output result
  make_fixture fixture_repo fixture_parent
  cat >"$fixture_repo/scripts/validate.sh" <<'EOF'
#!/usr/bin/env bash
printf 'forced validate failure\n'
exit 42
EOF
  chmod +x "$fixture_repo/scripts/validate.sh"
  request="$fixture_parent/failing-safe-request.json"
  evidence="$fixture_parent/evidence/failing-safe"
  write_validation_worker_profile_request "$request" safe "$fixture_repo" "$fixture_parent/bump-akk-stack-validation" "$evidence"

  capture_run status output "$fixture_repo/scripts/validation-worker.sh" run --request "$request"

  [[ "$status" -eq 1 ]] || return 1
  result="$(cat "$evidence/result.json")"
  assert_contains "$output" "validation_failed"
  assert_contains "$result" '"category": "validation_failed"'
  assert_contains "$(cat "$evidence/steps/safe.log")" "forced validate failure"

  make_fixture fixture_repo fixture_parent
  cat >"$fixture_repo/scripts/validate.sh" <<'EOF'
#!/usr/bin/env bash
sleep 5
EOF
  chmod +x "$fixture_repo/scripts/validate.sh"
  request="$fixture_parent/timeout-safe-request.json"
  evidence="$fixture_parent/evidence/timeout-safe"
  write_validation_worker_profile_request "$request" safe "$fixture_repo" "$fixture_parent/bump-akk-stack-validation" "$evidence" 1

  capture_run status output "$fixture_repo/scripts/validation-worker.sh" run --request "$request"

  [[ "$status" -eq 1 ]] || return 1
  result="$(cat "$evidence/result.json")"
  assert_contains "$output" "timeout"
  assert_contains "$result" '"category": "timeout"'
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
run_test "tier1 uses checkout build ccache dir" test_tier1_uses_checkout_build_ccache_dir
run_test "tier2-readonly dry-run describes one-off container" test_tier2_readonly_dry_run_describes_single_one_off_container
run_test "safe dry-run keeps readonly composition" test_safe_dry_run_keeps_readonly_composition
run_test "validation worker preflight writes structured evidence" test_validation_worker_preflight_writes_structured_evidence
run_test "validation worker preflight actively checks Docker DB ports and assets" test_validation_worker_preflight_actively_checks_docker_db_ports_and_assets
run_test "validation worker preflight reports host port conflict category" test_validation_worker_preflight_reports_host_port_conflict_category
run_test "validation worker preflight reports listener port conflict category" test_validation_worker_preflight_reports_listener_port_conflict_category
run_test "validation worker preflight reports missing Docker category" test_validation_worker_preflight_reports_missing_docker_category
run_test "validation worker preflight reports missing stack category" test_validation_worker_preflight_reports_missing_stack_category
run_test "validation worker preflight reports wrong checkout category" test_validation_worker_preflight_reports_wrong_checkout_category
run_test "validation worker preflight accepts automation schema commit and evidence dir" test_validation_worker_preflight_accepts_automation_schema_commit_and_evidence_dir
run_test "validation worker safe profile records command evidence" test_validation_worker_safe_profile_records_command_evidence
run_test "validation worker safe profile binds requested checkout and restores stack code" test_validation_worker_safe_profile_binds_requested_checkout_and_restores_stack_code
run_test "validation worker safe profile accepts automation schema and binds requested checkout" test_validation_worker_safe_profile_accepts_automation_schema_and_binds_requested_checkout
run_test "validation worker profile uses AKKSTACK_DIR for automation worktree" test_validation_worker_profile_uses_akkstack_dir_for_automation_worktree
run_test "validation worker profile requests use stack global lock" test_validation_worker_profile_requests_use_stack_global_lock
run_test "validation worker profile refuses non-symlink stack code" test_validation_worker_profile_refuses_non_symlink_stack_code
run_test "validation worker tier3 profile records harness command" test_validation_worker_tier3_profile_records_harness_command
run_test "validation worker tier1 plus tier3 profile records ordered dry-run commands" test_validation_worker_tier1_tier3_profile_records_ordered_dry_run_commands
run_test "validation worker tier1 plus tier3 profile skips harness after tier1 failure" test_validation_worker_tier1_tier3_profile_skips_harness_after_tier1_failure
run_test "validation worker profile failure and timeout categories" test_validation_worker_profile_failure_and_timeout_categories

if [[ "$failures" -gt 0 ]]; then
  exit 1
fi

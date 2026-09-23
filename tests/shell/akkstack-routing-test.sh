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

extract_embedded_python_heredoc() {
  local source_file="$1"
  local function_name="$2"

  python3 - "$source_file" "$function_name" <<'PY'
from pathlib import Path
import sys

source_path = Path(sys.argv[1])
function_name = sys.argv[2]
lines = source_path.read_text().splitlines()
inside_function = False
inside_python = False
capture = []

for line in lines:
    if not inside_function and line == f"{function_name}() {{":
        inside_function = True
        continue
    if inside_function and not inside_python and "<<'PY'" in line:
        inside_python = True
        continue
    if inside_python:
        if line == "PY":
            break
        capture.append(line)

if not capture:
    sys.exit(1)

sys.stdout.write("\n".join(capture) + "\n")
PY
}

assert_command_helpers_execute() {
  local script_file="$1"
  local headless_script="$tmp_root/assert-headless-target.py"
  local cursor_script="$tmp_root/assert-headless-cursor.py"
  local empty_script="$tmp_root/assert-empty-events.py"

  extract_embedded_python_heredoc "$script_file" assert_headless_target_scenario >"$headless_script"
  extract_embedded_python_heredoc "$script_file" assert_headless_target_cursor_progression >"$cursor_script"
  extract_embedded_python_heredoc "$script_file" assert_empty_event_payload >"$empty_script"

  headless_target_first="$(python3 - <<'PY'
import json

payload = {
    "completed": True,
    "observed": True,
    "database_mutation": "none:headless-target",
    "eqstream_backed": False,
    "completed_connect": False,
    "actor": {"kind": "client", "entity_id": 101},
    "target": {"kind": "npc", "entity_id": 202},
    "event_cursor_start": 10,
    "event_cursor_end": 12,
    "events": [
        {
            "id": 11,
            "type": "target_changed",
            "actor": {"entity_id": 101},
            "message": "target_set",
            "target": {"entity_id": 202},
        },
        {
            "id": 12,
            "type": "target_changed",
            "actor": {"entity_id": 101},
            "message": "target_cleared",
            "target": None,
            "previous_target": {"entity_id": 202},
        },
    ],
}
print(json.dumps(payload, separators=(",", ":")))
PY
)"
  headless_target_second="$(python3 - <<'PY'
import json

payload = {
    "completed": True,
    "observed": True,
    "database_mutation": "none:headless-target",
    "eqstream_backed": False,
    "completed_connect": False,
    "actor": {"kind": "client", "entity_id": 303},
    "target": {"kind": "npc", "entity_id": 404},
    "event_cursor_start": 12,
    "event_cursor_end": 14,
    "events": [
        {
            "id": 13,
            "type": "target_changed",
            "actor": {"entity_id": 303},
            "message": "target_set",
            "target": {"entity_id": 404},
        },
        {
            "id": 14,
            "type": "target_changed",
            "actor": {"entity_id": 303},
            "message": "target_cleared",
            "target": None,
            "previous_target": {"entity_id": 404},
        },
    ],
}
print(json.dumps(payload, separators=(",", ":")))
PY
)"

  HEADLESS_TARGET_PAYLOAD="$headless_target_first" python3 "$headless_script"
  HEADLESS_TARGET_PAYLOAD="$headless_target_second" python3 "$headless_script"
  EVENT_PAYLOAD='{"events":[]}' python3 "$empty_script" "headless target cleanup left unexpected events"
  HEADLESS_TARGET_FIRST="$headless_target_first" HEADLESS_TARGET_SECOND="$headless_target_second" python3 "$cursor_script"

  if HEADLESS_TARGET_PAYLOAD='{"completed":true,"observed":true,"database_mutation":"none:headless-target","eqstream_backed":false,"completed_connect":false,"actor":{"kind":"client","entity_id":1},"target":{"kind":"npc","entity_id":2},"event_cursor_start":20,"event_cursor_end":20,"events":[]}' python3 "$headless_script" >/dev/null 2>&1; then
    return 1
  fi

  if EVENT_PAYLOAD='{"events":[{"id":99}]}' python3 "$empty_script" "expected failure" >/dev/null 2>&1; then
    return 1
  fi

  if HEADLESS_TARGET_FIRST="$headless_target_first" HEADLESS_TARGET_SECOND='{"event_cursor_start":11,"event_cursor_end":15}' python3 "$cursor_script" >/dev/null 2>&1; then
    return 1
  fi
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

make_fake_smoke_execute_bin() {
  local fake_bin="$1"
  local capture_file="$2"
  local payload_file="$3"

  mkdir -p "$fake_bin"

  cat >"$fake_bin/mysqladmin" <<'EOF'
#!/usr/bin/env bash
exit 0
EOF
  chmod +x "$fake_bin/mysqladmin"

  cat >"$fake_bin/jq" <<'EOF'
#!/usr/bin/env bash
if [[ -f "${@: -1}" ]]; then
  exec /usr/bin/jq "$@"
fi
printf '%s\n' '{"server":{"database":{"host":"mariadb","port":"3306"},"qsdatabase":{"host":"mariadb","port":"3306"}}}'
EOF
  chmod +x "$fake_bin/jq"

  cat >"$fake_bin/docker-compose" <<EOF
#!/usr/bin/env bash
set -euo pipefail

if [[ " \$* " == *" up "* ]]; then
  exit 0
fi

printf '%s\n' "\$*" >"$capture_file"
payload=""
previous=""
for arg in "\$@"; do
  if [[ "\$previous" == "-lc" ]]; then
    payload="\$arg"
    break
  fi
  previous="\$arg"
done

if [[ -z "\$payload" ]]; then
  printf 'missing bash -lc payload\n' >&2
  exit 1
fi

printf '%s\n' "\$payload" >"$payload_file"
EQEMU_DB_PASSWORD=fixture bash -lc "\$payload"
EOF
  chmod +x "$fake_bin/docker-compose"
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

test_zone_harness_command_checks_build_artifacts_before_zone_launch() {
  local fixture_repo fixture_parent fake_bin capture_file payload_file status output command_text
  make_fixture fixture_repo fixture_parent
  fake_bin="$(mktemp -d "$tmp_root/fake-smoke-bin.XXXXXX")"
  capture_file="$tmp_root/smoke-zone-harness.command"
  payload_file="$tmp_root/smoke-zone-harness.payload"
  make_fake_smoke_execute_bin "$fake_bin" "$capture_file" "$payload_file"

  capture_run status output env PATH="$fake_bin:$PATH" "$fixture_repo/scripts/smoke-zone-harness.sh" --stack validation

  [[ "$status" -eq 1 ]] || return 1
  [[ -f "$capture_file" ]] || return 1
  command_text="$(cat "$capture_file")"
  assert_contains "$command_text" "link_runtime_dir plugins ~/server/quests/plugins ~/server/plugins"
  assert_contains "$command_text" "link_runtime_dir lua_modules ~/server/quests/lua_modules ~/server/lua_modules"
  assert_contains "$command_text" "require_runtime_binary ./bin/zone"
  assert_contains "$command_text" "require_runtime_binary ./bin/shared_memory"
  assert_contains "$command_text" "tier3-harness requires a prior Tier 1 build or a combined build+harness profile"
  assert_contains "$command_text" "./bin/zone tests:serve-http"
  assert_contains "$output" "tier3-harness requires a prior Tier 1 build"
  assert_contains "$output" "missing executable ./bin/zone"
  python3 - "$capture_file" <<'PY'
from pathlib import Path
import sys

text = Path(sys.argv[1]).read_text()
shared_memory_check = text.find("require_runtime_binary ./bin/shared_memory")
zone_launch = text.find("./bin/zone tests:serve-http")
if shared_memory_check == -1 or zone_launch == -1 or shared_memory_check > zone_launch:
    sys.exit(1)
PY
}

test_zone_harness_uses_service_dns_runtime_config_without_mutating_source() {
  local fixture_repo fixture_parent fake_bin fake_home fake_server capture_file payload_file source_hash status output
  make_fixture fixture_repo fixture_parent
  fake_bin="$(mktemp -d "$tmp_root/fake-smoke-config-bin.XXXXXX")"
  fake_home="$tmp_root/fake-home"
  fake_server="$fake_home/server"
  capture_file="$tmp_root/smoke-zone-harness-config.command"
  payload_file="$tmp_root/smoke-zone-harness-config.payload"
  mkdir -p "$fake_server" "$fake_home/code/build/bin"
  touch "$fake_home/code/build/bin/zone" "$fake_home/code/build/bin/shared_memory"
  chmod +x "$fake_home/code/build/bin/zone" "$fake_home/code/build/bin/shared_memory"
  cat >"$fake_server/eqemu_config.json" <<'JSON'
{"server":{"database":{"host":"127.0.0.1","port":"13306"},"qsdatabase":{"host":"127.0.0.1","port":"13306"}}}
JSON
  source_hash="$(sha256sum "$fake_server/eqemu_config.json")"
  cat >"$fake_bin/mysqladmin" <<'EOF'
#!/usr/bin/env bash
exit 0
EOF
  chmod +x "$fake_bin/mysqladmin"
  cat >"$fake_bin/docker-compose" <<EOF
#!/usr/bin/env bash
set -euo pipefail
if [[ " \$* " == *" up "* ]]; then exit 0; fi
previous=""
for arg in "\$@"; do
  if [[ "\$previous" == "-lc" ]]; then
    printf '%s\n' "\$arg" >"$payload_file"
    HOME="$fake_home" PATH="$fake_bin:\$PATH" bash -c "\$arg"
  fi
  previous="\$arg"
done
exit 1
EOF
  chmod +x "$fake_bin/docker-compose"

  capture_run status output env PATH="$fake_bin:$PATH" "$fixture_repo/scripts/smoke-zone-harness.sh" --stack validation

  [[ "$status" -eq 1 ]] || return 1
  [[ "$(jq -r '.server.database.host + ":" + .server.database.port' /tmp/zone-harness-validation-runtime/eqemu_config.json)" == "mariadb:3306" ]] || return 1
  [[ "$(jq -r '.server.qsdatabase.host + ":" + .server.qsdatabase.port' /tmp/zone-harness-validation-runtime/eqemu_config.json)" == "mariadb:3306" ]] || return 1
  [[ "$(sha256sum "$fake_server/eqemu_config.json")" == "$source_hash" ]] || return 1
}

test_zone_harness_command_exercises_headless_target_twice_with_cursor_cleanup_checks() {
  local fixture_repo fixture_parent fake_bin capture_file payload_file status output command_text
  make_fixture fixture_repo fixture_parent
  fake_bin="$(mktemp -d "$tmp_root/fake-smoke-headless-bin.XXXXXX")"
  capture_file="$tmp_root/smoke-zone-harness-headless.command"
  payload_file="$tmp_root/smoke-zone-harness-headless.payload"
  make_fake_smoke_execute_bin "$fake_bin" "$capture_file" "$payload_file"

  capture_run status output env PATH="$fake_bin:$PATH" "$fixture_repo/scripts/smoke-zone-harness.sh" --stack validation

  [[ "$status" -eq 1 ]] || return 1
  [[ -f "$capture_file" ]] || return 1
  [[ -f "$payload_file" ]] || return 1
  command_text="$(cat "$capture_file")"
  assert_contains "$command_text" "assert_headless_target_scenario()"
  assert_contains "$command_text" "assert_headless_target_cursor_progression()"
  assert_contains "$command_text" "/api/v1/harness/scenarios/headless-client/target"
  assert_contains "$command_text" "headless_target_first="
  assert_contains "$command_text" "headless_target_second="
  assert_contains "$command_text" 'assert_headless_target_scenario "$headless_target_first"'
  assert_contains "$command_text" 'assert_headless_target_scenario "$headless_target_second"'
  assert_contains "$command_text" 'headless_cleanup_events=$(curl -fsS "http://127.0.0.1:9099/api/v1/harness/events?since=${headless_target_first_end}&limit=10")'
  assert_contains "$command_text" 'assert_empty_event_payload "$headless_cleanup_events"'
  assert_contains "$command_text" 'assert_headless_target_cursor_progression "$headless_target_first" "$headless_target_second"'
  assert_command_helpers_execute "$fixture_repo/scripts/smoke-zone-harness.sh"
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

test_tier2_readonly_uses_service_dns_runtime_config() {
  local fixture_repo fixture_parent fake_bin capture_file payload_file status output
  make_fixture fixture_repo fixture_parent
  fake_bin="$(mktemp -d "$tmp_root/fake-tier2-bin.XXXXXX")"
  capture_file="$tmp_root/tier2-capture.txt"
  payload_file="$tmp_root/tier2-payload.txt"

  cat >"$fake_bin/docker-compose" <<EOF
#!/usr/bin/env bash
set -euo pipefail
printf '%s\n' "\$*" >"$capture_file"
if [[ " \$* " == *" up "* ]]; then
  exit 0
fi
previous=""
for arg in "\$@"; do
  if [[ "\$previous" == "-lc" ]]; then
    printf '%s\n' "\$arg" >"$payload_file"
    exit 0
  fi
  previous="\$arg"
done
printf 'missing bash -lc payload\n' >&2
exit 1
EOF
  chmod +x "$fake_bin/docker-compose"

  cat >"$fake_bin/mysqladmin" <<'EOF'
#!/usr/bin/env bash
exit 0
EOF
  chmod +x "$fake_bin/mysqladmin"

  capture_run status output env PATH="$fake_bin:$PATH" "$fixture_repo/scripts/validate.sh" tier2-readonly

  [[ "$status" -eq 0 ]] || return 1
  assert_contains "$(cat "$capture_file")" "run --rm --no-deps --entrypoint bash eqemu-server"
  assert_contains "$(cat "$payload_file")" 'mysqladmin status -ueqemu -p"$EQEMU_DB_PASSWORD" -h mariadb --silent'
  assert_contains "$(cat "$payload_file")" '.server.database.host = \"mariadb\"'
  assert_contains "$(cat "$payload_file")" '.server.qsdatabase.host = \"mariadb\"'
  assert_contains "$(cat "$payload_file")" 'link_runtime_dir plugins ~/server/quests/plugins ~/server/plugins'
  assert_contains "$(cat "$payload_file")" 'link_runtime_dir lua_modules ~/server/quests/lua_modules ~/server/lua_modules'
  assert_contains "$(cat "$payload_file")" '~/code/build/bin/zone tests:npc-handins'
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

run_test "preflight defaults to validation" test_preflight_defaults_to_validation
run_test "runtime proof defaults to gameplay" test_runtime_proof_defaults_to_gameplay
run_test "explicit roles are accepted by wrappers" test_explicit_roles_are_accepted_by_wrappers
run_test "invalid role fails" test_invalid_role_fails
run_test "custom path override is explicit" test_custom_path_override
run_test "missing default validation fails clearly" test_missing_default_validation_fails_clearly
run_test "default role paths are distinct" test_default_roles_must_not_resolve_to_same_directory
run_test "validation commands warn on gameplay stack" test_validation_commands_warn_on_gameplay_stack
run_test "zone harness dry-run describes stable DB and portless server" test_zone_harness_dry_run_describes_stable_db_and_portless_server
run_test "zone harness command checks build artifacts before launch" test_zone_harness_command_checks_build_artifacts_before_zone_launch
run_test "zone harness uses service DNS runtime config without mutating source" test_zone_harness_uses_service_dns_runtime_config_without_mutating_source
run_test "zone harness command exercises headless target twice with cursor cleanup checks" test_zone_harness_command_exercises_headless_target_twice_with_cursor_cleanup_checks
run_test "validate tier3-harness delegates to smoke script" test_validate_tier3_harness_delegates_to_smoke_script
run_test "help mentions stack and dry-run" test_help_mentions_stack_and_dry_run
run_test "dry-run prints route and skips Docker" test_dry_run_prints_route_and_skips_docker
run_test "tier2-readonly dry-run describes one-off container" test_tier2_readonly_dry_run_describes_single_one_off_container
run_test "tier2-readonly runtime payload rewrites DB host to mariadb service DNS" test_tier2_readonly_uses_service_dns_runtime_config
run_test "safe dry-run keeps readonly composition" test_safe_dry_run_keeps_readonly_composition

if [[ "$failures" -gt 0 ]]; then
  exit 1
fi

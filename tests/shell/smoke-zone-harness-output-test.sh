#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
smoke_script="$repo_root/scripts/smoke-zone-harness.sh"

extract_function() {
  local function_name="$1"
  awk -v declaration="${function_name}() {" '
    $0 == declaration { copying = 1 }
    copying { print }
    copying && $0 == "}" { exit }
  ' "$smoke_script"
}

eval "$(extract_function assert_bot_loot_request_scenario)"
eval "$(extract_function assert_bot_loot_request_failure_cleanup)"

cleanup_payload='{"scenario":"bot-loot-request-failure-cleanup","proved":true,"failure_induced_after_overrides":true,"rules_restored":true,"fixture_entities_cleaned":true,"delivery_state_restored":true,"dialogue_provider_state_restored":true,"decision_observer_state_restored":true}'
upgrade_payload='{"scenario":"bot-loot-request-upgrade","proved":true,"positive_request_count":1,"upgrade_score":17,"requesting_bot":{"name":"HarnessLootUpgradeBot"},"upgrade_item_id":1001,"upgrade_item_name":"Upgrade Helm","target_slot":2,"target_slot_name":"Head","deterministic_reason":"higher armor value","downgrade_suppressed":true,"duplicate_suppressed":true,"looted_item_reached_looter":true,"loot_completed":true,"dialogue_pending_at_loot_completion":true,"normal_processing_responsive":true,"bot_inventory_unchanged":true,"provider_independent":true,"loot_completion_elapsed_ms":10,"loot_completion_budget_ms":100,"grouped_bot_count":2,"database_mutation":"none: runtime fixture"}'

cleanup_output="$(assert_bot_loot_request_failure_cleanup "$cleanup_payload")"
if [[ -n "$cleanup_output" ]]; then
  printf 'failure-cleanup assertion unexpectedly wrote stdout: %s\n' "$cleanup_output" >&2
  exit 1
fi

result="$(assert_bot_loot_request_scenario "$upgrade_payload")"
expected='{"scenario":"bot-loot-request-upgrade","proved":true,"bot":"HarnessLootUpgradeBot","item_id":1001,"slot":"Head","score":17,"reason":"higher armor value"}'
if [[ "$result" != "$expected" ]]; then
  printf 'unexpected canonical result\nexpected: %s\nactual:   %s\n' "$expected" "$result" >&2
  exit 1
fi

if [[ "$(printf '%s\n' "$cleanup_output" "$result" | sed '/^$/d' | wc -l)" -ne 1 ]]; then
  printf 'successful assertion sequence did not emit exactly one record\n' >&2
  exit 1
fi

printf 'ok - smoke Zone Harness emits one canonical scenario result\n'

# Exercise the wrapper itself with a deterministic Compose stand-in. This
# covers -T, the bind-mounted result bridge, host-side publication, and both
# durable failure paths without starting Docker.
test_root="$(mktemp -d)"
trap 'rm -rf "$test_root"' EXIT
mkdir -p "$test_root/bin" "$test_root/stack"
touch "$test_root/stack/.env"
ln -s "$repo_root" "$test_root/stack/code"

cat >"$test_root/bin/docker-compose" <<'MOCK_COMPOSE'
#!/usr/bin/env bash
set -euo pipefail

printf '%s\n' "$*" >>"${MOCK_COMPOSE_CALLS:?}"
[[ " $* " == *" run "* ]] || exit 0

saw_no_tty=false
result_dir=''
while (($#)); do
  case "$1" in
    -T)
      saw_no_tty=true
      shift
      ;;
    -v)
      result_dir="${2%%:*}"
      shift 2
      ;;
    *)
      shift
      ;;
  esac
done

if [[ "$saw_no_tty" != true || -z "$result_dir" ]]; then
  printf 'mock: run did not use -T and a result bind mount\n' >&2
  exit 90
fi

case "${MOCK_COMPOSE_MODE:-success}" in
  success)
    printf '%s\n' "${EXPECTED_RESULT:?}" >"$result_dir/result.json"
    ;;
  missing)
    ;;
  failure)
    printf 'durable harness diagnostic\n' >"$result_dir/zone_harness.out"
    exit 23
    ;;
  *)
    exit 91
    ;;
esac
MOCK_COMPOSE
chmod +x "$test_root/bin/docker-compose"

calls="$test_root/compose.calls"
run_wrapper() {
  PATH="$test_root/bin:$PATH" \
    AKKSTACK_DIR="$test_root/stack" \
    MOCK_COMPOSE_CALLS="$calls" \
    EXPECTED_RESULT="$expected" \
    MOCK_COMPOSE_MODE="$1" \
    "$smoke_script"
}

wrapper_output="$(run_wrapper success)"
if [[ "$wrapper_output" != "$expected" ]]; then
  printf 'wrapper did not publish exactly the bind-mounted canonical result\nexpected: %s\nactual:   %s\n' "$expected" "$wrapper_output" >&2
  exit 1
fi
if ! grep -Eq ' run .* -T .* -v .+:/tmp/zone-harness-result ' "$calls"; then
  printf 'wrapper Compose invocation did not expose the non-TTY result bridge\n' >&2
  cat "$calls" >&2
  exit 1
fi

failure_stdout="$test_root/failure.stdout"
failure_stderr="$test_root/failure.stderr"
if run_wrapper failure >"$failure_stdout" 2>"$failure_stderr"; then
  printf 'wrapper unexpectedly accepted a failed Compose run\n' >&2
  exit 1
fi
if [[ -s "$failure_stdout" ]] || ! grep -q 'durable harness diagnostic' "$failure_stderr" \
  || ! grep -q 'Zone Harness container failed with status 23' "$failure_stderr"; then
  printf 'wrapper did not route failed-run diagnostics to stderr\n' >&2
  cat "$failure_stderr" >&2
  exit 1
fi

missing_stdout="$test_root/missing.stdout"
missing_stderr="$test_root/missing.stderr"
if run_wrapper missing >"$missing_stdout" 2>"$missing_stderr"; then
  printf 'wrapper unexpectedly accepted a missing structured result\n' >&2
  exit 1
fi
if [[ -s "$missing_stdout" ]] || ! grep -q 'produced no structured result' "$missing_stderr"; then
  printf 'wrapper did not diagnose a missing bind-mounted result on stderr\n' >&2
  cat "$missing_stderr" >&2
  exit 1
fi

printf 'ok - smoke Zone Harness wrapper routes non-TTY results and diagnostics\n'

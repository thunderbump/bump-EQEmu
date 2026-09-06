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

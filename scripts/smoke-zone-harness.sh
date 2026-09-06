#!/usr/bin/env bash
set -euo pipefail

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
repo_root="$(cd "$script_dir/.." && pwd)"
source "$script_dir/lib/akkstack-routing.sh"

usage() {
  cat <<'USAGE'
Usage: scripts/smoke-zone-harness.sh [--stack <validation|gameplay>] [--dry-run]

Options:
  --stack <validation|gameplay>  Select the role default stack. Defaults to validation.
  --dry-run                     Print selected stack, Compose files, and action without Docker.
  -h, --help                    Show this help.

AKKSTACK_DIR=/path/to/stack remains an explicit custom-path override for the
selected role.
USAGE
}

akkstack_init_routing "$repo_root" validation "$@"

if [[ "$AKKSTACK_HELP" -eq 1 ]]; then
  usage
  exit 0
fi

if [[ "${#AKKSTACK_REMAINING_ARGS[@]}" -ne 0 ]]; then
  usage >&2
  exit 2
fi

stack_dir="$AKKSTACK_STACK_DIR"
port="${ZONE_HARNESS_PORT:-9099}"
compose_files=(docker-compose.yml docker-compose.dev.yml "<generated eqemu-server portless override>")

akkstack_warn_if_validation_command_targets_gameplay "scripts/smoke-zone-harness.sh"

if [[ "$AKKSTACK_DRY_RUN" -eq 1 ]]; then
  akkstack_print_dry_run "would start or verify the selected stack MariaDB with canonical Compose and --no-recreate, then run the Zone Harness smoke in a one-off eqemu-server container with only eqemu-server host ports disabled" "${compose_files[@]}"
  exit 0
fi

compose_override="$(mktemp)"
# Keep the bridge below the checkout: Validation Workers may have a private
# /tmp namespace that is not the Docker daemon's /tmp namespace.
result_dir="$(mktemp -d "$repo_root/.zone-harness-result.XXXXXX")"
result_file="$result_dir/result.json"
harness_log="$result_dir/zone_harness.out"
chmod 0777 "$result_dir"

cat >"$compose_override" <<'COMPOSE'
services:
  eqemu-server:
    ports: !override []
COMPOSE

cleanup() {
  rm -f "$compose_override"
  rm -rf "$result_dir"
}
trap cleanup EXIT

canonical_compose=(docker-compose -f docker-compose.yml -f docker-compose.dev.yml)
harness_compose=(docker-compose -f docker-compose.yml -f docker-compose.dev.yml -f "$compose_override")

die() {
  printf 'error: %s\n' "$*" >&2
  exit 1
}

"$repo_root/scripts/check-akkstack-contract.sh" --stack "$AKKSTACK_STACK_ROLE"

command -v docker-compose >/dev/null 2>&1 || die "docker-compose is required"

(
  cd "$stack_dir"
  "${canonical_compose[@]}" up -d --no-recreate mariadb >/dev/null
  container_script="$(cat <<'ZONE_HARNESS_CONTAINER'
set -euo pipefail

port="${ZONE_HARNESS_PORT:?ZONE_HARNESS_PORT is required}"
harness_log="${ZONE_HARNESS_LOG_FILE:?ZONE_HARNESS_LOG_FILE is required}"

until mysqladmin status -ueqemu -p"$EQEMU_DB_PASSWORD" -h mariadb --silent; do
  sleep 1
done

runtime=/tmp/zone-harness-validation-runtime
rm -rf "$runtime"
mkdir -p "$runtime/bin" "$runtime/logs" "$runtime/maps" "$runtime/quests"
ln -s ~/code/build/bin/zone "$runtime/bin/zone"
ln -s ~/code/build/bin/shared_memory "$runtime/bin/shared_memory"
jq '.server.database.host = "mariadb" | .server.database.port = "3306" | .server.qsdatabase.host = "mariadb" | .server.qsdatabase.port = "3306"' ~/server/eqemu_config.json > "$runtime/eqemu_config.json"
cd "$runtime"

dump_harness_log() {
  status="${1:-$?}"
  if [[ "$status" -ne 0 && -f "$harness_log" ]]; then
    cat "$harness_log" >&2
  fi
  exit "$status"
}
trap dump_harness_log EXIT

require_runtime_binary() {
  local path="$1"

  if [[ ! -x "$path" ]]; then
    printf 'error: tier3-harness requires a prior Tier 1 build or a combined build+harness profile; missing executable %s\n' "$path" >&2
    exit 1
  fi
}

require_runtime_binary ./bin/zone
require_runtime_binary ./bin/shared_memory

link_runtime_dir() {
  local target="$1"
  shift
  local candidate

  for candidate in "$@"; do
    if [[ -d "$candidate" ]]; then
      ln -s "$candidate" "$runtime/$target"
      return 0
    fi
  done

  printf "missing runtime directory for %s\n" "$target" >&2
  return 1
}
link_runtime_dir shared ~/server/shared
link_runtime_dir plugins ~/server/quests/plugins ~/server/plugins
link_runtime_dir lua_modules ~/server/quests/lua_modules ~/server/lua_modules

if ! ./bin/shared_memory > logs/shared_memory.out 2>&1; then
  cat logs/shared_memory.out >&2
  exit 1
fi

./bin/zone tests:serve-http --zone qrg --port ${port} --max-runtime-seconds 300 >"$harness_log" 2>&1 &
harness_pid=$!
trap 'status=$?; kill -TERM "$harness_pid" 2>/dev/null || true; dump_harness_log "$status"' EXIT

health=''
for _ in $(seq 1 180); do
  if health=$(curl -fsS "http://127.0.0.1:${port}/api/v1/harness/health" 2>/dev/null); then
    break
  fi
  if ! kill -0 "$harness_pid" 2>/dev/null; then
    cat "$harness_log" >&2
    exit 1
  fi
  sleep 1
done

[[ "$health" == *'"healthy":true'* ]] || { printf '%s\n' "$health" >&2; exit 1; }

zone=$(curl -fsS "http://127.0.0.1:${port}/api/v1/harness/zone")
[[ "$zone" == *'"short_name":"qrg"'* ]] || { printf '%s\n' "$zone" >&2; exit 1; }

entities=$(curl -fsS "http://127.0.0.1:${port}/api/v1/harness/entities")
[[ "$entities" == *'"counts"'* ]] || { printf '%s\n' "$entities" >&2; exit 1; }

process=$(curl -fsS -X POST -H 'Content-Type: application/json' --data '{"ticks":2}' "http://127.0.0.1:${port}/api/v1/harness/process")
[[ "$process" == *'"ticks_processed":2'* ]] || { printf '%s\n' "$process" >&2; exit 1; }

events=$(curl -fsS "http://127.0.0.1:${port}/api/v1/harness/events")
[[ "$events" == *'"events":[]'* ]] || { printf '%s\n' "$events" >&2; exit 1; }

assert_slow_scenario() {
  local payload="$1"
  local scenario="$2"
  local expected_name="$3"
  local require_current_slowed="${4:-false}"
  local require_mezzed="${5:-false}"

  SCENARIO_PAYLOAD="$payload" python3 - "$scenario" "$expected_name" "$require_current_slowed" "$require_mezzed" <<'PY'
import json
import os
import sys

scenario, expected_name, require_current_slowed, require_mezzed = sys.argv[1:5]
payload = json.loads(os.environ["SCENARIO_PAYLOAD"])

def fail(message):
    print(json.dumps({"error": message, "payload": payload}, separators=(",", ":")), file=sys.stderr)
    sys.exit(1)

if payload.get("scenario") != scenario:
    fail("unexpected scenario")
if payload.get("observed") is not True:
    fail("expected slow cast start was not observed")
if not str(payload.get("database_mutation", "")).startswith("none:"):
    fail("scenario reported database mutation")
if require_current_slowed == "true" and payload.get("current_target_slowed") is not True:
    fail("current target was not marked slowed during setup")
if require_mezzed == "true" and payload.get("mezzed_hostile_mezzed") is not True:
    fail("mezzed hostile was not marked mezzed during setup")

expected_target = payload.get("expected_target") or {}
if expected_target.get("name") != expected_name:
    fail("unexpected expected_target")

events = payload.get("events") or []
matching = [
    event for event in events
    if event.get("type") == "spell_cast_started"
    and event.get("spell", {}).get("category") == "Slow"
    and event.get("spell", {}).get("targeting") == "single"
    and event.get("target", {}).get("entity_id") == expected_target.get("entity_id")
]
if not matching:
    fail("expected target slow cast-start event was not present")

mezzed = payload.get("mezzed_hostile") or {}
if require_mezzed == "true" and any(
    event.get("type") == "spell_cast_started"
    and event.get("target", {}).get("entity_id") == mezzed.get("entity_id")
    for event in events
):
    fail("slow cast-start targeted the mezzed hostile")
PY
}

assert_headless_target_scenario() {
  local payload="$1"

  HEADLESS_TARGET_PAYLOAD="$payload" python3 - <<'PY'
import json
import os
import sys

payload = json.loads(os.environ["HEADLESS_TARGET_PAYLOAD"])

def fail(message):
    print(json.dumps({"error": message, "payload": payload}, separators=(",", ":")), file=sys.stderr)
    sys.exit(1)

if payload.get("completed") is not True:
    fail("headless target scenario did not complete")
if payload.get("observed") is not True:
    fail("headless target scenario did not observe target_changed")
if not str(payload.get("database_mutation", "")).startswith("none:"):
    fail("headless target scenario reported database mutation")
if payload.get("eqstream_backed") is not False:
    fail("headless client unexpectedly reported an EQStream-backed connection")
if payload.get("completed_connect") is not False:
    fail("headless client unexpectedly completed connect")

actor = payload.get("actor") or {}
target = payload.get("target") or {}
events = payload.get("events") or []
start = payload.get("event_cursor_start", 0)
end = payload.get("event_cursor_end", 0)

if actor.get("kind") != "client":
    fail("headless actor is not a client")
if target.get("kind") != "npc":
    fail("headless target is not an NPC")
if not actor.get("entity_id") or not target.get("entity_id"):
    fail("headless actor or target identity missing")
if end <= start:
    fail("headless target scenario cursor did not advance")

target_events = [
    event for event in events
    if event.get("type") == "target_changed"
    and event.get("actor", {}).get("entity_id") == actor.get("entity_id")
]
if len(target_events) != 2:
    fail("headless target scenario did not emit exactly two actor target_changed events")

set_event, clear_event = target_events
if set_event.get("message") != "target_set":
    fail("headless target scenario did not record target_set first")
if set_event.get("target", {}).get("entity_id") != target.get("entity_id"):
    fail("headless target scenario target_set event targeted the wrong NPC")
if clear_event.get("message") != "target_cleared":
    fail("headless target scenario did not record target_cleared second")
if clear_event.get("previous_target", {}).get("entity_id") != target.get("entity_id"):
    fail("headless target scenario did not clear the expected target")
if clear_event.get("target") is not None:
    fail("headless target scenario target_cleared event kept a target payload")

event_ids = [event.get("id", 0) for event in target_events]
if any(event_id <= start for event_id in event_ids):
    fail("headless target scenario included a stale target_changed event")
if event_ids[-1] != end:
    fail("headless target scenario final cursor did not include cleanup event")
PY
}

assert_headless_target_cursor_progression() {
  local first_payload="$1"
  local second_payload="$2"

  HEADLESS_TARGET_FIRST="$first_payload" HEADLESS_TARGET_SECOND="$second_payload" python3 - <<'PY'
import json
import os
import sys

first = json.loads(os.environ["HEADLESS_TARGET_FIRST"])
second = json.loads(os.environ["HEADLESS_TARGET_SECOND"])

def fail(message):
    print(json.dumps({"error": message, "first": first, "second": second}, separators=(",", ":")), file=sys.stderr)
    sys.exit(1)

if second.get("event_cursor_start", 0) < first.get("event_cursor_end", 0):
    fail("second headless target scenario started before the first scenario cleanup cursor")
PY
}

assert_empty_event_payload() {
  local payload="$1"
  local description="$2"

  EVENT_PAYLOAD="$payload" python3 - "$description" <<'PY'
import json
import os
import sys

description = sys.argv[1]
payload = json.loads(os.environ["EVENT_PAYLOAD"])
events = payload.get("events")
if events != []:
    print(json.dumps({"error": description, "payload": payload}, separators=(",", ":")), file=sys.stderr)
    sys.exit(1)
PY
}

assert_autonomous_actor_loop() {
  local payload="$1"

  ACTOR_LOOP_PAYLOAD="$payload" python3 - <<'PY'
import json
import os
import sys

payload = json.loads(os.environ["ACTOR_LOOP_PAYLOAD"])

def fail(message):
    print(json.dumps({"error": message, "payload": payload}, separators=(",", ":")), file=sys.stderr)
    sys.exit(1)

if payload.get("completed") is not True:
    fail("autonomous actor loop did not complete")
if payload.get("persistent_actor") is not False:
    fail("autonomous actor loop reported persistence")
if not str(payload.get("database_mutation", "")).startswith("none:"):
    fail("autonomous actor loop reported database mutation")

actor = payload.get("actor") or {}
owner = payload.get("owner") or {}
status = payload.get("status") or {}
perception = payload.get("perception") or {}
current_target = perception.get("current_target") or {}
actions = payload.get("actions") or []
events = payload.get("events") or []

if not actor.get("entity_id") or not owner.get("entity_id"):
    fail("actor or owner identity missing")
if status.get("actor", {}).get("entity_id") != actor.get("entity_id"):
    fail("status actor identity mismatch")
if status.get("owner", {}).get("entity_id") != owner.get("entity_id"):
    fail("status owner identity mismatch")
if current_target.get("name") != "HarnessActorPrimaryTarget":
    fail("unexpected perception current target")

if payload.get("tick_budget", 0) <= 0 or payload.get("ticks_processed", 0) <= 0:
    fail("tick budget was not processed")
if payload.get("event_cursor_end", 0) <= payload.get("event_cursor_start", 0):
    fail("event cursor did not advance")

action_kinds = [action.get("kind") for action in actions]
if action_kinds[:2] != ["target", "say"]:
    fail("unexpected autonomous actor action order")
if any(action.get("observed") is not True for action in actions[:2]):
    fail("expected autonomous actor actions were not observed")

nearby_names = {entity.get("entity", {}).get("name") for entity in perception.get("nearby_entities") or []}
if "HarnessActorPrimaryTarget" not in nearby_names:
    fail("perception did not include the primary target")

target_events = [
    event for event in events
    if event.get("type") == "target_changed"
    and event.get("actor", {}).get("entity_id") == actor.get("entity_id")
    and event.get("target", {}).get("name") == "HarnessActorPrimaryTarget"
]
if not target_events:
    fail("target_changed event for actor target action was not observed")

speech_events = [
    event for event in events
    if event.get("type") == "speech_emitted"
    and event.get("actor", {}).get("entity_id") == actor.get("entity_id")
    and event.get("speech", {}).get("channel") == "say"
    and event.get("speech", {}).get("text") == "Harness autonomous actor ready."
]
if not speech_events:
    fail("speech_emitted event for actor say action was not observed")
PY
}

assert_actor_led_bot_party() {
  local payload="$1"
  local expected_followers="$2"

  ACTOR_PARTY_PAYLOAD="$payload" python3 - "$expected_followers" <<'PY'
import json
import os
import sys

payload = json.loads(os.environ["ACTOR_PARTY_PAYLOAD"])
expected_followers = int(sys.argv[1])

def fail(message):
    print(json.dumps({"error": message, "payload": payload}, separators=(",", ":")), file=sys.stderr)
    sys.exit(1)

if payload.get("proved") is not True:
    fail("actor-led bot party proof did not complete")
if not str(payload.get("database_mutation", "")).startswith("none:"):
    fail("actor-led bot party reported database mutation")
if payload.get("follower_count_requested") != expected_followers or payload.get("follower_count_created") != expected_followers:
    fail("unexpected follower count")

required_flags = [
    "all_bots_share_owner",
    "group_leader_change_to_actor_rejected",
    "followers_follow_actor_leader",
    "followers_clear_removed_actor_leader_follow_id",
    "owner_target_command_observed",
    "actor_target_command_observed",
    "owner_assist_command_observed",
    "actor_assist_command_observed",
    "owner_nearby_control_kept_combat_target",
    "owner_leash_default_observed",
    "actor_leash_source_kept_combat_target",
]
for flag in required_flags:
    if payload.get(flag) is not True:
        fail(f"{flag} was not proven")

required_ticks = payload.get("actor_leash_source_required_target_consecutive_ticks") or 0
observed_ticks = payload.get("actor_leash_source_target_consecutive_ticks") or 0
if required_ticks < 2:
    fail("actor leash proof did not require a sustained target window")
if observed_ticks < required_ticks:
    fail("actor leash proof did not sustain the hostile target long enough")

if len(payload.get("followers") or []) != expected_followers:
    fail("follower identities missing")
if payload.get("owner", {}).get("kind") != "client":
    fail("owner is not a client")
if payload.get("actor_leader", {}).get("kind") != "bot":
    fail("actor leader is not a bot")
if payload.get("group_leader", {}).get("kind") != "client":
    fail("group leader should remain the owner client")

owner_events = payload.get("owner_target_events") or []
owner_probe = payload.get("owner_target_probe_follower") or {}
owner_expected = payload.get("owner_target_expected_hostile") or {}
if not owner_probe.get("entity_id") or not owner_expected.get("entity_id"):
    fail("owner target probe follower or expected hostile identity missing")
if not any(
    event.get("type") == "spell_cast_started"
    and event.get("actor", {}).get("entity_id") == owner_probe.get("entity_id")
    and event.get("target", {}).get("entity_id") == owner_expected.get("entity_id")
    for event in owner_events
):
    fail("owner target did not produce the expected follower spell event")
actor_events = payload.get("actor_target_events") or []
actor_probe = payload.get("actor_target_probe_follower") or {}
actor_expected = payload.get("actor_target_expected_hostile") or {}
if not actor_probe.get("entity_id") or not actor_expected.get("entity_id"):
    fail("actor target probe follower or expected hostile identity missing")
if not any(
    event.get("type") == "spell_cast_started"
    and event.get("actor", {}).get("entity_id") == actor_probe.get("entity_id")
    and event.get("target", {}).get("entity_id") == actor_expected.get("entity_id")
    for event in actor_events
):
    fail("actor target did not produce the expected follower spell event")

owner_assist_events = payload.get("owner_assist_events") or []
owner_assist_probe = payload.get("owner_assist_probe_follower") or {}
owner_assist_expected = payload.get("owner_assist_expected_hostile") or {}
if not owner_assist_probe.get("entity_id") or not owner_assist_expected.get("entity_id"):
    fail("owner assist probe follower or expected hostile identity missing")
if not any(
    event.get("type") == "spell_cast_started"
    and event.get("actor", {}).get("entity_id") == owner_assist_probe.get("entity_id")
    and event.get("target", {}).get("entity_id") == owner_assist_expected.get("entity_id")
    for event in owner_assist_events
):
    fail("owner assist did not produce the expected follower spell event")

actor_assist_events = payload.get("actor_assist_events") or []
actor_assist_probe = payload.get("actor_assist_probe_follower") or {}
actor_assist_expected = payload.get("actor_assist_expected_hostile") or {}
if not actor_assist_probe.get("entity_id") or not actor_assist_expected.get("entity_id"):
    fail("actor assist probe follower or expected hostile identity missing")
if not any(
    event.get("type") == "spell_cast_started"
    and event.get("actor", {}).get("entity_id") == actor_assist_probe.get("entity_id")
    and event.get("target", {}).get("entity_id") == actor_assist_expected.get("entity_id")
    for event in actor_assist_events
):
    fail("actor assist did not produce the expected follower spell event")
PY
}

assert_bot_loot_request_scenario() {
  local payload="$1"
  BOT_LOOT_PAYLOAD="$payload" python3 - <<'PY'
import json, os, sys
payload = json.loads(os.environ["BOT_LOOT_PAYLOAD"])
def fail(message):
    print(json.dumps({"error": message, "payload": payload}, separators=(",", ":")), file=sys.stderr)
    sys.exit(1)
if payload.get("proved") is not True or payload.get("scenario") != "bot-loot-request-upgrade":
    fail("Bot Loot Request scenario did not prove the ordinary loot path")
if payload.get("positive_request_count") != 1 or payload.get("upgrade_score", 0) <= 0:
    fail("expected exactly one positive structured decision")
for field in ("requesting_bot", "upgrade_item_id", "upgrade_item_name", "target_slot", "target_slot_name", "deterministic_reason"):
    if payload.get(field) in (None, "", 0): fail("missing structured decision field: " + field)
for field in ("downgrade_suppressed", "duplicate_suppressed", "looted_item_reached_looter", "loot_completed", "dialogue_pending_at_loot_completion", "normal_processing_responsive", "bot_inventory_unchanged", "provider_independent"):
    if payload.get(field) is not True: fail("failed invariant: " + field)
elapsed_ms = payload.get("loot_completion_elapsed_ms")
budget_ms = payload.get("loot_completion_budget_ms")
if not isinstance(elapsed_ms, int) or elapsed_ms < 0:
    fail("missing loot completion latency observation")
if not isinstance(budget_ms, int) or budget_ms <= 0:
    fail("missing loot completion latency budget")
if elapsed_ms >= budget_ms:
    fail("loot completion exceeded responsiveness budget")
if payload.get("grouped_bot_count", 0) < 2 or not str(payload.get("database_mutation", "")).startswith("none:"):
    fail("fixture grouping or cleanup contract was not met")
print(json.dumps({"scenario": payload["scenario"], "proved": True, "bot": payload["requesting_bot"]["name"], "item_id": payload["upgrade_item_id"], "slot": payload["target_slot_name"], "score": payload["upgrade_score"], "reason": payload["deterministic_reason"]}, separators=(",", ":")))
PY
}

assert_bot_loot_request_failure_cleanup() {
  local payload="$1"
  BOT_LOOT_CLEANUP_PAYLOAD="$payload" python3 - <<'PY'
import json, os, sys
payload = json.loads(os.environ["BOT_LOOT_CLEANUP_PAYLOAD"])
def fail(message):
    print(json.dumps({"error": message, "payload": payload}, separators=(",", ":")), file=sys.stderr)
    sys.exit(1)
if payload.get("proved") is not True or payload.get("scenario") != "bot-loot-request-failure-cleanup":
    fail("failure-path cleanup scenario was not proved")
for field in (
    "failure_induced_after_overrides",
    "rules_restored",
    "fixture_entities_cleaned",
    "delivery_state_restored",
    "dialogue_provider_state_restored",
    "decision_observer_state_restored",
):
    if payload.get(field) is not True:
        fail("failed cleanup invariant: " + field)
print(json.dumps({"scenario": payload["scenario"], "proved": True}, separators=(",", ":")))
PY
}

assert_pressure_heal_scenario() {
  local payload="$1"

  SCENARIO_PAYLOAD="$payload" python3 - <<'PY'
import json
import os
import sys

payload = json.loads(os.environ["SCENARIO_PAYLOAD"])

def fail(message):
    print(json.dumps({"error": message, "payload": payload}, separators=(",", ":")), file=sys.stderr)
    sys.exit(1)

if payload.get("scenario") != "moderate-pressure-fast-heal":
    fail("unexpected scenario")
if payload.get("observed") is not True:
    fail("expected pressure-aware heal cast start was not observed")
if not str(payload.get("database_mutation", "")).startswith("none:"):
    fail("scenario reported database mutation")
if payload.get("requested_spell_type_name") != "RegularHeal":
    fail("unexpected requested heal category")
if payload.get("expected_spell_type_name") != "FastHeals":
    fail("unexpected selected heal category")
if int(payload.get("expected_spell_id", 0)) <= 0:
    fail("expected heal spell id was not prepared")
if int(payload.get("heal_target_hp_percent", 0)) <= 0 or int(payload.get("pressure_damage", 0)) <= 0:
    fail("pressure setup details missing")
if int(payload.get("ticks_processed", 0)) <= 0 or int(payload.get("max_ticks", 0)) <= 0:
    fail("runtime bounds missing")

owner = payload.get("owner") or {}
bot = payload.get("bot") or {}
heal_target = payload.get("heal_target") or {}
if not owner.get("name") or not bot.get("name") or not heal_target.get("name"):
    fail("scenario fixture identities missing")

events = payload.get("events") or []
matching = [
    event for event in events
    if event.get("type") == "spell_cast_started"
    and event.get("caster", {}).get("entity_id") == bot.get("entity_id")
    and event.get("target", {}).get("entity_id") == heal_target.get("entity_id")
    and event.get("spell", {}).get("id") == payload.get("expected_spell_id")
]
if not matching:
    fail("expected pressure-aware heal cast-start event was not present")
PY
}

scenario=$(curl -fsS -X POST -H 'Content-Type: application/json' --data '{"spell_id":200}' "http://127.0.0.1:${port}/api/v1/harness/scenarios/spell-cast-start")
[[ "$scenario" == *'"started":true'* ]] || { printf '%s\n' "$scenario" >&2; exit 1; }

cast_events=''
for _ in $(seq 1 20); do
  cast_events=$(curl -fsS "http://127.0.0.1:${port}/api/v1/harness/events?since=0&limit=10")
  if [[ "$cast_events" == *'"type":"spell_cast_started"'* ]]; then
    break
  fi
  sleep 1
done
[[ "$cast_events" == *'"type":"spell_cast_started"'* ]] || { printf '%s\n' "$cast_events" >&2; exit 1; }
[[ "$cast_events" == *'"caster"'* && "$cast_events" == *'"target"'* && "$cast_events" == *'"spell"'* && "$cast_events" == *'"cast"'* ]] || { printf '%s\n' "$cast_events" >&2; exit 1; }

headless_target_first=$(curl -fsS -X POST "http://127.0.0.1:${port}/api/v1/harness/scenarios/headless-client/target")
assert_headless_target_scenario "$headless_target_first"

headless_target_first_end=$(HEADLESS_TARGET_PAYLOAD="$headless_target_first" python3 - <<'PY'
import json
import os

payload = json.loads(os.environ["HEADLESS_TARGET_PAYLOAD"])
print(payload["event_cursor_end"])
PY
)
headless_cleanup_events=$(curl -fsS "http://127.0.0.1:${port}/api/v1/harness/events?since=${headless_target_first_end}&limit=10")
assert_empty_event_payload "$headless_cleanup_events" "headless target scenario left post-cleanup events after its reported cursor"

headless_target_second=$(curl -fsS -X POST "http://127.0.0.1:${port}/api/v1/harness/scenarios/headless-client/target")
assert_headless_target_scenario "$headless_target_second"
assert_headless_target_cursor_progression "$headless_target_first" "$headless_target_second"

slow_scenario=$(curl -fsS -X POST "http://127.0.0.1:${port}/api/v1/harness/scenarios/bot-slow-maintenance/current-target")
assert_slow_scenario "$slow_scenario" current-target HarnessSlowCurrentTarget

entities_zero_sample=$(curl -fsS "http://127.0.0.1:${port}/api/v1/harness/entities?sample_limit=0")
ENTITIES_ZERO_SAMPLE="$entities_zero_sample" python3 - <<'PY'
import json
import os
import sys

payload = json.loads(os.environ["ENTITIES_ZERO_SAMPLE"])
counts = payload.get("counts") or {}
if counts.get("mobs", 0) <= 0 or payload.get("sample") != []:
    print(json.dumps(payload, separators=(",", ":")), file=sys.stderr)
    sys.exit(1)
PY

fallback_scenario=$(curl -fsS -X POST "http://127.0.0.1:${port}/api/v1/harness/scenarios/bot-slow-maintenance/fallback")
assert_slow_scenario "$fallback_scenario" fallback HarnessSlowFallbackHostile true

mezzed_scenario=$(curl -fsS -X POST "http://127.0.0.1:${port}/api/v1/harness/scenarios/bot-slow-maintenance/mezzed")
assert_slow_scenario "$mezzed_scenario" mezzed HarnessSlowSecondaryHostile false true

pressure_heal_scenario=$(curl -fsS -X POST "http://127.0.0.1:${port}/api/v1/harness/scenarios/owned-bot-healing/moderate-pressure-fast-heal")
assert_pressure_heal_scenario "$pressure_heal_scenario"

bot_loot_request_cleanup=$(curl -fsS -X POST "http://127.0.0.1:${port}/api/v1/harness/scenarios/bot-loot-request/failure-cleanup")
assert_bot_loot_request_failure_cleanup "$bot_loot_request_cleanup"

bot_loot_request_scenario=$(curl -fsS -X POST "http://127.0.0.1:${port}/api/v1/harness/scenarios/bot-loot-request/upgrade")
# Validate and persist the same compact record that the host wrapper publishes.
assert_bot_loot_request_scenario "$bot_loot_request_scenario" >"$BOT_LOOT_RESULT_FILE"

	actor_loop=$(curl -fsS -X POST "http://127.0.0.1:${port}/api/v1/harness/scenarios/autonomous-actor-loop")
	assert_autonomous_actor_loop "$actor_loop"
	actor_loop_repeat=$(curl -fsS -X POST "http://127.0.0.1:${port}/api/v1/harness/scenarios/autonomous-actor-loop")
	assert_autonomous_actor_loop "$actor_loop_repeat"

	actor_party_min=$(curl -fsS -X POST -H 'Content-Type: application/json' --data '{"follower_count":1}' "http://127.0.0.1:${port}/api/v1/harness/scenarios/actor-led-bot-party")
	assert_actor_led_bot_party "$actor_party_min" 1
	actor_party_default=$(curl -fsS -X POST -H 'Content-Type: application/json' --data '{"follower_count":3}' "http://127.0.0.1:${port}/api/v1/harness/scenarios/actor-led-bot-party")
	assert_actor_led_bot_party "$actor_party_default" 3
	actor_party_max=$(curl -fsS -X POST -H 'Content-Type: application/json' --data '{"follower_count":4}' "http://127.0.0.1:${port}/api/v1/harness/scenarios/actor-led-bot-party")
	assert_actor_led_bot_party "$actor_party_max" 4

	shutdown=$(curl -fsS -X POST "http://127.0.0.1:${port}/api/v1/harness/shutdown")
[[ "$shutdown" == *'"shutdown_requested":true'* ]] || { printf '%s\n' "$shutdown" >&2; exit 1; }

wait "$harness_pid"
trap - EXIT
ZONE_HARNESS_CONTAINER
)"
  # Disable Compose's pseudo-terminal and bridge the compact result through a
  # temporary directory mount. Printing it from this host-side wrapper ensures the
  # Validation Worker captures it even when Compose consumes container output.
  compose_status=0
  "${harness_compose[@]}" run --rm --no-deps -T \
    -e ZONE_HARNESS_PORT="$port" \
    -e ZONE_HARNESS_LOG_FILE=/tmp/zone-harness-result/zone_harness.out \
    -e BOT_LOOT_RESULT_FILE=/tmp/zone-harness-result/result.json \
    -v "$result_dir:/tmp/zone-harness-result" \
    --entrypoint bash eqemu-server -lc "$container_script" || compose_status=$?
  if [[ "$compose_status" -ne 0 ]]; then
    # Compose output can be lost when this wrapper itself is redirected. The
    # bind-mounted log is a deterministic fallback for Validation Worker logs.
    [[ -s "$harness_log" ]] && cat "$harness_log" >&2
    die "Zone Harness container failed with status $compose_status"
  fi
  [[ -s "$result_file" ]] || die "Bot Loot Request scenario produced no structured result"
  cat "$result_file"
)

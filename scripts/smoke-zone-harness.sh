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

cat >"$compose_override" <<'COMPOSE'
services:
  eqemu-server:
    ports: !override []
COMPOSE

cleanup() {
  rm -f "$compose_override"
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
  "${harness_compose[@]}" run --rm --no-deps --entrypoint bash eqemu-server -lc "
set -euo pipefail

until mysqladmin status -ueqemu -p\"\$EQEMU_DB_PASSWORD\" -h mariadb --silent; do
  sleep 1
done

runtime=/tmp/zone-harness-validation-runtime
rm -rf \"\$runtime\"
mkdir -p \"\$runtime/bin\" \"\$runtime/logs\" \"\$runtime/maps\" \"\$runtime/quests\"
ln -s ~/code/build/bin/zone \"\$runtime/bin/zone\"
ln -s ~/code/build/bin/shared_memory \"\$runtime/bin/shared_memory\"
ln -s ~/server/eqemu_config.json \"\$runtime/eqemu_config.json\"
ln -s ~/server/plugins \"\$runtime/plugins\"
ln -s ~/server/lua_modules \"\$runtime/lua_modules\"
ln -s ~/server/shared \"\$runtime/shared\"
cd \"\$runtime\"

dump_harness_log() {
  status=\$?
  if [[ \"\$status\" -ne 0 && -f logs/zone_harness.out ]]; then
    cat logs/zone_harness.out >&2
  fi
  exit \"\$status\"
}
trap dump_harness_log EXIT

./bin/zone tests:serve-http --zone qrg --port ${port} --max-runtime-seconds 30 > logs/zone_harness.out 2>&1 &
harness_pid=\$!
trap 'kill -TERM \"\$harness_pid\" 2>/dev/null || true; dump_harness_log' EXIT

health=''
for _ in \$(seq 1 60); do
  if health=\$(curl -fsS \"http://127.0.0.1:${port}/api/v1/harness/health\" 2>/dev/null); then
    break
  fi
  if ! kill -0 \"\$harness_pid\" 2>/dev/null; then
    cat logs/zone_harness.out >&2
    exit 1
  fi
  sleep 1
done

[[ \"\$health\" == *'\"healthy\":true'* ]] || { printf '%s\n' \"\$health\" >&2; exit 1; }

zone=\$(curl -fsS \"http://127.0.0.1:${port}/api/v1/harness/zone\")
[[ \"\$zone\" == *'\"short_name\":\"qrg\"'* ]] || { printf '%s\n' \"\$zone\" >&2; exit 1; }

entities=\$(curl -fsS \"http://127.0.0.1:${port}/api/v1/harness/entities\")
[[ \"\$entities\" == *'\"counts\"'* ]] || { printf '%s\n' \"\$entities\" >&2; exit 1; }

process=\$(curl -fsS -X POST -H 'Content-Type: application/json' --data '{\"ticks\":2}' \"http://127.0.0.1:${port}/api/v1/harness/process\")
[[ \"\$process\" == *'\"ticks_processed\":2'* ]] || { printf '%s\n' \"\$process\" >&2; exit 1; }

events=\$(curl -fsS \"http://127.0.0.1:${port}/api/v1/harness/events\")
[[ \"\$events\" == *'\"events\":[]'* ]] || { printf '%s\n' \"\$events\" >&2; exit 1; }

assert_slow_scenario() {
  local payload=\"\$1\"
  local scenario=\"\$2\"
  local expected_name=\"\$3\"
  local require_current_slowed=\"\${4:-false}\"
  local require_mezzed=\"\${5:-false}\"

  SCENARIO_PAYLOAD=\"\$payload\" python3 - \"\$scenario\" \"\$expected_name\" \"\$require_current_slowed\" \"\$require_mezzed\" <<'PY'
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

scenario=\$(curl -fsS -X POST -H 'Content-Type: application/json' --data '{\"spell_id\":200}' \"http://127.0.0.1:${port}/api/v1/harness/scenarios/spell-cast-start\")
[[ \"\$scenario\" == *'\"started\":true'* ]] || { printf '%s\n' \"\$scenario\" >&2; exit 1; }

cast_events=''
for _ in \$(seq 1 20); do
  cast_events=\$(curl -fsS \"http://127.0.0.1:${port}/api/v1/harness/events?since=0&limit=10\")
  if [[ \"\$cast_events\" == *'\"type\":\"spell_cast_started\"'* ]]; then
    break
  fi
  sleep 1
done
[[ \"\$cast_events\" == *'\"type\":\"spell_cast_started\"'* ]] || { printf '%s\n' \"\$cast_events\" >&2; exit 1; }
[[ \"\$cast_events\" == *'\"caster\"'* && \"\$cast_events\" == *'\"target\"'* && \"\$cast_events\" == *'\"spell\"'* && \"\$cast_events\" == *'\"cast\"'* ]] || { printf '%s\n' \"\$cast_events\" >&2; exit 1; }

slow_scenario=\$(curl -fsS -X POST \"http://127.0.0.1:${port}/api/v1/harness/scenarios/bot-slow-maintenance/current-target\")
assert_slow_scenario \"\$slow_scenario\" current-target HarnessSlowCurrentTarget

entities_zero_sample=\$(curl -fsS \"http://127.0.0.1:${port}/api/v1/harness/entities?sample_limit=0\")
ENTITIES_ZERO_SAMPLE=\"\$entities_zero_sample\" python3 - <<'PY'
import json
import os
import sys

payload = json.loads(os.environ["ENTITIES_ZERO_SAMPLE"])
counts = payload.get("counts") or {}
if counts.get("mobs", 0) <= 0 or payload.get("sample") != []:
    print(json.dumps(payload, separators=(",", ":")), file=sys.stderr)
    sys.exit(1)
PY

fallback_scenario=\$(curl -fsS -X POST \"http://127.0.0.1:${port}/api/v1/harness/scenarios/bot-slow-maintenance/fallback\")
assert_slow_scenario \"\$fallback_scenario\" fallback HarnessSlowFallbackHostile true

mezzed_scenario=\$(curl -fsS -X POST \"http://127.0.0.1:${port}/api/v1/harness/scenarios/bot-slow-maintenance/mezzed\")
assert_slow_scenario \"\$mezzed_scenario\" mezzed HarnessSlowSecondaryHostile false true

shutdown=\$(curl -fsS -X POST \"http://127.0.0.1:${port}/api/v1/harness/shutdown\")
[[ \"\$shutdown\" == *'\"shutdown_requested\":true'* ]] || { printf '%s\n' \"\$shutdown\" >&2; exit 1; }

wait \"\$harness_pid\"
trap - EXIT
"
)

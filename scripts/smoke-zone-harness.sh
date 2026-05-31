#!/usr/bin/env bash
set -euo pipefail

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
repo_root="$(cd "$script_dir/.." && pwd)"
stack_dir="${AKKSTACK_DIR:-$repo_root/../bump-akk-stack}"
port="${ZONE_HARNESS_PORT:-9099}"
compose_override="$(mktemp)"

cat >"$compose_override" <<'COMPOSE'
services:
  eqemu-server:
    ports: !override []
  mariadb:
    ports: !override []
COMPOSE

cleanup() {
  rm -f "$compose_override"
}
trap cleanup EXIT

compose=(docker-compose -f docker-compose.yml -f docker-compose.dev.yml -f "$compose_override")

die() {
  printf 'error: %s\n' "$*" >&2
  exit 1
}

"$repo_root/scripts/check-akkstack-contract.sh"

command -v docker-compose >/dev/null 2>&1 || die "docker-compose is required"

(
  cd "$stack_dir"
  "${compose[@]}" up -d mariadb >/dev/null
  "${compose[@]}" run --rm --no-deps --entrypoint bash eqemu-server -lc "
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

shutdown=\$(curl -fsS -X POST \"http://127.0.0.1:${port}/api/v1/harness/shutdown\")
[[ \"\$shutdown\" == *'\"shutdown_requested\":true'* ]] || { printf '%s\n' \"\$shutdown\" >&2; exit 1; }

wait \"\$harness_pid\"
trap - EXIT
"
)

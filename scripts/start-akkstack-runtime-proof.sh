#!/usr/bin/env bash
set -euo pipefail

repo_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
akkstack_dir="${AKKSTACK_DIR:-"${repo_dir}/../bump-akk-stack"}"
min_zone_timeout_seconds="${AKKSTACK_ZONE_READY_TIMEOUT_SECONDS:-120}"
supervised_zone_count="${AKKSTACK_SUPERVISED_ZONE_COUNT:-5}"

die() {
  printf 'error: %s\n' "$*" >&2
  exit 1
}

info() {
  printf '%s\n' "$*"
}

command -v docker-compose >/dev/null 2>&1 || die "docker-compose is required"
command -v ss >/dev/null 2>&1 || die "ss is required"

"${repo_dir}/scripts/check-akkstack-contract.sh"

[[ -d "${akkstack_dir}" ]] || die "AkkStack directory not found: ${akkstack_dir}"
[[ -f "${akkstack_dir}/docker-compose.yml" ]] || die "AkkStack docker-compose.yml not found"

compose_args=(-f docker-compose.yml)

if grep -q '^ENV=development$' "${akkstack_dir}/.env" 2>/dev/null; then
  compose_args+=(-f docker-compose.dev.yml)
fi

if ss -ltn "sport = :8080" | awk 'NR > 1 { found = 1 } END { exit found ? 0 : 1 }'; then
  if [[ -f "${akkstack_dir}/docker-compose.local.yml" ]]; then
    info "Host port 8080 is already listening; using AkkStack docker-compose.local.yml override."
    compose_args+=(-f docker-compose.local.yml)
  else
    die "host port 8080 is already listening and ${akkstack_dir}/docker-compose.local.yml is missing"
  fi
fi

cd "${akkstack_dir}"

info "Starting dev AkkStack database and EQEmu server containers..."
COMPOSE_HTTP_TIMEOUT=1000 docker-compose "${compose_args[@]}" up -d mariadb eqemu-server >/dev/null

info "Waiting for MariaDB from the EQEmu container..."
docker-compose "${compose_args[@]}" exec -T eqemu-server bash -lc \
  'while ! mysqladmin status -ueqemu -p"$EQEMU_DB_PASSWORD" -h mariadb --silent; do sleep .5; done'

if docker-compose "${compose_args[@]}" exec -T eqemu-server bash -lc 'test -x ~/server/bin/spire'; then
  info "Restarting Spire launcher-managed runtime processes..."
  docker-compose "${compose_args[@]}" exec -T eqemu-server bash -lc \
    'cd ~/server && ./bin/spire spire:launcher restart'

  min_zones="$(
    docker-compose "${compose_args[@]}" exec -T eqemu-server bash -lc \
      'jq -r ".[\"web-admin\"].launcher.minZoneProcesses // 0" ~/server/eqemu_config.json' | tr -d '\r'
  )"
else
  info "Spire launcher is unavailable; starting dev supervised runtime with ${supervised_zone_count} dynamic zones."
  docker-compose "${compose_args[@]}" exec -T eqemu-server bash -lc "
    cd ~/server
    if [[ -f /tmp/runtime-proof-zone-supervisor.pid ]]; then
      kill -TERM \"\$(cat /tmp/runtime-proof-zone-supervisor.pid)\" 2>/dev/null || true
    fi
    for process_name in eqlaunch zone ucs world queryserv loginserver; do
      pkill -TERM -x \"\${process_name}\" 2>/dev/null || true
    done
    sleep 2
    if [[ -f /tmp/runtime-proof-zone-supervisor.pid ]]; then
      kill -KILL \"\$(cat /tmp/runtime-proof-zone-supervisor.pid)\" 2>/dev/null || true
      rm -f /tmp/runtime-proof-zone-supervisor.pid
    fi
    for process_name in eqlaunch zone ucs world queryserv loginserver; do
      pkill -KILL -x \"\${process_name}\" 2>/dev/null || true
    done
    mkdir -p logs
    nohup ./bin/shared_memory > logs/shared_memory_runtime_proof.out 2>&1 &
    nohup ./bin/world > logs/world_runtime_proof.out 2>&1 &
  "

  info "Waiting for world before starting supervised zones..."
  world_deadline=$((SECONDS + 60))
  until docker-compose "${compose_args[@]}" exec -T eqemu-server bash -lc 'pgrep -x world >/dev/null'; do
    (( SECONDS < world_deadline )) || die "world did not start within 60s"
    sleep 2
  done

  docker-compose "${compose_args[@]}" exec -T eqemu-server bash -lc "
    cd ~/server
    nohup ./bin/ucs > logs/ucs_runtime_proof.out 2>&1 &
    cat > /tmp/runtime-proof-zone-supervisor.sh <<'SUPERVISOR'
#!/usr/bin/env bash
set -u
cd /home/eqemu/server || exit 1
target_count=\"\${1:-5}\"
mkdir -p logs/zone
while true; do
  current_count=\"\$(pgrep -cx zone || true)\"
  while (( current_count < target_count )); do
    stamp=\"\$(date +%Y%m%d%H%M%S)\"
    nohup ./bin/zone > \"logs/zone/runtime_proof_zone_\${stamp}_\${current_count}.out\" 2>&1 &
    sleep 2
    current_count=\"\$(pgrep -cx zone || true)\"
  done
  sleep 5
done
SUPERVISOR
    chmod +x /tmp/runtime-proof-zone-supervisor.sh
    nohup /tmp/runtime-proof-zone-supervisor.sh '${supervised_zone_count}' > logs/runtime_proof_zone_supervisor.out 2>&1 &
    echo \$! > /tmp/runtime-proof-zone-supervisor.pid
  "

  min_zones="${supervised_zone_count}"
fi

[[ "${min_zones}" =~ ^[0-9]+$ ]] || die "could not determine launcher zone capacity"

info "Waiting for stable zone capacity..."
deadline=$((SECONDS + min_zone_timeout_seconds))
while (( SECONDS < deadline )); do
  status="$(
    docker-compose "${compose_args[@]}" exec -T eqemu-server bash -lc '
      zone_count="$(pgrep -cx zone || true)"
      world_count="$(pgrep -cx world || true)"
      ucs_count="$(pgrep -cx ucs || true)"
      printf "%s %s %s\n" "${zone_count}" "${world_count}" "${ucs_count}"
    ' | tr -d '\r'
  )"

  read -r zone_count world_count ucs_count <<<"${status}"

  if (( world_count >= 1 && zone_count >= min_zones && min_zones > 0 )); then
    info "Runtime ready: world=${world_count}, ucs=${ucs_count}, zones=${zone_count}/${min_zones}."
    info "Client-visible login/zoning smoke can now run against the dev server."
    exit 0
  fi

  sleep 2
done

docker-compose "${compose_args[@]}" exec -T eqemu-server bash -lc \
  'printf "world=%s ucs=%s zones=%s\n" \
    "$(pgrep -cx world || true)" \
    "$(pgrep -cx ucs || true)" \
    "$(pgrep -cx zone || true)"' >&2
printf 'min_zones=%s\n' "${min_zones}" >&2

die "zone capacity did not become ready within ${min_zone_timeout_seconds}s"

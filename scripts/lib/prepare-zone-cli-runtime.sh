#!/usr/bin/env bash
set -euo pipefail

runtime="${1:-}"
if [[ ! "$runtime" =~ ^/tmp/[A-Za-z0-9][A-Za-z0-9_-]*-runtime$ ]]; then
  printf 'error: zone CLI runtime must be one safe /tmp basename ending in -runtime\n' >&2
  exit 2
fi

db_ready_timeout_seconds="${ZONE_CLI_DB_READY_TIMEOUT_SECONDS:-60}"
if [[ ! "$db_ready_timeout_seconds" =~ ^[1-9][0-9]*$ ]]; then
  printf 'error: ZONE_CLI_DB_READY_TIMEOUT_SECONDS must be a positive integer\n' >&2
  exit 2
fi

db_ready_deadline=$((SECONDS + db_ready_timeout_seconds))
until mysqladmin status -ueqemu -p"$EQEMU_DB_PASSWORD" -h mariadb --connect-timeout=2 --silent; do
  if (( SECONDS >= db_ready_deadline )); then
    printf 'error: MariaDB service mariadb was not ready within %s seconds; inspect the selected AkkStack MariaDB service logs\n' "$db_ready_timeout_seconds" >&2
    exit 1
  fi
  sleep 1
done

rm -rf -- "$runtime"
mkdir -p "$runtime"
jq '.server.database.host = "mariadb" | .server.database.port = "3306" | .server.qsdatabase.host = "mariadb" | .server.qsdatabase.port = "3306"' \
  ~/server/eqemu_config.json >"$runtime/eqemu_config.json"

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

  printf 'missing runtime directory for %s\n' "$target" >&2
  return 1
}

link_runtime_dir shared ~/server/shared
link_runtime_dir plugins ~/server/quests/plugins ~/server/plugins
link_runtime_dir lua_modules ~/server/quests/lua_modules ~/server/lua_modules

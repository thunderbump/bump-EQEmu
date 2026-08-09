#!/usr/bin/env bash
set -euo pipefail

runtime="${1:-}"
if [[ "$runtime" != /tmp/*-runtime ]]; then
  printf 'error: zone CLI runtime must be a named /tmp/*-runtime directory\n' >&2
  exit 2
fi

until mysqladmin status -ueqemu -p"$EQEMU_DB_PASSWORD" -h mariadb --silent; do
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

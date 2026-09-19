#!/usr/bin/env bash
# Runs only inside the disposable rehearsal container. Never load an AkkStack config.
set -euo pipefail
cd /runtime
mkdir -p logs shared maps quests
cp -a /inputs/shared/. shared/
ln -s /inputs/plugins plugins
ln -s /inputs/lua_modules lua_modules
jq -n --arg password "$REHEARSAL_PASSWORD" '
  {host:"mariadb",port:"3306",db:"peq",username:"root",password:$password} as $db |
  {server:{database:$db,qsdatabase:$db,content_database:$db,auto_database_updates:false,
    world:{shortname:"afk_rehearsal",longname:"Isolated AFK rehearsal",locked:true},
    directories:{maps:"maps",quests:"quests",plugins:"plugins",lua_modules:"lua_modules",shared_memory:"shared",logs:"logs"}}}
' > eqemu_config.json
unset REHEARSAL_PASSWORD
case "$1" in
  update)
    /home/eqemu/code/build/bin/world database:updates --skip-backup --force
    ;;
  scenarios)
    # JSON strings may contain newlines; NUL framing preserves each array
    # element as one shell command.
    while IFS= read -r -d '' command; do
      bash -lc "$command"
    done < <(jq -j '.[] + "\u0000"' <<<"$MIGRATION_SCENARIOS_JSON")
    ;;
  recovery)
    [[ "$(sha256sum "$OLD_WORLD" | cut -d ' ' -f1)" == "$OLD_WORLD_SHA" ]]
    "$OLD_WORLD" > startup.log 2>&1 &
    world_pid=$!
    # Checking the process after the full window distinguishes a genuinely
    # surviving world from one that happens to exit with timeout's status 124.
    sleep 30
    if ! kill -0 "$world_pid" 2>/dev/null; then
      wait "$world_pid" || true
      cat startup.log
      printf 'error: archived world exited before the recovery window elapsed\n' >&2
      exit 1
    fi
    stop_world() {
      kill -TERM "$world_pid" 2>/dev/null || true
      for _ in 1 2 3 4 5; do
        kill -0 "$world_pid" 2>/dev/null || break
        sleep 1
      done
      if kill -0 "$world_pid" 2>/dev/null; then kill -KILL "$world_pid" 2>/dev/null || true; fi
      wait "$world_pid" || true
    }
    if ! grep -Fq 'Server (TCP) listener started' startup.log; then
      stop_world
      cat startup.log
      printf 'error: archived world did not reach its TCP listener\n' >&2
      exit 1
    fi
    stop_world
    cat startup.log
    ;;
  *) exit 2 ;;
esac

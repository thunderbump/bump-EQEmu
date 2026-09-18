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
  scenarios|no-scenarios)
    /home/eqemu/code/build/bin/world database:updates --skip-backup --force
    if [[ "$1" == scenarios ]]; then
      while IFS= read -r command; do bash -lc "$command"; done < <(jq -r '.[]' <<<"$MIGRATION_SCENARIOS_JSON")
    fi
    ;;
  recovery)
    [[ "$(sha256sum "$OLD_WORLD" | cut -d ' ' -f1)" == "$OLD_WORLD_SHA" ]]
    startup_status=0
    timeout --signal=TERM --kill-after=5s 30s "$OLD_WORLD" > startup.log 2>&1 || startup_status=$?
    cat startup.log
    [[ "$startup_status" == 124 ]] && grep -Fq 'Server (TCP) listener started' startup.log
    ;;
  *) exit 2 ;;
esac

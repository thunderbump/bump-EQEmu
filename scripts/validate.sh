#!/usr/bin/env bash
set -euo pipefail

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
repo_root="$(cd "$script_dir/.." && pwd)"
source "$script_dir/lib/akkstack-routing.sh"

akkstack_init_routing "$repo_root" validation "$@"
stack_dir="$AKKSTACK_STACK_DIR"
compose_files=(docker-compose.yml docker-compose.dev.yml)
compose=(docker-compose)
for compose_file in "${compose_files[@]}"; do
  compose+=(-f "$compose_file")
done

usage() {
  cat <<'USAGE'
Usage: scripts/validate.sh [--stack <validation|gameplay>] [--dry-run] <command>

Options:
  --stack <validation|gameplay>  Select the role default stack. Defaults to validation.
  --dry-run                     Print selected stack, Compose files, and action without Docker.
  -h, --help                    Show this help.

AKKSTACK_DIR=/path/to/stack remains an explicit custom-path override for the
selected role.

Commands:
  preflight       Verify the local AkkStack contract.
  tier1           Run the container build and unit test tier.
  tier2-readonly  Run read-mostly DB-backed zone CLI tests.
  tier3-harness   Run the canonical Zone Harness smoke.
  safe            Run preflight, tier1, and tier2-readonly.

The safe command intentionally does not run DB-mutating Tier 2 checks or Tier 3
Zone Harness validation. Use tier3-harness as an explicit opt-in command and
docs/testing/process.md for DB-mutating raw commands and their backup gate.

tier2-readonly and safe start or verify the selected AkkStack MariaDB service
with canonical Compose and then run read-mostly zone CLI checks in a one-off
eqemu-server container.
USAGE
}

validation_action() {
  case "$1" in
    preflight)
      printf '%s\n' "would verify the selected AkkStack contract"
      ;;
    tier1)
      printf '%s\n' "would run preflight, container build, and unit tests"
      ;;
    tier2-readonly)
      printf '%s\n' "would run preflight, start or verify MariaDB with canonical Compose (--no-recreate), and run tests:npc-handins and tests:npc-handins-multiquest as separate zone CLI processes in a single one-off eqemu-server container"
      ;;
    safe)
      printf '%s\n' "would run preflight, tier1, and tier2-readonly"
      ;;
  esac
}

run_preflight() {
  "$repo_root/scripts/check-akkstack-contract.sh" --stack "$AKKSTACK_STACK_ROLE"
}

run_tier1() {
  (
    cd "$stack_dir"
    "${compose[@]}" run --rm --no-deps --entrypoint bash eqemu-server -lc \
      'cd ~/code && cmake --preset linux-debug && cmake --build build --parallel && ./build/bin/tests'
  )
}

run_mariadb() {
  (
    cd "$stack_dir"
    "${compose[@]}" up -d --no-recreate mariadb
  )
}

run_tier2_readonly_zone_tests() {
  (
    cd "$stack_dir"
    "${compose[@]}" run --rm --no-deps --entrypoint bash eqemu-server -lc \
      'set -euo pipefail
until mysqladmin status -ueqemu -p"$EQEMU_DB_PASSWORD" -h mariadb --silent; do
  sleep 1
done
runtime=/tmp/zone-cli-validation-runtime
rm -rf "$runtime"
mkdir -p "$runtime"
jq ".server.database.host = \"mariadb\" | .server.database.port = \"3306\" | .server.qsdatabase.host = \"mariadb\" | .server.qsdatabase.port = \"3306\"" ~/server/eqemu_config.json > "$runtime/eqemu_config.json"
ln -s ~/server/shared "$runtime/shared"
ln -s ~/server/plugins "$runtime/plugins"
ln -s ~/server/lua_modules "$runtime/lua_modules"
cd "$runtime"
~/code/build/bin/zone tests:npc-handins
~/code/build/bin/zone tests:npc-handins-multiquest'
  )
}

run_tier2_readonly() {
  run_mariadb
  run_tier2_readonly_zone_tests
}

run_tier3_harness() {
  local smoke_args=(--stack "$AKKSTACK_STACK_ROLE")

  if [[ "$AKKSTACK_DRY_RUN" -eq 1 ]]; then
    smoke_args+=(--dry-run)
  fi

  "$repo_root/scripts/smoke-zone-harness.sh" "${smoke_args[@]}"
}

if [[ "$AKKSTACK_HELP" -eq 1 ]]; then
  usage
  exit 0
fi

if [[ "${#AKKSTACK_REMAINING_ARGS[@]}" -ne 1 ]]; then
  usage >&2
  exit 2
fi

command="${AKKSTACK_REMAINING_ARGS[0]}"

case "$command" in
  preflight|tier1|tier2-readonly|tier3-harness|safe)
    ;;
  *)
    usage >&2
    exit 2
    ;;
esac

if [[ "$command" == "tier3-harness" ]]; then
  run_tier3_harness
  exit 0
fi

akkstack_warn_if_validation_command_targets_gameplay "scripts/validate.sh"

if [[ "$AKKSTACK_DRY_RUN" -eq 1 ]]; then
  if [[ "$command" == "preflight" ]]; then
    akkstack_print_dry_run "$(validation_action "$command")"
  else
    akkstack_print_dry_run "$(validation_action "$command")" "${compose_files[@]}"
  fi
  exit 0
fi

case "$command" in
  preflight)
    run_preflight
    ;;
  tier1)
    run_preflight
    run_tier1
    ;;
  tier2-readonly)
    run_preflight
    run_tier2_readonly
    ;;
  safe)
    run_preflight
    run_tier1
    run_tier2_readonly
    ;;
esac

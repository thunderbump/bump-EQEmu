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
  safe            Run preflight, tier1, and tier2-readonly.

This wrapper intentionally does not run DB-mutating Tier 2 checks or Tier 3
live server smoke tests. Use docs/testing/process.md for those raw commands
and their backup gate.

tier2-readonly and safe require the AkkStack eqemu-server container to already
be running.
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
      printf '%s\n' "would run preflight and read-mostly zone CLI tests"
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

run_zone_test() {
  local test_name="$1"

  (
    cd "$stack_dir"
    "${compose[@]}" exec -T eqemu-server bash -lc "cd ~/server && ~/code/build/bin/zone $test_name"
  )
}

run_tier2_readonly() {
  run_zone_test tests:npc-handins
  run_zone_test tests:npc-handins-multiquest
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
  preflight|tier1|tier2-readonly|safe)
    ;;
  *)
    usage >&2
    exit 2
    ;;
esac

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

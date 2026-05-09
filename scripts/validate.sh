#!/usr/bin/env bash
set -euo pipefail

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
repo_root="$(cd "$script_dir/.." && pwd)"
stack_dir="${AKKSTACK_DIR:-$repo_root/../bump-akk-stack}"
compose=(docker-compose -f docker-compose.yml -f docker-compose.dev.yml)

usage() {
  cat <<'USAGE'
Usage: scripts/validate.sh <command>

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

run_preflight() {
  "$repo_root/scripts/check-akkstack-contract.sh"
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

if [[ $# -ne 1 ]]; then
  usage >&2
  exit 2
fi

case "$1" in
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
  -h|--help|help)
    usage
    ;;
  *)
    usage >&2
    exit 2
    ;;
esac

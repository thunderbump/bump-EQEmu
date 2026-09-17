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
  actor-queue-tier3
                  Run the DB-mutating durable actor queue integration scenario.
  actor-queue-stop
                  Stop and remove the current validation actor container.
  actor-queue-cleanup
                  Remove actor queue rows owned by the current validation token.
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
runtime=/tmp/zone-cli-validation-runtime
~/code/scripts/lib/prepare-zone-cli-runtime.sh "$runtime"
cd "$runtime"
~/code/build/bin/zone tests:npc-handins
~/code/build/bin/zone tests:npc-handins-multiquest'
  )
}

run_tier2_readonly() {
  run_mariadb
  run_tier2_readonly_zone_tests
}

require_actor_container_name() {
  [[ -n "${ACTOR_QUEUE_VALIDATION_CONTAINER:-}" ]] || {
    printf 'error: ACTOR_QUEUE_VALIDATION_CONTAINER is required\n' >&2
    return 2
  }
  [[ "$ACTOR_QUEUE_VALIDATION_CONTAINER" =~ ^[A-Za-z0-9][A-Za-z0-9_.-]*$ ]] || {
    printf 'error: invalid ACTOR_QUEUE_VALIDATION_CONTAINER\n' >&2
    return 2
  }
}

run_actor_queue_tier3() {
  require_actor_container_name
  run_preflight
  run_mariadb
  (
    cd "$stack_dir"
    "${compose[@]}" run --name "$ACTOR_QUEUE_VALIDATION_CONTAINER" --rm --no-deps -e ACTOR_QUEUE_VALIDATION_TOKEN --entrypoint bash eqemu-server -lc \
      'set -euo pipefail
test -x ~/code/build/bin/zone || { printf "error: actor-queue-tier3 requires a prior Tier 1 build; missing executable ~/code/build/bin/zone\n" >&2; exit 2; }
runtime=/tmp/actor-queue-tier3-runtime
~/code/scripts/lib/prepare-zone-cli-runtime.sh "$runtime"
cd "$runtime"
~/code/build/bin/zone tests:actor-events'
  )
}

run_actor_queue_stop() {
  local stabilization_seconds="${ACTOR_QUEUE_STOP_STABILIZATION_SECONDS:-2}"
  local absent_since_ns= now_ns= stabilization_ns
  require_actor_container_name
  [[ "$stabilization_seconds" =~ ^[0-9]+$ && "$stabilization_seconds" -gt 0 ]] || {
    printf 'error: ACTOR_QUEUE_STOP_STABILIZATION_SECONDS must be a positive integer\n' >&2
    return 2
  }
  stabilization_ns=$(( stabilization_seconds * 1000000000 ))

  # The Compose client is a local process, but its one-off container belongs to
  # the Docker daemon and can survive client termination. An accepted create
  # request may also finish after the client and the first inspect are gone.
  # Require a continuous absence window, restarting it whenever the named
  # container appears, before database cleanup or shared-lock release.
  while :; do
    if docker container inspect "$ACTOR_QUEUE_VALIDATION_CONTAINER" >/dev/null 2>&1; then
      absent_since_ns=
      docker container rm --force "$ACTOR_QUEUE_VALIDATION_CONTAINER" >/dev/null 2>&1 || true
    else
      # Distinguish confirmed absence from an unreachable daemon.
      docker info >/dev/null
      now_ns="$(date +%s%N)"
      if [[ -z "$absent_since_ns" ]]; then
        absent_since_ns="$now_ns"
      elif [[ $(( now_ns - absent_since_ns )) -ge "$stabilization_ns" ]]; then
        break
      fi
    fi
    sleep 0.1
  done

  docker info >/dev/null
  if docker container inspect "$ACTOR_QUEUE_VALIDATION_CONTAINER" >/dev/null 2>&1; then
    printf 'error: actor validation container remains present: %s\n' "$ACTOR_QUEUE_VALIDATION_CONTAINER" >&2
    return 1
  fi
}

run_actor_queue_cleanup() {
  require_actor_container_name
  run_preflight
  run_mariadb
  (
    cd "$stack_dir"
    "${compose[@]}" run --name "$ACTOR_QUEUE_VALIDATION_CONTAINER" --rm --no-deps -e ACTOR_QUEUE_VALIDATION_TOKEN --entrypoint bash eqemu-server -lc \
      'set -euo pipefail
runtime=/tmp/actor-queue-tier3-runtime
~/code/scripts/lib/prepare-zone-cli-runtime.sh "$runtime"
cd "$runtime"
~/code/build/bin/zone tests:actor-events --cleanup-only'
  )
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
  preflight|tier1|tier2-readonly|tier3-harness|actor-queue-tier3|actor-queue-stop|actor-queue-cleanup|safe)
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

if [[ "$AKKSTACK_DRY_RUN" -eq 1 && "$command" == "actor-queue-tier3" ]]; then
  akkstack_print_dry_run "would run tests:actor-events as a database-mutating runtime fixture; scenario-owned rows are cleaned up" "${compose_files[@]}"
  exit 0
fi

if [[ "$AKKSTACK_DRY_RUN" -eq 1 && "$command" == "actor-queue-stop" ]]; then
  akkstack_print_dry_run "would stop and remove the named actor validation container" "${compose_files[@]}"
  exit 0
fi

if [[ "$AKKSTACK_DRY_RUN" -eq 1 && "$command" == "actor-queue-cleanup" ]]; then
  akkstack_print_dry_run "would run tests:actor-events --cleanup-only for the validation token" "${compose_files[@]}"
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
  actor-queue-tier3)
    run_actor_queue_tier3
    ;;
  actor-queue-stop)
    run_actor_queue_stop
    ;;
  actor-queue-cleanup)
    run_actor_queue_cleanup
    ;;
  safe)
    run_preflight
    run_tier1
    run_tier2_readonly
    ;;
esac

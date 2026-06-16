#!/usr/bin/env bash
set -euo pipefail

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
repo_root="$(cd "$script_dir/.." && pwd)"
source "$script_dir/lib/akkstack-routing.sh"

usage() {
  cat <<'USAGE'
Usage: scripts/check-akkstack-contract.sh [--stack <validation|gameplay>] [--dry-run]

Options:
  --stack <validation|gameplay>  Select the role default stack. Defaults to validation.
  --dry-run                     Print the selected route without checking the stack.
  -h, --help                    Show this help.

AKKSTACK_DIR=/path/to/stack remains an explicit custom-path override for the
selected role.
USAGE
}

akkstack_init_routing "$repo_root" validation "$@"

if [[ "$AKKSTACK_HELP" -eq 1 ]]; then
  usage
  exit 0
fi

if [[ "${#AKKSTACK_REMAINING_ARGS[@]}" -ne 0 ]]; then
  usage >&2
  exit 2
fi

stack_dir="$AKKSTACK_STACK_DIR"
expected_checkout="${EXPECTED_EQEMU_CHECKOUT:-$repo_root}"
code_path="$stack_dir/code"

failures=0

note() {
  printf '%s\n' "$*"
}

fail() {
  note "FAIL: $*"
  failures=$((failures + 1))
}

note "AkkStack contract preflight"
note "  stack role: $AKKSTACK_STACK_ROLE"
note "  stack path: $stack_dir"
note "  path source: $AKKSTACK_PATH_SOURCE"
note "  expected: $(akkstack_resolve_path "$expected_checkout")"

if [[ "$AKKSTACK_DRY_RUN" -eq 1 ]]; then
  akkstack_print_compose_files
  note "  action: would verify selected AkkStack directory, .env, and code checkout contract"
  note "Dry run: Docker was not invoked."
  exit 0
fi

akkstack_require_selected_stack_dir
note "OK: AkkStack directory exists"

if [[ -f "$stack_dir/.env" ]]; then
  note "OK: AkkStack .env exists"
else
  fail "AkkStack .env is missing: $stack_dir/.env"
fi

if [[ ! -e "$code_path" ]]; then
  fail "AkkStack code path is missing: $code_path"
else
  if [[ -L "$code_path" ]]; then
    note "OK: AkkStack code path exists as a symlink"
    note "  symlink target: $(readlink "$code_path")"
  elif [[ -d "$code_path/.git" ]]; then
    note "OK: AkkStack code path exists as a directory checkout"
  elif [[ -d "$code_path" ]]; then
    note "OK: AkkStack code path exists as a directory"
  else
    fail "AkkStack code path exists but is not a directory or symlink: $code_path"
  fi

  if [[ -e "$code_path" ]]; then
    resolved_code="$(akkstack_resolve_path "$code_path")"
    resolved_expected="$(akkstack_resolve_path "$expected_checkout")"
    note "  resolved code: $resolved_code"

    if [[ "$resolved_code" == "$resolved_expected" ]]; then
      note "OK: AkkStack code points at the expected EQEmu checkout"
    else
      fail "AkkStack code points at '$resolved_code', expected '$resolved_expected'"
      note "Set EXPECTED_EQEMU_CHECKOUT=/path/to/accepted/checkout only when that alternate checkout is intentional."
    fi
  fi
fi

if [[ "$failures" -gt 0 ]]; then
  note "Preflight failed with $failures issue(s)."
  exit 1
fi

note "Preflight passed."

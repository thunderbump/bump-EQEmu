#!/usr/bin/env bash
set -euo pipefail

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
repo_root="$(cd "$script_dir/.." && pwd)"
stack_dir="${AKKSTACK_DIR:-$repo_root/../bump-akk-stack}"
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

resolve_path() {
  if command -v realpath >/dev/null 2>&1; then
    realpath "$1"
  else
    readlink -f "$1"
  fi
}

note "AkkStack contract preflight"
note "  stack:    $stack_dir"
note "  expected: $(resolve_path "$expected_checkout")"

if [[ ! -d "$stack_dir" ]]; then
  fail "AkkStack directory is missing: $stack_dir"
else
  note "OK: AkkStack directory exists"
fi

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
    resolved_code="$(resolve_path "$code_path")"
    resolved_expected="$(resolve_path "$expected_checkout")"
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

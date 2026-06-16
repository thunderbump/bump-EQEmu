#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
tmp_root="$(mktemp -d)"

cleanup() {
  rm -rf "$tmp_root"
}
trap cleanup EXIT

failures=0
assertion_failed=0

fail() {
  printf 'FAIL: %s\n' "$*" >&2
  failures=$((failures + 1))
}

assert_contains() {
  local haystack="$1"
  local needle="$2"

  if [[ "$haystack" != *"$needle"* ]]; then
    printf 'Expected output to contain:\n%s\n\nActual output:\n%s\n' "$needle" "$haystack" >&2
    assertion_failed=1
  fi
}

assert_not_contains() {
  local haystack="$1"
  local needle="$2"

  if [[ "$haystack" == *"$needle"* ]]; then
    printf 'Expected output not to contain:\n%s\n\nActual output:\n%s\n' "$needle" "$haystack" >&2
    assertion_failed=1
  fi
}

capture_run() {
  local -n status_ref="$1"
  local -n output_ref="$2"
  shift 2

  set +e
  output_ref="$("$@" 2>&1)"
  status_ref=$?
  set -e
}

make_stack() {
  local stack_dir="$1"
  local checkout_dir="$2"

  mkdir -p "$stack_dir"
  printf 'ENV=development\n' >"$stack_dir/.env"
  ln -s "$checkout_dir" "$stack_dir/code"
  touch "$stack_dir/docker-compose.yml"
  touch "$stack_dir/docker-compose.dev.yml"
}

make_fixture() {
  local -n fixture_repo_ref="$1"
  local -n fixture_parent_ref="$2"

  fixture_parent_ref="$(mktemp -d "$tmp_root/fixture.XXXXXX")"
  fixture_repo_ref="$fixture_parent_ref/bump-EQEmu"
  mkdir -p "$fixture_repo_ref"
  cp -R "$repo_root/scripts" "$fixture_repo_ref/"

  make_stack "$fixture_parent_ref/bump-akk-stack-validation" "$fixture_repo_ref"
  make_stack "$fixture_parent_ref/bump-akk-stack" "$fixture_repo_ref"
}

run_test() {
  local name="$1"
  shift

  assertion_failed=0
  if "$@" && [[ "$assertion_failed" -eq 0 ]]; then
    printf 'ok - %s\n' "$name"
  else
    fail "$name"
  fi
}

test_preflight_defaults_to_validation() {
  local fixture_repo fixture_parent status output
  make_fixture fixture_repo fixture_parent

  capture_run status output "$fixture_repo/scripts/check-akkstack-contract.sh"

  [[ "$status" -eq 0 ]] || return 1
  assert_contains "$output" "stack role: validation"
  assert_contains "$output" "stack path: $fixture_parent/bump-akk-stack-validation"
  assert_contains "$output" "Preflight passed."
}

test_runtime_proof_defaults_to_gameplay() {
  local fixture_repo fixture_parent status output
  make_fixture fixture_repo fixture_parent

  capture_run status output "$fixture_repo/scripts/start-akkstack-runtime-proof.sh" --dry-run

  [[ "$status" -eq 0 ]] || return 1
  assert_contains "$output" "stack role: gameplay"
  assert_contains "$output" "stack path: $fixture_parent/bump-akk-stack"
  assert_contains "$output" "compose files:"
  assert_contains "$output" "docker-compose.yml"
  assert_contains "$output" "docker-compose.dev.yml"
  assert_contains "$output" "action:"
}

test_explicit_roles_are_accepted_by_wrappers() {
  local fixture_repo fixture_parent status output
  make_fixture fixture_repo fixture_parent

  capture_run status output "$fixture_repo/scripts/check-akkstack-contract.sh" --stack gameplay --dry-run
  [[ "$status" -eq 0 ]] || return 1
  assert_contains "$output" "stack role: gameplay"

  capture_run status output "$fixture_repo/scripts/validate.sh" --stack validation --dry-run tier1
  [[ "$status" -eq 0 ]] || return 1
  assert_contains "$output" "stack role: validation"

  capture_run status output "$fixture_repo/scripts/validate.sh" --stack gameplay --dry-run tier1
  [[ "$status" -eq 0 ]] || return 1
  assert_contains "$output" "stack role: gameplay"

  capture_run status output "$fixture_repo/scripts/smoke-zone-harness.sh" --stack validation --dry-run
  [[ "$status" -eq 0 ]] || return 1
  assert_contains "$output" "stack role: validation"

  capture_run status output "$fixture_repo/scripts/smoke-zone-harness.sh" --stack gameplay --dry-run
  [[ "$status" -eq 0 ]] || return 1
  assert_contains "$output" "stack role: gameplay"

  capture_run status output "$fixture_repo/scripts/start-akkstack-runtime-proof.sh" --stack validation --dry-run
  [[ "$status" -eq 0 ]] || return 1
  assert_contains "$output" "stack role: validation"

  capture_run status output "$fixture_repo/scripts/start-akkstack-runtime-proof.sh" --stack gameplay --dry-run
  [[ "$status" -eq 0 ]] || return 1
  assert_contains "$output" "stack role: gameplay"
}

test_invalid_role_fails() {
  local fixture_repo fixture_parent status output
  make_fixture fixture_repo fixture_parent

  capture_run status output "$fixture_repo/scripts/validate.sh" --stack staging --dry-run tier1

  [[ "$status" -eq 2 ]] || return 1
  assert_contains "$output" "invalid --stack role"
}

test_custom_path_override() {
  local fixture_repo fixture_parent custom_stack status output
  make_fixture fixture_repo fixture_parent
  custom_stack="$fixture_parent/custom-akk-stack"
  make_stack "$custom_stack" "$fixture_repo"

  capture_run status output env AKKSTACK_DIR="$custom_stack" "$fixture_repo/scripts/check-akkstack-contract.sh" --stack gameplay

  [[ "$status" -eq 0 ]] || return 1
  assert_contains "$output" "stack role: gameplay"
  assert_contains "$output" "stack path: $custom_stack"
  assert_contains "$output" "path source: AKKSTACK_DIR"
  assert_contains "$output" "Preflight passed."
}

test_missing_default_validation_fails_clearly() {
  local fixture_repo fixture_parent status output
  make_fixture fixture_repo fixture_parent
  rm -rf "$fixture_parent/bump-akk-stack-validation"

  capture_run status output "$fixture_repo/scripts/check-akkstack-contract.sh"

  [[ "$status" -eq 1 ]] || return 1
  assert_contains "$output" "default validation AkkStack directory is missing"
  assert_not_contains "$output" "$fixture_parent/bump-akk-stack "
}

test_default_roles_must_not_resolve_to_same_directory() {
  local fixture_repo fixture_parent status output
  make_fixture fixture_repo fixture_parent
  rm -rf "$fixture_parent/bump-akk-stack-validation"
  ln -s "$fixture_parent/bump-akk-stack" "$fixture_parent/bump-akk-stack-validation"

  capture_run status output "$fixture_repo/scripts/check-akkstack-contract.sh" --dry-run

  [[ "$status" -eq 1 ]] || return 1
  assert_contains "$output" "default validation and gameplay AkkStack paths must not resolve to the same directory"
}

test_validation_commands_warn_on_gameplay_stack() {
  local fixture_repo fixture_parent status output
  make_fixture fixture_repo fixture_parent

  capture_run status output "$fixture_repo/scripts/validate.sh" --stack gameplay --dry-run tier1
  [[ "$status" -eq 0 ]] || return 1
  assert_contains "$output" "WARNING:"
  assert_contains "$output" "persistent gameplay data"

  capture_run status output "$fixture_repo/scripts/smoke-zone-harness.sh" --stack gameplay --dry-run
  [[ "$status" -eq 0 ]] || return 1
  assert_contains "$output" "WARNING:"
  assert_contains "$output" "persistent gameplay data"
}

test_help_mentions_stack_and_dry_run() {
  local fixture_repo fixture_parent status output script
  make_fixture fixture_repo fixture_parent

  for script in check-akkstack-contract.sh validate.sh smoke-zone-harness.sh start-akkstack-runtime-proof.sh; do
    capture_run status output "$fixture_repo/scripts/$script" --help
    [[ "$status" -eq 0 ]] || return 1
    assert_contains "$output" "--stack <validation|gameplay>"
    assert_contains "$output" "--dry-run"
  done
}

test_dry_run_prints_route_and_skips_docker() {
  local fixture_repo fixture_parent fake_bin marker status output
  make_fixture fixture_repo fixture_parent
  fake_bin="$(mktemp -d "$tmp_root/fake-bin.XXXXXX")"
  marker="$fake_bin/docker-compose-called"

  printf '#!/usr/bin/env bash\n: >"%s"\nexit 99\n' "$marker" >"$fake_bin/docker-compose"
  chmod +x "$fake_bin/docker-compose"

  capture_run status output env PATH="$fake_bin:$PATH" "$fixture_repo/scripts/validate.sh" --dry-run tier1

  [[ "$status" -eq 0 ]] || return 1
  [[ ! -e "$marker" ]] || return 1
  assert_contains "$output" "stack role: validation"
  assert_contains "$output" "stack path: $fixture_parent/bump-akk-stack-validation"
  assert_contains "$output" "compose files:"
  assert_contains "$output" "action:"
}

run_test "preflight defaults to validation" test_preflight_defaults_to_validation
run_test "runtime proof defaults to gameplay" test_runtime_proof_defaults_to_gameplay
run_test "explicit roles are accepted by wrappers" test_explicit_roles_are_accepted_by_wrappers
run_test "invalid role fails" test_invalid_role_fails
run_test "custom path override is explicit" test_custom_path_override
run_test "missing default validation fails clearly" test_missing_default_validation_fails_clearly
run_test "default role paths are distinct" test_default_roles_must_not_resolve_to_same_directory
run_test "validation commands warn on gameplay stack" test_validation_commands_warn_on_gameplay_stack
run_test "help mentions stack and dry-run" test_help_mentions_stack_and_dry_run
run_test "dry-run prints route and skips Docker" test_dry_run_prints_route_and_skips_docker

if [[ "$failures" -gt 0 ]]; then
  exit 1
fi

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
  local haystack="$1" needle="$2"
  if [[ "$haystack" != *"$needle"* ]]; then
    printf 'Expected output to contain:\n%s\n\nActual output:\n%s\n' "$needle" "$haystack" >&2
    assertion_failed=1
  fi
}

assert_json_equals() {
  local file="$1" expr="$2" expected="$3" actual
  actual="$(jq -r "$expr" "$file")"
  if [[ "$actual" != "$expected" ]]; then
    printf 'Expected %s in %s to be %s, got %s\n' "$expr" "$file" "$expected" "$actual" >&2
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

make_source_repo() {
  local -n source_ref="$1"
  source_ref="$tmp_root/source-repo"
  mkdir -p "$source_ref/scripts"
  cat >"$source_ref/scripts/validate.sh" <<'SCRIPT'
#!/usr/bin/env bash
set -euo pipefail
if [[ "${VALIDATION_WORKER_TEST_SLEEP:-0}" != "0" ]]; then
  sleep "$VALIDATION_WORKER_TEST_SLEEP"
fi
if [[ "${VALIDATION_WORKER_TEST_ASSERT_STACK_BINDING:-0}" == "1" ]]; then
  [[ -n "${AKKSTACK_DIR:-}" ]] || { printf 'missing AKKSTACK_DIR\n' >&2; exit 1; }
  [[ -n "${EXPECTED_EQEMU_CHECKOUT:-}" ]] || { printf 'missing EXPECTED_EQEMU_CHECKOUT\n' >&2; exit 1; }
  [[ "$(cd "$AKKSTACK_DIR/code" && pwd -P)" == "$(cd "$EXPECTED_EQEMU_CHECKOUT" && pwd -P)" ]] || {
    printf 'stack code did not point at expected checkout\n' >&2
    exit 1
  }
fi
printf 'fake validate: %s\n' "$*"
SCRIPT
  chmod +x "$source_ref/scripts/validate.sh"
  git -C "$source_ref" init >/dev/null 2>&1
  git -C "$source_ref" config user.email worker-test@example.com
  git -C "$source_ref" config user.name 'Worker Test'
  git -C "$source_ref" add scripts/validate.sh
  git -C "$source_ref" commit -m 'add fake validate' >/dev/null 2>&1
}

write_request() {
  local path="$1" source_repo="$2" evidence_dir="$3" ref="${4:-HEAD}" commit="${5:-}" lock_wait="${6:-0}" timeout="${7:-10}" stack_path="${8:-}"
  jq -n \
    --arg project bump-eqemu \
    --arg repo "$source_repo" \
    --arg ref "$ref" \
    --arg commit "$commit" \
    --arg profile preflight \
    --arg run_id "run-$(basename "$evidence_dir")" \
    --arg evidence_dir "$evidence_dir" \
    --arg stack_path "$stack_path" \
    --argjson timeout_seconds "$timeout" \
    --argjson lock_wait_seconds "$lock_wait" \
    '{project:$project, repo:$repo, ref:$ref, profile:$profile, run_id:$run_id, evidence_dir:$evidence_dir, timeout_seconds:$timeout_seconds, lock_wait_seconds:$lock_wait_seconds} + (if $commit == "" then {} else {commit:$commit} end) + (if $stack_path == "" then {} else {stack:{role:"validation", path:$stack_path}} end)' \
    >"$path"
}

worker_env() {
  env VALIDATION_WORKER_HOME="$tmp_root/worker-home" VALIDATION_WORKER_VALIDATE_DRY_RUN=1 "$@"
}

test_help() {
  local status output
  capture_run status output "$repo_root/scripts/validation-worker.sh" --help
  [[ "$status" -eq 0 ]] || return 1
  assert_contains "$output" "run --request <path>"
  assert_contains "$output" "evidence_dir"
  assert_contains "$output" "lock_wait_seconds"
}

test_invalid_request_writes_evidence() {
  local request evidence status output
  evidence="$tmp_root/evidence-invalid"
  request="$tmp_root/invalid.json"
  jq -n --arg evidence_dir "$evidence" '{project:"wrong", evidence_dir:$evidence_dir}' >"$request"

  capture_run status output worker_env "$repo_root/scripts/validation-worker.sh" run --request "$request"

  [[ "$status" -eq 2 ]] || return 1
  [[ -f "$evidence/request.json" ]] || return 1
  [[ -f "$evidence/result.json" ]] || return 1
  [[ -f "$evidence/logs/request.log" ]] || return 1
  assert_json_equals "$evidence/result.json" .category invalid_request
}

test_fetch_checkout_and_evidence() {
  local source request evidence status output head
  make_source_repo source
  head="$(git -C "$source" rev-parse HEAD)"
  evidence="$tmp_root/evidence-fetch"
  request="$tmp_root/fetch.json"
  write_request "$request" "$source" "$evidence" HEAD "$head"

  capture_run status output worker_env "$repo_root/scripts/validation-worker.sh" run --request "$request"

  [[ "$status" -eq 0 ]] || return 1
  [[ -f "$evidence/request.json" ]] || return 1
  [[ -f "$evidence/result.json" ]] || return 1
  [[ -f "$evidence/logs/fetch.log" ]] || return 1
  [[ -f "$evidence/logs/validation.log" ]] || return 1
  assert_json_equals "$evidence/result.json" .status passed
  assert_json_equals "$evidence/result.json" .head_commit "$head"
}

test_commit_mismatch() {
  local source request evidence status output
  make_source_repo source
  evidence="$tmp_root/evidence-mismatch"
  request="$tmp_root/mismatch.json"
  write_request "$request" "$source" "$evidence" HEAD "0000000000000000000000000000000000000000"

  capture_run status output worker_env "$repo_root/scripts/validation-worker.sh" run --request "$request"

  [[ "$status" -eq 1 ]] || return 1
  assert_json_equals "$evidence/result.json" .category commit_mismatch
}

test_fetch_failure() {
  local request evidence status output
  evidence="$tmp_root/evidence-fetch-failure"
  request="$tmp_root/fetch-failure.json"
  write_request "$request" "$tmp_root/no-such-repo" "$evidence" HEAD

  capture_run status output worker_env "$repo_root/scripts/validation-worker.sh" run --request "$request"

  [[ "$status" -eq 1 ]] || return 1
  assert_json_equals "$evidence/result.json" .category fetch_failed
}

test_lock_contention() {
  local source request evidence status output lock_dir existing_checkout
  make_source_repo source
  evidence="$tmp_root/evidence-busy"
  request="$tmp_root/busy.json"
  write_request "$request" "$source" "$evidence" HEAD "" 0
  lock_dir="$tmp_root/worker-home/locks/validation-slot.lock"
  mkdir -p "$lock_dir"
  existing_checkout="$tmp_root/worker-home/checkouts/run-$(basename "$evidence")"
  mkdir -p "$existing_checkout"
  printf 'do not mutate before lock\n' >"$existing_checkout/marker"

  capture_run status output worker_env "$repo_root/scripts/validation-worker.sh" run --request "$request"

  [[ "$status" -eq 1 ]] || return 1
  assert_json_equals "$evidence/result.json" .category worker_busy
  [[ -f "$evidence/logs/lock.log" ]] || return 1
  [[ -f "$existing_checkout/marker" ]] || return 1
}

test_timeout() {
  local source request evidence status output
  make_source_repo source
  evidence="$tmp_root/evidence-timeout"
  request="$tmp_root/timeout.json"
  rm -rf "$tmp_root/worker-home/locks/validation-slot.lock"
  write_request "$request" "$source" "$evidence" HEAD "" 0 1

  capture_run status output env VALIDATION_WORKER_HOME="$tmp_root/worker-home" VALIDATION_WORKER_VALIDATE_DRY_RUN=1 VALIDATION_WORKER_TEST_SLEEP=2 "$repo_root/scripts/validation-worker.sh" run --request "$request"

  [[ "$status" -eq 1 ]] || return 1
  assert_json_equals "$evidence/result.json" .category timeout
}

test_validation_worker_binds_requested_validation_stack_to_worker_checkout() {
  local source request evidence status output stack other_checkout
  make_source_repo source
  evidence="$tmp_root/evidence-stack-binding"
  request="$tmp_root/stack-binding.json"
  stack="$tmp_root/validation-stack"
  other_checkout="$tmp_root/other-checkout"
  mkdir -p "$stack" "$other_checkout"
  printf 'ENV=development\n' >"$stack/.env"
  ln -s "$other_checkout" "$stack/code"
  write_request "$request" "$source" "$evidence" HEAD "" 0 10 "$stack"

  capture_run status output env VALIDATION_WORKER_HOME="$tmp_root/worker-home" VALIDATION_WORKER_VALIDATE_DRY_RUN=1 VALIDATION_WORKER_TEST_ASSERT_STACK_BINDING=1 "$repo_root/scripts/validation-worker.sh" run --request "$request"

  [[ "$status" -eq 0 ]] || return 1
  [[ -f "$evidence/stack-binding.json" ]] || return 1
  assert_json_equals "$evidence/stack-binding.json" .status rebound
  assert_json_equals "$evidence/stack-binding.json" .role validation
  assert_json_equals "$evidence/result.json" .status passed
  assert_json_equals "$evidence/stack-binding.json" .restore_status restored
  [[ "$(cd "$stack/code" && pwd -P)" == "$(cd "$other_checkout" && pwd -P)" ]] || return 1
}

test_validation_worker_binds_stack_from_akkstack_dir_environment() {
  local source request evidence status output stack other_checkout
  make_source_repo source
  evidence="$tmp_root/evidence-env-stack-binding"
  request="$tmp_root/env-stack-binding.json"
  stack="$tmp_root/env-validation-stack"
  other_checkout="$tmp_root/env-other-checkout"
  mkdir -p "$stack" "$other_checkout"
  printf 'ENV=development\n' >"$stack/.env"
  ln -s "$other_checkout" "$stack/code"
  write_request "$request" "$source" "$evidence" HEAD "" 0 10

  capture_run status output env VALIDATION_WORKER_HOME="$tmp_root/worker-home" VALIDATION_WORKER_VALIDATE_DRY_RUN=1 VALIDATION_WORKER_TEST_ASSERT_STACK_BINDING=1 AKKSTACK_DIR="$stack" "$repo_root/scripts/validation-worker.sh" run --request "$request"

  [[ "$status" -eq 0 ]] || return 1
  [[ -f "$evidence/stack-binding.json" ]] || return 1
  assert_json_equals "$evidence/stack-binding.json" .status rebound
  assert_json_equals "$evidence/stack-binding.json" .source AKKSTACK_DIR
  assert_json_equals "$evidence/stack-binding.json" .restore_status restored
  assert_json_equals "$evidence/result.json" .status passed
  [[ "$(cd "$stack/code" && pwd -P)" == "$(cd "$other_checkout" && pwd -P)" ]] || return 1
}

test_stack_lock_blocks_distinct_worker_homes_on_same_stack() {
  local source request_a request_b evidence_a evidence_b status_b output_b stack other_checkout stack_lock
  make_source_repo source
  stack="$tmp_root/shared-validation-stack"
  other_checkout="$tmp_root/shared-other-checkout"
  mkdir -p "$stack" "$other_checkout"
  printf 'ENV=development\n' >"$stack/.env"
  ln -s "$other_checkout" "$stack/code"
  evidence_a="$tmp_root/evidence-shared-stack-a"
  evidence_b="$tmp_root/evidence-shared-stack-b"
  request_a="$tmp_root/shared-stack-a.json"
  request_b="$tmp_root/shared-stack-b.json"
  write_request "$request_a" "$source" "$evidence_a" HEAD "" 0 10 "$stack"
  write_request "$request_b" "$source" "$evidence_b" HEAD "" 0 10 "$stack"
  stack_lock="$stack/.validation-worker-code.lock"
  mkdir "$stack_lock"

  capture_run status_b output_b env VALIDATION_WORKER_HOME="$tmp_root/worker-home-b" VALIDATION_WORKER_VALIDATE_DRY_RUN=1 "$repo_root/scripts/validation-worker.sh" run --request "$request_b"

  [[ "$status_b" -eq 1 ]] || return 1
  assert_json_equals "$evidence_b/result.json" .category stack_busy
  [[ -f "$evidence_b/logs/stack.log" ]] || return 1
  [[ "$(cd "$stack/code" && pwd -P)" == "$(cd "$other_checkout" && pwd -P)" ]] || return 1
}

test_akkstack_dir_real_code_directory_fails_fast() {
  local source request evidence status output stack
  make_source_repo source
  evidence="$tmp_root/evidence-env-real-code"
  request="$tmp_root/env-real-code.json"
  stack="$tmp_root/env-real-code-stack"
  mkdir -p "$stack/code"
  printf 'ENV=development\n' >"$stack/.env"
  write_request "$request" "$source" "$evidence" HEAD "" 0 10

  capture_run status output env VALIDATION_WORKER_HOME="$tmp_root/worker-home" VALIDATION_WORKER_VALIDATE_DRY_RUN=1 AKKSTACK_DIR="$stack" "$repo_root/scripts/validation-worker.sh" run --request "$request"

  [[ "$status" -eq 1 ]] || return 1
  assert_json_equals "$evidence/result.json" .category stack_binding_failed
  assert_json_equals "$evidence/stack-binding.json" .previous_kind directory
  [[ -d "$stack/code" && ! -L "$stack/code" ]] || return 1
}

run_test "validation worker help mentions request contract" test_help
run_test "invalid request writes structured evidence" test_invalid_request_writes_evidence
run_test "fake repo fetch checkout writes evidence" test_fetch_checkout_and_evidence
run_test "commit mismatch is categorized" test_commit_mismatch
run_test "fetch failure is categorized" test_fetch_failure
run_test "lock contention is worker_busy" test_lock_contention
run_test "validation timeout is categorized" test_timeout
run_test "validation worker binds requested validation stack to worker checkout" test_validation_worker_binds_requested_validation_stack_to_worker_checkout
run_test "validation worker binds validation stack from AKKSTACK_DIR" test_validation_worker_binds_stack_from_akkstack_dir_environment
run_test "stack lock blocks distinct worker homes on same stack" test_stack_lock_blocks_distinct_worker_homes_on_same_stack
run_test "AKKSTACK_DIR real code directory fails fast" test_akkstack_dir_real_code_directory_fails_fast

if [[ "$failures" -gt 0 ]]; then
  exit 1
fi

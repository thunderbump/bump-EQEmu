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
if [[ "${VALIDATION_WORKER_TEST_SLEEP_TIER1:-0}" != "0" && " $* " == *" tier1"* ]]; then
  sleep "$VALIDATION_WORKER_TEST_SLEEP_TIER1"
fi
if [[ "${VALIDATION_WORKER_TEST_SLEEP_TIER3:-0}" != "0" && " $* " == *" tier3-harness"* ]]; then
  sleep "$VALIDATION_WORKER_TEST_SLEEP_TIER3"
fi
if [[ "${VALIDATION_WORKER_TEST_FAIL_TIER1:-0}" == "1" && " $* " == *" tier1"* ]]; then
  printf 'tier1 requested failure\n' >&2
  exit 1
fi
if [[ "${VALIDATION_WORKER_TEST_ASSERT_STACK_BINDING:-0}" == "1" ]]; then
  [[ -n "${AKKSTACK_DIR:-}" ]] || { printf 'missing AKKSTACK_DIR\n' >&2; exit 1; }
  [[ -n "${EXPECTED_EQEMU_CHECKOUT:-}" ]] || { printf 'missing EXPECTED_EQEMU_CHECKOUT\n' >&2; exit 1; }
  [[ "$(cd "$AKKSTACK_DIR/code" && pwd -P)" == "$(cd "$EXPECTED_EQEMU_CHECKOUT" && pwd -P)" ]] || {
    printf 'stack code did not point at expected checkout\n' >&2
    exit 1
  }
fi
repo_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
if [[ "${VALIDATION_WORKER_TEST_ASSERT_SUBMODULE:-0}" == "1" && ! -f "$repo_dir/vendor/submodule-fixture/marker.txt" ]]; then
  printf 'submodule marker missing\n' >&2
  exit 1
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

make_afk_contract_repo() {
  local -n source_ref="$1"
  local fixture_source
  make_source_repo fixture_source
  source_ref="$fixture_source"
  cp "$repo_root/scripts/validation-worker.sh" "$source_ref/scripts/validation-worker.sh"
  chmod +x "$source_ref/scripts/validation-worker.sh"
  mkdir -p "$tmp_root/bump-akk-stack-validation"
  printf 'ENV=development\n' >"$tmp_root/bump-akk-stack-validation/.env"
  git -C "$source_ref" add scripts/validation-worker.sh
  git -C "$source_ref" commit -m 'add validation worker' >/dev/null 2>&1
}

make_source_repo_with_real_validation_scripts() {
  local -n source_ref="$1"
  source_ref="$tmp_root/source-repo-real-validation"
  mkdir -p "$source_ref"
  cp -R "$repo_root/scripts" "$source_ref/"
  git -C "$source_ref" init >/dev/null 2>&1
  git -C "$source_ref" config user.email worker-test@example.com
  git -C "$source_ref" config user.name 'Worker Test'
  git -C "$source_ref" add scripts
  git -C "$source_ref" commit -m 'add real validation scripts' >/dev/null 2>&1
}

make_fake_tier3_bin() {
  local fake_bin="$1"
  mkdir -p "$fake_bin"

  cat >"$fake_bin/mysqladmin" <<'SCRIPT'
#!/usr/bin/env bash
exit 0
SCRIPT
  chmod +x "$fake_bin/mysqladmin"

  cat >"$fake_bin/docker-compose" <<'SCRIPT'
#!/usr/bin/env bash
set -euo pipefail

if [[ " $* " == *" up "* ]]; then
  exit 0
fi

payload=""
previous=""
for arg in "$@"; do
  if [[ "$previous" == "-lc" ]]; then
    payload="$arg"
    break
  fi
  previous="$arg"
done

if [[ -z "$payload" ]]; then
  printf 'missing bash -lc payload\n' >&2
  exit 1
fi

EQEMU_DB_PASSWORD=fixture bash -lc "$payload"
SCRIPT
  chmod +x "$fake_bin/docker-compose"
}

make_source_repo_with_submodule() {
  local -n source_ref="$1"
  local base_source submodule_repo
  make_source_repo base_source
  source_ref="$base_source"
  submodule_repo="$tmp_root/submodule-repo"
  mkdir -p "$submodule_repo"
  git -C "$submodule_repo" init >/dev/null 2>&1
  git -C "$submodule_repo" config user.email worker-test@example.com
  git -C "$submodule_repo" config user.name 'Worker Test'
  printf 'submodule fixture\n' >"$submodule_repo/marker.txt"
  git -C "$submodule_repo" add marker.txt
  git -C "$submodule_repo" commit -m 'add marker' >/dev/null 2>&1
  git -C "$source_ref" -c protocol.file.allow=always submodule add "$submodule_repo" vendor/submodule-fixture >/dev/null 2>&1
  git -C "$source_ref" commit -m 'add submodule fixture' >/dev/null 2>&1
}

write_request() {
  local path="$1" source_repo="$2" evidence_dir="$3" ref="${4:-HEAD}" commit="${5:-}" lock_wait="${6:-0}" timeout="${7:-10}" stack_path="${8:-}" profile="${9:-preflight}"
  jq -n \
    --arg project bump-eqemu \
    --arg repo "$source_repo" \
    --arg ref "$ref" \
    --arg commit "$commit" \
    --arg profile "$profile" \
    --arg run_id "run-$(basename "$evidence_dir")" \
    --arg evidence_dir "$evidence_dir" \
    --arg stack_path "$stack_path" \
    --argjson timeout_seconds "$timeout" \
    --argjson lock_wait_seconds "$lock_wait" \
    '{project:$project, repo:$repo, ref:$ref, profile:$profile, run_id:$run_id, evidence_dir:$evidence_dir, timeout_seconds:$timeout_seconds, lock_wait_seconds:$lock_wait_seconds} + (if $commit == "" then {} else {commit:$commit} end) + (if $stack_path == "" then {} else {stack:{role:"validation", path:$stack_path}} end)' \
    >"$path"
}

write_local_checkout_request() {
  local path="$1" checkout_path="$2" evidence_dir="$3" commit="${4:-}" lock_wait="${5:-0}" timeout="${6:-10}" stack_path="${7:-}" profile="${8:-preflight}"
  jq -n \
    --arg project bump-eqemu \
    --arg checkout_path "$checkout_path" \
    --arg commit "$commit" \
    --arg profile "$profile" \
    --arg run_id "run-$(basename "$evidence_dir")" \
    --arg evidence_dir "$evidence_dir" \
    --arg stack_path "$stack_path" \
    --argjson timeout_seconds "$timeout" \
    --argjson lock_wait_seconds "$lock_wait" \
    '{project:$project, checkout:{path:$checkout_path}, profile:$profile, run_id:$run_id, evidence_dir:$evidence_dir, timeout_seconds:$timeout_seconds, lock_wait_seconds:$lock_wait_seconds} + (if $commit == "" then {} else {commit:$commit} end) + (if $stack_path == "" then {} else {stack:{role:"validation", path:$stack_path}} end)' \
    >"$path"
}

write_afk_request() {
  local path="$1" candidate_sha="$2" evidence_dir="$3" run_id="${4:-afk-contract-test}"
  jq -n \
    --arg run_id "$run_id" \
    --arg candidate_sha "$candidate_sha" \
    --arg evidence_dir "$evidence_dir" \
    '{schema_version:1, run_id:$run_id, candidate_sha:$candidate_sha, evidence_dir:$evidence_dir}' \
    >"$path"
}

worker_env() {
  env VALIDATION_WORKER_HOME="$tmp_root/worker-home" VALIDATION_WORKER_VALIDATE_DRY_RUN=1 "$@"
}

reset_worker_home() {
  rm -rf "$tmp_root/worker-home"
}

test_help() {
  local status output
  capture_run status output "$repo_root/scripts/validation-worker.sh" --help
  [[ "$status" -eq 0 ]] || return 1
  assert_contains "$output" "profiles --json"
  assert_contains "$output" "run --request <path>"
  assert_contains "$output" "evidence_dir"
  assert_contains "$output" "lock_wait_seconds"
  assert_contains "$output" "tier1-tier3-harness"
}

test_afk_contract_config() {
  python3 - "$repo_root/afk.toml" <<'PY'
import sys
import tomllib

with open(sys.argv[1], "rb") as contract_file:
    contract = tomllib.load(contract_file)

assert contract == {
    "schema_version": 1,
    "validation": {
        "command": ["./scripts/validation-worker.sh", "run"],
        "timeout_seconds": 2700,
    },
}
PY
}

test_profiles_json() {
  local status output
  capture_run status output "$repo_root/scripts/validation-worker.sh" profiles --json
  [[ "$status" -eq 0 ]] || return 1
  jq -e '.profiles | length == 4' >/dev/null <<<"$output" || return 1
  jq -e '.profiles[] | select(.name == "preflight") | .mutation_classification == "read-only"' >/dev/null <<<"$output" || return 1
  jq -e '.profiles[] | select(.name == "safe") | (.timeout_guidance | length > 0) and (.lock_guidance | length > 0)' >/dev/null <<<"$output" || return 1
  jq -e '.profiles[] | select(.name == "tier3-harness")' >/dev/null <<<"$output" || return 1
  jq -e '.profiles[] | select(.name == "tier1-tier3-harness")' >/dev/null <<<"$output" || return 1
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
  reset_worker_home
  head="$(git -C "$source" rev-parse HEAD)"
  evidence="$tmp_root/evidence-fetch"
  request="$tmp_root/fetch.json"
  write_request "$request" "$source" "$evidence" HEAD "$head"

  capture_run status output worker_env "$repo_root/scripts/validation-worker.sh" run --request "$request"

  [[ "$status" -eq 0 ]] || return 1
  [[ -f "$evidence/request.json" ]] || return 1
  [[ -f "$evidence/result.json" ]] || return 1
  [[ -f "$evidence/worker-output.json" ]] || return 1
  [[ -f "$evidence/logs/fetch.log" ]] || return 1
  [[ -f "$evidence/logs/validation.log" ]] || return 1
  assert_json_equals "$evidence/result.json" .status passed
  assert_json_equals "$evidence/result.json" .head_commit "$head"
  assert_json_equals "$evidence/result.json" .expected_commit "$head"
  assert_json_equals "$evidence/result.json" .actual_checkout_commit "$head"
  assert_json_equals "$evidence/result.json" .profile preflight
  assert_json_equals "$evidence/result.json" .request_metadata.source.type fetch
  assert_json_equals "$evidence/result.json" .request_metadata.source.repo "$source"
  assert_json_equals "$evidence/result.json" .request_metadata.source.ref HEAD
  assert_json_equals "$evidence/result.json" .request_metadata.source.commit "$head"
  assert_json_equals "$evidence/result.json" .request_metadata.run_id "run-$(basename "$evidence")"
  assert_json_equals "$evidence/result.json" .request_metadata.timeout_seconds 10
  assert_json_equals "$evidence/result.json" .request_metadata.lock_wait_seconds 0
  assert_json_equals "$evidence/result.json" .stack.role validation
  assert_json_equals "$evidence/result.json" .stack.path ""
  assert_json_equals "$evidence/result.json" .stack.path_source ""
  assert_json_equals "$evidence/result.json" .evidence_dir "$evidence"
}

test_fetch_checkout_initializes_submodules_before_validation() {
  local source request evidence status output head
  make_source_repo_with_submodule source
  reset_worker_home
  head="$(git -C "$source" rev-parse HEAD)"
  evidence="$tmp_root/evidence-submodule"
  request="$tmp_root/submodule.json"
  write_request "$request" "$source" "$evidence" HEAD "$head"

  capture_run status output env VALIDATION_WORKER_HOME="$tmp_root/worker-home" VALIDATION_WORKER_VALIDATE_DRY_RUN=1 VALIDATION_WORKER_TEST_ASSERT_SUBMODULE=1 "$repo_root/scripts/validation-worker.sh" run --request "$request"

  [[ "$status" -eq 0 ]] || return 1
  [[ -f "$evidence/logs/submodule.log" ]] || return 1
  assert_json_equals "$evidence/result.json" .status passed
}

test_submodule_timeout_is_categorized() {
  local source request evidence status output head fake_bin real_git
  make_source_repo_with_submodule source
  reset_worker_home
  head="$(git -C "$source" rev-parse HEAD)"
  evidence="$tmp_root/evidence-submodule-timeout"
  request="$tmp_root/submodule-timeout.json"
  write_request "$request" "$source" "$evidence" HEAD "$head" 0 1
  fake_bin="$tmp_root/fake-bin"
  real_git="$(command -v git)"
  mkdir -p "$fake_bin"
  cat >"$fake_bin/git" <<SCRIPT
#!/usr/bin/env bash
for arg in "\$@"; do
  if [[ "\$arg" == "submodule" ]]; then
    sleep 2
    exit 0
  fi
done
exec "$real_git" "\$@"
SCRIPT
  chmod +x "$fake_bin/git"

  capture_run status output env PATH="$fake_bin:$PATH" VALIDATION_WORKER_HOME="$tmp_root/worker-home" VALIDATION_WORKER_VALIDATE_DRY_RUN=1 "$repo_root/scripts/validation-worker.sh" run --request "$request"

  [[ "$status" -eq 1 ]] || return 1
  assert_json_equals "$evidence/result.json" .category timeout
  assert_contains "$(cat "$evidence/result.json")" "submodule initialization timed out"
  [[ -f "$evidence/logs/submodule.log" ]] || return 1
}

test_stack_lock_is_not_held_during_submodule_initialization() {
  local source request evidence status output head stack other_checkout stack_lock
  make_source_repo_with_submodule source
  reset_worker_home
  head="$(git -C "$source" rev-parse HEAD)"
  evidence="$tmp_root/evidence-submodule-before-stack-lock"
  request="$tmp_root/submodule-before-stack-lock.json"
  stack="$tmp_root/validation-stack-before-lock"
  other_checkout="$tmp_root/other-checkout-before-lock"
  mkdir -p "$stack" "$other_checkout"
  printf 'ENV=development\n' >"$stack/.env"
  ln -s "$other_checkout" "$stack/code"
  write_request "$request" "$source" "$evidence" HEAD "$head" 0 10 "$stack"
  stack_lock="$stack/.validation-worker-code.lock"
  mkdir "$stack_lock"

  capture_run status output env VALIDATION_WORKER_HOME="$tmp_root/worker-home" VALIDATION_WORKER_VALIDATE_DRY_RUN=1 "$repo_root/scripts/validation-worker.sh" run --request "$request"

  [[ "$status" -eq 1 ]] || return 1
  assert_json_equals "$evidence/result.json" .category stack_busy
  [[ -f "$tmp_root/worker-home/checkouts/run-$(basename "$evidence")/vendor/submodule-fixture/marker.txt" ]] || return 1
}

test_commit_mismatch() {
  local source request evidence status output
  make_source_repo source
  reset_worker_home
  evidence="$tmp_root/evidence-mismatch"
  request="$tmp_root/mismatch.json"
  write_request "$request" "$source" "$evidence" HEAD "0000000000000000000000000000000000000000"

  capture_run status output worker_env "$repo_root/scripts/validation-worker.sh" run --request "$request"

  [[ "$status" -eq 1 ]] || return 1
  assert_json_equals "$evidence/result.json" .category commit_mismatch
  assert_json_equals "$evidence/result.json" .expected_commit 0000000000000000000000000000000000000000
}

test_fetch_failure() {
  local request evidence status output
  reset_worker_home
  evidence="$tmp_root/evidence-fetch-failure"
  request="$tmp_root/fetch-failure.json"
  write_request "$request" "$tmp_root/no-such-repo" "$evidence" HEAD

  capture_run status output worker_env "$repo_root/scripts/validation-worker.sh" run --request "$request"

  [[ "$status" -eq 1 ]] || return 1
  assert_json_equals "$evidence/result.json" .category fetch_failed
}

test_local_checkout_request_works() {
  local source request evidence status output head
  make_source_repo source
  reset_worker_home
  head="$(git -C "$source" rev-parse HEAD)"
  evidence="$tmp_root/evidence-local-checkout"
  request="$tmp_root/local-checkout.json"
  write_local_checkout_request "$request" "$source" "$evidence" "$head"

  capture_run status output worker_env "$repo_root/scripts/validation-worker.sh" run --request "$request"

  [[ "$status" -eq 0 ]] || return 1
  assert_json_equals "$evidence/result.json" .status passed
  assert_json_equals "$evidence/result.json" .request_metadata.source.type local-checkout
  assert_json_equals "$evidence/result.json" .request_metadata.source.checkout_path "$source"
  assert_json_equals "$evidence/result.json" .request_metadata.source.ref ""
  assert_json_equals "$evidence/result.json" .actual_checkout_commit "$head"
}

test_lock_contention() {
  local source request evidence status output lock_dir existing_checkout
  make_source_repo source
  reset_worker_home
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
  rm -rf "$lock_dir"
}

test_timeout() {
  local source request evidence status output
  make_source_repo source
  reset_worker_home
  evidence="$tmp_root/evidence-timeout"
  request="$tmp_root/timeout.json"
  rm -rf "$tmp_root/worker-home/locks/validation-slot.lock"
  write_request "$request" "$source" "$evidence" HEAD "" 0 1

  capture_run status output env VALIDATION_WORKER_HOME="$tmp_root/worker-home" VALIDATION_WORKER_VALIDATE_DRY_RUN=1 VALIDATION_WORKER_TEST_SLEEP=2 "$repo_root/scripts/validation-worker.sh" run --request "$request"

  [[ "$status" -eq 1 ]] || return 1
  assert_json_equals "$evidence/result.json" .category timeout
}

test_tier3_harness_failure_is_categorized_with_logs() {
  local source request evidence status output stack other_checkout fake_bin
  make_source_repo_with_real_validation_scripts source
  reset_worker_home
  evidence="$tmp_root/evidence-tier3-prebuild-failure"
  request="$tmp_root/tier3-prebuild-failure.json"
  stack="$tmp_root/tier3-validation-stack"
  other_checkout="$tmp_root/tier3-other-checkout"
  fake_bin="$tmp_root/tier3-fake-bin"
  mkdir -p "$stack" "$other_checkout"
  printf 'ENV=development\n' >"$stack/.env"
  ln -s "$other_checkout" "$stack/code"
  make_fake_tier3_bin "$fake_bin"
  write_request "$request" "$source" "$evidence" HEAD "" 0 10 "$stack" tier3-harness

  capture_run status output env PATH="$fake_bin:$PATH" VALIDATION_WORKER_HOME="$tmp_root/worker-home" "$repo_root/scripts/validation-worker.sh" run --request "$request"

  [[ "$status" -eq 1 ]] || return 1
  [[ -f "$evidence/logs/validation.log" ]] || return 1
  assert_json_equals "$evidence/result.json" .category validation_failed
  assert_contains "$(cat "$evidence/logs/validation.log")" "tier3-harness requires a prior Tier 1 build"
  assert_contains "$(cat "$evidence/logs/validation.log")" "missing executable ./bin/zone"
}

test_tier1_tier3_harness_profile_runs_tier1_before_tier3() {
  local source request evidence status output validation_log first_tier1 first_tier3
  make_source_repo source
  reset_worker_home
  evidence="$tmp_root/evidence-tier1-tier3"
  request="$tmp_root/tier1-tier3.json"
  write_request "$request" "$source" "$evidence" HEAD "" 0 10 "" tier1-tier3-harness

  capture_run status output env VALIDATION_WORKER_HOME="$tmp_root/worker-home" VALIDATION_WORKER_VALIDATE_DRY_RUN=1 "$repo_root/scripts/validation-worker.sh" run --request "$request"

  [[ "$status" -eq 0 ]] || return 1
  assert_json_equals "$evidence/result.json" .status passed
  validation_log="$evidence/logs/validation.log"
  first_tier1="$(grep -n 'fake validate: --stack validation --dry-run tier1' "$validation_log" | head -n1 | cut -d: -f1)"
  first_tier3="$(grep -n 'fake validate: --stack validation --dry-run tier3-harness' "$validation_log" | head -n1 | cut -d: -f1)"
  [[ -n "$first_tier1" && -n "$first_tier3" ]] || return 1
  [[ "$first_tier1" -lt "$first_tier3" ]] || return 1
  assert_contains "$(cat "$validation_log")" "fake validate: --stack validation --dry-run tier1"
  assert_contains "$(cat "$validation_log")" "fake validate: --stack validation --dry-run tier3-harness"
}

test_tier1_tier3_harness_profile_uses_one_timeout_budget() {
  local source request evidence status output validation_log start_ns end_ns elapsed_ms
  make_source_repo source
  reset_worker_home
  evidence="$tmp_root/evidence-tier1-tier3-timeout"
  request="$tmp_root/tier1-tier3-timeout.json"
  write_request "$request" "$source" "$evidence" HEAD "" 0 2 "" tier1-tier3-harness

  start_ns="$(date +%s%N)"
  capture_run status output env VALIDATION_WORKER_HOME="$tmp_root/worker-home" VALIDATION_WORKER_VALIDATE_DRY_RUN=1 VALIDATION_WORKER_TEST_SLEEP_TIER1=1.2 VALIDATION_WORKER_TEST_SLEEP_TIER3=3 "$repo_root/scripts/validation-worker.sh" run --request "$request"
  end_ns="$(date +%s%N)"
  elapsed_ms=$(( (end_ns - start_ns) / 1000000 ))

  [[ "$status" -eq 1 ]] || return 1
  assert_json_equals "$evidence/result.json" .status failed
  assert_json_equals "$evidence/result.json" .category timeout
  [[ "$elapsed_ms" -lt 3000 ]] || {
    printf 'Expected composite timeout to stay under 3000ms, got %sms\n' "$elapsed_ms" >&2
    return 1
  }
  validation_log="$evidence/logs/validation.log"
  assert_contains "$(cat "$validation_log")" "fake validate: --stack validation --dry-run tier1"
}

test_tier1_tier3_harness_profile_stops_after_tier1_failure_and_releases_lock() {
  local source request evidence status output validation_log lock_dir
  make_source_repo source
  reset_worker_home
  evidence="$tmp_root/evidence-tier1-tier3-tier1-failure"
  request="$tmp_root/tier1-tier3-tier1-failure.json"
  write_request "$request" "$source" "$evidence" HEAD "" 0 10 "" tier1-tier3-harness

  capture_run status output env VALIDATION_WORKER_HOME="$tmp_root/worker-home" VALIDATION_WORKER_VALIDATE_DRY_RUN=1 VALIDATION_WORKER_TEST_FAIL_TIER1=1 "$repo_root/scripts/validation-worker.sh" run --request "$request"

  [[ "$status" -eq 1 ]] || return 1
  assert_json_equals "$evidence/result.json" .status failed
  assert_json_equals "$evidence/result.json" .category validation_failed
  validation_log="$evidence/logs/validation.log"
  assert_contains "$(cat "$validation_log")" "tier1 requested failure"
  if grep -q 'fake validate: --stack validation --dry-run tier3-harness' "$validation_log"; then
    printf 'tier3-harness should not run after tier1 failure\n' >&2
    return 1
  fi
  lock_dir="$tmp_root/worker-home/locks/validation-slot.lock"
  [[ ! -e "$lock_dir" ]] || {
    printf 'validation lock was not released: %s\n' "$lock_dir" >&2
    return 1
  }
}

test_validation_worker_binds_requested_validation_stack_to_worker_checkout() {
  local source request evidence status output stack other_checkout
  make_source_repo source
  reset_worker_home
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
  reset_worker_home
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
  reset_worker_home
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
  reset_worker_home
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

test_afk_contract_passes_with_stable_checks() {
  local source request evidence status output head
  make_afk_contract_repo source
  reset_worker_home
  head="$(git -C "$source" rev-parse HEAD)"
  request="$tmp_root/afk-passed.json"
  evidence="$tmp_root/afk-passed"
  mkdir "$evidence"
  write_afk_request "$request" "$head" "$evidence"

  capture_run status output env VALIDATION_WORKER_HOME="$tmp_root/worker-home" VALIDATION_WORKER_VALIDATE_DRY_RUN=1 "$source/scripts/validation-worker.sh" run --request "$request"

  [[ "$status" -eq 0 ]] || return 1
  assert_json_equals "$evidence/result.json" .schema_version 1
  assert_json_equals "$evidence/result.json" .candidate_sha "$head"
  assert_json_equals "$evidence/result.json" .status passed
  assert_json_equals "$evidence/result.json" '.checks | map(.name) | join(",")' "preflight,tier1-build-and-unit-tests,tier2-read-only-database-tests"
  assert_json_equals "$evidence/result.json" '.checks | map(.status) | join(",")' "passed,passed,passed"
}

test_afk_contract_rejects_tier1_and_stops() {
  local source request evidence status output head
  make_afk_contract_repo source
  reset_worker_home
  head="$(git -C "$source" rev-parse HEAD)"
  request="$tmp_root/afk-rejected.json"
  evidence="$tmp_root/afk-rejected"
  mkdir "$evidence"
  write_afk_request "$request" "$head" "$evidence"

  capture_run status output env VALIDATION_WORKER_HOME="$tmp_root/worker-home" VALIDATION_WORKER_VALIDATE_DRY_RUN=1 VALIDATION_WORKER_TEST_FAIL_TIER1=1 "$source/scripts/validation-worker.sh" run --request "$request"

  [[ "$status" -eq 1 ]] || return 1
  assert_json_equals "$evidence/result.json" .status rejected
  assert_json_equals "$evidence/result.json" '.checks | map(.status) | join(",")' "passed,rejected,not_run"
}

test_afk_contract_reports_missing_prerequisite_as_inconclusive() {
  local source request evidence status output head
  make_afk_contract_repo source
  reset_worker_home
  head="$(git -C "$source" rev-parse HEAD)"
  request="$tmp_root/afk-inconclusive.json"
  evidence="$tmp_root/afk-inconclusive"
  rm -rf "$tmp_root/bump-akk-stack-validation"
  mkdir "$evidence"
  write_afk_request "$request" "$head" "$evidence"

  capture_run status output env VALIDATION_WORKER_HOME="$tmp_root/worker-home" VALIDATION_WORKER_VALIDATE_DRY_RUN=1 "$source/scripts/validation-worker.sh" run --request "$request"

  [[ "$status" -eq 2 ]] || return 1
  assert_json_equals "$evidence/result.json" .status inconclusive
  assert_json_equals "$evidence/result.json" '.checks | map(.status) | join(",")' "inconclusive,not_run,not_run"
}

run_test "validation worker help mentions request contract" test_help
run_test "AFK contract config selects the validation worker" test_afk_contract_config
run_test "validation worker profiles discovery emits portable metadata" test_profiles_json
run_test "invalid request writes structured evidence" test_invalid_request_writes_evidence
run_test "fake repo fetch checkout writes evidence" test_fetch_checkout_and_evidence
run_test "fetched checkout initializes submodules before validation" test_fetch_checkout_initializes_submodules_before_validation
run_test "submodule timeout is categorized" test_submodule_timeout_is_categorized
run_test "stack lock is not held during submodule initialization" test_stack_lock_is_not_held_during_submodule_initialization
run_test "commit mismatch is categorized" test_commit_mismatch
run_test "fetch failure is categorized" test_fetch_failure
run_test "local-checkout request still works" test_local_checkout_request_works
run_test "lock contention is worker_busy" test_lock_contention
run_test "validation timeout is categorized" test_timeout
run_test "tier3 harness failure is categorized with logs" test_tier3_harness_failure_is_categorized_with_logs
run_test "tier1 plus tier3 harness profile runs tier1 before tier3" test_tier1_tier3_harness_profile_runs_tier1_before_tier3
run_test "tier1 plus tier3 harness profile uses one timeout budget" test_tier1_tier3_harness_profile_uses_one_timeout_budget
run_test "tier1 plus tier3 harness profile stops after tier1 failure and releases lock" test_tier1_tier3_harness_profile_stops_after_tier1_failure_and_releases_lock
run_test "validation worker binds requested validation stack to worker checkout" test_validation_worker_binds_requested_validation_stack_to_worker_checkout
run_test "validation worker binds validation stack from AKKSTACK_DIR" test_validation_worker_binds_stack_from_akkstack_dir_environment
run_test "stack lock blocks distinct worker homes on same stack" test_stack_lock_blocks_distinct_worker_homes_on_same_stack
run_test "AKKSTACK_DIR real code directory fails fast" test_akkstack_dir_real_code_directory_fails_fast
run_test "AFK contract reports stable passing checks" test_afk_contract_passes_with_stable_checks
run_test "AFK contract rejects Tier 1 and stops" test_afk_contract_rejects_tier1_and_stops
run_test "AFK contract reports a missing prerequisite as inconclusive" test_afk_contract_reports_missing_prerequisite_as_inconclusive

if [[ "$failures" -gt 0 ]]; then
  exit 1
fi

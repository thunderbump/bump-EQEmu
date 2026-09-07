#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
tmp_root="$(mktemp -d)"
test_filter="${1:-}"

cleanup() {
  rm -rf "$tmp_root"
}
trap cleanup EXIT

failures=0
assertion_failed=0
tests_run=0

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

assert_combined_checks_not_run() {
  local evidence="$1" candidate_commit="$2"
  [[ -f "$evidence/afk-checks.json" ]] || return 1
  assert_json_equals "$evidence/result.json" '.checks | map(.scenario) | join(",")' "tier1-build-and-unit-tests,canonical-zone-harness,actor-events-runtime"
  assert_json_equals "$evidence/result.json" '.checks | map(.profile) | join(",")' "tier1,tier3-harness,actor-queue-tier3"
  assert_json_equals "$evidence/result.json" '.checks | map(.status) | join(",")' "not_run,not_run,not_run"
  assert_json_equals "$evidence/result.json" '.checks | map(.candidate_commit) | unique | join(",")' "$candidate_commit"
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

  if [[ -n "$test_filter" && "$name" != "$test_filter" ]]; then
    return
  fi
  tests_run=$((tests_run + 1))
  assertion_failed=0
  if "$@" && [[ "$assertion_failed" -eq 0 ]]; then
    printf 'ok - %s\n' "$name"
  else
    fail "$name"
  fi
}

make_source_repo() {
  local -n source_ref="$1"
  local suffix="${2:-}"
  source_ref="$tmp_root/source-repo${suffix:+-$suffix}"
  rm -rf "$source_ref"
  mkdir -p "$source_ref/scripts"
  cat >"$source_ref/scripts/validate.sh" <<'SCRIPT'
#!/usr/bin/env bash
set -euo pipefail
if [[ "${VALIDATION_WORKER_TEST_IGNORE_TERM:-0}" == "1" ]]; then
  trap '' TERM
  while :; do sleep 1; done
fi
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
if [[ "${VALIDATION_WORKER_TEST_FAIL_ACTOR_QUEUE:-0}" == "1" && " $* " == *" actor-queue-tier3"* ]]; then
  printf 'actor queue requested failure\n' >&2
  exit 1
fi
if [[ "${VALIDATION_WORKER_TEST_TIER1_EXIT_CODE:-0}" != "0" && " $* " == *" tier1"* ]]; then
  exit "$VALIDATION_WORKER_TEST_TIER1_EXIT_CODE"
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
if [[ "${VALIDATION_WORKER_TEST_ASSERT_SELF_CONTAINED_GIT:-0}" == "1" ]]; then
  repo_git_dir="$(git -C "$repo_dir" rev-parse --absolute-git-dir)"
  submodule_git_dir="$(git -C "$repo_dir/vendor/submodule-fixture" rev-parse --absolute-git-dir)"
  [[ "$repo_git_dir" == "$repo_dir/.git" ]] || {
    printf 'checkout git directory is not self-contained: %s\n' "$repo_git_dir" >&2
    exit 1
  }
  [[ "$submodule_git_dir" == "$repo_dir/.git/modules/"* ]] || {
    printf 'submodule git directory is not self-contained: %s\n' "$submodule_git_dir" >&2
    exit 1
  }
fi
printf 'fake validate: %s\n' "$*"
if [[ " $* " == *" tier3-harness "* && "${VALIDATION_WORKER_TEST_OMIT_PROOF_PROFILE:-}" != "tier3-harness" ]]; then
  printf '[PASS] canonical-zone-harness\n'
fi
if [[ " $* " == *" actor-queue-tier3 "* && "${VALIDATION_WORKER_TEST_OMIT_PROOF_PROFILE:-}" != "actor-queue-tier3" ]]; then
  printf '[PASS] actor-events-runtime\n'
fi
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
  local suffix="${2:-}" fixture_source
  make_source_repo fixture_source "$suffix"
  source_ref="$fixture_source"
  cp "$repo_root/scripts/validation-worker.sh" "$source_ref/scripts/validation-worker.sh"
  cp "$repo_root/scripts/validate-afk" "$source_ref/scripts/validate-afk"
  chmod +x "$source_ref/scripts/validation-worker.sh" "$source_ref/scripts/validate-afk"
  git -C "$source_ref" add scripts/validation-worker.sh scripts/validate-afk
  git -C "$source_ref" commit -m 'add validation worker entry points' >/dev/null 2>&1
  configure_afk_host "$source_ref"
}

configure_afk_host() {
  local source="$1" operator_home canonical_repo stack
  operator_home="$tmp_root/operator-home"
  canonical_repo="$operator_home/Projects/bump-eqemu/bump-EQEmu"
  stack="$operator_home/Projects/bump-eqemu/bump-akk-stack-validation"
  mkdir -p "$stack"
  printf 'ENV=development\n' >"$stack/.env"
  rm -f "$operator_home/.gitconfig"
  rm -rf "$canonical_repo"
  git clone "$source" "$canonical_repo" >/dev/null 2>&1
}

add_submodule_gitlink() {
  local source="$1" path="$2" url="$3" commit="$4"
  cat >"$source/.gitmodules" <<EOF
[submodule "$path"]
	path = $path
	url = $url
EOF
  git -C "$source" update-index --add --cacheinfo "160000,$commit,$path"
  git -C "$source" add .gitmodules
  git -C "$source" commit -m 'add candidate submodule config' >/dev/null 2>&1
}

make_submodule_update_observer() {
  local fake_bin="$1" marker="$2" skip_update="${3:-0}" real_git
  real_git="$(command -v git)"
  mkdir -p "$fake_bin"
  cat >"$fake_bin/git" <<SCRIPT
#!/usr/bin/env bash
if [[ " \$* " == *" submodule update "* ]]; then
  : >"$marker"
  if [[ "$skip_update" == "1" ]]; then
    exit 0
  fi
fi
exec "$real_git" "\$@"
SCRIPT
  chmod +x "$fake_bin/git"
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

fixture_home="$(mktemp -d)"
trap 'rm -rf "$fixture_home"' EXIT
mkdir -p "$fixture_home/server"
printf '%s\n' '{"server":{"database":{},"qsdatabase":{}}}' >"$fixture_home/server/eqemu_config.json"
HOME="$fixture_home" EQEMU_DB_PASSWORD=fixture ZONE_HARNESS_PORT=9099 ZONE_HARNESS_LOG_FILE=/tmp/zone-harness-test.log bash -c "$payload"
SCRIPT
  chmod +x "$fake_bin/docker-compose"
}

make_source_repo_with_submodule() {
  local -n source_ref="$1"
  local suffix="${2:-}" base_source submodule_repo
  make_source_repo base_source "$suffix"
  source_ref="$base_source"
  submodule_repo="$tmp_root/submodule-repo${suffix:+-$suffix}"
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
  assert_contains "$output" "actor-queue-tier3"
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
        "trusted_files": [
            "scripts/validation-worker.sh",
            "scripts/validate.sh",
            "scripts/check-akkstack-contract.sh",
            "scripts/lib/akkstack-routing.sh",
        ],
        "timeout_seconds": 2700,
    },
}
PY
}

test_profiles_json() {
  local status output
  capture_run status output "$repo_root/scripts/validation-worker.sh" profiles --json
  [[ "$status" -eq 0 ]] || return 1
  jq -e '.profiles | length == 5' >/dev/null <<<"$output" || return 1
  jq -e '.profiles[] | select(.name == "preflight") | .mutation_classification == "read-only"' >/dev/null <<<"$output" || return 1
  jq -e '.profiles[] | select(.name == "safe") | (.timeout_guidance | length > 0) and (.lock_guidance | length > 0)' >/dev/null <<<"$output" || return 1
  jq -e '.profiles[] | select(.name == "tier3-harness")' >/dev/null <<<"$output" || return 1
  jq -e '.profiles[] | select(.name == "tier1-tier3-harness") | .mutation_classification == "database-mutating/runtime-fixture"' >/dev/null <<<"$output" || return 1
  jq -e '.profiles[] | select(.name == "actor-queue-tier3") | .mutation_classification == "database-mutating/runtime-fixture"' >/dev/null <<<"$output" || return 1
}

test_discovered_profiles_are_accepted_requests() {
  local source profiles profile request evidence status output
  make_source_repo source discovered-profiles
  profiles="$($repo_root/scripts/validation-worker.sh profiles --json)"

  while IFS= read -r profile; do
    reset_worker_home
    evidence="$tmp_root/evidence-discovered-$profile"
    request="$tmp_root/discovered-$profile.json"
    write_request "$request" "$source" "$evidence" HEAD "" 0 10 "" "$profile"

    capture_run status output worker_env "$repo_root/scripts/validation-worker.sh" run --request "$request"

    [[ "$status" -eq 0 ]] || return 1
    assert_json_equals "$evidence/result.json" .profile "$profile"
  done < <(jq -r '.profiles[].name' <<<"$profiles")
}

test_actor_queue_tier3_profile_builds_before_runtime_from_clean_fetch() {
  local source request evidence status output validation_log first_tier1 first_actor_queue
  make_source_repo source actor-queue-tier3
  evidence="$tmp_root/evidence-actor-queue-tier3"
  request="$tmp_root/actor-queue-tier3.json"
  write_request "$request" "$source" "$evidence" HEAD "" 0 10 "" actor-queue-tier3

  capture_run status output worker_env "$repo_root/scripts/validation-worker.sh" run --request "$request"

  [[ "$status" -eq 0 ]] || return 1
  assert_json_equals "$evidence/result.json" .status passed
  assert_json_equals "$evidence/result.json" .profile actor-queue-tier3
  validation_log="$evidence/logs/validation.log"
  first_tier1="$(grep -n 'fake validate: --stack validation --dry-run tier1' "$validation_log" | head -n1 | cut -d: -f1)"
  first_actor_queue="$(grep -n 'fake validate: --stack validation --dry-run actor-queue-tier3' "$validation_log" | head -n1 | cut -d: -f1)"
  [[ -n "$first_tier1" && -n "$first_actor_queue" ]] || return 1
  [[ "$first_tier1" -lt "$first_actor_queue" ]] || return 1
}

test_safe_profile_remains_available_for_non_afk_requests() {
  local source request evidence status output
  make_source_repo source safe-profile
  request="$tmp_root/safe-profile.json"
  evidence="$tmp_root/evidence-safe-profile"
  write_request "$request" "$source" "$evidence" HEAD "" 0 10 "" safe

  capture_run status output worker_env "$repo_root/scripts/validation-worker.sh" run --request "$request"

  [[ "$status" -eq 0 ]] || return 1
  assert_json_equals "$evidence/result.json" .profile safe
  assert_contains "$(cat "$evidence/logs/validation.log")" "fake validate: --stack validation --dry-run safe"
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

  capture_run status output env VALIDATION_WORKER_HOME="$tmp_root/worker-home" VALIDATION_WORKER_VALIDATE_DRY_RUN=1 VALIDATION_WORKER_TEST_FILE_SUBMODULE_ROOT="$tmp_root" VALIDATION_WORKER_TEST_ASSERT_SUBMODULE=1 "$repo_root/scripts/validation-worker.sh" run --request "$request"

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

  capture_run status output env PATH="$fake_bin:$PATH" VALIDATION_WORKER_HOME="$tmp_root/worker-home" VALIDATION_WORKER_VALIDATE_DRY_RUN=1 VALIDATION_WORKER_TEST_FILE_SUBMODULE_ROOT="$tmp_root" "$repo_root/scripts/validation-worker.sh" run --request "$request"

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
  write_request "$request" "$source" "$evidence" HEAD "$head" 0 10 "$stack" tier1-tier3-harness
  stack_lock="$stack/.validation-worker-code.lock"
  mkdir "$stack_lock"

  capture_run status output env VALIDATION_WORKER_HOME="$tmp_root/worker-home" VALIDATION_WORKER_VALIDATE_DRY_RUN=1 VALIDATION_WORKER_TEST_FILE_SUBMODULE_ROOT="$tmp_root" "$repo_root/scripts/validation-worker.sh" run --request "$request"

  [[ "$status" -eq 1 ]] || return 1
  assert_json_equals "$evidence/result.json" .category stack_busy
  assert_combined_checks_not_run "$evidence" "$head"
  [[ -f "$tmp_root/worker-home/checkouts/run-$(basename "$evidence")/vendor/submodule-fixture/marker.txt" ]] || return 1
}

test_commit_mismatch() {
  local source request evidence status output
  make_source_repo source
  reset_worker_home
  evidence="$tmp_root/evidence-mismatch"
  request="$tmp_root/mismatch.json"
  write_request "$request" "$source" "$evidence" HEAD "0000000000000000000000000000000000000000" 0 10 "" tier1-tier3-harness

  capture_run status output worker_env "$repo_root/scripts/validation-worker.sh" run --request "$request"

  [[ "$status" -eq 1 ]] || return 1
  assert_json_equals "$evidence/result.json" .category commit_mismatch
  assert_json_equals "$evidence/result.json" .expected_commit 0000000000000000000000000000000000000000
  assert_combined_checks_not_run "$evidence" 0000000000000000000000000000000000000000
}

test_fetch_failure() {
  local request evidence status output
  reset_worker_home
  evidence="$tmp_root/evidence-fetch-failure"
  request="$tmp_root/fetch-failure.json"
  write_request "$request" "$tmp_root/no-such-repo" "$evidence" HEAD aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa 0 10 "" tier1-tier3-harness

  capture_run status output worker_env "$repo_root/scripts/validation-worker.sh" run --request "$request"

  [[ "$status" -eq 1 ]] || return 1
  assert_json_equals "$evidence/result.json" .category fetch_failed
  assert_combined_checks_not_run "$evidence" aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa
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

test_local_checkout_rejects_drifting_submodule() {
  local source request evidence status output
  make_source_repo_with_submodule source drift
  reset_worker_home
  git -C "$source/vendor/submodule-fixture" config user.email worker-test@example.com
  git -C "$source/vendor/submodule-fixture" config user.name 'Worker Test'
  printf 'drifted submodule fixture\n' >"$source/vendor/submodule-fixture/marker.txt"
  git -C "$source/vendor/submodule-fixture" add marker.txt
  git -C "$source/vendor/submodule-fixture" commit -m 'drift submodule' >/dev/null 2>&1
  evidence="$tmp_root/evidence-local-submodule-drift"
  request="$tmp_root/local-submodule-drift.json"
  write_local_checkout_request "$request" "$source" "$evidence"

  capture_run status output worker_env "$repo_root/scripts/validation-worker.sh" run --request "$request"

  [[ "$status" -eq 1 ]] || return 1
  assert_json_equals "$evidence/result.json" .category submodule_failed
}

test_lock_contention() {
  local source request evidence status output lock_dir existing_checkout head
  make_source_repo source
  reset_worker_home
  evidence="$tmp_root/evidence-busy"
  request="$tmp_root/busy.json"
  head="$(git -C "$source" rev-parse HEAD)"
  write_request "$request" "$source" "$evidence" HEAD "$head" 0 10 "" tier1-tier3-harness
  lock_dir="$tmp_root/worker-home/locks/validation-slot.lock"
  mkdir -p "$lock_dir"
  existing_checkout="$tmp_root/worker-home/checkouts/run-$(basename "$evidence")"
  mkdir -p "$existing_checkout"
  printf 'do not mutate before lock\n' >"$existing_checkout/marker"

  capture_run status output worker_env "$repo_root/scripts/validation-worker.sh" run --request "$request"

  [[ "$status" -eq 1 ]] || return 1
  assert_json_equals "$evidence/result.json" .category worker_busy
  assert_combined_checks_not_run "$evidence" "$head"
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

test_timeout_kills_a_term_resistant_validation_child() {
  local source request evidence status output start end
  make_source_repo source resistant-timeout
  reset_worker_home
  evidence="$tmp_root/evidence-resistant-timeout"
  request="$tmp_root/resistant-timeout.json"
  write_request "$request" "$source" "$evidence" HEAD "" 0 1

  start="$(date +%s)"
  capture_run status output env VALIDATION_WORKER_HOME="$tmp_root/worker-home" \
    VALIDATION_WORKER_VALIDATE_DRY_RUN=1 VALIDATION_WORKER_TEST_IGNORE_TERM=1 \
    VALIDATION_WORKER_TERMINATION_GRACE_SECONDS=1 \
    "$repo_root/scripts/validation-worker.sh" run --request "$request"
  end="$(date +%s)"

  [[ "$status" -eq 1 ]] || return 1
  [[ $((end - start)) -lt 4 ]] || return 1
  assert_json_equals "$evidence/result.json" .category timeout
  [[ ! -e "$tmp_root/worker-home/locks/validation-slot.lock" ]] || return 1
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
  local source request evidence status output
  make_source_repo source
  reset_worker_home
  evidence="$tmp_root/evidence-tier1-tier3"
  request="$tmp_root/tier1-tier3.json"
  write_request "$request" "$source" "$evidence" HEAD "" 0 10 "" tier1-tier3-harness

  capture_run status output env VALIDATION_WORKER_HOME="$tmp_root/worker-home" VALIDATION_WORKER_VALIDATE_DRY_RUN=1 "$repo_root/scripts/validation-worker.sh" run --request "$request"

  [[ "$status" -eq 0 ]] || return 1
  assert_json_equals "$evidence/result.json" .status passed
  assert_contains "$(cat "$evidence/logs/tier1-build-and-unit-tests.log")" "fake validate: --stack validation --dry-run tier1"
  assert_contains "$(cat "$evidence/logs/tier3-zone-harness.log")" "fake validate: --stack validation --dry-run tier3-harness"
  assert_contains "$(cat "$evidence/logs/actor-queue-runtime.log")" "fake validate: --stack validation --dry-run actor-queue-tier3"
  assert_json_equals "$evidence/result.json" '.checks | map(.scenario) | join(",")' "tier1-build-and-unit-tests,canonical-zone-harness,actor-events-runtime"
}

test_combined_profile_rejects_a_zero_exit_without_runtime_proof() {
  local source request evidence status output
  make_source_repo source combined-missing-proof
  reset_worker_home
  evidence="$tmp_root/evidence-combined-missing-proof"
  request="$tmp_root/combined-missing-proof.json"
  write_request "$request" "$source" "$evidence" HEAD "" 0 10 "" tier1-tier3-harness
  mkdir -p "$evidence/logs"
  printf '[PASS] actor-events-runtime\n' >"$evidence/logs/actor-queue-runtime.log"

  capture_run status output env VALIDATION_WORKER_HOME="$tmp_root/worker-home" \
    VALIDATION_WORKER_VALIDATE_DRY_RUN=1 \
    VALIDATION_WORKER_TEST_OMIT_PROOF_PROFILE=actor-queue-tier3 \
    "$repo_root/scripts/validation-worker.sh" run --request "$request"

  [[ "$status" -eq 1 ]] || return 1
  assert_json_equals "$evidence/result.json" .category validation_failed
  assert_json_equals "$evidence/result.json" '.checks | map(.status) | join(",")' "passed,passed,rejected"
  assert_json_equals "$evidence/result.json" '.checks[2].completion_marker' "[PASS] actor-events-runtime"
  assert_contains "$(cat "$evidence/logs/actor-queue-runtime.log")" "required completion evidence missing: [PASS] actor-events-runtime"
}

test_combined_profile_propagates_actor_queue_failure() {
  local source request evidence status output
  make_source_repo source combined-actor-failure
  reset_worker_home
  evidence="$tmp_root/evidence-combined-actor-failure"
  request="$tmp_root/combined-actor-failure.json"
  write_request "$request" "$source" "$evidence" HEAD "" 0 10 "" tier1-tier3-harness

  capture_run status output env VALIDATION_WORKER_HOME="$tmp_root/worker-home" VALIDATION_WORKER_VALIDATE_DRY_RUN=1 VALIDATION_WORKER_TEST_FAIL_ACTOR_QUEUE=1 "$repo_root/scripts/validation-worker.sh" run --request "$request"

  [[ "$status" -eq 1 ]] || return 1
  assert_json_equals "$evidence/result.json" .category validation_failed
  assert_json_equals "$evidence/result.json" '.checks | map(.status) | join(",")' "passed,passed,rejected"
  assert_contains "$(cat "$evidence/logs/actor-queue-runtime.log")" "actor queue requested failure"
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
  assert_contains "$(cat "$evidence/logs/tier1-build-and-unit-tests.log")" "fake validate: --stack validation --dry-run tier1"
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
  validation_log="$evidence/logs/tier1-build-and-unit-tests.log"
  assert_contains "$(cat "$validation_log")" "tier1 requested failure"
  if grep -q 'fake validate: --stack validation --dry-run tier3-harness' "$evidence/logs/tier3-zone-harness.log"; then
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

test_worker_termination_kills_checkout_deletion_descendant_after_leader_exit() {
  local source request evidence checkout_dir fake_bin real_rm marker child_ready child_pid_file worker_pid status output_file start end child_pid child_stat
  make_source_repo source interrupted-checkout-deletion
  reset_worker_home
  evidence="$tmp_root/evidence-interrupted-checkout-deletion"
  request="$tmp_root/interrupted-checkout-deletion.json"
  checkout_dir="$tmp_root/worker-home/checkouts/run-$(basename "$evidence")"
  fake_bin="$tmp_root/fake-bin-interrupted-checkout-deletion"
  marker="$tmp_root/interrupted-checkout-deletion.started"
  child_ready="$tmp_root/interrupted-checkout-deletion.child-ready"
  child_pid_file="$tmp_root/interrupted-checkout-deletion.pid"
  output_file="$tmp_root/interrupted-checkout-deletion.out"
  real_rm="$(command -v rm)"
  mkdir -p "$checkout_dir" "$fake_bin"
  printf 'old checkout\n' >"$checkout_dir/marker"
  cat >"$fake_bin/rm" <<SCRIPT
#!/usr/bin/env bash
if [[ "\${*: -1}" == "$checkout_dir" ]]; then
  # The group leader exits on TERM, while this descendant deliberately
  # survives it. Cleanup must continue tracking the process group and KILL the
  # descendant before restoring the stack or releasing locks.
  (
    trap '' TERM
    : >"$child_ready"
    while :; do sleep 1; done
  ) &
  descendant_pid=\$!
  printf '%s\n' "\$descendant_pid" >"$child_pid_file"
  # Do not publish fixture readiness until the descendant confirms that its
  # TERM disposition is installed. Otherwise the worker signal can race the
  # trap setup and let this regression test pass without exercising KILL.
  while [[ ! -e "$child_ready" ]]; do sleep 0.01; done
  : >"$marker"
  trap 'exit 0' TERM
  wait "\$descendant_pid"
fi
exec "$real_rm" "\$@"
SCRIPT
  chmod +x "$fake_bin/rm"
  write_request "$request" "$source" "$evidence" HEAD "" 0 60

  env PATH="$fake_bin:$PATH" VALIDATION_WORKER_HOME="$tmp_root/worker-home" \
    VALIDATION_WORKER_VALIDATE_DRY_RUN=1 VALIDATION_WORKER_TERMINATION_GRACE_SECONDS=1 \
    "$repo_root/scripts/validation-worker.sh" run --request "$request" >"$output_file" 2>&1 &
  worker_pid=$!
  for _ in {1..200}; do
    [[ -f "$marker" ]] && break
    kill -0 "$worker_pid" 2>/dev/null || break
    sleep 0.05
  done
  [[ -f "$marker" ]] || return 1

  start="$(date +%s)"
  kill -TERM "$worker_pid"
  set +e
  wait "$worker_pid"
  status=$?
  set -e
  end="$(date +%s)"

  [[ "$status" -eq 143 ]] || return 1
  [[ $((end - start)) -lt 4 ]] || return 1
  child_pid="$(cat "$child_pid_file")"
  child_stat="$(ps -o stat= -p "$child_pid" 2>/dev/null || true)"
  [[ -z "$child_stat" || "$child_stat" == Z* ]] || {
    printf 'TERM-resistant checkout descendant still active with status %s\n' "$child_stat" >&2
    return 1
  }
  [[ ! -e "$tmp_root/worker-home/locks/validation-slot.lock" ]] || return 1
  assert_json_equals "$evidence/result.json" .category interrupted
}

test_worker_termination_during_fetch_is_bounded() {
  local source request evidence fake_bin real_git marker child_pid_file worker_pid status output_file start end
  make_source_repo source interrupted-fetch
  reset_worker_home
  evidence="$tmp_root/evidence-interrupted-fetch"
  request="$tmp_root/interrupted-fetch.json"
  fake_bin="$tmp_root/fake-bin-interrupted-fetch"
  marker="$tmp_root/interrupted-fetch.started"
  child_pid_file="$tmp_root/interrupted-fetch.pid"
  output_file="$tmp_root/interrupted-fetch.out"
  real_git="$(command -v git)"
  mkdir -p "$fake_bin"
  cat >"$fake_bin/git" <<SCRIPT
#!/usr/bin/env bash
if [[ " \$* " == *" fetch "* ]]; then
  printf '%s\n' "\$\$" >"$child_pid_file"
  : >"$marker"
  trap '' TERM
  while :; do sleep 1; done
fi
exec "$real_git" "\$@"
SCRIPT
  chmod +x "$fake_bin/git"
  write_request "$request" "$source" "$evidence" HEAD "" 0 60

  env PATH="$fake_bin:$PATH" VALIDATION_WORKER_HOME="$tmp_root/worker-home" \
    VALIDATION_WORKER_VALIDATE_DRY_RUN=1 VALIDATION_WORKER_TERMINATION_GRACE_SECONDS=1 \
    "$repo_root/scripts/validation-worker.sh" run --request "$request" >"$output_file" 2>&1 &
  worker_pid=$!
  for _ in {1..200}; do
    [[ -f "$marker" ]] && break
    kill -0 "$worker_pid" 2>/dev/null || break
    sleep 0.05
  done
  [[ -f "$marker" ]] || return 1

  start="$(date +%s)"
  kill -TERM "$worker_pid"
  set +e
  wait "$worker_pid"
  status=$?
  set -e
  end="$(date +%s)"

  [[ "$status" -eq 143 ]] || return 1
  [[ $((end - start)) -lt 4 ]] || return 1
  ! kill -0 "$(cat "$child_pid_file")" 2>/dev/null || return 1
  [[ ! -e "$tmp_root/worker-home/locks/validation-slot.lock" ]] || return 1
  assert_json_equals "$evidence/result.json" .category interrupted
}

test_worker_termination_during_submodule_preparation_is_bounded() {
  local source request evidence fake_bin real_git marker child_pid_file worker_pid status output_file start end head
  make_source_repo_with_submodule source interrupted-submodule
  reset_worker_home
  head="$(git -C "$source" rev-parse HEAD)"
  evidence="$tmp_root/evidence-interrupted-submodule"
  request="$tmp_root/interrupted-submodule.json"
  fake_bin="$tmp_root/fake-bin-interrupted-submodule"
  marker="$tmp_root/interrupted-submodule.started"
  child_pid_file="$tmp_root/interrupted-submodule.pid"
  output_file="$tmp_root/interrupted-submodule.out"
  real_git="$(command -v git)"
  mkdir -p "$fake_bin"
  cat >"$fake_bin/git" <<SCRIPT
#!/usr/bin/env bash
if [[ " \$* " == *" submodule update "* ]]; then
  printf '%s\n' "\$\$" >"$child_pid_file"
  : >"$marker"
  trap '' TERM
  while :; do sleep 1; done
fi
exec "$real_git" "\$@"
SCRIPT
  chmod +x "$fake_bin/git"
  write_request "$request" "$source" "$evidence" HEAD "$head" 0 60

  env PATH="$fake_bin:$PATH" VALIDATION_WORKER_HOME="$tmp_root/worker-home" \
    VALIDATION_WORKER_VALIDATE_DRY_RUN=1 VALIDATION_WORKER_TEST_FILE_SUBMODULE_ROOT="$tmp_root" \
    VALIDATION_WORKER_TERMINATION_GRACE_SECONDS=1 \
    "$repo_root/scripts/validation-worker.sh" run --request "$request" >"$output_file" 2>&1 &
  worker_pid=$!
  for _ in {1..200}; do
    [[ -f "$marker" ]] && break
    kill -0 "$worker_pid" 2>/dev/null || break
    sleep 0.05
  done
  [[ -f "$marker" ]] || return 1

  start="$(date +%s)"
  kill -TERM "$worker_pid"
  set +e
  wait "$worker_pid"
  status=$?
  set -e
  end="$(date +%s)"

  [[ "$status" -eq 143 ]] || return 1
  [[ $((end - start)) -lt 4 ]] || return 1
  ! kill -0 "$(cat "$child_pid_file")" 2>/dev/null || return 1
  [[ ! -e "$tmp_root/worker-home/locks/validation-slot.lock" ]] || return 1
  assert_json_equals "$evidence/result.json" .category interrupted
}

test_worker_failed_termination_retains_stack_and_locks() {
  local source request evidence stack other_checkout fake_bin real_ps pgid_file worker_pid status output_file rebound=0
  make_source_repo source failed-termination-cleanup
  reset_worker_home
  evidence="$tmp_root/evidence-failed-termination-cleanup"
  request="$tmp_root/failed-termination-cleanup.json"
  stack="$tmp_root/failed-termination-cleanup-stack"
  other_checkout="$tmp_root/failed-termination-cleanup-other-checkout"
  fake_bin="$tmp_root/fake-bin-failed-termination-cleanup"
  pgid_file="$tmp_root/failed-termination-cleanup.pgid"
  output_file="$tmp_root/failed-termination-cleanup.out"
  real_ps="$(command -v ps)"
  mkdir -p "$stack" "$other_checkout" "$fake_bin"
  printf 'ENV=development\n' >"$stack/.env"
  ln -s "$other_checkout" "$stack/code"
  write_request "$request" "$source" "$evidence" HEAD "" 0 60 "$stack" preflight

  # Simulate a process group that remains observable after KILL. The initial
  # lookup still uses the real ps so termination targets the actual setsid
  # group; subsequent membership checks retain that pgid through both grace
  # periods and exercise the cleanup-withheld failure path.
  cat >"$fake_bin/ps" <<SCRIPT
#!/usr/bin/env bash
if [[ "\${1:-}" == "-eo" && "\${2:-}" == "pgid=,stat=" && -s "$pgid_file" ]]; then
  printf '%s S\n' "\$(cat "$pgid_file")"
  exit 0
fi
if [[ "\${1:-}" == "-o" && "\${2:-}" == "pgid=" ]]; then
  output="\$("$real_ps" "\$@")"
  printf '%s\n' "\$output"
  printf '%s\n' "\$output" | tr -d ' ' >"$pgid_file"
  exit 0
fi
exec "$real_ps" "\$@"
SCRIPT
  chmod +x "$fake_bin/ps"

  env PATH="$fake_bin:$PATH" VALIDATION_WORKER_HOME="$tmp_root/worker-home" \
    VALIDATION_WORKER_VALIDATE_DRY_RUN=1 \
    VALIDATION_WORKER_TEST_IGNORE_TERM=1 \
    VALIDATION_WORKER_TERMINATION_GRACE_SECONDS=1 \
    "$repo_root/scripts/validation-worker.sh" run --request "$request" >"$output_file" 2>&1 &
  worker_pid=$!

  for _ in {1..200}; do
    if [[ -L "$stack/code" && "$(readlink "$stack/code")" != "$other_checkout" ]]; then
      rebound=1
      break
    fi
    kill -0 "$worker_pid" 2>/dev/null || break
    sleep 0.05
  done
  if [[ "$rebound" -ne 1 ]]; then
    kill -KILL "$worker_pid" 2>/dev/null || true
    wait "$worker_pid" 2>/dev/null || true
    printf 'worker did not bind the validation stack before failed termination test\n' >&2
    return 1
  fi

  kill -TERM "$worker_pid"
  set +e
  wait "$worker_pid"
  status=$?
  set -e

  [[ "$status" -eq 1 ]] || {
    printf 'expected failed termination status 1, got %s; output:\n' "$status" >&2
    cat "$output_file" >&2
    return 1
  }
  [[ "$(readlink "$stack/code")" != "$other_checkout" ]] || return 1
  [[ -d "$stack/.validation-worker-code.lock" ]] || return 1
  [[ -d "$tmp_root/worker-home/locks/validation-slot.lock" ]] || return 1
  assert_json_equals "$evidence/result.json" .category child_termination_failed
  assert_json_equals "$evidence/result.json" .exit_code 1
}

test_worker_termination_restores_stack_and_releases_locks() {
  local source request evidence stack other_checkout worker_pid status output_file rebound=0
  make_source_repo source interrupted-cleanup
  reset_worker_home
  evidence="$tmp_root/evidence-interrupted-cleanup"
  request="$tmp_root/interrupted-cleanup.json"
  stack="$tmp_root/interrupted-cleanup-stack"
  other_checkout="$tmp_root/interrupted-cleanup-other-checkout"
  output_file="$tmp_root/interrupted-cleanup.out"
  mkdir -p "$stack" "$other_checkout"
  printf 'ENV=development\n' >"$stack/.env"
  ln -s "$other_checkout" "$stack/code"
  write_request "$request" "$source" "$evidence" HEAD "" 0 60 "$stack" preflight

  env VALIDATION_WORKER_HOME="$tmp_root/worker-home" \
    VALIDATION_WORKER_VALIDATE_DRY_RUN=1 \
    VALIDATION_WORKER_TEST_IGNORE_TERM=1 \
    VALIDATION_WORKER_TERMINATION_GRACE_SECONDS=1 \
    "$repo_root/scripts/validation-worker.sh" run --request "$request" >"$output_file" 2>&1 &
  worker_pid=$!

  for _ in {1..200}; do
    if [[ -L "$stack/code" && "$(readlink "$stack/code")" != "$other_checkout" ]]; then
      rebound=1
      break
    fi
    kill -0 "$worker_pid" 2>/dev/null || break
    sleep 0.05
  done
  if [[ "$rebound" -ne 1 ]]; then
    kill -TERM "$worker_pid" 2>/dev/null || true
    wait "$worker_pid" 2>/dev/null || true
    printf 'worker did not bind the validation stack before interruption\n' >&2
    return 1
  fi

  kill -TERM "$worker_pid"
  set +e
  wait "$worker_pid"
  status=$?
  set -e

  [[ "$status" -eq 143 ]] || {
    printf 'expected interrupted worker status 143, got %s; output:\n' "$status" >&2
    cat "$output_file" >&2
    return 1
  }
  [[ "$(readlink "$stack/code")" == "$other_checkout" ]] || return 1
  [[ ! -e "$stack/.validation-worker-code.lock" ]] || return 1
  [[ ! -e "$tmp_root/worker-home/locks/validation-slot.lock" ]] || return 1
  assert_json_equals "$evidence/stack-binding.json" .restore_status restored
  assert_json_equals "$evidence/result.json" .category interrupted
  assert_json_equals "$evidence/result.json" .exit_code 143
}

test_current_afk_wrapper_forwards_termination_and_retains_evidence() {
  local source evidence stack wrapper_pid status output_file rebound=0
  make_afk_contract_repo source wrapper-interruption
  reset_worker_home
  evidence="$tmp_root/current-afk-interrupted-evidence"
  stack="$tmp_root/operator-home/Projects/bump-eqemu/bump-akk-stack-validation"
  output_file="$tmp_root/current-afk-interrupted.out"

  env HOME="$tmp_root/operator-home" VALIDATION_WORKER_HOME="$tmp_root/worker-home" \
    VALIDATION_WORKER_TEST_SLEEP=30 VALIDATION_AFK_EVIDENCE_DIR="$evidence" \
    "$source/scripts/validate-afk" >"$output_file" 2>&1 &
  wrapper_pid=$!

  for _ in {1..200}; do
    if [[ -L "$stack/code" ]]; then
      rebound=1
      break
    fi
    kill -0 "$wrapper_pid" 2>/dev/null || break
    sleep 0.05
  done
  if [[ "$rebound" -ne 1 ]]; then
    kill -TERM "$wrapper_pid" 2>/dev/null || true
    wait "$wrapper_pid" 2>/dev/null || true
    return 1
  fi

  kill -TERM "$wrapper_pid"
  set +e
  wait "$wrapper_pid"
  status=$?
  set -e

  [[ "$status" -eq 143 ]] || return 1
  [[ ! -e "$stack/code" ]] || return 1
  [[ ! -e "$stack/.validation-worker-code.lock" ]] || return 1
  [[ ! -e "$tmp_root/worker-home/locks/validation-slot.lock" ]] || return 1
  [[ -f "$evidence/result.json" ]] || return 1
  assert_json_equals "$evidence/result.json" .category interrupted
  assert_json_equals "$evidence/result.json" '.checks | map(.status) | join(",")' "inconclusive,not_run,not_run"
  [[ -z "$(find "$tmp_root/worker-home/requests" -type f -print -quit)" ]] || return 1
}

test_akkstack_dir_real_code_directory_fails_fast() {
  local source request evidence status output stack head
  make_source_repo source
  reset_worker_home
  evidence="$tmp_root/evidence-env-real-code"
  request="$tmp_root/env-real-code.json"
  stack="$tmp_root/env-real-code-stack"
  mkdir -p "$stack/code"
  printf 'ENV=development\n' >"$stack/.env"
  head="$(git -C "$source" rev-parse HEAD)"
  write_request "$request" "$source" "$evidence" HEAD "$head" 0 10 "" tier1-tier3-harness

  capture_run status output env VALIDATION_WORKER_HOME="$tmp_root/worker-home" VALIDATION_WORKER_VALIDATE_DRY_RUN=1 AKKSTACK_DIR="$stack" "$repo_root/scripts/validation-worker.sh" run --request "$request"

  [[ "$status" -eq 1 ]] || return 1
  assert_json_equals "$evidence/result.json" .category stack_binding_failed
  assert_combined_checks_not_run "$evidence" "$head"
  assert_json_equals "$evidence/stack-binding.json" .previous_kind directory
  [[ -d "$stack/code" && ! -L "$stack/code" ]] || return 1
}

test_current_afk_command_validates_exact_head_without_arguments() {
  local source evidence status output head
  make_afk_contract_repo source current-command
  reset_worker_home
  head="$(git -C "$source" rev-parse HEAD)"
  evidence="$tmp_root/current-afk-evidence"

  capture_run status output env HOME="$tmp_root/operator-home" \
    VALIDATION_WORKER_HOME="$tmp_root/worker-home" \
    VALIDATION_WORKER_VALIDATE_DRY_RUN=1 \
    VALIDATION_WORKER_AFK_MODE=1 \
    VALIDATION_AFK_EVIDENCE_DIR="$evidence" \
    "$source/scripts/validate-afk"

  [[ "$status" -eq 0 ]] || return 1
  assert_contains "$output" "Validating committed Candidate $head"
  assert_contains "$output" "Validation evidence: $evidence"
  assert_json_equals "$evidence/request.json" .commit "$head"
  assert_json_equals "$evidence/request.json" .ref "$head"
  assert_json_equals "$evidence/request.json" .profile tier1-tier3-harness
  assert_json_equals "$evidence/request.json" .stack.role validation
  assert_json_equals "$evidence/result.json" .actual_checkout_commit "$head"
  assert_json_equals "$evidence/result.json" .status passed
  assert_json_equals "$evidence/result.json" '.checks | map(.log_path) | join(",")' "logs/tier1-build-and-unit-tests.log,logs/tier3-zone-harness.log,logs/actor-queue-runtime.log"
  assert_contains "$(cat "$evidence/logs/tier1-build-and-unit-tests.log")" "fake validate: --stack validation tier1"
  assert_contains "$(cat "$evidence/logs/tier3-zone-harness.log")" "fake validate: --stack validation tier3-harness"
  assert_contains "$(cat "$evidence/logs/actor-queue-runtime.log")" "fake validate: --stack validation actor-queue-tier3"
  assert_json_equals "$evidence/result.json" '.checks | map(.scenario) | join(",")' "tier1-build-and-unit-tests,canonical-zone-harness,actor-events-runtime"
}

test_current_afk_command_rejects_arguments() {
  local status output
  capture_run status output "$repo_root/scripts/validate-afk" unexpected
  [[ "$status" -eq 2 ]] || return 1
  assert_contains "$output" "takes no arguments"
}

test_current_afk_command_returns_nonzero_for_failure_and_missing_stack() {
  local source evidence status output stack head
  make_afk_contract_repo source current-command-failure
  head="$(git -C "$source" rev-parse HEAD)"
  stack="$tmp_root/operator-home/Projects/bump-eqemu/bump-akk-stack-validation"

  evidence="$tmp_root/current-afk-failed-evidence"
  capture_run status output env HOME="$tmp_root/operator-home" \
    VALIDATION_WORKER_HOME="$tmp_root/worker-home-current-failed" \
    VALIDATION_WORKER_VALIDATE_DRY_RUN=1 \
    VALIDATION_WORKER_AFK_MODE=1 \
    VALIDATION_WORKER_TEST_FAIL_TIER1=1 \
    VALIDATION_AFK_EVIDENCE_DIR="$evidence" \
    "$source/scripts/validate-afk"
  [[ "$status" -eq 1 ]] || return 1
  assert_json_equals "$evidence/result.json" .category validation_failed
  assert_json_equals "$evidence/result.json" '.checks[0].log_path' logs/tier1-build-and-unit-tests.log

  evidence="$tmp_root/current-afk-timeout-evidence"
  capture_run status output env HOME="$tmp_root/operator-home" \
    VALIDATION_WORKER_HOME="$tmp_root/worker-home-current-timeout" \
    VALIDATION_WORKER_VALIDATE_DRY_RUN=1 \
    VALIDATION_WORKER_AFK_MODE=1 \
    VALIDATION_WORKER_TEST_TIER1_EXIT_CODE=124 \
    VALIDATION_AFK_EVIDENCE_DIR="$evidence" \
    "$source/scripts/validate-afk"
  [[ "$status" -eq 1 ]] || return 1
  assert_json_equals "$evidence/result.json" .category timeout
  assert_json_equals "$evidence/result.json" '.checks[0].log_path' logs/tier1-build-and-unit-tests.log

  rm -rf "$stack"
  evidence="$tmp_root/current-afk-missing-stack-evidence"
  capture_run status output env HOME="$tmp_root/operator-home" \
    VALIDATION_WORKER_HOME="$tmp_root/worker-home-current-missing" \
    VALIDATION_WORKER_VALIDATE_DRY_RUN=1 \
    VALIDATION_AFK_EVIDENCE_DIR="$evidence" \
    "$source/scripts/validate-afk"
  [[ "$status" -ne 0 ]] || return 1
  assert_json_equals "$evidence/result.json" .category invalid_request
  assert_json_equals "$evidence/result.json" '.checks | map(.scenario) | join(",")' "tier1-build-and-unit-tests,canonical-zone-harness,actor-events-runtime"
  assert_json_equals "$evidence/result.json" '.checks | map(.status) | join(",")' "not_run,not_run,not_run"
  assert_json_equals "$evidence/result.json" '.checks | map(.candidate_commit) | unique | join(",")' "$head"
  [[ -f "$evidence/afk-checks.json" ]] || return 1
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

  capture_run status output env HOME="$tmp_root/operator-home" VALIDATION_WORKER_HOME="$tmp_root/worker-home" VALIDATION_WORKER_VALIDATE_DRY_RUN=1 "$source/scripts/validation-worker.sh" run --request "$request"

  [[ "$status" -eq 0 ]] || return 1
  assert_json_equals "$evidence/result.json" .schema_version 1
  assert_json_equals "$evidence/result.json" .candidate_sha "$head"
  assert_json_equals "$evidence/result.json" .status passed
  assert_json_equals "$evidence/result.json" '.checks | map(.name) | join(",")' "tier1-build-and-unit-tests,tier3-zone-harness,actor-queue-runtime"
  assert_json_equals "$evidence/result.json" '.checks | map(.status) | join(",")' "passed,passed,passed"
}

test_afk_contract_binds_tier1_tier3_evidence_to_the_candidate() {
  local source request evidence status output head
  make_afk_contract_repo source afk-tier1-tier3
  head="$(git -C "$source" rev-parse HEAD)"
  request="$tmp_root/afk-tier1-tier3.json"
  evidence="$tmp_root/afk-tier1-tier3"
  mkdir "$evidence"
  write_afk_request "$request" "$head" "$evidence" afk-tier1-tier3

  capture_run status output env HOME="$tmp_root/operator-home" VALIDATION_WORKER_HOME="$tmp_root/worker-home-afk-tier1-tier3" VALIDATION_WORKER_VALIDATE_DRY_RUN=1 "$source/scripts/validation-worker.sh" run --request "$request"

  [[ "$status" -eq 0 ]] || return 1
  assert_json_equals "$evidence/worker-request.json" .profile tier1-tier3-harness
  assert_json_equals "$evidence/worker/result.json" .profile tier1-tier3-harness
  assert_json_equals "$evidence/worker/result.json" .actual_checkout_commit "$head"
  assert_json_equals "$evidence/result.json" .candidate_sha "$head"
  assert_json_equals "$evidence/result.json" '.checks | map(.name) | join(",")' "tier1-build-and-unit-tests,tier3-zone-harness,actor-queue-runtime"
  assert_json_equals "$evidence/result.json" '.checks | map(.status) | join(",")' "passed,passed,passed"
  [[ -f "$evidence/worker/logs/tier1-build-and-unit-tests.log" ]] || return 1
  [[ -f "$evidence/worker/logs/tier3-zone-harness.log" ]] || return 1
  [[ -f "$evidence/worker/logs/actor-queue-runtime.log" ]] || return 1
}

test_afk_contract_is_independent_of_the_trusted_harness_location() {
  local source canonical_repo candidate_checkout harness_root operator_home stack request evidence status output head worker_request
  make_afk_contract_repo source unrelated-harness
  reset_worker_home
  mkdir -p "$source/scripts/lib"
  cp "$repo_root/scripts/check-akkstack-contract.sh" "$source/scripts/check-akkstack-contract.sh"
  cp "$repo_root/scripts/lib/akkstack-routing.sh" "$source/scripts/lib/akkstack-routing.sh"
  git -C "$source" add scripts/check-akkstack-contract.sh scripts/lib/akkstack-routing.sh
  git -C "$source" commit -m 'add trusted validation closure' >/dev/null 2>&1
  operator_home="$tmp_root/operator-home"
  canonical_repo="$operator_home/Projects/bump-eqemu/bump-EQEmu"
  candidate_checkout="$tmp_root/unpushed-candidate"
  configure_afk_host "$source"
  git -C "$canonical_repo" config user.email worker-test@example.com
  git -C "$canonical_repo" config user.name 'Worker Test'
  git -C "$canonical_repo" worktree add -b afk-unpushed-candidate "$candidate_checkout" >/dev/null 2>&1
  printf 'unpushed candidate\n' >"$candidate_checkout/candidate-marker.txt"
  git -C "$candidate_checkout" add candidate-marker.txt
  git -C "$candidate_checkout" commit -m 'add unpushed candidate marker' >/dev/null 2>&1
  head="$(git -C "$candidate_checkout" rev-parse HEAD)"
  if git -C "$source" cat-file -e "$head^{commit}" 2>/dev/null; then
    printf 'Candidate fixture was unexpectedly available from its source remote\n' >&2
    return 1
  fi
  harness_root="$tmp_root/materialized/trusted-harness"
  stack="$operator_home/Projects/bump-eqemu/bump-akk-stack-validation"
  request="$tmp_root/afk-unrelated-harness.json"
  evidence="$tmp_root/afk-unrelated-harness"
  worker_request="$evidence/worker-request.json"
  mkdir -p "$harness_root/scripts/lib" "$stack" "$evidence"
  cp "$candidate_checkout/scripts/validation-worker.sh" "$harness_root/scripts/validation-worker.sh"
  cp "$candidate_checkout/scripts/validate.sh" "$harness_root/scripts/validate.sh"
  cp "$candidate_checkout/scripts/check-akkstack-contract.sh" "$harness_root/scripts/check-akkstack-contract.sh"
  cp "$candidate_checkout/scripts/lib/akkstack-routing.sh" "$harness_root/scripts/lib/akkstack-routing.sh"
  chmod +x "$harness_root/scripts/validation-worker.sh"
  write_afk_request "$request" "$head" "$evidence"

  capture_run status output env HOME="$operator_home" VALIDATION_WORKER_HOME="$tmp_root/worker-home" VALIDATION_WORKER_VALIDATE_DRY_RUN=1 \
    "$harness_root/scripts/validation-worker.sh" run --request "$request"

  [[ "$status" -eq 0 ]] || return 1
  assert_json_equals "$worker_request" .repo "$canonical_repo"
  assert_json_equals "$worker_request" .ref "$head"
  assert_json_equals "$worker_request" .commit "$head"
  assert_json_equals "$worker_request" .profile tier1-tier3-harness
  assert_json_equals "$worker_request" .stack.role validation
  assert_json_equals "$worker_request" .stack.path "$stack"
  if grep -Fq "$candidate_checkout" "$worker_request" || grep -Fq "$harness_root" "$worker_request"; then
    printf 'nested request exposed the Candidate checkout or trusted-harness path\n' >&2
    return 1
  fi
  assert_json_equals "$evidence/result.json" '.checks | map(.name) | join(",")' "tier1-build-and-unit-tests,tier3-zone-harness,actor-queue-runtime"
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

  capture_run status output env HOME="$tmp_root/operator-home" VALIDATION_WORKER_HOME="$tmp_root/worker-home" VALIDATION_WORKER_VALIDATE_DRY_RUN=1 VALIDATION_WORKER_TEST_FAIL_TIER1=1 "$source/scripts/validation-worker.sh" run --request "$request"

  [[ "$status" -eq 1 ]] || return 1
  assert_json_equals "$evidence/result.json" .status rejected
  assert_json_equals "$evidence/result.json" '.checks | map(.status) | join(",")' "rejected,not_run,not_run"
}

test_afk_contract_reports_missing_prerequisite_as_inconclusive() {
  local source request evidence status output head
  make_afk_contract_repo source
  reset_worker_home
  head="$(git -C "$source" rev-parse HEAD)"
  request="$tmp_root/afk-inconclusive.json"
  evidence="$tmp_root/afk-inconclusive"
  rm -rf "$tmp_root/operator-home/Projects/bump-eqemu/bump-akk-stack-validation"
  mkdir "$evidence"
  write_afk_request "$request" "$head" "$evidence"

  capture_run status output env HOME="$tmp_root/operator-home" VALIDATION_WORKER_HOME="$tmp_root/worker-home" VALIDATION_WORKER_VALIDATE_DRY_RUN=1 "$source/scripts/validation-worker.sh" run --request "$request"

  [[ "$status" -eq 2 ]] || return 1
  assert_json_equals "$evidence/result.json" .status inconclusive
  assert_json_equals "$evidence/result.json" '.checks | map(.status) | join(",")' "not_run,not_run,not_run"
  assert_json_equals "$evidence/result.json" '.checks | map(.candidate_commit) | unique | join(",")' "$head"
}

test_afk_contract_maps_timeout_and_missing_command_to_inconclusive() {
  local exit_code source request evidence status output head
  for exit_code in 124 127; do
    make_afk_contract_repo source "tier1-exit-$exit_code"
    head="$(git -C "$source" rev-parse HEAD)"
    request="$tmp_root/afk-tier1-exit-$exit_code.json"
    evidence="$tmp_root/afk-tier1-exit-$exit_code"
    mkdir "$evidence"
    write_afk_request "$request" "$head" "$evidence" "afk-tier1-exit-$exit_code"

    capture_run status output env HOME="$tmp_root/operator-home" VALIDATION_WORKER_HOME="$tmp_root/worker-home-tier1-exit-$exit_code" VALIDATION_WORKER_VALIDATE_DRY_RUN=1 VALIDATION_WORKER_TEST_TIER1_EXIT_CODE="$exit_code" "$source/scripts/validation-worker.sh" run --request "$request"

    [[ "$status" -eq 2 ]] || return 1
    assert_json_equals "$evidence/result.json" .status inconclusive
    assert_json_equals "$evidence/result.json" '.checks | map(.status) | join(",")' "inconclusive,not_run,not_run"
  done
}

test_afk_contract_maps_timeout_infrastructure_failure_to_inconclusive() {
  local source request evidence status output head
  make_afk_contract_repo source "tier1-exit-125"
  head="$(git -C "$source" rev-parse HEAD)"
  request="$tmp_root/afk-tier1-exit-125.json"
  evidence="$tmp_root/afk-tier1-exit-125"
  mkdir "$evidence"
  write_afk_request "$request" "$head" "$evidence" "afk-tier1-exit-125"

  capture_run status output env HOME="$tmp_root/operator-home" VALIDATION_WORKER_HOME="$tmp_root/worker-home-tier1-exit-125" VALIDATION_WORKER_VALIDATE_DRY_RUN=1 VALIDATION_WORKER_TEST_TIER1_EXIT_CODE=125 "$source/scripts/validation-worker.sh" run --request "$request"

  [[ "$status" -eq 2 ]] || return 1
  assert_json_equals "$evidence/result.json" .status inconclusive
  assert_json_equals "$evidence/result.json" '.checks | map(.status) | join(",")' "inconclusive,not_run,not_run"
}

test_afk_contract_fetches_a_self_contained_checkout_from_a_linked_worktree() {
  local source linked_root linked_worktree request evidence status output head
  make_source_repo_with_submodule source afk-linked
  cp "$repo_root/scripts/validation-worker.sh" "$source/scripts/validation-worker.sh"
  chmod +x "$source/scripts/validation-worker.sh"
  git -C "$source" add scripts/validation-worker.sh
  git -C "$source" commit -m 'add validation worker' >/dev/null 2>&1
  linked_root="$tmp_root/linked-root"
  linked_worktree="$linked_root/candidate"
  mkdir -p "$linked_root"
  git -C "$source" worktree add -b afk-linked-candidate "$linked_worktree" >/dev/null 2>&1
  git -C "$linked_worktree" -c protocol.file.allow=always submodule update --init --recursive >/dev/null 2>&1
  configure_afk_host "$source"
  head="$(git -C "$linked_worktree" rev-parse HEAD)"
  request="$tmp_root/afk-linked-worktree.json"
  evidence="$tmp_root/afk-linked-worktree"
  mkdir "$evidence"
  write_afk_request "$request" "$head" "$evidence"

  capture_run status output env HOME="$tmp_root/operator-home" VALIDATION_WORKER_HOME="$tmp_root/worker-home-linked" VALIDATION_WORKER_VALIDATE_DRY_RUN=1 VALIDATION_WORKER_TEST_FILE_SUBMODULE_ROOT="$tmp_root" VALIDATION_WORKER_TEST_ASSERT_SUBMODULE=1 VALIDATION_WORKER_TEST_ASSERT_SELF_CONTAINED_GIT=1 "$linked_worktree/scripts/validation-worker.sh" run --request "$request"

  [[ "$status" -eq 0 ]] || return 1
  assert_json_equals "$evidence/result.json" .status passed
  assert_json_equals "$evidence/worker/result.json" .request_metadata.source.type fetch
  assert_json_equals "$evidence/worker/result.json" .actual_checkout_commit "$head"
}

test_afk_contract_treats_nonzero_inner_exit_after_passed_checks_as_inconclusive() {
  local source request evidence status output head fake_bin real_rm
  make_afk_contract_repo source
  reset_worker_home
  head="$(git -C "$source" rev-parse HEAD)"
  request="$tmp_root/afk-cleanup-failure.json"
  evidence="$tmp_root/afk-cleanup-failure"
  fake_bin="$tmp_root/afk-cleanup-failure-bin"
  real_rm="$(command -v rm)"
  mkdir "$evidence" "$fake_bin"
  write_afk_request "$request" "$head" "$evidence"
  cat >"$fake_bin/rm" <<SCRIPT
#!/usr/bin/env bash
if [[ " \$* " == *".validation-worker-code.lock"* ]]; then
  exit 1
fi
exec "$real_rm" "\$@"
SCRIPT
  chmod +x "$fake_bin/rm"

  capture_run status output env HOME="$tmp_root/operator-home" PATH="$fake_bin:$PATH" VALIDATION_WORKER_HOME="$tmp_root/worker-home" VALIDATION_WORKER_VALIDATE_DRY_RUN=1 "$source/scripts/validation-worker.sh" run --request "$request"

  [[ "$status" -eq 2 ]] || return 1
  assert_json_equals "$evidence/worker/afk-checks.json" .status inconclusive
  assert_json_equals "$evidence/worker/result.json" .category cleanup_failed
  assert_json_equals "$evidence/result.json" .status inconclusive
  assert_json_equals "$evidence/result.json" '.checks | map(.status) | join(",")' "passed,passed,inconclusive"
}

test_afk_contract_rejects_unapproved_submodule_transports_before_initialization() {
  local kind source target url head request evidence status output fake_bin marker target_head
  for kind in absolute file ext; do
    make_afk_contract_repo source "transport-$kind"
    target="$tmp_root/transport-target-$kind"
    mkdir "$target"
    git -C "$target" init >/dev/null 2>&1
    git -C "$target" config user.email worker-test@example.com
    git -C "$target" config user.name 'Worker Test'
    printf 'host data\n' >"$target/host-data"
    git -C "$target" add host-data
    git -C "$target" commit -m 'host data' >/dev/null 2>&1
    target_head="$(git -C "$target" rev-parse HEAD)"
    case "$kind" in
      absolute) url="$target" ;;
      file) url="file://$target" ;;
      ext) url="ext::sh -c 'exit 0'" ;;
    esac
    add_submodule_gitlink "$source" vendor/candidate-controlled "$url" "$target_head"
    configure_afk_host "$source"
    head="$(git -C "$source" rev-parse HEAD)"
    request="$tmp_root/afk-transport-$kind.json"
    evidence="$tmp_root/afk-transport-$kind"
    fake_bin="$tmp_root/afk-transport-$kind-bin"
    marker="$tmp_root/afk-transport-$kind-submodule-update"
    mkdir "$evidence"
    write_afk_request "$request" "$head" "$evidence" "afk-transport-$kind"
    make_submodule_update_observer "$fake_bin" "$marker"

    capture_run status output env HOME="$tmp_root/operator-home" PATH="$fake_bin:$PATH" VALIDATION_WORKER_HOME="$tmp_root/worker-home-transport-$kind" VALIDATION_WORKER_VALIDATE_DRY_RUN=1 "$source/scripts/validation-worker.sh" run --request "$request"

    [[ "$status" -eq 2 ]] || return 1
    [[ ! -e "$marker" ]] || return 1
    [[ ! -e "$tmp_root/worker-home-transport-$kind/checkouts/afk-${head:0:16}/vendor/candidate-controlled/.git" ]] || return 1
    [[ ! -e "$tmp_root/worker-home-transport-$kind/checkouts/afk-${head:0:16}/vendor/candidate-controlled/host-data" ]] || return 1
    assert_json_equals "$evidence/worker/result.json" .category submodule_failed
  done
}

test_afk_contract_accepts_the_approved_production_submodule_urls() {
  local source request evidence status output head fake_bin marker fake_commit
  make_afk_contract_repo source approved-submodules
  fake_commit="$(git -C "$source" rev-parse HEAD)"
  cat >"$source/.gitmodules" <<'EOF'
[submodule "submodules/websocketpp"]
	path = submodules/websocketpp
	url = https://github.com/zaphoyd/websocketpp.git
[submodule "submodules/vcpkg"]
	path = submodules/vcpkg
	url = https://github.com/microsoft/vcpkg.git
EOF
  git -C "$source" update-index --add --cacheinfo "160000,$fake_commit,submodules/websocketpp"
  git -C "$source" update-index --add --cacheinfo "160000,$fake_commit,submodules/vcpkg"
  git -C "$source" add .gitmodules
  git -C "$source" commit -m 'add approved submodule config' >/dev/null 2>&1
  configure_afk_host "$source"
  head="$(git -C "$source" rev-parse HEAD)"
  request="$tmp_root/afk-approved-submodules.json"
  evidence="$tmp_root/afk-approved-submodules"
  fake_bin="$tmp_root/afk-approved-submodules-bin"
  marker="$tmp_root/afk-approved-submodules-update"
  rm -rf "$tmp_root/operator-home/Projects/bump-eqemu/bump-akk-stack-validation/.validation-worker-code.lock"
  mkdir "$evidence"
  write_afk_request "$request" "$head" "$evidence"
  make_submodule_update_observer "$fake_bin" "$marker" 1

  capture_run status output env HOME="$tmp_root/operator-home" PATH="$fake_bin:$PATH" VALIDATION_WORKER_HOME="$tmp_root/worker-home-approved" VALIDATION_WORKER_VALIDATE_DRY_RUN=1 "$source/scripts/validation-worker.sh" run --request "$request"

  if [[ "$status" -ne 0 ]]; then
    cat "$evidence/worker/result.json" >&2
    cat "$evidence/worker/logs/submodule.log" >&2
    return 1
  fi
  [[ -e "$marker" ]] || return 1
  assert_json_equals "$evidence/result.json" .status passed
}

test_afk_contract_ignores_gitmodules_outside_initialized_submodules() {
  local source request evidence status output head
  make_afk_contract_repo source fixture-gitmodules
  mkdir -p "$source/tests/fixtures"
  cat >"$source/tests/fixtures/.gitmodules" <<'EOF'
[submodule "not-a-real-submodule"]
	path = escaped
	url = ext::sh -c 'exit 1'
EOF
  git -C "$source" add tests/fixtures/.gitmodules
  git -C "$source" commit -m 'add inert fixture gitmodules' >/dev/null 2>&1
  configure_afk_host "$source"
  head="$(git -C "$source" rev-parse HEAD)"
  request="$tmp_root/afk-fixture-gitmodules.json"
  evidence="$tmp_root/afk-fixture-gitmodules"
  mkdir "$evidence"
  write_afk_request "$request" "$head" "$evidence"

  capture_run status output env HOME="$tmp_root/operator-home" VALIDATION_WORKER_HOME="$tmp_root/worker-home-fixture-gitmodules" VALIDATION_WORKER_VALIDATE_DRY_RUN=1 "$source/scripts/validation-worker.sh" run --request "$request"

  [[ "$status" -eq 0 ]] || return 1
  assert_json_equals "$evidence/result.json" .status passed
}

test_afk_contract_ignores_nested_config_in_ordinary_declared_directory() {
  local source request evidence status output head
  make_afk_contract_repo source ordinary-declared-directory
  mkdir -p "$source/submodules/vcpkg"
  cat >"$source/.gitmodules" <<'EOF'
[submodule "submodules/vcpkg"]
	path = submodules/vcpkg
	url = https://github.com/microsoft/vcpkg.git
EOF
  cat >"$source/submodules/vcpkg/.gitmodules" <<'EOF'
[submodule "hostile-fixture"]
	path = escaped
	url = ext::sh -c 'exit 1'
EOF
  git -C "$source" add .gitmodules submodules/vcpkg/.gitmodules
  git -C "$source" commit -m 'add ordinary declared directory fixture' >/dev/null 2>&1
  configure_afk_host "$source"
  head="$(git -C "$source" rev-parse HEAD)"
  request="$tmp_root/afk-ordinary-declared-directory.json"
  evidence="$tmp_root/afk-ordinary-declared-directory"
  mkdir "$evidence"
  write_afk_request "$request" "$head" "$evidence"

  capture_run status output env HOME="$tmp_root/operator-home" VALIDATION_WORKER_HOME="$tmp_root/worker-home-ordinary-declared-directory" VALIDATION_WORKER_VALIDATE_DRY_RUN=1 "$source/scripts/validation-worker.sh" run --request "$request"

  [[ "$status" -eq 0 ]] || return 1
  assert_json_equals "$evidence/result.json" .status passed
}

run_test "validation worker help mentions request contract" test_help
run_test "AFK contract config selects the validation worker" test_afk_contract_config
run_test "validation worker profiles discovery emits portable metadata" test_profiles_json
run_test "discovered validation profiles are accepted requests" test_discovered_profiles_are_accepted_requests
run_test "actor queue Tier 3 profile builds before runtime from a clean fetch" test_actor_queue_tier3_profile_builds_before_runtime_from_clean_fetch
run_test "safe profile remains available for non-AFK requests" test_safe_profile_remains_available_for_non_afk_requests
run_test "invalid request writes structured evidence" test_invalid_request_writes_evidence
run_test "fake repo fetch checkout writes evidence" test_fetch_checkout_and_evidence
run_test "fetched checkout initializes submodules before validation" test_fetch_checkout_initializes_submodules_before_validation
run_test "submodule timeout is categorized" test_submodule_timeout_is_categorized
run_test "stack lock is not held during submodule initialization" test_stack_lock_is_not_held_during_submodule_initialization
run_test "commit mismatch is categorized" test_commit_mismatch
run_test "fetch failure is categorized" test_fetch_failure
run_test "local-checkout request still works" test_local_checkout_request_works
run_test "local-checkout request rejects a drifting submodule" test_local_checkout_rejects_drifting_submodule
run_test "lock contention is worker_busy" test_lock_contention
run_test "validation timeout is categorized" test_timeout
run_test "validation timeout kills a TERM-resistant child" test_timeout_kills_a_term_resistant_validation_child
run_test "tier3 harness failure is categorized with logs" test_tier3_harness_failure_is_categorized_with_logs
run_test "combined AFK profile runs Tier 1, canonical harness, and actor queue" test_tier1_tier3_harness_profile_runs_tier1_before_tier3
run_test "combined AFK profile rejects zero-exit actor queue without completion proof" test_combined_profile_rejects_a_zero_exit_without_runtime_proof
run_test "combined AFK profile propagates actor queue failure" test_combined_profile_propagates_actor_queue_failure
run_test "tier1 plus tier3 harness profile uses one timeout budget" test_tier1_tier3_harness_profile_uses_one_timeout_budget
run_test "tier1 plus tier3 harness profile stops after tier1 failure and releases lock" test_tier1_tier3_harness_profile_stops_after_tier1_failure_and_releases_lock
run_test "validation worker binds requested validation stack to worker checkout" test_validation_worker_binds_requested_validation_stack_to_worker_checkout
run_test "validation worker binds validation stack from AKKSTACK_DIR" test_validation_worker_binds_stack_from_akkstack_dir_environment
run_test "stack lock blocks distinct worker homes on same stack" test_stack_lock_blocks_distinct_worker_homes_on_same_stack
run_test "worker termination kills a checkout descendant after its leader exits" test_worker_termination_kills_checkout_deletion_descendant_after_leader_exit
run_test "worker termination during fetch is bounded" test_worker_termination_during_fetch_is_bounded
run_test "worker termination during submodule preparation is bounded" test_worker_termination_during_submodule_preparation_is_bounded
run_test "failed worker termination retains stack and locks" test_worker_failed_termination_retains_stack_and_locks
run_test "worker termination restores stack and releases locks" test_worker_termination_restores_stack_and_releases_locks
run_test "current AFK wrapper forwards termination and retains evidence" test_current_afk_wrapper_forwards_termination_and_retains_evidence
run_test "AKKSTACK_DIR real code directory fails fast" test_akkstack_dir_real_code_directory_fails_fast
run_test "current AFK command validates exact HEAD without arguments" test_current_afk_command_validates_exact_head_without_arguments
run_test "current AFK command rejects arguments" test_current_afk_command_rejects_arguments
run_test "current AFK command returns nonzero for failure and missing stack" test_current_afk_command_returns_nonzero_for_failure_and_missing_stack
run_test "AFK contract reports stable passing checks" test_afk_contract_passes_with_stable_checks
run_test "AFK contract binds Tier 1 and Tier 3 evidence to the Candidate" test_afk_contract_binds_tier1_tier3_evidence_to_the_candidate
run_test "AFK contract is independent of the trusted harness location" test_afk_contract_is_independent_of_the_trusted_harness_location
run_test "AFK contract rejects Tier 1 and stops" test_afk_contract_rejects_tier1_and_stops
run_test "AFK contract reports a missing prerequisite as inconclusive" test_afk_contract_reports_missing_prerequisite_as_inconclusive
run_test "AFK contract maps timeout and missing command to inconclusive" test_afk_contract_maps_timeout_and_missing_command_to_inconclusive
run_test "AFK contract maps timeout infrastructure failure to inconclusive" test_afk_contract_maps_timeout_infrastructure_failure_to_inconclusive
run_test "AFK contract fetches self-contained Git metadata from a linked worktree" test_afk_contract_fetches_a_self_contained_checkout_from_a_linked_worktree
run_test "AFK contract rejects passed checks paired with a nonzero worker exit" test_afk_contract_treats_nonzero_inner_exit_after_passed_checks_as_inconclusive
run_test "AFK contract rejects unapproved submodule transports before initialization" test_afk_contract_rejects_unapproved_submodule_transports_before_initialization
run_test "AFK contract accepts approved production submodule URLs" test_afk_contract_accepts_the_approved_production_submodule_urls
run_test "AFK contract ignores inert fixture gitmodules" test_afk_contract_ignores_gitmodules_outside_initialized_submodules
run_test "AFK contract ignores nested config in an ordinary declared directory" test_afk_contract_ignores_nested_config_in_ordinary_declared_directory

if [[ -n "$test_filter" && "$tests_run" -eq 0 ]]; then
  printf 'unknown test: %s\n' "$test_filter" >&2
  exit 2
fi
if [[ "$failures" -gt 0 ]]; then
  exit 1
fi

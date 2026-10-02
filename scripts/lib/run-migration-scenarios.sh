#!/usr/bin/env bash
# Require the durable actor proof from this invocation, including all exit codes.
set -euo pipefail
log="$(mktemp)"
trap 'rm -f -- "$log"' EXIT
while IFS= read -r -d '' command; do
  bash -lc "$command" | tee -a "$log"
done < <(jq -j '.[] + "\u0000"' <<<"$MIGRATION_SCENARIOS_JSON")
if ! grep -Fxq '[PASS] actor-events-runtime' "$log"; then
  printf 'error: required actor-events-runtime completion evidence missing\n' >&2
  exit 1
fi

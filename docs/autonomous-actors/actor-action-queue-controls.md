# Actor Action Queue Controls

Date: 2026-06-27

Beads item: `central-lhy.13`

## Purpose

`actor_action_queue` is the durable handoff point for low-cadence **Actor Action** requests that arrive outside the
owning zone process. It is intentionally a bounded queue for fresh intent, not an unbounded action history.

## Stale Intent Controls

- Every queued action should carry an `expires_at` when the request depends on current gameplay state.
- Claim paths must ignore rows whose `expires_at` is already in the past, even before a sweeper marks them `expired`.
- `ExpireDue()` should regularly convert stale `pending` or `claimed` rows into `expired` so operators can see that the
  action aged out instead of silently disappearing.
- `not_before` should only delay a request for a short, concrete reason such as waiting for a cast window or a follow-up
  observation. It should not be used as a long-lived parking lot for speculative plans.

## Queue Buildup Controls

- Use one idempotency key per logical planner decision window so retries return the existing row instead of enqueuing
  duplicates.
- Keep `action_json`, `source_metadata_json`, and `result_json` bounded. Large snapshots belong in derived state or
  event history, not in the queue row.
- Prefer small gameplay-native `action_type` values with compact payloads. If a request needs many steps, queue the next
  bounded step after the prior step produces an **Actor Event**.
- Zone claimers should only claim rows they can actively own and execute soon. Leaving broad worker pools to claim
  speculative rows increases retry churn and stale backlog.
- Terminal states (`completed`, `failed`, `expired`) are audit rows, not work items. Operational cleanup or retention
  policy can remove old terminal rows later without changing claim semantics.
- The first claim slice uses a portable single-row `UPDATE ... ORDER BY ... LIMIT 1` path. Under concurrent claimers it
  may briefly block and lose a turn instead of skipping to another eligible row; `central-lhy.14` can revisit that once
  execution ownership and DB compatibility are advanced together.

## Current Persistence Semantics

- `Enqueue()` inserts `pending` rows and returns the existing row when the `(actor_id, idempotency_key)` guard already
  exists.
- `ClaimNextPending()` claims at most one due, non-expired row and stamps `claimed_by` plus `claimed_at`.
- `MarkCompleted()` and `MarkFailed()` only succeed from fresh `claimed` rows; if the claim has already passed
  `expires_at`, the attempted terminal transition atomically converts the row to `expired` instead and returns that
  expired terminal row to the caller.
- `ExpireDue()` only affects `pending` or `claimed` rows whose `expires_at` has passed.

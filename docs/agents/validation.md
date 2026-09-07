# Validation for pipeline agents

AFK owns the work loop. Use the repository's checks and report their evidence; do not add a second review/repair orchestrator. The command and profile authority is [testing/process.md](../testing/process.md) and `scripts/validation-worker.sh`.

## Required proof

- Run `./scripts/validate-afk` from the committed Candidate. Checkout preparation, Tier 1, the canonical Zone Harness, and the durable actor queue scenario run against the separate validation stack under one shared timeout budget and lock. Uncommitted diagnostics are not final Candidate evidence.
- The combined `tier1-tier3-harness` profile is the conservative AFK gate because the entry point has no trusted change classification. `actor-queue-tier3` remains available for targeted diagnostics; it is not an additional AFK requirement.
- Each new world behavior ships with a registered automated scenario. Fixtures may create parties, NPCs and synthetic players, but the behavior under test must execute through production intent and ordinary gameplay. Observe outcomes; do not directly manufacture success.
- Prove bounded success and relevant rejection, timeout, interruption or restart cases. Clean up scenario-owned state on success and failure. The AFK entry point forwards INT/TERM to the worker; fetch, checkout, submodule, and validation children are tracked in dedicated process groups, so timeout and interruption escalate to KILL after a bounded grace period. Once the command tree is proven dead, the worker restores the stack and releases both locks. If descendants remain observable after the final KILL grace period, the worker instead records `child_termination_failed` and deliberately retains the stack binding and both locks to prevent another request from racing those descendants; validation remains unavailable until an operator confirms they have exited and safely restores the binding and removes the locks. Retain scenario identity, assertions and failure diagnostics with the profile, status and exact Candidate commit. Interrupted runs record the active check as `inconclusive`, and cleanup failures replace any earlier passing verdict with `cleanup_failed` evidence. The combined gate registers every required check before request validation, checkout, or lock/binding work; a pre-dispatch failure therefore retains the full check list with `not_run` statuses and the requested Candidate commit instead of omitting scenario evidence.
- If required controls, observations or validation profiles are missing, extend them in scope or report a concrete prerequisite. Never replace required proof with a manual-only checklist or a skipped scenario.

## Environments and scope

Automated checks use `../bump-akk-stack-validation`. Its database is currently persistent even when the server process is ephemeral. Respect worker locks and fixture cleanup. Read [database instructions](database.md) before schema or saved-data work.

The current Zone Harness controls one booted zone. Cross-zone behavior requires a future multi-process proof; do not claim existing single-zone coverage establishes handoff. Synthetic player inputs should cover server-observable contention and visibility where supported. Actual client rendering, protocol behavior not represented by the harness, and feel may still need gameplay-stack observation.

Keep feel checks low friction: ordinary play and a simple enable/disable path. They do not gate deterministic proofs on elaborate manual setup. Run only checks appropriate to the task in addition to the pipeline's required gate; docs-only edits need link/consistency checks outside an AFK run, not an ad hoc runtime deployment.

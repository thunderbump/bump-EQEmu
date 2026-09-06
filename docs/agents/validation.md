# Validation for pipeline agents

AFK owns the work loop. Use the repository's checks and report their evidence; do not add a second review/repair orchestrator. The command and profile authority is [testing/process.md](../testing/process.md) and `scripts/validation-worker.sh`.

## Required proof

- Run `./scripts/validate-afk` from the committed Candidate. It currently runs Tier 1 plus canonical Tier 3 against the separate validation stack. Uncommitted diagnostics are not final Candidate evidence.
- The durable actor queue proof is currently a separate `actor-queue-tier3` worker profile. Actor queue/helper changes require it in addition to the default check until the combined gate lands. Use the worker request contract in the testing process; do not claim that a default pass ran this profile.
- Each new world behavior ships with a registered automated scenario. Fixtures may create parties, NPCs and synthetic players, but the behavior under test must execute through production intent and ordinary gameplay. Observe outcomes; do not directly manufacture success.
- Prove bounded success and relevant rejection, timeout, interruption or restart cases. Clean up scenario-owned state on success and failure. Retain scenario identity, assertions and failure diagnostics with the profile, status and exact Candidate commit.
- If required controls, observations or validation profiles are missing, extend them in scope or report a concrete prerequisite. Never replace required proof with a manual-only checklist or a skipped scenario.

## Environments and scope

Automated checks use `../bump-akk-stack-validation`. Its database is currently persistent even when the server process is ephemeral. Respect worker locks and fixture cleanup. Read [database instructions](database.md) before schema or saved-data work.

The current Zone Harness controls one booted zone. Cross-zone behavior requires a future multi-process proof; do not claim existing single-zone coverage establishes handoff. Synthetic player inputs should cover server-observable contention and visibility where supported. Actual client rendering, protocol behavior not represented by the harness, and feel may still need gameplay-stack observation.

Keep feel checks low friction: ordinary play and a simple enable/disable path. They do not gate deterministic proofs on elaborate manual setup. Run only checks appropriate to the task in addition to the pipeline's required gate; docs-only edits need link/consistency checks outside an AFK run, not an ad hoc runtime deployment.

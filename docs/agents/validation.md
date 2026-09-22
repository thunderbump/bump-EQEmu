# Validation for pipeline agents

AFK owns the work loop. Use the repository's checks and report their evidence; do not add a second review/repair orchestrator. The command and profile authority is [testing/process.md](../testing/process.md) and `scripts/validation-worker.sh`.

## Required proof

- Run `./scripts/validate-afk` from the committed Candidate. It currently runs Tier 1, the isolated migration rehearsal, and canonical Tier 3 using the validation stack for Tier 1/Tier 3 and a separate disposable database environment for migration rehearsal. Select a checksummed captured baseline once with the worker's `migration-rehearsal-baseline.json` configuration. The gate generates Candidate-specific inputs under private run evidence before testing. Missing or invalid preparation is a nonzero failure, never a pass. Uncommitted diagnostics are not final Candidate evidence.
- The default gate runs the durable actor queue proof inside the disposable migration environment. Its scenario commands must exit successfully and emit `[PASS] actor-events-runtime`; the rehearsal result records the scenario and status against the exact Candidate. Do not run the separate `actor-queue-tier3` profile as an additional AFK requirement. That legacy manual profile still targets the persistent validation database and does not gain the disposable gate's termination guarantees.
- Each new world behavior ships with a registered automated scenario. Fixtures may create parties, NPCs and synthetic players, but the behavior under test must execute through production intent and ordinary gameplay. Observe outcomes; do not directly manufacture success.
- Prove bounded success and relevant rejection, timeout, interruption or restart cases. Clean up scenario-owned state on success and failure. Retain scenario identity, assertions and failure diagnostics with the profile, status and exact Candidate commit.
- If required controls, observations or validation profiles are missing, extend them in scope or report a concrete prerequisite. Never replace required proof with a manual-only checklist or a skipped scenario.

## Environments and scope

Automated checks use `../bump-akk-stack-validation`. Its database is currently persistent even when the server process is ephemeral. Respect worker locks and fixture cleanup. Read [database instructions](database.md) before schema or saved-data work.

The current Zone Harness controls one booted zone. Cross-zone behavior requires a future multi-process proof; do not claim existing single-zone coverage establishes handoff. Synthetic player inputs should cover server-observable contention and visibility where supported. Actual client rendering, protocol behavior not represented by the harness, and feel may still need gameplay-stack observation.

Keep feel checks low friction: ordinary play and a simple enable/disable path. They do not gate deterministic proofs on elaborate manual setup. Run only checks appropriate to the task in addition to the pipeline's required gate; docs-only edits need link/consistency checks outside an AFK run, not an ad hoc runtime deployment.

## Optional compilation during inference

`./scripts/compile-check zone/actor_action_executor.cpp zone/cli/tests/cli_actor_events.cpp` compiles the current worktree's selected source files, including uncommitted edits. This is a compilation diagnostic, not a fixture run. Agents may invoke it during `attempt` or `respond`; use the failure output to repair compiler errors before returning. AFK still owns commits, publication and full validation.

The command snapshots tracked and nonignored files into disposable storage and initializes submodules at their index revisions. Deleted files remain deleted. Modified submodule worktrees and symlink files are unsupported. It uses the normal `linux-debug` preset and CMake compilation database to select object targets, with four build jobs. It does not infer changed files or header consumers, link the server, run unit tests, migrate a database or run gameplay scenarios. Select every relevant C/C++ translation unit explicitly. New sources absent from the compilation database and unity-build-only sources are inconclusive.

The total work budget is 60 seconds, including snapshot/dependency preparation; `--timeout` may lower it. Cleanup can add time. Each check uses a fresh build and private compiler cache. The locally installed development image defaults to `eqemulator/eqemu-server:v16-dev`, overridable with `EQEMU_COMPILE_IMAGE`. The existing read-only dependency archive volume defaults to `bump-akk-stack-validation_go-build-cache`, overridable with `EQEMU_COMPILE_PACKAGE_CACHE`. Missing image/cache, unavailable Docker or dependency download failures are inconclusive. The command does not pull an image or create shared volumes. Public HTTPS submodule and vcpkg bootstrap access is needed. It uses the development image's normal user; the local environment must allow that user to read/write the private build/evidence directories.

Output is JSON with requested/checked sources, selected targets, source snapshot digest, HEAD, pinned image, submodule revisions, elapsed time and private evidence directory. `result.json` and `compile.log` remain under `$XDG_STATE_HOME/eqemu-compile-check/`, defaulting to `~/.local/state/eqemu-compile-check/`. Scratch source/build files are removed. Exit 0 means only the selected objects compiled; exit 1 means configured object compilation failed; exit 2 means the result is inconclusive. Never treat an inconclusive result or partial compile as passing full validation.

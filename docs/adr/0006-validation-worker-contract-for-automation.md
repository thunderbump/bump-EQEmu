# Validation Worker Contract for Automation

Automated Bump EQEmu validation will use a portable **Validation Worker** contract instead of assuming that the automation process can access a sibling AkkStack path or the same Docker host. A validation request names either a fetchable repository ref/commit or a local diagnostic checkout, a validation profile such as `preflight`, `safe`, or `tier3-harness`, and an evidence directory; the worker fetches the ref into worker-owned storage or validates the local checkout in place, runs the project-specific validation profile against its persistent validation AkkStack/database, writes structured evidence, and returns a normal process exit code.

**Considered Options**

- Keep validation path-based, where automation runs `scripts/validate.sh` directly against a sibling local AkkStack checkout.
- Let the automation container own Docker/AkkStack access directly, including remote or Docker-socket access when the container moves.
- Use a portable worker contract while keeping the first adapter local and CLI-based.

**Consequences**

- The local default validation stack remains useful, but it is an implementation detail of the local worker rather than the automation contract.
- Portable AFK validation requires a pushed or otherwise fetchable Git ref; unpushed worktree state is only valid for local diagnostic paths.
- Fetch requests run `git submodule update --init --recursive` in worker-owned storage before validation. Local diagnostic checkout requests do not mutate the checkout; they instead require submodules to already be initialized and pinned to the recorded commits.
- The automation workflow should compile worker validation into Case's normal `test`/`check` command model instead of bypassing Case's verifier or task schema.
- Worker evidence is mechanical proof for Case to review; the worker does not directly mark Case tasks tested.
- Each worker owns one validation slot at first. Validation profiles take an exclusive worker-local lock with bounded waiting so persistent validation DB and Compose state are not shared by concurrent runs.
- The current AFK Run Preparer invokes the repository's no-argument `scripts/validate-afk` command. It resolves the exact committed Candidate `HEAD` and delegates checkout preparation, validation-stack binding, locking, timeout handling, Tier 1 plus canonical Tier 3 execution, and evidence production to this worker.
- The tracked `afk.toml` remains the legacy request-driven AFK contract; it is not the current no-argument invocation surface.
- Both AFK entry points pin every Candidate to `tier1-tier3-harness` because they have no trusted base commit or change classification. The `safe` profile remains available to non-AFK automation whose scope does not require Tier 3.

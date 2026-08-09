## Agent skills

### Issue tracker

Issues are tracked in the central Beads workspace at `/home/bump/Projects/beads` using `bd <command>` from that workspace with `project:bump-eqemu` labels. Keep implementation scoped to the active Bead. See `docs/agents/issue-tracker.md`.

Beads uses the Dolt SQL password stored at `/home/bump/Projects/beads/secrets/dolt_beads_password.txt`. When `bd` needs database authentication, run it from `/home/bump/Projects/beads` with the password read from that file for the single command invocation:

```sh
cd /home/bump/Projects/beads
BEADS_DOLT_PASSWORD="$(sed -n '1p' secrets/dolt_beads_password.txt)" bd <command>
```

Do not print the secret, paste it into chat, commit it, or export it into a long-lived shell session.

### Triage labels

Use the default Beads triage label vocabulary. See `docs/agents/triage-labels.md`.

### Domain docs

Single-context repo: read root `CONTEXT.md` and `docs/adr/` when present. See `docs/agents/domain.md`.

## Testing

The repository-owned Validation Contract in `docs/testing/process.md` and `scripts/validation-worker.sh` is the
authority for automated validation. Its evidence identifies the profile, status, exact Candidate commit, and
artifacts; external automation must not duplicate repository-specific test semantics.

Automated validation defaults to the separate `../bump-akk-stack-validation` stack and its persistent developer
database. Use `../bump-akk-stack` only for gameplay, client-facing, or other live proof that the contract assigns to
the gameplay stack. Prefer repository wrappers and existing stack commands, and avoid changing either AkkStack
checkout unless the required validation cannot be supported from this repository.

## Agent skills

### Issue tracker

Issues are tracked in the central Beads workspace at `/home/bump/Projects/beads` using `bd <command>` from that workspace with `project:bump-eqemu` labels. See `docs/agents/issue-tracker.md`.

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

Use the tiered validation process in `docs/testing/process.md`. Prefer the persistent local AkkStack dev environment in `../bump-akk-stack`; avoid changing AkkStack unless the test process cannot be supported from this repo and existing stack commands.

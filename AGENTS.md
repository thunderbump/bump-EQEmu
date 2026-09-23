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

Use the relevant terms in `CONTEXT.md` and read only ADRs that affect the active Bead. See `docs/agents/domain.md`.

### Sub-agents

Whenever you use a sub-agent, choose a model you think would be appropriate for the task.

- For implementation try to use gpt-5.4.
- For very simple tasks like small updates or quick code searches use gpt-5.3-codex-spark

## Testing

AFK owns its implementation, review, repair, and publication workflow. This repo owns test semantics and exact-Candidate evidence. The current no-argument repository check is `./scripts/validate-afk`; `afk.toml` is legacy. Do not wrap an AFK run in another implementation/review pipeline.

For implementation, read [validation instructions](docs/agents/validation.md). Every new world behavior needs a bounded automated Zone Harness scenario through production gameplay paths. Manual play judges feel; it does not replace setup-heavy correctness tests.

For schema, migrations, saved-data changes, or database operations, also read [database instructions](docs/agents/database.md). Use the validation stack for automation; gameplay deployment is a separate operation. Prefer repository wrappers and avoid AkkStack edits unless required validation cannot be supported here.

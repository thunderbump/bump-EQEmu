# Issue tracker: Beads

Issues, PRDs, and triage state for this repo live in the central Beads workspace at `/home/bump/Projects/beads`.

Run Beads commands via:

```sh
cd /home/bump/Projects/beads
bd <command>
```

Do not use project-local `.beads/` unless this repo explicitly changes that convention later.

## Project ownership

Use the label `project:bump-eqemu` for work owned by this repo.

## When a skill says "publish to the issue tracker"

Create or update the relevant Beads issue in the central workspace using `bd`.

Apply `project:bump-eqemu` and any relevant triage labels from `docs/agents/triage-labels.md`.

## When a skill says "fetch the relevant ticket"

Read the relevant Beads issue using `bd`. The user may pass an issue id, title, or enough context to search Beads.

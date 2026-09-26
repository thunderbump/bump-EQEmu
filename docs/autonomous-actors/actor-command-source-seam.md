# Actor Command Source Seam

Date: 2026-06-26

Beads item: `central-lhy.10`

## Scope

This change follows `central-lhy.9` by extracting a narrow **ActorCommandSource** seam inside bot AI.
`central-lhy.10` introduced two gameplay intents:

- command target sourcing for the owned-bot attack path; and
- leash anchor sourcing for combat target validation.

`central-lhy.17` extends that same seam one step further for assist intent:

- actor-sourced auto-defend/assist can now resolve from the configured command source before falling back to the
  existing owner/client assist paths.

The seam is runtime-only. It stores ephemeral entity IDs on each `Bot` and resolves them through ordinary zone
entity lookup. When no actor-specific source is configured, the bot still defaults to the current owner-client
behavior. Existing entity lookup and removal cleanup also cover dead, removed, and out-of-group command sources; this
PR only adds happy-path owner/actor assist proof and does not add new negative harness coverage for those edge cases.

## What Stays The Same

- Bot ownership, persistence, and authority remain owner-client-shaped.
- The owned-bot path stays the production substrate.
- No FreeBot or ownerless path is introduced.
- No headless `Client` work is introduced.
- Group leadership remains unchanged; `Group::ChangeLeader()` still rejects bot leaders in the normal path.

## Dependency On `central-lhy.2`

`central-lhy.2` remains the owner decision for production identity, persistence, and authority. This seam depends
on that decision because bots still require an owner and still use the owner for bot options, pulling state, save
identity, and other existing invariants.

This work does not replace `central-lhy.2`. It only narrows two bot-AI reads that previously hard-coded the owner
client as the source of gameplay intent.

## Harness Proof

`POST /api/v1/harness/scenarios/actor-led-bot-party` now proves both sides of the seam:

- owner-client target, assist, and leash defaults still behave as before; and
- a follower bot can source target, assist, and leash intent from the actor-leader bot while all bots keep the
  same owner.

That proof stays intentionally narrow: it extends `central-lhy.10` rather than replacing bot ownership,
owner authority, or client-only group leadership.

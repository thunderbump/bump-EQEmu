# Actor Command Source Seam

Date: 2026-06-26

Beads item: `central-lhy.10`

## Scope

This change follows `central-lhy.9` by extracting a narrow **ActorCommandSource** seam inside bot AI for two
gameplay intents only:

- command target sourcing for the owned-bot attack path; and
- leash anchor sourcing for combat target validation.

The seam is runtime-only. It stores ephemeral entity IDs on each `Bot` and resolves them through ordinary zone
entity lookup. When no actor-specific source is configured, the bot still defaults to the current owner-client
behavior.

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

- owner-client target and leash defaults still behave as before; and
- a follower bot can source target and leash intent from the actor-leader bot while all bots keep the same owner.

That proof stays intentionally narrow so follow-up work can address assist sourcing or broader party leadership
separately if needed.

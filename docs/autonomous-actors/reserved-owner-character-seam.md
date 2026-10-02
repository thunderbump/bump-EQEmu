# Reserved Owner-Character Seam

Date: 2026-07-05

Beads item: `central-lhy.15`

## Purpose

Production bot-backed actors still need the existing bot owner authority path, which is keyed by a character ID.
This seam keeps that requirement explicit without pretending the actor is a real player session.

The identity split stays:

- `actor_id`: actor-facing identity in `actor_profiles`.
- `bot_id`: gameplay substrate identity in `bot_data` and bot inventory/state tables.
- `owner_character_id`: the reserved owner character row that current bot ownership/save/spawn paths still require.

## Reserved Owner Identification

A reserved owner record is provisioned with these durable markers in `character_data`:

1. `name` starts with the reserved prefix `Actorowner`.
2. `last_name` is stamped to the non-secret marker `ReservedActorOwner` by `Provision()`.
3. the row stays a non-playable shell: `level = 0`, `class = 0`, and `race = 0`.

An active reserved owner association is identified by both of these conditions:

1. the provisioned `character_data` row above exists; and
2. a bot-backed `actor_profiles` row (`actor_substrate = 'bot'` with `bot_id IS NOT NULL`) points at that row.

This keeps identification deterministic without storing a password, login token, or any actor secret in repo code,
`actor_profiles`, or harness payloads, and avoids treating an arbitrary player-created `Actorowner*` name as a
reserved owner record.

## Provisioning

The code-level provisioning helper is `EQ::Actor::ReservedOwners::Provision()` in
`common/actor_reserved_owners.h`.

Behavior:

- accepts only names with the `Actorowner` prefix;
- stamps the provisioned row with non-secret marker `character_data.last_name = ReservedActorOwner`;
- keeps the provisioned row non-playable by leaving `level`, `class`, and `race` at zero;
- reuses an existing row only when that row still carries the same provisioned non-playable shell shape;
- otherwise inserts a minimal `character_data` row and returns its `character_id`;
- stores no secrets.

Operator guidance:

- Prefer one reserved owner row per production actor profile so rollback is simple and actor ownership is obvious.
- If your environment requires character rows to belong to an account, use a dedicated operator-managed service
  account ID. Do not record that account password in actor config, docs checked into git, or `actor_profiles`.
- `actor_profiles.owner_character_id` is the only durable actor-to-owner link needed by this seam.
- Bot-backed `actor_profiles` must keep that reserved owner binding; `UpsertBotBackedProfile()` now rejects null,
  arbitrary, or non-provisioned owner bindings instead of clearing or replacing them.
- If a normal character already occupies an `Actorowner*` name without the reserved-owner marker, provisioning now
  fails instead of silently adopting that row. Pick a different reserved name or retire that ordinary character
  name first.
- If a playable `Actorowner*` character is manually stamped with `last_name = ReservedActorOwner`, provisioning and
  lookup still reject it because it no longer matches the provisioned non-playable shell.

## Runtime Boundary

Synthetic `Client()` owners remain harness-only.

They are still useful for deterministic tests because current bot spawn/save paths expect a live owner `Client`
pointer in addition to the durable owner character ID. The harness can create a synthetic owner client whose
`CharacterID()` matches the reserved owner row, prove the normal bot owner invariants, and then tear that client
down.

Production code should not treat synthetic clients as a new owner model. The production durable contract is the
reserved `owner_character_id` record plus ordinary bot ownership checks.

## Spawn/Save Invariant

`Bot::Spawn(Client *botCharacterOwner)` now rebinds the bot's owner pointer from the passed client after the
existing owner-character ID match succeeds. That keeps the normal bot invariant intact for reserved-owner flows:

- the bot still requires a matching owner character ID;
- the bot still ends up with a normal owner `Client *` during spawn/save work;
- the seam does not introduce an ownerless bot path.

## Deterministic Coverage

`zone tests:reserved-actor-owner` covers the reserved-owner path end to end:

1. prove that a plain `Actorowner*` collision row without the provisioned marker is not treated as reserved;
2. prove that a marker-bearing playable `Actorowner*` row is still rejected;
3. prove that a stale non-bot `actor_profiles.owner_character_id` association does not activate reserved-owner lookup;
4. provision a reserved owner row;
5. save a bot with that owner character ID;
6. associate `actor_profiles.owner_character_id` through the reserved binding path without writing secrets;
7. reload the bot with no live owner client present;
8. spawn and save it again through a harness-only synthetic owner client whose `CharacterID()` matches the
   reserved owner row;
9. verify rollback once the bot and actor profile rows are removed.

## Rollback

Use this order:

1. disable or remove the actor profile;
2. delete the bot substrate rows;
3. delete the reserved owner row.

`EQ::Actor::ReservedOwners::Rollback()` enforces that last step by refusing to delete the reserved owner when
`actor_profiles.owner_character_id` or `bot_data.owner_id` still reference it. Rollback also requires the
provisioned non-secret marker, so a plain `Actorowner*` character row is not treated as a reserved owner record.

That makes rollback operator-visible and keeps accidental dangling ownership from silently persisting.

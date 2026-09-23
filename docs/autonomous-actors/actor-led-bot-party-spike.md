# Actor-Led Bot Party Spike

Date: 2026-06-26

Beads item: `central-lhy.9`

## Question

Can Zone Harness prove a useful intermediate shape where one synthetic owner `Client` preserves normal bot owner
invariants, one bot-backed `Autonomous Actor` acts as the party gameplay lead, and 1-4 follower bots behave as a
normal group?

## Harness Proof

Added `POST /api/v1/harness/scenarios/actor-led-bot-party`.

The scenario builds one synthetic owner client, one owned bot actor leader, and a bounded 1-4 follower bot party
inside one normal `Group`. It then runs four bounded proofs through ordinary zone processing:

1. Owner invariants stay intact for the actor leader and all followers.
2. Followers can keep a bot leader as their `FollowID` anchor, so out-of-combat follow intent does not have to be
   owner-client sourced.
3. Current target command still comes from the owner client: an owner-target baseline produces a follower
   `spell_cast_started` slow event, while an actor-leader-target-only attempt does not.
4. Combat leash still falls back to the owner client even when the follower keeps the actor leader as its
   `FollowID`, and the leash clear is tied to the owner moving outside leash range rather than any target clear.

The smoke path is wired into `scripts/smoke-zone-harness.sh` and Tier 3 harness validation.

The upper bound is 4 followers, not 5, because the proof keeps the synthetic owner client and actor leader inside
one ordinary six-member EQ group. The Tier 3 smoke now proves the minimum (`1`) and maximum (`4`) follower
counts explicitly.

## Findings

- Synthetic owner authority is still the viable harness substrate. All bots in the proof keep the same owner
  pointer, so the party preserves current bot owner assumptions without touching persistence.
- Bot-led follow intent is already partially viable. Followers can keep the actor leader bot as their follow
  anchor during ordinary processing.
- Group leadership is still client-only for the useful leadership path. `Group::ChangeLeader()` returns early for
  non-client leaders, so a bot-backed actor cannot become the normal group leader today.
- Target command is owner-client-shaped. `Bot::SetOwnerTarget()` reads only the owner client's target, and the
  proof's positive baseline depends on that path.
- Assist was owner/client-shaped in the spike baseline. `Bot::TryAutoDefend()` scanned the owner client and client
  main-assist members only. `central-lhy.17` later extended the same command-source seam so actor-led assist can be
  sourced without replacing owner authority or group leadership.
- Combat leash is still owner-client-shaped. `Bot::SetLeashOwner()` falls back to the owner client for group
  leashing, and `Bot::IsValidTarget()` measures leash distance from that owner client.

## Decision

Do not split toward a broad `ActorPartyLeader` first.

The smallest useful split is:

- `ActorOwner`: persistence, bot ownership, authority, spawn/save identity, and any current client-shaped
  invariants that still need a reserved or synthetic owner.
- `ActorCommandSource`: gameplay intent for follow/target/assist/leash decisions, introduced narrowly where bot AI
  currently hard-codes the owner client.

`ActorPartyLeader` is too narrow as the first abstraction because the current blockers are not only group-leader
state. They are shared target, assist, and leash intent seams inside bot AI.

## Recommendation

`central-lhy.9` narrows but does not obviate `central-lhy.2`.

- `central-lhy.2` is still the prerequisite for production ownership and persistence shape.
- This spike shows that the next implementation step should not be a wide owner replacement or a bot group-leader
  refactor.
- The next implementation step should be a narrow command-source seam in bot AI, starting with target sourcing and
  leash sourcing while the existing owner model stays intact.

## Smallest Next Step

Add one narrow bot-AI intent adapter for harness and future production use:

- target source: replace direct owner-target reads in `Bot::SetOwnerTarget()` with a small command-source query;
- leash source: replace direct owner-client fallback in `Bot::SetLeashOwner()` with a command-source/leash-source
  choice that can still return the owner client by default;
- keep group leadership unchanged for that first step; treat client-only `Group::ChangeLeader()` as a separate
  follow-up only if target/leash command-source extraction proves insufficient.

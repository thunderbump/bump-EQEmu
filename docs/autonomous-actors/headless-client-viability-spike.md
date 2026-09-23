# Headless Client Viability Spike

This spike covers `central-lhy.5`: keep the headless `Client` branch in the autonomous-actor design tree, but treat it as high-risk until the project proves that a `Client`-derived actor can do something useful without an `EQStream`-backed session or the normal player login lifecycle.

## Recommendation

Recommendation: no-go for broader `Client`-backed **Autonomous Actor** work right now.

Keep the headless `Client` path as a design branch for later investigation, but do not use it as the next implementation slice for player-equivalent actors. The bounded proof below shows that a synthetic `Client()` can perform one direct target-selection action inside the **Zone Harness** without crashing or needing packet ingress. That is not enough player-equivalence to justify the runtime and maintenance cost of the missing login, packet, save, guild, task, and zoning lifecycle.

Use owned bots and harness-owned action queues for near-term autonomous-actor work. Re-open headless `Client` exploration only after there is a concrete need for a player-only mechanic that bots cannot express through ordinary server behavior.

## Bounded proof

I added one minimal **Zone Harness** scenario:

- HTTP route: `POST /api/v1/harness/scenarios/headless-client/target`
- Runtime entry: `EQ::ZoneHarness::ZoneHarnessRuntime::RunHeadlessClientTarget()`

The scenario does only this:

1. Boots a normal harness zone.
2. Creates a synthetic `Client()` with no `EQStream`.
3. Adds that client to `entity_list`.
4. Spawns one synthetic NPC target.
5. Calls `Client::SetTarget()` directly.
6. Verifies the actor now points at the target and that the harness observed a `target_changed` **Actor Event**.
7. Clears the synthetic target and removes both synthetic entities before returning the final event cursor.

This is intentionally narrow. It proves only that a headless `Client` object can survive one synchronous Mob-layer action when the action does not depend on packet decoding, login completion, or client-facing packet delivery.

## Why this proof works

The current repo already uses a synthetic `Client()` in safe non-login contexts:

- `zone/harness/owned_bot_actor_fixture.cpp`
  - The harness-owned bot fixture creates `owner = new Client();` and uses it as an in-memory owner/group anchor.
- `zone/cli/tests/cli_npc_handins.cpp`
  - The CLI test creates `new Client()` for in-memory hand-in behavior.
- `zone/cli/tests/cli_databuckets.cpp`
  - The CLI test creates `new Client()` for DB-backed bucket behavior.

The headless constructor in `zone/client.cpp` sets `eqs = nullptr`, leaves `client_state = CLIENT_CONNECTING`, disables autosave timers, and keeps `client_data_loaded = false`. That makes it usable for some synthetic server-side behaviors, but it also makes the object explicitly incomplete as a real logged-in player.

The target proof stays on the safe side of that boundary:

- `Mob::SetTarget()` in `zone/mob.cpp` changes the target pointer directly and records a harness `target_changed` event.
- That path does not require `HandlePacket()`, opcode mapping, `CompleteConnect()`, or `QueuePacket()` to succeed.
- The scenario captures its final event cursor after cleanup, so callers do not inherit an unreported target-clear event when polling later **Actor Events**.
- It is not side-effect free: `SetTarget()` also runs target-related server hooks such as HoTT updates, quest target-change dispatch, and target HP update paths. The proof is safe only because those paths currently tolerate a constructor-only `Client` with `eqs = nullptr`; it does not establish that arbitrary client-facing side effects are safe without a stream.

## Why this is not player-equivalent

The moment the actor needs normal player lifecycle behavior, the headless `Client` stops looking cheap.

### Packet and stream boundary

- `Client::HandlePacket()` in `zone/client_packet.cpp` dereferences `eqs` immediately to get the opcode manager.
- A headless `Client()` has `eqs = nullptr`.
- That means real packet-driven behavior cannot be exercised safely without either a fake stream implementation or a second non-packet action surface.

### Login lifecycle boundary

- `Client::CompleteConnect()` in `zone/client_packet.cpp` is the normal transition to `CLIENT_CONNECTED`.
- It loads task state, account flags, guild state, raid/group packets, zone-in packets, alternate currencies, disciplines, auras, and other connected-player state.
- The headless proof does not run any of that. The actor remains in the constructor-only `CLIENT_CONNECTING` state.

### Packet queue boundary

- `Client::QueuePacket()` and `Client::FastQueuePacket()` in `zone/client.cpp` only deliver packets when `eqs` exists and the connection state matches.
- Without `eqs`, many client-facing updates are either buffered, dropped, or deleted.
- This is acceptable for a proof that only mutates in-memory target state, but it blocks most true player-equivalent behavior.

## Risk map

### Crash risk

- High for packet-driven paths.
- `Client::HandlePacket()` assumes `eqs` is non-null.
- Any attempt to drive normal opcode handlers without a fake stream is unsafe.

### Null stream risk

- High.
- The headless constructor is explicitly `eqs = nullptr`.
- Direct Mob-layer calls can work; packet-driven or connection-aware client code often cannot.

### Save risk

- Medium.
- `Client::~Client()` calls `Save(2)`, but `Client::Save()` returns false when `client_data_loaded` is false.
- That keeps the bounded proof from writing normal character saves, but it also shows the object is not a real loaded player.

### Login risk

- High.
- No `CompleteConnect()`, no loaded task state, no normal zone-in packet stream, no connected-client state.

### Zoning risk

- High.
- Zoning code assumes the ordinary client session and packet lifecycle.
- A headless actor that needs cross-zone behavior would require substantially more lifecycle emulation than this spike proves.

### Packet queue risk

- High.
- Many systems use `QueuePacket()` or `FastQueuePacket()` as part of their observable effect.
- Without an attached stream, those updates are not a reliable gameplay surface.

### Guild risk

- High.
- Guild connect/update flows are part of `CompleteConnect()` and downstream packet sends.
- Headless `Client` work would need explicit decisions about whether guild state is loaded, stubbed, ignored, or replayed.

### Task risk

- High.
- Task loading and task packet emission also live behind the normal client connect lifecycle.
- A player-equivalent autonomous actor that can accept, progress, or inspect tasks would need much more than the current proof.

### Performance risk

- Medium to high.
- `Client` carries much more lifecycle surface than a bot or narrower actor primitive.
- Even if a fake stream made the object safe, the runtime cost and maintenance burden would be materially higher than the owned-bot path for the current autonomous-actor goals.

## Player-equivalence gained vs. cost

What the spike proves:

- A synthetic headless `Client` can perform one direct `SetTarget()` action under the **Zone Harness** and emit an observable `target_changed` event without an `EQStream` session.

What the spike does not prove:

- Packet-driven action handling
- Normal say/chat input handling
- Login-complete connected state
- Save/load character lifecycle
- Guild or task participation
- Zoning
- Reliable client-visible packet side effects

That is too little player-equivalence for too much risk. The owned-bot path already gives bounded **Actor Action**, **Actor Event**, and **Actor Perception** coverage inside the **Zone Harness** with much lower lifecycle cost.

## Preconditions required before reconsidering

Re-open headless `Client` work only if all of the following become necessary:

- A specific desired actor behavior exists only on `Client` and cannot be expressed safely through bots or a narrower action surface.
- The project is willing to define a fake or harness-owned `EQStreamInterface` strategy for non-crashing packet handling.
- The project is willing to choose an explicit policy for connect state, save behavior, guild/task loading, and zoning semantics for synthetic clients.
- The first follow-up bead is scoped to one player-only behavior, not general player parity.

Until then, the correct recommendation is: keep the idea in the tree, but no-go on broader `Client`-backed actor implementation.

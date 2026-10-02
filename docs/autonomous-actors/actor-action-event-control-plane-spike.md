# Actor Action And Event Control Plane Spike

This spike covers `central-lhy.4`: prototype the lowest-impact async **Actor Action** and **Actor Event** control plane for **Autonomous Actors** without recommending a headless `Client` path.

Recommendation: keep the first control plane inside the existing **Zone Harness** HTTP server as a thin transport over one ephemeral owned-bot session, a bounded request/ack queue, and the existing cursor-based event feed. Defer live-zone routing, world coordination, and durable queueing until there is a concrete need for multi-zone orchestration, stronger durability, or independent scaling.

## Scope

This slice intentionally stays narrow.

- Transport remains local harness HTTP.
- Actor remains an owned bot created by the harness fixture.
- Supported actions remain `target` and `say`.
- Event observation remains `GET /api/v1/harness/events?since=<cursor>&limit=<n>`.
- Processing remains explicit through bounded `/api/v1/harness/process` ticks.
- The prototype is disabled by default and must be opted into explicitly when starting `zone tests:serve-http`.

That shape reuses the learnings from `central-lhy.1`, `central-lhy.5`, and `central-lhy.6`: ordinary bot behavior is much cheaper and safer than manufacturing a player lifecycle, while cursor-based harness events are already enough to prove bounded action outcomes.

## Prototype flow

The harness-facing prototype uses four endpoints:

- `POST /api/v1/harness/autonomous-actors/prototype/session/start`
- `GET /api/v1/harness/autonomous-actors/prototype/session`
- `POST /api/v1/harness/autonomous-actors/prototype/actions`
- `POST /api/v1/harness/autonomous-actors/prototype/session/stop`

The request/ack plus event observation loop is:

1. Start an ephemeral session. The harness creates a synthetic owner, one owned bot actor, and bounded NPC targets in memory only.
2. Submit one action request such as `{"kind":"target","detail":"primary_target"}` or `{"kind":"say","detail":"Harness autonomous actor ready."}`.
3. Receive an immediate ack with:
   - `accepted`
   - `reason`
   - `request_id`
   - `event_cursor_start`
   - bounded polling hints: `process_ticks_hint`, `poll_after_ms`, `event_limit_hint`
4. Call `POST /api/v1/harness/process` with a small tick count, normally `1..2`.
5. Poll `GET /api/v1/harness/events?since=<event_cursor_start>&limit=<event_limit_hint>`.
6. Treat `target_changed` or `speech_emitted` as the observable completion signal.
7. Read `GET /api/v1/harness/autonomous-actors/prototype/session` when actor-scoped status or perception is needed.
8. Stop the session when done.

The ack does not claim the action succeeded in gameplay terms. It only means the harness accepted the bounded request into the ephemeral queue. Completion is still determined by ordinary processing plus observable **Actor Events**.

## Prototype constraints

### Backpressure

- One harness runtime owns at most one active prototype session.
- The session queue is capped at `4` pending actions.
- Requests beyond that cap are rejected with `reason = "queue_full"`.
- The prototype does not promise fairness across callers. It is a single-user harness tool, not a shared production broker.

### Event limits

- Keep using the in-memory recorder ring buffer.
- The current recorder cap of `512` events remains the hard upper bound.
- Keep `GET /api/v1/harness/events` clamped server-side to `1..1000`.
- Prototype callers should use the ack hint and keep event polling to `limit <= 32` unless they are draining unrelated scenario traffic.
- If a caller lets the event cursor fall behind the ring buffer, that is a caller error in this prototype.

### Auth

- Reuse the existing harness bearer token support.
- Do not add a second auth system inside the prototype.
- Treat missing bearer auth as acceptable only for localhost-only developer harness runs.
- Do not expose this prototype on non-local interfaces.

### Disabled-by-default settings

- The HTTP surface is off by default behind `--enable-autonomous-actor-prototype`.
- Default harness startup should continue exposing only the existing scenario and snapshot surfaces.
- Keep the prototype opt-in until there is live-zone demand and stronger validation coverage.

### Polling cadence

- Target cadence: one action request, then `1..2` process ticks, then one event poll.
- Recommended poll delay: around `50 ms`.
- Clamp action submission behavior at the caller. Do not enqueue bursts faster than one new request per completed observation cycle.
- Do not turn the harness into a busy-loop event stream. Explicit bounded polling keeps runtime ownership clear.

## Why harness HTTP first

Harness HTTP is the lowest-impact option because it already exists, is local to the zone process, and is explicitly test infrastructure rather than gameplay transport.

Advantages:

- No new world or login routing.
- No new durable storage.
- Reuses existing actor fixture, perception snapshot, and cursor event recorder.
- Keeps setup shortcuts visibly separated from ordinary actor actions.
- Easy to keep disabled by default.

Costs:

- Single-zone only.
- No durability across process exit.
- Explicit polling instead of push delivery.
- Not suitable for many actors or many remote callers.

That trade is correct for the first async control-plane slice.

## Option comparison

### 1. Zone Harness HTTP

Recommended for the current slice.

- Best when the goal is bounded validation and learning.
- Lowest implementation risk because the control plane lives beside the runtime under test.
- Works well with cursor polling and explicit tick processing.
- Should remain local-only and opt-in.

### 2. Live-zone HTTP or WebSocket

Viable only after the harness contract proves useful enough to survive outside tests.

HTTP pull:

- Closest operational shape to the harness prototype.
- Easier to stage behind existing bearer auth and disabled-by-default settings.
- Still needs careful port binding, lifecycle management, and per-zone enablement.

WebSocket push:

- Better when event latency or event volume makes polling too chatty.
- Adds connection lifecycle, subscriber fan-out, and backpressure complexity immediately.
- Premature for the current event set and current caller count.

Recommendation: if this leaves the harness, use live-zone HTTP pull before WebSocket push.

### 3. ServerTalk or world routing

This becomes worthwhile when the caller needs a stable world-facing control plane across many zone processes.

Advantages:

- Uses existing server-to-server patterns.
- Lets world or another coordinator address actors without a per-zone HTTP surface.
- Gives a natural place for cross-zone actor ownership and routing decisions.

Costs:

- More message-shape work up front.
- Requires routing, correlation IDs, response handling, and timeout behavior across processes.
- Harder to keep the first slice visibly bounded.

Recommendation: choose this once actor commands need to outlive a single zone harness process or target many zones from one coordinator.

### 4. DB-backed queue

Not recommended for the first slice.

Advantages:

- Durable handoff and retry semantics are straightforward to reason about.
- External workers can observe and recover state without a live socket.

Costs:

- Highest latency.
- Adds polling load to the database.
- Encourages durable workflow complexity before the action vocabulary is stable.
- Blurs gameplay control with persistence concerns.

Recommendation: use a DB-backed queue only when actor actions must survive zone/world restarts or when offline orchestration matters more than low-latency control.

## When a sidecar container becomes worthwhile

A separate sidecar is not justified yet for one zone-local owned-bot loop.

Adopt a sidecar when most of these become true:

- More than one zone process needs to be coordinated concurrently.
- The caller needs one stable endpoint instead of per-zone local harness endpoints.
- Event polling volume or WebSocket fan-out should be isolated from the zone process.
- Action planning becomes materially heavier than the bounded zone-local request handling.
- Credential management, rate limiting, or audit requirements exceed what a localhost harness bearer token should own.
- You need independent deploy/restart cadence from the game server.

Until then, a sidecar would mostly add orchestration overhead without solving the current learning problem.

## Recommended next slices

1. Keep this harness prototype ephemeral and validate it with one focused smoke path once the shape settles.
2. Add event-type filtering to `GET /api/v1/harness/events` if prototype polling starts to compete with unrelated scenario traffic.
3. Add one bounded live-zone design doc before implementing any non-harness transport.
4. Move to world-routed control only when there is a real multi-zone actor coordinator to support.

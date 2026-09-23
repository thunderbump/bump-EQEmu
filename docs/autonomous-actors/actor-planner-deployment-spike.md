# Autonomous Actor Planner Deployment Spike

Date: 2026-06-26

Beads item: `central-lhy.7`

## Question

Once the in-zone **Actor Action** / **Actor Event** primitive exists, should high-level **Autonomous Actor**
planning for low-cadence goals, reporting, chatter decisions, and inventory/economy planning stay inside the game
server process or move to a separate sidecar shape?

## Recommendation

Keep the execution primitive in zone runtime, but move high-level planning out of zone ticks.

For the MVP, run the planner as a separate helper process inside the existing `eqemu-server` container rather than
as an in-process scheduler or a new always-on sidecar container. Shape its contract so promotion to a dedicated
sidecar container is a deployment change, not a planner rewrite:

- discover actors from actor-owned persistence;
- read coarse status plus bounded event history;
- submit bounded, idempotent actions through a queue or control-plane adapter;
- let the zone remain the final authority on freshness, legality, and action execution.

Do not use an external worker for live actor planning. Reserve that shape for offline reporting, batch
re-planning, or operator-triggered maintenance.

## Why not in zone

`central-lhy.4` already keeps the first **Actor Action** / **Actor Event** control plane narrow: bounded request
acks, explicit polling, and queue depth caps inside the harness runtime. `central-lhy.3` then proposes actor-owned
`actor_status`, `actor_action_queue`, and `actor_events` tables specifically so persistent planning and reporting
do not have to pretend that bot-originated work is player work.

That split is enough to keep expensive reasoning out of the gameplay loop:

- zone runtime owns fresh state, ordinary gameplay execution, and bounded action acceptance;
- the planner owns slower inference, prioritization, reporting, and delayed intent generation;
- durable status/events let the planner restart without forcing reasoning into per-tick zone code.

High-level chatter, reporting, and inventory/economy planning are exactly the kinds of work that become dangerous
if they share the same CPU budget and failure domain as zone ticks.

## Option comparison

| Option | Fit | Strengths | Main costs | Verdict |
| --- | --- | --- | --- | --- |
| In-process scheduler | Small deterministic housekeeping only | Zero IPC, simplest state access, easiest local debugging | Shares crashes/CPU stalls with zone, invites expensive reasoning in tick-adjacent code, harder restart isolation | Reject for planner logic |
| Same-container helper process | First live MVP | Separate failure domain from zone, can use localhost or DB access cheaply, no new Compose service, aligns with one-off validation container patterns | Still shares container lifecycle, one more local process to supervise, not a stable multi-zone endpoint | Recommend now |
| Separate sidecar container | Promotion path once always-on planning is real | Clear resource/restart isolation, stable service per stack, easier health checks and rate limiting, natural place for multi-zone coordination | More Compose and secrets wiring, more network/ops work, need service discovery and health management | Adopt after MVP proves useful |
| External worker | Offline/batch only | Strong isolation, portable like the validation worker contract, easy to scale separately | Highest freshness lag, requires durable queues and retries, weak fit for minute-to-minute actor planning | Use only for batch/reporting |

## Planner contract

The planner should talk to actor-owned state, not to arbitrary in-zone internals.

### 1. Actor discovery

Use actor identity tables from `central-lhy.3` as the planner's roster source:

- `actor_profiles`: stable `actor_id`, actor type, bot substrate, owner character link, enabled flag;
- `actor_status`: current zone binding, current state, heartbeat, bounded status JSON.

Discovery query shape:

- select enabled actors;
- require a fresh `heartbeat_at`;
- require a live zone binding (`zone_id`, optional `instance_id`, optional `entity_id`);
- skip actors whose status is stale or already marked unavailable.

The planner should not scrape zone entity lists to find work. Zone runtime publishes actor presence; the planner
consumes it.

### 2. Status and event reads

Use two bounded reads:

- `actor_status` for the latest coarse snapshot needed to decide whether planning is even worthwhile;
- `actor_events` for monotonic deltas since the planner's last consumed `event_id`.

That division matters:

- status answers "is this actor alive, present, and worth planning right now?";
- events answer "what changed since the last decision?".

For validation or very early local-only experiments, the helper process may still poll a harness/live-zone HTTP
surface that mirrors `central-lhy.4` semantics. For durable planning, the planner should treat actor tables as the
source of truth and use HTTP only as the bounded execution ingress if needed.

### 3. Bounded action submission

Submit actions through a bounded queue or equivalent control-plane adapter, not by mutating gameplay state.

Recommended durable shape:

- one `actor_action_queue` row per requested action;
- action carries `actor_id`, `action_type`, bounded `action_json`, `not_before`, `expires_at`, and source metadata;
- zone runtime claims rows only for actors it currently owns;
- zone runtime emits resulting `actor_events` with the originating `action_id`.

Recommended action-shaping rules:

- use small, gameplay-native action kinds only;
- attach an idempotency key per planner decision window;
- include an expected status/event watermark when the action depends on a specific observed state;
- set a short expiry so stale intent dies instead of accumulating.

The planner chooses intent. The zone decides whether that intent is still legal and fresh enough to execute.

## Failure modes and controls

### Stale zone state

Risk:
planner decides from an old snapshot, then the actor zones, dies, changes inventory, or loses the target before the
action executes.

Controls:

- require fresh `heartbeat_at` before planning;
- stamp each action with the last observed status/event watermark;
- have zone runtime reject or expire actions whose expected watermark is too old or whose actor ownership changed;
- keep action TTL short for chatter and inventory/economy decisions.

### Duplicate commands

Risk:
planner restarts, event replay repeats, or multiple planners emit the same chatter/report/inventory action.

Controls:

- add a planner-generated idempotency key, for example `(actor_id, policy, trigger_event_id, goal_epoch)`;
- de-dupe pending/claimed/completed actions by that key;
- record `action_id` on emitted `actor_events`;
- keep one active planner owner per stack for the MVP.

### Queue buildup

Risk:
low-cadence planning becomes a backlog factory when actors are unavailable or zones are busy.

Controls:

- cap pending actions per actor and per action class;
- reject or supersede old low-value chatter/report actions;
- require `expires_at` on planner-submitted work;
- plan only after observing completion, expiry, or a meaningful new event delta instead of enqueueing every cadence.

Chatter and reporting should be the first work dropped under pressure. They should not compete with combat or
movement execution.

### Sidecar or helper outage

Risk:
planner process dies or is intentionally stopped.

Controls:

- zone runtime continues without planner-owned actions;
- actor status heartbeat eventually goes stale and operators can see planner absence;
- bounded in-zone gameplay behavior keeps working because execution logic is still local;
- when the planner returns, it resumes from the last consumed `actor_events` cursor and recalculates instead of
  replaying unbounded stale intent.

### Zone outage or ownership movement

Risk:
planner is healthy, but a zone process restarts or actor ownership moves.

Controls:

- planner treats `actor_status` heartbeat loss as authority loss;
- claimed queue rows that outlive the owning zone should return to `pending` or `expired`;
- a restarted zone should publish a fresh heartbeat before it can claim new work;
- planner should never assume that a previously owned actor is still local to the same ingress endpoint.

## MVP deployment shape

### Validation stack

Use a same-container helper process first.

Why this fits the current repo:

- validation already prefers one-off `docker-compose run --rm --no-deps --entrypoint bash eqemu-server ...`
  commands;
- `scripts/validation-worker.sh` already serializes access to one validation slot and binds the fetched checkout
  into the selected validation AkkStack;
- `scripts/smoke-zone-harness.sh` already proves the pattern of launching bounded zone HTTP work inside a one-off
  validation container without depending on the persistent gameplay server.

Practical MVP shape:

1. launch the zone-side execution primitive in the validation stack;
2. run one planner helper process in the same `eqemu-server` container namespace;
3. have it discover actors, poll status/events, and submit bounded actions;
4. validate outcomes through the existing harness/event observation path and evidence logs.

This keeps validation close to current wrappers and avoids adding a second long-lived validation service before the
planner contract settles.

### Gameplay AkkStack

Use the same helper-process shape first for manual runtime proofs and initial local gameplay experiments.

Promote to a dedicated sidecar container only when most of these become true:

- planner must stay running continuously;
- more than one live zone needs coordination at once;
- operator-facing health checks and restart policy matter;
- CPU and memory limits should be isolated from `eqemu-server`;
- secrets, rate limits, or audit controls deserve their own service boundary.

At that point, keep the planner contract the same and move only the deployment:

- new Compose service on the existing backend network;
- read/write access only to actor-owned tables and the bounded action ingress;
- explicit health check and restart policy;
- one planner instance per stack until real multi-planner coordination is needed.

## Decision

High-level **Autonomous Actor** planning should not live in zone ticks once the action/event primitive exists.

The first production-minded step is a separate helper process in the existing `eqemu-server` container, backed by
actor-owned status/event/queue tables and bounded zone-side execution. A dedicated sidecar container is the next
deployment step once planning is always-on or multi-zone, but it is not the cheapest or clearest MVP for the
current AkkStack and validation-stack shape.

## Source references

- `docs/autonomous-actors/actor-action-event-control-plane-spike.md`
- `docs/autonomous-actors/actor-persistence-event-schema-spike.md`
- `docs/autonomous-actors/actor-event-perception-expansion-spike.md`
- `docs/autonomous-actors/actor-command-source-seam.md`
- `docs/adr/0003-zone-harness-for-runtime-gameplay-validation.md`
- `docs/adr/0005-separate-validation-and-gameplay-akkstack-environments.md`
- `docs/adr/0006-validation-worker-contract-for-automation.md`
- `docs/testing/process.md`
- `scripts/smoke-zone-harness.sh`
- `scripts/validation-worker.sh`
- `zone/harness/zone_harness_runtime.cpp`
- `zone/cli/cli_sidecar_serve_http.cpp`
- `zone/sidecar_api/sidecar_api.cpp`

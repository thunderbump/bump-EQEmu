# Autonomous Actor runtime-cost baseline

Date: 2026-07-12 (America/Los_Angeles)

Wayfinder ticket: `central-dcq7.3`, **Establish the runtime cost baseline for live Actor-led Parties and zone activation**

Snapshot notice: this is the initial decision-time baseline. `central-dcq7.22` later repaired validation Zone Harness
database routing in
[`360cf66cc5240525d40e8cfe329708c64bf12346`](https://github.com/thunderbump/bump-EQEmu/commit/360cf66cc5240525d40e8cfe329708c64bf12346),
and `central-dcq7.23` later gathered persistent matched-party evidence. Those follow-ups supersede the blockers below;
the measurements and the decision not to infer a population budget from them remain historical evidence.

## Decision

The current validation environment supports a reproducible **host and persistent MariaDB baseline**, but it does not
currently support a defensible live-zone or marginal Actor-led Party budget. The canonical Zone Harness failed before
zone boot because its runtime configuration attempted MariaDB at `127.0.0.1` from a one-off container. More
importantly, the existing Actor-led Party scenario creates and destroys several transient fixtures inside one request;
it cannot hold zero, one, or multiple parties steady for matched CPU/memory samples.[^smoke][^party-runtime]

Therefore this investigation sets **no provisional party-count, active-zone, or world population budget**. Doing so
from the observations below would confuse container/database overhead, zone startup, fixture construction, four proof
phases, and steady-state actor processing. The next measurement-enabling prototype needs a persistent, bounded party
fixture and an explicitly identified portless measurement runner.

## What was measured

All observations used the validation checkout and persistent validation MariaDB. No gameplay stack, database row,
schema, repository source, published port, or secret was changed. The canonical harness creates a one-off
`eqemu-server` container with service ports removed and curls localhost from inside that container.[^process]

Identity:

- EQEmu source: `9ec39551c046bad3b4e94d6435f09a1073ba5540`.
- Validation AkkStack: `729b29c4fb09b9aa9a04afaf391eaebf85390326`.
- Built `zone`: SHA-256 `3c6cfc937948d5e127b2ea1ffabcda54516d2fcaea41d0430860037af4a6d061`,
  242,585,400 bytes, modified 2026-06-26 20:02:48 -0700. This build predates the source checkout, so even a successful
  run would need to be described as a binary baseline, not proof of current-HEAD behavior.
- Host: Linux 6.17.0-35-generic, AMD Ryzen AI MAX+ 395, 16 cores/32 threads, 33,421,365,248 bytes RAM.
- Docker Engine 29.6.1 and Compose 5.3.1.

### Host and persistent database context

Three samples were taken ten seconds apart. `docker stats --no-stream` CPU is the container's interval-derived CPU
percentage; memory is cgroup usage, not process RSS or PSS. `/proc/loadavg` is host runnable/load-average context, not
attribution to EQEmu.

| Trial | Host load (1/5/15 min) | MariaDB CPU | MariaDB cgroup memory | PIDs |
|---:|---|---:|---:|---:|
| 1 | 0.32 / 0.21 / 0.18 | 0.03% | 117.9 MiB | 10 |
| 2 | 0.35 / 0.22 / 0.19 | 0.03% | 117.9 MiB | 10 |
| 3 | 0.30 / 0.21 / 0.18 | 0.04% | 117.9 MiB | 10 |

At the initial observation the host had 25,697,001,472 bytes available memory and approximately 1 MB swap in use.
MariaDB had been running since 2026-07-10T00:28:43Z. Its actor tables existed but contained zero rows:

| Table | Rows |
|---|---:|
| `actor_profiles` | 0 |
| `actor_status` | 0 |
| `actor_action_queue` | 0 |
| `actor_events` | 0 |

This proves the storage schema is present and that an empty durable actor population imposes no separately measurable
row/query workload in this database snapshot. It does **not** measure per-row storage, polling, or planner load. A
read-only status sample after the table queries showed 38 total server `Questions`, zero slow queries, and one connected
thread; the counter includes earlier activity since MariaDB startup and is not an actor delta.

### Live Zone Harness attempt

Three canonical trials were planned using:

```sh
./scripts/smoke-zone-harness.sh --stack validation
```

The first trial stopped at zone initialization with:

```text
Connect Connection [default] Failed to connect to database ... 127.0.0.1
main Cannot continue without a database connection
```

The wrapper had already verified the validation contract and that canonical MariaDB was running. Because no zone
reached healthy state, later trials were not run: repeated startup failures would not add performance evidence. No idle
zone RSS/PSS, startup wall time, second-zone marginal cost, bounded tick cost, event cost, or actor scenario cost can be
reported from this attempt.

## Why party cost remains inconclusive

The harness does expose useful bounded controls: `/process` accepts 1–1000 ordinary process ticks, and the Actor-led
Party scenario accepts 1–5 followers.[^runtime-api] However, one party request creates separate fixtures for follow,
owner-target, actor-target, and leash proofs, then destroys them before returning.[^party-runtime] Its reported elapsed
time therefore is not steady-state cost, and external memory samples are likely to miss the short-lived fixtures.

The current evidence distinguishes three states only as follows:

| State | Evidence now | Required next measurement |
|---|---|---|
| Inactive durable actor | Tables exist; this snapshot has zero rows and no actor workload. | Populate bounded inert rows in an isolated/cleanable fixture and compare storage plus polling queries. |
| Idle materialized actor/party | Inconclusive; zone could not boot and fixture cannot persist. | Hold a party alive without activity; sample zone-process RSS/PSS and CPU against a matched booted-zone control. |
| Active Actor-led Party | Inconclusive; scenario mixes setup, four proofs, processing, sleeps, and teardown. | Drive fixed tick/event/action workloads against persistent 1/3/5-follower parties over multiple trials. |

## Measurement seam to build before setting budgets

Add a measurement-only Zone Harness lifecycle that can create, inspect, process, and explicitly destroy a bounded
Actor-led Party while leaving it materialized between requests. It should support a matched zero-party control and
multiple parties, expose entity/party counts, and keep all fixture state in memory. Pair it with a portless, explicitly
named one-off container or an inside-container sampler that records:

1. monotonic startup/health and request wall times;
2. zone-process user/system CPU ticks plus RSS and, where available, `/proc/<pid>/smaps_rollup` PSS;
3. container cgroup CPU and memory at fixed intervals;
4. 0/1/3/5 followers, idle and fixed tick/event workloads, at least three trials each;
5. one versus two zone processes, measured separately from party deltas;
6. database statement/row deltas around the bounded run and exact cleanup evidence.

Fix the validation runtime's database-host wiring before using that runner. Rivervale/Misty Thicket activation and
cross-zone handoff remain separate later measurements: the canonical harness is single-zone and cannot currently prove
world-driven zone activation or party persistence across a zone line.[^process]

## Reproduction commands

Preflight and baseline:

```sh
./scripts/check-akkstack-contract.sh --stack validation
uptime
free -b
docker stats --no-stream --format '{{.Name}}\t{{.CPUPerc}}\t{{.MemUsage}}\t{{.MemPerc}}\t{{.PIDs}}' \
  bump-akk-stack-validation-mariadb-1
```

The three database samples repeated the `docker stats` command at ten-second intervals and read `/proc/loadavg`.
Actor row counts were queried inside MariaDB using the container's existing environment variables, without displaying
credentials:

```sh
cd ../bump-akk-stack-validation
docker-compose -f docker-compose.yml -f docker-compose.dev.yml exec -T mariadb bash -lc \
  'mysql -uroot -p"$MYSQL_ROOT_PASSWORD" -h localhost "$MYSQL_DATABASE" -Nse \
  "SELECT \"actor_profiles\",COUNT(*) FROM actor_profiles
   UNION ALL SELECT \"actor_status\",COUNT(*) FROM actor_status
   UNION ALL SELECT \"actor_action_queue\",COUNT(*) FROM actor_action_queue
   UNION ALL SELECT \"actor_events\",COUNT(*) FROM actor_events;"'
```

Cleanup verification: after the failed canonical trial and an earlier failed measurement-runner attempt, all one-off
`eqemu-server` containers were force-removed. A final Compose-label query returned no validation `eqemu-server`
containers. Only the pre-existing persistent MariaDB and fail2ban containers remained; actor-table row counts remained
zero.

[^process]: [`docs/testing/process.md`](https://github.com/thunderbump/bump-EQEmu/blob/9ec39551c046bad3b4e94d6435f09a1073ba5540/docs/testing/process.md), “Environment Contract” and “Tier 3: Zone Harness Validation”.
[^smoke]: [`scripts/smoke-zone-harness.sh`](https://github.com/thunderbump/bump-EQEmu/blob/9ec39551c046bad3b4e94d6435f09a1073ba5540/scripts/smoke-zone-harness.sh), portless override, temporary runtime, health wait, scenarios, shutdown, and `--rm` cleanup.
[^runtime-api]: [`zone/harness/zone_harness_runtime.cpp`](https://github.com/thunderbump/bump-EQEmu/blob/9ec39551c046bad3b4e94d6435f09a1073ba5540/zone/harness/zone_harness_runtime.cpp), `ProcessTicks`, `RunActorLedBotPartyScenario`, and `ProcessOneTick`; [`zone/harness/zone_harness_http.cpp`](https://github.com/thunderbump/bump-EQEmu/blob/9ec39551c046bad3b4e94d6435f09a1073ba5540/zone/harness/zone_harness_http.cpp), `/process` and Actor-led Party handlers.
[^party-runtime]: [`zone/harness/zone_harness_runtime.cpp`](https://github.com/thunderbump/bump-EQEmu/blob/9ec39551c046bad3b4e94d6435f09a1073ba5540/zone/harness/zone_harness_runtime.cpp), the Actor-led Party scenario's follow, owner-target, actor-target, and leash fixture scopes; [`docs/autonomous-actors/actor-led-bot-party-spike.md`](https://github.com/thunderbump/bump-EQEmu/blob/9ec39551c046bad3b4e94d6435f09a1073ba5540/docs/autonomous-actors/actor-led-bot-party-spike.md), intended bounded proof and limitations.

# Isolated client collision A/B preparation

Owned scope is this directory only. Baseline Server source is
`3c2f12b8fc8e12cdc9cca7e0ab753fd22a8ad597`. Retained runtime binaries are
`4ce5d7391a048f609b037817b772d6c15ea482d6`, not 3c2f12b8.
Central Beads `central-ysg5` owns preparation; integration `central-cs66` stays open.
Operations publication is reserved by the independent Client research thread.
This pinned report is an interim handoff for parent consolidation, not its replacement.

## Implemented check

`run.py` verifies the selected client, both sets of Server executables, every
public fixture manifest input and the immutable runtime image. It captures the
committed harness identity and freezes the executed worker script. It only accepts
this task's rootless socket and refuses dirty source or an existing output run.
`preflight.py` runs inside a disposable container at 2 GiB, no swap, one CPU,
network disabled and no published ports. It hashes inputs again, attempts client
startup without arguments, and inspects both loginserver dependency closures with
`ldd -r`. Client usage exit 2 is expected; failure to reach usage is a loader failure.
No profile, token, account or password is read or generated. No world, DB, client
login or gameplay is started. Loader success would still not prove login readiness
or OpenSSL provider initialization. The launcher has no gameplay execution path.

From a clean committed checkout, after preparing the documented rootless daemon:

```sh
python3 tests/client_collision_ab/run.py \
  --socket unix:///run/user/1000/eqemu-client-collision-ab-20261114.sock
```

Artifacts are under
`~/.local/state/eqemu-map-translation/client-collision-ab-20261114/preflight`.
The exact command, inputs, process exit, container inspect, cgroup readings,
loader stdout/stderr and container removal receipt are retained. A failing closure
is a blocked client experiment, never an OFF/ON backend verdict. The operator must
stop the task daemon and check socket/containers after the launcher exits.

## Prepared direct query fixture

`2011.pl` is a prepared replacement for the public Qeynos2 Rat's fixture quest,
not an installed or executed script. It logs production `FindGroundZ` and
`CheckLoSToLoc` results at two identical endpoints per backend, with source position,
size, target position, target size and timestamps. These are diagnostic server
facts, never ordinary client perception. It needs quest-debug logging enabled,
the one retained Rat spawn, and its timer retired before shutdown. Preflight does
not load or syntax-qualify this quest in Server.

Verified source chain at the baseline:

- `zone/perl_mob.cpp` exports `FindGroundZ` and `CheckLoSToLoc`.
- `zone/mob.cpp::FindGroundZ` queries `zone->zonemap->FindBestZ`, with origin
  `z = NPC.GetZ() + offset`. The offset 100 is not an absolute query height.
- `zone/aggro.cpp::CheckLosFN` adds source/target size-dependent eye heights,
  then calls `zone->zonemap->CheckLoS`. Record the raw endpoints and sizes;
  do not compare against an unadjusted client ray.

The endpoints are a small survey, not a qualified wall/clear LoS control. A later
worker must prove positive and negative controls, loaded-map/backend identity and
same source/target coordinates before reporting LoS parity or coverage. This
quest alone does not prove that the intended Rust branch was called at runtime.
Retained build feature/symbol proof is in `tests/map_reload/RESULTS.md` and
`tests/map_server_validation/RESULTS.md`; preserve its actual source identity.

## Pending ordinary client experiment

First qualify a runtime closure that can load the exact CPU binary, including
its glibc requirements. Then add a guest-only worker using the retained public
PEQ archive, fixed import order, schema 221 tables and version 9328/0/0. Existing
`tests/map_server_validation/runtime.py` starts world/zones but no loginserver.
The private login path still needs `login.json`, opcode files, login schema,
OpenSSL providers and ordinary account creation tested; dependency resolution
alone is insufficient. Never use shared login or copy a player DB.

Keep DB, loginserver, world, one Qeynos2 zone and client in one task-owned
network namespace with no host publication. Advertise 127.0.0.1 consistently:
RoF2 login 5999, world 9000, explicit zone 7001. Disable the login web API.
Use world `Client collision AB private`, host `127.0.0.1`, account `clientab`,
character `Clientab`, control port 18766. These identities are reserved in this
plan only; nothing has registered or used them. Generate a fresh disposable
password and >=32-character token inside that namespace, in mode-600 profile
files. Never borrow the existing service's profile/control port/token. Retain
private identity receipts without emitting credentials.

Run C++ OFF then Rust ON serially with fresh equal DBs, same maps/client binary,
normal character creation, equal starting state and identical bounded commands.
Before every action, require exact expected world, zone `qeynos2`, geometry SHA,
ready state, session and zone epoch. Abort on mismatch. Start with read-only
state/events, then at most a short dry move and stop; no combat, chat, merchants
or loot. A qualified route/wall/ramp/ledge plan remains pending. The known stopping
overshoot/warp means stopping cannot be scored as backend correctness.

Record predicted feet, `applied_local` receipts, independently observed server
positions/corrections and direct server ground/LoS query logs separately.
`Handle_OP_ClientUpdate` ordinarily accepts positions without a map raycast;
walking and local wall blocking therefore provide client smoke evidence only.
No privileged observer data may become client perception. No normal EQ observer
was launched; a human-controlled independent client remains a later gate.

Empty Rust engines, NaN load rejection and failed-reload retention need separate
private fixtures. Never run an empty C++ engine or relabel NaN rejection as empty.
No such state or broader gameplay/sanitizer/staging qualification is claimed here.

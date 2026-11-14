# Isolated client collision A/B preparation

Owned scope is this directory only. Baseline Server source is
`3c2f12b8fc8e12cdc9cca7e0ab753fd22a8ad597`. Retained runtime binaries are
`4ce5d7391a048f609b037817b772d6c15ea482d6`, not 3c2f12b8.
Central Beads `central-ysg5` owns preparation; integration `central-cs66` stays open.
Operations publication is reserved by the independent Client research thread.
This pinned report is an interim handoff for parent consolidation, not its replacement.

## Executed scope and commands

See [RESULTS.md](RESULTS.md) and [results.json](results.json) for observed outcomes.
`run.py` pins all fixture inputs, Server/client binaries and scripts, verifies a
clean committed harness, and accepts only the task's rootless socket. `preflight`
checks client loading and both loginserver linked closures inside the immutable
runtime image. `OFF` and `ON` each initialize a fresh public PEQ DB, import the
source-owned login schema, pre-register only the private world, enable local
account creation, and run login/world/Qeynos2/client serially in one namespace.
The container has networking disabled, no host ports, 2 GiB/no swap and one CPU.

```sh
python3 tests/client_collision_ab/run.py --socket unix:///run/user/1000/eqemu-client-collision-ab-20261114.sock --stage preflight
python3 tests/client_collision_ab/run.py --socket unix:///run/user/1000/eqemu-client-collision-ab-20261114.sock --stage OFF
python3 tests/client_collision_ab/run.py --socket unix:///run/user/1000/eqemu-client-collision-ab-20261114.sock --stage ON
```

The launcher refuses existing output names, preserving first failures. This
retained run has already used all three output names; reproducing requires a
reviewed new output suffix in the harness. Prepare the unique daemon through
Stack's `validation/rootless-prototype/run` systemd user-service recipe. Use an
independent store/socket/unit, offline verified OCI import, 2 GiB/no swap, and
stop the daemon afterward. No daemon lifecycle is hidden in `run.py`.

Private artifacts live at
`~/.local/state/eqemu-map-translation/client-collision-ab-20261114`. Each run
retains exact Docker argv, committed harness and frozen script hashes, inputs,
process-bound `/proc/PID/exe` hashes, stdout/stderr, API state, resource limits,
process exits, and container removal. Private DBs/logs can contain generated
credentials/authentication material and must not be published. No existing
service profile, token or control port is read by this harness.

## Production query fixture

`2011.pl` replaces only the disposable public Qeynos2 Rat quest. It calls the
production Perl `FindGroundZ` and `CheckLoSToLoc` bindings at two fixed endpoints,
logging source/target coordinates, source/target size, time, ground and LoS.
These server diagnostics are never client perception. The worker enables quest
debug logging, retains one original Rat, and retires its timer before shutdown.

The source chain is `zone/perl_mob.cpp` to `zone/mob.cpp::FindGroundZ` to
`Map::FindBestZ`, and `zone/aggro.cpp::CheckLosFN` to `Map::CheckLoS`.
Ground origin is `NPC.GetZ()+100`; LoS adds size-dependent source/target eye
heights. The two endpoints survey clear LoS only, with no blocked control or
client-coordinate correlation. Process hash plus qualified OFF/ON build proof
identifies the selected implementation. No new Rust core call counter exists.
See `tests/map_reload/RESULTS.md` and `tests/map_server_validation/RESULTS.md`
for the retained feature/symbol/build provenance at the actual binary source.

## Pending ordinary client experiment

The closure and ordinary isolated login path are now exercised. The world is
`Client collision AB private`, host `127.0.0.1`, RoF2 login 5999, world 9000,
zone 7001, account `clientab`, character `Clientab`, control port 18766. Private
password/token files are generated inside each container, mode 600. The expected
world selection guard and Qeynos2 geometry guard fail closed. No player movement
is implemented. Runtime shutdown errors must be investigated before broader
qualification. World, DB, source inputs and client profile remain disposable.

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

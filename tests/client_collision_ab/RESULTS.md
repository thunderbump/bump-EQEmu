# 2026-11-14 isolated client collision A/B checkpoint

Both retained backends reached ordinary Qeynos2 Ready using the unchanged qualified
CPU client. Two fixed production Server ground/LoS survey cases matched exactly.
No movement command was sent. Shutdown failed the orderly-exit gate in both arms;
this is a partial checkpoint, not staging or gameplay qualification.

## Identities and evidence

Server baseline `3c2f12b8fc8e12cdc9cca7e0ab753fd22a8ad597`; actual retained binary
source `4ce5d7391a048f609b037817b772d6c15ea482d6`. Final executed harness
`fda63e552` is pinned in each input receipt. No rebuild or production source edit.
Process-bound zone SHA256 OFF
`7a0cb456c697c3fad5c5350cf84979f23b2c7d7940399ac16fce9171a8fc5b81`, ON
`18025f30ef137f235e23598258100e1edf59db17ea40858075ca64876e448d90`.
The full executable hashes, selected image, API snapshot and resource readings
are in [results.json](results.json). Retained feature/symbol/build proof is in
[map reload qualification](../map_reload/RESULTS.md) and
[full Server qualification](../map_server_validation/RESULTS.md).

Client source `d034b7eafae6a0c9cbd4461da6a09a847d61ba4a`, SHA256
`6271c614d1668d2966f68867e2cdab50df6b94f4c8339a37c1750c564c460505`.
The independent research found its tree identical to fork main e9edb517; no Client
upgrade or source modification occurred. Preflight found runtime glibc 2.36,
client usage exit 2, and resolved OFF/ON loginserver linked closures.

Runtime image
`sha256:93429edbf01c1631611af6c88220405136072afd78dc65b6ebf47415b54c38e1`.
All 23 retained OCI blobs verified before importing into a new task-owned store.
Public fixture `peq-1787356814.zip`, fixed import order, 221 tables and version
9328/0/0. All manifest members and original maps verified. Qeynos2 SHA256
`040343c54e3b02bfd1637ce8fca8839dd676e17c41fb24c042ccfa0bd649435c`;
PoK `794b618d86852b47bd593424129aaf34eee326e5e3fec7290aeeb0e32bc41d20`.
PoK was not booted or entered in this experiment.

Each arm used a fresh DB and ordinary account/character creation with the same
human monk settings. World `Client collision AB private`, expected host
`127.0.0.1`, account `clientab`, character `Clientab`, login 5999, world 9000,
zone 7001, authenticated control 18766, all inside one network-disabled container.
The profile was freshly generated mode 600, with unique disposable secrets.
No host ports or shared login registration. Normal character creation placed the
character at server origin `[288,344,3.75]`; API feet were local prediction.
The exact expected-world guard selected the private entry; zone/geometry guards
then checked `qeynos2` and the pinned `sha256:` digest before recording Ready.

## Observations and limits

The original Rat's production Perl queries used raw source
`[-935,1194,9.10000038146973]`, source size 7, target size 6. Ground offset 100
is relative to source Z; `CheckLoSToLoc` adds native eye heights before Map LoS.
Target `[-935,1194,0]` returned ground `7.62939453125e-06`, LoS 1.
Target `[-925,1194,0]` returned ground `1.52587890625e-05`, LoS 1.
Each final arm recorded four query lines, two observations per endpoint, with
identical inputs and results. Loaded V2 map messages and `/proc/PID/exe` hashes
bind the observations to the retained qualified binaries. No new Rust core
call-counter or combat/pathing caller trace was added.

These are direct Server binding observations, independent of local client LoS.
Both targets were clear. No blocked control, wall/ramp/ledge route, NPC route,
combat caller, client/server pose correlation, movement packet receipt, stop
behavior, empty/NaN/reload state or ordinary human observer was tested. The query
points are not the client start position. Client Ready proves an ordinary session
and local geometry readiness; it does not establish server collision acceptance.
The known stop overshoot/warp remains unresolved and was not exercised.

## First failures and cleanup

`OFF-runtime` authenticated the generated account, but auto-registration advertised
world ID 0 and the client refused it. `loginserver/world_server.cpp` assigns the
registered branch's ID but leaves the first auto-registration ID unchanged.
The fixture pre-registers only its own private world row to use the existing
normal registration path. Production loginserver was not repaired.

`OFF-runtime-registered` reached Ready, then the harness incorrectly compared a
bare expected hash with the API's `sha256:` digest. The guard was corrected, the
failure retained, and final arms used identical corrected worker bytes. These
fixture/harness changes were reviewed locally and committed before execution.
No source bytes or assertions were changed to hide a production discrepancy.

Final OFF took 34.17s, ON 32.91s; both client/zone/DB exits were 0, world exit 1,
loginserver exit -15. World shutdown logged `uv_udp_send() failed: invalid argument`
and unsuccessful crash-backtrace attachment. Loginserver has no installed signal
handler in inspected main; SIGTERM therefore remains a signal exit. All children
were reaped and containers removed, but this is not an orderly shutdown pass.
Both launcher exits were 1 as intended. No runtime retry repaired that failure.

Runtime peaks OFF 1,288,007,680 bytes, ON 1,287,606,272 bytes; both had 2 GiB/no
swap limits and zero runtime max/OOM events. The separate daemon unit reached its
2 GiB cap during image import/cache work, recording 8,468 max events and zero
OOM/kills. No memory increase, host install, sudo or security change.

All task containers are removed; own daemon inactive and socket absent. New
store, private DBs/profiles, binaries and raw evidence remain under
`~/.local/state/eqemu-map-translation/client-collision-ab-20261114`. Original map
hashes remained unchanged. Existing headless service, player, live staging and
shared LAN ports were untouched. Delete this artifact root only after retention
review; preserve first-failure evidence and never publish private raw DB/logs.

Central `central-ysg5` and integration `central-cs66` remain open for parent
acceptance. Next useful work is an isolated shutdown reproducer, then paired
blocked/clear server controls and caller coverage, before bounded movement with
an independent ordinary client observer. No merge/default-on/deployment decision
follows from this checkpoint.

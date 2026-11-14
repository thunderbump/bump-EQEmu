# 2026-11-14 bounded second checkpoint

Both backends completed canonical clear/blocked production Server queries, one
100 ms dry client move plus stop, and independent server entity pose sampling.
Both failed orderly shutdown. This is useful bounded evidence, not full client,
parity, staging, gameplay, or ordinary-observer qualification.

Executed harness `7621b0e4afc396fb33fbd853fb79fd5fbb8746f8` was committed and pushed
before both final arms. PR115 targets PR114's verified actual head branch
`t3/headless-client-collision-ab`, baseline `ff3f0675ff1003977007816a86ad31552215e8e8`.
Only this test directory changed. Server binary source remains
`4ce5d7391a048f609b037817b772d6c15ea482d6`; client source remains
`d034b7eafae6a0c9cbd4461da6a09a847d61ba4a`. All process hashes, script hashes,
command IDs, timestamps, input-receipt hashes and resource results are in
[controls-results.json](controls-results.json). Prior RESULTS.md/results.json
and all private first failures remain untouched.

## Canonical server controls

One fixture Rat, size 6, is held at raw model origin `[297.75,344,3.75]` by its
owned quest. Its spawn grid, aggro radius, assistance, faction and run speed are
disabled only in the private SQL fixture. Each timer resets its pose immediately
before calls to production Perl FindGroundZ and CheckLoSToLoc. Source eye is
`[297.75,344,6.44999980926514]`, using Mob's float32 HEAD_POSITION 0.9;
target size 6 gives target eye Z=5.25 through SEE_POSITION 0.5.

| ID | Raw target | Ground OFF = ON | LoS OFF = ON |
| --- | --- | --- | --- |
| clear | `[288,344,3.75]` | 19.5276260375977 | 1 |
| blocked | `[320,344,3.75]` | 20 | 0 |

The native client route fixture identifies the east room wall at body contact
x=304.825. OFF established actual blocked geometry before ON, with no endpoint
changes after candidate execution. All final raw query lines were parsed again
and required stable inputs/results, both IDs and exact OFF/ON values. No global
epsilon. FindGroundZ offset +100 selects overhead surfaces at these endpoints;
these ground-query values do not represent client standing height. Logs include
raw endpoints, eye conventions, sizes and timestamps; input/process receipts pin
map SHA and backend binary. This is direct Server binding coverage, not normal
client perception, NPC pathing or combat-caller coverage.

## Movement and server observation

Fresh equal DB/ordinary account and human monk creation per arm; private world
`Client collision AB private`, 127.0.0.1, Qeynos2. Each generated 0600 profile
and secret was private to authenticated port 18766 inside the network-disabled
container. No shared client/service/profile, staging, host ports or real players.
The identical map/client binaries and expected-world selection guard were retained.

Admission waited for grounded dry feet `[288,344,0]`, matching session/zone epoch,
entity/account/character, Ready geometry and map SHA, attack off, fresh server
aggro=0, <=250 ms network tick and <=5 s inbound/link age. The local safe box was
x=[287,293], y=[343,345], z=[-.02,.02], movement deadline 12 s. One unique-ID
`move_for(east=1,north=0,duration_ms=100)` then unique-ID `stop` each reached
`applied_local`. No other movement, combat, chat, loot, merchant or zone action.
Observe for 3.2 seconds after stop, exceeding two 1300 ms idle keepalive intervals.

OFF last sampled local feet `[290.81292724609375,344,0]`; independently queried
server model origin `[290.812927246094,344,3.125]`. ON local feet
`[290.8118591308594,344,0]`; server origin `[290.811859130859,344,3.125]`.
Each arm recorded four distinct one-second server pose timestamps with aggro=0.
The server starts at Z=3.75 and later records client-reported model Z=feet+3.125.
These samples are independent server entity reads, not packet-level receipts.
They demonstrate reported position recording, not server body collision.

Small local displacement differences are observations from short nondeterministic
runs. No exact trajectory/performance claim or attribution to Rust. Known stop
overshoot/warp and ordinary-client appearance remain unqualified without Brian's
human observer. No upstream upgrade, client fix or proposed diagnostic seam.

## Shutdown, failures, and resources

Both normal camps logged OP_Logout and OP_SessionDisconnect and exited client 0
before dependent teardown. Own quest timer quiescence was verified in both arms.
Both final process exits: client 0, zone 0, world 1, loginserver -15, DB 0.
World still logged `uv_udp_send() failed: invalid argument` and failed crash trace
attachment. Loginserver remains a signal exit. Both launchers returned 1, as
required. No production lifecycle repair, suppression, sanitizer or green retry.

Preserved second-checkpoint failures: OFF-controls-v1 rejected Perl's blank false
serialization before movement; fixture now emits numeric 0/1. OFF-controls-v2
aborted idle observation after two seconds of inbound silence, sending emergency
stop. The revised five-second bound is stricter than the client's existing
15-second Ready link bound and permits the planned idle sampling. Neither failure
is erased or presented as a backend mismatch. Final paired label controls-v3
uses byte-identical worker/quest/admission scripts and the same harness commit.

Runtime peaks: OFF 1,302,122,496 bytes; ON 1,278,550,016 bytes, 2 GiB/no swap,
zero runtime max/OOM/kill events. Separate daemon/import unit reached its 2 GiB
cap, 8,637 max events, zero OOM/kills. All 23 OCI blobs verified before import of
unchanged image sha256:93429edbf01c1631611af6c88220405136072afd78dc65b6ebf47415b54c38e1.
No memory increase, host install, sudo or security change. Containers removed,
children reaped, daemon inactive, own socket absent; original map hashes unchanged.

Private raw receipts, generated DBs/profiles, input/command manifests, stdout,
server query/pose logs, container cleanup, cgroup counters and OCI verification
remain under `~/.local/state/eqemu-map-translation/client-collision-ab-controls-20261114`.
Large outputs are outside Git, regenerable with the frozen input closure and fresh
run labels; preserve failure evidence and never publish private credentials/logs.
Commands are in README; tests are harmless host JSON/admission fixtures. Review
against ff3 verified owned-directory scope, fail-closed guards, unchanged binaries,
paired query identities and separate verdicts; three pure control tests passed.

Parent accepts/closes central-i07a; preparation central-ysg5 and integration
central-cs66 remain open. Next gates include production shutdown diagnostics,
Brian's ordinary observer, broader NPC pathing/combat caller coverage and deferred
empty/invalid/reload acceptance. No PoK movement or deployment/default-on/merge.

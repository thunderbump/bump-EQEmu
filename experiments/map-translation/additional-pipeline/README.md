# Additional standalone pipeline evidence

Central Bead central-9m80. Owned opt-in harness only, based on parent
cd66dd0a6234fdb519046716041e0aaf231321c0. This commit does NOT contain its
module dependency. worker.py verifies and extracts a Git archive of external
revision 60346d90b79b9c2b1a86590125b955219ecd1a41, then overlays ONLY this
committed directory from a second authenticated archive. Both commit/tree and
archive SHA256 identities are recorded. Never edit that dependency.

seed.txt was written before select.py ran. manifest.json freezes sorted eligible
paths, header census, ranking algorithm and both selected identities. No reroll.
Headers are eligibility declarations, not decoder coverage. Other resource
bounds remain unqualified. Original download provenance is unknown.

Each raw input is copied unchanged into the owned artifact root, checked against
source, deployed and prior inventory. The unchanged production C++ capture and
Rust map-poc decode those same bytes twice. Ordered schema-1 float bits, indices,
counts and input identity must match exactly; numeric diagnostics cannot accept
a mismatch. C++ geometry drives the unchanged C++ reference query recipe;
Rust geometry independently drives the unchanged Rust raycaster. Reference
endpoint bits, order, categories, input and geometry hashes are recorded.
The independent streaming comparator checks every result field exactly.

This extends the earlier shared-C++-geometry corpus with two complete decoder
pipelines. It proves neither all-ray equivalence, global floating-point
portability, direct C++ triangle ID equality nor production/CXX/client behavior.
No algorithms, defaults, assets or integration are changed. No Rust ASan claim.

Run only through a dedicated rootless Docker user-manager service with the
verified offline image 0aeb54f14590bf1631a27f60b3e7b7eed6dde549f08ba1f5738282e59d4cea1d.
Host Python performs inventory/archive/transport only. Native/Cargo runs inside
network-disabled 2 GiB/no-swap, two CPU, 128 PID workers, serially.
Stage timeout is 120 seconds; build/container timeout 600 seconds. Limits do not
increase on failure. Cgroup events/peak, GNU time RSS, exits and cleanup persist.
Existing analytic fixtures, decoder CLI preservation/identity checks, raycaster
reader/replay identity controls and comparator known mutations are reused.

Reproduction: use the exact unique service/socket/store names in commands.txt,
verified pinned importer, and a clean committed harness. Run `python3
experiments/map-translation/additional-pipeline/worker.py`. Choose a new exclusive
artifact root by changing STATE and SOCKET for a new run. Selection must remain
frozen; authenticate the same two input copies rather than rerunning selection.
Reports and successful-output cleanup receipts remain tracked; large artifacts
and build input archives are outside Git. Failed outputs remain for diagnosis.
Timing/storage/RSS describe this run, not a stable benchmark.

## 2026-11-11 results

Executed harness b3ad0bf5e39eefe798771db21e772ce52fe1dc1a, pushed before
execution. Reports-only commits do not change executable inputs. External module
60346d90b79b9c2b1a86590125b955219ecd1a41 remains separate. See reports/source.json
for both trees and archive identities. Direct review checked the diff against
cd66dd0 and the adapted guest against pinned 60346; no subagents were spawned.

440 eligible paths, SHA256 b13d12dc7f4f2e40794da439284640f8c82f054454a3c7bc07c1f4b690cbaf7b.
Frozen seed: `central-9m80/2026-11-11/additional-pipeline/v1` plus newline.
Hollowshade and Nurga selected, no reroll. No asset repair or algorithm edit.

| Map | Ordered vertices / indices | Exact pipeline queries | Hits | Misses |
| --- | --- | --- | --- | --- |
| hollowshade | 850984 / 1215309 | 5504 | 3740 | 1764 |
| nurga | 24494 / 55029 | 5504 | 2344 | 3160 |

Both decoders repeated identically. All schema/count/identity/index/float-bit
comparisons exact. Zero differing coordinates, signed-zero changes, absolute,
relative or ULP drift. All 11008 pipeline queries exact in classification and
seven hit fields, zero numeric drift. Endpoint binary SHA256 and categories,
C++ source geometry and Rust source geometry hashes are in reports/summary.json.
These two new full decoder pipeline checks add evidence beyond the 513 exact
sampled shared-geometry corpus comparisons and four explicit data contracts.
The earlier 2.82 million corpus queries did not independently exercise Rust decode.

Controls passed: 4 C++ capture tests, 8 Rust decoder fixture/CLI tests, 10 Rust
raycaster core tests and reused reader/replay/comparator mutations. Cargo decoder
unit suite has zero tests; its evidence comes from the eight Python fixture tests.
First attempt stopped before compile on a pre-existing results directory;
corrected mkdir to be idempotent and pushed before final run. Failed attempt
receipts remain outside Git. No map was replaced after that tooling failure.

Final containers exited 0. Build elapsed 5.35 s; Hollowshade 2.96 s; Nurga 0.42 s.
Stage time/RSS logs are retained. Largest native RSS was C++ query 51464 KiB;
Hollowshade Rust decode/query RSS 21440/40740 KiB; Nurga 3452/7092 KiB.
Cgroup peaks: build 127827968 B, Hollowshade 692494336 B including Python geometry
diagnostics, Nurga 34062336 B. OOM counters zero. No timeout or cap increase.
Host admission showed 118 GiB free disk and about 23 GiB available memory.
Execution artifacts before cleanup were 202049558 B excluding daemon store;
35306621 B of successful derived output removed with authenticated receipts.
Both raw input copies, 1307825 B total, remain. Module/harness archives and their
extractions removed after identity recording; regenerate with git archive at
recorded commits. Owned containers, images, service, store, socket and exec-root
were cleaned, with receipts in reports/cleanup-worker.json. Shared resources and
prior reports were untouched. No PR, merge, game deployment or watch was needed.

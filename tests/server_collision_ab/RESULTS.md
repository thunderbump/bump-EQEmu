# 2026-11-15 ordinary NPC collision completion

All six final qualification runs passed: Qeynos2 OFF/ON/OFF-repeat `native-v5`,
then PoK OFF/ON/OFF-repeat `native-v3`. Every worker exited 0, each native
world/zone/DB child exited 0, added timers retired and task containers were removed.
This reduced topology does not resolve full login/client shutdown bug central-866g.

Production build source is `1bcb08a99` with matched incremental full-target OFF/ON
builds and utilities. Builder peak 2677415936 bytes under 4 GiB/no swap, one job.
Both build phases used the same source/dependency closure and immutable builder.
Runtime was network-none/no host ports, 2 GiB/no swap, one CPU. All final runs
reported zero OOM events. See `results.json` for full executed harness/build SHAs,
executable hashes, per-arm peaks/exits, native caller counts and final poses.
This results-document commit is later than the executed harness commits.

Qeynos2: 48 native observations and nine poses per arm. Three fixed ground/LoS
cases, three route floor samples, native admission and adjusted nodes, movement
dispatch and ordinary NPC FixZ were observed. Route floors were
3.34708261489868, 10.111120223999, 10.3785705566406. Final pose was
264.237609863281,283.493774414062,14.1285705566406. Observed GetZOffset was 3.75
for this fixture's actual model/size, not an assumed race-only offset.

PoK: 58 native observations and 14 poses per arm. Public grid 3 entries 41,42,43
provided a genuine floor-height transition: -159.466003417969, -145.878997802734,
-120. Final pose was 426.196990966797,-560.963989257812,-116.25. The route rises
far beyond the actor interface's max6u gate; this is ordinary NPC coverage only.
PoK's negative LoS case demonstrates blocked static geometry, not a separately
identified vertical-wall triangle. The Qeynos2 wall control remains the suite's
wall-specific case. The fixed blocked-location ground query is not a route floor.

Across both triplets, fixed query identities/decisions/results, actual adjusted
Map endpoints/results, route admission/nodes and final pose matched exactly.
Moving timing varied; every pose was bound to an actual ground query and checked
against the route corridor plus native interpolation or FixZ. Native stair
interpolation can temporarily place poses above or below local stair surfaces;
this retained C++ behavior is not a claim of perfect floor contact or body sweep.
Stopped poses satisfied floor+observed-offset within the predeclared 0.05.
No numeric parity tolerance was relaxed. Float32 canonicalization binds alternate
Perl/printf decimal spellings of the same binary value, with a one-ULP rejection
control. Actor stop, combat, real client/player movement and sanitizers were not run.

Six server comparator test methods and three inherited client controls pass.
Independent Standards and Spec reviews found stale build admission, unbound
actual fixed/sample queries, insufficient traversal/state checks, timer cleanup
and stale comments. All concrete findings were fixed, including adversarial
wrong caller/backend/entity/input/nonfinite/state/teleport controls, before final
qualification. No default CMake/CI/utility hookup, Rust default, native MMF cache,
Rust MMF bypass, Stack implementation or production deployment changed.

Original failures remain retained: flat DB-grid candidate and bare NavigateTo
being overridden by guard AI; wrong exported ground caller; float32 argument and
nonintegral decimal-binding errors; missing PoK quest directory. No failed run
was overwritten or reclassified as a pass. Two early Qeynos baseline attempts
briefly overlapped; final runs were serial and the launcher now requires an idle
owned daemon. All resources used the authorized task-owned rootless worker.

Artifacts: ~/.local/state/eqemu-map-translation/server-collision-npc-completion-20261115.
47 OCI blobs verified against full SHA256 before offline image import. 18557
current dependency files matched retained inputs. Source deltas, compile commands,
CMake flags, binary manifests, toolchain closure, cgroups, raw logs, DB fixtures and
cleanup receipts remain there. `summarize.py` regenerates the small public JSON.
Large/private artifacts stay outside Git. The old PR116 survey remains preserved.
Central central-j4sy stays open for parent acceptance.

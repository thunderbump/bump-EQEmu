# 2026-11-10 installed corpus result

Executed `c319cb077cbf5c2bfbf82544f3eab125e5e3277e`, on baseline
`e064bcf058d4d728d1d86be5fdb58290c7f7f568`. Later commits contain evidence only.
All 517 frozen paths have one final outcome. The installed corpus is **not fully
qualified**: 510 pass, seven blocked. No engine or decoder implementation changed.

- Base V2: 502 pass, five blocked. Base V1: four pass, two empty.
- Supplemental legacy V2: all four pass, retaining distinct relative paths.
- Compared queries: 2,805,033, including 1,724,067 hits and 1,080,966 misses.
  Zero classification or seven-field bit changes; zero signed-zero drift.
- Grieg's End: 5,504 queries, 3,164 hits, 2,340 misses, exact. Its input and
  8,048,520-byte mesh hashes reproduce the supplied prior evidence.
- Qeynos2 and PoKnowledge reproduce authenticated historical C++ traces.
  Their original release/download provenance remains incompletely documented.
- 56 group-declaring and 56 terrain-declaring V2 assets reached exact comparison
  through C++ decoding. This tests Rust raycasting beyond Rust geometry decoder
  format support. Header declarations do not prove parser branch execution.

Blocked paths, with full geometry hashes/counts in `summary.json`:

| Path | Outcome |
|---|---|
| base/breedinggrounds.map | C++ exported nonfinite geometry |
| base/chapterhouse.map | C++ exported nonfinite geometry |
| base/firiona_lit.map | Empty V1 geometry |
| base/warslikswood_lit.map | Empty V1 geometry |
| base/chelsithreborn.map | C++ adapter 4M-vertex count limit, before Rust |
| base/cityofbronze.map | C++ adapter 4M-vertex count limit, before Rust |
| base/riftseekers.map | C++ adapter 4M-vertex count limit, before Rust |

These are coverage failures, not seven confirmed module defects. The decoder
returned success on the two nonfinite exports; source data versus arithmetic
expansion remains unresolved. No candidate outcome exists for any blocked path.
The three large maps also exceed the unchanged Rust reader's vertex limit;
cityofbronze exceeds its 100MB byte limit. No safety limits were relaxed.

All ordinary stages stayed at 2 GiB with swap disabled. No crash, timeout or
Docker OOMKilled event occurred. Recorded cgroup peaks span 10,461,184 to
288,296,960 bytes, median 44,908,544. Per-asset elapsed median is 0.728 seconds,
p95 1.579, worst 2.351; summed asset time is 424.883 seconds. These short runs
are indicative resource observations, not comparative speed benchmarks.

The runner removed 5,776,438,696 successful derived bytes. Final per-asset
receipts and seven failed meshes retain 363,125,577 bytes. No failure mesh hit
the 2 GiB retention cap. Full evidence remains outside Git at
`~/.local/state/eqemu-map-translation/raycaster-corpus-20261110/final/`.
`assets/<relative-map-path-without-extension>/` contains each outcome, stage
logs, cgroup observations, container state and cleanup receipts. Failed paths
retain `geometry.mesh`; there are no mismatch replay packets because none of
the failures reached comparison. Successful geometry and replay artifacts can
be regenerated using the committed README command and exact manifest path.

The frozen collection and deployed counterpart match every path/size/hash and
the earlier census. Original inputs and all prior experiments remain intact.
The task-owned image/archive/source extraction cleanup has separate receipts;
the daemon was stopped after pruning its own images. Reproduction needs a new
task-owned daemon and verified image import, followed by the recorded command.
To reproduce exact execution, check out the executed SHA in a separate clean
worktree. A later evidence-only HEAD is intentionally a different resume identity.

Validation includes three host tooling tests, nine Rust tests, C++ analytic
controls, existing decoder controls, independent comparator mutations, one
analytic executable replay and nine executable reader/replay rejection controls.
A preliminary run was interrupted after 181 checkpoints; cleanup left no running
containers. Resuming it with changed source was rejected. Same-source interrupted
resume was not separately exercised. Source review fixed process-group timeout,
false-pass error handling, mutable image admission, transport timeout and census
allocation problems before this complete run.

Next, independently locate the nonfinite values in the two source assets and
qualify a narrowly scoped adapter/resource strategy for the three large meshes.
Do not raise Rust limits merely to obtain passes. If decoder replacement becomes
necessary, use an owned ordered Mesh boundary with independently checked small
fixtures and an independent selected-triangle oracle. Combining Rust decoder and
raycaster behind that boundary is a followup option, not authorized work here.
Current evidence does not implicate both modules or justify replacing both.
There is no whole-corpus ASan, database override audit, CXX lifetime, Map/cache,
concurrency or server/game qualification. This result is not ready-to-swap approval.

# 2026-11-10 standalone raycaster qualification

Executed and pushed source: `092b36782f758751ebcae5bab49a589c64e8ff81`.
Fixed baseline: `f2cd85d4161bf5d75bab54e2ab5a0e6c7bc303f2`.
Subsequent commits contain evidence/docs only. Central-y9uz remains in progress
for parent acceptance. No C++ engine, reference driver, geometry crate, corpus,
Map, MMF/cache, CXX or production deployment changes.

Final run `qualified-02` passed. Qeynos2: 5,504 queries, 3,231 hits, 2,273 misses.
PoK: 5,504 queries, 3,354 hits, 2,150 misses. Both the Rust candidate and fresh
optimized C++ reference matched the frozen ordinary reference exactly in hit
classification and all seven valid fields. Absolute, relative and ULP maxima
are zero, with zero signed-zero changes. Miss fields were never compared.
Original geometry bytes, endpoint bits, row order and category are authenticated;
reference traces were never overwritten or regenerated from Rust output.
No direct C++ triangle-ID parity is claimed.

Nine Rust unit tests pass, including 4,800 seeded f64 brute-force queries over
small synthetic meshes, 64 repeated construction/query/drop cycles, winding,
seams, degeneracy, overlap ordinal choice, wrap reset, adjacent determinant and
normal thresholds, signed-zero/epsilon boxes, short/zero and endpoint behavior.
The f64 oracle independently implements intersection math, using separate loops
and no production predicates/tree. Its tolerance applies only to distance for
ordinary selected geometry, 0.002 world units oblique and 0.0001 horizontal.
Explicit primitive/box fixtures determine boundary decisions, not that tolerance.
It does not universally model legacy tree pruning at ambiguous boundaries.
Nine candidate parser/replay rejection cases per map passed, plus six frozen
trace authentication mutations, 17 existing streaming comparator mutations and
four cross-zero ULP ordering controls. Perturbed geometry changes outcomes in
unit tests. Resource failures cover capacity overflow mapping, not exhaustive
allocator fault injection. No Rust sanitizer coverage is claimed. PR106's
C++ exit-time LSan result remains separate historical evidence.

Two read-only review rounds found no confirmed core arithmetic/traversal bug.
The first led to added oblique/negative-coordinate, threshold, box, split and
allocation tests. The second found missing immutable trace authentication and
incorrect cross-sign ULP ordering. Both were fixed before final execution.
The runner also decodes each output independently so strict disagreements retain
numeric reports. There were no failed candidate qualification controls in the
retained development/final runs; their directories remain available.

## Time and memory

Both binaries used release optimization, the same immutable 5,504 endpoint rows
per map, serial separate engine processes and equivalent binary output sizes.
Reference triangle IDs are sentinel values. Per-query clocks add overhead to
both adapters. These are short single runs, not stable speedup measurements.
Rust consumes owned arrays; the existing public C++ engine copies arrays and
then releases input storage. Allocator, input reader, lazy normal and build
strategies differ, so process peak is not a pure tree-size measurement.

Qeynos2 Rust/C++ query time: 3.386 / 3.382 ms. PoK: 12.932 / 12.484 ms.
Serialization was 0.218 / 0.215 ms and 0.229 / 0.256 ms respectively.
GNU time peak RSS Rust/C++: 6,324 / 12,096 KiB for Qeynos2;
40,336 / 47,576 KiB for PoK. Phase /proc high-water readings differ slightly
from GNU time accounting. Phase logs retain read/build/release observations.
Rust build took 0.0150 s Qeynos2 and 0.1297 s PoK. Inspect the reference phase
logs for separately measured C++ construction. Candidate compiled in 0.43 s,
peak 150,680 KiB; reference compilation took 0.85 s, peak 151,788 KiB, excluding
CMake configure. These small builds are not full Server build-cost estimates.

The worker admitted exactly 2,147,483,648 bytes, swap limit zero. Final observed
cgroup peak was 99,688,448 bytes; max/oom/oom_kill events remained zero. Cgroup
charged residency differs from per-process RSS and includes the full worker
lifecycle. No diagnostic process or >2 GiB admission was needed here. The earlier
instrumented C++ multi-GiB baseline is not production or Rust memory usage.

## Reproduction, retention and boundary

`command.json` is the exact host invocation; `source.json` pins source/tree/archive.
The parent README documents daemon startup/import and worker use. Immutable image
and verified blob identities are in `image-import.json`; Rust 1.99, release flags,
compiler commands, dynamic libraries, binary hashes, tests, strict/numeric outputs,
process exits, cgroup admission/counters and cleanup receipts are adjacent.
Full source archives, binaries, traces, query packets, container inspections and
logs remain outside Git under:
`~/.local/state/eqemu-map-translation/rust-raycaster-poc-20261110/`.
Final execution is `qualified-02`; development-01/02 and qualified-01 are retained.
All container processes exited zero and owned containers were removed. The owned
daemon was stopped, its pruned store and 2.71 GB transfer archive removed. Original
maps/geometry/traces and retained source OCI blobs were preserved. Retained owned
outputs totaled 181,980,246 bytes at cleanup; deleting this explicitly owned task
root later reclaims them. No other daemon/store/worktree was modified.

The accelerated endpoint contract remains box-history dependent, with strict
segment-box distance and positive triangle t; equal-distance lower ordinal choice
applies only to reached triangles. The existing C++ brute-force method uses strict
triangle distance and is not a universal boundary/tie oracle. Read the core README
before integration. Results support a separately scoped CXX/lifetime/interface
integration evaluation. They do not authorize deployment or establish whole-corpus,
other toolchain/architecture, cache/MMF, concurrent query or production correctness.

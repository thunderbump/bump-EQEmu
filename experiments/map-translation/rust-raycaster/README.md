# Standalone Rust raycaster PoC

Central Bead central-y9uz. Opt-in only. This crate consumes owned finite bounded
vertices and indices, outside the actor workspace and all default build paths.
No CXX, Map, cache, MMF or deployment changes. Port derived from the Ratcliff
raycaster in zone/raycast_mesh.cpp, retaining its attribution and repository license.

`Raycaster::build(Vec<[f32;3]>, Vec<u32>)` consumes geometry. `segment(&mut self,
from, to)` returns a checked `Option<Hit>` containing position, winding normal,
distance and original triangle ordinal. Empty geometry gives no hit or bounds.
Inputs are limited to four million vertices, twelve million indices and finite
coordinates of magnitude <= 1e8. Nodes are capped at 131071 and leaf references
at sixteen million. Fallible reserves report allocation errors; the caller's
original Vec allocation is outside this interface. Recursive depth is capped at
15, min leaf count is 4 and min axis width 0.01, matching production defaults.
Scratch is owned by the engine and visited stamps clear before wrap reuse.
The core and CLI forbid unsafe code and declare no dependencies or FFI. This
makes no claim that Rust std, the allocator or linked platform libraries contain
no unsafe implementation. No instrumented Rust ASan coverage is claimed.

## Preserved contract

The two-sided triangle predicate rejects determinants strictly between the
C++ double literals +/-0.00001, negative barycentrics, sums >1, and t<=0.
The normalized segment length below 1e-10 returns no hit. AABB queries require
distance >1e-5, test signed bits, expand other planes by 1e-5, and require box
intersection distance squared strictly less than nearest distance squared.
Thus a triangle at the endpoint can be accepted if its box was entered earlier,
but a planar box first reached at that endpoint is rejected. This is intentionally
not a uniform inclusive endpoint API. Equal triangle distances accept the lower
original ordinal only among candidates reached by the tree. Box pruning can
exclude tied candidates. Left-first traversal, longest-axis tie priority,
spanning-triangle duplication and child-bound clamping are preserved.
The C++ bruteForceRaycast strict-< endpoint behavior is not the universal oracle.
Normals follow winding and never face the ray. Normal calculation preserves the
float sum, double sqrt, narrowing and float reciprocal sequence of computePlane.
No arithmetic cleanup, blanket epsilon or fast-math is used.

## Independent checks and replay

Unit tests cover analytic triangles, two-sided/reversed winding, seam/adjacent
floats, edges, endpoints with different box histories, zero/short segments,
degenerate geometry, overlaps with opposite normals, visited wrap, invalid input,
allocation capacity overflow and repeated lifecycles. Seeded f64 brute force
independently computes intersections for small horizontal and general oblique
meshes, without calling production triangle/tree paths. It validates ordinary
geometry away from boundaries, with a measured distance allowance of 0.002
world units for oblique cases. Its f64 determinant cutoff is not a legacy boundary
oracle. Explicit threshold and box fixtures cover those decisions separately.
The seeded oracle does not promise equivalence where tree pruning is ambiguous.

The guest adapter reads the frozen C++ traces in order, validates their grammar
with the existing independent comparator, and packages the exact endpoint f32
bits with mesh identity and sequential row ordinals in EQRAYS01. Category and
original row position remain in the adapter and are restored unchanged. The
candidate rejects bad identity/count/length/order and emits initialized fields
only for hits in binary form, including diagnostic triangle ordinals. The adapter
converts them losslessly to hex traces and calls the unchanged streaming comparator.
No claim of direct C++ triangle-ID parity is made. Input mesh bytes must exactly
match the independently captured C++ mesh before execution. Comparator mutations,
mesh/parser and replay rejection controls are required. Exact failures remain
failures; a separate numeric report quantifies differences without acceptance.

## Disposable execution

Never run Cargo or candidate binaries on the host. `import_image.py` verifies
retained immutable OCI blobs from the original proof store and imports them into
this task's dedicated daemon, leaving the source store untouched. Start that
daemon with its own socket, data-root, exec-root, pidfile and rootlesskit state.
`worker.py` transports a clean committed archive, requires rootless Docker and
runs release Cargo frozen/offline in a network-disabled 2 GiB/no-swap container.
Use the immutable image sha256:0aeb54f14590bf1631a27f60b3e7b7eed6dde549f08ba1f5738282e59d4cea1d.
The worker output retains source/archive identity, image/container configuration,
verbose build logs, toolchain, libraries, binary hashes, tests, exact/numeric
reports, phase timings/RSS, GNU time peak RSS, cgroup counters, process exits and
container cleanup even when execution fails. Each map runs serially in its own
process. Binary output differs from the C++ text IO workload; do not declare a
speedup from end-to-end times. Reused ordinary C++ baseline timing provenance
remains at raycaster-memory-20261110/ordinary-01.

Host command, with absolute fresh output path:

```sh
python3 experiments/map-translation/rust-raycaster/worker.py \
 --socket unix:///run/user/1000/eqemu-rust-raycaster-poc.sock \
 --image sha256:0aeb54f14590bf1631a27f60b3e7b7eed6dde549f08ba1f5738282e59d4cea1d \
 --references /home/bump/.local/state/eqemu-map-translation/capture-tests-20261110/handoff-ordinary/results/capture \
 --rust-meshes /home/bump/.local/state/eqemu-map-translation/rust-poc-20261110/qualified/results/rust \
 --baseline /home/bump/.local/state/eqemu-map-translation/raycaster-memory-20261110/ordinary-01/results \
 --output /home/bump/.local/state/eqemu-map-translation/rust-raycaster-poc-20261110/qualified-01
```

Large meshes/traces/binaries remain outside Git. Only remove owned run directories,
containers, daemon/store and transfer archive after retrieval. Preserve original
maps, reference meshes/traces and prior evidence. Two maps and one toolchain do
not qualify a whole corpus, CXX lifetimes, production concurrency/cache or game
behavior. Any integration requires a separate scope and review.

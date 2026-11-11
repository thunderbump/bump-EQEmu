# Collision-query checkpoint

Opt-in central-854f evaluation. Configure this directory directly; it is outside
Server's default build, tests and CI. No CXX bridge or server startup is involved.

`driver.cpp` reads schema-1 EQMESH01 arrays, checks input identity, lengths,
nonempty bounded counts, finite coordinates and index bounds. Pair comparison
requires identical counts, indices and triangle order. It passes contiguous
float triples and unsigned indices to the production
`zone/raycast_mesh.cpp`, using the same default createRaycastMesh configuration
as `Map`. The unchanged production allocator is linked too. The only adapter
is `shim/common/eqemu_logsys.h`, which disables logging and supplies standard
headers. This does not evaluate production logging, MMF, cache or CXX ownership.

The API supplies hit/miss, position, winding normal and distance, with no public
triangle identifier. All seven hit fields must be bit-identical. Miss fields
are never inspected or serialized. The report also measures maximum absolute
field delta without relaxing equality. No tolerance is chosen.

Each map starts xorshift32 at `0x854f2026`, with explicit high-24-bit float
conversion. There are 2,000 spatial segments, 1,000 vertical segments and 200
outside segments. Another 256 evenly spaced triangles each supply interior,
edge, vertex, adjacent +X representable-coordinate variants, a 0.002-unit short
segment, a near-parallel segment and a zero-length segment. Degenerate or
nonfinite triangle normals are skipped. Each real sample produced 5,504 queries.
Edges include potential seams; real-map seam connectivity is not independently
classified. A separate analytic square explicitly tests its shared diagonal and
both adjacent representable coordinates. These samples are bounded, not exhaustive.

Analytic controls check the triangle plane, distance 2, +Z winding normal,
two-sided queries, misses, segment limits and zero-length behavior. A collinear
face must not invent a hit. Thirty-two cycles construct/query/release original
and translated meshes. Translating the plane by 10 must change a hit to a miss
in every cycle. Comparator controls change each of seven fields and hit state.
Seven reader/structure rejection controls run per map: schema, input identity,
index bounds, empty count, nonfinite coordinate, truncation and winding order.
Empty meshes are deliberately rejected before entering the engine.

## Reproduce in an authorized disposable worker

Keep the reviewed source committed and the checkout clean. On the host:

```sh
python3 experiments/map-translation/collision/worker.py \
  --socket unix:///run/user/1000/eqemu-map-collision.sock \
  --image sha256:0aeb54f14590bf1631a27f60b3e7b7eed6dde549f08ba1f5738282e59d4cea1d \
  --inputs /home/bump/.local/state/eqemu-map-translation/two-maps-v1/inputs \
  --references /home/bump/.local/state/eqemu-map-translation/capture-tests-20261110/handoff-ordinary/results/capture \
  --rust-meshes /home/bump/.local/state/eqemu-map-translation/rust-poc-20261110/qualified/results/rust \
  --output /home/bump/.local/state/eqemu-map-translation/collision-eval-20261110/CHOOSE-FRESH-RUN
```

Use a dedicated rootless daemon with this image imported. This checkpoint read
immutable retained OCI blobs into a new image archive and loaded it into its own
store; it did not start or modify the earlier daemon. No dependency fetching was
needed. Mesh directories must retain the original layout, with source.json and
container.json two levels above them. The worker copies that provenance into
reused-provenance.json. The reused Rust implementation is
`1fa4f48d1198188662e4239c94fc71754dad5895`; C++ capture reference is
`f3826005ce96820291c2cdba48e5fe06b6426cf0`.

The host only archives and transports files. Compilation and all candidate
execution run network-disabled inside the disposable container, with 2 CPUs,
2 GiB physical memory and 128 PIDs. Source/archive/image/compiler/compile command,
binary/library fingerprints, exact commands, input/payload hashes, per-process
wall time and peak RSS are retained. Hexfloat traces record endpoints, categories
and both outcomes. Trace hit fields appear only on hits. Small reports live in
`reports/`; large traces remain outside Git. Both input identities are verified
against the actual original map bytes before evaluation.

For diagnostics add `--sanitizers` and choose another output directory. The C++
driver, raycaster and allocator use ASan/UBSan, debug info, O1 and non-PIE. Rust
is not executed or instrumented here. Existing tests/sanitizer_controls.cpp is
compiled independently and must pass its safe control and produce symbolized
use-after-free, out-of-bounds, overflow and leak diagnostics, each exiting 1.
Installed addr2line is enabled explicitly. Failures remain failures. Analytic or
map failures are collected so later checks can run; worker exit stays nonzero.

## Prior checkpoint findings and retention

The following describes the pre-repair checkpoint. See reports/summary.json and the retained raw diagnostic. Ordinary queries match
exactly on both maps. Sanitizers expose a production lifecycle leak, and the
larger map exceeds the diagnostic memory budget. No sanitizer pass is claimed.
That checkpoint left production source unchanged. The approved repair and bounded
diagnostic results are described below. Broader maps and other toolchains remain later
checkpoints; this does not establish whole-corpus or production suitability.

Original inputs total 1,072,288 bytes. Preserve them and all earlier evidence;
their upstream download is undocumented. Reused meshes are derived and can be
regenerated from those inputs plus the pinned C++/Rust sources and images, using
the parent experiment workflows. Query traces/reports can be regenerated with
the command above. After review, remove only a selected owned collision run
directory to reclaim its traces and logs. Never remove the shared inputs,
reference directories, Rust evidence or another agent's output. The dedicated
image store and transfer archive are disposable; retain the image digest and
import/cleanup receipts before removing them.

## Memory baseline follow-up

The narrow constructor repair removes the eager face-normal allocation that was
discarded before lazy computation. Winding computation and release are unchanged.
Analytic cycles now also check reversed winding.

The runner starts a reference process per map, then a candidate replay process.
Each owns one mesh/tree and releases its input arrays after building. Candidate
endpoints come from the reference hexfloat trace. The independent streaming
comparator checks float32 bits, categories, order, hit state and all seven valid
hit fields. It also compares against both outcomes in the retained ordinary
traces, preserving the original 11,008 queries. Comparator mutation controls
reject endpoint/category, length, hit and field changes. Original reader controls
remain available through the paired command and reject before building trees.

Add the required flag to the command above:
`--baseline-traces ~/.local/state/eqemu-map-translation/collision-eval-20261110/reviewed-ordinary/results`.
The worker copies only the two trace files. To admit the diagnostic experiment,
add `--sanitizers --memory-gib 4`. Accepted limits are 2 and 4 GiB; 4 requires
diagnostics. Memory plus swap equals memory, deliberately disabling container
swap. Ordinary default remains 2 GiB. No detector suppression or quarantine
changes are supplied. Run only one 4 GiB worker at a time after checking host
headroom. Keep native compilation and execution inside this rootless worker.

Every timed process records cgroup limits/current/peak/events before and after,
plus a 50 ms cgroup sampler. Each engine flushes RSS/HWM and cgroup observations
at start, read, build, input-array release, query and engine release. Cgroup peak
is cumulative for the container, including compilation; sampled current and
process GNU time peaks provide context. Short transients may fall between samples.
An exited process is the reclamation boundary, including ASan quarantine.
Freeing an engine alone does not promise reduced RSS.

Keep new large evidence under the owned
`~/.local/state/eqemu-map-translation/raycaster-memory-20261110` directory.
Preserve the original inputs and prior failed evidence. Regeneration deliberately
requires the pinned baseline traces and clean committed source; do not overwrite
reference traces. Remove only selected owned runs or the owned stopped daemon's
pruned store, retaining fingerprints and cleanup receipts.

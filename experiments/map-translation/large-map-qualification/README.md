# Three large map qualification, 2026-11-10

Executed and pushed source `2d72772d6519037acebc26b657a9eb8671b3900e`,
fixed point `2d6cc88a5ea891f79b72fc0e9530b35d7b2caec8`. Draft PR109 targets
`t3/map-raycaster-corpus`, PR108. Central Bead central-ncz2, root central-fnma.
Independent read-only review completed three rounds. Provenance, per-engine
cgroup isolation, regression binary authentication and input-hash sensitivity
findings were fixed before final execution. No remaining review blockers.

## Policy and API

The shared compile-time `assets.inc` table pins the logical input path, original
input byte length/SHA256, vertex/index counts and complete mesh byte length/SHA256.
`cases.json` preserves the investigation manifest from sibling report commit
`9b49773fb0cad81f4f49eab195948148fa83a0e6`; frozen engine/allocator/decoder source
hashes are checked before execution. Investigation traces were preserved but not
reused. Fresh unchanged-engine reference traces were generated with the existing
5504-query recipe.

Rust CLI: `mesh identity queries.bin output.bin --known-asset base/name.map original.map`.
C++ CLI: `--record mesh identity unused output.trace --known-asset base/name.map original.map`.
Omitting the final three arguments preserves conservative admission. Unknown
logical paths and mismatched input or mesh bytes cannot obtain the larger envelope.
Actual source-file paths can differ if their bytes authenticate to the selected
logical asset. The bytes hashed are the bytes parsed, avoiding a path re-open race.
The reference engine and all decoder/geometry arithmetic remain unchanged.

Rust's public `Raycaster::build` retains 4M vertices / 12M indices. Its private
`build_admitted` is called by the authenticated loader only. Default reader limit
is decimal 100,000,000 bytes. Cityofbronze is 100,672,188 bytes, below 100 MiB but
above that default. Known policies require exact counts and lengths, rather than
loosely increasing the defaults. Existing finite +/-1e8 coordinate, index, node,
depth, leaf-reference and query bounds remain in the core. The C++ reader retains
its existing positive-count/finite/index checks, now with bounded reading; every
mesh admitted under its original count caps is smaller than its 100M byte bound.

Bounded chunk reads reject trailing or growing input even if metadata was stale.
Count/length arithmetic uses checked usize operations in Rust and widened C++
arithmetic, with explicit size_t protection for vertex components. Replay requires
schema, identity, ordinal order, count/length and final EOF. Neither adapter mutates,
rounds, welds or drops geometry. No triangle-ID parity claim is made.

## Results

Each engine ran serially in its own fresh 2 GiB / no-swap / network-disabled
rootless container. Image is immutable
`sha256:0aeb54f14590bf1631a27f60b3e7b7eed6dde549f08ba1f5738282e59d4cea1d`.
Builds used Rust 1.99 frozen/offline and C++ release O3 without fast-math.
Host headroom before launch was 26,153,517,056 available bytes; existing host swap
use was 5,431,836,672 bytes. No worker swap, OOM, allocation failure or resource
escalation occurred. **No RAM bump needed; no optimization investigation triggered.**

| Map | Queries | Hits / misses | Classifier / field-bit changes | Rust peak RSS KiB | C++ peak RSS KiB | Rust cgroup peak bytes |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| chelsithreborn | 5504 | 3467 / 2037 | 0 / 0 | 186124 | 259696 | 202412032 |
| cityofbronze | 5504 | 4217 / 1287 | 0 / 0 | 201688 | 279804 | 218550272 |
| riftseekers | 5504 | 3416 / 2088 | 0 / 0 | 163340 | 224492 | 179904512 |

All seven valid fields matched exactly, including signed zero. Numeric diagnostics
report zero absolute/relative/ULP differences and no nonfinite outcomes. Category,
endpoint bits and order are checked by the independent comparator. Query binary,
reference trace, candidate trace, mesh, executable and source hashes are retained.
Fresh reference traces supply immutable binary endpoint input for Rust.

Ten Rust unit tests passed, including the existing nine tests / 4800 oracle queries,
plus byte-vs-MiB, overflow, bounded read and SHA256 known-vector controls. Each map
passed fourteen paired Rust/C++ rejection controls plus Rust range/order controls.
Controls cover defaults, exact envelope count and index-count excess, schema,
truncation/trailing bytes, logical path, identity, mesh-hash mutation, wrong input,
same-length input-hash mutation, nonfinite coordinates and invalid indices. Frozen
qeynos2 and poknowledge each passed 5504 queries against the original authenticated
PR106 traces and fresh reference replay, including their original adapter controls.
No need to rerun the historical 510 successful paths: traversal/arithmetic did not
change. This is 510 historical passes at their recorded source plus three new passes,
2,821,545 aggregate queries across revisions, with four data-contract cases still
unresolved. It is not a single-source 517-path qualification.

## Memory and evidence limits

Rust owns the parsed arrays and drops the raw buffer before building. During read,
raw bytes and parsed arrays overlap; incremental fallible exact reserves can also
briefly overlap old/new raw allocations. Tree construction retains visited stamps,
leaf references, ancestor/sibling split lists and Vec capacity slack, with temporary
reallocation overlap. These are obvious remaining copies/capacities; no speculative
optimization was added. C++ copies raw bytes into arrays, writes an automatically
removed temporary file for hash-tool stdin, and its unchanged constructor copies
input arrays before the adapter releases them. Cgroup accounting includes helper
processes and charged file cache; GNU time records process RSS peaks. Phase samples
are boundaries, not continuous allocation attribution. Fresh cgroups distinguish
reference and Rust peaks. Short elapsed/storage observations are not benchmarks.

Hash authentication depends on the pinned worker's `/usr/bin/sha256sum`, invoked
without shell interpolation. Rust's fallible reserves cover major explicit Vec
allocations, not every std, formatting, process or allocator operation. Cgroup
containment can kill a process and does not promise graceful handling of all
allocator failures. There is no Rust ASan claim. No sanitizer corpus, CXX/MMF,
server integration, concurrency, live game or broader toolchain qualification.
The two NaN maps and two empty V1 files remain outside this change.

## Reproduction and retained artifacts

`commands.txt` and `reports/*commands.json` record transport. `worker.py` requires a
clean commit and authenticates build source/image/executables before map/replay.
Every native/Cargo command executes only inside the disposable worker. Stage timeouts
retain first-failure receipts; there is no automated retry or memory escalation.
`reports/results.json` has per-process timings, cgroup events, peaks and identities;
per-map phase stderr/GNU time and compact rejection receipts are tracked. Full
traces, binaries, build stderr and container configs remain under
`~/.local/state/eqemu-map-translation/rust-large-three-20261110/`.

Build-01 at aa66ee00d passed. Build-02 at e9b193eab failed compilation because a
phase timer variable was missing; no map ran. That failure is retained, fixed in
28d3d54ee. Build-03/maps-01/regressions-01 passed at that revision. The final rerun at
2d72772d6 incorporates the remaining review fixes; this was not a retry of a failed
map, tolerance relaxation or RAM increase. Disposable archives/extractions and the
owned daemon/store are removed after retrieval, with cleanup receipts. Original
meshes/maps, sibling worktrees and all prior evidence remain unchanged.

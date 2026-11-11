# 2026-11-10 large-map admission investigation

Task `central-mhj8`, root `central-fnma`, [draft PR 108](https://github.com/thunderbump/bump-EQEmu/pull/108).
This is investigation evidence and a proposed next change. No decoder, raycaster,
adapter, runner or maintained cap changed. No new compared outcomes exist.

## Identity and execution boundary

Assigned worktree `/home/bump/.t3/worktrees/bump-EQEmu/t3-75636b95`, branch
`t3/75636b95`, was clean at `cd66dd0a6234fdb519046716041e0aaf231321c0`.
`git merge --ff-only 2d6cc88a5ea891f79b72fc0e9530b35d7b2caec8` succeeded,
preserving that branch. Before commit, an external branch rename to
`t3/map-limit-investigation` appeared in the reflog after that fast-forward.
This investigation retained the current branch rather than overriding the rename.
All source references below are to that full revision.
`cases.json` pins source file SHA256s, the frozen summary SHA256 and exact cases.
The prior corpus **executed** `c319cb077cbf5c2bfbf82544f3eab125e5e3277e`;
`2d6cc88` is the reviewed evidence revision, not a new executed raycaster.
Parent dirty BUILD.md/docs/.validation-worker and all previous worktrees are untouched.
A source diff from the executed c319cb0 revision to 2d6cc88 is empty for the
engine, decoder, allocator, collision driver, Rust reader/core, corpus guest and capture source.

New output root is `~/.local/state/eqemu-map-translation/corpus-limit-gaps-20261110`.
Host actions were source review, file inventory/hashing, writing this report/tool,
Python AST syntax checking and a failed dedicated daemon launch. No native build,
mesh parser, C++ engine, Rust CLI or candidate code ran in this investigation.

The documented rootless launch with a dedicated data-root/socket/exec-root/pidfile
and `DOCKERD_ROOTLESS_ROOTLESSKIT_STATE_DIR` failed before a daemon existed:
`newuidmap: write to uid_map failed: Operation not permitted`. The invoking
process reports `NoNewPrivs: 1`. `daemon.log` preserves the full command/error.
No privilege workaround, shared daemon, live service or 4 GiB job was used.
No running owned daemon/container/image requires cleanup. The failed provisioning
log and rootlesskit directory are retained as evidence. No image import ran.

The known ordinary immutable image receipt is
`raycaster-corpus-20261110/image-import.json`, manifest
`sha256:0aeb54f14590bf1631a27f60b3e7b7eed6dde549f08ba1f5738282e59d4cea1d`,
config `sha256:f8de3c199631d78e4fb433d2b1dfeb3f9d8709a5c7e6fc76589bc740c5106558`.
The existing importer verifies all OCI descriptors and blobs; it hardcodes the
old task's daemon/output and must not be run unchanged for this task. Parent must
provision a separately owned disposable daemon and verify/import into that store.

## Retained identities and known structure

`inventory.json` records nine freshly verified size/SHA256 pairs. Collection and
deployed inputs match the frozen source identities. Retained `geometry.mesh`
files match the frozen mesh identities. These checks read opaque bytes only.
Header interpretation and finite/index checks below are **prior corpus worker
results**, authenticated by those hashes, not newly executed parser results.

| Map | Vertices | Indices | Triangles | Mesh bytes | MiB |
| --- | ---: | ---: | ---: | ---: | ---: |
| chelsithreborn | 5,810,335 | 5,985,597 | 1,995,199 | 93,666,492 | 89.3273 |
| cityofbronze | 6,161,009 | 6,684,999 | 2,228,333 | 100,672,188 | 96.0085 |
| riftseekers | 5,001,642 | 5,497,866 | 1,832,622 | 82,011,252 | 78.2120 |

Each prior export passed EQMESH01 schema 1, input SHA in the 64-byte ASCII header,
`84 + 12*vertices + 4*indices` exact length, index divisibility by 3, all-finite
coordinates and every index below vertex count. Ordered f32 triples precede
ordered u32 indices. No deduplication/reindexing is justified by this evidence.
The counts show these are not necessarily three fresh vertices per triangle.
Prior header census has models/placements 170/8576, 173/12648, 55/1932 respectively,
and zero groups/tiles. Declarations do not prove branch execution.

Coordinate extrema, vertices beyond +/-1e8, unused vertices, repeated-index
triangles, geometric degeneracy and actual tree shape are **not newly measured**.
`inspect_mesh.py` is an investigation-only streaming inspector for the coordinate/index-use
metrics and repeated-index triangles. It pins the complete mesh hash and header
against `cases.json`, uses a vertex bitmap and fixed blocks, never rewrites bytes,
and refuses anything except a 2 GiB, zero-swap cgroup. It does not build a tree.
It has passed AST syntax review only; it has not executed on fixtures or assets.
A worker run is required before treating its output as evidence.

## Exact current constraints

Paths are relative to the pinned source above.

- `raycaster-corpus/guest.py`, sanity stage: magic/version/input identity, exact
  byte formula, index count multiple of three, all coordinates finite, indices
  below vertex count, then nonempty vertex and index arrays. No 4M vertex cap,
  100M byte cap, +/-1e8 range check, tree, node or leaf proof at this stage.
  The Python sanity path temporarily duplicates raw arrays through slicing.
- `collision/driver.cpp::read`, also included unchanged by
  `rust-raycaster/reference.cpp`: reads the **entire file before admission**, then
  at least 84 bytes, schema 1, `0 < nv <= 4,000,000`,
  `0 < ni <= 12,000,000`, `ni % 3 == 0`, exact length with u64 arithmetic,
  64 lowercase hexadecimal identity characters and matching header, finite floats,
  and indices below nv. There is no separate byte cap or +/-1e8 cap. The binary
  reference wrapper inherits this reader; it is not a bypass.
- `rust-raycaster/src/main.rs::load`: file metadata length <= **100,000,000 bytes**,
  reserves raw bytes, reads to end, minimum header/schema; nv <= 4,000,000,
  ni <= 12,000,000, ni divisible by 3, exact length and identity. Empty arrays
  pass this reader. It reserves decoded arrays with `try_reserve_exact`; raw bytes
  coexist with both arrays during parsing. Core checks coordinates and indices.
  Byte metadata checking alone does not bound read-to-end if the file grows.
- `rust-raycaster/src/lib.rs::Raycaster::build`: repeats both count caps and index
  divisibility; every component finite and absolute value <= 1e8; every index
  below nv. Empty arrays build an empty engine. `try_reserve` protects allocations
  with returned errors, but is not an aggregate physical-memory reservation.
  Node cap is 131,071; total stored leaf references cap is 16,000,000. Depth >=15,
  fewer than four triangle IDs or largest axis <0.01 ends subdivision.
- Rust queries require finite endpoints within +/-1e8. CLI replay schema is
  EQRAYS01, matching identity, `1 <= count <= 100,000`, exact `76+28*count`
  bytes and consecutive u32 ordinals. Rows have six f32 endpoints. Output is
  one classification byte and, on hits, seven finite f32 values plus u32 triangle
  diagnostic. Thus output <=3,300,000 bytes at the replay cap. Triangle IDs here
  are <2,228,333; casting them to u32 is safe. Frame-stamp wrap is explicitly
  cleared. No C++/Rust triangle-ID parity is claimed.
- C++ text record recipe yields at most `3200+256*9 = 5504` queries. Degenerate
  sampled faces can reduce that number. It checks finite hit fields, not a
  +/-1e8 query bound. Binary reference replay has schema/identity, 1..100000
  count, row order, endpoint reads and trailing EOF checks. It writes the same
  hit payload size with an unavailable-triangle sentinel. The corpus packages
  reference-generated endpoints for Rust; it does not fabricate new endpoints.
- `zone/raycast_mesh.h/.cpp`: public vertex and triangle counts, indices, visit
  stamps and C++ leaf offsets/counts are u32. No 4M/100M production constructor
  gate exists. Depth is clamped to 2..15, default 15; default leaf threshold 4
  and minimum axis size 0.01. Index expressions include u32 `vertex_index*3` and
  `triangle_id*3`; generic bounds must protect those multiplications, not merely
  check counts fit u32. All three meshes are far below those arithmetic limits.
  Leaf offsets must also fit u32 including C++ per-leaf count words.
- `zone/map_geometry.cpp` is the unchanged production decoder. V2 uses u32
  compressed/inflated sizes and counts, reserves output counts, and appends
  placement/model/terrain geometry. It has no matching experimental 4M/100M cap;
  this is not a comprehensive safe-parser audit. `capture.cpp` rejects final
  array counts above UINT32_MAX, non-triangular indices and out-of-range indices
  before writing. The prior three decodes passed; no decoder fix follows from
  their downstream count rejection.

Paths in the first bullets are under `experiments/map-translation/`.
All three maps independently fail C++ adapter, Rust CLI count and Rust core count
admission. City of Bronze additionally fails the Rust byte cap **first**.
100,672,188 > 100,000,000 but < 104,857,600, which is 100 MiB. None exceeds the
12M index cap. No node or leaf limit is known to fail these actual maps; coordinate
range and endpoint range remain unverified. There is no Rust outcome to compare.

## Bounds, memory and semantics

**Proof:** a binary tree with root depth zero and maximum leaf depth 15 has at
most `2^16-1 = 65,535` nodes and `2^15 = 32,768` leaves. The Rust 131,071-node cap
cannot independently reject such a tree. C++ preallocates 98,303 nodes because
its `pow2Table[15]` is 65,536 rather than 32,768. That is over-allocation, not a
node shortage and not a reason to change topology. Node object sizes differ
between the implementations; do not infer Rust memory from C++ logging.

**Proof:** each triangle may enter both children. Without geometric assumptions,
total leaf references can be as large as `nt * 32,768`, independent of original
vertex reuse. The three worst-case reference bounds are 65,378,680,832;
73,018,015,744; and 60,051,357,696 respectively. Even the smallest exceeds
16M and u32 C++ leaf offsets. These are conservative theoretical upper bounds,
not predictions or actual counts. A vertex cap increase cannot prove them safe.
C++ also stores a count word per leaf. Rust stores usize triangle IDs in leaves
and scratch lists, 8 bytes on the recorded x86-64 worker versus C++ 4-byte IDs.

**Source accounting, not RSS:** final input arrays occupy mesh bytes minus 84.
Visit stamps cost 4*nt. C++ copies both arrays while caller arrays are still live,
preallocates nodes, retains ancestor and sibling scratch lists while descending,
and grows a leaf vector. On first normal request it allocates `12*nt` additional
bytes for all face normals, without an explicit null check there. Rust owns input
arrays, but its reader first duplicates the raw mesh; tree construction also
retains pending sibling lists and grows nodes/leaves. Vector capacity can exceed
length; reallocation temporarily keeps old and new allocations. The leaf cap
alone does not cap pending split lists or allocation overlap. Container page cache,
Python process, allocator overhead and cgroup memory charges also matter.

City's two input-array copies total 201,344,208 bytes. Its visit stamps cost
8,913,332 bytes and C++ lazy normals 26,739,996 bytes. These known components are
well below 2 GiB, but they omit the unmeasured duplication and scratch peak.
The earlier 288,296,960-byte asset peak includes export/sanity/adapter rejection;
there was no tree build. It is not a tree-memory baseline.

Streaming the strict CLI readers into final owned arrays can remove the raw-file
copy and constrain growth while preserving byte/order/identity validation. It
does not remove tree duplication or C++ constructor copying. A streaming read
must validate checked sizes before allocation, read exactly the admitted payload
and reject trailing bytes. Repacking vertices, splitting the map into separately
queried chunks or changing depth/leaf rules changes identity/topology/pruning or
tie traversal. Such partitioning is a new algorithm requiring separate evidence.

## Recommended next change and authorization decision

Prefer an **exact known-asset envelope**, tied to original input SHA, exact
ordered mesh SHA/counts/length in `cases.json`. That admits these three inputs
for a bounded investigation without globally widening trust in arbitrary meshes.
The smallest common numerical envelope is nv <=6,161,009, ni <=6,684,999 and
bytes <=100,672,188, with exact checked length. For a documented round envelope,
6,200,000 vertices, 6,700,000 indices and **101,200,084 bytes** is internally
consistent. Neither proposal is a memory guarantee. A 100 MiB byte cap alone
would admit these files but would leave both independent vertex gates blocking.
Do not change the existing 16M leaf, coordinate, node, time or 2 GiB/swap caps.

First obtain worker structure results, including actual coordinate/query ranges.
Then parent can authorize a **lab C++ wrapper only**, with unchanged engine,
unchanged seeded query recipe and explicitly validated exact envelope. Prefer
City of Bronze as the largest representative, but success on it does not prove
the other tree shapes fit. Stream the mesh to avoid the original adapter's raw
copy; validate every existing schema/identity/finiteness/index bound and the
additional production u32 multiplication/leaf-offset bounds. Keep one engine per
process, 120-second stage and 2 GiB/no swap, ordinary only. Record engine source,
wrapper source/patch, binary, image, mesh, replay and trace hashes, cgroup peak,
events, OOMKilled, timings and cleanup. Report this as a new lab result, never as
the original blocked adapter or unchanged Rust succeeding.

Before a maintained Rust admission expansion, add an explicit shared policy at
reader and build entry points with checked byte/count arithmetic. Account live
**capacity bytes**, transient reallocations, pending sibling lists and stored
leaves before allocation. Use fallible reservations and deterministic budget
errors; maintain a margin below the hard 2 GiB worker ceiling. A provisional
1 GiB aggregate engine allocation budget is a conservative policy choice to test,
not a measured required amount or an RSS proof. It must cover both reader and
build overlap, not just final leaves. Generic C++ wrappers need a corresponding
preflight/budget or a geometry-specific tree proof; the unchanged C++ engine has
no fallible aggregate resource gate. The hard cgroup cap remains the final
containment boundary. An OOM or timeout must remain an explicit blocked result.

Minimal followup files are `collision/driver.cpp` admission, `rust-raycaster/src/main.rs`
reader admission/streaming, `rust-raycaster/src/lib.rs` core admission/accounting,
and corresponding reader/core controls. If the count policy is shared, add one
small policy definition rather than independently editing constants. Corpus
`guest.py`/runner identity and rejection controls need explicit policy recording;
reference.cpp inherits driver.cpp. No production decoder/raycaster change is
supported by this investigation. A lab wrapper belongs under this owned limits
subdirectory, with distinct source identity.

Required unit/control checks: old defaults still reject over-limit assets;
explicit policy exact boundary and +1 for vertices/indices/bytes; decimal vs MiB;
checked overflow before allocation; truncated/trailing/growing inputs; bad schema,
input identity, mesh hash, nonfinite/out-of-range vertices, index and query range;
empty geometry behavior unchanged; scratch/leaf/capacity budget failure before
allocation; node-depth bound; frame wrap; ordinal/count/output protocol; identical
ordered arrays, unchanged seeded endpoints and useful hits/misses. New tests and
all native/Cargo controls execute only in the disposable worker. Follow with all
three maps serially, reference then Rust, preserving every failure.

**Decision:** parent can authorize the inspector and bounded lab reference
experiment after worker provisioning. This report does **not** justify declaring
all three qualified or authorizing a cap-only Rust rerun as safe. The known static
count/byte gates can be removed by the proposed envelope, but coordinate/query
range, actual leaf duplication, scratch peak and 2 GiB fit remain unknown. Confidence
is high in gate localization and combinatorial bounds, low in unmeasured memory.
No source evidence here implicates both decoder and engine or calls for replacement.

## Reproduction and handoff

Read-only host inventory can be repeated with `sha256sum`/`stat` for the nine
paths in `inventory.json`. Source identities use `sha256sum` for `cases.json`
source keys. Review syntax without running asset parsing:

```sh
python3 -c "import ast,pathlib; ast.parse(pathlib.Path('experiments/map-translation/corpus-investigation/limits/inspect_mesh.py').read_text())"
git diff --check
```

After documented provisioning and verified image import into the **new owned**
daemon, copy/mount this directory read-only at `/limits`, three retained meshes
read-only at `/inputs/<zone>.mesh`, and a fresh owned writable output at `/results`.
Use the immutable image above, network none, memory 2g, memory-swap 2g, cpus 2,
pids-limit 128. The guest command is:

```sh
/usr/bin/timeout --signal=KILL 120 python3 /limits/inspect_mesh.py /limits/cases.json /inputs /results/structure.json
```

A host transport must bound start/copy waits, inspect final container state, kill
on outer timeout, retrieve logs and cgroup/exit receipts, then remove only its
owned container/daemon/store. This command is **not executed** in this report.
Do not reuse the old corpus socket, store or importer destination. No lab native
wrapper was written, patched, compiled or run; no shim hash or new binary exists.

Parent owns combined Operations publication. Include this report's final commit
as a pinned source-authority link and preserve the incomplete runtime evidence.
`central-mhj8` and `central-fnma` stay in progress for parent disposition. No new
code PR is warranted for an unexecuted inspector and findings alone.

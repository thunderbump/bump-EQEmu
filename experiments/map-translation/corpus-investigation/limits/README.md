# Large-map limits investigation, source and runtime evidence

Task `central-mhj8`, root `central-fnma`. [Draft PR 108](https://github.com/thunderbump/bump-EQEmu/pull/108)
remains the frozen original corpus result: 510 exact paths and seven blocked.
This continuation adds retained-mesh inspection and **investigation-only C++ lab
results** for three maps. It does not change those original outcomes or qualify Rust.
No maintained decoder, engine, reader, adapter, runner, manifest or cap changed.
Only this new `limits/**` directory changed. Operations publication remains owned
by the parent/other agent; no Operations files were written.

## Results

All ten executable inspector fixtures passed before the input run. Inspector
`adff87f2b7a99d19518db82d9a542e440f0f0371` source identity is recorded in full in `runtime-results.json`.
The three hash-pinned retained meshes then passed schema/header/input identity,
exact byte length and index checks in the worker. Every vertex is referenced.
No nonfinite component or component outside the Rust core's +/-1e8 range exists.
Repeated-index triangle counts are 1, 6 and 0 respectively. General geometric
degeneracy was not measured; original mesh/index ordering and all bytes remain intact.

| Map | Coordinate minima x,y,z | Coordinate maxima x,y,z |
| --- | --- | --- |
| chelsithreborn | -1531.4647216796875, -1225.7586669921875, -3776.33544921875 | 1519.9896240234375, 2833.57421875, 1103.4571533203125 |
| cityofbronze | -3305.666748046875, -3647.25048828125, -1267.1986083984375 | 3336.332763671875, 2913.7490234375, 3474.8974609375 |
| riftseekers | -1638.4407958984375, -840.9263305664062, -861.5304565429688 | 1874.6309814453125, 2035.624755859375, 580.8037109375 |

A separately labeled copied C++ lab adapter, using the unchanged production engine
and unchanged seeded query recipe, built and queried each mesh serially. The
adapter accepts only the three pinned input identities with **exact** vertex,
index and byte counts. Schema, lowercase input identity, finite coordinates,
index bounds, length and divisibility checks remain. Worker-side pre/post mesh
SHA256 checks authenticate the complete read-only geometry. Read-only tree
statistics were added to the copied adapter. The original adapter remains blocked.

| Map | Nodes | Leaf nodes | Leaf references | Largest leaf | Queries | Hits | Misses | Stage seconds | Cgroup peak bytes |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| chelsithreborn | 49,723 | 24,862 | 2,345,785 | 9,308 | 5,504 | 3,467 | 2,037 | 0.566 | 275,521,536 |
| cityofbronze | 34,609 | 17,305 | 2,512,354 | 16,033 | 5,504 | 4,217 | 1,287 | 0.666 | 314,384,384 |
| riftseekers | 62,393 | 31,197 | 3,006,675 | 3,675 | 5,504 | 3,416 | 2,088 | 0.565 | 251,539,456 |

These are fresh per-map container observations, including the Python supervisor,
reader/build/query phases and charged cache, not just final tree bytes. Every
container enforced 2,147,483,648 memory bytes, zero swap, two CPUs, 128 PIDs and
network none. All successful stages exited zero, OOMKilled=false, with zero
`memory.events` max/oom/oom_kill/oom_group_kill. No stage timed out. The inspector
completed in 8.750 seconds with a 14,700,544-byte cgroup peak. GNU time maximum
RSS for the three lab processes was 262,380 / 299,440 / 239,616 KiB. Raw GNU time,
phase RSS/HWM and cgroup readings are preserved separately; they are different
counters. Timings are single-run observations, not comparative benchmarks.

All C++ node objects were 56 bytes in the recorded GCC 12.2 x86-64 image. The
engine allocated 98,303 node slots and leaf vectors with capacity 4,194,304 u32
words. Leaf **words** include one count per leaf; reference counts above exclude
those words, matching the conceptual Rust reference count. This does not measure
Rust usize-vector capacities or scratch lists. All 16,512 reference queries had
finite endpoints within +/-1e8 and finite returned hit fields; endpoint absolute
maxima were 3786.33544921875 / 3657.25048828125 / 2045.624755859375. There were
11,100 hits and 5,412 misses. No Rust output, binary replay comparison or native
versus Rust tree-ID comparison was produced or blessed.

## What now blocks qualification

The exact source constraints and proofs remain in [SOURCE.md](SOURCE.md), the
accepted earlier source-only report retained as a historical snapshot. Its claims
that runtime inspection was unavailable/unmeasured are superseded by this report.
`cases.json` and `inventory.json` retain all original counts and input/source hashes.

All three independently hit the original C++ adapter, Rust CLI and Rust core
4,000,000-vertex gates. City of Bronze also hits the Rust CLI's **decimal**
100,000,000-byte gate; 100,672,188 bytes is below 100 MiB =104,857,600 bytes.
All index counts are below 12M. Actual coordinates and reference endpoints now
pass Rust range bounds. Actual C++ nodes and leaf references are below current
Rust caps, but that does not prove independently executed Rust tree resource use.

Depth 15 proves at most 65,535 nodes; the Rust 131,071-node cap does not bind.
The worst-case triangle duplication bound `nt*32768` remains enormous even though
these actual C++ trees are small. This distinction matters for admitting arbitrary
future assets. C++ node preallocation overestimates the bound because of its
power-table entry; no correction is needed to qualify these inputs. No evidence
here implicates the decoder or calls for a new raycasting algorithm.

## Concrete next-change recommendation

Parent can now authorize a **narrow, explicit known-asset admission followup and
serial three-map differential rerun under the existing 2 GiB/no-swap bounds**.
The evidence supports doing that experiment; it does not establish Rust success.
Keep all time, coordinate, node, leaf, replay and output safety checks.

Prefer opt-in exact input+ordered-mesh SHA/count/byte envelopes from `cases.json`,
leaving original defaults unchanged. A common numerical envelope sufficient for
these three is vertices <=6,161,009, indices <=6,684,999 and bytes <=100,672,188,
with checked exact length. A rounded consistent envelope is 6,200,000 vertices,
6,700,000 indices and 101,200,084 bytes. Raising just the reader byte cap or just
one vertex gate cannot complete the path. Do not widen unrelated input policies.

Minimal maintained experimental files would be
`collision/driver.cpp`, `rust-raycaster/src/main.rs` and `rust-raycaster/src/lib.rs`
under `experiments/map-translation/`, plus controls and corpus policy/source
identity recording. The binary reference wrapper inherits the collision reader.
Use one explicit policy consistently at both Rust entry points; avoid independently
edited constants. Production decoder/raycaster sources need no change.

For these exact assets, the measured C++ margin does not require streaming or
spatial partitioning. Rust memory remains unmeasured. Streaming strict reader input
can remove a raw mesh copy while preserving array order, but it is an optional
memory reduction rather than evidence of a current physical-memory failure.
Splitting maps into query chunks changes pruning/tie/topology behavior and is a
new algorithm, not a necessary fix demonstrated by these results.

A generic runtime admission policy still needs checked arithmetic and fallible
allocation with accounting for **live capacities, pending split siblings and
reallocation overlap**, not only final leaf length. Guard u32 index*3 and C++ leaf
offsets/count words as well as usize/byte arithmetic. Keep the cgroup ceiling as
final containment. A proposed 1 GiB aggregate engine allocation budget is a policy
to test, not a measured Rust requirement or a proof of RSS. A bounded exact-asset
experiment can precede a general-purpose allocation-policy redesign; its evidence
must remain confined to the recorded hashes, toolchain and worker limits.

Before qualifying a followup, run old-default rejection and explicit-envelope
boundary/+1 controls, byte-vs-MiB and overflow checks, schema/hash/identity,
truncation/trailing input, nonfinite/range/index, replay count/order/endpoints,
leaf/scratch budget and allocation failure controls, then all three maps serially.
Preserve original ordered mesh bytes, reference endpoint bits and all failures.
No decoder replacement, new tree topology or safety-limit suppression is justified.

Confidence is high in current gate localization, input/range checks and measured
C++ results. Rust peak allocation, leaf/scratch behavior and bit-exact query
outcomes remain unknown until that authorized followup actually executes.

## Execution identities and failed attempts

- Reviewed engine/decoder baseline: `2d6cc88a5ea891f79b72fc0e9530b35d7b2caec8`.
  Engine/decoder/allocator/driver source bytes still match original corpus execution
  `c319cb077cbf5c2bfbf82544f3eab125e5e3277e`.
- Inspector execution: full commit recorded in `runtime-results.json`, prefix
  `adff87f2b`. Inspector SHA256
  `aaa71ca19aacba5e233092fcfdb6056a8bb200b5f8c31d565e351034b6cf9916`.
- Successful lab tools: `69a1eb3f319706961e0c1e9e1da43fdd88b13731`.
  Generated adapter SHA256
  `1443393886495c575f25179955d161a5d903496c848a362e12ddc6078fe64ea2`.
  Binary SHA256
  `28fcf09a6d376befb195900996221ca3c66933aea2938a3c65d3ca7ff1f3a3af`.
- Unchanged engine SHA256
  `de0b71fa404839d899793e9e9814d410dc0505b6ba8b2bd0c068625c7ef131f4`.
  GCC 12.2.0 compiled with `-O3 -DNDEBUG -std=c++17`; no sanitizer job ran.
  The existing standalone logging shim was reused unchanged.
- Ordinary image manifest
  `sha256:0aeb54f14590bf1631a27f60b3e7b7eed6dde549f08ba1f5738282e59d4cea1d`.
  Every OCI blob was verified before streaming import into the new owned store.
  No original image store was copied or changed. Full descriptors are in results.
- The original direct-launch `newuidmap` failure is preserved at output-root
  `daemon.log`. The documented existing `systemd-run --user` workflow then worked
  with a unique user-owned unit. No service policy, privilege setting, security
  detector or kernel restriction was weakened. No live service was used.
- `lab-01`, tools `ddd466425`, failed compilation because the newly added lab
  statistics incorrectly namespace-qualified the `TRI_EOF` macro. No map ran.
  Logs and generated failed source/patch are retained. Fixing the wrapper macro
  spelling produced the successful distinct `lab-02` source above. No detector,
  compiler diagnostic or engine source was suppressed or altered.

## Reproduction, artifacts and cleanup

Worktree `/home/bump/.t3/worktrees/bump-EQEmu/t3-75636b95`, branch
`t3/map-limit-investigation`. Only own `limits/**` files are committed. Parent
checkouts, shared runners/manifests, original inputs and previous evidence remain
untouched. No new PR was opened. PR108 is registered with this thread.

`commands.txt` records launch/import/fixture/inspection/lab/cleanup commands.
The source must be clean and committed; runners record full executed source/tool
hashes and enforce fixture completion before map inspection and lab execution.
`prepare_lab.py` generates a copied adapter with an exact envelope and statistics;
its unified patch and generated source remain outside Git. No Rust tool ran.

Output root is `~/.local/state/eqemu-map-translation/corpus-limit-gaps-20261110`.
Within `user-worker/`, `inspection-01` contains executable fixture logs and
structure/cgroup/time/container receipts. `lab-01` retains the failed build;
`lab-02/build` retains generated C++ source/patch, binary, source hashes, compile
and analytic-control logs. Per-map directories retain full hex reference traces,
phase memory, GNU time, query/tree summaries and container/cleanup receipts.
`runtime-results.json` commits compact exact results and identities; full generated
artifacts remain outside Git. Original and deployed map plus retained geometry
size/SHA256 checks passed again after all execution.

All owned containers were removed. Only the owned daemon was pruned and stopped,
then its owned store removed through the documented user-manager/rootlesskit
cleanup path. The unit is inactive/dead, Result=success, store/socket/exec-root absent.
No other daemon/store/unit was modified. Regenerable source archive/extraction
copies from both lab attempts were removed with paths, sizes and hashes in
`user-worker/cleanup.json`. Generated native source/patch/binary, failedsetup
and failed-compile evidence, logs, traces and all original evidence are retained.

Task and root Beads remain open for parent disposition. Parent owns combined
Operations publication using the final report commit as the pinned source link.

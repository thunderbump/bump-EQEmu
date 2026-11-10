# Map translation reference tests

Opt-in two-map experiment, central-qfiz. No Rust, CI or game deployment changes.
Inputs are pinned by `corpus.json`; keep inputs and all generated results outside
Git. The old manifest reference commit was runtime-unqualified, not a golden.
The scalar memcpy prerequisite originated in local commit `78c07284908931297ea72e25da358965b60ee913`.

`zone/map_geometry.cpp` is the real production V1/V2 loader and expansion code,
extracted from `Map`. Both production `Map::LoadV1/LoadV2` and the offline tool
call it. Production still owns `createRaycastMesh` and every public query. The
capture tool serializes the returned owned arrays at that boundary, without MMF,
zone globals, database, world startup or a second transformation implementation.
Extraction changes file location, not transformation arithmetic/order.

## Reproduce

Use an authorized disposable rootless daemon and an immutable image with CMake,
Clang/GCC, Python3, zlib development files and GLM. Commit reviewed source first.
Run from the Server checkout on the host, substituting the existing daemon socket
and immutable image ID. This command only transports source and artifacts on the
host; all build and test execution happens inside the disposable worker:

```sh
python3 experiments/map-translation/worker.py \
  --socket unix:///run/user/1000/eqemu-map-capture.sock \
  --image sha256:ecfffeb317764c9ab48b08f06b0e7fbd07403a8e320349024647efe6342869f2 \
  --glm /candidate/build/vcpkg_installed/x64-linux/include \
  --inputs /home/bump/.local/state/eqemu-map-translation/two-maps-v1/inputs \
  --output /home/bump/.local/state/eqemu-map-translation/CHOOSE-A-FRESH-DIRECTORY
```

Use the actual retained image ID from `container.json`, not an assumed image
installation. Add `--sanitizers` and a different output directory for ASan/UBSan.
The diagnostic target uses non-PIE to avoid observed intermittent Clang14 ASan
startup SIGSEGVs with PIE in this worker. This is a separate diagnostic profile,
not evidence about the normal full server sanitizer build. Failures are retained;
there is no retry-to-pass behavior. Containers are removed after results are retrieved. Failed retrieval exits nonzero
and retains the stopped container ID in `cleanup.json` for manual recovery.
The worker prints no success unless analytic tests, rejection controls, input
hash checks, two captures per asset, bounds and predicted counts all pass.

`source.json` pins the Git commit/tree, source archive hash and prerequisite
origin. `provenance.json` records actual compiler, exact compile commands,
architecture, FP flags, binary hash, linked-library paths/hashes including
zlib/libm, and complete GLM include-tree hashes. Container inspection pins the image. Each
`.time` records wait status, peak RSS KiB and CPU time via Linux wait4. Elapsed
wall time and output hashes/sizes are in `results.json`. Provenance is separate
from geometry. No existing artifact or reference is overwritten by the workflow.

## Schema 1 and future Rust comparison

Every field is explicit little endian, no padded structs:

- Bytes 0..7: ASCII `EQMESH01`.
- Bytes 8..19: uint32 schema=1, vertex count, index count.
- Bytes 20..83: 64 lowercase ASCII input SHA256 characters.
- Then vertex count * 3 uint32 IEEE754 float32 coordinate bits in x,y,z order.
- Then index count uint32 indices, triangle order and winding unchanged.
- Exact file length is 84 + vertex count*12 + index count*4. No trailing bytes.

The capture binary takes `input.map output.mesh input-sha256`; the workflow
verifies the hash first. Direct binary callers are responsible for truthful input
identity. Future Rust emits this schema for the same input. Inside the disposable
worker run `python3 experiments/map-translation/mesh.py reviewed.mesh rust.mesh`.
Nonzero exit means rejection. First difference reports file/map, vertex and axis
or triangle/corner, expected/actual hex bits, and optional numeric delta. There
is no epsilon, normalization, welding, sorting or automatic golden promotion.
Compare geometry payload hashes separately from provenance. Review and retain
references explicitly; a fresh capture never blesses a candidate.

## Coverage

Four automated test methods cover a hand-computed asymmetric base/model fixture,
identity and nonuniform scale/translation placements, output order/winding/x-y
swap, hidden polygons and skipped noncollidable data; seven comparator rejection
controls including changed vertex/index, reordered triangles, truncation, schema,
input identity and bounds; plus basic driver errors and exclusive output creation, including symlink/hardlink
protection. Diagnostic execution also requires a deliberate unaligned load to
fail with the expected UBSan diagnostic. Fixture placement rotations
are zero. Real starter assets exercise many nonzero rotations, but have no groups
or terrain. No independent nonzero-rotation, group, terrain or V1 runtime fixture
is claimed. The production parser retains unchecked packed-buffer reads on
malformed payloads; this extraction is not parser hardening or fuzz qualification.
Capture tests do not exercise the raycaster, MMF, public queries, future CXX
ownership or startup/shutdown. No full Server build is implied by this target.

## Storage and cleanup

The two original inputs total 1,072,288 bytes. Preserve them. Their hashes pin
identity, but the manifest does not supply a known obtainable upstream download;
Git alone cannot regenerate these original assets. Do not treat them as cache.

One derived reference pair occupies 13,401,288 bytes including schema headers.
Two repeatability copies occupy 26,802,576 bytes per profile; ordinary plus
sanitizer copies occupy 53,605,152 bytes, excluding small logs and provenance.
Derived meshes, binaries and comparison reports can be removed from an owned run
directory after review and regenerated with the command above, provided the
original inputs, Git commit and matching worker image/dependencies are retained.
Retain the small source/image identity and command alongside any handoff. Cleanup
means deleting only an explicitly selected owned output directory, never the
shared `two-maps-v1/inputs` or another run. No storage framework or whole-corpus
retention policy is introduced here.

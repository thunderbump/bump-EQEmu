# Rust individual-placement PoC

Opt-in standalone crate, central-z9lx. Baseline is PR #102 commit
b562693cc4fa07970695e1be0ad889a0ed5ee02d, fast-forwarded from cd66dd0.
The C++ reference is unchanged. This crate is outside the actor workspace and
all default build/test/CI paths. No game deployment or CXX integration.

`decode_map(&[u8]) -> Result<Mesh, MapError>` owns ordered `[f32; 3]` vertices
and `u32` indices. It decodes little-endian V2 zlib using flate2's Rust backend.
It keeps raw NUL-terminated name bytes, last duplicate definition, missing-model
skip, visibility `!= 0`, base geometry and placement/polygon/corner order.
Rotation is X then Y then Z in radians, followed by component scale,
translation, then X/Y exchange. Arithmetic uses f32 assignments and idiomatic
sin/cos, without fused multiply-add, rounding, sorting or welding.

V1, placement groups and terrain error explicitly. Malformed lengths, zlib
streams, indices, triangle counts and trailing bytes error. Hidden polygons'
indices are also checked. PoC limits: 128 MiB compressed/inflated data,
4 million output/model vertices, 12 million output indices, 4 million polygons
per model, 65,536 model definitions, one million placements, 4096 name bytes.
These are deliberate resource bounds, not a general map-format promise.
Errors have stable descriptive categories; OS I/O errors retain OS details.
The crate forbids unsafe code and declares no FFI. Rust std implements trig
using the platform runtime; dependencies may use upstream unsafe optimizations.
This is not a claim that the entire linked process contains no unsafe code.

The CLI takes `input.map output.mesh input-sha256`, checks the actual SHA256,
and exclusively creates schema-1 EQMESH01 output. Existing files, hardlinks and
symlinks are preserved. A failed write may retain a partial newly created output;
choose a fresh path for each attempt. It reports load/hash, decode/expand and
serialization seconds on stderr. Hashing is included only in Rust load time;
end-to-end timings therefore do not isolate decoder cost.

## Run only in disposable workers

Never execute Cargo or candidate binaries on the host. Host Python below only
transports data and orchestrates rootless Docker. Use an authorized dedicated
rootless daemon, exact socket and image IDs. Preserve original inputs and
qualified C++ references. Review source and commit it before qualification.

The reference image has pinned Rust 1.99.0 but lacks these dependencies. Provision
once with network access confined to a disposable container:

```sh
python3 experiments/map-translation/rust/provision.py \
  --socket unix:///run/user/1000/eqemu-rust-map-poc.sock \
  --image sha256:ecfffeb317764c9ab48b08f06b0e7fbd07403a8e320349024647efe6342869f2 \
  --output /absolute/new/provision-directory
```

Retain the returned `image.txt`, lockfile, provision log and toolchain identity.
The tracked lockfile pins transitive dependencies. Fetching is separate from
qualification, which is network-disabled and uses `--frozen --offline`:

```sh
python3 experiments/map-translation/rust/worker.py \
  --socket unix:///run/user/1000/eqemu-rust-map-poc.sock \
  --image sha256:0aeb54f14590bf1631a27f60b3e7b7eed6dde549f08ba1f5738282e59d4cea1d \
  --glm /candidate/build/vcpkg_installed/x64-linux/include \
  --inputs /home/bump/.local/state/eqemu-map-translation/two-maps-v1/inputs \
  --references /home/bump/.local/state/eqemu-map-translation/capture-tests-20261110/handoff-ordinary/results/capture \
  --output /absolute/new/qualification-directory
```

The worker archives the clean commit, verifies rootless mode, builds optimized
Rust and C++, runs independent analytic/parser/CLI/comparator checks, captures
each candidate twice, checks predicted counts and repeatability, and runs the
unchanged strict comparator against the supplied references first. The separate
`diagnose.py` reports absolute/relative/ULP metrics, signed-zero changes,
nonfinite bit patterns and finite bounds without relaxing structural equality.
Near-zero reference magnitudes below 1e-6 are excluded from relative maxima.
ULP ordering maps both signed zeros to the same numerical position but reports
changed zero bits separately. No epsilon or automatic acceptance is implemented.

Fresh C++ timing outputs are checked against immutable references then deleted.
`/usr/bin/time` records peak RSS for both optimized binaries, avoiding Python
fork-inherited high-water marks. Initial `wait4` timing evidence is historical
and superseded. Short runs and two repetitions are indicative, not a benchmark.
The normal C++ run's `provenance.json` retains compiler/GLM/library fingerprints;
Rust verbose build logs, rustc identity, Cargo.lock, binary/shared-library hashes
and FP settings are retained alongside source/image/container identities.
Cleanup receipts distinguish removed containers from retrieval failures.

Independent tests cover asymmetric base geometry, nonuniform scale/translation,
visibility, order, non-UTF8 names, duplicate replacement and missing models.
A single Z rotation and a noncommuting XYZ rotation use independent f64 matrices
with 2e-5 world-unit analytic allowance, not inverse-rotation identities.
Parser controls cover truncated headers/records, huge counts, bad indices,
unsupported versions/groups/terrain, corrupt or mismatched zlib and trailing data.
Diagnostic controls cover empty geometry, signed zero, adjacent floats and
nonfinite data. No fuzz infrastructure is supplied.

## Retention and decision boundary

Keep manifests, commands, small reports and source/image identities. Large
meshes remain outside Git. Two candidate repeats occupy 26,802,576 bytes;
ordinary C++ timing captures retained by the inherited runner add the same size.
Deleting only an explicitly owned run directory can reclaim these reproducible
outputs. Preserve the original 1,072,288 input bytes and existing reference
artifacts. Their upstream download is not documented.

Exact equality is the baseline; measured drift could support a later explicit
tolerance decision. Two-map parity does not establish other toolchains,
whole-corpus compatibility, raycaster/collision equivalence, cache behavior,
CXX lifetime safety or production suitability. Next work should be a separately
authorized collision-query/integration evaluation, not automatic deployment.

# 2026-11-10 additional-map result

`griegsend.map` was selected once from 449 header-supported existing maps.
Seed `brian-extra-map-20261110-v1` was recorded before selection. The pinned
manifest records the exact algorithm, candidate digest, census hash and source
collection. The original selected asset and copied input both hash to
`2d12a9614a1db9a5cb1abcceca09b3007f0fdfc6ac497a61fe71c0a7724e9c58`.
This is a filtered V2 individual-placement smoke sample, not all-map coverage.
Its census includes 114 models and 649 placements; those counts are inventory,
not output predictions.

Source `6cee8f2f0eb6be7a0811c62223c26b8ebd6b54ba` was built with GCC 12.2.0
and Rust 1.99.0 inside immutable image
`sha256:0aeb54f14590bf1631a27f60b3e7b7eed6dde549f08ba1f5738282e59d4cea1d`.
No dependencies were fetched. Rust decoder/CLI/lockfile are identical to qualified
`1fa4f48d1198188662e4239c94fc71754dad5895`. C++ capture is identical to
`f3826005ce96820291c2cdba48e5fe06b6426cf0`; map_geometry.cpp differs only by
removing its trailing blank line. `identities.json` records file/binary/library
hashes and points to complete compile commands, GLM fingerprints and provenance.

All four captures exited 0, each with 470,745 vertices and 599,874 ordered
indices. Each schema-1 mesh is 8,048,520 bytes and SHA256
`e3f634921cc58ebb6e56fa8e1d7610d623a055aec2dda76f1ebddeee54539025`.
The host independently rehashed all four retained files. Two repeat comparisons
and one C++/Rust strict comparison passed. Numeric diagnostics found zero
changed coordinates, signed-zero changes and nonfinite values. No tolerance was
introduced. C++ wall times were 0.0445/0.0488 seconds with peak RSS
15,776/15,296 KiB; Rust 0.0192/0.0161 seconds with 13,680/13,980 KiB. Two short
runs, including process startup/serialization, are not a benchmark.

This added runner was source-reviewed for pinned selection, scope, repeatability,
ordered comparison, fail-closed exits and cleanup. Rechecking selection/input
identity and `git diff --check` passed. No new analytic, parser, malformed-input,
sanitizer, collision or full server suite was run. Existing two-map tests were
not rerun. No decoder, raycaster, shared manifest or qualification files changed.

Artifacts live at
`/home/bump/.local/state/eqemu-map-translation/additional-map-20261110/`.
`run-01/` retains raw logs, container/source identities, worker exits, complete
provenance and `results/capture/griegsend.{cpp,rust}.{0,1}.mesh`.
The four meshes total 32,194,080 bytes. `inputs/griegsend.map` adds 636,006
original bytes. Preserve that copied original until obtainable provenance is
known. Regenerate derived outputs using the two exact commands in the parent
README and a fresh output path. Deleting only `run-01/` reclaims derived data;
Git source and the retained original image store can reproduce it.

A separate rootless daemon, socket, exec root, PID and copied image store were
used. Direct rootlesskit copy initially failed with
`newuidmap: write to uid_map failed: Operation not permitted`. A distinct user
service successfully performed the same copy; no approval rejection occurred.
This was setup failure, not a failed candidate capture. The disposable container
was removed after successful retrieval. The owned daemon was stopped and its
20 GiB copied image store removed. The original image store, input collections,
two-map originals and other agents' artifacts were preserved. See
`daemon-cleanup.json` and `commands.txt` for exact receipts and setup commands.

This evidence says nothing about groups, terrain, V1, all-map resource bounds,
other toolchains, public collision queries, cache behavior, CXX ownership or
production readiness. No game deployment or Operations publication occurred.

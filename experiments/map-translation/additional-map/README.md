# Additional map smoke sample

Opt-in central-2065. This directory owns a separate manifest and runner, outside
all default build, test and CI paths. Existing two-map evidence is untouched.
The fixed seed was recorded in Beads before selection. The manifest pins the
choice and SHA256 of the sorted candidate list. This samples the V2 header pool
with no groups or terrain, excluding the original two maps. Decoder resource
bounds are not prequalified. No reroll is allowed after a failed capture.

Host Python inventory and transport only:

```sh
python3 experiments/map-translation/additional-map/select.py \
  --census /home/bump/.local/state/eqemu-map-translation/investigation-2026-11-10/asset-census.json \
  --inputs /home/bump/.local/state/eqemu-map-translation/additional-map-20261110/inputs
python3 experiments/map-translation/additional-map/worker.py \
  --socket unix:///run/user/1000/eqemu-map-additional.sock \
  --image sha256:0aeb54f14590bf1631a27f60b3e7b7eed6dde549f08ba1f5738282e59d4cea1d \
  --glm /candidate/build/vcpkg_installed/x64-linux/include \
  --inputs /home/bump/.local/state/eqemu-map-translation/additional-map-20261110/inputs \
  --output /home/bump/.local/state/eqemu-map-translation/additional-map-20261110/CHOOSE-FRESH-RUN
```

Commit reviewed source first. Supply an authorized rootless daemon with that
immutable image loaded. All compilation and map execution happens inside its
network-disabled disposable container. No dependencies are fetched here; if the
image is unavailable use the existing `rust/provision.py` documented workflow.
Two captures per implementation use the same verified bytes. Counts come from
the strict schema reader, which checks lengths, index bounds and input identity.
Repeatability is bit-exact. The unchanged strict comparator runs before separate
numeric diagnostics. Mismatch returns failure; no tolerance or golden promotion.
Raw stdout/stderr, exits, time/RSS, hashes and source/toolchain/library/GLM
identities are retained. This runner adds no parser fixtures or collision tests.

Preserve `inputs/griegsend.map`, copied from the existing collection. Its upstream
download is undocumented. Preserve existing census and two-map evidence. Derived
meshes/builds can be regenerated from that input, this Git source and image.
Delete only an explicitly selected owned run directory to reclaim generated
output. A daemon image store is separately removable after stopping its owned
service. Do not delete original collections or other agents' evidence.

One extra filtered sample cannot establish whole-corpus, other toolchain,
raycaster, cache, CXX ownership or production suitability.

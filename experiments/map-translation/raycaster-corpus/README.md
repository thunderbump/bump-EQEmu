# Installed map raycaster qualification

Opt-in central-fnma experiment. No production code or Rust geometry decoder edits.
`guest.py` calls the standalone production C++ decoder once, checks its ordered
EQMESH01 arrays, and supplies the same immutable bytes to serial C++ and Rust
processes. The unchanged collision driver's seed `0x854f2026` and triangle-based
query recipe produce the reference endpoints. The independent streaming exact
comparator checks classification and all seven valid fields before numerical
diagnostics. Candidate triangle IDs are diagnostic only; no C++ ID parity claim.

The runner inventories every installed `base/*.map` and supplemental
`legacy/base/*.map`, authenticates the deployed counterpart and prior census,
then runs Grieg's End, Qeynos2, PoKnowledge and remaining paths lexically.
The original two maps must reproduce authenticated historical C++ traces.
Header declarations are census evidence only, never proof of parser branches.

All native/Cargo work runs in a dedicated rootless worker. Host Python transports
archives, inputs and receipts. Every map has a separate network-disabled 2 GiB,
zero-swap, two-CPU container with 120-second native stage limits and a 420-second
outer limit. Build has 600 seconds. Limits remain fixed on failure. Existing
adapter count limits and Rust safety limits remain unchanged. No corpus ASan claim.

```sh
python3 -m unittest discover -s experiments/map-translation/raycaster-corpus -v
python3 experiments/map-translation/raycaster-corpus/import_image.py
python3 experiments/map-translation/raycaster-corpus/runner.py \
 --socket unix:///run/user/1000/eqemu-raycaster-corpus.sock \
 --collection /home/bump/Projects/bump-eqemu/bump-akk-stack/server/maps \
 --deployed /home/bump/.local/share/eqemu-upstream/state/server/maps
```

Start the task-owned rootless daemon first, using state directory
`~/.local/state/eqemu-map-translation/raycaster-corpus-20261110/docker` and its
own socket, exec-root and rootlesskit state. Image importer verifies every retained
OCI blob against immutable manifest `0aeb54f...dcea1d` before import.
Only clean committed source is accepted. `--resume` requires identical source,
inputs, configuration and built image identity; completed per-path outcomes are
not rerolled. Interrupted attempts remain beside fresh attempts.

To regenerate one map, use `--run regenerate_griegsend --only base/griegsend.map
--retain` with the same socket/collection/deployed arguments. Never reuse a run
name for a changed source. Successful derived meshes/replays/traces are removed
unless `--retain`; receipts preserve sizes and hashes. Inputs and historical
evidence are never deleted. Failed trace packets remain. Full expanded failure
meshes above the aggregate 2 GiB cap are removed with explicit regeneration
receipts; bounded triangle samples preserve candidate array IDs and coordinates.
Stop/remove only this task's daemon, store and derived artifacts after retrieval.

Results establish installed-asset differential evidence for recorded toolchains.
They do not audit database override selection, prove whole-corpus sanitizers,
qualify CXX lifetime/cache/Map integration, or establish readiness to swap engines.
All-hit/all-miss maps are coverage-limited even if exact. Failed comparisons remain
failures regardless of numerical delta. A reference decoder failure prevents
raycaster coverage and warrants a separate owned Mesh boundary investigation,
not rewriting the reference in this phase.

Build gating includes synthetic candidate reader/replay rejection controls for
schema, identity, index, nonfinite, count, truncation, order and query identity.
The comparator's classification, endpoint, category, count and seven-field
mutation controls run independently. `summarize.py RUN OUTPUT` emits a compact
per-path report only after completeness and uniqueness checks succeed.

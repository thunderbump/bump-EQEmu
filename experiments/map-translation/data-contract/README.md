# Safe geometry boundary qualification

This focused PoC exercises existing Rust behavior. No decoder, raycaster math,
production Map, CXX or client change. No assets are repaired or triangles dropped.

Policy approved for these four paths:

- Structurally empty geometry loads successfully. Bounds are `None`, finite
  segments return `None`, invalid endpoints still return errors.
- Nonfinite geometry returns an explicit load error before engine construction.
  Current diagnostic is `nonfinite or out-of-bound vertex`. Cases manifest supplies
  map path, vertex, axis and original float bits. This is not a valid-empty result.
- This intentionally replaces unsafe native empty-vector access. It is not C++
  raycaster parity for these paths. No unsafe native construction is executed.

`cases.json` freezes original, deployed and retained unchanged C++ export hashes,
counts, nonfinite components and export/toolchain provenance file hashes. The
prior raw-data investigation is at commit
`f569fba04939767302978a2b2dcfe221687d496a`, under
`experiments/map-translation/corpus-investigation/data/`. All nine raw NaN
components were already present in three stored model vertices, with finite
placements. The two distinct logical empty paths have identical input/export
bytes and identities; they are still tested and reported separately.

`worker.py` authenticates inputs and provenance before transporting a clean Git
archive into an exclusive rootless container. `guest.py` builds the unmodified
PoC CLI and dependency plus the standalone `contract-probe`. It runs existing
core tests, focused nonfinite/finite controls, actual empty ownership/query tests,
and CLI rejection/output-preservation controls. Unexpected exits, timeouts,
missing diagnostics and misleading outcome files fail the qualification. Expected
load rejections are reported explicitly, with zero collision comparisons.

Run only in a disposable owned rootless worker, never host Cargo/native execution.
Use the immutable verified offline image and import transport from
`../large-map-qualification/import_image.py`. Reproduction commands and final
receipts are added after execution. Large artifacts remain outside Git under
`~/.local/state/eqemu-map-translation/map-safe-data-contract-20261111`.

Future integration acceptance, not performed here:

- Decide zone first-load failure and reload policy. If preserving the last valid
  map on failed reload is chosen, verify it, including repeated load/unload cycles.
- Exercise client ground, LoS, movement and collision with valid, explicitly empty
  and failed-load states. A caller interpreting `None` as unrestricted space must
  not silently allow walking through the world after a load failure.
- Verify gameplay effects interactively before accepting integration. Existing
  `Result<engine>` and optional bounds suffice at this PoC boundary; this adds no
  speculative state framework and proves no client interaction safe.

## 2026-11-11 result

Executed/pushed source `d22537c011bcd457cd36765408d8ec07bf7d97b2`.
Later receipts/report commits do not change executable sources. Base is
`74394fe21aed226ca376e0b99dd8e2360b6fc94f`. No shared Rust code changes.

- Breedinggrounds and chapterhouse: expected CLI exit 1, diagnostic
  `nonfinite or out-of-bound vertex`, no completed engine/build phase, no new
  output, existing output sentinel unchanged. Two checks per asset.
- Firiona_lit and warslikswood_lit: each has 64 reader/load/drop cycles,
  `bounds=None`, 14,336 finite no-hit queries, 384 invalid-endpoint errors,
  plus one CLI no-hit replay. Both logical paths recorded separately.
- Ten existing core tests and two focused contract tests pass. Contract tests
  check 24 core nonfinite combinations, finite coordinate boundaries and a
  useful finite triangle hit. Serialized fixtures add 24 nonfinite CLI
  rejections, six parser-corruption rejections, and two positive CLI loads.
- 44 bounded subprocess checks total. Zero collision comparisons and no
  invented NaN ray outcomes. The original two frozen differential regressions
  were not rerun because the shared reader/core/CLI are unchanged; their pinned
  prior results remain historical evidence, not results of this run.
- Rust/Cargo 1.99.0 in verified immutable image, frozen/offline release build.
  2 GiB memory/no swap, network disabled, two CPUs, 128 PID cap. Recorded
  cgroup peak 91,885,568 bytes; all OOM counters zero, no timeout, final exit 0.
  No Rust sanitizer coverage is claimed. Host headroom before run was 119 GiB
  disk and about 25 GiB available memory.
- Own container removed, image pruned, service stopped successfully and own
  store/socket/exec-root removed. Original assets/retained exports untouched.
  Large run outputs and candidates remain in the exclusive output root.

Compact receipts live in `reports/`. Review was direct inspection of the harness,
manifest, source diff and all run results; no review sub-agent was used.

## Reproduction

From a clean checkout at the executed source, provision a unique user-manager
rootless daemon using `systemd-run --user`, separate rootlesskit state, socket,
data-root, exec-root and pidfile. The exact unit was
`eqemu-map-safe-data-contract-20261111`; the successful pattern is documented in
`../large-map-qualification/commands.txt`. Do not reuse another task's daemon.
Import using verified `../large-map-qualification/import_image.py`, then run:

```sh
python3 experiments/map-translation/data-contract/worker.py \
  --socket unix:///run/user/1000/eqemu-map-safe-data-contract-20261111.sock \
  --output /home/bump/.local/state/eqemu-map-translation/map-safe-data-contract-20261111/qualified
```

For reproduction choose a new exclusive output directory and unique rootless
paths. The original output already exists and must be preserved. Source/command,
image-import, cgroup/exit and cleanup receipts are tracked. The OCI transport
verifies every immutable blob before import. Never execute Cargo/candidates on
host, and never construct the unsafe native empty-vector raycaster.

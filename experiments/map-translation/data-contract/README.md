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

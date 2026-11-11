# 2026-11-10 nonfinite and empty asset investigation

central-42r2, child of central-fnma. Investigation only. No decoder, raycaster,
input, runner, manifest or limit changes. PR108 remains blocked on seven paths.

## Confirmed findings

All ordinals and offsets below are zero-based. Raw offsets address the inflated
V2 payload, not compressed file positions. `findings.json` preserves identities,
raw vertex/polygon/placement byte excerpts and complete affected export records.

- Breedinggrounds stores three negative quiet-NaN components, all `0xffc00000`,
  at offsets 5792314, 5792318, 5792322. These are model 260,
  `OBJ_maus_InnerWalls.MOD`, vertex 1613. Visible polygon 2177 at 5837703
  references it as corner 2; placement 234 at 6619240 emits output vertex
  239353, referenced once by triangle 95711. There are no other nonfinite
  exported components, raw model vertices or base vertices.
- Chapterhouse stores six components with the same bits, at offsets
  253650/253654/253658 and 253686/253690/253694. Model 14,
  `OBJ_BFS_HallwallH.MOD`, vertices 323 and 326 are referenced by visible polygons
  536 and 537 at 263642 and 263655, corners 2 and 0. Placement 556 at 8970529
  emits output vertices 1450240 and 1450241, each referenced once by triangles
  483815 and 483816. These two consecutive output vertices form the entire
  nonfinite cluster. No other nonfinite exported components or raw vertices.
- No infinities or signaling NaNs were found. All placement operands are finite;
  affected placements have scale 1, rotation [0,0,-1.5707964897155762].
  Checked names, counts, polygon indices and final cursor consume exactly
  6706612 and 9036188 inflated bytes. Both maps declare zero groups/tiles.
  The raw NaNs precede every rotation/scale/axis swap. Finite arithmetic overflow,
  transform order or a terrain path is therefore unnecessary to explain these
  observed nonfinite outputs. This is confirmed invalid stored geometry, not a
  demonstrated transform defect. Asset generation intent remains unknown.

The independent fixed-offset checker does not traverse models or reproduce
transforms. It checks exact raw vertex bytes, IEEE exponent/mantissa/quiet bit,
polygon indices, placement name anchor and finite transform bit patterns.
The checked parser separately counts visible polygons to map raw records to all
exported NaNs and verifies every index reference. No second transformation copy
is used as a correctness oracle. There is no instrumented native source.

## Empty V1 files

Both `_lit` files are identical 39-byte V1 assets. Their 14-byte header declares
zero faces, one node, zero facelist entries. The remaining 25 bytes decode as
four bounds `[+999999995904,+999999995904,-999999995904,-999999995904]`, flag 1,
zero face count and zero offset. These match the deprecated azone2 producer's
empty-face sentinel initialization and leaf serialization
(`utils/deprecated/azone2/azone.cpp:251,346,441`). This is strong evidence of an
empty producer result, not a truncated face payload or a wrong V2 parser branch.
It does not prove the historical tool/version, intentional placeholder use, or
that `_lit` means unused auxiliary data. No local provenance receipt establishes
those claims. Ordinary firiona/warslikswood counterparts exist and have nonzero
V2 declarations; see `counterparts.json`. They are distinct assets, not evidence
that selecting them preserves the intended geometry.

`MapGeometry::LoadV1` returns true for zero faces. `Map::LoadV1` then forms
`&verts[0]` and `&indices[0]` on empty vectors (`zone/map.cpp:387`), a confirmed
unsafe empty boundary. We did not execute that known UB. Standalone map-capture
only exported the empty arrays safely. Rust acceptance of an empty serialized
mesh does not qualify native Map integration. `zone/zone.cpp:1443` selects a DB
map_file_name override or short_name; `Map::LoadMapFile` constructs base/<name>.map.
Active DB selection and MMF cache behavior were not audited.

## Minimum followup

1. Add explicit finite-coordinate admission to the owned decoder-to-Mesh boundary
   with asset/record diagnostics and checked parsing. Reject these two inputs
   before raycaster construction. Preserve raw excerpts as rejection fixtures.
   Repairing/re-exporting upstream assets requires a separate data-owner decision;
   replacing NaNs, dropping triangles or changing transforms is unsupported here.
2. Define the empty-asset contract explicitly. Either reject empty maps before
   native vector access, or represent a valid empty map with defined no-hit behavior
   without constructing the native mesh. Decide which DB-selected assets are
   expected to be empty; never silently exclude `_lit` filenames from qualification.
3. Qualification tooling needs explicit expected-rejection/empty-policy tests and
   separate status from raycaster comparisons. These four paths still provide no
   C++/Rust raycaster compared outcomes. No modified-input pass is claimed.

Confidence is high for stored NaN localization, all affected output ordinals,
empty structure and unsafe empty native boundary; moderate for the legacy producer
inference; unknown for historical generation cause and active selection intent.
A combined Rust decoder/raycaster behind an owned Mesh interface could enforce
these contracts once, but these inputs do not show both modules failing and do
not justify replacing both. A narrow admission/empty contract is the smaller step.

## Reproduction and identities

Clean assigned worktree started at cd66dd0a6234fdb519046716041e0aaf231321c0,
then fast-forwarded on t3/map-corpus-data-gaps to
2d6cc88a5ea891f79b72fc0e9530b35d7b2caec8. Native source is exactly that commit,
unchanged production `zone/map_geometry.cpp`. Original corpus execution was
c319cb077cbf5c2bfbf82544f3eab125e5e3277e. Investigation Python is analysis only.
All four regenerated official exports passed `cmp` against retained geometry.
`execution.json` records original production binary/source hashes, worker state,
image, analysis hashes and memory peak. `inputs.json` records current/deployed
identical hashes, matching supplied failures. Input and old evidence are untouched.

All parsing and native execution used the immutable offline image manifest
0aeb54f14590bf1631a27f60b3e7b7eed6dde549f08ba1f5738282e59d4cea1d,
in the task-owned rootless daemon, network disabled, 2 GiB memory/swap ceiling,
2 CPUs, 128 PIDs, with a 2 GiB parent service cap. No sanitizer claim, candidate
raycaster run, CXX/full-server qualification or game deployment.

Host command after starting the owned daemon and importing verified OCI blobs:

```sh
python3 experiments/map-translation/corpus-investigation/data/transport.py NEW_RUN_ID
```

Use an identifier for NEW_RUN_ID. Transport requires an absent run directory.
The daemon socket is unix:///run/user/1000/eqemu-corpus-data.sock. Import follows
raycaster-corpus/import_image.py with only root/socket redirected to this task.
The dedicated user service is eqemu-corpus-data-investigation. A direct launcher
failed UID mapping; the user service succeeded. No detector suppression was used.

Large reports, inflated payloads, full regenerated meshes, compiler output,
container inspections and verified OCI import receipt live only in
~/.local/state/eqemu-map-translation/corpus-data-gaps-20261110/.
`reviewed/` is the final run. Earlier local passes remain distinguishable.

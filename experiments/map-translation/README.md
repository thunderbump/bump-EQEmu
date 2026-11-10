# Map translation evaluations

Translation-project-only experiment. This directory is not registered with CMake,
CTest, Cargo, the normal utility suite or CI. No runnable evaluation exists yet.

Branch: `experiment/map-translation-evals`.
Baseline: `78c07284908931297ea72e25da358965b60ee913`, including the reviewed but
runtime-unqualified packed-map scalar alignment fix.
Task: `central-qfiz`.

## Inputs and status

`corpus.json` pins the two initial map files. Input bytes and copied bytes were
both checked against its sizes and SHA256 values during setup. These are samples
for proving the evaluation mechanism, not the complete existing-map collection.

Local data root:
`/home/bump/.local/state/eqemu-map-translation/two-maps-v1/`

- `inputs/`: verified copies of `qeynos2.map` and `poknowledge.map`.
- `reference/`: reserved for captured C++ reference outputs; not generated.
- `candidate/`: reserved for translation output; not generated.
- `results/`: reserved for comparisons; not generated.

Keep all large inputs and generated output outside Git. Manifest and evaluation
code belong on this branch. No reference outputs, parser census, triangle counts,
repeatability measurements, performance measurements or passing evaluations have
been produced.

## First evaluation acceptance criteria

1. Use the production `Map::Load` path, with MMF bypassed, and capture ordered
   float32 vertices and uint32 triangle indices immediately before
   `createRaycastMesh`. Do not copy transformation logic into the test driver.
2. Use a small offline driver; no world, zone, database or live deployment needed.
   Extraction/link dependencies remain unproven.
3. Record source commit, patch/source hashes, compiler/options, architecture,
   floating-point settings, dependencies, input hashes and output schema version.
   Serialize fields explicitly, not padded native structs.
4. Capture each map twice and require exact repeatability. Check counts, index
   bounds, ordering and raw float bits. Do not sort, weld, round or normalize.
5. Demonstrate that the comparator rejects an altered vertex, changed index,
   reordered triangle and truncated output. Identify the map and first differing
   element; report numeric deltas without treating them as automatic tolerances.
6. Add a small independently calculated geometry case to avoid relying solely on
   the existing implementation. Decoded-record capture can localize mismatches;
   final ordered-mesh parity is the acceptance boundary.
7. Measure elapsed time, peak memory, expanded geometry and output size. Use these
   results to estimate the cost of the complete existing-map corpus.

Unchanged raycaster inputs provide bounded geometry parity, not proof of bridge
ownership, shutdown behavior or arbitrary future inputs. Future maps are outside
this project's stated compatibility target.

## Execution boundary

A previous delegated parser-test/build task was automatically rejected for
possible cybersecurity risk. This bootstrap only copies and hashes data and
records the contract; it does not retry that task or resolve the rejection.
Production parser capture and runtime qualification remain outstanding. Candidate
execution must follow the authorized disposable-worker path, never execute on the
host. Keep runtime failures distinct from ordinary file inventory checks.

The authoritative options and worklog are in Operations, under project
`bump-akk-stack`, plan `2026-11-09-rootless-validation-and-map-options`.

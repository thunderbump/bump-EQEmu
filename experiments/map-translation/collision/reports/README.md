# 2026-11-10 collision evaluation evidence

Executed driver and orchestration commit is pinned in source.json. Later commits
add reports and documentation only. The ordinary profile passed 17 process
checks: analytic controls, two maps and fourteen deliberate rejection checks.
Each map has 5,504 comparisons, with zero disagreements or numeric deltas.
Qeynos2 has 3,231 hits / 2,273 misses; poknowledge 3,354 / 2,150.
The analytic run includes 32 lifecycle/perturbation cycles, seven field comparator
controls per cycle, shared-seam/adjacent-float tests and a collinear-face miss.

The diagnostic profile failed. Five sanitizer qualification controls produced
the expected statuses and symbolized reports. Analytic assertions completed but
LeakSanitizer reported 816 bytes in 66 lost allocations. Qeynos2 completed exact
queries, then reported 582,840 leaked bytes in two allocations. Poknowledge was
killed with exit 137 at 2,083,960 KiB RSS under the 2 GiB worker limit; it has no
sanitizer outcomes. Fourteen reader rejection checks still completed as expected.
Normal arrays are allocated by the production constructor at raycast_mesh.cpp:806,
then its pointer is overwritten by NULL at :819. This is a concrete production
lifecycle defect, outside this change's file ownership. No suppression or repair
was applied. Qeynos2's diagnostic peak includes leak checking and was 2,070,048
KiB. Ordinary peaks were 19,624 / 100,224 KiB, roughly 0.07 / 0.31 seconds.
Consult retained .time files for exact current-run measurements.

All attempts, including failed build/expectation/symbolizer setup and diagnostic
memory failures, remain in the owned external root listed in summary.json.
Some early diagnostic orchestration attempts stopped while parsing the empty
stdout of a killed process. The reviewed run records that failure explicitly
and still writes a complete summary. No historical failure was overwritten.
All containers were removed, the dedicated daemon stopped, and its imported
image store and transfer archive removed. Receipts include actual container IDs.
Other agents' daemons, original inputs, meshes and live game services were left
alone. Reports contain no large geometry payloads. Ordinary exact traces total
2,105,315 bytes and are retained outside Git. Additional diagnostic/earlier traces
can be removed only from this explicitly owned output root after review.

The reused C++/Rust source and image provenance is copied verbatim into
reused-provenance.json, including historically created-state container metadata.
That metadata is not proof of installed state in this worker; compiler identity,
compile commands, library hashes and the final worker receipts record this run.
No Rust binary is rebuilt or instrumented. These results do not establish
whole-corpus parity, other-toolchain behavior, CXX lifetime/cache correctness or
full Server startup/build correctness. Next work should separately authorize the
small constructor leak fix and repeat bounded diagnostics with a resource budget
decision before integration or production claims.

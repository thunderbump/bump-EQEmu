# 2026-11-11 Zone collision reload repair

Executed/pushed source `4ce5d7391a048f609b037817b772d6c15ea482d6`, based on
`1a2ef74ec19899d2d97b429878e5832b42e6d5af`. Draft PR113 targets PR112's
`t3/rust-map-server-validation`. Later commits contain evidence only.

Zone loads a scoped candidate, keeps the current collision map on failed load,
logs retention, and installs a successful candidate before deleting the old map.
Zone cleanup still owns the final map. Queries borrow the map on the zone thread;
the source audit found no stored Map aliases in Zone callers. Installation has
no allocating/throwing operations. Factory exceptions retain the old pointer;
internal pre-existing factory exception handling is unchanged. Watermap/pathing,
first load, default C++ selection, timers/combat/teardown are unchanged.

Shared production-decision regression passed failed first load, two rejected
candidates, throwing loader, changed successful replacement, valid empty candidate
and exactly-once destruction. Both ordinary full default target builds passed
incrementally, OFF 8.22s and ON 8.92s, then all 95 utilities per backend. These are
not clean builds. Both reused copied qualified artifacts at the same guest paths.
382/383 native object hashes remained identical per backend; only zone.cpp.o
changed bytes. ON also rebuilt the adapter to identical bytes. Frozen/offline
Cargo helpers passed; Rust source unchanged, no new Rust-unit or in-server suite
claim. Full dependency/feature selection and compile commands match prior builds.

OFF zone SHA256 `7a0cb456c697c3fad5c5350cf84979f23b2c7d7940399ac16fce9171a8fc5b81`.
ON zone SHA256 `18025f30ef137f235e23598258100e1edf59db17ea40858075ca64876e448d90`.

Each backend ran once with a fresh public PEQ DB, 221 tables, version 9328/0/0,
both original zones loaded, world connections proven and six protocol responses
per zone. Production Perl FindGroundZ observed `7.62939453125e-06` initially,
the same value after missing-map and invalid-header reloads, `42` after the valid
replacement, then the original value after recovery. Processed load attempt
counts were 2→3→4→5→6 in each backend; every phase had subsequent query timestamps
and 7–9 queries. Both failures emitted retention logs. The four-byte unknown
version rejects before decoding. The changed valid fixture is a finite nonempty
V1 floor, not NaN/empty C++ input. No new direct LoS or client checks are claimed.

Both reload verdicts passed. Separately, both probes quiesced and each backend's
qeynos2, poknowledge, world and database exited 0. Prior intermittent teardown and
combat failures remain unresolved; these runs do not establish general stability.
Sanitizers and interactive clients remain explicitly deferred.

Only disposable rootless native execution, networking disabled, no sudo or host
package changes. Builds used one job and authorized 4GiB/no swap; build peak
2364256256 bytes, no build max/OOM events. Runtime limits 2GiB/no swap, peaks OFF
1347735552 and ON 1295052800 bytes, no max/OOM events, each 84.7s. Daemon unit peak
4GiB with 1395 max events during its complete image-import/build/runtime lifetime,
zero OOM/kills. This unit pressure is distinct from workload cgroups.

Evidence root: `~/.local/state/eqemu-map-translation/zone-map-reload-20261111`.
`source-delta.json`, `production-diff.patch`, `reuse-{before,after}.json`,
`dependency-copy-proof.json`, image identities, phase argv/RSS/cgroups, binary
hashes, runtime query logs/receipts and cleanup receipts retain raw proof.
`results.json` is the small checked-in summary. Repro commands from README:
`python3 tests/map_reload/validate.py build`, then `OFF`, then `ON`.
Operator must prepare its unique daemon and verified retained images first.
The initial host copy failed before execution on guest-only symlink dereferencing;
its record is retained, transport corrected, then fresh copies used. No runtime
rerun to obtain green. Prior artifacts untouched; original map hashes unchanged.
All owned containers removed, unit inactive, socket absent. Generated caches,
first-copy tree, OCI archive, builds and runtime/DB volumes are removable under
this task's evidence root after evidence retention review. No live world changes.

Operations worklog is authoritative for cross-project handoff. Parent should
review PR113 and decide acceptance/Beads closure; no automatic merge, deployment
or Rust default change.

## Parent packaging review

Parent review moved the byte-identical replacement helper from tests/map_reload
to zone/replace_collision_map.h and updated production/test includes. Source
`346d5c0b7142234c2afc092af702a2bb5765f10b` passed the ownership executable and both production OFF/ON zone.cpp
compile commands inside a separate rootless 2 GiB/no-swap container. This is
focused compile/ownership validation, not another full-target build or runtime
run. The earlier executed `4ce5d7391a048f609b037817b772d6c15ea482d6` remains the
source identity for the disposable runtime results above.

The first parent compile attempt stopped on a precompiled-header mtime mismatch.
Only byte-identical source timestamps were restored from the qualified snapshot,
then the focused checks passed. Both attempts and exact commands are retained at
`~/.local/state/eqemu-map-translation/zone-map-reload-parent-review-20261111`.
The production ownership helper has SHA256
`6d28ef10c980a62f7ce71e9a2bf67d4cad8dbfeefc1cb2cd2ce86d101cc6173b`, unchanged by relocation.

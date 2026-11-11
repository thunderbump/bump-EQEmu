# 2026-11-10 raycaster memory baseline

Executed and pushed source: 30d7a13e7e2be870913eeaa0cae35ed67dd42abd.
Later commits contain documentation and evidence only. Draft PR 106 stacks on
PR 105. Parent acceptance remains outstanding for central-ns74 and central-mqee.

Both ordinary and 4 GiB diagnostics passed 11,008 original queries, 6,585 hits
and 4,423 misses, with exact endpoint/order identity and seven valid hit fields
against both retained ordinary outcomes. Each map used separate reference and
candidate processes. Analytic lifecycle, reversed winding, shared seam,
perturbed geometry, comparator mutations and fourteen reader rejection controls
passed. Diagnostics qualified all five sanitizer controls, with symbolized
reports for the four intended failures. Full exit-time LSan passed analytic and
each actual mesh process. No suppression or quarantine tuning was used.

GNU time peak RSS, KiB, reference/candidate:
- Ordinary qeynos2: 11,464 / 12,136.
- Ordinary poknowledge: 47,504 / 47,304.
- Diagnostic qeynos2: 1,041,056 / 1,041,000.
- Diagnostic poknowledge: 2,952,476 / 2,953,692.

Phase observations use /proc status; GNU time uses resource accounting, so their
peaks differ slightly. For diagnostic poknowledge reference, RSS was 53,124 KiB
after read, 2,945,060 after build, 2,946,672 after array release, 2,950,592 after
queries, and 2,954,032 after engine release. Cgroup current at release was
3,056,717,824 bytes, then 40,812,544 before candidate startup. Candidate release
current was 3,058,315,264 bytes. Container peak reached 3,058,315,264 bytes.
All observed memory.events max/oom/oom_kill counters stayed zero. Admitted
memory.max was 4,294,967,296 and memory.swap.max was zero, also verified in
container inspect and runs.json. Ordinary admission was 2,147,483,648 bytes,
no swap.

Construction dominates the diagnostic gap, rather than query generation or
holding two trees alone. The production recursive split uses temporary
page-aligned triangle vectors. Their allocator and sanitizer overhead are a
plausible contributor, not an allocation-level measurement. Ordinary build RSS
is tens of MiB; instrumented build RSS is GiB with the same geometry and
toolchain. Freeing arrays/tree did not reduce diagnostic RSS. Exit reclaimed
the process residency. This supports using separate engine processes. It does
not attribute the earlier SIGKILL definitively: that run retained no cgroup
OOM counters. The constructor leak fix is small, not a cure for GiB pressure.

The controlled 4 GiB decision is sufficient for these samples. A 2 GiB follow-up
was omitted because a single successful poknowledge engine exceeded it. Do not
raise limits or tune away diagnostics automatically. No whole Server build,
game deployment, Rust raycaster implementation or production qualification.

Exact worker commands, source/archive/binary/library hashes, compiler commands,
timings, phase logs, cgroup samples and cleanup receipts are adjacent. Full
container inspect, worker logs and traces remain under
~/.local/state/eqemu-map-translation/raycaster-memory-20261110/{ordinary-01,diagnostic-4g-01}.
The isolated image was imported from verified retained OCI blobs; its receipt
is image-import.json. The containers were removed, owned store pruned, daemon
stopped and owned archive/store removed. Existing original maps, previous failed
evidence, other daemons and game services were untouched.

Next bounded Rust checkpoint: define one standalone engine interface over the
existing checked arrays, replay these immutable rays, compare public hit/miss
and valid fields, and establish explicit tie/normal semantics before any CXX
integration. That implementation is outside this change.

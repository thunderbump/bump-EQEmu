# 2026-11-11 ordinary full Server qualification

Executed Server source `239480beac57c78770a209b8fb4f3a2b79fd68b3`, the integration
head of PR111. No production Server source or Stack repository changes were
needed. Qualification tools are on `t3/rust-map-server-validation`, draft PR112,
based on `t3/rust-raycaster-integration`. Rust remains default OFF.

Artifacts are retained outside Git at
`~/.local/state/eqemu-map-translation/rust-map-server-validation-20261111`.
This report records ordinary checks only. Sanitizers and interactive client
qualification remain explicitly deferred. Prior sanitizer diagnostics were not
modified or rerun.

## Full dependency environment and builds

Used Stack's reviewed user-service rootless path, with unique unit
`eqemu-map-server-validation-20261111`, matching socket under `/run/user/1000/`,
and task-owned Docker store under the artifact root. No sudo, host package
installation, security setting changes, live game processes or live databases.
The default Docker context was not changed. Existing image export was read-only.

Full builder `sha256:dc2003a3fb395b63706f6dd975cb29e94c18fccf04784b4c47dab00d913f1894`
was copied from the retained staging builder. The real installed export came
from `eqemu-staging-commission/2026-10-30/code/build/vcpkg_installed`, with the exact
manifest baseline `d1ff36c6520ee43f1a656c03cd6425c2974a449e`. Both builds consumed
that copied export through its supported vcpkg toolchain and
`VCPKG_MANIFEST_INSTALL=OFF`. No fake package configs or empty-toolchain fallback.

Compiler Clang 14.0.6, CMake 3.25.1, Rust 1.99.0; Boost 1.89.0, fmt 12.1.0,
GLM 1.0.2, zlib 1.3.1, MariaDB client 3.4.7, OpenSSL 3.6.0, LuaJIT 5.1.4,
Perl 5.32.1. websocketpp `b9aeec6eaf3d5610503439b4fae3581d9aff08e8`.
`packages.json`, `dependency-files.json`, `toolchain-hashes.txt`, and the configure
logs retain versions, package ABIs, config/header/library hashes and executable
fingerprints. The cosmetic top-level MariaDB detection message says MISSING
because it checks a different variable; required unofficial-libmariadb config
resolution and complete binary linking succeeded.

Both fresh builds used RelWithDebInfo, one job, separate native and Cargo output
directories, all SERVER/TESTS/LOGIN/LUA/PERL/CLIENT_FILES options ON,
`EQEMU_SANITIZERS=OFF`, no compiler semantic/optimization downgrades, frozen/offline
Cargo and networking disabled. Full default builds completed, OFF 403 steps in
937.2 seconds; ON 404 steps in 949.3 seconds. Each passed all 95 utility tests and
10 raycaster tests. CTest inventory is empty, so these are real `bin/tests`
results, not a CTest claim. Separate actor checks passed 43 unit/integration tests,
one doctest, actor Clippy with warnings denied, and both workspaces' formatting.
The exact Rustfmt/Clippy components were provisioned inside a separate derived
container; the two native builds used the same unchanged builder image.

Both configure format controls passed. Production compile commands have
`-Wformat -Werror=format` on actor_body, mob and inserver_tests; the common sample
has neither. Neither build contains sanitizer compile flags. OFF build.ninja has
no map Cargo helper/archive; its separate raycaster unit tests deliberately use
Cargo after the native build. ON has the map helper and archive; the linked zone
contains RustRaycastMesh and bridge symbols. Both retain the pre-existing actor
bootstrap symbol. See `backend-build-proof.json` and `{OFF,ON}-link-proof.log`.

Zone SHA256 OFF `b323df011aa7065d807602a20cb089316ffb9c6c2a971865f66200afd0f49c9a`;
ON `18a27e53a2949cbd29252f8112a5ad9d320a93aaf3a12ff788aa34ca5a7fb005`.
All binary hashes are in `results/{OFF,ON}-binaries.json`.

## Resources and retained first failures

Started at 2 GiB/no swap. One-job Lua compilation reached the user unit's 2 GiB
cap with repeated memory.max pressure and ample host available memory. Used the
single authorized increase to 4 GiB, without increasing compile parallelism.
`resource-increase.json` retains the prior counters and host memory snapshot.
OFF phase cumulative container peak was 2,631,532,544 bytes; final combined
OFF/ON container peak 3,840,131,072 bytes. These include filesystem cache and are
cumulative within one container, not per-process heap sizes. No build-container
OOM events. Sampled clang peak RSS was 1,410,556 KiB. Sampling began during OFF
compilation; early small phases have cgroup snapshots but no process RSS samples.
A later optimization can reduce the full worker's physical-memory requirement.

First runtime-image provisioning failed at its separate 768 MiB cap during
package extraction. The retained retry used two GiB and single-thread package
extraction and succeeded. First expanded Rust check stopped because Rustfmt was
absent; pinned component provisioning succeeded, then all those checks passed.
No failures were relabeled as passes. Image identities, logs and container states
are retained for both attempts.

## Disposable fixture and ordinary runtime

Used retained public `runtime-qeynos2-inputs-20261025` PEQ archive
`peq-1787356814.zip`, its fixed five-member import order and hash manifest.
Verified 221 tables and DB version 9328/0/0 before native startup, matching Server.
No live credentials or player data. Fixture originals stayed read-only; all SQL
derivation, map copies, generated shared data and runtime files were task-owned.
Retained water/navigation assets support the existing in-server scenarios.

Original Qeynos2 SHA256
`040343c54e3b02bfd1637ce8fca8839dd676e17c41fb24c042ccfa0bd649435c`;
PoK `794b618d86852b47bd593424129aaf34eee326e5e3fec7290aeeb0e32bc41d20`.
They exactly match the retained fixture base maps. `fixture-inventory.json` and
`fixture-manifest-v2.json` retain the other archive/input identities.

Runtime image `sha256:93429edbf01c1631611af6c88220405136072afd78dc65b6ebf47415b54c38e1`
adds disposable MariaDB to the same builder. Rust check image
`sha256:8ce34dd2c93db82384ae5dceb9f9df05369a6e8721e98b3075b511857768952e`
adds the pinned formatter/linter. Runtime containers use 2 GiB/no swap, no
networking or published ports, and their own namespace and data directories.

Both backends proved both original map loads, world connections and six echoed
protocol challenges per explicit zone port. Successful reload and recovery reload
worked. The fixture Rat's production Perl FindGroundZ query returned
`7.62939453125e-06` before failure, `-99999` after a new failed load attempt, then
the original height after restoring the copied asset. Retention therefore FAILS
in both backends. `Zone::ReloadMaps` replaces its pointer with a fresh
`Map::LoadMapFile` result; this bypasses the earlier same-Map transactional engine
install checks. This concrete C++ baseline defect is outside integration-caused
source-fix scope. No source workaround was added.

Earlier probe-active shutdowns included a nonzero RustON zone exit and intermittent
C++ heap-corruption diagnostics. They remain unresolved first-failure evidence.
The final probe explicitly retires its own timer before native shutdown. RustON
then had all four exits 0; the last C++ probe retry still required killing Qeynos
after shutdown stalled. An earlier C++ full-fixture probe had all four exits 0.
This intermittent teardown failure remains unqualified; timer retirement did not
establish a repair or qualify arbitrary quest teardown.
The in-server mode uses the original quest fixture without this added probe.


## Final outcomes and remaining gates

Both native full builds and utility/core checks passed. The original-quest
in-server suite finalized 21/21 assertions and clean DB/world/zone exits with
RustON. C++ had 21/21 assertions followed by a teardown failure in the first
probe-added fixture, then a clean-exit 20/21 result in the original fixture:
`rust_native_qeynos_melee` reported native refusal/range/flee/defeat. One unchanged
repeat passed 21/21 with clean exits. The repeat does not erase the earlier
failure. No test expectation, NPC combat setting, warning or Server source was
changed to obtain that result. See `OFF-runtime-inserver{1,2,3}` and
`ON-runtime-inserver1`, each with its exact frozen harness identity.

Both original-map startups, ordinary queries, successful reload and recovery are
proven. Failed reload retaining the engine is disproven in both modes. RustON's
final probe and original in-server run had orderly exits. Intermittent native
teardown errors and the baseline melee-test failure remain separate blockers to
calling the entire ordinary runtime stable. There is no full gameplay safety,
sanitizer or client acceptance claim.

The build archive hash is
`477dbd6ffce6b5c47694457512554f01efd155ee7dd363e17c0ef76722d935aa`.
The build ran the first captured guest harness, before its observation/transport
review fixes; its production source and configure/build/test argv are unchanged.
`harness-identity.json` preserves executed versus committed runner hashes.
Runtimes used committed/pushed qualification tools, frozen per invocation under
`source.json` with byte hashes. Final production source remains 239480 throughout.

Reproduce the full build from the prepared export with `run.py`; reproduce these
specific runtime recipes using `launch_runtime.py OFF|ON UNIQUE_LABEL` from the
repo root. Labels starting `inserver` run the full original-quest suite; other
labels run both-zone reload/query checks. The task-specific launcher expects the
retained artifact root/image/export and creates a fresh output directory. Raw
Docker argv is retained in each `command.json`; build argv and the single memory
increase are in `build-command.json`. CMake/compiler arguments, exit/timing/limit
snapshots, RSS samples and every first failure remain outside Git. Never reuse an
existing run label and call it a clean run.

Next work is to resolve the whole-Zone failed-reload contract and obtain an
ordinary teardown/melee reproducer with agreed scope. Their presence in C++ is
not permission to repair unrelated Server code here. Client walking/placement,
stairs/slopes/walls/falling/LoS/empty/load-failure/reload checks remain later, as do
all deferred sanitizer controls and coverage. No automatic merge, game deployment
or default-on change.


## Owned cleanup

All remaining task containers were inspected, confirmed stopped and removed.
The rootless user unit is inactive and its socket is absent. `final-cleanup.json`
and `containers-cleanup.json` retain the receipts. Original map hashes were
rechecked unchanged. Task-owned images/store, dependency/Cargo caches, generated
DB directories, binaries and raw evidence remain under the artifact root. They
can be removed together after evidence retention is no longer needed; no service
is left running. The default Docker context and live game Stack were untouched.
Operations publication is separate from game deployment.

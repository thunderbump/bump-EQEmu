# Ordinary full Server qualification

Guest-only native/Cargo execution. Sanitizers are explicitly OFF. Client checks
remain a separate gate. A configure or utility pass is not server startup proof.

`run.py` captures clean committed Server source and initialized websocketpp into
a new artifact directory, checks the selected daemon is rootless, and runs
`guest.py` offline at 2 GiB/no swap. It removes its container even after failure.
The operator owns daemon startup/shutdown and prepared dependencies. Use Stack's
reviewed `validation/rootless-prototype/run` user-service recipe with unique
unit/socket/store names. Never change the default Docker context or host packages.

Prepare a compatible full Server image and a real vcpkg export inside disposable
workers. `--deps` must contain `vcpkg/scripts/buildsystems/vcpkg.cmake` and
`installed/x64-linux` plus installed package metadata. This runner uses supported
`VCPKG_MANIFEST_INSTALL=OFF` to consume that export without reinstalling packages.
It does not remove package requirements or synthesize package metadata. Record
baseline, package status/ABIs, image/compiler identities and hashes of all inputs.
Populate a task-owned Cargo home using `cargo fetch --locked` for both actor and
raycaster manifests in a disposable provisioning container. Qualification runs
use frozen/offline Cargo through the unchanged production CMake helpers.

```sh
python3 tests/map_server_validation/run.py \
  --socket unix:///run/user/1000/UNIQUE.sock \
  --image sha256:IMMUTABLE_IMAGE_ID \
  --output /absolute/new-run --deps /absolute/prepared-deps \
  --cargo /absolute/task-cargo-home
```

OFF and ON use distinct build and Cargo directories, RelWithDebInfo, Clang,
all Server/test/login/client-file/Lua/Perl features, and the zone format gate.
The complete default build runs with one job. Each phase retains argv, exit,
time, cgroup limits/events/peak, sampled process RSS and cumulative child max RSS.
Build failures do not prevent the other backend from being attempted. Inspect
`runs.json`, raw logs and container exit; transport success is not build success.
Utility tests run only after a complete default build. Inventory from `ctest -N`
is advisory because this repository's utility runner is `bin/tests`.

Startup/reload/shutdown requires a separate disposable database/runtime fixture
and exact built binaries. This runner does not claim that coverage or modify live
services. Keep original maps read-only. Keep resource increases explicit and
retain the first failure; do not rerun into an existing build directory and call
it a clean build. Large artifacts stay outside Git.

`runtime.py` adapts the reviewed Stack rootless smoke recipe at
`b9c881a6b24956f398699a34f777e330e049f740`. Run it only inside a disposable
runtime worker with MariaDB server installed. Mount fresh writable `/runtime`
and `/runtime-results`, read-only `/binaries`, `/fixture`, `/fixture-manifest.json`,
and `/originals`. The manifest uses the retained public fixture's `files` records.
It hashes every declared fixture input, imports the fixed public SQL members,
keeps one original North Qeynos Rat, and boots both original zones. A fixture-only
Rat quest calls the production Perl `FindGroundZ` API each second. No real player
or deployment state is used.

The check requests map reload through the localhost world console. It removes
only the disposable Qeynos map copy for a failed reload, restores it in `finally`,
and checks ground-query recovery. Failed-reload engine retention is a separate
boolean assertion. An unchanged baseline failure must be reported, not silently
repaired as part of the Rust integration. Every owned database/world/zone process
is terminated and awaited; nonzero exits prevent a cleanup pass. This is a
headless check and cannot establish client movement safety.

Set `EQEMU_MAP_INSERVER_CHECK=1` for a separate fresh invocation of the complete
source-owned in-server suite. It boots one PoK host and lets the suite perform
its own zone visits. Supply the full retained fixture map tree, including water
and navigation assets, and include those files in the input hash manifest.
This mode validates the fresh report identity, finalization, counts and every
case result, then requires orderly process exits. It runs separately from the
two-zone reload probe, so suite-driven zone visits cannot weaken endpoint proof.

`launch_runtime.py OFF|ON UNIQUE_LABEL` reproduces this task's retained runtime
mounts/image and writes the exact Docker command, source identity and removal
receipt. Labels beginning `inserver` select the original-quest suite. The launcher
is task-specific and expects the artifact root named in RESULTS.md. It propagates
the guest failure, including expected failed-retention assertions. Do not treat
container removal as an orderly native-shutdown pass.

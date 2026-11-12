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

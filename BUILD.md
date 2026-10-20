# Guide To Building From Source Without Installer

This guide is far from exhaustive, you should expect to have some experience with building C++ code before considering compiling the code from scratch.  You should instead consider using the installer scripts if you don't want to hack on the code directly.

### CMake

EQEmu uses CMake as the build system on all platforms.  You will need CMake 3.20 or higher to build from source.

### Dependencies

The following libraries are required to build from source:
- uuid (unix)

The following libraries are not strictly required but in many cased recommended.
- [Perl](https://www.perl.org/ "Perl") (Required for Perl Quest Scripting)

##### Windows
If you wish to use Perl then you should use whichever version of Perl you have installed on the target system.

##### Linux
For Linux you simply can install the dependencies from your package manager, below is an example of doing it on Ubuntu using apt-get.

    sudo apt-get install libperl-dev uuid-dev

### Running CMake

##### Windows
The following is a modified command our automated build server uses to run CMake via the release vcpkg export and its toolchain file. 

	mkdir build
	cd build
	cmake -G "Visual Studio 15 2022 Win64" -DEQEMU_BUILD_TESTS=ON -DEQEMU_BUILD_LOGIN=ON ..

##### Linux
Similarly to Windows running CMake on Linux is simple it just uses a different generator.

	mkdir build
	cd build
	cmake -G "Unix Makefiles" -DEQEMU_BUILD_TESTS=ON -DEQEMU_BUILD_LOGIN=ON ..

### Bootstrap Rust bridge (zone)

On native x86-64 Linux builds, the zone target links the bootstrap
`actor-bridge` static library from `rust/actors`. This integration targets `x86_64-unknown-linux-gnu` with the
repository's pinned Rust toolchain and locked dependencies. Other platforms
(including Windows/MSVC) and cross builds retain the native-only zone path.
Cargo uses `--release --frozen --offline`; it cannot fetch dependencies or update the lockfile.

Rust execution (including build scripts and proc macros) belongs only in the
isolated validator guest, not on the host. The guest must supply its pinned
Cargo via `CARGO` or `PATH`, and its pinned `RUSTC`, `RUSTDOC`, `CARGO_HOME` and
offline dependency environment. CMake does not execute Cargo during configure.
The build uses the guest's `CARGO_TARGET_DIR` when set. Otherwise it uses
`../.eqemu-rust-target/<hash-of-cmake-build-path>` beside the source tree, so
`cmake -S . -B build` and the checked-in presets keep working without placing
Cargo outputs in source. The source parent must be writable, or supply an
external `CARGO_TARGET_DIR`. Cargo outputs inside the source tree are rejected.
No Rust toolchain or dependencies are installed by CMake. The owner of the
build must remove its external Cargo target directory during cleanup; CMake's
clean target does not remove Cargo's retained incremental state.

On every bridge-enabled zone startup, including utility invocations, a scalar
C ABI call to `eqemu_rust_bridge_v1` must return 42 or startup fails. This live reference keeps
the bridge in the actual zone executable under linker dead stripping. It is
only a bootstrap **link check**, not an actor control interface; it adds no
actor policy, NPC hooks or gameplay behavior. Successful startup is silent and
continues the existing native path without a runtime feature flag.

Full enabled Rust checks and exact-zone symbol/hash qualification, native
utilities, fresh-consumer checks and cleanup remain validator responsibilities;
this integration alone is not evidence that those checks passed.

### Early Rust formatting

The developer command uses the same pinned formatter, workspace scope and
configuration as the isolated validator. It sends the current working snapshot
to the dedicated worker. It does not run Cargo or repository build scripts on
the implementation host. The reviewed snapshot-capable worker/client must be
installed and activated first; an older installation cannot provide this operation.

In an approved isolated development environment, configure the absolute path to
the reviewed installed client once per checkout:

```sh
git config --local eqemu.formatterClient /absolute/reviewed/installation/scripts/format-worker-snapshot
```

The path is operator configuration, outside this checkout. Do not point it at
candidate source. The command also accepts `--client /absolute/path` for one
explicit invocation. From the repository root:

```sh
python3 utils/scripts/dev-format.py
git diff
python3 utils/scripts/dev-format.py --check
```

The first command can boot a disposable worker VM. It applies only verified
formatting changes to captured Rust files whose input bytes remain unchanged.
The trusted installed frontend validates and applies the result; this repository
does not maintain a second snapshot policy or formatter-output writer.
It never stages files, modifies native code, adds files or removes files. Review
and stage the changes yourself. Check mode can also boot a VM; it changes no
source and saves a successful result under the checkout's private Git directory.
A formatting result alone does not create a check receipt. Neither operation is
full build, test or gameplay acceptance.

#### Optional pre-commit check

Enable this hook only in an approved isolated development environment, after
reviewing the checked-out hook/helper. AFK does not install or depend on it.
Inspect any existing hook configuration before replacing it:

```sh
git config --get core.hooksPath
git config --local core.hooksPath .githooks
```

To remove this local override, run `git config --local --unset core.hooksPath`.
If you replaced a prior value, restore that value instead.

The hook checks staged whitespace errors and conflict markers using Git's
`diff --cached --check`. For Rust/config changes it then checks that a prior
successful isolated check matches the current formatter identity, snapshot and
the actual index bytes. Its frontend calls are identity-only. It never boots a
VM, formats, builds, installs tools, stages or discards files. Native-only and
documentation-only changes need no formatter receipt.

Partial staging of formatter inputs is deliberately unsupported. Stage all
intended Rust/config changes and deletions, then run the explicit `--check`
command before committing. Untracked formatter inputs, stale receipts, changed
tools, missing setup and alternate Git indexes, including `git commit --only`,
produce a refusal with instructions. Unrelated native changes can remain unstaged.
Receipts are editable local convenience data, not an acceptance credential.
Bypassed or missing hooks do not bypass AFK's independent pre-publication
formatting or final exact-head, check-only validation.

### Building

##### Windows
Inside the build directory a file EQEmu.sln should be produced by a successful run of the CMake command.  You can either open this with Visual Studio or build it directly with MSBuild via the command line.

	msbuild EQEmu.sln /p:Configuration=Release

##### Linux
From the build directory you can simply call make to build.  

For example.

	make -j4

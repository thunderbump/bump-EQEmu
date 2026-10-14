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

The zone target now links the bootstrap `actor-bridge` static library from
`rust/actors`. This integration targets `x86_64-unknown-linux-gnu` with the
repository's pinned Rust toolchain and locked dependencies. Cargo uses
`--release --frozen --offline`; it cannot fetch dependencies or update the lockfile.

Rust execution (including build scripts and proc macros) belongs only in the
isolated validator guest, not on the host. The guest must supply its pinned
Cargo via `CARGO` or `PATH`, and its pinned `RUSTC`, `RUSTDOC`, `CARGO_HOME` and
offline dependency environment. CMake does not execute Cargo during configure.
The build uses the guest's `CARGO_TARGET_DIR` when set, otherwise a `rust-target`
directory under the zone build directory. Use an out-of-source build; Cargo
outputs inside the source tree are rejected. No Rust toolchain or dependencies
are installed by CMake.

On every zone startup, including utility invocations, a scalar C ABI call to
`eqemu_rust_bridge_v1` must return 42 or startup fails. This live reference keeps
the bridge in the actual zone executable under linker dead stripping. It is
only a bootstrap **link check**, not an actor control interface; it adds no
actor policy, NPC hooks or gameplay behavior. Successful startup is silent and
continues the existing native path without a runtime feature flag.

Full enabled Rust checks and exact-zone symbol/hash qualification, native
utilities, fresh-consumer checks and cleanup remain validator responsibilities;
this integration alone is not evidence that those checks passed.

### Building

##### Windows
Inside the build directory a file EQEmu.sln should be produced by a successful run of the CMake command.  You can either open this with Visual Studio or build it directly with MSBuild via the command line.

	msbuild EQEmu.sln /p:Configuration=Release

##### Linux
From the build directory you can simply call make to build.  

For example.

	make -j4

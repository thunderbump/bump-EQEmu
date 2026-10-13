# Actor workspace bootstrap

This workspace is owned by EQEmu's actor work. It prepares source for isolated
validation; it does not implement actor runtime policy or native integration.
The `answer` value call and `eqemu_rust_bridge_v1` qualification marker are **not
an actor policy interface**.

- `actor-core` will own deterministic actor policy without native dependencies.
  For now it forbids unsafe code and supplies only a saturating increment, one
  unit test and one doctest.
- `actor-bridge` will own the narrow native boundary. For now it is a staticlib
  with a minimal CXX value bridge and a no-pointer, fixed-width versioned marker
  returning 42. The qualification namespace remains `eqemu_proof`.

The manifests, lockfile and source come from the reviewed qualification fixture
in `thunderbump/bump-akk-stack`, commit
`9343e602220a4e655c27e1815497684519f258d4`, `validation/rust/fixture/`.
All eight embedded files were SHA-256 verified before adapting only local
package names and crate references. Third-party locked versions, checksums and
edges are unchanged. The root `rust-toolchain.toml` pins Rust 1.99.0, the Linux
x86-64 target, rustfmt and clippy; CXX and cxx-build are pinned to 1.0.194.

## Boundary obligations

Core remains safe Rust. The bridge denies unsafe operations in unsafe functions;
CXX owns generated boundary code. The `unsafe(no_mangle)` marker requires its
versioned symbol to be unique in the final native link. It takes no pointers,
transfers no ownership and returns only a `u32`; the native qualification caller
owns invocation and must use the matching C ABI and return type. Any future
pointer, lifetime, threading or ownership contract needs separate review.

## Qualification still required

Do not run Cargo, rustc, build scripts, proc macros or Rust tests on the host.
Rust execution belongs only in the isolated guest validator after its
actual-workspace contract is enabled, using this workspace and locked graph.
The installed validator's Rust stages are currently dormant. Native build/unit
results alone are not Rust acceptance.

Offline dependency/toolchain binding, Rust build/unit/doctest qualification and
native-link qualification remain pending. Later integration owns actor policy,
NPC/zone hooks, CMake linkage and gameplay validation. This bootstrap adds none
of those and includes no vendor archives, tool binaries or proof output.

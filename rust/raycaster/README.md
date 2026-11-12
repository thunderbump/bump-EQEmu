# Rust map engine

Enable `-DEQEMU_RUST_MAP_RAYCASTER=ON` when configuring zone. Default is OFF.
Reconfigure OFF and rebuild to roll back. Selection applies to every map in that
binary, with no runtime override and no fallback after Rust errors. Unsupported
platforms fail configuration when ON: only native Linux x86-64 is admitted.
The OFF helper never discovers Cargo. The existing actor bridge has its own
independent Linux Cargo requirement; this change does not remove it.

C++ retains V1/V2 model/group/terrain decoding and expansion. The maintained
crate owns geometry copied during a CXX call, an opaque boxed engine, and query
stamps. It depends on no experiment files or geometry SHA table. Queries need
exclusive engine access even though Map's public methods are const. Zone-thread
ownership is required; concurrent access has not been qualified. C++ release
runs the Rust box destructor exactly once.

CXX Result errors become C++ exceptions. Construction catches standard exceptions,
logs a load diagnostic, and retains the previous Map engine. Invalid query
coordinates propagate an exception; they do not become an unobstructed ray.
A Rust panic crossing CXX aborts. OOM may still abort in Box/CXX/runtime or native
allocations; recoverability is not guaranteed for all allocation failures.
No unsafe Rust is used in the core. Generated bridge code uses CXX's unsafe
machinery. Memory safety does not establish gameplay safety.

Empty valid geometry creates a loaded engine with absent bounds and no hits.
Invalid/nonfinite vertices fail, including unused vertices. Coordinates and
query endpoints must be finite and within +/-1e8. No triangles are dropped.
A complete replacement is built before Map swaps ownership. Rust mode bypasses
native MMF load/save/reload and always rebuilds from decoded maps. It neither
consumes nor serializes native engine state; existing C++ cache behavior remains.
Map sentinels, optional outputs, start-position adjustments and rule/zone limits
remain in C++. Zone-dependent height checks require a hit and a present zone.

Admission is 8,000,000 vertices and 12,000,000 indices, including count multiples,
index validation, and finite checks. At most 144 MB of owned geometry, 16 MB of
visited stamps, 131071 nodes and 16 million leaf references are admitted. Counts
fit u32 and arithmetic fits usize on the required 64-bit target. Tree depth is
15, with bounded queries and stamp-wrap reset. Vec growth uses fallible reserve.
This is a count budget, not a strict RSS cap: decoder arrays, adapter copies,
allocator capacity and recursive partition lists add transient memory. The
largest historical mesh has 6,161,009 vertices and 6,684,999 indices; production
admission does not authenticate identities. Tests authenticate frozen assets.

Cargo builds frozen/offline with pinned CXX 1.0.194 and the repository Rust
1.99 toolchain. Supply offline dependencies in the disposable worker. CMake
requires external Cargo output; the worker owner cleans it. Sanitizer builds
instrument C++ callers and generated CXX through a separate Cargo target path.
Stable Rust code remains uninstrumented. See tests/map_bridge for focused tests
and retained qualification receipts for actual coverage.

Client acceptance remains open: check placement, walking, stairs, slopes, walls,
falling, LoS, normal versus empty versus failed load, and failed reload retaining
the prior map. Compare both selected backends in an isolated two-zone session.
No live game deployment or real-player messages are part of this change.

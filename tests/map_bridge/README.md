# Focused maintained bridge qualification

Run only in authorized disposable rootless workers. `guest.py` expects source
at `/work`, writable results at `/results`, readonly historical map-translation
artifacts at `/evidence`, and GLM/fmt headers at `/deps`. Impose network none,
2 GiB memory with equal memory-swap, two CPUs and 128 PIDs. Each subprocess has
a 600-second timeout. The immutable image and exact docker command are recorded
in reports; the owned transport and complete manifests remain in the artifact root.
`EQEMU_MAP_SKIP_SANITIZERS=1` selects ordinary qualification only.

The CMake project links real Map/decoder sources with explicit environment-only
adapters, plus the maintained adapter and Cargo-generated CXX. It does not link
zone, a DB, quests or live services. Default native Map compilation is a separate
object target. MMF-enabled Rust tests link the legacy cache functions but prove
the public Rust load path ignores an existing cache and does not save one.

`replay.cpp` reuses the frozen reference reader and trace IO with a different
engine factory. These experimental dependencies are test-only. `replay.py`
authenticates original geometry and trace identity, replays exact endpoint bits
and order through CXX, then calls the unchanged independent comparator. The
three larger meshes receive one finite query each, establishing admission only.

Final ordinary executed SHA and per-phase results are in reports/summary.json
and reports/runs.json. Source review used baseline
60346d90b79b9c2b1a86590125b955219ecd1a41 plus Brian's integration brief. Independent
review found miss/zone access and diagnostic brute endpoint issues; both were
fixed and rechecked. Later harness fixes received direct review.

No full Server, DB startup or interactive client pass exists. Full CMake stops
at missing Boost dynamic_bitset metadata in this worker when the absent vcpkg
toolchain is explicitly bypassed. Native dependency provisioning and full
Server/DB qualification remain necessary.

Sanitizers are unqualified. Clang leak control exited 0 without a report; GCC
OOB produced UBSan insufficient-space evidence before ASan; a later GCC pass
control hung and its owned container was stopped. The MMF diagnostic target
initially lacked sanitizer runtime link flags and was fixed without claiming a
new pass. Every attempt remains distinct. No suppressions or quarantine tuning.
Generated CXX/native flags do not instrument stable Rust. No sanitizer workload
pass is claimed. Keep ordinary and diagnostic Cargo output paths separate.

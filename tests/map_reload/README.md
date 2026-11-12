# Zone collision reload regression

`zone/replace_collision_map.h` is the ownership decision used by the real Zone caller.
The focused probe checks failed first load, two rejected replacements, a throwing
loader, successful replacement, valid empty replacement and exactly-once cleanup.
Compile and execute only inside the disposable worker, as `worker.py` does.

`validate.py build` makes fresh task-owned copies of the prior qualified source,
builds, real dependencies and Cargo cache. It overlays changed tracked bytes while
preserving identical source mtimes, then runs the complete default build with
one job and both backends. This is an incremental full-target build, not a clean
build. Read `source-delta.json`, `production-diff.patch`, compile commands, phase
resource logs and binary hashes before accepting reuse.

`validate.py OFF` and `validate.py ON` each prepare a fresh disposable public DB
and both zones. The shared runtime probe checks a processed missing-file reload,
an unknown four-byte version header rejected before decoding, a finite nonempty
V1 floor at height 42, then restoration of the public map. Ground queries after
each processed attempt prove failure retention and actual successful replacement.
Probe timestamps and reload attempt counts are retained. Reload verdict and
native shutdown verdict are separate; a shutdown failure still fails the worker.

The launcher uses the task-owned rootless socket and artifact root
`~/.local/state/eqemu-map-translation/zone-map-reload-20261111`. Images are immutable,
networking disabled, builds 4 GiB/no swap, runtime 2 GiB/no swap. Daemon preparation
and teardown are operator responsibilities. Do not reuse an existing output root.
Sanitizers, clients, watermap and pathing semantics are outside this regression.

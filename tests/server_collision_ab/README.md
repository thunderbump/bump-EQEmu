# Server-owned collision A/B

Standalone, opt-in ordinary NPC suite. No default CMake, CI or utility hookup.
Rust remains OFF by default. ON bypasses native MMF; OFF retains native cache
semantics with fresh private map copies. No client, combat or actor-stop claim.

Use Stack's `validation/rootless-prototype/run` user-systemd recipe with unique
unit/socket/store `eqemu-server-collision-npc-completion-20261115`. Never bypass
NoNewPrivs with direct newuidmap or use sudo. Verify every OCI blob before offline
import of the immutable builder/runtime image IDs in `build.py`/`run.py`.

From clean committed source:

```sh
python3 -B -m unittest discover -s tests/server_collision_ab -v
python3 -B tests/server_collision_ab/build.py
python3 -B tests/server_collision_ab/run.py --arm OFF --label native-v5
python3 -B tests/server_collision_ab/run.py --arm ON --label native-v5
python3 -B tests/server_collision_ab/run.py --arm OFF-repeat --label native-v5
```

`build.py` copies retained matched dependency/source/build closures, overlays
tracked changed bytes while preserving identical mtimes, then runs complete
OFF/ON default targets and utilities serially. This is an incremental full-target
build, not a clean build. Both successful build phases and actual binary hashes
are required. Production changes require new matched builds. Preserve previous
build directories and failed runs before another attempt.

Runtime uses fresh private DB/maps/rules and NPC 2011, size 6, speed 1.25. Initial
GMMove positions the fixture only. Ordinary `NPC::MoveTo` with saveguardspot sets
the destination; native AI/pathfinding/interpolation owns subsequent movement.
`route.json` pins route inputs derived from actual OFF observations before ON.
The original grid candidate was flat and bare NavigateTo was overridden by guard
AI; both failures remain outside Git.

`EQEMU_COLLISION_OBSERVE=server-collision-npc-2011` explicitly enables a native
4096-record budget for this NPC. It records actual GetGroundZ/FindDestGroundZ,
CheckLosFN, AdjustRoute, ground-route admission, movement dispatch/teleport and
FixZ observations. Absent/wrong callers, backend/entity/input changes, exhausted
budgets, nonfinite evidence, teleports and invalid final state fail qualification.
Omit the environment variable to disable diagnostics. No gameplay rollback needed.

Queries, actual endpoints/results, floors, route nodes and final pose compare
exactly. Decimal observations canonicalize to float32 only to bind Perl and native
formatting. Timing may vary. Sampled poses must stay within 2 units of the frozen
route corridor and within 0.05 Z of native interpolation or observed FixZ. Stopped
poses must be within 0.05 of floor plus observed model-dependent GetZOffset, and
within 0.5 XY of destination. Moving poses can cross discrete stair geometry by
linear interpolation; they are not required to be perfectly floor-grounded.
All tolerances are declared before ON. No sanitizer execution.

Artifacts: `~/.local/state/eqemu-map-translation/server-collision-npc-completion-20261115`.
Private DB/runtime logs and large derived data stay outside Git. Child exits,
collision verdict and cleanup are separate. Known full-topology central-866g is
not resolved by reduced world/zone/DB success. Stop the owned daemon after runs.
PoK is gated on complete Qeynos2 collision parity and cleanup. Then run the same
three arms with `--zone poknowledge --label native-v3`. Its private NPC uses public
grid 3 entries 41,42,43 and a separate pinned quest/config. `summarize.py` regenerates
the small public JSON receipt from the retained six qualification directories.

Fast regressions: Python adversarial controls and focused synthetic map/bridge
checks. Affected integration: this explicit matched suite, bridge/reload checks.
Rewrite/corpus: explicit full native/Rust regressions and complete corpus replay.
Reuse prior `map_reload` and `map_server_validation` qualified coverage; this suite
does not recertify reload/NaN/empty-map behavior or player/client collision.

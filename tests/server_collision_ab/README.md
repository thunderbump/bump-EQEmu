# Standalone server collision evidence

This opt-in suite is an **incomplete implementation of the server-first scenario**.
It captures fixed production Perl GetGroundZ/CheckLosFN queries without a client,
using the existing fresh public PEQ DB runtime. It does not request NPC movement.
No CMake, default utility test, or CI target invokes it.

The retained Rat model origin and size are 297.75,344,3.75 and 6. Clear and wall
endpoints are the previously observed 288,344,3.75 and 320,344,3.75. A third query
samples the start. Ground offset is zero, yielding FindBestZ input Z=4.75 after
the default +1 rule. This deliberately avoids the previous +100 roof selection.
These are fixed floor samples, not a height-transition route. Acceptance requires
exact query identity, exact clear/blocked decisions, finite ground and exact
numeric equality, with zero tolerance declared before ON. OFF freezes observed
results; ON and OFF-repeat require the same scripts, assets and transport.

`scenario.py` inventories original public spawn/NPC/grid/Map-rule rows before
fixture mutation. Only one private Rat remains, with roaming and aggro disabled.
The existing bindings do not export ordinary NPC route nodes or GetZOffset.
Fix Z logging can expose GetFixedZ results, but does not prove each particular
movement caller ran. Missing route, model-offset and movement-caller evidence
forces collision=false and overall exit 1 even when fixed queries agree. No actor
stop is labeled FixZ. PoK is blocked until Qeynos2 collision gates pass.

No production diagnostic seam was added. Completing route qualification needs a
bounded opt-in route/caller observation seam and matched OFF/ON rebuilds, or a
supported interface that supplies those observations. No combat, body sweep,
sanitizer, stability, player movement or full gameplay claim follows.

## Reproduction

Read Stack's `validation/rootless-prototype/run` first. Start a task-owned systemd
user-manager daemon, unique store/socket/unit, 2 GiB and no swap. Do not use staging
or another task's daemon. Verify the complete offline runtime OCI closure before
import. Runtime image is the inherited immutable 93429ed… image in
`tests/client_collision_ab/run.py`; retained binaries are from production source
4ce5d7391a048f609b037817b772d6c15ea482d6, not this harness HEAD. The production
source delta is retained separately. Rust is OFF by default; ON bypasses native
MMF, and OFF follows unchanged native cache behavior with a fresh private map copy.

Run from a clean committed checkout, using a new label to preserve first failures:

```sh
python3 -B -m unittest discover -s tests/server_collision_ab -v
python3 -B tests/server_collision_ab/run.py --arm OFF --label survey-v1
python3 -B tests/server_collision_ab/run.py --arm ON --label survey-v1
python3 -B tests/server_collision_ab/run.py --arm OFF-repeat --label survey-v1
```

All three currently return nonzero because route evidence is incomplete. Raw child
exits and shutdown verdict remain separate. The known world shutdown failure
central-866g is never suppressed. The host wrapper removes its owned container on
failure and retains inspect/exit/cgroup receipts. Stop its daemon after execution.
Artifacts belong under
`~/.local/state/eqemu-map-translation/server-collision-scenarios-20261115`.
Private database/runtime logs must not be published.

## Test tiers

Fast: adversarial comparator controls here, existing Python client controls and
focused synthetic map/bridge regression tests. Run native tests only in disposable
workers. Affected-code integration: explicit matching-backend query/route suites,
map bridge and reload qualification. Full rewrite/corpus: explicit full native and
Rust regressions plus complete map corpus replay. Keep that expensive tier opt-in.
Reuse qualified reload/NaN/empty evidence in `tests/map_reload` and
`tests/map_server_validation`; this suite does not recertify it.

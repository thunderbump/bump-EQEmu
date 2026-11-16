# 2026-11-15 fixed-query survey, partial implementation

Executed pushed harness dbc54f8fe4ee476076110ebf86fce0f7181eb4b0 on prepared
PR115 head 4c0d43b0d75a5a650701e7fe1a44cc824685985c. This report is a later
documentation-only commit. Retained binaries are from production source
4ce5d7391a048f609b037817b772d6c15ea482d6, not harness HEAD. The only production
delta was verified as the byte-identical replacement helper relocation.

OFF / ON / OFF-repeat, label survey-v1: exact fixed query input, decision and
numeric parity. Ground start=0, clear=0, blocked=20; LoS start=1, clear=1,
blocked=0. The blocked-location query selects wall geometry; it is not a route
floor. Ground offset=0, active height adjustment=1, adjusted Z=4.75. Actual source
pose and size came from NPC getters; eye endpoints were reconstructed from the
production formula. No intercepted Map endpoints or GetZOffset observation.

All runs returned **exit 1**, collision=false because movement/caller evidence is
missing. No route was requested or height-transition route frozen. The original
public Rat has pathgrid=0. There is no supported ordinary NPC route-node or
GetZOffset export in the reused Perl interface. A bounded observation seam and
matched rebuilds, or a suitable supported interface, are needed to finish that
coverage. PoK remains blocked. No actor-stop/FixZ/combat/body-sweep/stability claim.

Each reduced topology used world/zone/DB only, with all child exits 0 and timer
retirement observed. This does not resolve central-866g's full login/client
shutdown failure. No loginserver or client ran here. The worker's inherited final
assertion text misleadingly mentions reload; the receipt identifies missing
collision evidence as the actual failure. First failures remain preserved.

Runtime image 93429edbf01c1631611af6c88220405136072afd78dc65b6ebf47415b54c38e1,
all 23 OCI blobs verified before offline import. Unique rootless systemd daemon,
network-none containers, no host ports, 2 GiB/no swap, one CPU, serial execution.
Peak cgroup bytes: OFF 1315659776, ON 1290948608, repeat 1294741504. No OOM events.
No native/Cargo build or sanitizers. All task containers removed, daemon inactive.
Private artifacts: ~/.local/state/eqemu-map-translation/server-collision-scenarios-20261115,
including original receipts/logs/DBs, summary.json, OCI verification, source delta,
fixture/binary/script pins, rule and public-grid inventory, cgroup/cleanup receipts.

Four comparator rejection controls and three inherited client controls passed.
Read-only Standards review identified cleanup and broad-context coupling; fixed.
Spec review identified absent movement, assumed inputs and readiness gaps. Getter,
rule receipt, process-hash and map-load checks were added before execution. Full
pre-action backend/caller qualification is still incomplete. No complete spec pass.
Central central-j4sy remains in progress for parent acceptance.

# First Living-World Two-Zone Route

Date: 2026-07-12

Wayfinder ticket: `central-dcq7.2`, **Select a safe existing two-zone route for the first living-world slice**

Snapshot notice: route data and runtime assets were observed on 2026-07-12 against
[`b40489eff9b619525b4bc90e9086bb6f1d9c15f0`](https://github.com/thunderbump/bump-EQEmu/tree/b40489eff9b619525b4bc90e9086bb6f1d9c15f0)
and the local persistent PEQ database. Revalidate database content and map/nav asset provenance before implementation;
the accepted route decision itself remains the result recorded here.

## Decision

Use **Rivervale (`rivervale`, zone 19) and Misty Thicket (`misty`, zone 33)** for the first living-world
vertical slice. Start with a fixed level **1-3** Autonomous Actor leading an ordinary Bot party. Use the existing zone
line in both directions, the existing merchants and spawns, and the existing map/nav assets. Do not edit zone points,
doors, paths, spawns, factions, loot, merchants, or zone topology.

The first combat target policy should be an explicit `npc_types.id` allowlist, initially:

- `33005`, `a_large_rat`, level 1, faction 38, loot table 3052;
- `33160`, `a_mangy_rat`, level 1, faction 38, loot table 3093;
- `33024`, `a_fire_beetle`, level 2, faction 982, loot table 11383.

This allowlist is an experimental safety boundary, not generalized friend-or-foe identification. Every attack must
still pass normal runtime attack eligibility, player-content deference, ownership, and contention checks. `a_large_rat`
is the best first target because 25 version-0 spawn points select NPC 33005 at 99 percent; `a_mangy_rat` is often a
low-probability alternative in those same spawn groups, while the 15 fire-beetle spawn points are deterministic.

Use a Rivervale-aligned actor identity for the experiment. Rivervale merchants are faction 1035/1036 in most cases,
and Misty Thicket contains guards and other aligned inhabitants. This recommendation reduces faction uncertainty; it
does not solve faction reasoning.

## Why this route

| Criterion | Evidence and consequence |
|---|---|
| Existing connection | Zone point 493 crosses Rivervale at `(x=96, y=-77, z=4)` to Misty `(target_x=-2552, target_y=410, target_z=-4)`. Zone point 676 returns from Misty at `(x=-2588, y=407, z=-7)` to Rivervale `(71.62, -70.13, 3.8)`. Both use the all-client mask and have no expansion bounds. |
| Ordinary selling | The version-0 Rivervale population contains 44 distinct merchant NPCs/lists. Examples include Fiddy Bobick (NPC 19083, merchant list 19083, spawn 10979 at `-428, 19.88, -5.8`) and Teelie Meegles (NPC 19031, list 19031, spawn 10925 at `-123, 32, 2.13`). Merchant faction still needs a valid actor identity. |
| Bounded low-level targets | Misty has 25 spawn points for NPC 33005, 28 possible points for NPC 33160, and 15 deterministic points for NPC 33024. All are level 1-2. Representative nearer-side points include fire beetle spawn 7423 at `(-2055.88, 709.88, -6.38)`, large-rat spawn 7559 at `(-2078.88, 251, -2.5)`, and large-rat spawn 7572 at `(-2164.88, 251.88, -3.1)`. |
| Navigation | The persistent AkkStack contains non-empty `server/maps/nav/rivervale.nav` (141,233 bytes) and `misty.nav` (622,427 bytes). Runtime pathfinder loading selects a navmesh when `maps/nav/<zone>.nav` exists and otherwise silently falls back to a null pathfinder ([`zone/pathfinder_interface.cpp`, lines 28-35](https://github.com/thunderbump/bump-EQEmu/blob/b40489eff9b619525b4bc90e9086bb6f1d9c15f0/zone/pathfinder_interface.cpp#L28-L35)). Presence is necessary evidence, not proof that every route is connected. |
| Era stability | Both zone records and both zone points have `min_expansion=-1`, `max_expansion=-1`; the zone points use client mask 4294967295. This avoids the old/new Freeport naming split found in the database. |
| Database safety | The route can boot against existing version-0 data and requires no fixture mutation. Zone Harness explicitly disables saved zone state after boot ([`zone/harness/zone_harness_runtime.cpp`, lines 42-61](https://github.com/thunderbump/bump-EQEmu/blob/b40489eff9b619525b4bc90e9086bb6f1d9c15f0/zone/harness/zone_harness_runtime.cpp#L42-L61)). Persistent actor inventory/economy validation remains mutation-bearing and must use the validation database with backup/restore gates. |

## Runner-up: North Qeynos and Qeynos Hills

**North Qeynos (`qeynos2`, zone 2) ↔ Qeynos Hills (`qeytoqrg`, zone 4)** is the runner-up.

- It has direct existing zone points: North Qeynos point 7 at `(73, 1272, 2.5)` targets Qeynos Hills
  `(999999, -220, 4)`; return point 700 uses sentinel `x=999999` at Qeynos Hills `(y=-350, z=-4)` and targets
  North Qeynos `(999999, 1395, 4)`. These sentinel coordinates demand an actual live zoning proof rather than
  assuming geometric traversal.
- North Qeynos has 26 distinct version-0 merchant NPCs/lists. Qeynos Hills has abundant candidates including NPC
  4010 (`a_decaying_skeleton`, level 1, 33 points), 4026 (`a_fire_beetle`, level 2, 18 points), 4013
  (`a_large_field_rat`, level 2, 8 points), and 4079 (`a_gnoll_pup`, level 1, 4 points).
- Both nav files exist (`qeynos2.nav`, 232,993 bytes; `qeytoqrg.nav`, 824,307 bytes).
- It is less suitable for the first slice because the zone line uses sentinel coordinates, Qeynos is a more complex
  city, and the hunting zone mixes many faction families and scripted/invisible utility NPCs. It is a useful second
  route after the actor can demonstrate safe target filtering.

## Rejected candidates

### Halas and Everfrost Peaks

Halas (`halas`, zone 29) and Everfrost (`everfrost`, zone 30) are directly connected by ordinary numeric points:
Halas point 397 at `(-77.88, -692.79, 3.13)` targets `(380, 3684, 5)`; Everfrost point 590 at
`(370, 3700, 3)` targets `(-76.46, -676, 3.13)`. Halas has 44 merchants, and Everfrost has plentiful level 1-3
targets. Both nav files exist.

Reject it for the first slice because Everfrost is much larger (its nav file is 2,020,489 bytes), has severe elevation,
ice/water and long travel distances, and mixes wildlife, goblin and gnoll faction hazards. Those traits make it a good
later pathing/recovery stress route, not the lowest-risk behavior slice.

### Paineel and Toxxulia Forest

Paineel (`paineel`, zone 75) and Toxxulia (`tox`, zone 38) have direct numeric zone points, 39 Paineel merchants,
non-empty nav files, and many low-level Toxxulia targets. Reject it because Paineel's geometry and lifts make city
navigation more demanding, its merchants share faction 990, and Toxxulia's most abundant candidate NPCs include
kobolds on faction 830. It would make faction identity and vertical navigation part of the first proof.

### Freeport and East Commonlands

Reject this pair because the database contains both classic (`freportw`/`ecommons`) and revamped
(`freeportwest`/`commonlands`) zone families. Classic West Freeport point 387 connects to East Commonlands, while
revamped West Freeport point 1544 connects to Commonlands; the revamped zones have `cancombat=0` in this database.
Choosing this route would introduce client-era/content-family decisions that Rivervale/Misty does not require.

## Fixture and validation shape

1. Keep selection, pricing, faction and contention decision matrices in unit/helper tests. The repository test process
   says Zone Harness should prove only representative runtime wiring and ordinary processing
   ([`docs/testing/process.md`, lines 328-342](https://github.com/thunderbump/bump-EQEmu/blob/b40489eff9b619525b4bc90e9086bb6f1d9c15f0/docs/testing/process.md#L328-L342)).
2. Add parameterized, read-only setup for a single named zone and fixture-owned entities. Boot `rivervale` and
   `misty` as separate one-off Zone Harness runs. The harness accepts a zone short name but holds one booted global
   zone; a second call only succeeds for that same zone/instance ([`zone/harness/zone_harness_runtime.cpp`,
   lines 42-55](https://github.com/thunderbump/bump-EQEmu/blob/b40489eff9b619525b4bc90e9086bb6f1d9c15f0/zone/harness/zone_harness_runtime.cpp#L42-L55)).
3. Do not claim that two Zone Harness boots prove zoning. Repository guidance explicitly reserves behavior depending
   on zoning between zones, client packets, and real client timing for persistent/manual runtime
   ([`docs/testing/process.md`, lines 340-342](https://github.com/thunderbump/bump-EQEmu/blob/b40489eff9b619525b4bc90e9086bb6f1d9c15f0/docs/testing/process.md#L340-L342)).
4. In Tier 3, prove representative behavior separately: actor/party materialization in Rivervale, merchant lookup and
   interaction eligibility without committing a sale; then allowlisted target discovery, movement, combat and loot
   observation in Misty. Use fixture-owned/synthetic state wherever possible.
5. In the persistent **validation** AkkStack, prove the actual Rivervale → Misty → Rivervale transition, party
   survival/reformation, and real merchant transaction. Do not use the gameplay database for mutation-bearing
   automation. The canonical Tier 3 wrapper already isolates the one-off zone container and retains the validation
   database ([`docs/testing/process.md`, lines 350-380](https://github.com/thunderbump/bump-EQEmu/blob/b40489eff9b619525b4bc90e9086bb6f1d9c15f0/docs/testing/process.md#L350-L380)).
6. Before implementation, run a nav reachability prototype from each selected materialization point to the zone line,
   merchant, and bounded hunting area. File presence alone does not guarantee a complete path.

## Exact evidence and reproducibility

Evidence was gathered read-only on 2026-07-12 from pushed commit
[`b40489eff9b619525b4bc90e9086bb6f1d9c15f0`](https://github.com/thunderbump/bump-EQEmu/tree/b40489eff9b619525b4bc90e9086bb6f1d9c15f0)
and the persistent local AkkStack PEQ database. No database or stack
state was changed. Database authentication came from the container's existing environment and was not printed or
persisted.

The following are the material SQL query shapes. All used `SELECT` only:

```sql
-- Connections and client/content bounds
SELECT zp.id, zp.zone, zp.x, zp.y, zp.z, z.short_name,
       zp.target_x, zp.target_y, zp.target_z,
       zp.client_version_mask, zp.min_expansion, zp.max_expansion
FROM zone_points zp
JOIN zone z ON z.zoneidnumber = zp.target_zone_id AND z.version = 0
WHERE zp.zone IN ('rivervale','misty','qeynos2','qeytoqrg','halas','everfrost','paineel','tox')
ORDER BY zp.zone, zp.id;

-- Merchant counts and concrete identities
SELECT s.zone, COUNT(DISTINCT n.id), COUNT(DISTINCT n.merchant_id)
FROM spawn2 s
JOIN spawnentry se ON se.spawngroupID = s.spawngroupID
JOIN npc_types n ON n.id = se.npcID
WHERE s.version = 0 AND s.zone IN ('rivervale','qeynos2','halas','paineel')
  AND n.merchant_id > 0
GROUP BY s.zone;

-- Candidate identities and spawn stability
SELECT s.id, s.spawngroupID, s.zone, s.x, s.y, s.z,
       s.respawntime, s.pathgrid, se.npcID, se.chance,
       n.name, n.level, n.npc_faction_id, n.loottable_id
FROM spawn2 s
JOIN spawnentry se ON se.spawngroupID = s.spawngroupID
JOIN npc_types n ON n.id = se.npcID
WHERE s.version = 0 AND s.zone = 'misty'
  AND n.id IN (33005, 33160, 33024)
ORDER BY n.id, s.id;
```

Observed aggregate results:

| Zone | Merchants | Candidate result |
|---|---:|---|
| Rivervale | 44 NPCs / 44 lists | Town side of recommendation |
| North Qeynos | 26 / 26 | Runner-up town |
| Halas | 44 / 44 | Rejected first-slice town |
| Paineel | 39 / 39 | Rejected first-slice town |
| Misty Thicket | n/a | 25 large-rat points; 28 possible mangy-rat points; 15 fire-beetle points |
| Qeynos Hills | n/a | 33 decaying-skeleton; 18 fire-beetle; 8 level-2 large-field-rat points |
| Everfrost | n/a | 31 polar-bear-cub; 30 ice-goblin-whelp; 28 wooly-spiderling points |

Nav evidence came from read-only `stat` calls beneath `../bump-akk-stack/server/maps/nav/`. These files are runtime
assets in the local persistent stack, not repository guarantees; validation workers must provision matching map/nav
assets and report their hashes or versions.

## Caveats that remain decisions, not hidden assumptions

- Verify actor race/deity/class and initial faction values against Rivervale merchants and guards before a real sale.
- Pick exact safe materialization, zone-line, merchant, and hunting-area coordinates through a pathing prototype.
- Decide how the live runtime hands persistent actor state across zone processes; Zone Harness cannot answer that.
- Decide whether an actor may target a mobile spawn by NPC type alone or needs a bounded spatial/camp constraint.
- Measure Bot party formation and leash behavior on both sides of a real zone transition.
- Treat all persistent inventory, currency, merchant stock and loot changes as database mutations with cleanup and
  evidence requirements.

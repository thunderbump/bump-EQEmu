# Bot Slow Maintenance Live Smoke

Use this manual smoke when validating single-target bot `Slow` maintenance in the local AkkStack dev runtime.
It is a Tier 4 manual client test that complements the automated C++ coverage for slow target selection.

The behavior under test is the first single-target `Slow` maintenance iteration:

- a slower-capable bot should prefer its current target first;
- if the current target is already slowed or otherwise does not need a slow, a later normal slow opportunity may
  slow another unslowed **Engaged Hostile**;
- mezzed **Engaged Hostiles** are skipped in v1;
- `AESlow` behavior is not part of this smoke.

## Runtime

Start the dev runtime from this checkout:

```sh
./scripts/start-akkstack-runtime-proof.sh
```

Then log in with a GM-capable test character. Use a quiet zone with room to separate and observe several NPCs.
Record the client build, character, zone, server commit, and whether the runtime helper used the normal AkkStack
compose files or the local port-remap overlay.

Do not run destructive server operations. Prefer temporary `#spawn` NPCs and ordinary bot commands over database
spawn edits.

## Test Setup

1. Create or spawn one slower-capable bot. A Shaman bot is the preferred case because its single-target `Slow`
   spell line is the target behavior.

   ```text
   #bot botcreate SmokeSlow 10 1 0
   #bot botspawn SmokeSlow
   #bot stance 4 byname SmokeSlow
   #bot spellengagedpriority slows 1 byname SmokeSlow
   ```

   If this character already owns a comparable Shaman, reuse it and record the exact bot name, level, stance,
   and `#bot spellengagedpriority slows current byname <name>` output. Use `#bot help`, `#bot stance help`, and
   `#bot spellengagedpriority help` if local aliases differ.

2. Spawn or choose at least three attackable NPCs that are close enough to fight the owner group but far enough
   apart that individual targets can be inspected.

   Example temporary spawns:

   ```text
   #spawn smoke_slow_a 1 20 0 10000 0 1
   #spawn smoke_slow_b 1 20 0 10000 0 1
   #spawn smoke_slow_c 1 20 0 10000 0 1
   ```

3. Make all test NPCs **Engaged Hostiles** by pulling them onto the owner, the owner group, or group pets. They
   must be alive, attackable, and actively part of the same combat situation. Nearby idle NPCs are not enough.

4. Turn on enough evidence to identify bot casts and target buffs. Useful surfaces are:

   ```text
   #show buffs
   #showbuffs
   ```

   Also keep zone logs from the AkkStack container available if the client does not show enough spell detail:

   ```sh
   cd ../bump-akk-stack
   docker-compose -f docker-compose.yml -f docker-compose.dev.yml logs --tail=200 eqemu-server
   ```

## Smoke Steps

### 1. Current Target Is Slowed First

1. Clear prior test buffs by waiting out, dispelling, or replacing the temporary NPCs.
2. Target `smoke_slow_a`.
3. Start combat with `smoke_slow_a`, then add `smoke_slow_b` and `smoke_slow_c` to the same fight before the bot's
   first slow opportunity.
4. Observe the Shaman bot's first single-target `Slow` cast.

Expected result: the first successful slow is cast on the bot's current target, `smoke_slow_a`. `smoke_slow_b`
and `smoke_slow_c` should remain unslowed until a later slow opportunity.

Record: target order, bot stance, slow spell name or ID, the first NPC with the slow buff, and any relevant client
or zone log lines.

### 2. Already-Slowed Current Target Allows Another Engaged Hostile Later

1. Keep `smoke_slow_a` as the current target after it is slowed.
2. Keep at least one other unslowed NPC actively engaged with the owner group.
3. Wait for the bot's next normal slow opportunity without issuing a direct slow command.

Expected result: the bot may choose another unslowed **Engaged Hostile**, such as `smoke_slow_b`, on a later
single-target slow opportunity. The bot should not cast multiple slow spells in one AI pass, so this should be
eventual rather than immediate burst behavior.

Record: how long or how many combat rounds passed before the second slow, which NPC was slowed next, and whether
the current target remained selected.

### 3. Mezzed Engaged Hostile Is Skipped In v1

1. Reset or continue with at least three engaged NPCs.
2. Make one non-current **Engaged Hostile** mezzed before the bot's next slow opportunity. Use a player mez,
   an Enchanter bot configured for crowd control, or a GM spell cast if that is the cleanest local setup.
3. Keep another non-mezzed unslowed **Engaged Hostile** in the fight.
4. Leave the current target already slowed or otherwise not needing a slow.

Expected result: the bot skips the mezzed **Engaged Hostile** and may slow a different unslowed engaged NPC. If
the only remaining unslowed candidate is mezzed, the bot should skip it instead of breaking mez with slow.

Record: the mez spell used, which NPC was mezzed, buff state for the mezzed NPC, and the next slow target.

### 4. Optional Duplicate-Cast Observation With A Second Slower

1. Add a second slower-capable bot, preferably another Shaman with the same stance and slow priority settings.
2. Repeat the multi-NPC engagement with the first target already slowed and at least one other unslowed
   **Engaged Hostile**.
3. Observe whether both slower bots try to cast on the same candidate around the same time.

Expected result: v1 relies on existing spell casting and stacking checks, so explicit cross-bot coordination is
not expected. Duplicate attempts are acceptable evidence to capture if they occur; successful duplicate slow
stacking should not occur unless the spells legitimately stack under normal rules.

Record: both bot names, spell names or IDs, timestamps or combat-round order, target names, and whether one cast
was blocked, resisted, overwritten, or landed.

## If Behavior Differs From The PRD

Keep the server running long enough to capture evidence, then record:

- exact server commit and branch;
- bot names, classes, levels, stances, and slow priority settings;
- zone, NPC names, levels, slow mitigation or immunity if known, and whether each NPC was an **Engaged Hostile**;
- current target at each slow opportunity;
- buff lists for the current target, the mezzed target, and the next slowed target;
- slow and mez spell names or IDs;
- relevant client chat/combat lines and `eqemu-server` log tail;
- whether the difference is target choice, skipped casting, duplicate casting, mez breakage, stacking, resist, range,
  line of sight, immunity, or another spell-validity failure.

Attach those notes to the PRD or follow-up Bead rather than changing AkkStack or persistent database state during
the smoke.

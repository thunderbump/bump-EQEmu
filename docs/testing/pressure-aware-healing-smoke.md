# Pressure-Aware Bot Healing Live Smoke

Use this manual smoke when validating opt-in pressure-aware bot healing in the local AkkStack dev runtime.
It is a Tier 4 or Tier 5 validation pass that complements the deterministic C++ coverage in
`tests/pressure_aware_healing_test.h`.

This smoke validates the first pressure-aware healing implementation only:

- pressure-aware healing is disabled by default and must be enabled deliberately;
- **Incoming Damage Pressure** is recorded on the heal target from recent combat HP loss;
- missing or expired pressure may prefer valid single-target HoT sustain;
- fresh pressure suppresses single-target HoT sustain and may escalate ordinary direct heals;
- direct-heal escalation is limited to `RegularHeal`, `FastHeals`, and `VeryFastHeals`;
- existing spell holds, thresholds, recast timers, mana limits, HP limits, aggro checks, range, stacking, and
  spell validity remain authoritative;
- `CompleteHeal`, `GroupCompleteHeals`, group heals, group HoTs, heal rotations, and cross-healer coordination
  remain out of scope for v1 behavior changes.

## State Flow

Pressure-aware healing is controlled by server rules under the `Bots` rule category:

- `PressureAwareHealingEnabled`, default `false`
- `PressureAwareHealingPressureSampleMS`, default `3000`
- `PressureAwareHealingEmergencyProjectionMS`, default `2000`
- `PressureAwareHealingHoTSustainMS`, default `8000`

`PressureAwareHealingEnabled` must be set to `true` for pressure-aware selection to affect bot heal choice.
With the default `false` value, bot healing should follow existing behavior.

**Incoming Damage Pressure** is updated in the normal combat damage path. After final positive damage is known in
`Mob::CommonDamage`, `PressureAwareHealing::ShouldRecordCombatDamage` filters out non-positive damage,
no-attacker environmental damage, and self-inflicted damage where the runtime can identify those cases cheaply.
Accepted damage is recorded through `Mob::RecordIncomingDamagePressure`, which stores the rolling
`PressureAwareHealing::IncomingDamagePressure` on the damaged `Mob`.

Bot heal selection reads that target-owned state in `Bot::BotCastHeal`. The bot loads pressure-aware settings,
asks the target whether it has active pressure using the configured sample window, and then applies the helper
decisions inside the existing spell settings model:

- under fresh pressure, single-target HoT sustain preference is suppressed;
- for ordinary direct heal categories, `PressureAwareHealing::SelectDirectHealSpellType` compares candidate cast
  time plus the emergency projection window against existing bot heal thresholds and selects the least urgent
  available direct heal projected to land safely;
- with missing or expired pressure, `RegularHeal` and `PetRegularHeals` may prefer valid single-target HoT sustain
  if ordinary `PrecastChecks` and spell lookup allow the HoT;
- if the candidate HoT or direct-heal category fails ordinary bot checks, the normal fallback path remains in
  control.

Healing received changes current HP, but it does not subtract from **Incoming Damage Pressure** directly. The next
selection uses the improved current HP with the still-recent pressure signal.

## Runtime

Start the dev runtime from this checkout:

```sh
./scripts/start-akkstack-runtime-proof.sh
```

Then log in with a GM-capable test character. Use a quiet zone with room to isolate one tank-like target and a
healer bot. Record the client build, character, zone, server commit, branch, and whether the runtime helper used
the normal AkkStack compose files or the local port-remap overlay.

Do not run destructive server operations. Prefer temporary `#spawn` NPCs and ordinary bot commands over database
spawn edits.

## Test Setup

1. Enable pressure-aware healing explicitly for the dev runtime. Record the exact rule values used:

   ```text
   PressureAwareHealingEnabled=true
   PressureAwareHealingPressureSampleMS=3000
   PressureAwareHealingEmergencyProjectionMS=2000
   PressureAwareHealingHoTSustainMS=8000
   ```

   Use the local server's normal rule-editing workflow. If rule changes require a world or zone restart, record
   that restart.

2. Create or reuse a healer bot with usable `RegularHeal`, `FastHeals`, `VeryFastHeals`, and single-target HoT
   spells at its level. A Cleric, Shaman, or Druid bot can be useful depending on the spell list being inspected.
   Record bot name, class, level, stance, spell holds, spell priorities, and relevant threshold settings.

3. Choose a tank-like heal target that the bot already heals under ordinary rules. This may be the player, a bot,
   or a pet if that target type is already considered by existing bot healing behavior. Do not create a new target
   selection path for the smoke.

4. Choose one or more attackable NPCs that can produce controlled incoming damage without instantly killing the
   target. Temporary spawns are preferred.

5. Keep enough evidence visible to identify selected heal type and timing. Useful evidence includes client combat
   text, bot spell messages, target HP percentage, visible buffs, healer mana, and zone logs:

   ```sh
   cd ../bump-akk-stack
   docker-compose -f docker-compose.yml -f docker-compose.dev.yml logs --tail=300 eqemu-server
   ```

## Smoke Steps

### 1. Disabled Mode Baseline

1. Confirm `PressureAwareHealingEnabled=false`.
2. Repeat one ordinary healing situation where the bot would cast a direct heal or HoT under existing settings.

Expected result: pressure-aware behavior is not visible. Existing target selection, spell holds, thresholds,
recast timers, mana limits, and spell validity control the heal choice.

Record: rule value, selected heal type, target HP, healer mana, and any visible difference from known baseline
behavior.

### 2. Missing Or Expired Pressure Allows HoT Sustain

1. Enable pressure-aware healing.
2. Start from a target with missing pressure or wait longer than `PressureAwareHealingPressureSampleMS` after the
   last combat hit.
3. Put the target into an eligible sustain-healing range where a single-target HoT passes ordinary bot checks.
4. Let the healer choose naturally.

Expected result: a valid single-target HoT may be preferred for sustain. If HoTs are held, unavailable, blocked by
stacking, blocked by recast, out of mana, out of range, or otherwise invalid, the bot falls back to ordinary direct
heal behavior.

Record: selected heal type, target HP, elapsed time since last hit, whether pressure should be expired, healer
mana, HoT availability, and fallback reason if no HoT is selected.

### 3. Chip Pressure Does Not Force Fastest Heal

1. Enable pressure-aware healing.
2. Let the target take light steady combat damage.
3. Keep the target above emergency thresholds where `RegularHeal` is projected to land safely.
4. Let the healer choose naturally.

Expected result: pressure-aware selection should not always jump to the fastest heal. If `RegularHeal` passes
ordinary checks and is projected to land safely before the next dangerous threshold, `RegularHeal` remains
eligible.

Record: selected heal type, target HP, recent pressure estimate if available from logs or instrumentation,
candidate heal cast time, configured emergency projection, healer mana, and whether any overheal was obvious.

### 4. Moderate Pressure Escalates To FastHeals

1. Increase incoming combat damage so `RegularHeal` appears too slow but `FastHeals` can plausibly land safely.
2. Keep `FastHeals` valid under ordinary bot checks.
3. Let the healer choose naturally.

Expected result: the bot may escalate from `RegularHeal` to `FastHeals`, choosing the least urgent direct heal
category projected to land safely.

Record: selected heal type, target HP, recent pressure estimate, `RegularHeal` and `FastHeals` cast times if
known, threshold settings, healer mana, deaths or near-deaths, and overheal observations.

### 5. Spike Pressure Escalates To VeryFastHeals

1. Increase incoming combat damage enough that `FastHeals` appears unsafe but `VeryFastHeals` is available and
   valid.
2. Let the healer choose naturally.

Expected result: the bot may escalate to `VeryFastHeals` when slower direct heal categories are not projected to
land safely. Existing spell checks still block unavailable or invalid very-fast heals.

Record: selected heal type, target HP, recent pressure estimate, danger-window decision if instrumented, healer
mana, deaths or near-deaths, and overheal observations.

### 6. Emergency Direct Heals Are Not Delayed By HoT Preference

1. Put the target inside an emergency direct-heal threshold.
2. Ensure a HoT exists and would otherwise be available.
3. Let the healer choose naturally.

Expected result: HoT preference does not delay emergency direct healing. `FastHeals` or `VeryFastHeals` should
remain available according to existing settings and ordinary spell checks.

Record: selected heal type, target HP, emergency threshold settings, HoT availability, healer mana, and survival
outcome.

### 7. Two-Healer Observation Without New Coordination Scope

1. Add a second healer bot with comparable direct-heal options.
2. Repeat a moderate-pressure or spike-pressure scenario.
3. Observe whether both healers react to the same pressured target.

Expected result: v1 does not add cross-healer coordination. Existing per-target recast timers, spell validity,
and heal rotations remain the only coordination mechanisms. Duplicate panic reactions are evidence to capture,
not a failure by itself unless they bypass existing settings or destabilize survival.

Record: both healer names, classes, levels, selected heal types, target HP, healer mana, cast order, overheal,
deaths or near-deaths, and whether a heal rotation was active.

## If Behavior Differs From The PRD

Keep the server running long enough to capture evidence, then record:

- exact server commit and branch;
- rule values for `PressureAwareHealingEnabled`, `PressureAwareHealingPressureSampleMS`,
  `PressureAwareHealingEmergencyProjectionMS`, and `PressureAwareHealingHoTSustainMS`;
- healer bot names, classes, levels, stances, spell holds, priorities, thresholds, delays, and mana settings;
- target type, max HP, current HP at each heal opportunity, and whether the target was already under an emergency
  direct-heal threshold;
- recent damage pattern, attacker names, and whether damage was combat damage, self-inflicted, or environmental;
- selected heal type, spell name or ID, cast time if known, and whether the cast landed, failed, was resisted, or
  was blocked by ordinary spell validity;
- pressure estimate and danger-window decision if visible through logs or temporary instrumentation;
- healer mana, deaths or near-deaths, and obvious overheal observations;
- whether a second healer or heal rotation was present.

Attach those notes to the PRD or a follow-up Bead. Complete-heal timing concerns belong in `central-hii`; raid-scale
or multi-healer coordination concerns belong in `central-95v`.

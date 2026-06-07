# Efficient RegularHeal Live Smoke

Use this manual smoke when validating opt-in efficient bot `RegularHeal` spell selection in the local AkkStack dev
runtime. It is a Tier 4 or Tier 5 validation pass that follows the deterministic C++ coverage in
`tests/regular_heal_efficiency_test.h` and `tests/pressure_aware_healing_test.h`; it does not replace those tests.

This smoke validates real bot spell-list behavior only:

- `Bots:PreferEfficientRegularHeals` is disabled by default and must be enabled deliberately;
- with the rule disabled, `RegularHeal` spell choice should remain the current priority-first choice;
- with the rule enabled, non-rotation `RegularHeal` may choose the smallest valid direct heal that sufficiently
  covers the target's missing HP;
- insufficient smaller heals must not be selected over the larger current choice;
- ordinary spell validity, thresholds, holds, recast timers, mana limits, HP limits, aggro checks, range, stacking,
  and heal rotations remain authoritative;
- active pressure still runs through pressure-aware heal type selection before efficient `RegularHeal` spell choice;
- `FastHeals`, `VeryFastHeals`, `CompleteHeal`, group heals, HoTs, pet heals, heal rotations, and cross-healer
  coordination remain out of scope for efficient `RegularHeal` spell selection.

## State Flow

Efficient `RegularHeal` selection is controlled by this server rule under the `Bots` rule category:

- `PreferEfficientRegularHeals`, default `false`

When the rule is `false`, bot `RegularHeal` selection should use the existing priority-first spell choice. When the
rule is `true`, a non-rotation `RegularHeal` opportunity may inspect the runtime candidate list, estimate ordinary
direct-heal amounts, and choose the smallest sufficient heal. The fallback remains the existing current choice when
an estimate is uncertain or no candidate covers the target's missing HP.

Pressure-aware healing is controlled separately by these `Bots` rules:

- `PressureAwareHealingEnabled`, default `false`
- `PressureAwareHealingPressureSampleMS`, default `3000`
- `PressureAwareHealingEmergencyProjectionMS`, default `2000`
- `PressureAwareHealingHoTSustainMS`, default `8000`

When pressure-aware healing is enabled and the target has active **Incoming Damage Pressure**, pressure-aware
selection may keep `RegularHeal` or escalate to `FastHeals` or `VeryFastHeals` before efficient `RegularHeal`
selection can choose a spell. If pressure escalation changes the heal type away from `RegularHeal`,
`PreferEfficientRegularHeals` should not weaken that escalation.

## Runtime

Start the dev runtime from this checkout:

```sh
./scripts/start-akkstack-runtime-proof.sh
```

Then log in with a GM-capable test character. Use a quiet zone with room to isolate one healer bot and one
tank-like heal target. The target may be the player, a tank bot, or another target type already healed by ordinary
bot behavior. Do not create a new target selection path for this smoke.

Record the client build, character, zone, server commit, branch, bot name, bot class, bot level, bot stance,
target type, target max HP, and whether the runtime helper used the normal AkkStack compose files or the local
port-remap overlay.

Do not run destructive server operations. Prefer temporary `#spawn` NPCs, controlled GM damage, and ordinary bot
commands over database spawn edits.

## Test Setup

1. Create or reuse one healer bot with at least two usable ordinary single-target `RegularHeal` spells where a
   smaller spell and a larger current priority-first spell can both be observed. Record spell names, spell IDs if
   visible, priorities, spell holds, relevant thresholds, cast times, recast state, and mana costs if known.

2. Create or reuse one tank-like target whose max HP is high enough to show both a moderate missing-HP case and a
   larger missing-HP case. Keep other healers away so selected spells and survivability outcomes are attributable
   to the one healer bot.

3. Keep enough evidence visible to identify the selected spell and healing result. Useful evidence includes client
   combat text, bot spell messages, target HP before and after each cast, healer mana before and after each cast,
   deaths or near-deaths, and zone logs:

   ```sh
   cd ../bump-akk-stack
   docker-compose -f docker-compose.yml -f docker-compose.dev.yml logs --tail=300 eqemu-server
   ```

4. If temporary instrumentation is available, capture the estimated heal amount for each `RegularHeal` candidate
   and the candidate that efficient selection chose. The smoke remains useful without instrumentation if selected
   spell, target HP movement, and healer mana are recorded.

## Smoke Steps

### 1. Rule-Off Baseline

1. Confirm `PreferEfficientRegularHeals=false`.
2. Keep `PressureAwareHealingEnabled=false` for the baseline.
3. Put the tank-like target into a moderate missing-HP state where the smaller `RegularHeal` would be sufficient.
4. Let the healer choose naturally.

Expected result: the bot uses the current priority-first `RegularHeal` choice rather than switching to the smaller
sufficient spell because efficient selection is disabled.

Record: rule values, selected spell, target HP before healing, target HP after healing, estimated heal amount if
instrumented, healer mana before and after, deaths or near-deaths, and whether the baseline appears survivable.

### 2. Rule-On Moderate Missing HP Saves Mana

1. Set `PreferEfficientRegularHeals=true`.
2. Keep `PressureAwareHealingEnabled=false`.
3. Repeat the same moderate missing-HP setup with the same healer bot and tank-like target.
4. Let the healer choose naturally.

Expected result: if the smaller `RegularHeal` passes ordinary bot checks and its estimated amount sufficiently
covers the target's missing HP, the bot may choose it instead of the larger current priority-first spell.
Survivability should not be weaker than the rule-off baseline.

Record: selected spell, target HP before and after healing, estimated heal amount if instrumented, healer mana
before and after, deaths or near-deaths, and whether mana was saved without weakening survivability.

### 3. Rule-On Larger Missing HP Keeps Larger Choice

1. Keep `PreferEfficientRegularHeals=true`.
2. Keep `PressureAwareHealingEnabled=false`.
3. Put the target into a larger missing-HP state where the smaller `RegularHeal` is insufficient but the larger
   current priority-first spell is sufficient and valid.
4. Let the healer choose naturally.

Expected result: the bot should not pick the insufficient smaller heal. The larger current choice should still cast
when it is the smallest sufficient available candidate or when no smaller sufficient candidate exists.

Record: selected spell, target HP before and after healing, estimated heal amount if instrumented, healer mana
before and after, deaths or near-deaths, and whether the target remained safely alive.

### 4. Active Pressure Keeps Escalation In Control

1. Keep `PreferEfficientRegularHeals=true`.
2. Enable pressure-aware healing and record exact rule values:

   ```text
   PressureAwareHealingEnabled=true
   PressureAwareHealingPressureSampleMS=3000
   PressureAwareHealingEmergencyProjectionMS=2000
   PressureAwareHealingHoTSustainMS=8000
   ```

3. Create controlled active combat pressure against the same tank-like target with one attackable NPC.
4. Increase pressure until the pressure-aware rules should escalate from `RegularHeal` to `FastHeals` or
   `VeryFastHeals` before the target reaches a dangerous threshold.
5. Let the healer choose naturally.

Expected result: active pressure controls heal type before efficient `RegularHeal` spell choice. If pressure-aware
selection escalates to `FastHeals` or `VeryFastHeals`, efficient `RegularHeal` selection should not pull the cast
back to a smaller `RegularHeal`. If pressure remains low enough that `RegularHeal` is still projected safe, then
efficient `RegularHeal` selection may still choose among valid `RegularHeal` spells.

Record: selected heal type, selected spell, target HP before and after healing, recent pressure estimate if
available, estimated `RegularHeal` amounts if instrumented, healer mana before and after, deaths or near-deaths,
and whether mana was saved without weakening survivability.

## If Behavior Differs From The PRD

Keep the server running long enough to capture evidence, then record:

- exact server commit and branch;
- rule values for `PreferEfficientRegularHeals`, `PressureAwareHealingEnabled`,
  `PressureAwareHealingPressureSampleMS`, `PressureAwareHealingEmergencyProjectionMS`, and
  `PressureAwareHealingHoTSustainMS`;
- healer bot name, class, level, stance, spell holds, spell priorities, thresholds, delays, and mana settings;
- target type, max HP, current HP before and after each heal opportunity, and whether the target was already under
  an emergency direct-heal threshold;
- selected heal type, selected spell name or ID, cast time if known, and whether the cast landed, failed, was
  resisted, or was blocked by ordinary spell validity;
- estimated heal amount and candidate list if visible through logs or temporary instrumentation;
- recent damage pattern, attacker name, pressure estimate, and pressure-aware danger-window decision if visible;
- healer mana before and after each cast, deaths or near-deaths, and obvious overheal observations;
- whether mana was saved without weakening survivability compared with the rule-off baseline.

Attach those notes to the PRD or a follow-up Bead.

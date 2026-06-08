# Zone-owned Bot Heal Selection

Normal single-target bot direct-heal spell choice will move behind a zone-owned **Bot Heal Selection** module. **Bot Heal Selection** will produce both the selected bot heal spell type and the concrete heal spell for an already-selected heal target and requested bot heal spell type.

The first pass will include pressure-aware direct-heal escalation, **Recovery Healing** HoT substitution that starts from a single-target direct-heal request, and efficient regular-heal choice. Primary HoT requests, group heals, group HoTs, complete heals, and heal rotations remain outside the first pass.

This module will live with zone runtime behavior instead of `common/` because selection depends on live bot spell checks, target state, cast timing, spell thresholds, and concrete `BotSpell` details. Common pure helpers, including the current efficient regular-heal ranking logic, may remain as internal support. **Bot Heal Selection** should receive explicit healing behavior settings rather than reading server rules internally.

The first module interface will use existing zone runtime objects directly rather than a custom adapter. A custom adapter should wait until a second real caller needs one.

**Consequences**

- `BotCastHeal` can stop coordinating pressure escalation, HoT substitution, efficient regular-heal selection, and fallback spell lookup directly.
- The first implementation may reuse existing bot spell lookup helpers inside **Bot Heal Selection**; the initial goal is to move orchestration out of `BotCastHeal`, not rewrite all heal spell lookup at once.
- The selected bot heal spell type remains visible to cast execution for recast timers and player-facing cast messages.
- Candidate spell validity checks for pressure-escalation and **Recovery Healing** alternatives belong inside **Bot Heal Selection**.
- Alternative selections should fall back to the ordinary requested heal spell when the alternative cannot produce a valid concrete spell.
- Heal rotations keep their current cadence and fast-heal behavior until separately evaluated.

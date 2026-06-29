/*	EQEmu: EQEmulator

	Copyright (C) 2001-2026 EQEmu Development Team

	This program is free software; you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation; either version 3 of the License, or
	(at your option) any later version.

	This program is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
	GNU General Public License for more details.

	You should have received a copy of the GNU General Public License
	along with this program. If not, see <http://www.gnu.org/licenses/>.
*/
#pragma once

#include "common/spdat.h"
#include "common/types.h"

namespace BotHealSelection {

template <typename Spell>
struct SelectionResult {
	bool found = false;
	uint16 selected_spell_type = 0;
	Spell spell{};
};

template <typename Spell>
SelectionResult<Spell> Found(uint16 selected_spell_type, const Spell &spell)
{
	if (spell.SpellId == 0) {
		return {};
	}

	return {
		.found = true,
		.selected_spell_type = selected_spell_type,
		.spell = spell
	};
}

template <typename Spell>
SelectionResult<Spell> PreferAlternativeOrFallback(
	uint16 alternative_spell_type,
	const Spell &alternative_spell,
	uint16 fallback_spell_type,
	const Spell &fallback_spell
)
{
	const auto alternative = Found(alternative_spell_type, alternative_spell);
	if (alternative.found) {
		return alternative;
	}

	return Found(fallback_spell_type, fallback_spell);
}

inline bool AllowsCompleteHealParentFallback(
	uint16 requested_spell_type,
	uint16 candidate_spell_type,
	uint8 target_hp_ratio,
	uint8 regular_heal_max_threshold
)
{
	return requested_spell_type != BotSpellTypes::CompleteHeal ||
		candidate_spell_type != BotSpellTypes::RegularHeal ||
		target_hp_ratio <= regular_heal_max_threshold;
}

}

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
#include "bot_heal_selection.h"

#include "bot.h"
#include "common/spdat.h"
#include "mob.h"

#include <array>

namespace BotHealSelection {

namespace {

bool IsPlayerDirectHealFamily(uint16 spell_type)
{
	return spell_type == BotSpellTypes::RegularHeal ||
		spell_type == BotSpellTypes::FastHeals ||
		spell_type == BotSpellTypes::VeryFastHeals;
}

bool IsPetDirectHealFamily(uint16 spell_type)
{
	return spell_type == BotSpellTypes::PetRegularHeals ||
		spell_type == BotSpellTypes::PetFastHeals ||
		spell_type == BotSpellTypes::PetVeryFastHeals;
}

std::array<uint16, 3> DirectHealFamilyFor(uint16 spell_type)
{
	if (IsPetDirectHealFamily(spell_type)) {
		return {
			BotSpellTypes::PetRegularHeals,
			BotSpellTypes::PetFastHeals,
			BotSpellTypes::PetVeryFastHeals
		};
	}

	return {
		BotSpellTypes::RegularHeal,
		BotSpellTypes::FastHeals,
		BotSpellTypes::VeryFastHeals
	};
}

BotSpell GetCheckedHealSpell(
	Bot &caster,
	Mob &target,
	uint16 spell_type,
	const RegularHealEfficiency::Settings &efficiency_settings
)
{
	if (!caster.PrecastChecks(&target, spell_type)) {
		return {};
	}

	return caster.GetSpellByHealType(spell_type, &target, &efficiency_settings);
}

Result SelectPressureAwareDirectHeal(
	Bot &caster,
	Mob &target,
	uint16 requested_spell_type,
	const PressureAwareHealing::Settings &pressure_settings,
	const RegularHealEfficiency::Settings &efficiency_settings
)
{
	if (
		!pressure_settings.enabled ||
		!target.HasActiveIncomingDamagePressure(pressure_settings) ||
		(!IsPlayerDirectHealFamily(requested_spell_type) && !IsPetDirectHealFamily(requested_spell_type))
	) {
		return {};
	}

	const auto direct_heal_spell_types = DirectHealFamilyFor(requested_spell_type);
	std::array<PressureAwareHealing::DirectHealCandidate, direct_heal_spell_types.size()> candidates{};
	std::array<BotSpell, direct_heal_spell_types.size()> candidate_spells{};

	for (size_t i = 0; i < direct_heal_spell_types.size(); ++i) {
		const auto candidate_spell_type = direct_heal_spell_types[i];
		const auto candidate_spell = GetCheckedHealSpell(caster, target, candidate_spell_type, efficiency_settings);

		candidate_spells[i] = candidate_spell;
		candidates[i] = {
			.spell_type = candidate_spell_type,
			.available = IsValidSpell(candidate_spell.SpellId),
			.cast_time_ms = IsValidSpell(candidate_spell.SpellId) ?
				static_cast<uint32>(caster.GetActSpellCasttime(candidate_spell.SpellId, spells[candidate_spell.SpellId].cast_time)) :
				0,
			.max_threshold_percent = caster.GetUltimateSpellTypeMaxThreshold(candidate_spell_type, &target)
		};
	}

	const auto selected_spell_type = PressureAwareHealing::SelectDirectHealSpellType(
		requested_spell_type,
		target.GetIncomingDamagePressure(),
		pressure_settings,
		target.GetHP(),
		target.GetMaxHP(),
		{candidates[0], candidates[1], candidates[2]}
	);

	if (selected_spell_type == requested_spell_type) {
		return {};
	}

	for (size_t i = 0; i < direct_heal_spell_types.size(); ++i) {
		if (direct_heal_spell_types[i] == selected_spell_type) {
			return Found(selected_spell_type, candidate_spells[i]);
		}
	}

	return {};
}

Result SelectRecoveryHeal(
	Bot &caster,
	Mob &target,
	uint16 requested_spell_type,
	const PressureAwareHealing::Settings &pressure_settings,
	const RegularHealEfficiency::Settings &efficiency_settings
)
{
	const uint16 hot_spell_type = PressureAwareHealing::SustainHoTSpellTypeFor(requested_spell_type);
	if (!pressure_settings.enabled || hot_spell_type == 0) {
		return {};
	}

	const auto hot_spell = GetCheckedHealSpell(caster, target, hot_spell_type, efficiency_settings);
	const auto selected_spell_type = PressureAwareHealing::SelectSustainHealSpellType(
		requested_spell_type,
		IsValidSpell(hot_spell.SpellId) ? hot_spell_type : 0,
		target.HasActiveIncomingDamagePressure(pressure_settings),
		pressure_settings
	);

	if (selected_spell_type != hot_spell_type) {
		return {};
	}

	return Found(selected_spell_type, hot_spell);
}

}

Result Select(
	Bot &caster,
	Mob &target,
	uint16 requested_spell_type,
	const PressureAwareHealing::Settings &pressure_settings,
	const RegularHealEfficiency::Settings &efficiency_settings
)
{
	const auto direct_heal_selection = SelectPressureAwareDirectHeal(
		caster,
		target,
		requested_spell_type,
		pressure_settings,
		efficiency_settings
	);
	if (direct_heal_selection.found) {
		return direct_heal_selection;
	}

	const auto selected_spell_type = requested_spell_type;

	const auto recovery_selection = SelectRecoveryHeal(
		caster,
		target,
		selected_spell_type,
		pressure_settings,
		efficiency_settings
	);
	if (recovery_selection.found) {
		return recovery_selection;
	}

	return Found(selected_spell_type, caster.GetSpellByHealType(selected_spell_type, &target, &efficiency_settings));
}

}

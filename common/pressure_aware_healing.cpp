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
#include "pressure_aware_healing.h"

#include "rulesys.h"
#include "spdat.h"

#include <vector>

namespace PressureAwareHealing {

Settings LoadSettingsFromRules()
{
	return {
		.enabled = RuleB(Bots, PressureAwareHealingEnabled),
		.pressure_sample_ms = RuleI(Bots, PressureAwareHealingPressureSampleMS),
		.emergency_projection_ms = RuleI(Bots, PressureAwareHealingEmergencyProjectionMS),
		.hot_sustain_ms = RuleI(Bots, PressureAwareHealingHoTSustainMS)
	};
}

uint16_t DisabledModeSpellType(uint16_t current_spell_type, const Settings &settings)
{
	if (!settings.enabled) {
		return current_spell_type;
	}

	return current_spell_type;
}

uint16_t SustainHoTSpellTypeFor(uint16_t current_spell_type)
{
	switch (current_spell_type) {
		case BotSpellTypes::RegularHeal:
			return BotSpellTypes::HoTHeals;
		case BotSpellTypes::PetRegularHeals:
			return BotSpellTypes::PetHoTHeals;
		default:
			return 0;
	}
}

bool ShouldRecordCombatDamage(int64_t damage, bool has_attacker, bool is_self_inflicted)
{
	return damage > 0 && has_attacker && !is_self_inflicted;
}

void RecordCombatDamage(IncomingDamagePressure &pressure, int64_t damage, uint32_t current_time_ms)
{
	if (damage <= 0) {
		return;
	}

	pressure.damage += damage;
	pressure.updated_at_ms = current_time_ms;
}

bool HasActiveDamagePressure(
	const IncomingDamagePressure &pressure,
	const Settings &settings,
	uint32_t current_time_ms
)
{
	if (!settings.enabled || pressure.damage <= 0 || settings.pressure_sample_ms <= 0) {
		return false;
	}

	return current_time_ms - pressure.updated_at_ms <= static_cast<uint32_t>(settings.pressure_sample_ms);
}

uint16_t SelectSustainHealSpellType(
	uint16_t current_spell_type,
	uint16_t hot_spell_type,
	bool has_active_damage_pressure,
	const Settings &settings
)
{
	if (!settings.enabled) {
		return current_spell_type;
	}

	if (hot_spell_type == 0) {
		return current_spell_type;
	}

	if (has_active_damage_pressure) {
		return current_spell_type;
	}

	switch (current_spell_type) {
		case BotSpellTypes::RegularHeal:
		case BotSpellTypes::PetRegularHeals:
			return hot_spell_type;
		default:
			return current_spell_type;
	}
}

uint16_t SelectDirectHealSpellType(
	uint16_t current_spell_type,
	const IncomingDamagePressure &pressure,
	const Settings &settings,
	int64_t current_hp,
	int64_t max_hp,
	std::initializer_list<DirectHealCandidate> candidates
)
{
	if (!settings.enabled || pressure.damage <= 0 || settings.pressure_sample_ms <= 0 || current_hp <= 0 || max_hp <= 0) {
		return current_spell_type;
	}

	const std::vector<DirectHealCandidate> ordered_candidates(candidates);
	size_t start_index = 0;
	for (; start_index < ordered_candidates.size(); ++start_index) {
		if (ordered_candidates[start_index].spell_type == current_spell_type) {
			break;
		}
	}

	if (start_index >= ordered_candidates.size()) {
		return current_spell_type;
	}

	for (size_t i = start_index; i < ordered_candidates.size(); ++i) {
		const auto &candidate = ordered_candidates[i];
		if (!candidate.available) {
			continue;
		}

		if (i + 1 >= ordered_candidates.size()) {
			return candidate.spell_type;
		}

		const auto danger_threshold = ordered_candidates[i + 1].max_threshold_percent;
		const auto projection_ms = static_cast<int64_t>(candidate.cast_time_ms) + settings.emergency_projection_ms;
		const auto projected_hp = current_hp - ((pressure.damage * projection_ms) / settings.pressure_sample_ms);
		const auto projected_hp_percent = (projected_hp * 100) / max_hp;

		if (projected_hp_percent > danger_threshold) {
			return candidate.spell_type;
		}
	}

	return current_spell_type;
}

}

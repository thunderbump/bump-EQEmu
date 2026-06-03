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

uint16_t SelectSustainHealSpellType(
	uint16_t current_spell_type,
	uint16_t hot_spell_type,
	const Settings &settings
)
{
	if (!settings.enabled) {
		return current_spell_type;
	}

	if (hot_spell_type == 0) {
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

}

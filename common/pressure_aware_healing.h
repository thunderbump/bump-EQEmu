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

#include <cstdint>

namespace PressureAwareHealing {

struct Settings {
	bool enabled = false;
	int pressure_sample_ms = 3000;
	int emergency_projection_ms = 2000;
	int hot_sustain_ms = 8000;
};

struct IncomingDamagePressure {
	int64_t damage = 0;
	uint32_t updated_at_ms = 0;
};

Settings LoadSettingsFromRules();

uint16_t DisabledModeSpellType(uint16_t current_spell_type, const Settings &settings);
uint16_t SustainHoTSpellTypeFor(uint16_t current_spell_type);
bool ShouldRecordCombatDamage(int64_t damage, bool has_attacker, bool is_self_inflicted);
void RecordCombatDamage(IncomingDamagePressure &pressure, int64_t damage, uint32_t current_time_ms);
bool HasActiveDamagePressure(
	const IncomingDamagePressure &pressure,
	const Settings &settings,
	uint32_t current_time_ms
);
uint16_t SelectSustainHealSpellType(
	uint16_t current_spell_type,
	uint16_t hot_spell_type,
	bool has_active_damage_pressure,
	const Settings &settings
);

}

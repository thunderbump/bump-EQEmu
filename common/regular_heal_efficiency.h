/*	EQEmu: EQEmulator

	Copyright (C) 2001-2026 EQEmu Development Team

	This program is free software; you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation; either version 3 of the License, or
	(at your option) any later version.

	This program is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERMERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
	GNU General Public License for more details.

	You should have received a copy of the GNU General Public License
	along with this program. If not, see <http://www.gnu.org/licenses/>.
*/
#pragma once

#include <cstdint>
#include <initializer_list>

namespace RegularHealEfficiency {

struct Settings {
	bool prefer_efficient_regular_heals = false;
};

struct Candidate {
	uint16_t spell_id = 0;
	uint32_t list_order = 0;
	int32_t mana_cost = 0;
	bool has_usable_estimated_heal = false;
	int64_t estimated_heal = 0;
};

struct Selection {
	bool found = false;
	uint16_t spell_id = 0;
	uint32_t list_order = 0;
	bool selected_for_efficiency = false;
};

Settings LoadSettingsFromRules();
Selection SelectRegularHealCandidate(
	const Settings &settings,
	int64_t target_missing_hp,
	int64_t sufficient_heal_margin,
	std::initializer_list<Candidate> candidates
);

}

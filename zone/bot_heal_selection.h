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

#include "bot_heal_selection_result.h"
#include "bot_structs.h"
#include "common/pressure_aware_healing.h"
#include "common/regular_heal_efficiency.h"
#include "common/types.h"

class Bot;
class Mob;

namespace BotHealSelection {

using Result = SelectionResult<BotSpell>;

Result Select(
	Bot &caster,
	Mob &target,
	uint16 requested_spell_type,
	const PressureAwareHealing::Settings &pressure_settings,
	const RegularHealEfficiency::Settings &efficiency_settings
);

}

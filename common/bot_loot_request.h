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

#include "item_data.h"

#include <cstdint>
#include <string>
#include <vector>

namespace BotLootRequest {

struct Settings {
	bool enabled = false;
};

struct ItemSnapshot {
	const EQ::ItemData *item = nullptr;
};

struct GroupedBotSnapshot {
	uint32_t name_stable_id = 0;
	std::string name;
	std::vector<ItemSnapshot> equipped_items;
};

struct SuccessfulLootEvent {
	std::string looter_name;
	const EQ::ItemData *looted_item = nullptr;
	std::string looted_item_link;
	std::vector<GroupedBotSnapshot> grouped_bots;
};

struct Request {
	bool produced = false;
	uint32_t requesting_bot_stable_id = 0;
	std::string requesting_bot_name;
	std::string message;
	int target_slot = -1;
	int upgrade_score = 0;
};

Request BuildRequestForSuccessfulLoot(const SuccessfulLootEvent &event, const Settings &settings);

} // namespace BotLootRequest

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

#include "common/bot_loot_request.h"
#include "common/emu_constants.h"
#include "common/item_data.h"
#include "common/rulesys.h"
#include "cppunit/cpptest.h"

#include <cstring>
#include <string>
#include <vector>

class BotLootRequestTest : public Test::Suite {
public:
	BotLootRequestTest()
	{
		TEST_ADD(BotLootRequestTest::DefaultRulesDisableBotLootRequests);
		TEST_ADD(BotLootRequestTest::DisabledSettingsPreventBotLootRequest);
		TEST_ADD(BotLootRequestTest::EnabledSuccessfulLootProducesDeterministicTemplateRequest);
		TEST_ADD(BotLootRequestTest::EnabledSuccessfulLootWithNoGroupedBotsProducesNoRequest);
		TEST_ADD(BotLootRequestTest::BotLootRequestDoesNotMutateBotInventorySnapshot);
	}

private:
	EQ::ItemData Gear(
		uint32_t id,
		const std::string &name,
		int slot,
		int ac,
		int hp = 0,
		int mana = 0,
		int endur = 0
	)
	{
		EQ::ItemData item{};
		item.ItemClass = EQ::item::ItemClassCommon;
		item.ID = id;
		std::strncpy(item.Name, name.c_str(), sizeof(item.Name) - 1);
		item.Slots = 1 << slot;
		item.ItemType = EQ::item::ItemTypeArmor;
		item.AC = ac;
		item.HP = hp;
		item.Mana = mana;
		item.Endur = endur;
		return item;
	}

	void DefaultRulesDisableBotLootRequests()
	{
		RuleManager::Instance()->ResetRules();

		TEST_ASSERT(!RuleB(Chat, BotLootRequestEnabled));
	}

	void DisabledSettingsPreventBotLootRequest()
	{
		const auto looted = Gear(1001, "Bronze Breastplate", EQ::invslot::slotChest, 12, 20);

		const auto result = BotLootRequest::BuildRequestForSuccessfulLoot(
			{
				.looter_name = "Aten",
				.looted_item = &looted,
				.looted_item_link = "[Bronze Breastplate]",
				.grouped_bots = {{
					.name_stable_id = 7,
					.name = "Atenbot",
					.equipped_items = {}
				}}
			},
			{.enabled = false}
		);

		TEST_ASSERT(!result.produced);
	}

	void EnabledSuccessfulLootProducesDeterministicTemplateRequest()
	{
		const auto old_chest = Gear(1001, "Cloth Shirt", EQ::invslot::slotChest, 1);
		const auto looted = Gear(1002, "Bronze Breastplate", EQ::invslot::slotChest, 12, 20);

		const auto result = BotLootRequest::BuildRequestForSuccessfulLoot(
			{
				.looter_name = "Aten",
				.looted_item = &looted,
				.looted_item_link = "[Bronze Breastplate]",
				.grouped_bots = {{
					.name_stable_id = 7,
					.name = "Atenbot",
					.equipped_items = {{.item = &old_chest}}
				}}
			},
			{.enabled = true}
		);

		TEST_ASSERT(result.produced);
		TEST_ASSERT_EQUALS(result.requesting_bot_stable_id, 7u);
		TEST_ASSERT_EQUALS(result.requesting_bot_name, std::string("Atenbot"));
		TEST_ASSERT_EQUALS(result.target_slot, EQ::invslot::slotChest);
		TEST_ASSERT(result.upgrade_score > 0);
		TEST_ASSERT_EQUALS(
			result.message,
			std::string("Aten, could I use [Bronze Breastplate]? It looks like an upgrade for my chest.")
		);
	}

	void EnabledSuccessfulLootWithNoGroupedBotsProducesNoRequest()
	{
		const auto looted = Gear(1002, "Bronze Breastplate", EQ::invslot::slotChest, 12, 20);

		const auto result = BotLootRequest::BuildRequestForSuccessfulLoot(
			{
				.looter_name = "Aten",
				.looted_item = &looted,
				.looted_item_link = "[Bronze Breastplate]",
				.grouped_bots = {}
			},
			{.enabled = true}
		);

		TEST_ASSERT(!result.produced);
	}

	void BotLootRequestDoesNotMutateBotInventorySnapshot()
	{
		const auto old_chest = Gear(1001, "Cloth Shirt", EQ::invslot::slotChest, 1);
		const auto looted = Gear(1002, "Bronze Breastplate", EQ::invslot::slotChest, 12, 20);
		std::vector<BotLootRequest::ItemSnapshot> equipped_items = {{.item = &old_chest}};

		const auto result = BotLootRequest::BuildRequestForSuccessfulLoot(
			{
				.looter_name = "Aten",
				.looted_item = &looted,
				.looted_item_link = "[Bronze Breastplate]",
				.grouped_bots = {{
					.name_stable_id = 7,
					.name = "Atenbot",
					.equipped_items = equipped_items
				}}
			},
			{.enabled = true}
		);

		TEST_ASSERT(result.produced);
		TEST_ASSERT_EQUALS(equipped_items.size(), 1u);
		TEST_ASSERT(equipped_items.front().item == &old_chest);
	}
};

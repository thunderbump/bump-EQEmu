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
#include "common/classes.h"
#include "common/emu_constants.h"
#include "common/item_data.h"
#include "common/races.h"
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
		TEST_ADD(BotLootRequestTest::BotMustMatchRaceClassAndSlotEligibility);
		TEST_ADD(BotLootRequestTest::V1IgnoresNonGearLoot);
		TEST_ADD(BotLootRequestTest::NoDropGearCanStillBeRequested);
		TEST_ADD(BotLootRequestTest::LoreConflictSuppressesRequest);
		TEST_ADD(BotLootRequestTest::MultiSlotGearUsesBestReplacementSlot);
		TEST_ADD(BotLootRequestTest::NonUpgradeDoesNotProduceRequest);
		TEST_ADD(BotLootRequestTest::HighestUpgradeScoreWinsAcrossEligibleBots);
		TEST_ADD(BotLootRequestTest::GroupOrderBreaksTiedUpgradeScores);
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
		item.Classes = GetPlayerClassBit(Class::Warrior);
		item.Races = GetPlayerRaceBit(Race::Human);
		item.NoDrop = 255;
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
					.race_id = Race::Human,
					.class_id = Class::Warrior,
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
					.race_id = Race::Human,
					.class_id = Class::Warrior,
					.equipped_items = {{.item = &old_chest, .slot_id = EQ::invslot::slotChest}}
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
		std::vector<BotLootRequest::ItemSnapshot> equipped_items = {{.item = &old_chest, .slot_id = EQ::invslot::slotChest}};

		const auto result = BotLootRequest::BuildRequestForSuccessfulLoot(
			{
				.looter_name = "Aten",
				.looted_item = &looted,
				.looted_item_link = "[Bronze Breastplate]",
				.grouped_bots = {{
					.name_stable_id = 7,
					.name = "Atenbot",
					.race_id = Race::Human,
					.class_id = Class::Warrior,
					.equipped_items = equipped_items
				}}
			},
			{.enabled = true}
		);

		TEST_ASSERT(result.produced);
		TEST_ASSERT_EQUALS(equipped_items.size(), 1u);
		TEST_ASSERT(equipped_items.front().item == &old_chest);
	}

	void BotMustMatchRaceClassAndSlotEligibility()
	{
		auto warrior_chest = Gear(1002, "Bronze Breastplate", EQ::invslot::slotChest, 12, 20);
		const auto old_chest = Gear(1001, "Cloth Shirt", EQ::invslot::slotChest, 1);

		const auto wrong_class = BotLootRequest::BuildRequestForSuccessfulLoot(
			{
				.looter_name = "Aten",
				.looted_item = &warrior_chest,
				.looted_item_link = "[Bronze Breastplate]",
				.grouped_bots = {{
					.name_stable_id = 8,
					.name = "Notawarrior",
					.race_id = Race::Human,
					.class_id = Class::Cleric,
					.equipped_items = {{.item = &old_chest, .slot_id = EQ::invslot::slotChest}}
				}}
			},
			{.enabled = true}
		);

		TEST_ASSERT(!wrong_class.produced);

		warrior_chest.Slots = 1u << EQ::invslot::slotLegs;
		const auto no_valid_replacement_slot = BotLootRequest::BuildRequestForSuccessfulLoot(
			{
				.looter_name = "Aten",
				.looted_item = &warrior_chest,
				.looted_item_link = "[Bronze Breastplate]",
				.grouped_bots = {{
					.name_stable_id = 7,
					.name = "Atenbot",
					.race_id = Race::Human,
					.class_id = Class::Warrior,
					.equipped_items = {{.item = &old_chest, .slot_id = EQ::invslot::slotChest}}
				}}
			},
			{.enabled = true}
		);

		TEST_ASSERT(no_valid_replacement_slot.produced);
		TEST_ASSERT_EQUALS(no_valid_replacement_slot.target_slot, EQ::invslot::slotLegs);
	}

	void V1IgnoresNonGearLoot()
	{
		auto spell_scroll = Gear(2001, "Scroll of Minor Healing", EQ::invslot::slotPrimary, 50);
		spell_scroll.ItemType = EQ::item::ItemTypeSpell;

		auto consumable = Gear(2005, "Potion of Vigor", EQ::invslot::slotPrimary, 50);
		consumable.ItemType = EQ::item::ItemTypePotion;

		auto tradeskill_item = Gear(2002, "Sheet Metal", EQ::invslot::slotChest, 50);
		tradeskill_item.ItemType = EQ::item::ItemTypeCombinable;

		auto bag_clicky = Gear(2003, "Dimensional Satchel", EQ::invslot::slotCharm, 50);
		bag_clicky.BagSlots = 8;
		bag_clicky.Click.Effect = 1;

		auto quest_item = Gear(2004, "Sealed Writ", EQ::invslot::slotFinger1, 50);
		quest_item.QuestItemFlag = true;

		const auto old_primary = Gear(1001, "Rusty Sword", EQ::invslot::slotPrimary, 1);
		const BotLootRequest::GroupedBotSnapshot bot{
			.name_stable_id = 7,
			.name = "Atenbot",
			.race_id = Race::Human,
			.class_id = Class::Warrior,
			.equipped_items = {{.item = &old_primary, .slot_id = EQ::invslot::slotPrimary}}
		};

		for (const auto *ignored_item : {&spell_scroll, &consumable, &tradeskill_item, &bag_clicky, &quest_item}) {
			const auto result = BotLootRequest::BuildRequestForSuccessfulLoot(
				{
					.looter_name = "Aten",
					.looted_item = ignored_item,
					.looted_item_link = "[Ignored Item]",
					.grouped_bots = {bot}
				},
				{.enabled = true}
			);

			TEST_ASSERT(!result.produced);
		}
	}

	void NoDropGearCanStillBeRequested()
	{
		const auto old_chest = Gear(1001, "Cloth Shirt", EQ::invslot::slotChest, 1);
		auto looted = Gear(1002, "No Drop Breastplate", EQ::invslot::slotChest, 12, 20);
		looted.NoDrop = 0;

		const auto result = BotLootRequest::BuildRequestForSuccessfulLoot(
			{
				.looter_name = "Aten",
				.looted_item = &looted,
				.looted_item_link = "[No Drop Breastplate]",
				.grouped_bots = {{
					.name_stable_id = 7,
					.name = "Atenbot",
					.race_id = Race::Human,
					.class_id = Class::Warrior,
					.equipped_items = {{.item = &old_chest, .slot_id = EQ::invslot::slotChest}}
				}}
			},
			{.enabled = true}
		);

		TEST_ASSERT(result.produced);
	}

	void LoreConflictSuppressesRequest()
	{
		auto worn_lore = Gear(3001, "Band of Office", EQ::invslot::slotFinger1, 1);
		worn_lore.LoreFlag = true;
		worn_lore.LoreGroup = 77;

		auto looted = Gear(3002, "Band of Rank", EQ::invslot::slotFinger2, 20);
		looted.LoreFlag = true;
		looted.LoreGroup = 77;

		const auto result = BotLootRequest::BuildRequestForSuccessfulLoot(
			{
				.looter_name = "Aten",
				.looted_item = &looted,
				.looted_item_link = "[Band of Rank]",
				.grouped_bots = {{
					.name_stable_id = 7,
					.name = "Atenbot",
					.race_id = Race::Human,
					.class_id = Class::Warrior,
					.equipped_items = {{.item = &worn_lore, .slot_id = EQ::invslot::slotFinger1}}
				}}
			},
			{.enabled = true}
		);

		TEST_ASSERT(!result.produced);
	}

	void MultiSlotGearUsesBestReplacementSlot()
	{
		const auto strong_ring = Gear(4001, "Velium Ring", EQ::invslot::slotFinger1, 100);
		const auto weak_ring = Gear(4002, "Copper Ring", EQ::invslot::slotFinger2, 1);
		auto looted = Gear(4003, "Silver Ring", EQ::invslot::slotFinger1, 10);
		looted.Slots = (1u << EQ::invslot::slotFinger1) | (1u << EQ::invslot::slotFinger2);

		const auto result = BotLootRequest::BuildRequestForSuccessfulLoot(
			{
				.looter_name = "Aten",
				.looted_item = &looted,
				.looted_item_link = "[Silver Ring]",
				.grouped_bots = {{
					.name_stable_id = 7,
					.name = "Atenbot",
					.race_id = Race::Human,
					.class_id = Class::Warrior,
					.equipped_items = {
						{.item = &strong_ring, .slot_id = EQ::invslot::slotFinger1},
						{.item = &weak_ring, .slot_id = EQ::invslot::slotFinger2}
					}
				}}
			},
			{.enabled = true}
		);

		TEST_ASSERT(result.produced);
		TEST_ASSERT_EQUALS(result.target_slot, EQ::invslot::slotFinger2);
		TEST_ASSERT_EQUALS(result.upgrade_score, 9);
	}

	void NonUpgradeDoesNotProduceRequest()
	{
		const auto strong_chest = Gear(5001, "Fine Breastplate", EQ::invslot::slotChest, 20);
		const auto looted = Gear(5002, "Worn Breastplate", EQ::invslot::slotChest, 20);

		const auto result = BotLootRequest::BuildRequestForSuccessfulLoot(
			{
				.looter_name = "Aten",
				.looted_item = &looted,
				.looted_item_link = "[Worn Breastplate]",
				.grouped_bots = {{
					.name_stable_id = 7,
					.name = "Atenbot",
					.race_id = Race::Human,
					.class_id = Class::Warrior,
					.equipped_items = {{.item = &strong_chest, .slot_id = EQ::invslot::slotChest}}
				}}
			},
			{.enabled = true}
		);

		TEST_ASSERT(!result.produced);
	}

	void HighestUpgradeScoreWinsAcrossEligibleBots()
	{
		const auto modest_chest = Gear(6001, "Banded Mail", EQ::invslot::slotChest, 15);
		const auto poor_chest = Gear(6002, "Cloth Shirt", EQ::invslot::slotChest, 1);
		const auto looted = Gear(6003, "Bronze Breastplate", EQ::invslot::slotChest, 30);

		const auto result = BotLootRequest::BuildRequestForSuccessfulLoot(
			{
				.looter_name = "Aten",
				.looted_item = &looted,
				.looted_item_link = "[Bronze Breastplate]",
				.grouped_bots = {
					{
						.name_stable_id = 7,
						.name = "Firstbot",
						.race_id = Race::Human,
						.class_id = Class::Warrior,
						.equipped_items = {{.item = &modest_chest, .slot_id = EQ::invslot::slotChest}}
					},
					{
						.name_stable_id = 8,
						.name = "Secondbot",
						.race_id = Race::Human,
						.class_id = Class::Warrior,
						.equipped_items = {{.item = &poor_chest, .slot_id = EQ::invslot::slotChest}}
					}
				}
			},
			{.enabled = true}
		);

		TEST_ASSERT(result.produced);
		TEST_ASSERT_EQUALS(result.requesting_bot_stable_id, 8u);
		TEST_ASSERT_EQUALS(result.upgrade_score, 29);
	}

	void GroupOrderBreaksTiedUpgradeScores()
	{
		const auto first_chest = Gear(7001, "First Tunic", EQ::invslot::slotChest, 10);
		const auto second_chest = Gear(7002, "Second Tunic", EQ::invslot::slotChest, 10);
		const auto looted = Gear(7003, "Bronze Breastplate", EQ::invslot::slotChest, 30);

		const auto result = BotLootRequest::BuildRequestForSuccessfulLoot(
			{
				.looter_name = "Aten",
				.looted_item = &looted,
				.looted_item_link = "[Bronze Breastplate]",
				.grouped_bots = {
					{
						.name_stable_id = 7,
						.name = "Firstbot",
						.race_id = Race::Human,
						.class_id = Class::Warrior,
						.equipped_items = {{.item = &first_chest, .slot_id = EQ::invslot::slotChest}}
					},
					{
						.name_stable_id = 8,
						.name = "Secondbot",
						.race_id = Race::Human,
						.class_id = Class::Warrior,
						.equipped_items = {{.item = &second_chest, .slot_id = EQ::invslot::slotChest}}
					}
				}
			},
			{.enabled = true}
		);

		TEST_ASSERT(result.produced);
		TEST_ASSERT_EQUALS(result.requesting_bot_stable_id, 7u);
		TEST_ASSERT_EQUALS(result.upgrade_score, 20);
	}
};

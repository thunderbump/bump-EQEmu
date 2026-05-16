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
#include "common/item_instance.h"
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
		TEST_ADD(BotLootRequestTest::AugmentedLootedItemValueCanProduceRequest);
		TEST_ADD(BotLootRequestTest::RequiredLevelAboveBotSuppressesRequest);
		TEST_ADD(BotLootRequestTest::RecommendedLevelScalesLootedItemValue);
		TEST_ADD(BotLootRequestTest::RecommendedLevelScalesEquippedItemValue);
		TEST_ADD(BotLootRequestTest::NonUpgradeDoesNotProduceRequest);
		TEST_ADD(BotLootRequestTest::AugmentedEquippedNonUpgradeDoesNotProduceRequest);
		TEST_ADD(BotLootRequestTest::WisdomGearChangesWinnerOnlyForWisdomBotGearRoles);
		TEST_ADD(BotLootRequestTest::SurvivabilityGearChangesWinnerForDurableBotGearRoles);
		TEST_ADD(BotLootRequestTest::IntelligenceGearSplitsShadowKnightFromOtherTankBotGearRoles);
		TEST_ADD(BotLootRequestTest::ResourceGearFollowsManaAndEnduranceBotGearRoles);
		TEST_ADD(BotLootRequestTest::SpellDamageGearChangesWinnerForCasterBotGearRoles);
		TEST_ADD(BotLootRequestTest::HealingGearChangesWinnerForPriestBotGearRoles);
		TEST_ADD(BotLootRequestTest::CharismaGearChangesWinnerForBardBotGearRole);
		TEST_ADD(BotLootRequestTest::MeleeOffenseGearChangesWinnerForBaselineMeleeBotGearRoles);
		TEST_ADD(BotLootRequestTest::WeaponDamageDelayRatioChangesWinner);
		TEST_ADD(BotLootRequestTest::OneHandWeaponCanWinSecondaryWithoutPrimaryUpgrade);
		TEST_ADD(BotLootRequestTest::SecondaryDamageWeaponRequiresDualWield);
		TEST_ADD(BotLootRequestTest::SecondaryWeaponPaysShieldReplacementCost);
		TEST_ADD(BotLootRequestTest::TwoHanderMustBeatCurrentPrimaryAndSecondaryPackage);
		TEST_ADD(BotLootRequestTest::RogueBackstabValueCanChangeWeaponWinner);
		TEST_ADD(BotLootRequestTest::MartialWeaponDamageDelayCanProduceMonkUpgrade);
		TEST_ADD(BotLootRequestTest::ValidWeaponProcSignalCanProduceSmallUpgrade);
		TEST_ADD(BotLootRequestTest::HighestUpgradeScoreWinsAcrossEligibleBots);
		TEST_ADD(BotLootRequestTest::GroupOrderBreaksTiedUpgradeScores);
		TEST_ADD(BotLootRequestTest::CooldownSuppressesSameLooterAndRequestingBot);
		TEST_ADD(BotLootRequestTest::CooldownDoesNotSuppressDifferentRequestingBot);
		TEST_ADD(BotLootRequestTest::CooldownDoesNotSuppressDifferentLooter);
		TEST_ADD(BotLootRequestTest::SingleLootEventEmitsOneVisibleRequest);
		TEST_ADD(BotLootRequestTest::VisibleRequestIsGroupChatTemplateWithServerItemLink);
		TEST_ADD(BotLootRequestTest::AsyncDialogueQueuesWithoutVisibleMessage);
		TEST_ADD(BotLootRequestTest::LootRequestDialoguePromptUsesOnlyCompactIntent);
		TEST_ADD(BotLootRequestTest::StaleGroupSuppressesDelayedLootRequestDialogue);
		TEST_ADD(BotLootRequestTest::AcceptedGeneratedLootRequestDialogueUsesServerItemLink);
		TEST_ADD(BotLootRequestTest::GeneratedLootRequestThatNamesLooterFallsBackToTemplate);
		TEST_ADD(BotLootRequestTest::FailedGeneratedLootRequestDialogueFallsBackToTemplate);
		TEST_ADD(BotLootRequestTest::UnsafeGeneratedLootRequestDialogueFallsBackToTemplate);
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

	EQ::ItemData AllClassGear(
		uint32_t id,
		const std::string &name,
		int slot,
		int ac = 0,
		int hp = 0,
		int mana = 0,
		int endur = 0
	)
	{
		auto item = Gear(id, name, slot, ac, hp, mana, endur);
		item.Classes = Class::ALL_CLASSES_BITMASK;
		return item;
	}

	EQ::ItemData Weapon(
		uint32_t id,
		const std::string &name,
		int slot,
		uint8 item_type,
		uint32 damage,
		uint8 delay,
		uint32 classes = Class::ALL_CLASSES_BITMASK
	)
	{
		auto item = AllClassGear(id, name, slot);
		item.ItemType = item_type;
		item.Damage = damage;
		item.Delay = delay;
		item.Classes = classes;
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
		TEST_ASSERT(result.delivery_channel == BotLootRequest::DeliveryChannel::GroupChat);
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

	void AugmentedLootedItemValueCanProduceRequest()
	{
		const auto equipped = Gear(4101, "Polished Ring", EQ::invslot::slotFinger1, 12);
		const auto looted = Gear(4102, "Socketed Ring", EQ::invslot::slotFinger1, 10);
		auto augment = Gear(4103, "Sturdy Gem", EQ::invslot::slotCharm, 5);
		augment.AugType = 1;

		EQ::ItemInstance looted_instance(&looted);
		looted_instance.PutAugment(EQ::invaug::SOCKET_BEGIN, EQ::ItemInstance(&augment));

		const auto result = BotLootRequest::BuildRequestForSuccessfulLoot(
			{
				.looter_name = "Aten",
				.looted_item = &looted,
				.looted_item_instance = &looted_instance,
				.looted_item_link = "[Socketed Ring]",
				.grouped_bots = {{
					.name_stable_id = 7,
					.name = "Atenbot",
					.race_id = Race::Human,
					.class_id = Class::Warrior,
					.equipped_items = {{.item = &equipped, .slot_id = EQ::invslot::slotFinger1}}
				}}
			},
			{.enabled = true}
		);

		TEST_ASSERT(result.produced);
		TEST_ASSERT_EQUALS(result.upgrade_score, 3);
	}

	void RequiredLevelAboveBotSuppressesRequest()
	{
		const auto equipped = Gear(4201, "Cloth Shirt", EQ::invslot::slotChest, 1);
		auto looted = Gear(4202, "Veteran Breastplate", EQ::invslot::slotChest, 100);
		looted.ReqLevel = 51;
		EQ::ItemInstance looted_instance(&looted);

		const auto result = BotLootRequest::BuildRequestForSuccessfulLoot(
			{
				.looter_name = "Aten",
				.looted_item = &looted,
				.looted_item_instance = &looted_instance,
				.looted_item_link = "[Veteran Breastplate]",
				.grouped_bots = {{
					.name_stable_id = 7,
					.name = "Atenbot",
					.race_id = Race::Human,
					.class_id = Class::Warrior,
					.level = 50,
					.equipped_items = {{.item = &equipped, .slot_id = EQ::invslot::slotChest}}
				}}
			},
			{.enabled = true}
		);

		TEST_ASSERT(!result.produced);
	}

	void RecommendedLevelScalesLootedItemValue()
	{
		const auto equipped = Gear(4301, "Banded Mail", EQ::invslot::slotChest, 40);
		auto looted = Gear(4302, "Aspirant Breastplate", EQ::invslot::slotChest, 100);
		looted.RecLevel = 100;
		EQ::ItemInstance looted_instance(&looted);

		const auto result = BotLootRequest::BuildRequestForSuccessfulLoot(
			{
				.looter_name = "Aten",
				.looted_item = &looted,
				.looted_item_instance = &looted_instance,
				.looted_item_link = "[Aspirant Breastplate]",
				.grouped_bots = {{
					.name_stable_id = 7,
					.name = "Atenbot",
					.race_id = Race::Human,
					.class_id = Class::Warrior,
					.level = 50,
					.equipped_items = {{.item = &equipped, .slot_id = EQ::invslot::slotChest}}
				}}
			},
			{.enabled = true}
		);

		TEST_ASSERT(result.produced);
		TEST_ASSERT_EQUALS(result.upgrade_score, 10);
	}

	void RecommendedLevelScalesEquippedItemValue()
	{
		auto equipped = Gear(4401, "Aspirant Breastplate", EQ::invslot::slotChest, 100);
		equipped.RecLevel = 100;
		const auto looted = Gear(4402, "Veteran Breastplate", EQ::invslot::slotChest, 60);
		EQ::ItemInstance equipped_instance(&equipped);

		const auto result = BotLootRequest::BuildRequestForSuccessfulLoot(
			{
				.looter_name = "Aten",
				.looted_item = &looted,
				.looted_item_link = "[Veteran Breastplate]",
				.grouped_bots = {{
					.name_stable_id = 7,
					.name = "Atenbot",
					.race_id = Race::Human,
					.class_id = Class::Warrior,
					.level = 50,
					.equipped_items = {{
						.item = &equipped,
						.item_instance = &equipped_instance,
						.slot_id = EQ::invslot::slotChest
					}}
				}}
			},
			{.enabled = true}
		);

		TEST_ASSERT(result.produced);
		TEST_ASSERT_EQUALS(result.upgrade_score, 10);
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

	void AugmentedEquippedNonUpgradeDoesNotProduceRequest()
	{
		const auto equipped = Gear(5101, "Socketed Breastplate", EQ::invslot::slotChest, 10);
		auto augment = Gear(5102, "Sturdy Gem", EQ::invslot::slotCharm, 10);
		augment.AugType = 1;
		const auto looted = Gear(5103, "Plain Breastplate", EQ::invslot::slotChest, 15);
		EQ::ItemInstance equipped_instance(&equipped);
		equipped_instance.PutAugment(EQ::invaug::SOCKET_BEGIN, EQ::ItemInstance(&augment));

		const auto result = BotLootRequest::BuildRequestForSuccessfulLoot(
			{
				.looter_name = "Aten",
				.looted_item = &looted,
				.looted_item_link = "[Plain Breastplate]",
				.grouped_bots = {{
					.name_stable_id = 7,
					.name = "Atenbot",
					.race_id = Race::Human,
					.class_id = Class::Warrior,
					.equipped_items = {{
						.item = &equipped,
						.item_instance = &equipped_instance,
						.slot_id = EQ::invslot::slotChest
					}}
				}}
			},
			{.enabled = true}
		);

		TEST_ASSERT(!result.produced);
	}

	void WisdomGearChangesWinnerOnlyForWisdomBotGearRoles()
	{
		const auto old_warrior_ring = AllClassGear(5201, "Warrior Copper Ring", EQ::invslot::slotFinger1);
		const auto old_cleric_ring = AllClassGear(5202, "Cleric Copper Ring", EQ::invslot::slotFinger1);
		auto looted = AllClassGear(5203, "Sage Ring", EQ::invslot::slotFinger1);
		looted.AWis = 12;

		const auto result = BotLootRequest::BuildRequestForSuccessfulLoot(
			{
				.looter_name = "Aten",
				.looted_item = &looted,
				.looted_item_link = "[Sage Ring]",
				.grouped_bots = {
					{
						.name_stable_id = 7,
						.name = "Warriorbot",
						.race_id = Race::Human,
						.class_id = Class::Warrior,
						.equipped_items = {{.item = &old_warrior_ring, .slot_id = EQ::invslot::slotFinger1}}
					},
					{
						.name_stable_id = 8,
						.name = "Clericbot",
						.race_id = Race::Human,
						.class_id = Class::Cleric,
						.equipped_items = {{.item = &old_cleric_ring, .slot_id = EQ::invslot::slotFinger1}}
					}
				}
			},
			{.enabled = true}
		);

		TEST_ASSERT(result.produced);
		TEST_ASSERT_EQUALS(result.requesting_bot_stable_id, 8u);
	}

	void SurvivabilityGearChangesWinnerForDurableBotGearRoles()
	{
		const auto old_wizard_ring = AllClassGear(5301, "Wizard Copper Ring", EQ::invslot::slotFinger1);
		const auto old_warrior_ring = AllClassGear(5302, "Warrior Copper Ring", EQ::invslot::slotFinger1);
		auto looted = AllClassGear(5303, "Bulwark Ring", EQ::invslot::slotFinger1);
		looted.AC = 20;
		looted.HP = 40;
		looted.ASta = 10;

		const auto result = BotLootRequest::BuildRequestForSuccessfulLoot(
			{
				.looter_name = "Aten",
				.looted_item = &looted,
				.looted_item_link = "[Bulwark Ring]",
				.grouped_bots = {
					{
						.name_stable_id = 7,
						.name = "Wizardbot",
						.race_id = Race::Human,
						.class_id = Class::Wizard,
						.equipped_items = {{.item = &old_wizard_ring, .slot_id = EQ::invslot::slotFinger1}}
					},
					{
						.name_stable_id = 8,
						.name = "Warriorbot",
						.race_id = Race::Human,
						.class_id = Class::Warrior,
						.equipped_items = {{.item = &old_warrior_ring, .slot_id = EQ::invslot::slotFinger1}}
					}
				}
			},
			{.enabled = true}
		);

		TEST_ASSERT(result.produced);
		TEST_ASSERT_EQUALS(result.requesting_bot_stable_id, 8u);
	}

	void IntelligenceGearSplitsShadowKnightFromOtherTankBotGearRoles()
	{
		const auto old_warrior_ring = AllClassGear(5401, "Warrior Copper Ring", EQ::invslot::slotFinger1);
		const auto old_paladin_ring = AllClassGear(5402, "Paladin Copper Ring", EQ::invslot::slotFinger1);
		const auto old_shadow_knight_ring = AllClassGear(5403, "Shadow Knight Copper Ring", EQ::invslot::slotFinger1);
		auto looted = AllClassGear(5404, "Ebon Thought Ring", EQ::invslot::slotFinger1);
		looted.AInt = 12;

		const auto result = BotLootRequest::BuildRequestForSuccessfulLoot(
			{
				.looter_name = "Aten",
				.looted_item = &looted,
				.looted_item_link = "[Ebon Thought Ring]",
				.grouped_bots = {
					{
						.name_stable_id = 7,
						.name = "Warriorbot",
						.race_id = Race::Human,
						.class_id = Class::Warrior,
						.equipped_items = {{.item = &old_warrior_ring, .slot_id = EQ::invslot::slotFinger1}}
					},
					{
						.name_stable_id = 8,
						.name = "Paladinbot",
						.race_id = Race::Human,
						.class_id = Class::Paladin,
						.equipped_items = {{.item = &old_paladin_ring, .slot_id = EQ::invslot::slotFinger1}}
					},
					{
						.name_stable_id = 9,
						.name = "Shadowknightbot",
						.race_id = Race::Human,
						.class_id = Class::ShadowKnight,
						.equipped_items = {{.item = &old_shadow_knight_ring, .slot_id = EQ::invslot::slotFinger1}}
					}
				}
			},
			{.enabled = true}
		);

		TEST_ASSERT(result.produced);
		TEST_ASSERT_EQUALS(result.requesting_bot_stable_id, 9u);
	}

	void ResourceGearFollowsManaAndEnduranceBotGearRoles()
	{
		const auto old_bard_ring = AllClassGear(5501, "Bard Copper Ring", EQ::invslot::slotFinger1);
		const auto old_wizard_ring = AllClassGear(5502, "Wizard Copper Ring", EQ::invslot::slotFinger1);
		auto mana_ring = AllClassGear(5503, "Azure Mind Ring", EQ::invslot::slotFinger1);
		mana_ring.Mana = 20;

		const auto mana_result = BotLootRequest::BuildRequestForSuccessfulLoot(
			{
				.looter_name = "Aten",
				.looted_item = &mana_ring,
				.looted_item_link = "[Azure Mind Ring]",
				.grouped_bots = {
					{
						.name_stable_id = 7,
						.name = "Bardbot",
						.race_id = Race::Human,
						.class_id = Class::Bard,
						.equipped_items = {{.item = &old_bard_ring, .slot_id = EQ::invslot::slotFinger1}}
					},
					{
						.name_stable_id = 8,
						.name = "Wizardbot",
						.race_id = Race::Human,
						.class_id = Class::Wizard,
						.equipped_items = {{.item = &old_wizard_ring, .slot_id = EQ::invslot::slotFinger1}}
					}
				}
			},
			{.enabled = true}
		);

		TEST_ASSERT(mana_result.produced);
		TEST_ASSERT_EQUALS(mana_result.requesting_bot_stable_id, 8u);

		const auto old_second_wizard_ring = AllClassGear(5504, "Second Wizard Copper Ring", EQ::invslot::slotFinger1);
		const auto old_warrior_ring = AllClassGear(5505, "Warrior Copper Ring", EQ::invslot::slotFinger1);
		auto endurance_ring = AllClassGear(5506, "Stalwart Ring", EQ::invslot::slotFinger1);
		endurance_ring.Endur = 20;

		const auto endurance_result = BotLootRequest::BuildRequestForSuccessfulLoot(
			{
				.looter_name = "Aten",
				.looted_item = &endurance_ring,
				.looted_item_link = "[Stalwart Ring]",
				.grouped_bots = {
					{
						.name_stable_id = 9,
						.name = "Wizardbot",
						.race_id = Race::Human,
						.class_id = Class::Wizard,
						.equipped_items = {{.item = &old_second_wizard_ring, .slot_id = EQ::invslot::slotFinger1}}
					},
					{
						.name_stable_id = 10,
						.name = "Warriorbot",
						.race_id = Race::Human,
						.class_id = Class::Warrior,
						.equipped_items = {{.item = &old_warrior_ring, .slot_id = EQ::invslot::slotFinger1}}
					}
				}
			},
			{.enabled = true}
		);

		TEST_ASSERT(endurance_result.produced);
		TEST_ASSERT_EQUALS(endurance_result.requesting_bot_stable_id, 10u);
	}

	void SpellDamageGearChangesWinnerForCasterBotGearRoles()
	{
		const auto old_warrior_ring = AllClassGear(5601, "Warrior Copper Ring", EQ::invslot::slotFinger1);
		const auto old_wizard_ring = AllClassGear(5602, "Wizard Copper Ring", EQ::invslot::slotFinger1);
		auto looted = AllClassGear(5603, "Evoker Ring", EQ::invslot::slotFinger1);
		looted.SpellDmg = 8;

		const auto result = BotLootRequest::BuildRequestForSuccessfulLoot(
			{
				.looter_name = "Aten",
				.looted_item = &looted,
				.looted_item_link = "[Evoker Ring]",
				.grouped_bots = {
					{
						.name_stable_id = 7,
						.name = "Warriorbot",
						.race_id = Race::Human,
						.class_id = Class::Warrior,
						.equipped_items = {{.item = &old_warrior_ring, .slot_id = EQ::invslot::slotFinger1}}
					},
					{
						.name_stable_id = 8,
						.name = "Wizardbot",
						.race_id = Race::Human,
						.class_id = Class::Wizard,
						.equipped_items = {{.item = &old_wizard_ring, .slot_id = EQ::invslot::slotFinger1}}
					}
				}
			},
			{.enabled = true}
		);

		TEST_ASSERT(result.produced);
		TEST_ASSERT_EQUALS(result.requesting_bot_stable_id, 8u);
	}

	void HealingGearChangesWinnerForPriestBotGearRoles()
	{
		const auto old_wizard_ring = AllClassGear(5701, "Wizard Copper Ring", EQ::invslot::slotFinger1);
		const auto old_cleric_ring = AllClassGear(5702, "Cleric Copper Ring", EQ::invslot::slotFinger1);
		auto looted = AllClassGear(5703, "Mercy Ring", EQ::invslot::slotFinger1);
		looted.HealAmt = 8;

		const auto result = BotLootRequest::BuildRequestForSuccessfulLoot(
			{
				.looter_name = "Aten",
				.looted_item = &looted,
				.looted_item_link = "[Mercy Ring]",
				.grouped_bots = {
					{
						.name_stable_id = 7,
						.name = "Wizardbot",
						.race_id = Race::Human,
						.class_id = Class::Wizard,
						.equipped_items = {{.item = &old_wizard_ring, .slot_id = EQ::invslot::slotFinger1}}
					},
					{
						.name_stable_id = 8,
						.name = "Clericbot",
						.race_id = Race::Human,
						.class_id = Class::Cleric,
						.equipped_items = {{.item = &old_cleric_ring, .slot_id = EQ::invslot::slotFinger1}}
					}
				}
			},
			{.enabled = true}
		);

		TEST_ASSERT(result.produced);
		TEST_ASSERT_EQUALS(result.requesting_bot_stable_id, 8u);
	}

	void CharismaGearChangesWinnerForBardBotGearRole()
	{
		const auto old_warrior_ring = AllClassGear(5801, "Warrior Copper Ring", EQ::invslot::slotFinger1);
		const auto old_bard_ring = AllClassGear(5802, "Bard Copper Ring", EQ::invslot::slotFinger1);
		auto looted = AllClassGear(5803, "Performer Ring", EQ::invslot::slotFinger1);
		looted.ACha = 12;

		const auto result = BotLootRequest::BuildRequestForSuccessfulLoot(
			{
				.looter_name = "Aten",
				.looted_item = &looted,
				.looted_item_link = "[Performer Ring]",
				.grouped_bots = {
					{
						.name_stable_id = 7,
						.name = "Warriorbot",
						.race_id = Race::Human,
						.class_id = Class::Warrior,
						.equipped_items = {{.item = &old_warrior_ring, .slot_id = EQ::invslot::slotFinger1}}
					},
					{
						.name_stable_id = 8,
						.name = "Bardbot",
						.race_id = Race::Human,
						.class_id = Class::Bard,
						.equipped_items = {{.item = &old_bard_ring, .slot_id = EQ::invslot::slotFinger1}}
					}
				}
			},
			{.enabled = true}
		);

		TEST_ASSERT(result.produced);
		TEST_ASSERT_EQUALS(result.requesting_bot_stable_id, 8u);
	}

	void MeleeOffenseGearChangesWinnerForBaselineMeleeBotGearRoles()
	{
		const auto old_wizard_ring = AllClassGear(5901, "Wizard Copper Ring", EQ::invslot::slotFinger1);
		const auto old_rogue_ring = AllClassGear(5902, "Rogue Copper Ring", EQ::invslot::slotFinger1);
		auto looted = AllClassGear(5903, "Striker Ring", EQ::invslot::slotFinger1);
		looted.AStr = 8;
		looted.Attack = 10;
		looted.Haste = 5;

		const auto result = BotLootRequest::BuildRequestForSuccessfulLoot(
			{
				.looter_name = "Aten",
				.looted_item = &looted,
				.looted_item_link = "[Striker Ring]",
				.grouped_bots = {
					{
						.name_stable_id = 7,
						.name = "Wizardbot",
						.race_id = Race::Human,
						.class_id = Class::Wizard,
						.equipped_items = {{.item = &old_wizard_ring, .slot_id = EQ::invslot::slotFinger1}}
					},
					{
						.name_stable_id = 8,
						.name = "Roguebot",
						.race_id = Race::Human,
						.class_id = Class::Rogue,
						.equipped_items = {{.item = &old_rogue_ring, .slot_id = EQ::invslot::slotFinger1}}
					}
				}
			},
			{.enabled = true}
		);

		TEST_ASSERT(result.produced);
		TEST_ASSERT_EQUALS(result.requesting_bot_stable_id, 8u);
	}

	void WeaponDamageDelayRatioChangesWinner()
	{
		const auto slow_heavy_sword = Weapon(
			5951,
			"Slow Heavy Sword",
			EQ::invslot::slotPrimary,
			EQ::item::ItemType1HSlash,
			24,
			48
		);
		const auto rusty_sword = Weapon(
			5952,
			"Rusty Sword",
			EQ::invslot::slotPrimary,
			EQ::item::ItemType1HSlash,
			8,
			40
		);
		const auto quick_sword = Weapon(
			5953,
			"Quick Sword",
			EQ::invslot::slotPrimary,
			EQ::item::ItemType1HSlash,
			16,
			20
		);

		const auto result = BotLootRequest::BuildRequestForSuccessfulLoot(
			{
				.looter_name = "Aten",
				.looted_item = &quick_sword,
				.looted_item_link = "[Quick Sword]",
				.grouped_bots = {
					{
						.name_stable_id = 7,
						.name = "Heavybot",
						.race_id = Race::Human,
						.class_id = Class::Warrior,
						.equipped_items = {{.item = &slow_heavy_sword, .slot_id = EQ::invslot::slotPrimary}}
					},
					{
						.name_stable_id = 8,
						.name = "Rustybot",
						.race_id = Race::Human,
						.class_id = Class::Warrior,
						.equipped_items = {{.item = &rusty_sword, .slot_id = EQ::invslot::slotPrimary}}
					}
				}
			},
			{.enabled = true}
		);

		TEST_ASSERT(result.produced);
		TEST_ASSERT_EQUALS(result.requesting_bot_stable_id, 8u);
		TEST_ASSERT_EQUALS(result.target_slot, EQ::invslot::slotPrimary);
	}

	void OneHandWeaponCanWinSecondaryWithoutPrimaryUpgrade()
	{
		const auto strong_primary = Weapon(
			5954,
			"Strong Primary",
			EQ::invslot::slotPrimary,
			EQ::item::ItemType1HSlash,
			30,
			30
		);
		auto sidearm = Weapon(
			5955,
			"Sidearm",
			EQ::invslot::slotPrimary,
			EQ::item::ItemType1HSlash,
			18,
			30
		);
		sidearm.Slots = (1u << EQ::invslot::slotPrimary) | (1u << EQ::invslot::slotSecondary);

		const auto result = BotLootRequest::BuildRequestForSuccessfulLoot(
			{
				.looter_name = "Aten",
				.looted_item = &sidearm,
				.looted_item_link = "[Sidearm]",
				.grouped_bots = {{
					.name_stable_id = 7,
					.name = "Warriorbot",
					.race_id = Race::Human,
					.class_id = Class::Warrior,
					.equipped_items = {{.item = &strong_primary, .slot_id = EQ::invslot::slotPrimary}}
				}}
			},
			{.enabled = true}
		);

		TEST_ASSERT(result.produced);
		TEST_ASSERT_EQUALS(result.target_slot, EQ::invslot::slotSecondary);
	}

	void SecondaryDamageWeaponRequiresDualWield()
	{
		const auto old_symbol = AllClassGear(5961, "Dented Symbol", EQ::invslot::slotSecondary);
		const auto offhand_sword = Weapon(
			5962,
			"Offhand Sword",
			EQ::invslot::slotSecondary,
			EQ::item::ItemType1HSlash,
			18,
			30
		);

		const auto result = BotLootRequest::BuildRequestForSuccessfulLoot(
			{
				.looter_name = "Aten",
				.looted_item = &offhand_sword,
				.looted_item_link = "[Offhand Sword]",
				.grouped_bots = {{
					.name_stable_id = 7,
					.name = "Paladinbot",
					.race_id = Race::Human,
					.class_id = Class::Paladin,
					.equipped_items = {{.item = &old_symbol, .slot_id = EQ::invslot::slotSecondary}}
				}}
			},
			{.enabled = true}
		);

		TEST_ASSERT(!result.produced);
	}

	void SecondaryWeaponPaysShieldReplacementCost()
	{
		auto sturdy_shield = AllClassGear(5963, "Sturdy Shield", EQ::invslot::slotSecondary);
		sturdy_shield.ItemType = EQ::item::ItemTypeShield;
		sturdy_shield.AC = 90;
		const auto offhand_sword = Weapon(
			5964,
			"Offhand Sword",
			EQ::invslot::slotSecondary,
			EQ::item::ItemType1HSlash,
			18,
			30
		);

		const auto result = BotLootRequest::BuildRequestForSuccessfulLoot(
			{
				.looter_name = "Aten",
				.looted_item = &offhand_sword,
				.looted_item_link = "[Offhand Sword]",
				.grouped_bots = {{
					.name_stable_id = 7,
					.name = "Warriorbot",
					.race_id = Race::Human,
					.class_id = Class::Warrior,
					.equipped_items = {{.item = &sturdy_shield, .slot_id = EQ::invslot::slotSecondary}}
				}}
			},
			{.enabled = true}
		);

		TEST_ASSERT(!result.produced);
	}

	void TwoHanderMustBeatCurrentPrimaryAndSecondaryPackage()
	{
		const auto primary_sword = Weapon(
			5971,
			"Main Sword",
			EQ::invslot::slotPrimary,
			EQ::item::ItemType1HSlash,
			18,
			30
		);
		const auto offhand_sword = Weapon(
			5972,
			"Offhand Sword",
			EQ::invslot::slotSecondary,
			EQ::item::ItemType1HSlash,
			18,
			30
		);
		const auto greatsword = Weapon(
			5973,
			"Greatsword",
			EQ::invslot::slotPrimary,
			EQ::item::ItemType2HSlash,
			30,
			30
		);

		const auto result = BotLootRequest::BuildRequestForSuccessfulLoot(
			{
				.looter_name = "Aten",
				.looted_item = &greatsword,
				.looted_item_link = "[Greatsword]",
				.grouped_bots = {{
					.name_stable_id = 7,
					.name = "Warriorbot",
					.race_id = Race::Human,
					.class_id = Class::Warrior,
					.equipped_items = {
						{.item = &primary_sword, .slot_id = EQ::invslot::slotPrimary},
						{.item = &offhand_sword, .slot_id = EQ::invslot::slotSecondary}
					}
				}}
			},
			{.enabled = true}
		);

		TEST_ASSERT(!result.produced);
	}

	void RogueBackstabValueCanChangeWeaponWinner()
	{
		const auto warrior_piercer = Weapon(
			5981,
			"Warrior Piercer",
			EQ::invslot::slotPrimary,
			EQ::item::ItemType1HPiercing,
			15,
			30
		);
		const auto rogue_piercer = Weapon(
			5982,
			"Rogue Piercer",
			EQ::invslot::slotPrimary,
			EQ::item::ItemType1HPiercing,
			15,
			30
		);
		auto backstab_piercer = Weapon(
			5983,
			"Backstab Piercer",
			EQ::invslot::slotPrimary,
			EQ::item::ItemType1HPiercing,
			15,
			30
		);
		backstab_piercer.BackstabDmg = 12;

		const auto result = BotLootRequest::BuildRequestForSuccessfulLoot(
			{
				.looter_name = "Aten",
				.looted_item = &backstab_piercer,
				.looted_item_link = "[Backstab Piercer]",
				.grouped_bots = {
					{
						.name_stable_id = 7,
						.name = "Warriorbot",
						.race_id = Race::Human,
						.class_id = Class::Warrior,
						.equipped_items = {{.item = &warrior_piercer, .slot_id = EQ::invslot::slotPrimary}}
					},
					{
						.name_stable_id = 8,
						.name = "Roguebot",
						.race_id = Race::Human,
						.class_id = Class::Rogue,
						.equipped_items = {{.item = &rogue_piercer, .slot_id = EQ::invslot::slotPrimary}}
					}
				}
			},
			{.enabled = true}
		);

		TEST_ASSERT(result.produced);
		TEST_ASSERT_EQUALS(result.requesting_bot_stable_id, 8u);
	}

	void MartialWeaponDamageDelayCanProduceMonkUpgrade()
	{
		const auto old_knuckles = Weapon(
			5984,
			"Old Knuckles",
			EQ::invslot::slotPrimary,
			EQ::item::ItemTypeMartial,
			12,
			30
		);
		const auto new_knuckles = Weapon(
			5985,
			"New Knuckles",
			EQ::invslot::slotPrimary,
			EQ::item::ItemTypeMartial,
			18,
			30
		);

		const auto result = BotLootRequest::BuildRequestForSuccessfulLoot(
			{
				.looter_name = "Aten",
				.looted_item = &new_knuckles,
				.looted_item_link = "[New Knuckles]",
				.grouped_bots = {{
					.name_stable_id = 7,
					.name = "Monkbot",
					.race_id = Race::Human,
					.class_id = Class::Monk,
					.equipped_items = {{.item = &old_knuckles, .slot_id = EQ::invslot::slotPrimary}}
				}}
			},
			{.enabled = true}
		);

		TEST_ASSERT(result.produced);
		TEST_ASSERT_EQUALS(result.requesting_bot_stable_id, 7u);
	}

	void ValidWeaponProcSignalCanProduceSmallUpgrade()
	{
		const auto plain_sword = Weapon(
			5991,
			"Plain Sword",
			EQ::invslot::slotPrimary,
			EQ::item::ItemType1HSlash,
			15,
			30
		);
		auto proc_sword = Weapon(
			5992,
			"Proc Sword",
			EQ::invslot::slotPrimary,
			EQ::item::ItemType1HSlash,
			15,
			30
		);
		proc_sword.Proc.Effect = 1234;
		proc_sword.Proc.Level2 = 1;

		const auto result = BotLootRequest::BuildRequestForSuccessfulLoot(
			{
				.looter_name = "Aten",
				.looted_item = &proc_sword,
				.looted_item_link = "[Proc Sword]",
				.grouped_bots = {{
					.name_stable_id = 7,
					.name = "Warriorbot",
					.race_id = Race::Human,
					.class_id = Class::Warrior,
					.level = 50,
					.equipped_items = {{.item = &plain_sword, .slot_id = EQ::invslot::slotPrimary}}
				}}
			},
			{.enabled = true}
		);

		TEST_ASSERT(result.produced);
		TEST_ASSERT_EQUALS(result.upgrade_score, 4);
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

	void CooldownSuppressesSameLooterAndRequestingBot()
	{
		const auto old_chest = Gear(8001, "Cloth Shirt", EQ::invslot::slotChest, 1);
		const auto looted = Gear(8002, "Bronze Breastplate", EQ::invslot::slotChest, 30);
		BotLootRequest::DeliveryState delivery_state;
		const BotLootRequest::SuccessfulLootEvent event{
			.looter_stable_id = 42,
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
		};

		const auto first = BotLootRequest::PlanVisibleRequestForSuccessfulLoot(
			event,
			{.enabled = true, .cooldown_seconds = 30, .current_time_ms = 1000},
			delivery_state
		);
		const auto second = BotLootRequest::PlanVisibleRequestForSuccessfulLoot(
			event,
			{.enabled = true, .cooldown_seconds = 30, .current_time_ms = 2000},
			delivery_state
		);

		TEST_ASSERT(first.produced);
		TEST_ASSERT(!second.produced);
	}

	void CooldownDoesNotSuppressDifferentRequestingBot()
	{
		const auto first_old_chest = Gear(8101, "Cloth Shirt", EQ::invslot::slotChest, 1);
		const auto second_old_chest = Gear(8102, "Tattered Tunic", EQ::invslot::slotChest, 1);
		const auto first_looted = Gear(8103, "Bronze Breastplate", EQ::invslot::slotChest, 30);
		const auto second_looted = Gear(8104, "Fine Breastplate", EQ::invslot::slotChest, 40);
		BotLootRequest::DeliveryState delivery_state;

		const auto first = BotLootRequest::PlanVisibleRequestForSuccessfulLoot(
			{
				.looter_stable_id = 42,
				.looter_name = "Aten",
				.looted_item = &first_looted,
				.looted_item_link = "[Bronze Breastplate]",
				.grouped_bots = {{
					.name_stable_id = 7,
					.name = "Firstbot",
					.race_id = Race::Human,
					.class_id = Class::Warrior,
					.equipped_items = {{.item = &first_old_chest, .slot_id = EQ::invslot::slotChest}}
				}}
			},
			{.enabled = true, .cooldown_seconds = 30, .current_time_ms = 1000},
			delivery_state
		);
		const auto second = BotLootRequest::PlanVisibleRequestForSuccessfulLoot(
			{
				.looter_stable_id = 42,
				.looter_name = "Aten",
				.looted_item = &second_looted,
				.looted_item_link = "[Fine Breastplate]",
				.grouped_bots = {{
					.name_stable_id = 8,
					.name = "Secondbot",
					.race_id = Race::Human,
					.class_id = Class::Warrior,
					.equipped_items = {{.item = &second_old_chest, .slot_id = EQ::invslot::slotChest}}
				}}
			},
			{.enabled = true, .cooldown_seconds = 30, .current_time_ms = 2000},
			delivery_state
		);

		TEST_ASSERT(first.produced);
		TEST_ASSERT(second.produced);
		TEST_ASSERT_EQUALS(second.requesting_bot_stable_id, 8u);
	}

	void CooldownDoesNotSuppressDifferentLooter()
	{
		const auto old_chest = Gear(8201, "Cloth Shirt", EQ::invslot::slotChest, 1);
		const auto looted = Gear(8202, "Bronze Breastplate", EQ::invslot::slotChest, 30);
		BotLootRequest::DeliveryState delivery_state;
		const BotLootRequest::GroupedBotSnapshot bot{
			.name_stable_id = 7,
			.name = "Atenbot",
			.race_id = Race::Human,
			.class_id = Class::Warrior,
			.equipped_items = {{.item = &old_chest, .slot_id = EQ::invslot::slotChest}}
		};

		const auto first = BotLootRequest::PlanVisibleRequestForSuccessfulLoot(
			{
				.looter_stable_id = 42,
				.looter_name = "Aten",
				.looted_item = &looted,
				.looted_item_link = "[Bronze Breastplate]",
				.grouped_bots = {bot}
			},
			{.enabled = true, .cooldown_seconds = 30, .current_time_ms = 1000},
			delivery_state
		);
		const auto second = BotLootRequest::PlanVisibleRequestForSuccessfulLoot(
			{
				.looter_stable_id = 43,
				.looter_name = "Bump",
				.looted_item = &looted,
				.looted_item_link = "[Bronze Breastplate]",
				.grouped_bots = {bot}
			},
			{.enabled = true, .cooldown_seconds = 30, .current_time_ms = 2000},
			delivery_state
		);

		TEST_ASSERT(first.produced);
		TEST_ASSERT(second.produced);
	}

	void SingleLootEventEmitsOneVisibleRequest()
	{
		const auto old_chest = Gear(8301, "Cloth Shirt", EQ::invslot::slotChest, 1);
		const auto looted = Gear(8302, "Bronze Breastplate", EQ::invslot::slotChest, 30);
		BotLootRequest::DeliveryState delivery_state;
		const BotLootRequest::SuccessfulLootEvent event{
			.looter_stable_id = 42,
			.loot_event_id = 9001,
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
		};

		const auto first = BotLootRequest::PlanVisibleRequestForSuccessfulLoot(
			event,
			{.enabled = true, .cooldown_seconds = 0, .current_time_ms = 1000},
			delivery_state
		);
		const auto second = BotLootRequest::PlanVisibleRequestForSuccessfulLoot(
			event,
			{.enabled = true, .cooldown_seconds = 0, .current_time_ms = 2000},
			delivery_state
		);

		TEST_ASSERT(first.produced);
		TEST_ASSERT(!second.produced);
	}

	void VisibleRequestIsGroupChatTemplateWithServerItemLink()
	{
		const auto old_chest = Gear(8401, "Cloth Shirt", EQ::invslot::slotChest, 1);
		const auto looted = Gear(8402, "Bronze Breastplate", EQ::invslot::slotChest, 30);

		const auto result = BotLootRequest::BuildRequestForSuccessfulLoot(
			{
				.looter_name = "Aten",
				.looted_item = &looted,
				.looted_item_link = "SERVER_ITEM_LINK::Bronze Breastplate",
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
		TEST_ASSERT(result.delivery_channel == BotLootRequest::DeliveryChannel::GroupChat);
		TEST_ASSERT_EQUALS(
			result.message,
			std::string("Aten, could I use SERVER_ITEM_LINK::Bronze Breastplate? It looks like an upgrade for my chest.")
		);
	}

	void AsyncDialogueQueuesWithoutVisibleMessage()
	{
		const auto old_chest = Gear(8501, "Cloth Shirt", EQ::invslot::slotChest, 1);
		const auto looted = Gear(8502, "Bronze Breastplate", EQ::invslot::slotChest, 30);
		BotLootRequest::DeliveryState delivery_state;
		BotLootRequest::TestDelayedDialogueProvider provider;
		BotLootRequest::DelayedDialogueQueue queue(provider, {});
		const BotLootRequest::SuccessfulLootEvent event{
			.looter_stable_id = 42,
			.loot_event_id = 9101,
			.looter_name = "Aten",
			.looted_item = &looted,
			.looted_item_link = "SERVER_ITEM_LINK::Bronze Breastplate",
			.grouped_bots = {{
				.name_stable_id = 7,
				.name = "Atenbot",
				.race_id = Race::Human,
				.class_id = Class::Warrior,
				.equipped_items = {{.item = &old_chest, .slot_id = EQ::invslot::slotChest}}
			}}
		};

		const auto request = BotLootRequest::PlanVisibleRequestForSuccessfulLoot(
			event,
			{.enabled = true, .cooldown_seconds = 0, .current_time_ms = 1000},
			delivery_state
		);
		const auto queued = queue.Enqueue(request, event, {.endpoint = "http://127.0.0.1:11434/api/generate", .model = "test", .timeout_ms = 1});

		TEST_ASSERT(request.produced);
		TEST_ASSERT(queued.queued);
		TEST_ASSERT_EQUALS(provider.PendingRequests().size(), 1u);
		TEST_ASSERT_EQUALS(queued.message, std::string(""));
	}

	void LootRequestDialoguePromptUsesOnlyCompactIntent()
	{
		BotLootRequest::RequestDialogueIntent intent{
			.looter_name = "Aten",
			.requesting_bot_name = "Atenbot",
			.plain_item_name = "Bronze Breastplate",
			.item_link = "SERVER_ITEM_LINK::Bronze Breastplate",
			.target_slot_name = "chest",
			.reason_summary = "upgrade for chest",
			.deterministic_template = "Aten, could I use SERVER_ITEM_LINK::Bronze Breastplate? It looks like an upgrade for my chest."
		};

		const auto prompt = BotLootRequest::BuildLootRequestDialoguePrompt(intent);

		TEST_ASSERT(prompt.find("Bot: Atenbot") != std::string::npos);
		TEST_ASSERT(prompt.find("Looter: Aten") != std::string::npos);
		TEST_ASSERT(prompt.find("Plain item: Bronze Breastplate") != std::string::npos);
		TEST_ASSERT(prompt.find("Target slot: chest") != std::string::npos);
		TEST_ASSERT(prompt.find("Reason: upgrade for chest") != std::string::npos);
		TEST_ASSERT(prompt.find("SERVER_ITEM_LINK") == std::string::npos);
		TEST_ASSERT(prompt.find("AC") == std::string::npos);
		TEST_ASSERT(prompt.find("inventory") == std::string::npos);
		TEST_ASSERT(prompt.find("corpse") == std::string::npos);
		TEST_ASSERT(prompt.find("score") == std::string::npos);
	}

	void StaleGroupSuppressesDelayedLootRequestDialogue()
	{
		BotLootRequest::TestDelayedDialogueProvider provider;
		BotLootRequest::DelayedDialogueQueue queue(provider, {});
		auto request = ReadyAsyncRequest(queue);

		TEST_ASSERT(provider.CompleteNextSuccess("That breastplate would suit me."));

		BotLootRequest::DialogueResult result;
		const bool ready = queue.PopReadyResult(
			[](const BotLootRequest::DelayedDialogueRequest &) {
				return BotLootRequest::CurrentGroupState{
					.looter_present = true,
					.requesting_bot_present = true,
					.still_grouped = false
				};
			},
			result
		);

		TEST_ASSERT(request.produced);
		TEST_ASSERT(!ready);
	}

	void AcceptedGeneratedLootRequestDialogueUsesServerItemLink()
	{
		BotLootRequest::TestDelayedDialogueProvider provider;
		BotLootRequest::DelayedDialogueQueue queue(provider, {});
		ReadyAsyncRequest(queue);

		TEST_ASSERT(provider.CompleteNextSuccess("That breastplate would suit me."));

		BotLootRequest::DialogueResult result;
		const bool ready = queue.PopReadyResult(CurrentGroupStillValid, result);

		TEST_ASSERT(ready);
		TEST_ASSERT(result.produced);
		TEST_ASSERT_EQUALS(result.requesting_bot_stable_id, 7u);
		TEST_ASSERT_EQUALS(result.message, std::string("That breastplate would suit me. SERVER_ITEM_LINK::Bronze Breastplate"));
		TEST_ASSERT(result.delivery_channel == BotLootRequest::DeliveryChannel::GroupChat);
	}

	void GeneratedLootRequestThatNamesLooterFallsBackToTemplate()
	{
		BotLootRequest::TestDelayedDialogueProvider provider;
		BotLootRequest::DelayedDialogueQueue queue(provider, {});
		ReadyAsyncRequest(queue);

		TEST_ASSERT(provider.CompleteNextSuccess("Aten could use that breastplate."));

		BotLootRequest::DialogueResult result;
		const bool ready = queue.PopReadyResult(CurrentGroupStillValid, result);

		TEST_ASSERT(ready);
		TEST_ASSERT_EQUALS(
			result.message,
			std::string("Aten, could I use SERVER_ITEM_LINK::Bronze Breastplate? It looks like an upgrade for my chest.")
		);
	}

	void FailedGeneratedLootRequestDialogueFallsBackToTemplate()
	{
		BotLootRequest::TestDelayedDialogueProvider provider;
		BotLootRequest::DelayedDialogueQueue queue(provider, {});
		ReadyAsyncRequest(queue);

		TEST_ASSERT(provider.CompleteNextFailure());

		BotLootRequest::DialogueResult result;
		const bool ready = queue.PopReadyResult(CurrentGroupStillValid, result);

		TEST_ASSERT(ready);
		TEST_ASSERT(result.produced);
		TEST_ASSERT_EQUALS(
			result.message,
			std::string("Aten, could I use SERVER_ITEM_LINK::Bronze Breastplate? It looks like an upgrade for my chest.")
		);
	}

	void UnsafeGeneratedLootRequestDialogueFallsBackToTemplate()
	{
		for (const auto &dialogue_response : {
			"metadata: use this item",
			"*grabs the breastplate*",
			"That would suit me.\nI need it.",
			"That would suit me. [Bronze Breastplate]"
		}) {
			BotLootRequest::TestDelayedDialogueProvider provider;
			BotLootRequest::DelayedDialogueQueue queue(provider, {});
			ReadyAsyncRequest(queue);

			TEST_ASSERT(provider.CompleteNextSuccess(dialogue_response));

			BotLootRequest::DialogueResult result;
			const bool ready = queue.PopReadyResult(CurrentGroupStillValid, result);

			TEST_ASSERT(ready);
			TEST_ASSERT_EQUALS(
				result.message,
				std::string("Aten, could I use SERVER_ITEM_LINK::Bronze Breastplate? It looks like an upgrade for my chest.")
			);
		}
	}

	static BotLootRequest::CurrentGroupState CurrentGroupStillValid(const BotLootRequest::DelayedDialogueRequest &)
	{
		return {
			.looter_present = true,
			.requesting_bot_present = true,
			.still_grouped = true
		};
	}

	BotLootRequest::Request ReadyAsyncRequest(BotLootRequest::DelayedDialogueQueue &queue)
	{
		const auto old_chest = Gear(8601, "Cloth Shirt", EQ::invslot::slotChest, 1);
		const auto looted = Gear(8602, "Bronze Breastplate", EQ::invslot::slotChest, 30);
		BotLootRequest::DeliveryState delivery_state;
		const BotLootRequest::SuccessfulLootEvent event{
			.looter_stable_id = 42,
			.loot_event_id = 9201,
			.looter_name = "Aten",
			.looted_item = &looted,
			.looted_item_link = "SERVER_ITEM_LINK::Bronze Breastplate",
			.grouped_bots = {{
				.name_stable_id = 7,
				.name = "Atenbot",
				.race_id = Race::Human,
				.class_id = Class::Warrior,
				.equipped_items = {{.item = &old_chest, .slot_id = EQ::invslot::slotChest}}
			}}
		};

		auto request = BotLootRequest::PlanVisibleRequestForSuccessfulLoot(
			event,
			{.enabled = true, .cooldown_seconds = 0, .current_time_ms = 1000},
			delivery_state
		);
		queue.Enqueue(request, event, {.endpoint = "http://127.0.0.1:11434/api/generate", .model = "test", .timeout_ms = 1});
		return request;
	}
};

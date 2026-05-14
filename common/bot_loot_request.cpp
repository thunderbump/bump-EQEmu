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
#include "bot_loot_request.h"

#include "emu_constants.h"

#include <fmt/format.h>

namespace BotLootRequest {

namespace {

bool IsEquipmentSlot(int slot_id)
{
	return slot_id >= EQ::invslot::EQUIPMENT_BEGIN && slot_id <= EQ::invslot::EQUIPMENT_END;
}

bool ItemCanEquipInSlot(const EQ::ItemData &item, int slot_id)
{
	return IsEquipmentSlot(slot_id) && (item.Slots & (1u << slot_id));
}

bool IsV1GearItemType(const EQ::ItemData &item)
{
	switch (item.ItemType) {
	case EQ::item::ItemType1HSlash:
	case EQ::item::ItemType2HSlash:
	case EQ::item::ItemType1HPiercing:
	case EQ::item::ItemType1HBlunt:
	case EQ::item::ItemType2HBlunt:
	case EQ::item::ItemTypeBow:
	case EQ::item::ItemTypeLargeThrowing:
	case EQ::item::ItemTypeShield:
	case EQ::item::ItemTypeArmor:
	case EQ::item::ItemTypeWindInstrument:
	case EQ::item::ItemTypeStringedInstrument:
	case EQ::item::ItemTypeBrassInstrument:
	case EQ::item::ItemTypePercussionInstrument:
	case EQ::item::ItemTypeSmallThrowing:
	case EQ::item::ItemTypeJewelry:
	case EQ::item::ItemType2HPiercing:
	case EQ::item::ItemTypeMartial:
	case EQ::item::ItemTypeSinging:
	case EQ::item::ItemTypeAllInstrumentTypes:
	case EQ::item::ItemTypeCharm:
		return true;
	default:
		return false;
	}
}

bool IsGearCandidateForBot(const EQ::ItemData &item, const GroupedBotSnapshot &bot)
{
	if (
		item.ItemClass != EQ::item::ItemClassCommon ||
		item.BagSlots > 0 ||
		item.Slots == 0 ||
		item.IsQuestItem() ||
		!IsV1GearItemType(item) ||
		!item.IsEquipable(bot.race_id, bot.class_id)
	) {
		return false;
	}

	for (int slot_id = EQ::invslot::EQUIPMENT_BEGIN; slot_id <= EQ::invslot::EQUIPMENT_END; ++slot_id) {
		if (ItemCanEquipInSlot(item, slot_id)) {
			return true;
		}
	}

	return false;
}

int GearScore(const EQ::ItemData *item)
{
	if (!item) {
		return 0;
	}

	return item->AC + item->HP + item->Mana + item->Endur + item->AStr + item->ASta +
		item->AAgi + item->ADex + item->ACha + item->AInt + item->AWis;
}

const EQ::ItemData *EquippedItemForSlot(const GroupedBotSnapshot &bot, int slot_id)
{
	for (const auto &equipped_item : bot.equipped_items) {
		if (equipped_item.item && equipped_item.slot_id == slot_id) {
			return equipped_item.item;
		}
	}

	return nullptr;
}

bool HasLoreConflict(const GroupedBotSnapshot &bot, const EQ::ItemData &item)
{
	if (!item.LoreFlag) {
		return false;
	}

	for (const auto &equipped_item : bot.equipped_items) {
		if (EQ::ItemData::CheckLoreConflict(equipped_item.item, &item)) {
			return true;
		}
	}

	return false;
}

std::string SlotName(int slot_id)
{
	switch (slot_id) {
	case EQ::invslot::slotCharm:
		return "charm";
	case EQ::invslot::slotEar1:
	case EQ::invslot::slotEar2:
		return "ear";
	case EQ::invslot::slotHead:
		return "head";
	case EQ::invslot::slotFace:
		return "face";
	case EQ::invslot::slotNeck:
		return "neck";
	case EQ::invslot::slotShoulders:
		return "shoulders";
	case EQ::invslot::slotArms:
		return "arms";
	case EQ::invslot::slotBack:
		return "back";
	case EQ::invslot::slotWrist1:
	case EQ::invslot::slotWrist2:
		return "wrist";
	case EQ::invslot::slotRange:
		return "range";
	case EQ::invslot::slotHands:
		return "hands";
	case EQ::invslot::slotPrimary:
		return "primary";
	case EQ::invslot::slotSecondary:
		return "secondary";
	case EQ::invslot::slotFinger1:
	case EQ::invslot::slotFinger2:
		return "finger";
	case EQ::invslot::slotChest:
		return "chest";
	case EQ::invslot::slotLegs:
		return "legs";
	case EQ::invslot::slotFeet:
		return "feet";
	case EQ::invslot::slotWaist:
		return "waist";
	case EQ::invslot::slotPowerSource:
		return "power source";
	case EQ::invslot::slotAmmo:
		return "ammo";
	default:
		return "gear";
	}
}

std::string ItemDisplayName(const SuccessfulLootEvent &event)
{
	if (!event.looted_item_link.empty()) {
		return event.looted_item_link;
	}

	if (event.looted_item) {
		return event.looted_item->Name;
	}

	return "that item";
}

} // namespace

Request BuildRequestForSuccessfulLoot(const SuccessfulLootEvent &event, const Settings &settings)
{
	if (!settings.enabled || !event.looted_item || event.grouped_bots.empty()) {
		return {};
	}

	Request best_request{};

	for (const auto &bot : event.grouped_bots) {
		if (!IsGearCandidateForBot(*event.looted_item, bot) || HasLoreConflict(bot, *event.looted_item)) {
			continue;
		}

		for (int slot_id = EQ::invslot::EQUIPMENT_BEGIN; slot_id <= EQ::invslot::EQUIPMENT_END; ++slot_id) {
			if (!ItemCanEquipInSlot(*event.looted_item, slot_id)) {
				continue;
			}

			const int upgrade_score = GearScore(event.looted_item) - GearScore(EquippedItemForSlot(bot, slot_id));
			if (upgrade_score <= 0) {
				continue;
			}

			if (!best_request.produced || upgrade_score > best_request.upgrade_score) {
				best_request.produced = true;
				best_request.requesting_bot_stable_id = bot.name_stable_id;
				best_request.requesting_bot_name = bot.name;
				best_request.target_slot = slot_id;
				best_request.upgrade_score = upgrade_score;
			}
		}
	}

	if (!best_request.produced) {
		return {};
	}

	best_request.message = fmt::format(
		"{}, could I use {}? It looks like an upgrade for my {}.",
		event.looter_name,
		ItemDisplayName(event),
		SlotName(best_request.target_slot)
	);

	return best_request;
}

} // namespace BotLootRequest

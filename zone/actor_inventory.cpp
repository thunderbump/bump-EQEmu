#include "actor_inventory.h"
#ifdef EQEMU_RUST_BOOTSTRAP_BRIDGE
#include "npc.h"
#include "zonedb.h"
#include "common/json/json.hpp"
#include "common/inventory_profile.h"
#include "common/item_instance.h"
#include <functional>
#include <cstdio>
#include <memory>
#include <stdexcept>
#include <set>
#include <tuple>

extern ZoneDatabase database;
namespace ActorInventory {
using Json = nlohmann::json;
namespace {
void Capture(Json &records, const EQ::ItemInstance &item, int parent, int kind, int slot, unsigned depth) {
	if (depth > 3 || records.size() >= 512 || !item.GetItem()) { throw std::runtime_error("actor_inventory_bounds"); }
	for (const auto &child : *const_cast<EQ::ItemInstance &>(item).GetContents()) {
		const auto limit = item.IsClassBag() ? item.GetItem()->BagSlots : EQ::invaug::SOCKET_COUNT;
		if (child.second && child.first >= limit) { throw std::runtime_error("actor_inventory_hidden_slot"); }
	}
	const auto index = records.size();
	records.push_back({{"parent", parent}, {"kind", kind}, {"slot", slot}, {"id", item.GetID()},
			{"charges", item.GetCharges()}, {"color", item.GetColor()}, {"custom", item.GetCustomDataString()},
			{"attuned", item.IsAttuned()}, {"ornament_icon", item.GetOrnamentationIcon()},
			{"ornament_file", item.GetOrnamentationIDFile()}, {"ornament_model", item.GetOrnamentHeroModel()},
			{"serial", item.GetSerialNumber()}, {"recast", item.GetRecastTimestamp()},
			{"scaling", item.IsScaling()}, {"scaling_exp", item.GetExp()},
			{"evolve_id", item.GetEvolveUniqueID()}, {"evolve_character", item.GetEvolveCharID()},
			{"evolve_item", item.GetEvolveItemID()}, {"evolve_final", item.GetEvolveFinalItemID()},
			{"evolve_amount", item.GetEvolveCurrentAmount()}, {"evolve_progress", item.GetEvolveProgression()},
			{"evolve_active", item.GetEvolveActivated()}, {"evolve_equipped", item.GetEvolveEquipped()}});
	if (item.IsClassBag()) {
		for (unsigned i = 0; i < item.GetItem()->BagSlots; ++i) {
			if (auto *child = item.GetItem(i)) { Capture(records, *child, index, 1, i, depth + 1); }
		}
	} else {
		for (unsigned i = 0; i < EQ::invaug::SOCKET_COUNT; ++i) {
			if (auto *augment = item.GetAugment(i)) { Capture(records, *augment, index, 2, i, depth + 1); }
		}
	}
}
}
std::string Snapshot(NPC &body) {
	Json records = Json::array();
	auto &inventory = body.GetInv();
	for (int slot = EQ::invslot::POSSESSIONS_BEGIN; slot <= EQ::invslot::POSSESSIONS_END; ++slot) {
		if (slot == EQ::invslot::slotCursor) { continue; }
		if (auto *item = inventory.GetItem(slot)) { Capture(records, *item, -1, 0, slot, 0); }
	}
	// Player cursor queues are ordered and can contain more than one item.
	int cursor = 0;
	for (auto it = inventory.cursor_cbegin(); it != inventory.cursor_cend(); ++it) {
		Capture(records, **it, -1, 3, cursor++, 0);
	}
	return records.dump();
}
void Restore(NPC &body, const std::string &snapshot) {
	auto &inventory = body.GetInv();
	inventory.SetInventoryVersion(EQ::versions::MobVersion::RoF2);
	inventory.SetGMInventory(false);
	if (snapshot == "null") {
		// Same configured starting item selection and placement as character creation.
		PlayerProfile_Struct profile{};
		char name[64];
		std::snprintf(name, sizeof(name), "%s", body.GetName());
		database.SetStartingItems(&profile, &inventory, body.GetRace(), body.GetClass(),
				body.GetDeity(), 2, name, 0);
	} else {
		const auto records = Json::parse(snapshot);
		if (!records.is_array() || records.size() > 512) { throw std::runtime_error("actor_inventory_shape"); }
		std::set<std::tuple<int, int, int>> slots;
		for (size_t i = 0; i < records.size(); ++i) {
			const auto &record = records[i];
			const int parent = record.at("parent"), kind = record.at("kind"), slot = record.at("slot");
			if (!slots.emplace(parent, kind, slot).second || record.at("id").get<uint32>() == 0 ||
					(kind != 0 && kind != 3 && (parent < 0 || parent >= int(i))) ||
					((kind == 0 || kind == 3) && parent != -1) || kind < 0 || kind > 3) {
				throw std::runtime_error("actor_inventory_tree");
			}
		}
		std::vector<bool> visited(records.size(), false);
		for (const auto &record : records) {
			const auto serial = record.at("serial").get<int32>();
			if (serial) { EQ::ItemInstance::AddGUIDToMap(serial); }
		}
		std::function<std::unique_ptr<EQ::ItemInstance>(size_t, unsigned)> build;
		build = [&](size_t index, unsigned depth) {
			if (depth > 3 || index >= records.size()) { throw std::runtime_error("actor_inventory_tree"); }
			if (visited[index]) { throw std::runtime_error("actor_inventory_duplicate_child"); }
			visited[index] = true;
			const auto &r = records[index];
			std::unique_ptr<EQ::ItemInstance> item(database.CreateItem(r.at("id").get<uint32>(), r.at("charges").get<int16>()));
			if (!item) { throw std::runtime_error("actor_inventory_unknown_item"); }
			item->SetColor(r.at("color").get<uint32>());
			item->SetCustomDataString(r.at("custom").get<std::string>());
			item->SetAttuned(r.at("attuned").get<bool>());
			item->SetOrnamentIcon(r.at("ornament_icon").get<uint32>());
			item->SetOrnamentationIDFile(r.at("ornament_file").get<uint32>());
			item->SetOrnamentHeroModel(r.at("ornament_model").get<uint32>());
			item->SetSerialNumber(r.at("serial").get<int32>());
			item->SetRecastTimestamp(r.at("recast").get<uint32>());
			item->SetScaling(r.at("scaling").get<bool>());
			item->SetExp(r.at("scaling_exp").get<uint32>());
			item->SetEvolveUniqueID(r.at("evolve_id").get<uint64>());
			item->SetEvolveCharID(r.at("evolve_character").get<uint32>());
			item->SetEvolveItemID(r.at("evolve_item").get<uint32>());
			item->SetEvolveFinalItemID(r.at("evolve_final").get<uint32>());
			item->SetEvolveCurrentAmount(r.at("evolve_amount").get<uint64>());
			item->SetEvolveProgression(r.at("evolve_progress").get<double>());
			item->SetEvolveActivated(r.at("evolve_active").get<bool>());
			item->SetEvolveEquipped(r.at("evolve_equipped").get<bool>());
			for (size_t j = index + 1; j < records.size(); ++j) {
				if (records[j].at("parent").get<int>() != int(index)) { continue; }
				auto child = build(j, depth + 1);
				const auto slot = records[j].at("slot").get<int>();
				if (records[j].at("kind").get<int>() == 1) {
					if (!item->IsClassBag() || slot < 0 || slot >= item->GetItem()->BagSlots) { throw std::runtime_error("actor_inventory_bag_slot"); }
					item->PutItem(slot, *child);
				} else {
					if (!item->IsClassCommon() || slot < 0 || slot >= EQ::invaug::SOCKET_COUNT) { throw std::runtime_error("actor_inventory_augment_slot"); }
					item->PutAugment(slot, *child);
				}
			}
			if (item->IsScaling()) { item->ScaleItem(); }
			return item;
		};
		// Only publish a completely reconstructed inventory. No missing item may
		// silently remove a bag, augment or equipped item during restoration.
		EQ::InventoryProfile restored;
		restored.SetInventoryVersion(EQ::versions::MobVersion::RoF2);
		restored.SetGMInventory(false);
		for (size_t i = 0; i < records.size(); ++i) {
			const auto &record = records[i];
			if (record.at("parent").get<int>() != -1) { continue; }
			auto item = build(i, 0);
			const auto slot = record.at("slot").get<int16>();
			if (record.at("kind").get<int>() == 3) {
				if (restored.PushCursor(*item) == EQ::invslot::SLOT_INVALID) { throw std::runtime_error("actor_inventory_cursor"); }
			} else {
				const bool possession = slot >= EQ::invslot::POSSESSIONS_BEGIN && slot <= EQ::invslot::POSSESSIONS_END;
				if ((!possession && slot != EQ::invslot::slotPowerSource) || slot == EQ::invslot::slotCursor || !item->IsSlotAllowed(slot) || restored.PutItem(slot, *item) == EQ::invslot::SLOT_INVALID) {
					throw std::runtime_error("actor_inventory_possession_slot");
				}
			}
		}
		for (bool present : visited) { if (!present) { throw std::runtime_error("actor_inventory_orphan"); } }
		for (int slot = EQ::invslot::POSSESSIONS_BEGIN; slot < EQ::invslot::slotCursor; ++slot) {
			if (auto *item = restored.GetItem(slot)) {
				if (inventory.PutItem(slot, *item) == EQ::invslot::SLOT_INVALID) { throw std::runtime_error("actor_inventory_publish"); }
			}
		}
		for (auto it = restored.cursor_cbegin(); it != restored.cursor_cend(); ++it) {
			if (inventory.PushCursor(**it) == EQ::invslot::SLOT_INVALID) { throw std::runtime_error("actor_inventory_publish_cursor"); }
		}
	}
	body.RefreshActorEquipment();
	body.CalcBonuses();
}
} // namespace ActorInventory
#endif

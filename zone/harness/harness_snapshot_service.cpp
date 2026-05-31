/*	EQEmu: EQEmulator

	Copyright (C) 2001-2026 EQEmu Development Team

	This program is free software; you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation; either version 3 of the License, or
	(at your option) any later version.
*/

#include "harness_snapshot_service.h"

#include "zone/entity.h"
#include "zone/mob.h"
#include "zone/zone.h"

extern EntityList entity_list;
extern Zone *zone;
extern volatile bool is_zone_loaded;

namespace EQ::ZoneHarness {

ZoneIdentitySnapshot HarnessSnapshotService::ZoneIdentity() const
{
	if (!zone || !is_zone_loaded) {
		return {};
	}

	return {
		.loaded = true,
		.zone_id = zone->GetZoneID(),
		.instance_id = zone->GetInstanceID(),
		.instance_version = zone->GetInstanceVersion(),
		.short_name = zone->GetShortName(),
		.long_name = zone->GetLongName(),
	};
}

EntitySnapshot HarnessSnapshotService::Entities(uint32_t sample_limit) const
{
	EntitySnapshot snapshot;
	snapshot.counts.mobs = entity_list.GetMobList().size();
	snapshot.counts.npcs = entity_list.GetNPCList().size();
	snapshot.counts.clients = entity_list.GetClientList().size();
	snapshot.counts.bots = entity_list.GetBotList().size();
	snapshot.counts.corpses = entity_list.GetCorpseList().size();
	snapshot.counts.doors = entity_list.GetDoorsList().size();
	snapshot.counts.objects = entity_list.GetObjectList().size();

	for (const auto &[entity_id, mob]: entity_list.GetMobList()) {
		if (!mob) {
			continue;
		}

		std::string type = "mob";
		if (mob->IsClient()) {
			type = "client";
		}
		else if (mob->IsBot()) {
			type = "bot";
		}
		else if (mob->IsNPC()) {
			type = "npc";
		}
		else if (mob->IsCorpse()) {
			type = "corpse";
		}

		snapshot.sample.push_back({
			.entity_id = entity_id,
			.npc_type_id = mob->GetNPCTypeID(),
			.type = type,
			.name = mob->GetCleanName(),
			.level = mob->GetLevel(),
			.class_id = mob->GetClass(),
			.race_id = mob->GetRace(),
			.x = mob->GetX(),
			.y = mob->GetY(),
			.z = mob->GetZ(),
		});

		if (snapshot.sample.size() >= sample_limit) {
			break;
		}
	}

	return snapshot;
}

}

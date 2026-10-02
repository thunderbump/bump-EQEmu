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

#include <algorithm>

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

	if (sample_limit == 0) {
		return snapshot;
	}

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

namespace {

uint8_t Percent(int64_t current, int64_t maximum)
{
	if (maximum <= 0) {
		return 0;
	}

	return static_cast<uint8_t>(std::clamp<int64_t>((current * 100) / maximum, 0, 100));
}

std::string DistanceBucket(float distance_squared)
{
	if (distance_squared <= (15.0f * 15.0f)) {
		return "melee";
	}

	if (distance_squared <= (60.0f * 60.0f)) {
		return "near";
	}

	return "far";
}

}

ActorPerceptionSnapshot HarnessSnapshotService::PerceptionFor(Mob *actor, Mob *owner, uint32_t nearby_limit) const
{
	if (!actor) {
		return {
			.available = false,
			.reason = "actor_not_found",
		};
	}

	ActorPerceptionSnapshot perception{
		.available = true,
		.reason = "ok",
		.self = DescribeMobEntity(actor),
	};

	if (actor->GetTarget()) {
		perception.current_target = DescribeMobEntity(actor->GetTarget());
	}

	struct Candidate {
		float distance_squared = 0.0f;
		PerceivedEntitySnapshot snapshot;
	};

	std::vector<Candidate> candidates;
	for (const auto &[entity_id, mob]: entity_list.GetMobList()) {
		if (!mob || mob == actor) {
			continue;
		}

		Candidate candidate;
		candidate.distance_squared = DistanceSquared(actor->GetPosition(), mob->GetPosition());
		candidate.snapshot.entity = DescribeMobEntity(mob);
		candidate.snapshot.distance_bucket = DistanceBucket(candidate.distance_squared);
		candidate.snapshot.alive = !mob->HasDied();
		candidate.snapshot.hp_percent = Percent(mob->GetHP(), mob->GetMaxHP());

		if (mob == owner) {
			candidate.snapshot.relation_tags.push_back("owner");
		}
		if (actor->IsInGroupOrRaid(mob)) {
			candidate.snapshot.relation_tags.push_back("group");
		}
		if (actor->GetTarget() == mob) {
			candidate.snapshot.relation_tags.push_back("current_target");
		}
		if (actor->CheckAggro(mob) || mob->CheckAggro(actor)) {
			candidate.snapshot.relation_tags.push_back("hostile");
		}

		candidates.push_back(std::move(candidate));
	}

	std::sort(
		candidates.begin(),
		candidates.end(),
		[](const Candidate &lhs, const Candidate &rhs) {
			return lhs.distance_squared < rhs.distance_squared;
		}
	);

	const auto bounded_limit = std::clamp<uint32_t>(nearby_limit, 1, 25);
	for (size_t i = 0; i < candidates.size() && perception.nearby_entities.size() < bounded_limit; ++i) {
		perception.nearby_entities.push_back(std::move(candidates[i].snapshot));
	}

	return perception;
}

}

/*	EQEmu: EQEmulator

	Copyright (C) 2001-2026 EQEmu Development Team

	This program is free software; you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation; either version 3 of the License, or
	(at your option) any later version.
*/

#pragma once

#include "zone/harness/actor_event_recorder.h"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

class Mob;

namespace EQ::ZoneHarness {

struct ZoneIdentitySnapshot {
	bool loaded = false;
	uint32_t zone_id = 0;
	uint32_t instance_id = 0;
	uint16_t instance_version = 0;
	std::string short_name;
	std::string long_name;
};

struct EntityCountsSnapshot {
	uint64_t mobs = 0;
	uint64_t npcs = 0;
	uint64_t clients = 0;
	uint64_t bots = 0;
	uint64_t corpses = 0;
	uint64_t doors = 0;
	uint64_t objects = 0;
};

struct EntitySummary {
	uint16_t entity_id = 0;
	uint32_t npc_type_id = 0;
	std::string type;
	std::string name;
	uint8_t level = 0;
	uint8_t class_id = 0;
	uint16_t race_id = 0;
	float x = 0.0f;
	float y = 0.0f;
	float z = 0.0f;
};

struct EntitySnapshot {
	EntityCountsSnapshot counts;
	std::vector<EntitySummary> sample;
};

struct PerceivedEntitySnapshot {
	ActorEventEntity entity;
	std::vector<std::string> relation_tags;
	std::string distance_bucket;
	bool alive = false;
	uint8_t hp_percent = 0;
};

struct ActorPerceptionSnapshot {
	bool available = false;
	std::string reason;
	ActorEventEntity self;
	std::optional<ActorEventEntity> current_target;
	std::vector<PerceivedEntitySnapshot> nearby_entities;
};

class HarnessSnapshotService {
public:
	ZoneIdentitySnapshot ZoneIdentity() const;
	EntitySnapshot Entities(uint32_t sample_limit = 25) const;
	ActorPerceptionSnapshot PerceptionFor(Mob *actor, Mob *owner = nullptr, uint32_t nearby_limit = 12) const;
};

}

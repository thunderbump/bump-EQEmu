/*	EQEmu: EQEmulator

	Copyright (C) 2001-2026 EQEmu Development Team

	This program is free software; you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation; either version 3 of the License, or
	(at your option) any later version.
*/

#pragma once

#include "common/classes.h"
#include "common/races.h"
#include "zone/harness/actor_event_recorder.h"

#include <cstdint>
#include <string>
#include <vector>

#include <glm/vec4.hpp>

class Bot;
class Client;
class Group;
class Mob;
class NPC;

namespace EQ::ZoneHarness {

struct OwnedBotActorConfig {
	std::string owner_name = "HarnessActorOwner";
	std::string bot_name = "HarnessOwnedBot";
	uint8_t level = 60;
	uint16_t race = Race::Barbarian;
	uint8_t bot_class = Class::Shaman;
	uint8_t gender = Gender::Male;
	uint32_t bot_spell_list_id = 3010;
};

struct HostileNpcConfig {
	std::string name;
	glm::vec4 position;
};

// Zone Harness fixture plumbing for owned bot Autonomous Actor scenarios.
// Setup/reset methods create synthetic owner, owned bot, group, hostile NPCs, and hate/combat state that shape
// Actor Perception, then clean those fixtures up. Actor Action methods express gameplay intent through ordinary
// Mob target state; scenarios observe Actor Events such as spell cast-start rather than using test-only
// completion shortcuts.
class OwnedBotActorFixture {
public:
	OwnedBotActorFixture() = default;
	~OwnedBotActorFixture();

	OwnedBotActorFixture(const OwnedBotActorFixture&) = delete;
	OwnedBotActorFixture& operator=(const OwnedBotActorFixture&) = delete;

	// Zone Harness setup/reset shortcuts.
	bool SetUpOwnedBotGroup(const OwnedBotActorConfig &config = {});
	NPC *AddHostileNPC(const HostileNpcConfig &config);
	void EngageHostileWithOwnerGroup(NPC *hostile, int32_t owner_hate = 25, int32_t bot_hate = 25);
	void OwnedBotEngages(Mob *hostile, int32_t hate = 100);
	void RefreshOwnedBotPerception();
	void Reset();

	// Actor Actions.
	void OwnerTargets(Mob *target);
	void BotTargets(Mob *target);

	// Zone Harness setup shortcuts for preconditions, not Actor Actions.
	bool MarkHostileSlowed(NPC *hostile, uint16_t slow_spell_id, uint32_t ticks = 600);
	bool MarkHostileMezzed(NPC *hostile);

	uint16_t FindPreparedSingleTargetSlowSpell(Mob *target) const;
	bool IsSingleTargetSlowCastStartFor(const ActorEvent &event, Mob *target) const;

	Client *Owner() const { return owner; }
	Bot *OwnedBot() const { return bot; }
	Group *ActorGroup() const { return group; }
	ActorEventEntity Describe(Mob *mob) const;
	ActorEventEntity OwnerEntity() const;
	ActorEventEntity OwnedBotEntity() const;
	std::string DatabaseMutationSummary() const;

private:
	void RememberMob(Mob *mob);

	Client *owner = nullptr;
	Bot *bot = nullptr;
	Group *group = nullptr;
	uint32_t group_id = 0;
	std::vector<uint16_t> mob_ids;
};

}

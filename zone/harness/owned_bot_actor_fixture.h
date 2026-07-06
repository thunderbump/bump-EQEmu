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

inline constexpr uint8_t kMinOwnedBotPartyFollowers = 1;
inline constexpr uint8_t kMaxOwnedBotPartyFollowers = 4;

struct OwnedBotActorConfig {
	std::string owner_name = "HarnessActorOwner";
	uint32_t owner_character_id = 0;
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

struct OwnedBotActorFixtureNames {
	std::string owner_name;
	std::string actor_name;
	std::string primary_target_name;
	std::string secondary_target_name;
};

struct OwnedBotPartyConfig {
	std::string owner_name = "HarnessPartyOwner";
	uint32_t owner_character_id = 0;
	std::string actor_leader_name = "HarnessActorLeader";
	std::string follower_name_prefix = "HarnessFollower";
	uint8_t follower_count = 3;
	uint8_t level = 60;
	uint16_t race = Race::Barbarian;
	uint8_t leader_class = Class::Shaman;
	uint8_t follower_class = Class::Shaman;
	uint8_t gender = Gender::Male;
	uint32_t leader_bot_spell_list_id = 3010;
	uint32_t follower_bot_spell_list_id = 3010;
};

// Zone Harness fixture plumbing for owned bot Autonomous Actor scenarios.
// Setup/reset methods create harness-only synthetic owner clients, owned bots, groups, hostile NPCs, and
// hate/combat state that shape Actor Perception, then clean those fixtures up. Production reserved-owner actor
// setup should use persisted owner_character_id records; synthetic Client owners stay inside zone harness/tests
// as setup shortcuts rather than becoming a new production owner runtime.
// Actor Action methods express gameplay intent through ordinary Mob target state; scenarios observe Actor Events
// such as spell cast-start rather than using test-only completion shortcuts.
class OwnedBotActorFixture {
public:
	OwnedBotActorFixture() = default;
	~OwnedBotActorFixture();

	OwnedBotActorFixture(const OwnedBotActorFixture&) = delete;
	OwnedBotActorFixture& operator=(const OwnedBotActorFixture&) = delete;

	// Zone Harness setup/reset shortcuts.
	bool Create(const OwnedBotActorFixtureNames &names);
	bool SetUpOwnedBotSolo(const OwnedBotActorConfig &config = {});
	bool SetUpOwnedBotGroup(const OwnedBotActorConfig &config = {});
	bool SetUpOwnedBotParty(const OwnedBotPartyConfig &config = {});
	NPC *AddHostileNPC(const HostileNpcConfig &config);
	void PrimeOwnedBotEngagement(bool set_actor_target);
	void EngageHostileWithOwnerGroup(NPC *hostile, int32_t owner_hate = 25, int32_t bot_hate = 25);
	void EngageHostileWithParty(NPC *hostile, int32_t owner_hate = 25, int32_t bot_hate = 25);
	void OwnedBotEngages(Mob *hostile, int32_t hate = 100);
	void RefreshOwnedBotPerception();
	void RefreshPerception(Bot *bot);
	void RefreshPartyPerception();
	void AssignBotID(Bot *bot, uint32_t bot_id);
	bool RemoveMob(Mob *mob);
	void Reset();
	void Cleanup() { Reset(); }

	// Actor Actions.
	void OwnerTargets(Mob *target);
	void BotTargets(Mob *target);
	void BotTargets(Bot *actor, Mob *target);
	void SetBotCommandTargetSource(Bot *actor, Mob *source);
	void SetBotLeashSource(Bot *actor, Mob *source);
	void SetBotFollowTarget(Bot *actor, Mob *target);
	void SetFollowersFollowActorLeader();
	void SetBotAttackFlag(Bot *actor, bool enabled = true);

	// Zone Harness setup shortcuts for preconditions, not Actor Actions.
	bool MarkHostileSlowed(NPC *hostile, uint16_t slow_spell_id, uint32_t ticks = 600);
	bool MarkHostileMezzed(NPC *hostile);

	uint16_t FindPreparedSingleTargetSlowSpell(Mob *target) const;
	uint16_t FindPreparedSingleTargetSlowSpell(Bot *actor, Mob *target) const;
	bool IsSingleTargetSlowCastStartFor(const ActorEvent &event, Mob *target) const;
	bool IsSingleTargetSlowCastStartFor(Bot *actor, const ActorEvent &event, Mob *target) const;

	Client *Owner() const { return owner; }
	Bot *OwnedBot() const { return bot; }
	Bot *ActorLeader() const { return bot; }
	const std::vector<Bot*> &FollowerBots() const { return followers; }
	Group *ActorGroup() const { return group; }
	ActorEventEntity Describe(Mob *mob) const;
	ActorEventEntity OwnerEntity() const;
	ActorEventEntity OwnedBotEntity() const;
	ActorEventEntity ActorLeaderEntity() const { return OwnedBotEntity(); }
	std::string DatabaseMutationSummary() const;

	std::string failure_reason;
	std::string database_mutation = "none: synthetic owner, owned bot, group, NPCs, hate, and target state are in-memory only";
	Client *owner = nullptr;
	Bot *bot = nullptr;
	Bot *actor = nullptr;
	NPC *primary_target = nullptr;
	NPC *secondary_target = nullptr;
	Group *group = nullptr;

private:
	Client *CreateSyntheticOwnerClient(const std::string &owner_name, uint32_t owner_character_id, uint8_t level);
	Bot *CreateOwnedBot(const OwnedBotActorConfig &config);
	void RememberMob(Mob *mob);

	std::vector<Bot*> followers;
	uint32_t group_id = 0;
	std::vector<uint16_t> mob_ids;
};

}

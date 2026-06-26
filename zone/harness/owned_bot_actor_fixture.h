/*	EQEmu: EQEmulator

	Copyright (C) 2001-2026 EQEmu Development Team

	This program is free software; you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation; either version 3 of the License, or
	(at your option) any later version.
*/

#pragma once

#include <cstdint>
#include <string>

class Bot;
class Client;
class Group;
class NPC;

namespace EQ::ZoneHarness {

struct OwnedBotActorFixtureNames {
	std::string owner_name;
	std::string actor_name;
	std::string primary_target_name;
	std::string secondary_target_name;
};

class OwnedBotActorFixture {
public:
	static constexpr uint32_t kHarnessShamanBotSpellListID = 3010;
	static constexpr uint32_t kHarnessGroupID = 900001;

	bool Create(const OwnedBotActorFixtureNames &names);
	void PrimeOwnedBotEngagement(bool set_actor_target);
	void Cleanup();

	std::string failure_reason;
	std::string database_mutation = "none: synthetic owner, owned bot, group, NPCs, hate, and target state are in-memory only";
	Client *owner = nullptr;
	Bot *actor = nullptr;
	NPC *primary_target = nullptr;
	NPC *secondary_target = nullptr;
	Group *group = nullptr;
};

}

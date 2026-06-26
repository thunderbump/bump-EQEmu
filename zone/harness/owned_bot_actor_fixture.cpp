/*	EQEmu: EQEmulator

	Copyright (C) 2001-2026 EQEmu Development Team

	This program is free software; you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation; either version 3 of the License, or
	(at your option) any later version.
*/

#include "owned_bot_actor_fixture.h"

#include "common/classes.h"
#include "common/races.h"
#include "zone/bot.h"
#include "zone/client.h"
#include "zone/entity.h"
#include "zone/groups.h"
#include "zone/npc.h"

extern EntityList entity_list;

namespace EQ::ZoneHarness {

bool OwnedBotActorFixture::Create(const OwnedBotActorFixtureNames &names)
{
	Cleanup();
	failure_reason.clear();

	owner = new Client();
	owner->TempName(names.owner_name.c_str());
	owner->Mob::SetLevel(60);
	owner->SetHP(10000);
	owner->SetMana(10000);
	owner->GMMove(0.0f, 0.0f, 0.0f, 0.0f);
	entity_list.AddClient(owner);

	auto *bot_type = Bot::CreateDefaultNPCTypeStructForBot(
		names.actor_name.c_str(),
		"",
		60,
		Race::Barbarian,
		Class::Shaman,
		Gender::Male
	);
	bot_type->npc_spells_id = kHarnessShamanBotSpellListID;
	bot_type->Mana = 6000;
	bot_type->max_hp = 5000;
	bot_type->current_hp = 5000;

	actor = new Bot(bot_type, owner);
	actor->GMMove(2.0f, 0.0f, 0.0f, 0.0f);
	actor->SetMana(actor->GetMaxMana());
	actor->SetHP(actor->GetMaxHP());
	actor->SetBotSpellID(kHarnessShamanBotSpellListID);
	actor->LoadDefaultBotSettings();
	for (uint16 spell_type = BotSpellTypes::START; spell_type <= BotSpellTypes::END; ++spell_type) {
		actor->SetSpellTypePriority(
			spell_type,
			BotPriorityCategories::Engaged,
			spell_type == BotSpellTypes::Slow ? 1 : 0
		);
	}

	if (!actor->AI_AddBotSpells(kHarnessShamanBotSpellListID)) {
		failure_reason = "bot_spell_list_unavailable";
		Cleanup();
		return false;
	}

	entity_list.AddBot(actor, false, true);
	actor->AI_Bot_Start();

	group = new Group(owner);
	group->AddMember(actor);
	entity_list.AddGroup(group, kHarnessGroupID);

	auto *target_type = content_db.LoadNPCTypesData(754008);
	if (!target_type) {
		failure_reason = "npc_type_unavailable";
		Cleanup();
		return false;
	}

	primary_target = new NPC(target_type, nullptr, glm::vec4(12, 0, 0, 0), GravityBehavior::Water);
	secondary_target = new NPC(target_type, nullptr, glm::vec4(18, 0, 0, 0), GravityBehavior::Water);
	primary_target->TempName(names.primary_target_name.c_str());
	secondary_target->TempName(names.secondary_target_name.c_str());
	entity_list.AddNPC(primary_target, false, true);
	entity_list.AddNPC(secondary_target, false, true);
	return true;
}

void OwnedBotActorFixture::PrimeOwnedBotEngagement(bool set_actor_target)
{
	if (!owner || !actor || !primary_target || !secondary_target) {
		return;
	}

	owner->SetTarget(primary_target);
	if (set_actor_target) {
		actor->SetTarget(primary_target);
	}

	primary_target->AddToHateList(owner, 100, 1, false);
	primary_target->AddToHateList(actor, 25, 1, false);
	secondary_target->AddToHateList(owner, 25, 1, false);
	secondary_target->AddToHateList(actor, 25, 1, false);
	if (set_actor_target) {
		actor->AddToHateList(primary_target, 100, 1, false);
	}

	entity_list.ScanCloseMobs(actor);
}

void OwnedBotActorFixture::Cleanup()
{
	if (secondary_target) {
		secondary_target->Depop(false);
		secondary_target = nullptr;
	}

	if (primary_target) {
		primary_target->Depop(false);
		primary_target = nullptr;
	}

	if (actor) {
		actor->Depop();
		actor = nullptr;
	}

	if (group) {
		entity_list.RemoveGroup(group->GetID());
		group = nullptr;
	}

	if (owner) {
		entity_list.RemoveMob(owner->GetID());
		owner = nullptr;
	}
}

}

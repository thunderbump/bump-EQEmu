/*	EQEmu: EQEmulator

	Copyright (C) 2001-2026 EQEmu Development Team

	This program is free software; you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation; either version 3 of the License, or
	(at your option) any later version.
*/

#include "owned_bot_actor_fixture.h"

#include "common/spdat.h"
#include "zone/bot.h"
#include "zone/client.h"
#include "zone/entity.h"
#include "zone/groups.h"
#include "zone/npc.h"
#include "zone/zonedb.h"

extern EntityList entity_list;

namespace EQ::ZoneHarness {

namespace {

ActorEventEntity EntityFor(Mob *mob)
{
	if (!mob) {
		return {};
	}

	std::string kind = "mob";
	if (mob->IsClient()) {
		kind = "client";
	}
	else if (mob->IsBot()) {
		kind = "bot";
	}
	else if (mob->IsNPC()) {
		kind = "npc";
	}

	return {
		.entity_id = mob->GetID(),
		.entity_ref = "mob:" + std::to_string(mob->GetID()),
		.name = mob->GetCleanName(),
		.kind = kind,
	};
}

}

OwnedBotActorFixture::~OwnedBotActorFixture()
{
	Reset();
}

bool OwnedBotActorFixture::SetUpOwnedBotGroup(const OwnedBotActorConfig &config)
{
	Reset();

	owner = new Client();
	owner->TempName(config.owner_name.c_str());
	owner->Mob::SetLevel(config.level);
	owner->SetHP(10000);
	owner->SetMana(10000);
	owner->GMMove(0.0f, 0.0f, 0.0f, 0.0f);
	entity_list.AddClient(owner);
	RememberMob(owner);

	auto *bot_type = Bot::CreateDefaultNPCTypeStructForBot(
		config.bot_name,
		"",
		config.level,
		config.race,
		config.bot_class,
		config.gender
	);
	bot_type->npc_spells_id = config.bot_spell_list_id;
	bot_type->Mana = 6000;
	bot_type->max_hp = 5000;
	bot_type->current_hp = 5000;

	bot = new Bot(bot_type, owner);
	bot->GMMove(2.0f, 0.0f, 0.0f, 0.0f);
	bot->SetMana(bot->GetMaxMana());
	bot->SetHP(bot->GetMaxHP());
	bot->SetBotSpellID(config.bot_spell_list_id);
	bot->LoadDefaultBotSettings();
	for (uint16 spell_type = BotSpellTypes::START; spell_type <= BotSpellTypes::END; ++spell_type) {
		bot->SetSpellTypePriority(spell_type, BotPriorityCategories::Engaged, spell_type == BotSpellTypes::Slow ? 1 : 0);
	}

	if (!bot->AI_AddBotSpells(config.bot_spell_list_id)) {
		delete bot;
		bot = nullptr;
		return false;
	}

	entity_list.AddBot(bot, false, true);
	RememberMob(bot);
	bot->AI_Bot_Start();

	group = new Group(owner);
	group->AddMember(bot);
	group_id = 900001;
	entity_list.AddGroup(group, group_id);

	return true;
}

NPC *OwnedBotActorFixture::AddHostileNPC(const HostileNpcConfig &config)
{
	auto *npc_type = content_db.LoadNPCTypesData(754008);
	if (!npc_type) {
		return nullptr;
	}

	auto *hostile = new NPC(npc_type, nullptr, config.position, GravityBehavior::Water);
	hostile->TempName(config.name.c_str());
	entity_list.AddNPC(hostile, false, true);
	RememberMob(hostile);
	return hostile;
}

void OwnedBotActorFixture::EngageHostileWithOwnerGroup(NPC *hostile, int32_t owner_hate, int32_t bot_hate)
{
	if (!hostile) {
		return;
	}

	if (owner && owner_hate > 0) {
		hostile->AddToHateList(owner, owner_hate, 1, false);
	}

	if (bot && bot_hate > 0) {
		hostile->AddToHateList(bot, bot_hate, 1, false);
	}
}

void OwnedBotActorFixture::OwnedBotEngages(Mob *hostile, int32_t hate)
{
	if (bot && hostile && hate > 0) {
		bot->AddToHateList(hostile, hate, 1, false);
	}
}

void OwnedBotActorFixture::RefreshOwnedBotPerception()
{
	if (bot) {
		entity_list.ScanCloseMobs(bot);
	}
}

void OwnedBotActorFixture::Reset()
{
	if (group_id) {
		entity_list.RemoveGroup(group_id);
		group_id = 0;
		group = nullptr;
	}

	for (auto id = mob_ids.rbegin(); id != mob_ids.rend(); ++id) {
		entity_list.RemoveMob(*id);
	}
	mob_ids.clear();
	owner = nullptr;
	bot = nullptr;
}

void OwnedBotActorFixture::OwnerTargets(Mob *target)
{
	if (owner) {
		owner->SetTarget(target);
	}
}

void OwnedBotActorFixture::BotTargets(Mob *target)
{
	if (bot) {
		bot->SetTarget(target);
	}
}

bool OwnedBotActorFixture::MarkHostileSlowed(NPC *hostile, uint16_t slow_spell_id, uint32_t ticks)
{
	if (!hostile || !bot || !IsValidSpell(slow_spell_id)) {
		return false;
	}

	hostile->AddBuff(bot, slow_spell_id, ticks, bot->GetLevel());
	return hostile->FindBuff(slow_spell_id);
}

bool OwnedBotActorFixture::MarkHostileMezzed(NPC *hostile)
{
	if (!hostile) {
		return false;
	}

	hostile->Mesmerize();
	return hostile->IsMezzed();
}

uint16_t OwnedBotActorFixture::FindPreparedSingleTargetSlowSpell(Mob *target) const
{
	if (!bot || !target) {
		return 0;
	}

	const auto slow_spells = Bot::GetPrioritizedBotSpellsBySpellType(bot, BotSpellTypes::Slow, target, false);
	for (const auto &spell: slow_spells) {
		if (IsValidSpell(spell.SpellId) && IsSlowSpell(spell.SpellId) && !IsAnyAESpell(spell.SpellId)) {
			return spell.SpellId;
		}
	}

	return 0;
}

bool OwnedBotActorFixture::IsSingleTargetSlowCastStartFor(const ActorEvent &event, Mob *target) const
{
	return bot &&
		target &&
		event.type == "spell_cast_started" &&
		event.caster.entity_id == bot->GetID() &&
		event.target.has_value() &&
		event.target->entity_id == target->GetID() &&
		event.spell.category == "Slow" &&
		event.spell.targeting == "single";
}

ActorEventEntity OwnedBotActorFixture::Describe(Mob *mob) const
{
	return EntityFor(mob);
}

ActorEventEntity OwnedBotActorFixture::OwnerEntity() const
{
	return Describe(owner);
}

ActorEventEntity OwnedBotActorFixture::OwnedBotEntity() const
{
	return Describe(bot);
}

std::string OwnedBotActorFixture::DatabaseMutationSummary() const
{
	return "none: synthetic owner, owned bot, group, NPCs, hate, and target state are in-memory only";
}

void OwnedBotActorFixture::RememberMob(Mob *mob)
{
	if (mob && mob->GetID()) {
		mob_ids.push_back(mob->GetID());
	}
}

}

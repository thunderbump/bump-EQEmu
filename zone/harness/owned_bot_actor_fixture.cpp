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

#include <algorithm>
#include <utility>

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

bool OwnedBotActorFixture::Create(const OwnedBotActorFixtureNames &names)
{
	failure_reason.clear();

	if (!SetUpOwnedBotGroup({
		.owner_name = names.owner_name,
		.bot_name = names.actor_name,
	})) {
		if (failure_reason.empty()) {
			failure_reason = "bot_spell_list_unavailable";
		}
		return false;
	}

	primary_target = AddHostileNPC({
		.name = names.primary_target_name,
		.position = glm::vec4(12, 0, 0, 0),
	});
	secondary_target = AddHostileNPC({
		.name = names.secondary_target_name,
		.position = glm::vec4(18, 0, 0, 0),
	});
	if (!primary_target || !secondary_target) {
		failure_reason = "npc_type_unavailable";
		Reset();
		return false;
	}

	return true;
}

Bot *OwnedBotActorFixture::CreateOwnedBot(const OwnedBotActorConfig &config)
{
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

	auto *created_bot = new Bot(bot_type, owner);
	created_bot->SetMana(created_bot->GetMaxMana());
	created_bot->SetHP(created_bot->GetMaxHP());
	created_bot->SetBotSpellID(config.bot_spell_list_id);
	created_bot->LoadDefaultBotSettings();
	for (uint16 spell_type = BotSpellTypes::START; spell_type <= BotSpellTypes::END; ++spell_type) {
		created_bot->SetSpellTypePriority(
			spell_type,
			BotPriorityCategories::Engaged,
			spell_type == BotSpellTypes::Slow ? 1 : 0
		);
	}

	if (!created_bot->AI_AddBotSpells(config.bot_spell_list_id)) {
		delete created_bot;
		return nullptr;
	}

	entity_list.AddBot(created_bot, false, true);
	RememberMob(created_bot);
	created_bot->AI_Bot_Start();
	return created_bot;
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

	bot = CreateOwnedBot(config);
	if (!bot) {
		failure_reason = "bot_spell_list_unavailable";
		Reset();
		return false;
	}
	actor = bot;

	bot->GMMove(2.0f, 0.0f, 0.0f, 0.0f);

	group = new Group(owner);
	if (!group->AddMember(bot)) {
		failure_reason = "group_member_add_failed";
		Reset();
		return false;
	}

	entity_list.AddGroup(group);
	group_id = group->GetID();
	if (!group_id) {
		failure_reason = "group_registration_failed";
		Reset();
		return false;
	}

	return true;
}

bool OwnedBotActorFixture::SetUpOwnedBotParty(const OwnedBotPartyConfig &config)
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

	bot = CreateOwnedBot({
		.owner_name = config.owner_name,
		.bot_name = config.actor_leader_name,
		.level = config.level,
		.race = config.race,
		.bot_class = config.leader_class,
		.gender = config.gender,
		.bot_spell_list_id = config.leader_bot_spell_list_id,
	});
	if (!bot) {
		failure_reason = "bot_spell_list_unavailable";
		Reset();
		return false;
	}
	actor = bot;

	bot->GMMove(4.0f, 0.0f, 0.0f, 0.0f);

	const uint8_t bounded_followers = std::clamp<uint8_t>(
		config.follower_count,
		kMinOwnedBotPartyFollowers,
		kMaxOwnedBotPartyFollowers
	);
	std::vector<Bot*> created_followers;
	created_followers.reserve(bounded_followers);

	for (uint8_t index = 0; index < bounded_followers; ++index) {
		auto *follower = CreateOwnedBot({
			.owner_name = config.owner_name,
			.bot_name = config.follower_name_prefix + std::to_string(index + 1),
			.level = config.level,
			.race = config.race,
			.bot_class = config.follower_class,
			.gender = config.gender,
			.bot_spell_list_id = config.follower_bot_spell_list_id,
		});
		if (!follower) {
			failure_reason = "bot_spell_list_unavailable";
			Reset();
			return false;
		}

		follower->GMMove(6.0f + static_cast<float>(index * 2), 2.0f, 0.0f, 0.0f);
		created_followers.push_back(follower);
	}

	group = new Group(owner);
	if (!group->AddMember(bot)) {
		failure_reason = "group_member_add_failed";
		Reset();
		return false;
	}

	for (auto *follower : created_followers) {
		if (!group->AddMember(follower)) {
			failure_reason = "group_member_add_failed";
			Reset();
			return false;
		}
	}

	entity_list.AddGroup(group);
	group_id = group->GetID();
	if (!group_id) {
		failure_reason = "group_registration_failed";
		Reset();
		return false;
	}

	followers = std::move(created_followers);
	SetFollowersFollowActorLeader();

	return true;
}

NPC *OwnedBotActorFixture::AddHostileNPC(const HostileNpcConfig &config)
{
	auto *npc_type = content_db.LoadNPCTypesData(754008);
	if (!npc_type) {
		failure_reason = "npc_type_unavailable";
		return nullptr;
	}

	auto *hostile = new NPC(npc_type, nullptr, config.position, GravityBehavior::Water);
	hostile->TempName(config.name.c_str());
	entity_list.AddNPC(hostile, false, true);
	RememberMob(hostile);
	return hostile;
}

void OwnedBotActorFixture::PrimeOwnedBotEngagement(bool set_actor_target)
{
	if (!owner || !bot || !primary_target || !secondary_target) {
		return;
	}

	OwnerTargets(primary_target);
	if (set_actor_target) {
		BotTargets(primary_target);
	}

	EngageHostileWithOwnerGroup(primary_target, 100, 25);
	EngageHostileWithOwnerGroup(secondary_target, 25, 25);
	if (set_actor_target) {
		OwnedBotEngages(primary_target, 100);
	}

	RefreshOwnedBotPerception();
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

void OwnedBotActorFixture::EngageHostileWithParty(NPC *hostile, int32_t owner_hate, int32_t bot_hate)
{
	EngageHostileWithOwnerGroup(hostile, owner_hate, bot_hate);

	if (!hostile || bot_hate <= 0) {
		return;
	}

	for (auto *follower : followers) {
		if (follower) {
			hostile->AddToHateList(follower, bot_hate, 1, false);
		}
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

void OwnedBotActorFixture::RefreshPerception(Bot *actor)
{
	if (actor) {
		entity_list.ScanCloseMobs(actor);
	}
}

void OwnedBotActorFixture::RefreshPartyPerception()
{
	RefreshOwnedBotPerception();
	for (auto *follower : followers) {
		RefreshPerception(follower);
	}
}

bool OwnedBotActorFixture::RemoveMob(Mob *mob)
{
	if (!mob) {
		return false;
	}

	const auto entity_id = mob->GetID();
	const bool removed_owner = owner == mob;
	const bool removed_bot = bot == mob;
	const bool removed_actor = actor == mob;
	const bool removed_primary_target = primary_target == mob;
	const bool removed_secondary_target = secondary_target == mob;
	if (!entity_list.RemoveMob(entity_id)) {
		return false;
	}

	mob_ids.erase(std::remove(mob_ids.begin(), mob_ids.end(), entity_id), mob_ids.end());

	if (removed_owner) {
		owner = nullptr;
	}

	if (removed_bot) {
		bot = nullptr;
	}

	if (removed_actor) {
		actor = nullptr;
	}

	if (removed_primary_target) {
		primary_target = nullptr;
	}

	if (removed_secondary_target) {
		secondary_target = nullptr;
	}

	followers.erase(
		std::remove_if(
			followers.begin(),
			followers.end(),
			[mob](Bot *follower) {
				return follower == mob;
			}
		),
		followers.end()
	);

	return true;
}

void OwnedBotActorFixture::Reset()
{
	if (group_id) {
		entity_list.RemoveGroup(group_id);
		group_id = 0;
		group = nullptr;
	}
	else if (group) {
		delete group;
		group = nullptr;
	}

	for (auto id = mob_ids.rbegin(); id != mob_ids.rend(); ++id) {
		entity_list.RemoveMob(*id);
	}
	mob_ids.clear();
	owner = nullptr;
	bot = nullptr;
	actor = nullptr;
	primary_target = nullptr;
	secondary_target = nullptr;
	followers.clear();
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

void OwnedBotActorFixture::BotTargets(Bot *actor, Mob *target)
{
	if (actor) {
		actor->SetTarget(target);
	}
}

void OwnedBotActorFixture::SetBotCommandTargetSource(Bot *actor, Mob *source)
{
	if (actor) {
		actor->SetCommandTargetSource(source);
	}
}

void OwnedBotActorFixture::SetBotLeashSource(Bot *actor, Mob *source)
{
	if (actor) {
		actor->SetLeashSource(source);
	}
}

void OwnedBotActorFixture::SetBotFollowTarget(Bot *actor, Mob *target)
{
	if (actor && target) {
		actor->SetFollowID(target->GetID());
	}
}

void OwnedBotActorFixture::SetFollowersFollowActorLeader()
{
	for (auto *follower : followers) {
		SetBotFollowTarget(follower, bot);
	}
}

void OwnedBotActorFixture::SetBotAttackFlag(Bot *actor, bool enabled)
{
	if (actor) {
		actor->SetAttackFlag(enabled);
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
	return FindPreparedSingleTargetSlowSpell(bot, target);
}

uint16_t OwnedBotActorFixture::FindPreparedSingleTargetSlowSpell(Bot *actor, Mob *target) const
{
	if (!actor || !target) {
		return 0;
	}

	const auto slow_spells = Bot::GetPrioritizedBotSpellsBySpellType(actor, BotSpellTypes::Slow, target, false);
	for (const auto &spell: slow_spells) {
		if (IsValidSpell(spell.SpellId) && IsSlowSpell(spell.SpellId) && !IsAnyAESpell(spell.SpellId)) {
			return spell.SpellId;
		}
	}

	return 0;
}

bool OwnedBotActorFixture::IsSingleTargetSlowCastStartFor(const ActorEvent &event, Mob *target) const
{
	return IsSingleTargetSlowCastStartFor(bot, event, target);
}

bool OwnedBotActorFixture::IsSingleTargetSlowCastStartFor(Bot *actor, const ActorEvent &event, Mob *target) const
{
	return actor &&
		target &&
		event.type == "spell_cast_started" &&
		event.caster.entity_id == actor->GetID() &&
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
	return database_mutation;
}

void OwnedBotActorFixture::RememberMob(Mob *mob)
{
	if (mob && mob->GetID()) {
		mob_ids.push_back(mob->GetID());
	}
}

}

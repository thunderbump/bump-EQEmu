/*	EQEmu: EQEmulator

	Copyright (C) 2001-2026 EQEmu Development Team

	This program is free software; you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation; either version 3 of the License, or
	(at your option) any later version.
*/

#include "zone_harness_runtime.h"

#include "common/classes.h"
#include "common/races.h"
#include "common/timer.h"
#include "zone/bot.h"
#include "zone/client.h"
#include "zone/entity.h"
#include "zone/groups.h"
#include "zone/npc.h"
#include "zone/questmgr.h"
#include "zone/worldserver.h"
#include "zone/zone.h"
#include "zone/zone_event_scheduler.h"

#include <algorithm>
#include <thread>

extern EntityList entity_list;
extern WorldServer worldserver;
extern Zone *zone;
extern volatile bool is_zone_loaded;

namespace EQ::ZoneHarness {

bool ZoneHarnessRuntime::Boot(const std::string &zone_short_name, uint32_t instance_id)
{
	std::lock_guard lock(mutex);

	if (booted && zone && is_zone_loaded) {
		return zone_short_name == zone->GetShortName() && instance_id == zone->GetInstanceID();
	}

	if (ZoneID(zone_short_name.c_str()) == 0) {
		return false;
	}

	if (!Zone::Bootup(ZoneID(zone_short_name.c_str()), instance_id, false)) {
		return false;
	}

	if (zone) {
		zone->StopShutdownTimer();
		zone->SetSaveZoneState(false);
	}

	entity_list.Process();
	entity_list.MobProcess();

	started_at = std::chrono::steady_clock::now();
	booted = true;
	shutdown_requested = false;
	process_ticks = 0;
	ActorEventRecorder::RegisterActiveRecorder(&events);
	return true;
}

HealthSnapshot ZoneHarnessRuntime::Health()
{
	std::lock_guard lock(mutex);

	const auto runtime = RuntimeLocked();
	const bool loaded = runtime.booted && runtime.zone.loaded;
	return {
		.healthy = loaded && !runtime.shutdown_requested,
		.status = runtime.shutdown_requested ? "shutdown_requested" : (loaded ? "ok" : "not_booted"),
		.runtime = runtime,
	};
}

RuntimeSnapshot ZoneHarnessRuntime::Runtime()
{
	std::lock_guard lock(mutex);
	return RuntimeLocked();
}

ZoneIdentitySnapshot ZoneHarnessRuntime::ZoneIdentity()
{
	std::lock_guard lock(mutex);
	return snapshots.ZoneIdentity();
}

EntitySnapshot ZoneHarnessRuntime::Entities(uint32_t sample_limit)
{
	std::lock_guard lock(mutex);
	return snapshots.Entities(sample_limit);
}

ProcessResult ZoneHarnessRuntime::ProcessWorld(uint32_t ticks)
{
	std::lock_guard lock(mutex);

	const uint32_t bounded_ticks = std::min<uint32_t>(std::max<uint32_t>(ticks, 1), 1000);
	uint32_t processed = 0;

	for (; processed < bounded_ticks; ++processed) {
		ProcessOneTick();
		++process_ticks;
	}

	return {
		.ticks_requested = ticks,
		.ticks_processed = processed,
		.runtime = RuntimeLocked(),
	};
}

std::vector<ActorEvent> ZoneHarnessRuntime::DrainEvents()
{
	std::lock_guard lock(mutex);
	return events.Drain();
}

std::vector<ActorEvent> ZoneHarnessRuntime::EventsSince(uint64_t since_id, size_t limit)
{
	std::lock_guard lock(mutex);
	return events.Since(since_id, limit);
}

namespace {

constexpr uint32_t kHarnessShamanBotSpellListID = 3010;

std::string ScenarioName(BotSlowMaintenanceScenarioKind scenario)
{
	switch (scenario) {
		case BotSlowMaintenanceScenarioKind::CurrentTarget:
			return "current-target";
		case BotSlowMaintenanceScenarioKind::Fallback:
			return "fallback";
		case BotSlowMaintenanceScenarioKind::Mezzed:
			return "mezzed";
	}

	return "unknown";
}

ActorEventEntity ScenarioEntityFor(Mob *mob)
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

bool IsExpectedSlowCast(const ActorEvent &event, uint16_t bot_id, uint16_t target_id)
{
	return event.type == "spell_cast_started" &&
		event.caster.entity_id == bot_id &&
		event.target.has_value() &&
		event.target->entity_id == target_id &&
		event.spell.category == "Slow" &&
		event.spell.targeting == "single";
}

uint16_t FindPreparedSingleTargetSlowSpell(Bot *bot, Mob *target)
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

}

SpellCastStartScenarioResult ZoneHarnessRuntime::StartKnownSpellCast(uint16_t spell_id)
{
	std::lock_guard lock(mutex);

	if (!booted || !zone || !is_zone_loaded) {
		return {
			.started = false,
			.reason = "zone_not_booted",
			.spell_id = spell_id,
			.runtime = RuntimeLocked(),
		};
	}

	if (!IsValidSpell(spell_id)) {
		return {
			.started = false,
			.reason = "invalid_spell",
			.spell_id = spell_id,
			.runtime = RuntimeLocked(),
		};
	}

	auto *caster_type = content_db.LoadNPCTypesData(754008);
	auto *target_type = content_db.LoadNPCTypesData(754008);
	if (!caster_type || !target_type) {
		return {
			.started = false,
			.reason = "npc_type_unavailable",
			.spell_id = spell_id,
			.runtime = RuntimeLocked(),
		};
	}

	auto *caster = new NPC(caster_type, nullptr, glm::vec4(0, 0, 0, 0), GravityBehavior::Water);
	auto *target = new NPC(target_type, nullptr, glm::vec4(5, 0, 0, 0), GravityBehavior::Water);
	caster->TempName("HarnessCastStartCaster");
	target->TempName("HarnessCastStartTarget");
	entity_list.AddNPC(caster, false, true);
	entity_list.AddNPC(target, false, true);
	caster->SetTarget(target);

	const bool started = caster->CastSpell(spell_id, target->GetID(), EQ::spells::CastingSlot::Gem2);
	if (!started) {
		caster->Depop(false);
		target->Depop(false);
	}

	return {
		.started = started,
		.reason = started ? "cast_started" : "cast_not_started",
		.caster_id = caster->GetID(),
		.target_id = target->GetID(),
		.spell_id = spell_id,
		.runtime = RuntimeLocked(),
	};
}

BotSlowMaintenanceScenarioResult ZoneHarnessRuntime::RunBotSlowMaintenanceCurrentTarget(uint32_t max_ticks, uint32_t sleep_ms)
{
	return RunBotSlowMaintenanceScenario(BotSlowMaintenanceScenarioKind::CurrentTarget, max_ticks, sleep_ms);
}

BotSlowMaintenanceScenarioResult ZoneHarnessRuntime::RunBotSlowMaintenanceFallback(uint32_t max_ticks, uint32_t sleep_ms)
{
	return RunBotSlowMaintenanceScenario(BotSlowMaintenanceScenarioKind::Fallback, max_ticks, sleep_ms);
}

BotSlowMaintenanceScenarioResult ZoneHarnessRuntime::RunBotSlowMaintenanceMezzed(uint32_t max_ticks, uint32_t sleep_ms)
{
	return RunBotSlowMaintenanceScenario(BotSlowMaintenanceScenarioKind::Mezzed, max_ticks, sleep_ms);
}

BotSlowMaintenanceScenarioResult ZoneHarnessRuntime::RunBotSlowMaintenanceScenario(
	BotSlowMaintenanceScenarioKind scenario,
	uint32_t max_ticks,
	uint32_t sleep_ms
)
{
	std::unique_lock lock(mutex);

	BotSlowMaintenanceScenarioResult result{
		.reason = "not_run",
		.scenario = ScenarioName(scenario),
		.database_mutation = "none: synthetic owner, owned bot, group, NPCs, hate, and target state are in-memory only",
	};

	if (!booted || !zone || !is_zone_loaded) {
		result.reason = "zone_not_booted";
		result.runtime = RuntimeLocked();
		return result;
	}

	auto *owner = new Client();
	owner->TempName("HarnessSlowOwner");
	owner->Mob::SetLevel(60);
	owner->SetHP(10000);
	owner->SetMana(10000);
	owner->GMMove(0.0f, 0.0f, 0.0f, 0.0f);
	entity_list.AddClient(owner);

	auto *bot_type = Bot::CreateDefaultNPCTypeStructForBot(
		"HarnessSlowBot",
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

	auto *bot = new Bot(bot_type, owner);
	bot->GMMove(2.0f, 0.0f, 0.0f, 0.0f);
	bot->SetMana(bot->GetMaxMana());
	bot->SetHP(bot->GetMaxHP());
	bot->SetBotSpellID(kHarnessShamanBotSpellListID);
	bot->LoadDefaultBotSettings();
	for (uint16 spell_type = BotSpellTypes::START; spell_type <= BotSpellTypes::END; ++spell_type) {
		bot->SetSpellTypePriority(spell_type, BotPriorityCategories::Engaged, spell_type == BotSpellTypes::Slow ? 1 : 0);
	}

	if (!bot->AI_AddBotSpells(kHarnessShamanBotSpellListID)) {
		result.reason = "bot_spell_list_unavailable";
		result.owner = ScenarioEntityFor(owner);
		result.bot = ScenarioEntityFor(bot);

		delete bot;
		entity_list.RemoveMob(owner->GetID());

		result.runtime = RuntimeLocked();
		return result;
	}

	entity_list.AddBot(bot, false, true);
	bot->AI_Bot_Start();

	auto *group = new Group(owner);
	group->AddMember(bot);
	entity_list.AddGroup(group, 900001);

	auto *target_type = content_db.LoadNPCTypesData(754008);
	if (!target_type) {
		result.reason = "npc_type_unavailable";
		result.owner = ScenarioEntityFor(owner);
		result.bot = ScenarioEntityFor(bot);
		result.runtime = RuntimeLocked();
		bot->Depop();
		return result;
	}

	auto *current_target = new NPC(target_type, nullptr, glm::vec4(12, 0, 0, 0), GravityBehavior::Water);
	auto *secondary_hostile = new NPC(target_type, nullptr, glm::vec4(18, 0, 0, 0), GravityBehavior::Water);
	current_target->TempName(
		scenario == BotSlowMaintenanceScenarioKind::Mezzed ?
			"HarnessSlowMezzedCurrentTarget" :
			"HarnessSlowCurrentTarget"
	);
	secondary_hostile->TempName(
		scenario == BotSlowMaintenanceScenarioKind::Fallback ?
			"HarnessSlowFallbackHostile" :
			"HarnessSlowSecondaryHostile"
	);
	entity_list.AddNPC(current_target, false, true);
	entity_list.AddNPC(secondary_hostile, false, true);

	owner->SetTarget(current_target);
	bot->SetTarget(current_target);
	current_target->AddToHateList(owner, 100, 1, false);
	current_target->AddToHateList(bot, 25, 1, false);
	secondary_hostile->AddToHateList(owner, 25, 1, false);
	secondary_hostile->AddToHateList(bot, 25, 1, false);
	bot->AddToHateList(current_target, 100, 1, false);
	entity_list.ScanCloseMobs(bot);

	result.slow_spell_id = FindPreparedSingleTargetSlowSpell(bot, current_target);
	if (!result.slow_spell_id) {
		result.reason = "single_target_slow_spell_unavailable";
		result.owner = ScenarioEntityFor(owner);
		result.bot = ScenarioEntityFor(bot);
		result.current_target = ScenarioEntityFor(current_target);
		result.secondary_hostile = ScenarioEntityFor(secondary_hostile);
		result.runtime = RuntimeLocked();
		return result;
	}

	Mob *expected_target = current_target;
	if (scenario == BotSlowMaintenanceScenarioKind::Fallback) {
		current_target->AddBuff(bot, result.slow_spell_id, 600, bot->GetLevel());
		result.current_target_slowed = current_target->FindBuff(result.slow_spell_id);
		expected_target = secondary_hostile;
	}
	else if (scenario == BotSlowMaintenanceScenarioKind::Mezzed) {
		current_target->Mesmerize();
		result.mezzed_hostile_mezzed = current_target->IsMezzed();
		result.mezzed_hostile = ScenarioEntityFor(current_target);
		expected_target = secondary_hostile;
	}

	result.owner = ScenarioEntityFor(owner);
	result.bot = ScenarioEntityFor(bot);
	result.current_target = ScenarioEntityFor(current_target);
	result.expected_target = ScenarioEntityFor(expected_target);
	result.secondary_hostile = ScenarioEntityFor(secondary_hostile);

	const uint64_t since_event_id = events.MaxEventID();
	const uint16_t bot_id = bot->GetID();
	const uint16_t expected_target_id = expected_target->GetID();
	const uint32_t bounded_ticks = std::clamp<uint32_t>(max_ticks, 1, 1000);
	const uint32_t bounded_sleep_ms = std::min<uint32_t>(sleep_ms, 250);
	const auto started = std::chrono::steady_clock::now();

	for (uint32_t tick = 0; tick < bounded_ticks; ++tick) {
		if (!booted || !zone || !is_zone_loaded || shutdown_requested) {
			result.reason = "zone_unavailable_during_scenario";
			break;
		}

		ProcessOneTick();
		++process_ticks;
		++result.ticks_processed;

		result.events = events.Since(since_event_id, 100);
		const auto observed = std::find_if(
			result.events.begin(),
			result.events.end(),
			[bot_id, expected_target_id](const ActorEvent &event) {
				return IsExpectedSlowCast(event, bot_id, expected_target_id);
			}
		);

		if (observed != result.events.end()) {
			result.observed = true;
			result.reason = "observed_expected_single_target_slow_cast_start";
			break;
		}

		if (bounded_sleep_ms > 0) {
			lock.unlock();
			std::this_thread::sleep_for(std::chrono::milliseconds(bounded_sleep_ms));
			lock.lock();
		}
	}

	result.elapsed_ms = static_cast<uint32_t>(
		std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - started).count()
	);
	if (!result.observed && result.reason == "not_run") {
		result.reason = "expected_slow_cast_start_not_observed_within_bounds";
		result.events = events.Since(since_event_id, 100);
	}
	result.runtime = RuntimeLocked();
	return result;
}

void ZoneHarnessRuntime::RequestShutdown()
{
	std::lock_guard lock(mutex);
	shutdown_requested = true;
}

void ZoneHarnessRuntime::Shutdown()
{
	std::lock_guard lock(mutex);
	shutdown_requested = true;
	ActorEventRecorder::ClearActiveRecorder(&events);

	entity_list.Clear();
	entity_list.RemoveAllEncounters();

	if (zone) {
		zone->SetSaveZoneState(false);
		zone->Shutdown(true);
		zone = nullptr;
	}
}

RuntimeSnapshot ZoneHarnessRuntime::RuntimeLocked() const
{
	const auto elapsed = booted ?
		std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - started_at).count() :
		0;

	return {
		.booted = booted,
		.shutdown_requested = shutdown_requested,
		.uptime_ms = static_cast<uint64_t>(elapsed),
		.process_ticks = process_ticks,
		.pending_events = events.PendingCount(),
		.max_event_id = events.MaxEventID(),
		.zone = snapshots.ZoneIdentity(),
	};
}

void ZoneHarnessRuntime::ProcessOneTick()
{
	::Timer::SetCurrentTime();
	worldserver.Process();

	if (!is_zone_loaded) {
		return;
	}

	entity_list.GroupProcess();
	entity_list.DoorProcess();
	entity_list.ObjectProcess();
	entity_list.CorpseProcess();
	entity_list.TrapProcess();
	entity_list.RaidProcess();
	entity_list.Process();
	entity_list.MobProcess();
	entity_list.BeaconProcess();
	entity_list.EncounterProcess();

	ZoneEventScheduler::Instance()->Process(zone, WorldContentService::Instance());

	if (zone && !zone->Process()) {
		zone->Shutdown();
	}

	quest_manager.Process();
}

}

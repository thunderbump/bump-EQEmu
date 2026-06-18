/*	EQEmu: EQEmulator

	Copyright (C) 2001-2026 EQEmu Development Team

	This program is free software; you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation; either version 3 of the License, or
	(at your option) any later version.
*/

#include "zone_harness_runtime.h"

#include "common/spdat.h"
#include "common/timer.h"
#include "zone/entity.h"
#include "zone/npc.h"
#include "zone/harness/owned_bot_actor_fixture.h"
#include "zone/worldserver.h"
#include "zone/zone.h"
#include "zone/zone_event_scheduler.h"
#include "zone/zonedb.h"

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
	};
	OwnedBotActorFixture fixture;
	result.database_mutation = fixture.DatabaseMutationSummary();

	if (!booted || !zone || !is_zone_loaded) {
		result.reason = "zone_not_booted";
		result.runtime = RuntimeLocked();
		return result;
	}

	if (!fixture.SetUpOwnedBotGroup({
		.owner_name = "HarnessSlowOwner",
		.bot_name = "HarnessSlowBot",
	})) {
		result.reason = "bot_spell_list_unavailable";
		result.owner = fixture.OwnerEntity();
		result.bot = fixture.OwnedBotEntity();
		result.runtime = RuntimeLocked();
		return result;
	}

	auto *current_target = fixture.AddHostileNPC({
		.name = scenario == BotSlowMaintenanceScenarioKind::Mezzed ?
			"HarnessSlowMezzedCurrentTarget" :
			"HarnessSlowCurrentTarget",
		.position = glm::vec4(12, 0, 0, 0),
	});
	auto *secondary_hostile = fixture.AddHostileNPC({
		.name = scenario == BotSlowMaintenanceScenarioKind::Fallback ?
			"HarnessSlowFallbackHostile" :
			"HarnessSlowSecondaryHostile",
		.position = glm::vec4(18, 0, 0, 0),
	});
	if (!current_target || !secondary_hostile) {
		result.reason = "npc_type_unavailable";
		result.owner = fixture.OwnerEntity();
		result.bot = fixture.OwnedBotEntity();
		result.runtime = RuntimeLocked();
		return result;
	}

	fixture.OwnerTargets(current_target);
	fixture.BotTargets(current_target);
	fixture.EngageHostileWithOwnerGroup(current_target, 100, 25);
	fixture.EngageHostileWithOwnerGroup(secondary_hostile, 25, 25);
	fixture.OwnedBotEngages(current_target, 100);
	fixture.RefreshOwnedBotPerception();

	result.slow_spell_id = fixture.FindPreparedSingleTargetSlowSpell(current_target);
	if (!result.slow_spell_id) {
		result.reason = "single_target_slow_spell_unavailable";
		result.owner = fixture.OwnerEntity();
		result.bot = fixture.OwnedBotEntity();
		result.current_target = fixture.Describe(current_target);
		result.secondary_hostile = fixture.Describe(secondary_hostile);
		result.runtime = RuntimeLocked();
		return result;
	}

	Mob *expected_target = current_target;
	if (scenario == BotSlowMaintenanceScenarioKind::Fallback) {
		result.current_target_slowed = fixture.MarkHostileSlowed(current_target, result.slow_spell_id);
		expected_target = secondary_hostile;
	}
	else if (scenario == BotSlowMaintenanceScenarioKind::Mezzed) {
		result.mezzed_hostile_mezzed = fixture.MarkHostileMezzed(current_target);
		result.mezzed_hostile = fixture.Describe(current_target);
		expected_target = secondary_hostile;
	}

	result.owner = fixture.OwnerEntity();
	result.bot = fixture.OwnedBotEntity();
	result.current_target = fixture.Describe(current_target);
	result.expected_target = fixture.Describe(expected_target);
	result.secondary_hostile = fixture.Describe(secondary_hostile);

	const uint64_t since_event_id = events.MaxEventID();
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
			[&fixture, expected_target](const ActorEvent &event) {
				return fixture.IsSingleTargetSlowCastStartFor(event, expected_target);
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

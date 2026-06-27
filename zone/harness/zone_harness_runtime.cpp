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
#include <cmath>
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

std::vector<ActorEventEntity> DescribeBots(
	const OwnedBotActorFixture &fixture,
	const std::vector<Bot*> &bots
)
{
	std::vector<ActorEventEntity> entities;
	entities.reserve(bots.size());
	for (auto *bot : bots) {
		entities.push_back(fixture.Describe(bot));
	}

	return entities;
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

ActorLedBotPartyScenarioResult ZoneHarnessRuntime::RunActorLedBotPartyProof(
	uint8_t follower_count,
	uint32_t max_ticks,
	uint32_t sleep_ms
)
{
	std::unique_lock lock(mutex);

	ActorLedBotPartyScenarioResult result{
		.follower_count_requested = follower_count,
	};

	if (!booted || !zone || !is_zone_loaded) {
		result.reason = "zone_not_booted";
		result.runtime = RuntimeLocked();
		return result;
	}

	const uint8_t bounded_followers = std::clamp<uint8_t>(follower_count, 1, 5);
	const uint32_t bounded_ticks = std::clamp<uint32_t>(max_ticks, 1, 1000);
	const uint32_t bounded_sleep_ms = std::min<uint32_t>(sleep_ms, 250);
	const auto started = std::chrono::steady_clock::now();

	auto bounded_sleep = [&]() {
		if (bounded_sleep_ms == 0) {
			return;
		}

		lock.unlock();
		std::this_thread::sleep_for(std::chrono::milliseconds(bounded_sleep_ms));
		lock.lock();
	};

	{
		OwnedBotActorFixture fixture;
		result.database_mutation = fixture.DatabaseMutationSummary();

		if (!fixture.SetUpOwnedBotParty({
			.owner_name = "HarnessPartyOwner",
			.actor_leader_name = "HarnessActorLeader",
			.follower_name_prefix = "HarnessFollower",
			.follower_count = bounded_followers,
		})) {
			result.reason = "party_setup_failed";
			result.runtime = RuntimeLocked();
			return result;
		}

		result.owner = fixture.OwnerEntity();
		result.group_leader = fixture.Describe(fixture.ActorGroup() ? fixture.ActorGroup()->GetLeader() : nullptr);
		result.actor_leader = fixture.ActorLeaderEntity();
		result.followers = DescribeBots(fixture, fixture.FollowerBots());
		result.follower_count_created = static_cast<uint8_t>(fixture.FollowerBots().size());

		auto *group = fixture.ActorGroup();
		auto *actor_leader = fixture.ActorLeader();
		auto *owner = fixture.Owner();
		if (!group || !actor_leader || !owner || fixture.FollowerBots().empty()) {
			result.reason = "party_setup_incomplete";
			result.runtime = RuntimeLocked();
			return result;
		}

		result.all_bots_share_owner = actor_leader->GetBotOwner() == owner;
		for (auto *follower : fixture.FollowerBots()) {
			result.all_bots_share_owner = result.all_bots_share_owner && follower && follower->GetBotOwner() == owner;
		}

		fixture.SetFollowersFollowActorLeader();
		group->ChangeLeader(actor_leader);
		result.group_leader_change_to_actor_rejected =
			group->GetLeader() == owner &&
			group->GetLeader() != actor_leader;

		for (uint32_t tick = 0; tick < std::min<uint32_t>(bounded_ticks, 16); ++tick) {
			if (!booted || !zone || !is_zone_loaded || shutdown_requested) {
				result.reason = "zone_unavailable_during_follow_phase";
				result.runtime = RuntimeLocked();
				return result;
			}

			ProcessOneTick();
			++process_ticks;
			++result.ticks_processed;
			bounded_sleep();
		}

		result.followers_follow_actor_leader = true;
		for (auto *follower : fixture.FollowerBots()) {
			result.followers_follow_actor_leader =
				result.followers_follow_actor_leader &&
				follower &&
				follower->GetFollowID() == actor_leader->GetID();
		}
	}

	{
		OwnedBotActorFixture fixture;
		if (!fixture.SetUpOwnedBotParty({
			.owner_name = "HarnessPartyOwner",
			.actor_leader_name = "HarnessActorLeader",
			.follower_name_prefix = "HarnessFollower",
			.follower_count = bounded_followers,
		})) {
			result.reason = "owner_target_setup_failed";
			result.runtime = RuntimeLocked();
			return result;
		}

		auto *probe_follower = fixture.FollowerBots().front();
		auto *hostile = fixture.AddHostileNPC({
			.name = "HarnessOwnerTargetHostile",
			.position = glm::vec4(12, 0, 0, 0),
		});
		if (!probe_follower || !hostile) {
			result.reason = "owner_target_fixture_incomplete";
			result.runtime = RuntimeLocked();
			return result;
		}

		result.slow_spell_id = fixture.FindPreparedSingleTargetSlowSpell(probe_follower, hostile);
		if (!result.slow_spell_id) {
			result.reason = "follower_slow_spell_unavailable";
			result.runtime = RuntimeLocked();
			return result;
		}

		fixture.OwnerTargets(hostile);
		fixture.SetBotAttackFlag(probe_follower);
		fixture.RefreshPerception(probe_follower);

		const uint64_t since_event_id = events.MaxEventID();
		result.owner_target_reason = "owner_target_slow_cast_not_observed_within_bounds";
		for (uint32_t tick = 0; tick < bounded_ticks; ++tick) {
			if (!booted || !zone || !is_zone_loaded || shutdown_requested) {
				result.reason = "zone_unavailable_during_owner_target_phase";
				result.runtime = RuntimeLocked();
				return result;
			}

			ProcessOneTick();
			++process_ticks;
			++result.ticks_processed;

			result.owner_target_events = events.Since(since_event_id, 100);
			const auto observed = std::find_if(
				result.owner_target_events.begin(),
				result.owner_target_events.end(),
				[&fixture, probe_follower, hostile](const ActorEvent &event) {
					return fixture.IsSingleTargetSlowCastStartFor(probe_follower, event, hostile);
				}
			);

			if (observed != result.owner_target_events.end()) {
				result.owner_target_command_observed = true;
				result.owner_target_reason = "owner_target_drove_follower_slow_cast";
				break;
			}

			bounded_sleep();
		}

		if (!result.owner_target_command_observed) {
			result.reason = "owner_target_phase_failed";
			result.runtime = RuntimeLocked();
			return result;
		}
	}

	{
		OwnedBotActorFixture fixture;
		if (!fixture.SetUpOwnedBotParty({
			.owner_name = "HarnessPartyOwner",
			.actor_leader_name = "HarnessActorLeader",
			.follower_name_prefix = "HarnessFollower",
			.follower_count = bounded_followers,
		})) {
			result.reason = "actor_target_setup_failed";
			result.runtime = RuntimeLocked();
			return result;
		}

		auto *actor_leader = fixture.ActorLeader();
		auto *probe_follower = fixture.FollowerBots().front();
		auto *hostile = fixture.AddHostileNPC({
			.name = "HarnessActorTargetHostile",
			.position = glm::vec4(12, 0, 0, 0),
		});
		if (!actor_leader || !probe_follower || !hostile) {
			result.reason = "actor_target_fixture_incomplete";
			result.runtime = RuntimeLocked();
			return result;
		}

		fixture.BotTargets(actor_leader, hostile);
		fixture.OwnedBotEngages(hostile, 100);
		hostile->AddToHateList(actor_leader, 100, 1, false);
		fixture.SetBotAttackFlag(probe_follower);
		fixture.RefreshPartyPerception();

		const uint64_t since_event_id = events.MaxEventID();
		result.actor_target_reason = "actor_target_did_not_source_follower_action_within_bounds";
		for (uint32_t tick = 0; tick < bounded_ticks; ++tick) {
			if (!booted || !zone || !is_zone_loaded || shutdown_requested) {
				result.reason = "zone_unavailable_during_actor_target_phase";
				result.runtime = RuntimeLocked();
				return result;
			}

			ProcessOneTick();
			++process_ticks;
			++result.ticks_processed;

			result.actor_target_events = events.Since(since_event_id, 100);
			const auto observed = std::find_if(
				result.actor_target_events.begin(),
				result.actor_target_events.end(),
				[&fixture, probe_follower, hostile](const ActorEvent &event) {
					return fixture.IsSingleTargetSlowCastStartFor(probe_follower, event, hostile);
				}
			);

			if (observed != result.actor_target_events.end() || probe_follower->GetTarget()) {
				result.actor_target_reason = "actor_target_unexpectedly_drove_follower_action";
				break;
			}

			bounded_sleep();
		}

		result.actor_target_command_blocked =
			!probe_follower->GetTarget() &&
			std::none_of(
				result.actor_target_events.begin(),
				result.actor_target_events.end(),
				[&fixture, probe_follower, hostile](const ActorEvent &event) {
					return fixture.IsSingleTargetSlowCastStartFor(probe_follower, event, hostile);
				}
			);
		if (!result.actor_target_command_blocked) {
			result.reason = "actor_target_phase_unexpectedly_succeeded";
			result.runtime = RuntimeLocked();
			return result;
		}
	}

	{
		OwnedBotActorFixture fixture;
		if (!fixture.SetUpOwnedBotParty({
			.owner_name = "HarnessPartyOwner",
			.actor_leader_name = "HarnessActorLeader",
			.follower_name_prefix = "HarnessFollower",
			.follower_count = bounded_followers,
		})) {
			result.reason = "leash_setup_failed";
			result.runtime = RuntimeLocked();
			return result;
		}

		auto *owner = fixture.Owner();
		auto *actor_leader = fixture.ActorLeader();
		auto *probe_follower = fixture.FollowerBots().front();
		if (!owner || !actor_leader || !probe_follower) {
			result.reason = "leash_fixture_incomplete";
			result.runtime = RuntimeLocked();
			return result;
		}

		const float leash_radius = std::sqrt(std::max<float>(RuleR(Bots, LeashDistance), 100.0f)) + 25.0f;
		owner->GMMove(0.0f, 0.0f, 0.0f, 0.0f);
		actor_leader->GMMove(leash_radius, 0.0f, 0.0f, 0.0f);
		probe_follower->GMMove(leash_radius + 4.0f, 0.0f, 0.0f, 0.0f);
		fixture.SetBotFollowTarget(probe_follower, actor_leader);

		auto *hostile = fixture.AddHostileNPC({
			.name = "HarnessActorLeashHostile",
			.position = glm::vec4(leash_radius + 8.0f, 0.0f, 0.0f, 0.0f),
		});
		if (!hostile) {
			result.reason = "leash_hostile_unavailable";
			result.runtime = RuntimeLocked();
			return result;
		}

		probe_follower->AddToHateList(hostile, 100, 1, false);
		hostile->AddToHateList(probe_follower, 100, 1, false);
		fixture.BotTargets(probe_follower, hostile);
		fixture.RefreshPerception(probe_follower);

		result.leash_reason = "owner_client_leash_did_not_clear_actor_led_combat_target";
		for (uint32_t tick = 0; tick < std::min<uint32_t>(bounded_ticks, 8); ++tick) {
			if (!booted || !zone || !is_zone_loaded || shutdown_requested) {
				result.reason = "zone_unavailable_during_leash_phase";
				result.runtime = RuntimeLocked();
				return result;
			}

			ProcessOneTick();
			++process_ticks;
			++result.ticks_processed;

			if (!probe_follower->GetTarget()) {
				result.owner_leash_blocks_actor_led_combat = probe_follower->GetFollowID() == actor_leader->GetID();
				result.leash_reason = result.owner_leash_blocks_actor_led_combat ?
					"owner_client_leash_cleared_combat_target_while_follow_anchor_stayed_actor_leader" :
					"owner_client_leash_cleared_combat_target";
				break;
			}

			bounded_sleep();
		}

		if (!result.owner_leash_blocks_actor_led_combat) {
			result.reason = "leash_phase_failed";
			result.runtime = RuntimeLocked();
			return result;
		}
	}

	result.proved =
		result.all_bots_share_owner &&
		result.group_leader_change_to_actor_rejected &&
		result.followers_follow_actor_leader &&
		result.owner_target_command_observed &&
		result.actor_target_command_blocked &&
		result.owner_leash_blocks_actor_led_combat;
	result.reason = result.proved ?
		"actor_led_party_proved_follow_with_owner_command_and_leash_blockers" :
		"actor_led_party_proof_incomplete";
	result.elapsed_ms = static_cast<uint32_t>(
		std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - started).count()
	);
	result.runtime = RuntimeLocked();
	return result;
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

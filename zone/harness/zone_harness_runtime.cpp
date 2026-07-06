/*	EQEmu: EQEmulator

	Copyright (C) 2001-2026 EQEmu Development Team

	This program is free software; you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation; either version 3 of the License, or
	(at your option) any later version.
*/

#include "zone_harness_runtime.h"

#include "common/rulesys.h"
#include "common/timer.h"
#include "zone/bot.h"
#include "zone/client.h"
#include "zone/entity.h"
#include "zone/harness/owned_bot_actor_fixture.h"
#include "zone/npc.h"
#include "zone/questmgr.h"
#include "zone/worldserver.h"
#include "zone/xtargetautohaters.h"
#include "zone/zone.h"
#include "zone/zone_event_scheduler.h"

#include <algorithm>
#include <thread>

extern EntityList entity_list;
extern WorldServer worldserver;
extern Zone *zone;
extern volatile bool is_zone_loaded;

namespace EQ::ZoneHarness {

namespace {

inline constexpr uint32_t kActorLeashSourceRequiredTargetTicks = 3;

}

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
	events.SetPersistenceSink(&actor_event_persistence_sink);
	ActorEventRecorder::RegisterActiveRecorder(&events);
	return true;
}

void ZoneHarnessRuntime::EnableAutonomousActorPrototype(bool enabled)
{
	std::lock_guard lock(mutex);
	autonomous_actor_prototype.enabled = enabled;
	if (!enabled) {
		StopAutonomousActorPrototypeSessionLocked();
	}
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
	std::lock_guard scenario_lock(scenario_mutex);
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

uint8_t Percent(int64_t current, int64_t maximum)
{
	if (maximum <= 0) {
		return 0;
	}

	return static_cast<uint8_t>(std::clamp<int64_t>((current * 100) / maximum, 0, 100));
}

AutonomousActorStatusSnapshot StatusFor(Bot *actor, Client *owner)
{
	AutonomousActorStatusSnapshot status;
	status.actor = DescribeMobEntity(actor);
	status.owner = DescribeMobEntity(owner);
	status.alive = actor && !actor->HasDied();
	status.hp_percent = actor ? Percent(actor->GetHP(), actor->GetMaxHP()) : 0;
	status.mana_percent = actor ? Percent(actor->GetMana(), actor->GetMaxMana()) : 0;
	status.has_target = actor && actor->GetTarget();
	status.current_target_id = actor && actor->GetTarget() ? actor->GetTarget()->GetID() : 0;
	return status;
}

bool HasTargetChangedEvent(const std::vector<ActorEvent> &events, uint16_t actor_id, uint16_t target_id)
{
	return std::any_of(
		events.begin(),
		events.end(),
		[actor_id, target_id](const ActorEvent &event) {
			return event.type == "target_changed" &&
				event.caster.entity_id == actor_id &&
				event.target.has_value() &&
				event.target->entity_id == target_id;
		}
	);
}

bool HasSpeechEvent(const std::vector<ActorEvent> &events, uint16_t actor_id, const std::string &text)
{
	return std::any_of(
		events.begin(),
		events.end(),
		[actor_id, &text](const ActorEvent &event) {
			return event.type == "speech_emitted" &&
				event.caster.entity_id == actor_id &&
				event.speech.channel == "say" &&
				event.speech.text == text;
		}
	);
}

bool ExecuteAutonomousActorAction(Bot *actor, Mob *target, const std::string &kind, const std::string &detail, std::string &reason)
{
	if (!actor) {
		reason = "actor_missing";
		return false;
	}

	if (kind == "target") {
		actor->SetTarget(target);
		const bool accepted = actor->GetTarget() == target;
		reason = accepted ? "target_set" : "target_not_set";
		return accepted;
	}

	if (kind == "say") {
		actor->Say("%s", detail.c_str());
		reason = "say_emitted";
		return true;
	}

	reason = "unsupported_action_kind";
	return false;
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

class ScopedRuleOverride {
public:
	ScopedRuleOverride(const std::string &rule_name, const std::string &rule_value) : rule_name(rule_name)
	{
		if (RuleManager::Instance()->GetRule(rule_name, original_value)) {
			changed = RuleManager::Instance()->SetRule(rule_name, rule_value, nullptr, false, false);
		}
	}

	~ScopedRuleOverride()
	{
		if (changed) {
			RuleManager::Instance()->SetRule(rule_name, original_value, nullptr, false, false);
		}
	}

private:
	std::string rule_name;
	std::string original_value;
	bool changed = false;
};

}

SpellCastStartScenarioResult ZoneHarnessRuntime::StartKnownSpellCast(uint16_t spell_id)
{
	std::lock_guard scenario_lock(scenario_mutex);
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
	const uint16_t caster_id = caster->GetID();
	const uint16_t target_id = target->GetID();

	entity_list.RemoveMob(caster_id);
	entity_list.RemoveMob(target_id);

	return {
		.started = started,
		.reason = started ? "cast_started" : "cast_not_started",
		.caster_id = caster_id,
		.target_id = target_id,
		.spell_id = spell_id,
		.runtime = RuntimeLocked(),
	};
}

HeadlessClientTargetScenarioResult ZoneHarnessRuntime::RunHeadlessClientTarget()
{
	std::lock_guard scenario_lock(scenario_mutex);
	std::lock_guard lock(mutex);

	HeadlessClientTargetScenarioResult result{
		.reason = "not_run",
		.database_mutation = "none: synthetic headless client and NPC target are in-memory only",
	};

	if (!booted || !zone || !is_zone_loaded) {
		result.reason = "zone_not_booted";
		result.runtime = RuntimeLocked();
		return result;
	}

	auto *target_type = content_db.LoadNPCTypesData(754008);
	if (!target_type) {
		result.reason = "npc_type_unavailable";
		result.runtime = RuntimeLocked();
		return result;
	}

	auto *actor = new Client();
	actor->TempName("HarnessHeadlessClient");
	actor->Mob::SetLevel(60);
	actor->SetHP(10000);
	actor->SetMana(10000);
	actor->GMMove(0.0f, 0.0f, 0.0f, 0.0f);
	entity_list.AddClient(actor);
	result.eqstream_backed = actor->Connection() != nullptr;
	result.completed_connect = actor->Connected();

	auto *target = new NPC(target_type, nullptr, glm::vec4(12, 0, 0, 0), GravityBehavior::Water);
	target->TempName("HarnessHeadlessClientTarget");
	entity_list.AddNPC(target, false, true);

	result.actor = DescribeMobEntity(actor);
	result.target = DescribeMobEntity(target);
	const uint16_t actor_id = actor->GetID();
	const uint16_t target_id = target->GetID();
	result.event_cursor_start = events.MaxEventID();

	actor->SetTarget(target);

	result.completed = actor->GetTarget() == target;

	actor->SetTarget(nullptr);
	entity_list.RemoveMob(target_id);
	entity_list.RemoveMob(actor_id);
	result.events = events.Since(result.event_cursor_start, 10);
	result.observed = HasTargetChangedEvent(result.events, actor_id, target_id);
	result.event_cursor_end = events.MaxEventID();
	result.reason = result.completed && result.observed ?
		"observed_headless_client_target_changed_without_eqstream" :
		(result.completed ? "target_set_without_observed_event" : "target_not_set");
	result.runtime = RuntimeLocked();
	return result;
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
	std::lock_guard scenario_lock(scenario_mutex);
	std::unique_lock lock(mutex);

	ActorLedBotPartyScenarioResult result{
		.follower_count_requested = follower_count,
	};

	if (!booted || !zone || !is_zone_loaded) {
		result.reason = "zone_not_booted";
		result.runtime = RuntimeLocked();
		return result;
	}

	const uint8_t bounded_followers = std::clamp<uint8_t>(
		follower_count,
		kMinOwnedBotPartyFollowers,
		kMaxOwnedBotPartyFollowers
	);
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

		if (!fixture.RemoveMob(actor_leader)) {
			result.reason = "actor_leader_remove_failed";
			result.runtime = RuntimeLocked();
			return result;
		}

		result.followers_clear_removed_actor_leader_follow_id = true;
		for (auto *follower : fixture.FollowerBots()) {
			result.followers_clear_removed_actor_leader_follow_id =
				result.followers_clear_removed_actor_leader_follow_id &&
				follower &&
				follower->GetFollowID() == 0;
		}

		if (!result.followers_clear_removed_actor_leader_follow_id) {
			result.reason = "removed_actor_leader_follow_id_not_cleared";
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

		result.owner_target_probe_follower = fixture.Describe(probe_follower);
		result.owner_target_expected_hostile = fixture.Describe(hostile);
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

		result.actor_target_probe_follower = fixture.Describe(probe_follower);
		result.actor_target_expected_hostile = fixture.Describe(hostile);
		fixture.SetBotCommandTargetSource(probe_follower, actor_leader);
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

			if (observed != result.actor_target_events.end()) {
				result.actor_target_command_observed = true;
				result.actor_target_reason = "actor_target_drove_follower_slow_cast_through_command_source";
				break;
			}

			bounded_sleep();
		}

		if (!result.actor_target_command_observed) {
			result.reason = "actor_target_phase_failed";
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
			result.reason = "owner_assist_setup_failed";
			result.runtime = RuntimeLocked();
			return result;
		}

		auto *owner = fixture.Owner();
		auto *probe_follower = fixture.FollowerBots().front();
		auto *hostile = fixture.AddHostileNPC({
			.name = "HarnessOwnerAssistHostile",
			.position = glm::vec4(12, 0, 0, 0),
		});
		if (!owner || !probe_follower || !hostile) {
			result.reason = "owner_assist_fixture_incomplete";
			result.runtime = RuntimeLocked();
			return result;
		}

		result.owner_assist_probe_follower = fixture.Describe(probe_follower);
		result.owner_assist_expected_hostile = fixture.Describe(hostile);
		fixture.OwnerTargets(nullptr);
		owner->IncrementAggroCount();
		owner->GetXTargetAutoMgr()->increment_count(hostile);
		hostile->AddToHateList(owner, 100, 1, false);
		fixture.RefreshPerception(probe_follower);

		const uint64_t since_event_id = events.MaxEventID();
		result.owner_assist_reason = "owner_assist_did_not_source_follower_action_within_bounds";
		for (uint32_t tick = 0; tick < bounded_ticks; ++tick) {
			if (!booted || !zone || !is_zone_loaded || shutdown_requested) {
				result.reason = "zone_unavailable_during_owner_assist_phase";
				result.runtime = RuntimeLocked();
				return result;
			}

			ProcessOneTick();
			++process_ticks;
			++result.ticks_processed;

			result.owner_assist_events = events.Since(since_event_id, 100);
			const auto observed = std::find_if(
				result.owner_assist_events.begin(),
				result.owner_assist_events.end(),
				[&fixture, probe_follower, hostile](const ActorEvent &event) {
					return fixture.IsSingleTargetSlowCastStartFor(probe_follower, event, hostile);
				}
			);

			if (observed != result.owner_assist_events.end()) {
				result.owner_assist_command_observed = true;
				result.owner_assist_reason = "owner_assist_drove_follower_slow_cast";
				break;
			}

			bounded_sleep();
		}

		if (!result.owner_assist_command_observed) {
			result.reason = "owner_assist_phase_failed";
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
			result.reason = "actor_assist_setup_failed";
			result.runtime = RuntimeLocked();
			return result;
		}

		auto *owner = fixture.Owner();
		auto *actor_leader = fixture.ActorLeader();
		auto *probe_follower = fixture.FollowerBots().front();
		auto *hostile = fixture.AddHostileNPC({
			.name = "HarnessActorAssistHostile",
			.position = glm::vec4(12, 0, 0, 0),
		});
		if (!owner || !actor_leader || !probe_follower || !hostile) {
			result.reason = "actor_assist_fixture_incomplete";
			result.runtime = RuntimeLocked();
			return result;
		}

		result.actor_assist_probe_follower = fixture.Describe(probe_follower);
		result.actor_assist_expected_hostile = fixture.Describe(hostile);
		fixture.OwnerTargets(nullptr);
		fixture.SetBotCommandTargetSource(probe_follower, actor_leader);
		fixture.BotTargets(actor_leader, hostile);
		fixture.OwnedBotEngages(hostile, 100);
		hostile->AddToHateList(actor_leader, 100, 1, false);
		fixture.RefreshPartyPerception();

		const uint64_t since_event_id = events.MaxEventID();
		result.actor_assist_reason = "actor_assist_did_not_source_follower_action_within_bounds";
		for (uint32_t tick = 0; tick < bounded_ticks; ++tick) {
			if (!booted || !zone || !is_zone_loaded || shutdown_requested) {
				result.reason = "zone_unavailable_during_actor_assist_phase";
				result.runtime = RuntimeLocked();
				return result;
			}

			ProcessOneTick();
			++process_ticks;
			++result.ticks_processed;

			result.actor_assist_events = events.Since(since_event_id, 100);
			const auto observed = std::find_if(
				result.actor_assist_events.begin(),
				result.actor_assist_events.end(),
				[&fixture, probe_follower, hostile](const ActorEvent &event) {
					return fixture.IsSingleTargetSlowCastStartFor(probe_follower, event, hostile);
				}
			);

			if (observed != result.actor_assist_events.end()) {
				result.actor_assist_command_observed = true;
				result.actor_assist_reason = "actor_assist_drove_follower_slow_cast_through_command_source";
				break;
			}

			bounded_sleep();
		}

		if (!result.actor_assist_command_observed) {
			result.reason = "actor_assist_phase_failed";
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
		owner->GMMove(leash_radius - 8.0f, 0.0f, 0.0f, 0.0f);
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

		result.leash_reason = "owner_nearby_control_did_not_keep_actor_led_combat_target";
		for (uint32_t tick = 0; tick < std::min<uint32_t>(bounded_ticks, 4); ++tick) {
			if (!booted || !zone || !is_zone_loaded || shutdown_requested) {
				result.reason = "zone_unavailable_during_leash_phase";
				result.runtime = RuntimeLocked();
				return result;
			}

			ProcessOneTick();
			++process_ticks;
			++result.ticks_processed;

			if (probe_follower->GetTarget() != hostile) {
				result.leash_reason = "owner_nearby_control_lost_actor_led_combat_target";
				break;
			}

			bounded_sleep();
		}

		result.owner_nearby_control_kept_combat_target = probe_follower->GetTarget() == hostile;
		if (!result.owner_nearby_control_kept_combat_target) {
			result.reason = "leash_phase_failed";
			result.runtime = RuntimeLocked();
			return result;
		}

		owner->GMMove(0.0f, 0.0f, 0.0f, 0.0f);
		fixture.RefreshPerception(probe_follower);
		result.leash_reason = "owner_client_leash_did_not_clear_actor_led_combat_target_after_owner_moved_outside_radius";
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
				result.owner_leash_default_observed = probe_follower->GetFollowID() == actor_leader->GetID();
				result.leash_reason = result.owner_leash_default_observed ?
					"owner_client_leash_cleared_combat_target_only_after_owner_moved_outside_radius_while_follow_anchor_stayed_actor_leader" :
					"owner_client_leash_cleared_combat_target_after_owner_moved_outside_radius";
				break;
			}

			bounded_sleep();
		}

		if (!result.owner_leash_default_observed) {
			result.reason = "leash_phase_failed";
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
			result.reason = "actor_leash_setup_failed";
			result.runtime = RuntimeLocked();
			return result;
		}

		auto *owner = fixture.Owner();
		auto *actor_leader = fixture.ActorLeader();
		auto *probe_follower = fixture.FollowerBots().front();
		if (!owner || !actor_leader || !probe_follower) {
			result.reason = "actor_leash_fixture_incomplete";
			result.runtime = RuntimeLocked();
			return result;
		}

		const float leash_radius = std::sqrt(std::max<float>(RuleR(Bots, LeashDistance), 100.0f)) + 25.0f;
		owner->GMMove(leash_radius - 8.0f, 0.0f, 0.0f, 0.0f);
		actor_leader->GMMove(leash_radius, 0.0f, 0.0f, 0.0f);
		probe_follower->GMMove(leash_radius + 4.0f, 0.0f, 0.0f, 0.0f);
		fixture.SetBotFollowTarget(probe_follower, actor_leader);
		fixture.SetBotLeashSource(probe_follower, actor_leader);

		auto *hostile = fixture.AddHostileNPC({
			.name = "HarnessActorLeashSourceHostile",
			.position = glm::vec4(leash_radius + 8.0f, 0.0f, 0.0f, 0.0f),
		});
		if (!hostile) {
			result.reason = "actor_leash_hostile_unavailable";
			result.runtime = RuntimeLocked();
			return result;
		}

		probe_follower->AddToHateList(hostile, 100, 1, false);
		hostile->AddToHateList(probe_follower, 100, 1, false);
		fixture.BotTargets(probe_follower, hostile);
		fixture.RefreshPerception(probe_follower);

		owner->GMMove(0.0f, 0.0f, 0.0f, 0.0f);
		result.leash_reason = "actor_leash_source_did_not_keep_combat_target_after_owner_moved_outside_radius";
		result.actor_leash_source_required_target_consecutive_ticks = kActorLeashSourceRequiredTargetTicks;
		for (uint32_t tick = 0; tick < std::min<uint32_t>(bounded_ticks, 8); ++tick) {
			if (!booted || !zone || !is_zone_loaded || shutdown_requested) {
				result.reason = "zone_unavailable_during_actor_leash_phase";
				result.runtime = RuntimeLocked();
				return result;
			}

			ProcessOneTick();
			++process_ticks;
			++result.ticks_processed;

			if (probe_follower->GetTarget() == hostile) {
				++result.actor_leash_source_target_consecutive_ticks;
				if (
					result.actor_leash_source_target_consecutive_ticks >=
					result.actor_leash_source_required_target_consecutive_ticks
				) {
					result.actor_leash_source_kept_combat_target = true;
					result.leash_reason = "actor_leash_source_kept_combat_target_after_owner_moved_outside_radius";
					break;
				}
			}
			else {
				result.actor_leash_source_target_consecutive_ticks = 0;
			}

			bounded_sleep();
		}

		if (!result.actor_leash_source_kept_combat_target) {
			result.reason = "actor_leash_phase_failed";
			result.runtime = RuntimeLocked();
			return result;
		}
	}

	result.proved =
		result.all_bots_share_owner &&
		result.group_leader_change_to_actor_rejected &&
		result.followers_follow_actor_leader &&
		result.followers_clear_removed_actor_leader_follow_id &&
		result.owner_target_command_observed &&
		result.actor_target_command_observed &&
		result.owner_assist_command_observed &&
		result.actor_assist_command_observed &&
		result.owner_nearby_control_kept_combat_target &&
		result.owner_leash_default_observed &&
		result.actor_leash_source_kept_combat_target;
	result.reason = result.proved ?
		"actor_led_party_proved_owner_defaults_with_actor_target_assist_leash_command_source_seam" :
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
	std::lock_guard scenario_lock(scenario_mutex);
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

	OwnedBotActorFixture fixture;
	if (!fixture.Create({
		.owner_name = "HarnessSlowOwner",
		.actor_name = "HarnessSlowBot",
		.primary_target_name = scenario == BotSlowMaintenanceScenarioKind::Mezzed ?
			"HarnessSlowMezzedCurrentTarget" :
			"HarnessSlowCurrentTarget",
		.secondary_target_name = scenario == BotSlowMaintenanceScenarioKind::Fallback ?
			"HarnessSlowFallbackHostile" :
			"HarnessSlowSecondaryHostile",
	})) {
		result.reason = fixture.failure_reason;
		result.runtime = RuntimeLocked();
		return result;
	}

	fixture.PrimeOwnedBotEngagement(true);
	result.database_mutation = fixture.database_mutation;

	auto *owner = fixture.owner;
	auto *bot = fixture.actor;
	auto *current_target = fixture.primary_target;
	auto *secondary_hostile = fixture.secondary_target;

	result.slow_spell_id = FindPreparedSingleTargetSlowSpell(bot, current_target);
	if (!result.slow_spell_id) {
		result.reason = "single_target_slow_spell_unavailable";
		result.owner = DescribeMobEntity(owner);
		result.bot = DescribeMobEntity(bot);
		result.current_target = DescribeMobEntity(current_target);
		result.secondary_hostile = DescribeMobEntity(secondary_hostile);
		result.runtime = RuntimeLocked();
		fixture.Cleanup();
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
		result.mezzed_hostile = DescribeMobEntity(current_target);
		expected_target = secondary_hostile;
	}

	result.owner = DescribeMobEntity(owner);
	result.bot = DescribeMobEntity(bot);
	result.current_target = DescribeMobEntity(current_target);
	result.expected_target = DescribeMobEntity(expected_target);
	result.secondary_hostile = DescribeMobEntity(secondary_hostile);

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
	fixture.Cleanup();
	return result;
}

AutonomousActorLoopScenarioResult ZoneHarnessRuntime::RunAutonomousActorLoop(uint32_t tick_budget, uint32_t sleep_ms)
{
	std::lock_guard scenario_lock(scenario_mutex);
	std::unique_lock lock(mutex);

	AutonomousActorLoopScenarioResult result{
		.reason = "not_run",
		.persistent_actor = false,
		.tick_budget = std::clamp<uint32_t>(tick_budget, 1, 1000),
	};

	if (!booted || !zone || !is_zone_loaded) {
		result.reason = "zone_not_booted";
		result.runtime = RuntimeLocked();
		return result;
	}

	OwnedBotActorFixture fixture;
	if (!fixture.Create({
		.owner_name = "HarnessActorOwner",
		.actor_name = "HarnessActorBot",
		.primary_target_name = "HarnessActorPrimaryTarget",
		.secondary_target_name = "HarnessActorSecondaryTarget",
	})) {
		result.reason = fixture.failure_reason;
		result.runtime = RuntimeLocked();
		return result;
	}

	result.database_mutation = fixture.database_mutation;
	result.owner = DescribeMobEntity(fixture.owner);
	result.actor = DescribeMobEntity(fixture.actor);
	result.target = DescribeMobEntity(fixture.primary_target);
	result.event_cursor_start = events.MaxEventID();

	ScopedRuleOverride dialogue_window_rule("Chat:QuestDialogueUsesDialogueWindow", "false");
	ScopedRuleOverride saylink_rule("Chat:AutoInjectSaylinksToSay", "false");

	result.actions = {
		{.kind = "target", .detail = fixture.primary_target->GetCleanName()},
		{.kind = "say", .detail = "Harness autonomous actor ready."},
	};

	const uint16_t actor_id = fixture.actor->GetID();
	const uint16_t target_id = fixture.primary_target->GetID();
	const auto started = std::chrono::steady_clock::now();
	const uint32_t bounded_sleep_ms = std::min<uint32_t>(sleep_ms, 250);

	for (uint32_t tick = 0; tick < result.tick_budget; ++tick) {
		if (!booted || !zone || !is_zone_loaded || shutdown_requested) {
			result.reason = "zone_unavailable_during_scenario";
			break;
		}

		for (auto &action: result.actions) {
			if (!action.accepted && action.reason.empty()) {
				action.accepted = ExecuteAutonomousActorAction(
					fixture.actor,
					fixture.primary_target,
					action.kind,
					action.detail,
					action.reason
				);
				break;
			}
		}

		ProcessOneTick();
		++process_ticks;
		++result.ticks_processed;

		result.events = events.Since(result.event_cursor_start, 100);
		for (auto &action: result.actions) {
			if (action.kind == "target" && !action.observed) {
				action.observed = HasTargetChangedEvent(result.events, actor_id, target_id);
				if (action.observed) {
					action.reason = "observed_target_changed";
				}
			}
			else if (action.kind == "say" && !action.observed) {
				action.observed = HasSpeechEvent(result.events, actor_id, action.detail);
				if (action.observed) {
					action.reason = "observed_speech_emitted";
				}
			}
		}

		result.completed = std::all_of(
			result.actions.begin(),
			result.actions.end(),
			[](const AutonomousActorActionResult &action) {
				return action.accepted && action.observed;
			}
		);
		if (result.completed) {
			result.reason = "observed_bounded_target_and_say_actions";
			break;
		}

		if (bounded_sleep_ms > 0) {
			lock.unlock();
			std::this_thread::sleep_for(std::chrono::milliseconds(bounded_sleep_ms));
			lock.lock();
		}
	}

	result.event_cursor_end = events.MaxEventID();
	result.status = StatusFor(fixture.actor, fixture.owner);
	result.perception = snapshots.PerceptionFor(fixture.actor, fixture.owner);
	result.runtime = RuntimeLocked();
	if (!result.completed && result.reason == "not_run") {
		result.reason = "autonomous_actor_actions_not_observed_within_bounds";
	}
	if (!result.completed) {
		const auto event_count = result.events.size();
		const auto last_action = std::find_if(
			result.actions.begin(),
			result.actions.end(),
			[](const AutonomousActorActionResult &action) {
				return !(action.accepted && action.observed);
			}
		);
		result.failure_output =
			"actor=" + result.actor.name +
			" owner=" + result.owner.name +
			" action=" + (last_action != result.actions.end() ? last_action->kind : "unknown") +
			" tick_budget=" + std::to_string(result.tick_budget) +
			" observed_events=" + std::to_string(event_count) +
			" persistence=" + (result.persistent_actor ? "persistent" : "ephemeral") +
			" database_mutation=" + result.database_mutation;
	}

	fixture.Cleanup();
	return result;
}

AutonomousActorPrototypeSessionSnapshot ZoneHarnessRuntime::StartAutonomousActorPrototypeSession()
{
	std::lock_guard scenario_lock(scenario_mutex);
	std::lock_guard lock(mutex);

	if (!autonomous_actor_prototype.enabled) {
		return {
			.enabled = false,
			.active = false,
			.reason = "autonomous_actor_prototype_disabled",
			.runtime = RuntimeLocked(),
		};
	}

	if (!booted || !zone || !is_zone_loaded) {
		return {
			.enabled = true,
			.active = false,
			.reason = "zone_not_booted",
			.runtime = RuntimeLocked(),
		};
	}

	StopAutonomousActorPrototypeSessionLocked();

	auto &prototype = autonomous_actor_prototype;
	if (!prototype.fixture.Create({
		.owner_name = "HarnessPrototypeOwner",
		.actor_name = "HarnessPrototypeBot",
		.primary_target_name = "HarnessPrototypePrimaryTarget",
		.secondary_target_name = "HarnessPrototypeSecondaryTarget",
	})) {
		return {
			.enabled = true,
			.active = false,
			.reason = prototype.fixture.failure_reason,
			.runtime = RuntimeLocked(),
		};
	}

	prototype.active = true;
	prototype.pending_actions.clear();
	prototype.database_mutation = prototype.fixture.database_mutation;
	prototype.last_event_cursor = events.MaxEventID();
	prototype.session_id = "session-" + std::to_string(prototype.next_session_id++);
	return AutonomousActorPrototypeSessionLocked();
}

AutonomousActorPrototypeSessionSnapshot ZoneHarnessRuntime::AutonomousActorPrototypeSession()
{
	std::lock_guard lock(mutex);
	return AutonomousActorPrototypeSessionLocked();
}

AutonomousActorPrototypeActionAck ZoneHarnessRuntime::EnqueueAutonomousActorPrototypeAction(
	const std::string &kind,
	const std::string &detail
)
{
	std::lock_guard scenario_lock(scenario_mutex);
	std::lock_guard lock(mutex);

	AutonomousActorPrototypeActionAck ack{
		.session_id = autonomous_actor_prototype.session_id,
		.kind = kind,
		.detail = detail,
		.max_queue_depth = AutonomousActorPrototypeState::max_pending_actions,
		.process_ticks_hint = 2,
		.poll_after_ms = 50,
		.event_limit_hint = 32,
	};

	if (!autonomous_actor_prototype.enabled) {
		ack.reason = "autonomous_actor_prototype_disabled";
		return ack;
	}

	auto &prototype = autonomous_actor_prototype;
	if (!prototype.active || !prototype.fixture.actor) {
		ack.reason = "session_not_active";
		return ack;
	}

	if (kind != "target" && kind != "say") {
		ack.reason = "unsupported_action_kind";
		return ack;
	}

	if (kind == "target") {
		if (!prototype.fixture.primary_target) {
			ack.reason = "target_not_available";
			return ack;
		}

		const auto target_name = prototype.fixture.primary_target->GetCleanName();
		if (!(detail.empty() || detail == "primary_target" || detail == target_name)) {
			ack.reason = "unknown_target";
			return ack;
		}
	}

	if (kind == "say" && (detail.empty() || detail.size() > 120)) {
		ack.reason = detail.empty() ? "say_detail_required" : "say_detail_too_long";
		return ack;
	}

	if (prototype.pending_actions.size() >= AutonomousActorPrototypeState::max_pending_actions) {
		ack.reason = "queue_full";
		ack.queue_depth = static_cast<uint32_t>(prototype.pending_actions.size());
		return ack;
	}

	ack.accepted = true;
	ack.reason = "queued";
	ack.request_id = prototype.next_request_id++;
	ack.event_cursor_start = events.MaxEventID();
	prototype.pending_actions.push_back({
		.request_id = ack.request_id,
		.kind = kind,
		.detail = detail,
		.event_cursor_start = ack.event_cursor_start,
	});
	ack.queue_depth = static_cast<uint32_t>(prototype.pending_actions.size());
	return ack;
}

AutonomousActorPrototypeSessionSnapshot ZoneHarnessRuntime::StopAutonomousActorPrototypeSession()
{
	std::lock_guard scenario_lock(scenario_mutex);
	std::lock_guard lock(mutex);
	StopAutonomousActorPrototypeSessionLocked();
	return AutonomousActorPrototypeSessionLocked();
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
	events.SetPersistenceSink(nullptr);
	StopAutonomousActorPrototypeSessionLocked();

	entity_list.Clear();
	entity_list.RemoveAllEncounters();

	if (zone) {
		zone->SetSaveZoneState(false);
		zone->Shutdown(true);
		zone = nullptr;
	}
}

AutonomousActorPrototypeSessionSnapshot ZoneHarnessRuntime::AutonomousActorPrototypeSessionLocked() const
{
	AutonomousActorPrototypeSessionSnapshot snapshot{
		.enabled = autonomous_actor_prototype.enabled,
		.active = autonomous_actor_prototype.active,
		.reason = autonomous_actor_prototype.enabled ? "ok" : "autonomous_actor_prototype_disabled",
		.session_id = autonomous_actor_prototype.session_id,
		.database_mutation = autonomous_actor_prototype.database_mutation,
		.queue_depth = static_cast<uint32_t>(autonomous_actor_prototype.pending_actions.size()),
		.max_queue_depth = AutonomousActorPrototypeState::max_pending_actions,
		.last_event_cursor = autonomous_actor_prototype.last_event_cursor,
		.runtime = RuntimeLocked(),
	};

	if (!autonomous_actor_prototype.active) {
		if (autonomous_actor_prototype.enabled) {
			snapshot.reason = "session_not_active";
		}
		return snapshot;
	}

	snapshot.owner = DescribeMobEntity(autonomous_actor_prototype.fixture.owner);
	snapshot.actor = DescribeMobEntity(autonomous_actor_prototype.fixture.actor);
	snapshot.target = DescribeMobEntity(autonomous_actor_prototype.fixture.primary_target);
	snapshot.status = StatusFor(autonomous_actor_prototype.fixture.actor, autonomous_actor_prototype.fixture.owner);
	snapshot.perception = snapshots.PerceptionFor(
		autonomous_actor_prototype.fixture.actor,
		autonomous_actor_prototype.fixture.owner
	);
	return snapshot;
}

void ZoneHarnessRuntime::StopAutonomousActorPrototypeSessionLocked()
{
	auto &prototype = autonomous_actor_prototype;
	if (prototype.active) {
		prototype.fixture.Cleanup();
	}

	prototype.active = false;
	prototype.session_id.clear();
	prototype.database_mutation.clear();
	prototype.pending_actions.clear();
	prototype.last_event_cursor = events.MaxEventID();
}

void ZoneHarnessRuntime::ProcessAutonomousActorPrototypeActionLocked()
{
	auto &prototype = autonomous_actor_prototype;
	if (!prototype.enabled || !prototype.active || prototype.pending_actions.empty()) {
		return;
	}

	if (!prototype.fixture.actor || !prototype.fixture.primary_target) {
		StopAutonomousActorPrototypeSessionLocked();
		return;
	}

	const auto action = prototype.pending_actions.front();
	std::string reason;
	const bool accepted = ExecuteAutonomousActorAction(
		prototype.fixture.actor,
		prototype.fixture.primary_target,
		action.kind,
		action.kind == "target" ? prototype.fixture.primary_target->GetCleanName() : action.detail,
		reason
	);
	if (accepted) {
		prototype.last_event_cursor = events.MaxEventID();
	}

	prototype.pending_actions.erase(prototype.pending_actions.begin());
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
	ProcessAutonomousActorPrototypeActionLocked();
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

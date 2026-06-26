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

class AutonomousActorActionQueue {
public:
	bool EnqueueTarget(const std::string &target_name)
	{
		return Enqueue({
			.kind = "target",
			.detail = target_name,
		});
	}

	bool EnqueueSay(const std::string &message)
	{
		return Enqueue({
			.kind = "say",
			.detail = message,
		});
	}

	const std::vector<AutonomousActorActionResult> &Actions() const
	{
		return actions;
	}

	bool HasPending() const
	{
		return next_action < actions.size();
	}

	bool ExecuteNext(Bot *actor, Mob *target, AutonomousActorActionResult &result)
	{
		if (!HasPending()) {
			return false;
		}

		const auto queued_action = actions[next_action++];
		result = queued_action;
		if (!actor) {
			result.reason = "actor_missing";
			return true;
		}

		if (result.kind == "target") {
			actor->SetTarget(target);
			result.accepted = actor->GetTarget() == target;
			result.reason = result.accepted ? "target_set" : "target_not_set";
			return true;
		}

		if (result.kind == "say") {
			actor->Say("%s", result.detail.c_str());
			result.accepted = true;
			result.reason = "say_emitted";
			return true;
		}

		result.reason = "unknown_action_kind";
		return true;
	}

private:
	bool Enqueue(const AutonomousActorActionResult &action)
	{
		if (actions.size() >= max_actions) {
			return false;
		}

		actions.push_back(action);
		return true;
	}

	static constexpr size_t max_actions = 8;
	std::vector<AutonomousActorActionResult> actions;
	size_t next_action = 0;
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

	AutonomousActorActionQueue action_queue;
	if (!action_queue.EnqueueTarget(fixture.primary_target->GetCleanName()) ||
		!action_queue.EnqueueSay("Harness autonomous actor ready.")) {
		result.reason = "autonomous_actor_action_queue_full";
		result.runtime = RuntimeLocked();
		fixture.Cleanup();
		return result;
	}
	result.actions = action_queue.Actions();

	const uint16_t actor_id = fixture.actor->GetID();
	const uint16_t target_id = fixture.primary_target->GetID();
	const auto started = std::chrono::steady_clock::now();
	const uint32_t bounded_sleep_ms = std::min<uint32_t>(sleep_ms, 250);

	for (uint32_t tick = 0; tick < result.tick_budget; ++tick) {
		if (!booted || !zone || !is_zone_loaded || shutdown_requested) {
			result.reason = "zone_unavailable_during_scenario";
			break;
		}

		if (action_queue.HasPending()) {
			for (auto &action: result.actions) {
				if (!action.accepted && action.reason.empty()) {
					action_queue.ExecuteNext(fixture.actor, fixture.primary_target, action);
					break;
				}
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

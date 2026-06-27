/*	EQEmu: EQEmulator

	Copyright (C) 2001-2026 EQEmu Development Team

	This program is free software; you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation; either version 3 of the License, or
	(at your option) any later version.
*/

#pragma once

#include "zone/harness/actor_event_persistence_sink.h"
#include "zone/harness/actor_event_recorder.h"
#include "zone/harness/harness_snapshot_service.h"
#include "zone/harness/owned_bot_actor_fixture.h"

#include <chrono>
#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

namespace EQ::ZoneHarness {

struct RuntimeSnapshot {
	bool booted = false;
	bool shutdown_requested = false;
	uint64_t uptime_ms = 0;
	uint64_t process_ticks = 0;
	uint64_t pending_events = 0;
	uint64_t max_event_id = 0;
	ZoneIdentitySnapshot zone;
};

struct HealthSnapshot {
	bool healthy = false;
	std::string status;
	RuntimeSnapshot runtime;
};

struct ProcessResult {
	uint32_t ticks_requested = 0;
	uint32_t ticks_processed = 0;
	RuntimeSnapshot runtime;
};

struct SpellCastStartScenarioResult {
	bool started = false;
	std::string reason;
	uint16_t caster_id = 0;
	uint16_t target_id = 0;
	uint16_t spell_id = 0;
	RuntimeSnapshot runtime;
};

struct HeadlessClientTargetScenarioResult {
	bool completed = false;
	bool observed = false;
	std::string reason;
	std::string action = "set_target";
	std::string database_mutation;
	bool eqstream_backed = false;
	bool completed_connect = false;
	uint64_t event_cursor_start = 0;
	uint64_t event_cursor_end = 0;
	ActorEventEntity actor;
	ActorEventEntity target;
	std::vector<ActorEvent> events;
	RuntimeSnapshot runtime;
};

struct BotSlowMaintenanceScenarioResult {
	bool observed = false;
	std::string reason;
	std::string scenario;
	uint32_t ticks_processed = 0;
	uint32_t elapsed_ms = 0;
	std::string database_mutation;
	ActorEventEntity owner;
	ActorEventEntity bot;
	ActorEventEntity current_target;
	ActorEventEntity expected_target;
	ActorEventEntity secondary_hostile;
	ActorEventEntity mezzed_hostile;
	uint16_t slow_spell_id = 0;
	bool current_target_slowed = false;
	bool mezzed_hostile_mezzed = false;
	std::vector<ActorEvent> events;
	RuntimeSnapshot runtime;
};

struct ActorLedBotPartyScenarioResult {
	bool proved = false;
	std::string reason;
	uint8_t follower_count_requested = 0;
	uint8_t follower_count_created = 0;
	uint32_t ticks_processed = 0;
	uint32_t elapsed_ms = 0;
	std::string database_mutation;
	ActorEventEntity owner;
	ActorEventEntity group_leader;
	ActorEventEntity actor_leader;
	std::vector<ActorEventEntity> followers;
	bool all_bots_share_owner = false;
	bool group_leader_change_to_actor_rejected = false;
	bool followers_follow_actor_leader = false;
	bool followers_clear_removed_actor_leader_follow_id = false;
	bool owner_target_command_observed = false;
	bool actor_target_command_observed = false;
	bool owner_nearby_control_kept_combat_target = false;
	bool owner_leash_default_observed = false;
	bool actor_leash_source_kept_combat_target = false;
	uint32_t actor_leash_source_target_consecutive_ticks = 0;
	uint32_t actor_leash_source_required_target_consecutive_ticks = 0;
	uint16_t slow_spell_id = 0;
	std::string owner_target_reason;
	std::string actor_target_reason;
	std::string leash_reason;
	ActorEventEntity owner_target_probe_follower;
	ActorEventEntity owner_target_expected_hostile;
	ActorEventEntity actor_target_probe_follower;
	ActorEventEntity actor_target_expected_hostile;
	std::vector<ActorEvent> owner_target_events;
	std::vector<ActorEvent> actor_target_events;
	RuntimeSnapshot runtime;
};

struct AutonomousActorStatusSnapshot {
	ActorEventEntity actor;
	ActorEventEntity owner;
	bool alive = false;
	uint8_t hp_percent = 0;
	uint8_t mana_percent = 0;
	bool has_target = false;
	uint16_t current_target_id = 0;
};

struct AutonomousActorActionResult {
	std::string kind;
	std::string detail;
	bool accepted = false;
	bool observed = false;
	std::string reason;
};

struct AutonomousActorLoopScenarioResult {
	bool completed = false;
	std::string reason;
	std::string failure_output;
	std::string database_mutation;
	bool persistent_actor = false;
	uint32_t tick_budget = 0;
	uint32_t ticks_processed = 0;
	uint64_t event_cursor_start = 0;
	uint64_t event_cursor_end = 0;
	ActorEventEntity owner;
	ActorEventEntity actor;
	ActorEventEntity target;
	AutonomousActorStatusSnapshot status;
	ActorPerceptionSnapshot perception;
	std::vector<AutonomousActorActionResult> actions;
	std::vector<ActorEvent> events;
	RuntimeSnapshot runtime;
};

struct AutonomousActorPrototypeSessionSnapshot {
	bool enabled = false;
	bool active = false;
	std::string reason;
	std::string session_id;
	std::string database_mutation;
	uint32_t queue_depth = 0;
	uint32_t max_queue_depth = 0;
	uint64_t last_event_cursor = 0;
	ActorEventEntity owner;
	ActorEventEntity actor;
	ActorEventEntity target;
	AutonomousActorStatusSnapshot status;
	ActorPerceptionSnapshot perception;
	RuntimeSnapshot runtime;
};

struct AutonomousActorPrototypeActionAck {
	std::string session_id;
	uint64_t request_id = 0;
	std::string kind;
	std::string detail;
	bool accepted = false;
	std::string reason;
	uint32_t queue_depth = 0;
	uint32_t max_queue_depth = 0;
	uint64_t event_cursor_start = 0;
	uint32_t process_ticks_hint = 0;
	uint32_t poll_after_ms = 0;
	uint32_t event_limit_hint = 0;
};

enum class BotSlowMaintenanceScenarioKind {
	CurrentTarget,
	Fallback,
	Mezzed,
};

class ZoneHarnessRuntime {
public:
	bool Boot(const std::string &zone_short_name, uint32_t instance_id = 0);
	void EnableAutonomousActorPrototype(bool enabled);
	HealthSnapshot Health();
	RuntimeSnapshot Runtime();
	ZoneIdentitySnapshot ZoneIdentity();
	EntitySnapshot Entities(uint32_t sample_limit = 25);
	ProcessResult ProcessWorld(uint32_t ticks);
	std::vector<ActorEvent> DrainEvents();
	std::vector<ActorEvent> EventsSince(uint64_t since_id, size_t limit);
	SpellCastStartScenarioResult StartKnownSpellCast(uint16_t spell_id = 200);
	HeadlessClientTargetScenarioResult RunHeadlessClientTarget();
	BotSlowMaintenanceScenarioResult RunBotSlowMaintenanceCurrentTarget(uint32_t max_ticks = 160, uint32_t sleep_ms = 25);
	BotSlowMaintenanceScenarioResult RunBotSlowMaintenanceFallback(uint32_t max_ticks = 160, uint32_t sleep_ms = 25);
	BotSlowMaintenanceScenarioResult RunBotSlowMaintenanceMezzed(uint32_t max_ticks = 160, uint32_t sleep_ms = 25);
	ActorLedBotPartyScenarioResult RunActorLedBotPartyProof(
		uint8_t follower_count = 3,
		uint32_t max_ticks = 160,
		uint32_t sleep_ms = 25
	);
	AutonomousActorLoopScenarioResult RunAutonomousActorLoop(uint32_t tick_budget = 24, uint32_t sleep_ms = 25);
	AutonomousActorPrototypeSessionSnapshot StartAutonomousActorPrototypeSession();
	AutonomousActorPrototypeSessionSnapshot AutonomousActorPrototypeSession();
	AutonomousActorPrototypeActionAck EnqueueAutonomousActorPrototypeAction(
		const std::string &kind,
		const std::string &detail
	);
	AutonomousActorPrototypeSessionSnapshot StopAutonomousActorPrototypeSession();
	void RequestShutdown();
	void Shutdown();

private:
	struct AutonomousActorPrototypeAction {
		uint64_t request_id = 0;
		std::string kind;
		std::string detail;
		uint64_t event_cursor_start = 0;
	};

	struct AutonomousActorPrototypeState {
		bool enabled = false;
		bool active = false;
		uint64_t next_session_id = 1;
		uint64_t next_request_id = 1;
		uint64_t last_event_cursor = 0;
		std::string session_id;
		OwnedBotActorFixture fixture;
		std::vector<AutonomousActorPrototypeAction> pending_actions;
		std::string database_mutation;
		static constexpr uint32_t max_pending_actions = 4;
	};

	BotSlowMaintenanceScenarioResult RunBotSlowMaintenanceScenario(
		BotSlowMaintenanceScenarioKind scenario,
		uint32_t max_ticks,
		uint32_t sleep_ms
	);
	AutonomousActorPrototypeSessionSnapshot AutonomousActorPrototypeSessionLocked() const;
	void StopAutonomousActorPrototypeSessionLocked();
	void ProcessAutonomousActorPrototypeActionLocked();
	RuntimeSnapshot RuntimeLocked() const;
	void ProcessOneTick();

	mutable std::mutex mutex;
	std::mutex scenario_mutex;
	HarnessSnapshotService snapshots;
	ActorEventRepositoryPersistenceSink actor_event_persistence_sink;
	ActorEventRecorder events;
	AutonomousActorPrototypeState autonomous_actor_prototype;
	std::chrono::steady_clock::time_point started_at;
	bool booted = false;
	bool shutdown_requested = false;
	uint64_t process_ticks = 0;
};

}

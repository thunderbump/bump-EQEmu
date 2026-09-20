/*	EQEmu: EQEmulator

	Copyright (C) 2001-2026 EQEmu Development Team

	This program is free software; you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation; either version 3 of the License, or
	(at your option) any later version.
*/

#include "actor_event_persistence_sink.h"

#include "common/repositories/actor_events_repository.h"
#include "zone/bot.h"
#include "zone/zone.h"
#include "common/database.h"
#include "common/eqemu_config.h"

#include <algorithm>
#include <cstdlib>
#include <optional>
#include <memory>

extern Zone* zone;

namespace EQ::ZoneHarness {

ActorEventRepositoryPersistenceSink::ActorEventRepositoryPersistenceSink(size_t max_records, size_t max_bytes,
																		 PersistenceOperation persistence_operation)
	: max_records_(std::max<size_t>(1, max_records)), max_bytes_(std::max<size_t>(1, max_bytes)),
	  persistence_operation_(std::move(persistence_operation)), worker_([this]() { Run(); }) {
}

ActorEventRepositoryPersistenceSink::~ActorEventRepositoryPersistenceSink() {
	// Normal harness shutdown explicitly flushes. This short best effort keeps
	// destruction bounded and leaves failures visible through FlushFor/Metrics.
	FlushFor(std::chrono::seconds(2));
	Stop();
}

ActorEventCaptureResult ActorEventRepositoryPersistenceSink::PersistSpeechEmitted(Mob* actor, const ActorEvent& event) {
	if (!actor || !actor->IsBot()) {
		return ActorEventCaptureResult::NotRequired;
	}

	const auto bot_id = actor->CastToBot()->GetBotID();
	if (!bot_id) {
		return ActorEventCaptureResult::NotRequired;
	}

	return Enqueue({
		.bot_id = bot_id,
		.entity_id = static_cast<uint32_t>(actor->GetID()),
		.zone_id = zone ? static_cast<uint32_t>(zone->GetZoneID()) : 0,
		.instance_id = zone ? static_cast<uint32_t>(zone->GetInstanceID()) : 0,
		.channel = event.speech.channel,
		.text = event.speech.text,
		.audible_radius = event.speech.audible_radius,
	});
}

ActorEventCaptureResult ActorEventRepositoryPersistenceSink::Enqueue(PendingSpeechEvent event) {
	const auto started_at = std::chrono::steady_clock::now();
	const auto event_bytes = EventBytes(event);
	std::lock_guard lock(mutex_);
	metrics_.attempted_records++;
	metrics_.attempted_bytes += event_bytes;

	ActorEventCaptureResult result = ActorEventCaptureResult::Accepted;
	if (stop_requested_) {
		metrics_.stopped_records++;
		result = ActorEventCaptureResult::Stopped;
	} else if (queue_.size() >= max_records_ || event_bytes > max_bytes_ ||
			   metrics_.queue_bytes > max_bytes_ - event_bytes) {
		// The caller receives an explicit deferral signal. Never evict an older
		// required event to make a new event appear successful.
		metrics_.saturated_records++;
		result = ActorEventCaptureResult::Saturated;
	} else {
		queue_.push_back(std::move(event));
		metrics_.accepted_records++;
		metrics_.accepted_bytes += event_bytes;
		metrics_.queue_records = queue_.size();
		metrics_.queue_bytes += event_bytes;
		metrics_.queue_high_water_records = std::max(metrics_.queue_high_water_records, metrics_.queue_records);
		metrics_.queue_high_water_bytes = std::max(metrics_.queue_high_water_bytes, metrics_.queue_bytes);
		work_available_.notify_one();
	}

	metrics_.capture_nanoseconds += static_cast<uint64_t>(
		std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - started_at).count());
	return result;
}

bool ActorEventRepositoryPersistenceSink::FlushFor(std::chrono::milliseconds timeout) {
	std::unique_lock lock(mutex_);
	return state_changed_.wait_for(lock, timeout, [this]() { return queue_.empty() && !persistence_in_flight_; });
}

ActorEventRepositoryPersistenceSink::Metrics ActorEventRepositoryPersistenceSink::GetMetrics() const {
	std::lock_guard lock(mutex_);
	return metrics_;
}

size_t ActorEventRepositoryPersistenceSink::EventBytes(const PendingSpeechEvent& event) {
	// Count the fixed scalar envelope as well as variable payload bytes. This is
	// an accounting bound, not allocator-specific heap introspection.
	return sizeof(PendingSpeechEvent) + event.channel.size() + event.text.size();
}

Database* ActorEventRepositoryPersistenceSink::RepositoryConnection() {
	thread_local Database connection;
	if (connection.GetStatus() != DBcore::Connected) {
		const auto* config = EQEmuConfig::get();
		connection.SetConnectionTimeouts(1, 1, 1);
		if (!config || !connection.Connect(config->DatabaseHost, config->DatabaseUsername,
				config->DatabasePassword, config->DatabaseDB, config->DatabasePort, "actor-events")) {
			return nullptr;
		}
	}
	return &connection;
}

bool ActorEventRepositoryPersistenceSink::PersistToRepository(const PendingSpeechEvent& event) {
	auto* connection = RepositoryConnection();
	if (!connection) {
		return false;
	}
	// Admission already accepted this evidence. A missing or disabled profile
	// must retain the record for retry, never acknowledge a write that did not
	// happen. The bounded queue supplies backpressure until persistence recovers.
	auto profile_result = connection->QueryDatabase(fmt::format(
		"SELECT actor_id, owner_character_id, enabled FROM actor_profiles WHERE bot_id = {} LIMIT 1", event.bot_id));
	if (!profile_result.Success()) {
		return false;
	}
	if (profile_result.RowCount() == 0) {
		return false;
	}
	auto row = profile_result.begin();
	if (!row[0] || !row[2]) {
		return false;
	}
	const auto actor_id = static_cast<uint32_t>(strtoul(row[0], nullptr, 10));
	const bool enabled = strtoul(row[2], nullptr, 10) != 0;
	if (!actor_id || !enabled) {
		return false;
	}
	const auto owner_character_id = row[1]
		? std::optional<uint32_t>(static_cast<uint32_t>(strtoul(row[1], nullptr, 10)))
		: std::nullopt;

	return ActorEventsRepository::AppendObservedSpeechEmitted(
			   *connection,
			   {
				   .actor_id = actor_id,
				   .bot_id = event.bot_id,
				   .owner_character_id = owner_character_id,
				   .zone_id = event.zone_id ? std::optional<uint32_t>(event.zone_id) : std::nullopt,
				   .instance_id = std::optional<uint32_t>(event.instance_id),
				   .entity_id = event.entity_id ? std::optional<uint32_t>(event.entity_id) : std::nullopt,
				   .channel = event.channel,
				   .text = event.text,
				   .audible_radius = event.audible_radius,
			   })
			   .event_id != 0;
}

void ActorEventRepositoryPersistenceSink::Run() {
	using namespace std::chrono_literals;
	for (;;) {
		PendingSpeechEvent event;
		size_t event_bytes = 0;
		{
			std::unique_lock lock(mutex_);
			work_available_.wait(lock, [this]() { return stop_requested_ || !queue_.empty(); });
			if (stop_requested_) {
				return;
			}
			event = queue_.front();
			event_bytes = EventBytes(event);
			persistence_in_flight_ = true;
			metrics_.persistence_attempts++;
		}

		const auto started_at = std::chrono::steady_clock::now();
		const bool persisted = persistence_operation_ ? persistence_operation_(event) : PersistToRepository(event);
		const auto elapsed = static_cast<uint64_t>(
			std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - started_at)
				.count());

		std::unique_lock lock(mutex_);
		metrics_.flush_nanoseconds += elapsed;
		persistence_in_flight_ = false;
		if (persisted) {
			queue_.pop_front();
			metrics_.persisted_records++;
			metrics_.persisted_bytes += event_bytes;
			metrics_.queue_records = queue_.size();
			metrics_.queue_bytes -= event_bytes;
			state_changed_.notify_all();
			continue;
		}

		metrics_.persistence_failures++;
		state_changed_.notify_all();
		// Retain the failed front record and retry it in order. A short bounded
		// backoff prevents an unavailable store from consuming a zone CPU core.
		work_available_.wait_for(lock, 25ms, [this]() { return stop_requested_; });
		if (stop_requested_) {
			return;
		}
	}
}

void ActorEventRepositoryPersistenceSink::Stop() {
	{
		std::lock_guard lock(mutex_);
		stop_requested_ = true;
		work_available_.notify_all();
		state_changed_.notify_all();
	}
	if (worker_.joinable()) {
		worker_.join();
	}
}

} // namespace EQ::ZoneHarness

/*	EQEmu: EQEmulator

	Copyright (C) 2001-2026 EQEmu Development Team

	This program is free software; you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation; either version 3 of the License, or
	(at your option) any later version.
*/

#include "actor_event_persistence_sink.h"

#include "common/database.h"
#include "common/eqemu_config.h"
#include "common/repositories/actor_events_repository.h"
#include "zone/bot.h"
#include "zone/zone.h"

#include <algorithm>
#include <condition_variable>
#include <cstdlib>
#include <deque>
#include <iostream>
#include <mutex>
#include <optional>

extern Zone* zone;

namespace EQ::ZoneHarness {

struct ActorEventRepositoryPersistenceSink::WorkerState {
	WorkerState(size_t record_limit, size_t byte_limit, PersistenceOperation operation)
		: max_records(std::max<size_t>(1, record_limit)), max_bytes(std::max<size_t>(1, byte_limit)),
		  persistence_operation(std::move(operation)) {
		// This connection belongs only to the evidence worker. Its deadlines both
		// isolate the zone's shared connection and bound a wedged database call.
		persistence_database.SetConnectionTimeouts(1, 1, 1);
	}

	const size_t max_records;
	const size_t max_bytes;
	PersistenceOperation persistence_operation;
	Database persistence_database;
	bool database_connection_initialized = false;
	std::mutex mutex;
	std::condition_variable work_available;
	std::condition_variable state_changed;
	std::deque<PendingSpeechEvent> queue;
	std::deque<PendingSpeechEvent> dead_letters;
	Metrics metrics;
	bool persistence_in_flight = false;
	bool stop_requested = false;
	bool worker_exited = false;
};

ActorEventRepositoryPersistenceSink::ActorEventRepositoryPersistenceSink(size_t max_records, size_t max_bytes,
														 PersistenceOperation persistence_operation)
	: state_(std::make_shared<WorkerState>(max_records, max_bytes, std::move(persistence_operation))),
	  worker_([state = state_]() { Run(state); }) {
}

ActorEventRepositoryPersistenceSink::~ActorEventRepositoryPersistenceSink() {
	// Preserve a bounded final drain attempt. Stop never waits indefinitely for
	// an injected callback or client-library call that ignores its deadline.
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
		.evidence_required_at_capture = true,
	});
}

ActorEventCaptureResult ActorEventRepositoryPersistenceSink::Enqueue(PendingSpeechEvent event) {
	const auto started_at = std::chrono::steady_clock::now();
	const auto event_bytes = EventBytes(event);
	const auto state = state_;
	if (!state) {
		return ActorEventCaptureResult::Stopped;
	}

	std::lock_guard lock(state->mutex);
	state->metrics.attempted_records++;
	state->metrics.attempted_bytes += event_bytes;

	ActorEventCaptureResult result = ActorEventCaptureResult::Accepted;
	if (state->stop_requested) {
		state->metrics.stopped_records++;
		result = ActorEventCaptureResult::Stopped;
	} else if (state->queue.size() + state->dead_letters.size() >= state->max_records ||
			   event_bytes > state->max_bytes || state->metrics.queue_bytes > state->max_bytes - event_bytes) {
		// The caller receives an explicit deferral signal. Never evict an older
		// required event to make a new event appear successful.
		state->metrics.saturated_records++;
		result = ActorEventCaptureResult::Saturated;
	} else {
		state->queue.push_back(std::move(event));
		state->metrics.accepted_records++;
		state->metrics.accepted_bytes += event_bytes;
		state->metrics.queue_records = state->queue.size() + state->dead_letters.size();
		state->metrics.queue_bytes += event_bytes;
		state->metrics.queue_high_water_records =
			std::max(state->metrics.queue_high_water_records, state->metrics.queue_records);
		state->metrics.queue_high_water_bytes =
			std::max(state->metrics.queue_high_water_bytes, state->metrics.queue_bytes);
		state->work_available.notify_one();
	}

	state->metrics.capture_nanoseconds += static_cast<uint64_t>(
		std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - started_at).count());
	return result;
}

bool ActorEventRepositoryPersistenceSink::FlushFor(std::chrono::milliseconds timeout) {
	const auto state = state_;
	if (!state) {
		return true;
	}
	std::unique_lock lock(state->mutex);
	return state->state_changed.wait_for(
		lock, timeout, [&]() { return state->queue.empty() && !state->persistence_in_flight; });
}

ActorEventRepositoryPersistenceSink::Metrics ActorEventRepositoryPersistenceSink::GetMetrics() const {
	const auto state = state_;
	if (!state) {
		return {};
	}
	std::lock_guard lock(state->mutex);
	return state->metrics;
}

std::vector<ActorEventRepositoryPersistenceSink::PendingSpeechEvent>
ActorEventRepositoryPersistenceSink::GetDeadLetters() const {
	const auto state = state_;
	if (!state) {
		return {};
	}
	std::lock_guard lock(state->mutex);
	return {state->dead_letters.begin(), state->dead_letters.end()};
}

size_t ActorEventRepositoryPersistenceSink::EventBytes(const PendingSpeechEvent& event) {
	// Count the fixed scalar envelope as well as variable payload bytes. This is
	// an accounting bound, not allocator-specific heap introspection.
	return sizeof(PendingSpeechEvent) + event.channel.size() + event.text.size();
}

ActorEventRepositoryPersistenceSink::PersistenceDisposition
ActorEventRepositoryPersistenceSink::PersistToRepository(
	const std::shared_ptr<WorkerState>& state, PendingSpeechEvent& event) {
	if (!state->database_connection_initialized) {
		state->database_connection_initialized = true;
		const auto config = EQEmuConfig::get();
		if (!config || !state->persistence_database.Connect(
					   config->DatabaseHost,
					   config->DatabaseUsername,
					   config->DatabasePassword,
					   config->DatabaseDB,
					   config->DatabasePort,
					   "actor-events")) {
			return PersistenceDisposition::Retry;
		}
	}

	if (!event.identity_resolved) {
		// Resolve once, then retain the binding with the event across insert
		// retries. The lookup happens after capture, so absence or disablement can
		// never prove that an accepted bot event was not required when captured.
		// Preserve that uncertainty visibly instead of acknowledging the event as
		// NotRequired (which would silently lose a profile deleted while queued).
		auto profile_result = state->persistence_database.QueryDatabase(fmt::format(
			"SELECT actor_id, owner_character_id, enabled FROM actor_profiles WHERE bot_id = {} LIMIT 1", event.bot_id));
		if (!profile_result.Success()) {
			return PersistenceDisposition::Retry;
		}
		if (profile_result.RowCount() == 0) {
			return event.evidence_required_at_capture ? PersistenceDisposition::DeadLetter
											 : PersistenceDisposition::NotRequired;
		}
		auto row = profile_result.begin();
		if (!row[0] || !row[2]) {
			return event.evidence_required_at_capture ? PersistenceDisposition::DeadLetter
											 : PersistenceDisposition::NotRequired;
		}
		event.actor_id = static_cast<uint32_t>(strtoul(row[0], nullptr, 10));
		const bool enabled = strtoul(row[2], nullptr, 10) != 0;
		if (!event.actor_id || !enabled) {
			return event.evidence_required_at_capture ? PersistenceDisposition::DeadLetter
											 : PersistenceDisposition::NotRequired;
		}
		event.owner_character_id = row[1]
			? std::optional<uint32_t>(static_cast<uint32_t>(strtoul(row[1], nullptr, 10)))
			: std::nullopt;
		event.identity_resolved = true;
	}

	return ActorEventsRepository::AppendObservedSpeechEmitted(
			   state->persistence_database,
			   {
				   .actor_id = event.actor_id,
				   .bot_id = event.bot_id,
				   .owner_character_id = event.owner_character_id,
				   .zone_id = event.zone_id ? std::optional<uint32_t>(event.zone_id) : std::nullopt,
				   .instance_id = std::optional<uint32_t>(event.instance_id),
				   .entity_id = event.entity_id ? std::optional<uint32_t>(event.entity_id) : std::nullopt,
				   .channel = event.channel,
				   .text = event.text,
				   .audible_radius = event.audible_radius,
			   })
			   .event_id != 0
		? PersistenceDisposition::Persisted
		: PersistenceDisposition::Retry;
}

void ActorEventRepositoryPersistenceSink::Run(const std::shared_ptr<WorkerState>& state) {
	using namespace std::chrono_literals;
	for (;;) {
		PendingSpeechEvent event;
		size_t event_bytes = 0;
		{
			std::unique_lock lock(state->mutex);
			state->work_available.wait(lock, [&]() { return state->stop_requested || !state->queue.empty(); });
			if (state->stop_requested) {
				state->worker_exited = true;
				state->state_changed.notify_all();
				return;
			}
			event = state->queue.front();
			event_bytes = EventBytes(event);
			state->persistence_in_flight = true;
			state->metrics.persistence_attempts++;
		}

		const auto started_at = std::chrono::steady_clock::now();
		const auto disposition = state->persistence_operation ? state->persistence_operation(event)
														  : PersistToRepository(state, event);
		const auto elapsed = static_cast<uint64_t>(
			std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - started_at)
				.count());

		std::unique_lock lock(state->mutex);
		state->metrics.flush_nanoseconds += elapsed;
		state->persistence_in_flight = false;
		if (state->stop_requested) {
			state->worker_exited = true;
			state->state_changed.notify_all();
			return;
		}
		if (disposition == PersistenceDisposition::Persisted ||
			disposition == PersistenceDisposition::NotRequired ||
			disposition == PersistenceDisposition::DeadLetter) {
			state->queue.pop_front();
			if (disposition == PersistenceDisposition::Persisted) {
				state->metrics.persisted_records++;
				state->metrics.persisted_bytes += event_bytes;
			} else if (disposition == PersistenceDisposition::NotRequired) {
				state->metrics.not_required_records++;
				state->metrics.not_required_bytes += event_bytes;
			} else {
				state->dead_letters.push_back(event);
				state->metrics.dead_letter_records++;
				state->metrics.dead_letter_bytes += event_bytes;
				std::cerr << "[ACTOR-EVIDENCE-DEAD-LETTER] bot_id=" << event.bot_id
						  << " entity_id=" << event.entity_id
						  << " reason=identity_unavailable_at_flush\n";
			}
			state->metrics.queue_records = state->queue.size() + state->dead_letters.size();
			if (disposition != PersistenceDisposition::DeadLetter) {
				state->metrics.queue_bytes -= event_bytes;
			}
			state->state_changed.notify_all();
			continue;
		}

		// Persist identity-resolution progress on the retained queue record.
		state->queue.front() = event;
		state->metrics.persistence_failures++;
		state->state_changed.notify_all();
		// Retain the failed front record and retry it in order. A short bounded
		// backoff prevents an unavailable store from consuming a zone CPU core.
		state->work_available.wait_for(lock, 25ms, [&]() { return state->stop_requested; });
		if (state->stop_requested) {
			state->worker_exited = true;
			state->state_changed.notify_all();
			return;
		}
	}
}

void ActorEventRepositoryPersistenceSink::Stop() {
	using namespace std::chrono_literals;
	const auto state = state_;
	if (!state) {
		return;
	}

	bool worker_exited = false;
	{
		std::unique_lock lock(state->mutex);
		state->stop_requested = true;
		state->work_available.notify_all();
		state->state_changed.notify_all();
		worker_exited = state->state_changed.wait_for(lock, 100ms, [&]() { return state->worker_exited; });
	}

	if (worker_.joinable()) {
		if (worker_exited) {
			worker_.join();
		} else {
			// The worker owns shared state, including its dedicated connection, so
			// abandoning a non-cooperative operation cannot access this sink.
			worker_.detach();
		}
	}
	state_.reset();
}

} // namespace EQ::ZoneHarness

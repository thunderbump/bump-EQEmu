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
#include "common/path_manager.h"
#include "common/json/json.h"

#include <algorithm>
#include <cstdlib>
#include <optional>
#include <memory>
#include <filesystem>
#include <iostream>
#include <vector>
#include <cerrno>
#include <cstring>
#ifndef _WIN32
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#endif

extern Zone* zone;

namespace EQ::ZoneHarness {

struct ActorEventRepositoryPersistenceSink::WorkerState {
	WorkerState(size_t records, size_t bytes, PersistenceOperation operation)
		: max_records(std::max<size_t>(1, records)), max_bytes(std::max<size_t>(1, bytes)),
		  persistence_operation(std::move(operation)) {}
	const size_t max_records;
	const size_t max_bytes;
	PersistenceOperation persistence_operation;
	std::mutex mutex;
	std::condition_variable work_available;
	std::condition_variable state_changed;
	std::deque<PendingSpeechEvent> queue;
	Metrics metrics;
	bool accepting = true;
	bool persistence_in_flight = false;
	bool in_flight_at_stop = false;
	bool stop_requested = false;
	bool worker_exited = false;
};

ActorEventRepositoryPersistenceSink::ActorEventRepositoryPersistenceSink(size_t max_records, size_t max_bytes,
	PersistenceOperation persistence_operation)
	: state_(std::make_shared<WorkerState>(max_records, max_bytes, std::move(persistence_operation))),
	  worker_([state = state_]() { Run(state); }) {}

ActorEventRepositoryPersistenceSink::~ActorEventRepositoryPersistenceSink() {
	try {
		const auto result = ShutdownFor(std::chrono::seconds(2));
		if (result.retained_records || !result.error.empty()) {
			std::cerr << "[ACTOR-EVIDENCE-RECOVERY] retained_records=" << result.retained_records
				<< " path=" << result.recovery_path << " error=" << result.error << "\n";
		}
	} catch (const std::exception& error) {
		std::cerr << "[ACTOR-EVIDENCE-FAIL] shutdown exception: " << error.what() << "\n";
		if (worker_.joinable()) { worker_.detach(); }
	}
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
	const auto state = state_;
	const auto started_at = std::chrono::steady_clock::now();
	const auto event_bytes = EventBytes(event);
	std::lock_guard lock(state->mutex);
	state->metrics.attempted_records++;
	state->metrics.attempted_bytes += event_bytes;

	ActorEventCaptureResult result = ActorEventCaptureResult::Accepted;
	if (!state->accepting) {
		state->metrics.stopped_records++;
		result = ActorEventCaptureResult::Stopped;
	} else if (state->queue.size() >= state->max_records || event_bytes > state->max_bytes ||
			   state->metrics.queue_bytes > state->max_bytes - event_bytes) {
		// The caller receives an explicit deferral signal. Never evict an older
		// required event to make a new event appear successful.
		state->metrics.saturated_records++;
		result = ActorEventCaptureResult::Saturated;
	} else {
		state->queue.push_back(std::move(event));
		state->metrics.accepted_records++;
		state->metrics.accepted_bytes += event_bytes;
		state->metrics.queue_records = state->queue.size();
		state->metrics.queue_bytes += event_bytes;
		state->metrics.queue_high_water_records = std::max(state->metrics.queue_high_water_records, state->metrics.queue_records);
		state->metrics.queue_high_water_bytes = std::max(state->metrics.queue_high_water_bytes, state->metrics.queue_bytes);
		state->work_available.notify_one();
	}

	state->metrics.capture_nanoseconds += static_cast<uint64_t>(
		std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - started_at).count());
	return result;
}

bool ActorEventRepositoryPersistenceSink::FlushFor(std::chrono::milliseconds timeout) {
	const auto state = state_;
	std::unique_lock lock(state->mutex);
	return state->state_changed.wait_for(lock, timeout, [&]() { return state->queue.empty() && !state->persistence_in_flight; });
}

ActorEventRepositoryPersistenceSink::Metrics ActorEventRepositoryPersistenceSink::GetMetrics() const {
	const auto state = state_;
	std::lock_guard lock(state->mutex);
	return state->metrics;
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
		bool persisted = false;
		try {
			persisted = state->persistence_operation ? state->persistence_operation(event) : PersistToRepository(event);
		} catch (...) {
			// Failure is retained for retry or shutdown recovery, never acknowledged.
		}
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
		if (persisted) {
			state->queue.pop_front();
			state->metrics.persisted_records++;
			state->metrics.persisted_bytes += event_bytes;
			state->metrics.queue_records = state->queue.size();
			state->metrics.queue_bytes -= event_bytes;
			state->state_changed.notify_all();
			continue;
		}

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

ActorEventRepositoryPersistenceSink::ShutdownResult ActorEventRepositoryPersistenceSink::ShutdownFor(
	std::chrono::milliseconds timeout, const std::string& recovery_directory) {
	if (shutdown_result_ && shutdown_result_->error.empty()) { return *shutdown_result_; }
	const auto state = state_;
	const auto deadline = std::chrono::steady_clock::now() + timeout;
	ShutdownResult result;
	std::deque<PendingSpeechEvent> pending;
	{
		std::unique_lock lock(state->mutex);
		state->accepting = false;
		if (!state->stop_requested) {
			state->state_changed.wait_until(lock, deadline, [&]() {
				return state->queue.empty() && !state->persistence_in_flight;
			});
		}
		state->in_flight_at_stop = state->in_flight_at_stop || state->persistence_in_flight;
		state->stop_requested = true;
		state->work_available.notify_all();
		state->state_changed.wait_until(lock, deadline, [&]() { return state->worker_exited; });
		result.worker_stopped = state->worker_exited;
		result.drained = state->queue.empty();
		result.in_flight_outcome_unknown = state->in_flight_at_stop;
		pending = state->queue;
		result.retained_records = pending.size();
	}
	if (worker_.joinable()) {
		// The loop exit flag precedes thread-local database cleanup. Never join
		// here: even that cleanup is outside the caller's wait budget.
		worker_.detach();
	}
	if (!pending.empty()) {
		try {
			Json::Value document;
			document["schema_version"] = 1;
			document["reason"] = "shutdown_unwritten_evidence";
			document["automatic_replay_safe"] = false;
			document["in_flight_outcome_unknown"] = result.in_flight_outcome_unknown;
			document["records"] = Json::Value(Json::arrayValue);
			for (const auto& event : pending) {
				Json::Value record;
				record["bot_id"] = event.bot_id;
				record["entity_id"] = event.entity_id;
				record["zone_id"] = event.zone_id;
				record["instance_id"] = event.instance_id;
				record["channel"] = event.channel;
				record["text"] = event.text;
				record["audible_radius"] = event.audible_radius;
				document["records"].append(record);
			}
			Json::StreamWriterBuilder writer;
			const auto bytes = Json::writeString(writer, document);
			const auto directory = recovery_directory.empty()
				? std::filesystem::path(PathManager::Instance()->GetLogPath().empty() ? "logs" : PathManager::Instance()->GetLogPath()) / "actor-evidence-recovery"
				: std::filesystem::path(recovery_directory);
			std::filesystem::create_directories(directory);
#ifndef _WIN32
			std::string pattern = (directory / "pending-XXXXXX").string();
			std::vector<char> filename(pattern.begin(), pattern.end());
			filename.push_back(0);
			const int fd = mkstemp(filename.data()); // Exclusive, owner-readable/writable only.
			if (fd < 0) { throw std::runtime_error(std::strerror(errno)); }
			result.recovery_path = filename.data();
			bool saved = true;
			size_t offset = 0;
			while (offset < bytes.size()) {
				const auto written = write(fd, bytes.data() + offset, bytes.size() - offset);
				if (written < 0 && errno == EINTR) { continue; }
				if (written <= 0) { saved = false; break; }
				offset += static_cast<size_t>(written);
			}
			if (saved && fsync(fd) != 0) { saved = false; }
			if (close(fd) != 0) { saved = false; }
			if (!saved) {
				std::filesystem::remove(result.recovery_path);
				result.recovery_path.clear();
				throw std::runtime_error("could not write and sync recovery payload");
			}
			const int directory_fd = open(directory.c_str(), O_RDONLY | O_DIRECTORY);
			if (directory_fd < 0) { throw std::runtime_error("could not open recovery directory for sync"); }
			const bool directory_synced = fsync(directory_fd) == 0;
			close(directory_fd);
			if (!directory_synced) { throw std::runtime_error("could not sync recovery directory"); }
#else
			throw std::runtime_error("private recovery-file writing requires the POSIX runtime");
#endif
		} catch (const std::exception& error) { result.error = error.what(); }
	}
	shutdown_result_ = result;
	return result;
}

} // namespace EQ::ZoneHarness

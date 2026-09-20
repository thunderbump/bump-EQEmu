/*	EQEmu: EQEmulator

	Copyright (C) 2001-2026 EQEmu Development Team

	This program is free software; you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation; either version 3 of the License, or
	(at your option) any later version.
*/

#pragma once

#include "zone/harness/actor_event_recorder.h"

#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <mutex>
#include <string>
#include <thread>

class Mob;
class Database;

namespace EQ::ZoneHarness {

class ActorEventPersistenceSink {
public:
	virtual ~ActorEventPersistenceSink() = default;

	virtual ActorEventCaptureResult PersistSpeechEmitted(Mob* actor, const ActorEvent& event) = 0;
};

// This is deliberately a small, speech-only queue. It bounds the first local
// actor loop without introducing the generalized evidence sequencer/outbox.
class ActorEventRepositoryPersistenceSink final : public ActorEventPersistenceSink {
public:
	struct PendingSpeechEvent {
		uint32_t bot_id = 0;
		uint32_t entity_id = 0;
		uint32_t zone_id = 0;
		uint32_t instance_id = 0;
		std::string channel;
		std::string text;
		uint32_t audible_radius = 0;
	};

	struct Metrics {
		uint64_t attempted_records = 0;
		uint64_t attempted_bytes = 0;
		uint64_t accepted_records = 0;
		uint64_t accepted_bytes = 0;
		uint64_t persisted_records = 0;
		uint64_t persisted_bytes = 0;
		uint64_t persistence_attempts = 0;
		uint64_t persistence_failures = 0;
		uint64_t saturated_records = 0;
		uint64_t stopped_records = 0;
		uint64_t queue_records = 0;
		uint64_t queue_bytes = 0;
		uint64_t queue_high_water_records = 0;
		uint64_t queue_high_water_bytes = 0;
		uint64_t capture_nanoseconds = 0;
		uint64_t flush_nanoseconds = 0;
	};

	using PersistenceOperation = std::function<bool(const PendingSpeechEvent&)>;

	explicit ActorEventRepositoryPersistenceSink(size_t max_records = 64, size_t max_bytes = 64 * 1024,
												 PersistenceOperation persistence_operation = {});
	~ActorEventRepositoryPersistenceSink() override;

	ActorEventRepositoryPersistenceSink(const ActorEventRepositoryPersistenceSink&) = delete;
	ActorEventRepositoryPersistenceSink& operator=(const ActorEventRepositoryPersistenceSink&) = delete;

	ActorEventCaptureResult PersistSpeechEmitted(Mob* actor, const ActorEvent& event) override;
	ActorEventCaptureResult Enqueue(PendingSpeechEvent event);
	bool FlushFor(std::chrono::milliseconds timeout);
	Metrics GetMetrics() const;

	// Per-thread connection used only by persistence, including injected adapter wrappers.
	// Returns null on connection failure; socket/connect timeouts apply to this connection.
	static Database* RepositoryConnection();

	// Default adapter; injected operations can wrap it to control worker timing.
	static bool PersistToRepository(const PendingSpeechEvent& event);

private:
	static size_t EventBytes(const PendingSpeechEvent& event);
	void Run();
	void Stop();

	const size_t max_records_;
	const size_t max_bytes_;
	PersistenceOperation persistence_operation_;
	mutable std::mutex mutex_;
	std::condition_variable work_available_;
	std::condition_variable state_changed_;
	std::deque<PendingSpeechEvent> queue_;
	Metrics metrics_;
	bool persistence_in_flight_ = false;
	bool stop_requested_ = false;
	std::thread worker_;
};

} // namespace EQ::ZoneHarness

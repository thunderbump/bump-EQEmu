/*	EQEmu: EQEmulator

	Copyright (C) 2001-2026 EQEmu Development Team

	This program is free software; you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation; either version 3 of the License, or
	(at your option) any later version.
*/

#pragma once

#include "zone/harness/actor_event_recorder.h"
#include "zone/harness/harness_snapshot_service.h"

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

class ZoneHarnessRuntime {
public:
	bool Boot(const std::string &zone_short_name, uint32_t instance_id = 0);
	HealthSnapshot Health();
	RuntimeSnapshot Runtime();
	ZoneIdentitySnapshot ZoneIdentity();
	EntitySnapshot Entities(uint32_t sample_limit = 25);
	ProcessResult ProcessWorld(uint32_t ticks);
	std::vector<ActorEvent> DrainEvents();
	void RequestShutdown();
	void Shutdown();

private:
	RuntimeSnapshot RuntimeLocked() const;
	void ProcessOneTick();

	mutable std::mutex mutex;
	HarnessSnapshotService snapshots;
	ActorEventRecorder events;
	std::chrono::steady_clock::time_point started_at;
	bool booted = false;
	bool shutdown_requested = false;
	uint64_t process_ticks = 0;
};

}

#pragma once

#include "common/database.h"

#include <cstddef>
#include <cstdint>
#include <ctime>
#include <filesystem>
#include <optional>

class ActorHelper {
public:
	struct Options {
		std::filesystem::path state_directory;
		std::optional<uint32_t> actor_id;
		std::optional<uint32_t> zone_id;
		uint32_t instance_id = 0;
		uint32_t freshness_seconds = 30;
		size_t event_limit = 32;
		size_t discovery_limit = 64;
	};

	struct CycleResult {
		size_t discovered = 0;
		size_t enqueued = 0;
		size_t outcomes_observed = 0;
		size_t busy = 0;
		size_t cursor_gaps = 0;
		size_t retained_event_losses = 0;
		bool DidWork() const { return enqueued != 0 || outcomes_observed != 0; }
	};

	ActorHelper(Database& database, Options options);
	CycleResult RunCycle(time_t now = 0);

private:
	uint64_t LoadCursor(uint32_t actor_id) const;
	bool StoreCursor(uint32_t actor_id, uint64_t cursor) const;
	uint32_t LoadDiscoveryCursor() const;
	bool StoreDiscoveryCursor(uint32_t cursor) const;

	Database& database_;
	Options options_;
	uint32_t discovery_cursor_ = 0;
};

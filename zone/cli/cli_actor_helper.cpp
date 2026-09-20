#include "zone/zone_cli.h"

#include "common/strings.h"
#include "zone/actor_helper.h"
#include "zone/zonedb.h"

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <thread>
#include <utility>

extern ZoneDatabase database;

void ZoneCLI::ActorHelperRun(int, char**, argh::parser& cmd, std::string& description) {
	description = "Run the disabled-by-default durable autonomous actor helper.";
	if (cmd[{"-h", "--help"}]) {
		std::cout << "Usage: actor-helper:run --enabled [--state-dir path] [--actor-id id] "
					 "[--zone-id id] [--instance-id id] [--max-cycles count] [--poll-ms milliseconds] "
					 "[--exit-after-outcome]\n";
		return;
	}
	if (!cmd[{"--enabled"}]) {
		std::cout << "actor-helper disabled (pass --enabled to opt in)\n";
		return;
	}

	ActorHelper::Options options;
	options.state_directory = cmd("--state-dir").str().empty()
		? std::filesystem::path("actor-helper-state")
		: std::filesystem::path(cmd("--state-dir").str());
	if (!cmd("--actor-id").str().empty()) {
		const auto value = Strings::ToUnsignedInt(cmd("--actor-id").str());
		if (value == 0) {
			std::cerr << "actor-helper: invalid actor id\n";
			std::exit(2);
		}
		options.actor_id = value;
	}
	if (!cmd("--zone-id").str().empty()) {
		const auto value = Strings::ToUnsignedInt(cmd("--zone-id").str());
		if (value == 0) {
			std::cerr << "actor-helper: invalid zone id\n";
			std::exit(2);
		}
		options.zone_id = value;
		options.instance_id = Strings::ToUnsignedInt(cmd("--instance-id").str());
	}
	const uint32_t max_cycles = cmd("--max-cycles").str().empty()
		? 0 : Strings::ToUnsignedInt(cmd("--max-cycles").str());
	const uint32_t poll_ms = cmd("--poll-ms").str().empty()
		? 1000 : std::clamp<uint32_t>(Strings::ToUnsignedInt(cmd("--poll-ms").str()), 10, 60000);

	ActorHelper helper(database, std::move(options));
	uint32_t cycle = 0;
	do {
		const auto result = helper.RunCycle();
		std::cout << "[ACTOR-HELPER] discovered=" << result.discovered << " enqueued=" << result.enqueued
				  << " outcomes=" << result.outcomes_observed << " busy=" << result.busy
				  << " cursor_gaps=" << result.cursor_gaps
				  << " retained_event_losses=" << result.retained_event_losses
				  << " retained_outcome_losses=" << result.retained_outcome_losses
				  << " state_persistence_errors=" << result.state_persistence_errors << "\n";
		std::cout.flush();
		if (result.Fatal()) {
			std::cerr << "actor-helper: durable state persistence failed\n";
			std::exit(1);
		}
		++cycle;
		if (cmd[{"--exit-after-outcome"}] &&
			(result.outcomes_observed != 0 || result.retained_outcome_losses != 0)) {
			break;
		}
		if (max_cycles != 0 && cycle >= max_cycles) {
			break;
		}
		// Polling is intentionally low cadence, and idle cycles always back off.
		std::this_thread::sleep_for(std::chrono::milliseconds(poll_ms));
	} while (true);
}

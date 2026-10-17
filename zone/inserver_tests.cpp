#include "inserver_tests.h"

#include "common/json/json.hpp"
#include "common/timer.h"
#include "zone/beacon.h"
#include "zone/entity.h"
#include "zone/worldserver.h"
#include "zone/zone.h"

#include <array>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <string>

extern EntityList entity_list;
extern WorldServer worldserver;
extern Zone *zone;
extern volatile bool is_zone_loaded;

namespace InserverTests {
namespace {

enum class Outcome { Pending, Passed, Failed };

struct Result {
	Outcome outcome;
	std::string diagnostic;
};

// This scenario observes registration and native timer-driven removal. It does
// not call Beacon::Process or EntityList::BeaconProcess on the test's behalf.
Result BeaconLifetime()
{
	static bool registered = false;
	static uint16 entity_id = 0;
	static uint32 started_at = 0;
	if (!registered) {
		auto *beacon = new Beacon(glm::vec4(0.0f), 250);
		entity_list.AddBeacon(beacon);
		entity_id = beacon->GetID();
		started_at = Timer::GetCurrentTime();
		registered = true;
		if (!entity_id || entity_list.GetEntityBeacon(entity_id) != beacon) {
			return {Outcome::Failed, "native beacon was not registered"};
		}
		return {Outcome::Pending, ""};
	}

	const auto elapsed = Timer::GetCurrentTime() - started_at;
	if (!entity_list.GetEntityBeacon(entity_id)) {
		if (elapsed <= 250) {
			return {Outcome::Failed, "native beacon disappeared before its lifetime expired"};
		}
		return {Outcome::Passed, "native beacon registered and expired through normal zone processing"};
	}
	if (elapsed > 5000) {
		return {Outcome::Failed, "native beacon remained registered after its lifetime expired"};
	}
	return {Outcome::Pending, ""};
}

struct Scenario {
	const char *name;
	Result (*process)();
};

// Add scenarios here. The validator runs this complete source-owned suite and
// does not select names or interpret actor mechanics.
constexpr std::array scenarios = {
	Scenario{"native_beacon_lifetime", BeaconLifetime}
};

bool enabled = false;
bool finalized = false;
bool assertion_control = false;
std::string run_id;
std::filesystem::path result_path;
size_t current = 0;
size_t started = 0;
bool case_started = false;
uint32 case_started_at = 0;
nlohmann::json results = nlohmann::json::array();

bool ValidRunId(const std::string &value)
{
	if (value.size() != 32) {
		return false;
	}
	for (const auto character : value) {
		if (!((character >= '0' && character <= '9') || (character >= 'a' && character <= 'f'))) {
			return false;
		}
	}
	return true;
}

// Publish a complete regular file without replacing an existing result. The
// fixture owns the fresh directory, and observes only the final hard-link name.
bool Publish()
{
	size_t failed = 0;
	for (const auto &result : results) {
		if (!result["passed"].get<bool>()) {
			++failed;
		}
	}
	nlohmann::json report = {
		{"version", 1}, {"run_id", run_id}, {"suite", "zone"},
		{"selected", scenarios.size()}, {"started", started},
		{"completed", results.size()}, {"failed", failed},
		{"finalized", true}, {"passed", !scenarios.empty() && failed == 0},
		{"tests", results}
	};
	const auto bytes = report.dump() + "\n";
	if (bytes.size() > 4096) {
		std::fputs("In-server test report exceeds 4096 bytes\n", stderr);
		return false;
	}
	const auto temporary = result_path.string() + ".tmp";
	FILE *file = std::fopen(temporary.c_str(), "wx");
	if (!file) {
		std::fputs("Cannot create fresh in-server test report\n", stderr);
		return false;
	}
	const bool written = std::fwrite(bytes.data(), 1, bytes.size(), file) == bytes.size();
	const bool closed = std::fclose(file) == 0;
	std::error_code error;
	if (written && closed) {
		std::filesystem::create_hard_link(temporary, result_path, error);
	}
	std::error_code ignored;
	std::filesystem::remove(temporary, ignored);
	if (!written || !closed || error) {
		std::fputs("Cannot publish in-server test report without replacing a file\n", stderr);
		return false;
	}
	return true;
}

} // namespace

bool Configure()
{
	const char *run = std::getenv("EQEMU_INSERVER_TEST_RUN");
	const char *path = std::getenv("EQEMU_INSERVER_TEST_RESULT");
	const char *control = std::getenv("EQEMU_INSERVER_TEST_CONTROL");
	if (!run && !path && !control) {
		return true;
	}
	if (!run || !path || !ValidRunId(run) || std::string(path).size() > 1024 ||
		(control && std::string(control) != "assertion")) {
		std::fputs("Invalid in-server test environment\n", stderr);
		return false;
	}
	result_path = path;
	std::error_code error;
	if (!result_path.is_absolute() || result_path.filename().empty() ||
		std::filesystem::symlink_status(result_path, error).type() != std::filesystem::file_type::not_found) {
		std::fputs("In-server test result must be a fresh absolute file path\n", stderr);
		return false;
	}
	if (scenarios.empty() || scenarios.size() > 32) {
		std::fputs("In-server suite must contain between 1 and 32 tests\n", stderr);
		return false;
	}
	for (size_t i = 0; i < scenarios.size(); ++i) {
		const std::string name = scenarios[i].name;
		if (name.empty() || name.size() > 64) {
			std::fputs("Invalid in-server scenario name\n", stderr);
			return false;
		}
		for (size_t j = 0; j < i; ++j) {
			if (name == scenarios[j].name) {
				std::fputs("Duplicate in-server scenario name\n", stderr);
				return false;
			}
		}
	}
	run_id = run;
	assertion_control = control != nullptr;
	enabled = true;
	return true;
}

void Process()
{
	if (!enabled || finalized || !zone || !is_zone_loaded || !worldserver.Connected()) {
		return;
	}
	if (!case_started) {
		case_started = true;
		++started;
		case_started_at = Timer::GetCurrentTime();
	}
	auto result = scenarios[current].process();
	if (result.outcome == Outcome::Pending && Timer::GetCurrentTime() - case_started_at > 10000) {
		result = {Outcome::Failed, "in-server scenario exceeded its 10 second deadline"};
	}
	if (result.outcome == Outcome::Pending) {
		return;
	}
	// Qualification uses the same real scenario and then proves an assertion
	// failure reaches the VM caller. It cannot replace native execution.
	if (assertion_control && result.outcome == Outcome::Passed) {
		result = {Outcome::Failed, "deliberate in-server assertion control after native processing"};
	}
	results.push_back({
		{"name", scenarios[current].name}, {"passed", result.outcome == Outcome::Passed},
		{"diagnostic", result.diagnostic.substr(0, 256)}
	});
	++current;
	case_started = false;
	if (current == scenarios.size()) {
		finalized = true;
		if (!Publish()) {
			// No completion must never look like a successful suite to the fixture.
			std::exit(1);
		}
	}
}

} // namespace InserverTests

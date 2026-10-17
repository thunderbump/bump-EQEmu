#include "inserver_tests.h"

#include "common/json/json.hpp"
#include "common/timer.h"
#include "zone/beacon.h"
#include "zone/actor_body.h"
#include "zone/npc.h"
#include "zone/entity.h"
#include "zone/worldserver.h"
#include "zone/zone.h"

#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>
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

#ifdef EQEMU_RUST_BOOTSTRAP_BRIDGE
bool actor_idle_saved = false;
bool actor_prior_idle_override = false;
uint16 actor_neighbor_id = 0;
NPC *actor_neighbor = nullptr; // compared only; never dereferenced after lookup
bool actor_shutdown_armed = false;
bool actor_shutdown_valid = false;
unsigned actor_shutdown_stages = 0;
uint16 actor_shutdown_id = 0;
uint64 actor_shutdown_epoch = 0;
uint64 actor_shutdown_generation = 0;

void CleanupActorBody()
{
    // The final step destroys the zone and its adapter. Do not reconstruct it
    // during scenario cleanup (or touch the destroyed zone's idle override).
    actor_shutdown_armed = false;
    if (zone) { ActorBody::Get().BeginTeardown(); }
    auto *neighbor = entity_list.GetNPCByID(actor_neighbor_id);
    if (neighbor && neighbor == actor_neighbor) { neighbor->Depop(false); }
    if (actor_idle_saved && zone) {
        zone->quest_idle_override = actor_prior_idle_override;
    }
    actor_idle_saved = false;
}

Result ActorBodyLifetime()
{
    using namespace eqemu_proof;
    static unsigned step = 0;
    auto &neighbor_id = actor_neighbor_id;
    static uint16 reused_id = 0;
    static uint64 old_generation = 0;
    static uint64 epoch = 0;
    static uint32 status_started = 0;
    auto &adapter = ActorBody::Get();
    const auto state = adapter.Inspect();
    auto command = [&](Kind kind, uint64 generation, uint64 zone_epoch) {
        return adapter.Send({kind, zone_epoch, generation, 0, 0, 0, 0});
    };
    auto fail = [&](const char *diagnostic) -> Result {
        // Close admission even on assertions; normal entity processing owns deletes.
        CleanupActorBody();
        return {Outcome::Failed, diagnostic};
    };
    switch (step) {
    case 0: {
        // Opt-in suite only: keep the normal entity loop active in an empty zone.
        // Restore the preexisting quest override on success, failure or timeout.
        actor_prior_idle_override = zone->quest_idle_override;
        actor_idle_saved = true;
        zone->quest_idle_override = true;
        epoch = state.epoch;
        const auto requested = command(Kind::CreateBody, state.generation, epoch);
        if (requested.event != Event::BodyRequested || !adapter.Body() ||
            adapter.Inspect().body != Body::Requested ||
            command(Kind::Retire, requested.generation, epoch).status != Status::Busy) {
            return fail("body request did not register native NPC with admission pending");
        }
        reused_id = adapter.NativeID();
        old_generation = requested.generation;
        auto *type = new NPCType{};
        std::strcpy(type->name, "ActorOrdinaryNeighbor");
        type->current_hp = type->max_hp = 100;
        type->race = type->class_ = type->level = 1;
        type->gender = 2;
        type->size = 6;
        type->bodytype = 11;
        type->untargetable = 1;
        type->skip_global_loot = true;
        auto *neighbor = new NPC(type, nullptr, glm::vec4(20, 0, 0, 0), GravityBehavior::Flying);
        neighbor->GiveNPCTypeData(type);
        entity_list.AddNPC(neighbor, false);
        neighbor_id = neighbor->GetID();
        actor_neighbor = neighbor;
        ++step;
        break;
    }
    case 1: {
        auto *body = adapter.Body();
        auto *neighbor = entity_list.GetNPCByID(neighbor_id);
        if (state.body != Body::Present || adapter.LastReport().event != Event::BodyPresent ||
            !body || !body->ActorProcessCount() || !neighbor || neighbor->IsRustActor() ||
            !neighbor->IsAIControlled() || !body->IsAIControlled()) {
            return fail("registration acknowledgement or independent native processing missing");
        }
        body->Stun(200);
        neighbor->Stun(200);
        if (!body->IsStunned() || !neighbor->IsStunned()) {
            return fail("native status timer was not armed");
        }
        status_started = Timer::GetCurrentTime();
        ++step;
        break;
    }
    case 2: {
        auto *body = adapter.Body();
        auto *neighbor = entity_list.GetNPCByID(neighbor_id);
        if (!body || !neighbor) { return fail("native status subjects disappeared"); }
        if (body->IsStunned() || neighbor->IsStunned() || !neighbor->NativeDecisionCount()) {
            if (Timer::GetCurrentTime() - status_started > 3000) {
                return fail("native stun expiry or ordinary NPC decision processing missing");
            }
            break;
        }
        if (body->NativeDecisionCount() || body->ActorProcessCount() < 2 ||
            command(Kind::Retire, state.generation, epoch).event != Event::RemovalRequested ||
            adapter.Inspect().body != Body::Removing || !entity_list.GetNPCByID(reused_id) ||
            command(Kind::CreateBody, state.generation, epoch).status != Status::Busy) {
            return fail("actor voluntary AI ran or retirement falsely acknowledged absence");
        }
        ++step;
        break;
    }
    case 3: {
        if (state.alive || state.body != Body::Absent || entity_list.GetNPCByID(reused_id) ||
            adapter.LastReport().event != Event::BodyAbsent) {
            return fail("retirement did not establish actual native absence");
        }
        if (!entity_list.ReuseFreeIDForTest(reused_id) ||
            command(Kind::CreateBody, state.generation, epoch).event != Event::BodyRequested ||
            adapter.NativeID() != reused_id) {
            return fail("deterministic native ID reuse failed");
        }
        ++step;
        break;
    }
    case 4: {
        auto *replacement = adapter.Body();
        if (!replacement || state.body != Body::Present ||
            command(Kind::Retire, old_generation, epoch).status != Status::Stale ||
            command(Kind::Retire, state.generation, epoch + 1).status != Status::Stale ||
            adapter.Lookup(epoch, old_generation, reused_id) ||
            adapter.Lookup(epoch + 1, state.generation, reused_id) ||
            !replacement->MatchesRustActor(epoch, state.generation)) {
            return fail("stale identity reached a reused native ID");
        }
        // External native removal, not a Rust retirement request.
        replacement->Depop(false);
        if (!adapter.Body()) { return fail("external depop was mistaken for completed removal"); }
        ++step;
        break;
    }
    case 5:
        if (state.alive || entity_list.GetNPCByID(reused_id) ||
            adapter.LastReport().event != Event::BodyAbsent) {
            return fail("external removal was not observed through entity processing");
        }
        if (!entity_list.ReuseFreeIDForTest(reused_id) ||
            command(Kind::CreateBody, state.generation, epoch).event != Event::BodyRequested) {
            return fail("post-external-removal replacement failed");
        }
        ++step;
        break;
    case 6:
        if (state.body != Body::Present || !adapter.Body()) {
            return fail("shutdown subject was not native-present");
        }
        adapter.BeginTeardown();
        if (command(Kind::CreateBody, state.generation, epoch).status != Status::Closed ||
            !entity_list.GetNPCByID(reused_id)) {
            return fail("teardown admission did not close before native removal");
        }
        if (auto *neighbor = entity_list.GetNPCByID(neighbor_id)) { neighbor->Depop(false); }
        ++step;
        break;
    case 7:
        if (entity_list.GetNPCByID(reused_id) || entity_list.GetNPCByID(neighbor_id) || state.alive) {
            return fail("teardown drain left a registered native subject");
        }
        adapter.FinishTeardown();
        if (!adapter.Inspect().closed || adapter.LastReport().event != Event::Shutdown) {
            return fail("teardown did not close Rust after native absence");
        }
        actor_neighbor = nullptr;
        neighbor_id = 0;
        // Reconstruct the zone-owned runtime, then reuse the same native ID.
        // Generation starts at one again; only the new epoch distinguishes it.
        ActorBody::FinishTeardown();
        if (!entity_list.ReuseFreeIDForTest(reused_id)) {
            return fail("could not reuse native ID across runtime reconstruction");
        }
        {
            auto &fresh = ActorBody::Get();
            const auto initial = fresh.Inspect();
            if (initial.epoch == epoch ||
                fresh.Send({Kind::CreateBody, initial.epoch, initial.generation, 0, 0, 0, 0}).event != Event::BodyRequested ||
                fresh.NativeID() != reused_id) {
                return fail("runtime reconstruction did not allocate a fresh zone epoch");
            }
        }
        ++step;
        break;
    case 8:
        if (state.body != Body::Present || !adapter.Body() || state.generation != old_generation ||
            command(Kind::Retire, old_generation, epoch).status != Status::Stale ||
            adapter.Lookup(epoch, old_generation, reused_id)) {
            return fail("old zone epoch reached replacement with same native ID and generation");
        }
        adapter.BeginTeardown();
        ++step;
        break;
    case 9:
        if (state.alive || entity_list.GetNPCByID(reused_id)) {
            return fail("reconstructed runtime teardown did not drain native body");
        }
        adapter.FinishTeardown();
        if (!adapter.Inspect().closed) { return fail("reconstructed runtime did not shut down"); }
        // Preserve the adapter-drain/epoch checks above, then use a fresh live
        // body to exercise the real Zone::Shutdown -> ~Zone -> Clear path.
        ActorBody::FinishTeardown();
        {
            auto &fresh = ActorBody::Get();
            const auto initial = fresh.Inspect();
            if (fresh.Send({Kind::CreateBody, initial.epoch, initial.generation, 0, 0, 0, 0}).event != Event::BodyRequested) {
                return fail("real zone shutdown body request failed");
            }
        }
        ++step;
        break;
    case 10:
        if (state.body != Body::Present || !adapter.Body() || !adapter.Body()->ActorProcessCount()) {
            return fail("real zone shutdown subject was not registered and processing");
        }
        actor_shutdown_id = adapter.NativeID();
        actor_shutdown_epoch = state.epoch;
        actor_shutdown_generation = state.generation;
        actor_shutdown_stages = 0;
        actor_shutdown_valid = true;
        actor_shutdown_armed = true;
        // Shutdown destroys both zone and adapter synchronously. Do not use
        // the references captured above after this call, even on failure.
        zone->Shutdown();
        actor_shutdown_armed = false;
        if (zone || is_zone_loaded || !actor_shutdown_valid || actor_shutdown_stages != 3 ||
            entity_list.GetNPCByID(actor_shutdown_id) || entity_list.GetMob(actor_shutdown_id)) {
            return fail("real zone shutdown missed admission/absence/Rust-close ordering");
        }
        return {Outcome::Passed, "Rust body processed; stun expired; ordinary AI retained; retire/external absence; reused-ID/epoch refusal; real zone shutdown closed admission, cleared body, then closed Rust"};
    }
    return {Outcome::Pending, ""};
}
#endif

struct Scenario {
	const char *name;
	Result (*process)();
	void (*cleanup)() = nullptr;
};

// Add scenarios here. The validator runs this complete source-owned suite and
// does not select names or interpret actor mechanics.
constexpr std::array scenarios = {
	Scenario{"native_beacon_lifetime", BeaconLifetime}
#ifdef EQEMU_RUST_BOOTSTRAP_BRIDGE
    // Destructive final scenario: publishes synchronously after real zone shutdown.
    // Keep all scenarios that require a loaded zone before this one.
    , Scenario{"rust_native_actor_body_lifetime", ActorBodyLifetime, CleanupActorBody}
#endif
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

#ifdef EQEMU_RUST_BOOTSTRAP_BRIDGE
void ObserveActorShutdown(ActorShutdownStage stage)
{
    if (!actor_shutdown_armed) { return; }
    using namespace eqemu_proof;
    auto &adapter = ActorBody::Get();
    const auto state = adapter.Inspect();
    const bool native_absent = !entity_list.GetNPCByID(actor_shutdown_id) &&
        !entity_list.GetMob(actor_shutdown_id);
    bool valid = state.epoch == actor_shutdown_epoch && state.generation == actor_shutdown_generation;
    switch (stage) {
    case ActorShutdownStage::AdmissionClosed:
        valid = valid && actor_shutdown_stages == 0 && !state.closed && state.alive &&
            state.body == Body::Removing && adapter.Body() && !native_absent &&
            adapter.Send({Kind::CreateBody, state.epoch, state.generation, 0, 0, 0, 0}).status == Status::Closed;
        break;
    case ActorShutdownStage::EntitiesCleared:
        // The native indexes must be empty while Rust is still open. The
        // following production FinishTeardown must observe absence and close.
        valid = valid && actor_shutdown_stages == 1 && native_absent &&
            !state.closed && state.alive && state.body == Body::Removing;
        break;
    case ActorShutdownStage::RuntimeClosed:
        valid = valid && actor_shutdown_stages == 2 && native_absent &&
            state.closed && !state.alive && state.body == Body::Absent &&
            adapter.LastReport().event == Event::Shutdown;
        break;
    }
    actor_shutdown_valid = actor_shutdown_valid && valid;
    ++actor_shutdown_stages;
}
#endif

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
	// A scenario may synchronously shut down the zone. Finish recording and
	// publishing its result here; do not require another loaded-zone advance.
	if (result.outcome == Outcome::Pending && Timer::GetCurrentTime() - case_started_at > 10000) {
		result = {Outcome::Failed, "in-server scenario exceeded its 10 second deadline"};
	}
	if (result.outcome == Outcome::Pending) {
		return;
	}
	if (scenarios[current].cleanup) { scenarios[current].cleanup(); }
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

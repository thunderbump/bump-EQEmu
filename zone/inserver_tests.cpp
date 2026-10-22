#include "inserver_tests.h"
#include "inserver_test_progress.h"

#include "common/json/json.hpp"
#include "common/timer.h"
#include "common/rulesys.h"
#include "zone/beacon.h"
#include "zone/actor_body.h"
#include "zone/npc.h"
#include "zone/entity.h"
#include "zone/worldserver.h"
#include "zone/zone.h"
#include "zone/mob_movement_manager.h"
#include <cmath>

#include <array>
#include <algorithm>
#include <chrono>
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
bool actor_latched_stop_armed = false;
bool actor_latched_stop_valid = false;
bool actor_shutdown_armed = false;
bool actor_shutdown_valid = false;
unsigned actor_shutdown_stages = 0;
uint16 actor_shutdown_id = 0;
uint64 actor_shutdown_epoch = 0;
uint64 actor_shutdown_generation = 0;

// Only used around the synchronous shutdown/boot transition. Never persist the
// override, and restore even when an assertion or Bootup fails.
struct KeepZoneHostAlive {
    const bool prior = RuleB(Zone, KillProcessOnDynamicShutdown);
    bool Disable() {
        return RuleManager::Instance()->SetRule("Zone:KillProcessOnDynamicShutdown", "false");
    }
    bool Restore() const {
        return RuleManager::Instance()->SetRule("Zone:KillProcessOnDynamicShutdown", prior ? "true" : "false");
    }
    ~KeepZoneHostAlive() { Restore(); }
};

void CleanupActorBody()
{
    // Restore only the current zone's saved override; never retain a Zone or
    // Adapter reference across the real shutdown/reboot transition.
    actor_shutdown_armed = false;
    actor_latched_stop_armed = false;
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
            command(Kind::Move, old_generation, epoch).status != Status::Stale ||
            command(Kind::Move, state.generation, epoch + 1).status != Status::Stale ||
            adapter.Inspect().phase != Phase::Idle || replacement->IsMoving() ||
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
    case 10: {
        if (state.body != Body::Present || !adapter.Body() || !adapter.Body()->ActorProcessCount()) {
            return fail("real zone shutdown subject was not registered and processing");
        }
        actor_shutdown_id = adapter.NativeID();
        actor_shutdown_epoch = state.epoch;
        actor_shutdown_generation = state.generation;
        actor_shutdown_stages = 0;
        actor_shutdown_valid = true;
        actor_shutdown_armed = true;
        // Capture values, not references: Shutdown destroys zone and adapter.
        const auto zone_id = zone->GetZoneID();
        const auto instance_id = zone->GetInstanceID();
        const bool is_static = zone->IsStaticZone();
        const auto zone_server_id = zone->GetZoneServerId();
        if (actor_idle_saved) { zone->quest_idle_override = actor_prior_idle_override; }
        actor_idle_saved = false;
        bool shutdown_ok = false;
        bool booted = false;
        bool restored = false;
        {
            KeepZoneHostAlive host;
            if (!host.Disable()) { return fail("could not disable test host termination"); }
            zone->Shutdown();
            actor_shutdown_armed = false;
            shutdown_ok = !zone && !is_zone_loaded && actor_shutdown_valid && actor_shutdown_stages == 3 &&
                !entity_list.GetNPCByID(actor_shutdown_id) && !entity_list.GetMob(actor_shutdown_id);
            // Recover the normal loaded-zone contract even if an assertion failed.
            if (!zone && !is_zone_loaded) {
                booted = Zone::Bootup(zone_id, instance_id, is_static);
                if (booted && zone) { zone->SetZoneServerId(zone_server_id); }
            }
            restored = host.Restore();
        }
        if (!shutdown_ok) { return fail("real zone shutdown missed admission/absence/Rust-close ordering"); }
        if (!booted || !zone || !is_zone_loaded) { return fail("replacement zone boot failed"); }
        if (!restored) { return fail("could not restore test host termination rule"); }
        actor_prior_idle_override = zone->quest_idle_override;
        actor_idle_saved = true;
        zone->quest_idle_override = true;
        auto &fresh = ActorBody::Get();
        const auto initial = fresh.Inspect();
        if (initial.epoch == actor_shutdown_epoch || initial.alive || initial.closed ||
            fresh.Send({Kind::CreateBody, initial.epoch, initial.generation, 0, 0, 0, 0}).event != Event::BodyRequested) {
            return fail("replacement zone did not admit a fresh actor identity");
        }
        ++step;
        break;
    }
    case 11:
        if (state.body != Body::Present || !adapter.Body() || !adapter.Body()->ActorProcessCount() ||
            state.epoch == actor_shutdown_epoch ||
            command(Kind::Retire, actor_shutdown_generation, actor_shutdown_epoch).status != Status::Stale ||
            adapter.Lookup(actor_shutdown_epoch, actor_shutdown_generation, adapter.NativeID())) {
            return fail("replacement zone processing or old-zone identity refusal missing");
        }
        reused_id = adapter.NativeID();
        if (command(Kind::Retire, state.generation, state.epoch).event != Event::RemovalRequested) {
            return fail("replacement zone actor retirement failed");
        }
        ++step;
        break;
    case 12:
        if (state.alive || state.body != Body::Absent || entity_list.GetNPCByID(reused_id) ||
            entity_list.GetMob(reused_id)) {
            return fail("replacement zone actor retirement did not establish absence");
        }
        return {Outcome::Passed, "Rust body processed; stun expired; ordinary AI retained; retire/external absence; reused-ID/epoch refusal; real shutdown admission/absence/Rust-close ordering; replacement zone processed and refused old identity"};
    }
    return {Outcome::Pending, ""};
}
// Each case runs through the ordinary world loop. These are conservative
// candidate bounds, NOT measured qualification values; retain per-case native
// telemetry so Operations can establish the VM baseline before acceptance.
enum class TravelCase { Arrival, LatchedStop, Stop, StopQueued, Stun, Root, Blocked, External, Reused, Suspended };
template<TravelCase mode>
Result ActorTravel()
{
    using namespace eqemu_proof;
    static unsigned step = 0;
    static uint32 began = 0, last_at = 0, acted_at = 0, settled_at = 0, terminal_at = 0;
    static unsigned moving_samples = 0;
    static glm::vec3 origin, previous, settled, action_position, replacement_origin;
    static int32 tx = 0, ty = 0, tz = 0;
    static uint16 native_id = 0;
    static uint64 old_epoch = 0, old_generation = 0;
    static float distance = 0, max_step = 0, max_drift = 0, max_action_drift = 0;
    static uint32 max_gap = 0, observed_window = 0;
    static unsigned repetition = 0;
    static std::string route, observations;
    static glm::vec3 baseline_origin;
    static std::array<int32, 3> baseline_target;
    constexpr unsigned repetitions = mode == TravelCase::Arrival || mode == TravelCase::Stop ||
        mode == TravelCase::Stun || mode == TravelCase::Root ? 3 : 1;
    auto &adapter = ActorBody::Get();
    const auto state = adapter.Inspect();
    auto *body = adapter.Body();
    const auto now = Timer::GetCurrentTime();
    static const auto case_began = now;
    auto command = [&](Kind kind, uint64 operation = 1) {
        return adapter.Send({kind, state.epoch, state.generation, operation, tx, ty, tz});
    };
    auto measurement = [&] {
        char text[160];
        const bool arrived = mode == TravelCase::Arrival || mode == TravelCase::LatchedStop || mode == TravelCase::Reused;
        // Display precision only. All assertions below use native float values.
        char error[40];
        if (arrived && settled_at) {
            std::snprintf(error, sizeof(error), "%.3f,%.3f", std::hypot(settled.x - tx, settled.y - ty), std::abs(settled.z - tz));
        } else {
            std::snprintf(error, sizeof(error), "-,-");
        }
        char latency[16];
        if (acted_at) {
            std::snprintf(latency, sizeof(latency), "%u", (terminal_at ? terminal_at : now) - acted_at);
        } else {
            std::snprintf(latency, sizeof(latency), "-");
        }
        const auto written = std::snprintf(text, sizeof(text), "%u,%s,%s,%.3f,%u,%.2f,%.2f,%u,%.3f",
            now - began, error, latency, max_drift, observed_window, distance, max_step, max_gap, max_action_drift);
        return written >= 0 && static_cast<size_t>(written) < sizeof(text) ? std::string(text) : std::string("omitted-size");
    };
    auto diagnostic = [&](const char *reason) {
        char text[128];
        std::snprintf(text, sizeof(text), "%.56s stage=%u repeat=%u/%u ", reason, step,
            std::min(repetition + 1, repetitions), repetitions);
        const auto snapshot = ActorBody::Get().Inspect();
        char terminal[64];
        std::snprintf(terminal, sizeof(terminal), " phase=%u terminal=%u op=%llu",
            static_cast<unsigned>(snapshot.phase), static_cast<unsigned>(snapshot.last_terminal),
            static_cast<unsigned long long>(snapshot.last_operation));
        const auto detail = std::string(text) + terminal + " " + route + " m=" + measurement();
        std::fprintf(stderr, "actor travel case=%u %s\n", static_cast<unsigned>(mode), detail.c_str());
        // Keep the current measurement and terminal correlation if unusually
        // large coordinates cannot fit. Mark the omitted route explicitly.
        return detail.size() <= 256 ? detail : std::string(text) + terminal + " r=omitted-size m=" + measurement();
    };
    auto fail = [&](const char *why) { return Result{Outcome::Failed, diagnostic(why)}; };
    // The registry keeps its independent outer gate. Capture measurements here
    // first when all repetitions exhaust that same 10-second case allowance.
    if (now - case_began > 10000) { return fail("in-server scenario deadline"); }
    if (step && now - began > 7500) { return fail("native travel deadline"); }
    if (step && body) {
        const glm::vec3 position(body->GetX(), body->GetY(), body->GetZ());
        const auto delta = glm::distance(position, previous);
        max_gap = std::max(max_gap, now - last_at);
        max_step = std::max(max_step, delta);
        distance += delta;
        // Observe real samples, not request coordinates. Bound frame displacement
        // by native speed and elapsed time; a teleport is never accepted as travel.
        const float bound = (now - last_at) * 0.001f * body->GetRunspeed() * 0.4f * 1.45f + 2.0f;
        if (delta > bound || delta > 8 || now - last_at > 250 || body->NativeDecisionCount()) {
            return fail("unobservable sample gap, jump or competing AI");
        }
        if (delta > 0.05f) { ++moving_samples; }
        if (step == 4 && mode != TravelCase::External && mode != TravelCase::Reused) {
            max_action_drift = std::max(max_action_drift, glm::distance(position, action_position));
        }
        if (step == 4 && mode != TravelCase::External && mode != TravelCase::Reused && max_action_drift > 0.05f) {
            std::fprintf(stderr, "actor stop drift case=%u action=(%.3f,%.3f,%.3f) observed=(%.3f,%.3f,%.3f)\n",
                static_cast<unsigned>(mode), action_position.x, action_position.y, action_position.z,
                position.x, position.y, position.z);
            return fail("native drift after stop/control action");
        }
        previous = position;
        last_at = now;
    }
    switch (step) {
    case 0: {
        // Prior case cleanup closes admission. Drain it through the real loop
        // before reconstructing; never recycle a still-live body/runtime.
        if (state.alive) { return {Outcome::Pending, ""}; }
        ActorBody::FinishTeardown();
        auto &fresh = ActorBody::Get();
        const auto initial = fresh.Inspect();
        actor_prior_idle_override = zone->quest_idle_override;
        actor_idle_saved = true;
        zone->quest_idle_override = true;
        const auto safe = zone->GetSafePoint();
        tx = static_cast<int32>(std::lround(safe.x));
        ty = static_cast<int32>(std::lround(safe.y));
        tz = static_cast<int32>(std::lround(safe.z));
        if (fresh.Send({Kind::CreateBody, initial.epoch, initial.generation, 0, tx, ty, tz}).event != Event::BodyRequested) {
            return fail("travel body creation refused");
        }
        body = fresh.Body();
        native_id = fresh.NativeID();
        old_epoch = fresh.Inspect().epoch;
        old_generation = fresh.Inspect().generation;
        if (!body || !zone->zonemap) { return fail("native body/map missing"); }
        origin = previous = glm::vec3(body->GetX(), body->GetY(), body->GetZ());
        moving_samples = 0;
        distance = max_step = max_drift = max_action_drift = 0;
        max_gap = observed_window = 0;
        acted_at = settled_at = terminal_at = 0;
        last_at = began = now;
        tx += 40;
        // FindBestZ adjusts its input height; keep that mutation in a probe,
        // separate from the requested endpoint and observed native position.
        glm::vec3 ground_probe(tx, ty, tz + 20);
        const auto ground = zone->zonemap->FindBestZ(ground_probe, nullptr);
        if (ground == BEST_Z_INVALID) { return fail("short route endpoint has no native ground"); }
        tz = static_cast<int32>(std::lround(ground + body->GetZOffset()));
        if constexpr (mode == TravelCase::Blocked) {
            // Require a real map obstruction within the admitted locality bound,
            // not merely an out-of-range request or a synthetic Rust rejection.
            bool found = false;
            for (unsigned direction = 0; direction < 16 && !found; ++direction) {
                const float angle = direction * 6.2831853f / 16;
                const int32 x = static_cast<int32>(std::lround(origin.x + 60 * std::cos(angle)));
                const int32 y = static_cast<int32>(std::lround(origin.y + 60 * std::sin(angle)));
                glm::vec3 floor_probe(x, y, origin.z + 20);
                const auto floor = zone->zonemap->FindBestZ(floor_probe, nullptr);
                if (floor == BEST_Z_INVALID) { continue; }
                const int32 z = static_cast<int32>(std::lround(floor + body->GetZOffset()));
                if (std::abs(z - origin.z) <= 6 &&
                    !zone->zonemap->CheckLoS(origin, glm::vec3(x, y, z))) {
                    tx = x; ty = y; tz = z; found = true;
                }
            }
            if (!found) { return fail("no measured local map obstruction; route qualification required"); }
        }
        if (!repetition) {
            baseline_origin = origin;
            baseline_target = {tx, ty, tz};
            char text[128];
            std::snprintf(text, sizeof(text), "r=%.2f,%.2f,%.2f>%d,%d,%d v=%.2f",
                origin.x, origin.y, origin.z, tx, ty, tz, body->GetRunspeed());
            route = text;
        } else if (origin != baseline_origin || baseline_target != std::array<int32, 3>{tx, ty, tz}) {
            return fail("repeat route changed");
        }
        std::fprintf(stderr, "actor travel case=%u start=(%.2f,%.2f,%.2f) end=(%d,%d,%d) "
            "candidate_bounds arrival_xy=2 z=6 action_ms=1000 drift=0.05 window_ms=400 total_ms=7500\n",
            static_cast<unsigned>(mode), origin.x, origin.y, origin.z, tx, ty, tz);
        auto *type = new NPCType{};
        std::strcpy(type->name, "ActorTravelNeighbor");
        type->current_hp = type->max_hp = 100;
        type->race = type->class_ = type->level = 1;
        type->gender = 2;
        type->size = 6;
        type->bodytype = 11;
        type->untargetable = 1;
        type->skip_global_loot = true;
        auto *neighbor = new NPC(type, nullptr, glm::vec4(origin + glm::vec3(0, 20, 0), 0), GravityBehavior::Flying);
        neighbor->GiveNPCTypeData(type);
        entity_list.AddNPC(neighbor, false);
        actor_neighbor_id = neighbor->GetID();
        actor_neighbor = neighbor;
        neighbor->Stun(200);
        step = 1;
        break;
    }
    case 1:
        if (state.body != Body::Present) { break; }
        if (!body) { return fail("native body disappeared before travel admission"); }
        if constexpr (mode == TravelCase::Suspended) {
            body->Mesmerize();
            if (!body->IsMezzed()) { return fail("native forced-control suspension missing"); }
        }
        if (command(Kind::Move).event != Event::MoveRequested || adapter.Inspect().phase != Phase::Requested) {
            return fail("move request/admission conflated");
        }
        step = 2;
        break;
    case 2:
        if (state.phase == Phase::Requested) { break; }
        if constexpr (mode == TravelCase::Blocked || mode == TravelCase::Suspended) {
            if (state.last_terminal != Event::Rejected || state.last_operation != 1 ||
                !body || body->IsMoving() || glm::distance(previous, origin) > 0.05f) {
                return fail("blocked/control-suspended request moved or lacked rejection");
            }
            settled = previous;
            settled_at = now;
            step = 5;
        } else {
            if (state.phase != Phase::Admitted || !body) { return fail("clear route was not admitted"); }
            if constexpr (mode == TravelCase::StopQueued) {
                // Admission runs after the movement manager in the ordinary
                // loop: cancel retained commands before their first execution.
                if (body->IsMoving() || glm::distance(previous, origin) > 0.05f ||
                    command(Kind::Stop).event != Event::StopRequested) {
                    return fail("stop before first native movement failed");
                }
                action_position = previous;
                acted_at = now;
                step = 4;
            } else {
                if constexpr (mode == TravelCase::LatchedStop) {
                    actor_latched_stop_valid = false;
                    actor_latched_stop_armed = true;
                }
                step = 3;
            }
        }
        break;
    case 3:
        if (!body) { return fail("body vanished before deliberate action"); }
        if constexpr (mode == TravelCase::Arrival || mode == TravelCase::LatchedStop) {
            if (state.phase != Phase::Idle) { break; }
            if constexpr (mode == TravelCase::LatchedStop) {
                if (actor_latched_stop_armed || !actor_latched_stop_valid) {
                    return fail("completion-before-stop interposition missing or changed native latch");
                }
            }
            if (state.last_terminal != Event::Completed || state.last_operation != 1 ||
                distance < 20 || moving_samples < 3 || std::hypot(body->GetX() - tx, body->GetY() - ty) > 2 ||
                std::abs(body->GetZ() - tz) > 6 || body->IsMoving()) {
                return fail("arrival disagrees with native travel");
            }
            std::fprintf(stderr, "actor native arrival ms=%u samples=%u xy_error=%.3f z_error=%.3f pos=(%.3f,%.3f,%.3f)\n",
                now - began, moving_samples, std::hypot(body->GetX() - tx, body->GetY() - ty),
                std::abs(body->GetZ() - tz), previous.x, previous.y, previous.z);
            settled = previous;
            settled_at = now;
            step = 5;
        } else {
            if (distance < 4 || !body->IsMoving()) { break; }
            if (command(Kind::Stop, 2).status != Status::InvalidResult ||
                adapter.Send({Kind::Move, state.epoch, state.generation - 1, 2, tx, ty, tz}).status != Status::Stale ||
                command(Kind::Completed).status != Status::InvalidInput) {
                return fail("wrong operation/stale identity/caller native fact accepted");
            }
            if constexpr (mode == TravelCase::Stop) {
                if (command(Kind::Stop).event != Event::StopRequested) { return fail("stop not requested"); }
            } else if constexpr (mode == TravelCase::Stun) {
                body->Stun(1500);
                if (!body->IsStunned()) { return fail("native stun not armed"); }
            } else if constexpr (mode == TravelCase::Root) {
                // Real native spell effects, not a Rust declaration or mock root.
                if (!body->SpellEffect(body, 230) || !body->IsRooted()) { return fail("native Root spell failed"); }
            } else if constexpr (mode == TravelCase::External || mode == TravelCase::Reused) {
                body->Depop(false);
            }
            action_position = previous;
            acted_at = now;
            step = 4;
        }
        break;
    case 4:
        if (now - acted_at > 1000) { return fail("native stop/interruption deadline"); }
        if (state.phase != Phase::Idle || (body && body->IsMoving())) { break; }
        if (state.last_terminal != Event::Interrupted || state.last_operation != 1) {
            return fail("native interruption missing or wrong operation");
        }
        if constexpr (mode == TravelCase::External || mode == TravelCase::Reused) {
            if (state.alive || entity_list.GetMob(native_id) || entity_list.GetNPCByID(native_id)) { break; }
        } else if (!body) { return fail("interrupted body disappeared"); }
        terminal_at = now;
        std::fprintf(stderr, "actor native terminal case=%u latency_ms=%u samples=%u pos=(%.3f,%.3f,%.3f)\n",
            static_cast<unsigned>(mode), now - acted_at, moving_samples, previous.x, previous.y, previous.z);
        if constexpr (mode == TravelCase::Reused) {
            if (!entity_list.ReuseFreeIDForTest(native_id) ||
                adapter.Send({Kind::CreateBody, state.epoch, state.generation, 0,
                    static_cast<int32>(origin.x), static_cast<int32>(origin.y), static_cast<int32>(origin.z)}).event != Event::BodyRequested ||
                adapter.NativeID() != native_id) {
                return fail("travel replacement did not reuse native ID");
            }
            // CreateBody takes integer coordinates, whereas origin is a native
            // float observation. Truncating origin (notably negative Z) need not
            // reproduce it. Establish the replacement's native baseline now,
            // before its first normal-loop processing; do not count spawning as
            // travel or excuse any later displacement as coordinate conversion.
            auto *replacement = adapter.Body();
            if (!replacement || replacement->IsMoving() ||
                MobMovementManager::Get().InspectActorTravel(replacement) != MobMovementManager::ActorTravel::Idle) {
                return fail("replacement inherited navigation at creation");
            }
            replacement_origin = previous = glm::vec3(replacement->GetX(), replacement->GetY(), replacement->GetZ());
            std::fprintf(stderr, "actor replacement old=(%.3f,%.3f,%.3f) native=(%.3f,%.3f,%.3f)\n",
                origin.x, origin.y, origin.z, replacement_origin.x, replacement_origin.y, replacement_origin.z);
            last_at = now;
            step = 7;
            break;
        }
        settled = previous;
        settled_at = now;
        step = 5;
        break;
    case 7:
        if (state.body != Body::Present) { break; }
        if (!body || state.generation == old_generation ||
            adapter.Lookup(old_epoch, old_generation, native_id) ||
            MobMovementManager::Get().InspectActorTravel(body) != MobMovementManager::ActorTravel::Idle ||
            body->IsMoving() || glm::distance(previous, replacement_origin) > 0.05f) {
            return fail("replacement moved before fresh request");
        }
        // Keep observing the idle replacement through normal movement ticks,
        // so delayed old work cannot hide behind its fresh navigation request.
        if (now - terminal_at < 400) { break; }
        if (adapter.Send({Kind::Stop, old_epoch, old_generation, 1, 0, 0, 0}).status != Status::Stale ||
            adapter.Send({Kind::Move, old_epoch, old_generation, 2, tx, ty, tz}).status != Status::Stale ||
            command(Kind::Move, 2).event != Event::MoveRequested) {
            return fail("replacement stale rejection or fresh request failed");
        }
        step = 8;
        break;
    case 8:
        // Reject stale cancellation while a replacement really has pending
        // travel, not only while it is idle. No test-driven movement ticks.
        if (!body || adapter.Send({Kind::Stop, old_epoch, old_generation, 2, 0, 0, 0}).status != Status::Stale ||
            adapter.Send({Kind::Move, state.epoch + 1, state.generation, 3, tx, ty, tz}).status != Status::Stale) {
            return fail("stale command accepted during replacement movement");
        }
        if (state.phase != Phase::Idle) { break; }
        if (state.last_terminal != Event::Completed || state.last_operation != 2 || body->IsMoving() ||
            std::hypot(body->GetX() - tx, body->GetY() - ty) > 2 || std::abs(body->GetZ() - tz) > 6) {
            return fail("replacement travel interrupted by old identity or pending work");
        }
        settled = previous;
        settled_at = now;
        step = 5;
        break;
    case 5: {
        const auto expected_terminal = (mode == TravelCase::Arrival || mode == TravelCase::LatchedStop || mode == TravelCase::Reused)
            ? Event::Completed : (mode == TravelCase::Blocked || mode == TravelCase::Suspended) ? Event::Rejected : Event::Interrupted;
        if (state.phase != Phase::Idle || state.last_terminal != expected_terminal ||
            state.last_operation != (mode == TravelCase::Reused ? 2u : 1u)) {
            return fail("terminal correlation changed after settlement");
        }
        if (body) { max_drift = std::max(max_drift, glm::distance(previous, settled)); }
        observed_window = now - settled_at;
        if (body && (glm::distance(previous, settled) > 0.05f || body->IsMoving())) {
            return fail("post-terminal native drift");
        }
        if (now - settled_at < 400) { break; }
        auto *neighbor = entity_list.GetNPCByID(actor_neighbor_id);
        if (!neighbor || neighbor != actor_neighbor || neighbor->IsRustActor() ||
            neighbor->IsStunned() || !neighbor->NativeDecisionCount()) {
            return fail("ordinary neighbor control/status regressed");
        }
        if (state.alive && command(Kind::Retire, 0).event != Event::RemovalRequested) {
            return fail("travel retirement refused");
        }
        step = 6;
        break;
    }
    case 6: {
        if (state.alive || body || entity_list.GetMob(native_id) || entity_list.GetNPCByID(native_id)) { break; }
        const auto sample = measurement();
        if (sample == "omitted-size") { return fail("measurement sample exceeds buffer"); }
        std::fprintf(stderr, "actor measurement case=%u repeat=%u sample=%s\n",
            static_cast<unsigned>(mode), repetition + 1, sample.c_str());
        observations += (observations.empty() ? "" : ";") + sample;
        if (++repetition < repetitions) {
            // Retire and actual absence were checked above. Restore the scoped
            // idle override before the next ordinary-loop reconstruction.
            CleanupActorBody();
            step = 0;
            return {Outcome::Pending, ""};
        }
        char position[80];
        std::snprintf(position, sizeof(position), " p=%.2f,%.2f,%.2f", settled.x, settled.y, settled.z);
        const auto summary = "m1 " + route + position + " m=" + observations;
        if (summary.size() > 256) { return fail("measurement diagnostic exceeds256 bytes"); }
        return {Outcome::Passed, summary};
    }
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
    // Exercises real shutdown, then boots a replacement in the same zone host.
    , Scenario{"rust_native_actor_body_lifetime", ActorBodyLifetime, CleanupActorBody}
    , Scenario{"rust_native_actor_arrival", ActorTravel<TravelCase::Arrival>, CleanupActorBody}
    , Scenario{"rust_native_actor_latched_stop", ActorTravel<TravelCase::LatchedStop>, CleanupActorBody}
    , Scenario{"rust_native_actor_reused_travel", ActorTravel<TravelCase::Reused>, CleanupActorBody}
    , Scenario{"rust_native_actor_stop", ActorTravel<TravelCase::Stop>, CleanupActorBody}
    , Scenario{"rust_native_actor_stop_queued", ActorTravel<TravelCase::StopQueued>, CleanupActorBody}
    , Scenario{"rust_native_actor_stun", ActorTravel<TravelCase::Stun>, CleanupActorBody}
    , Scenario{"rust_native_actor_root", ActorTravel<TravelCase::Root>, CleanupActorBody}
    , Scenario{"rust_native_actor_blocked", ActorTravel<TravelCase::Blocked>, CleanupActorBody}
    , Scenario{"rust_native_actor_external_depop", ActorTravel<TravelCase::External>, CleanupActorBody}
    , Scenario{"rust_native_actor_control_suspended", ActorTravel<TravelCase::Suspended>, CleanupActorBody}
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
std::chrono::steady_clock::time_point progress_started_at;
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
void BeforeActorFacts()
{
    if (!actor_latched_stop_armed) { return; }
    auto &adapter = ActorBody::Get();
    auto *body = adapter.Body();
    auto &movement = MobMovementManager::Get();
    if (!body || movement.InspectActorTravel(body) != MobMovementManager::ActorTravel::Completed) { return; }
    using namespace eqemu_proof;
    const auto state = adapter.Inspect();
    const auto position = glm::vec3(body->GetX(), body->GetY(), body->GetZ());
    const auto report = adapter.Send({Kind::Stop, state.epoch, state.generation, state.operation, 0, 0, 0});
    actor_latched_stop_valid = state.phase == Phase::Admitted && !body->IsMoving() &&
        report.event == Event::StopRequested && adapter.Inspect().phase == Phase::Stopping &&
        movement.InspectActorTravel(body) == MobMovementManager::ActorTravel::Completed &&
        glm::distance(position, glm::vec3(body->GetX(), body->GetY(), body->GetZ())) == 0;
    actor_latched_stop_armed = false;
}

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
		progress_started_at = std::chrono::steady_clock::now();
		EmitCaseProgress(stderr, run_id, scenarios[current].name, 0);
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
	// Qualification uses the same real scenario and then proves an assertion
	// failure reaches the VM caller. It cannot replace native execution.
	if (assertion_control && result.outcome == Outcome::Passed) {
		result = {Outcome::Failed, "deliberate in-server assertion control after native processing"};
	}
	EmitCaseProgress(stderr, run_id, scenarios[current].name,
		std::chrono::duration_cast<std::chrono::milliseconds>(
			std::chrono::steady_clock::now() - progress_started_at).count(),
		result.outcome == Outcome::Passed, result.diagnostic);
	if (scenarios[current].cleanup) { scenarios[current].cleanup(); }
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

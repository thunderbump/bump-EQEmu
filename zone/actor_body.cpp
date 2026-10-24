#include "actor_body.h"
#ifdef EQEMU_BUILD_TESTS
#include "zone/inserver_tests.h"
#endif
#ifdef EQEMU_RUST_BOOTSTRAP_BRIDGE
#include "zone/entity.h"
#include "zone/npc.h"
#include "zone/mob_movement_manager.h"
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <memory>
#include <utility>

extern EntityList entity_list;
namespace ActorBody {
namespace {
uint64_t next_epoch = 0;
std::unique_ptr<Adapter> adapter;
uint64_t NewEpoch()
{
    if (next_epoch == std::numeric_limits<uint64_t>::max()) { std::abort(); }
    return ++next_epoch;
}
}

Adapter::Adapter() : runtime_(eqemu_proof::construct(NewEpoch())) {}

NPC *Adapter::Lookup(uint64_t epoch, uint64_t generation, uint16_t id) const
{
    const auto state = Inspect();
    if (epoch != state.epoch || generation != state.generation || !state.alive ||
        id != id_ || !id) { return nullptr; }
    // Check both registries: removing only the NPC index is not native absence.
    auto *mob = entity_list.GetMob(id);
    auto *npc = mob && mob->IsNPC() ? mob->CastToNPC() : entity_list.GetNPCByID(id);
    return npc && npc->MatchesRustActor(epoch, generation) ? npc : nullptr;
}

NPC *Adapter::Body() const
{
    const auto state = Inspect();
    return Lookup(state.epoch, state.generation, id_);
}

eqemu_proof::Report Adapter::Advance(eqemu_proof::Input input, bool empty)
{
    const auto state = Inspect();
    if (state.sequence == std::numeric_limits<uint64_t>::max() ||
        state.tick == std::numeric_limits<uint64_t>::max()) { std::abort(); }
    rust::Vec<eqemu_proof::Input> inputs;
    if (!empty) { inputs.push_back(input); }
    last_ = runtime_->advance(state.sequence + 1, state.tick + 1, std::move(inputs));
    Apply(last_);
    return last_;
}

eqemu_proof::Report Adapter::Send(eqemu_proof::Input input)
{
    using namespace eqemu_proof;
    // Native facts are not caller commands. Admission closes before teardown.
    if (closing_ || (input.kind != Kind::CreateBody && input.kind != Kind::Retire &&
        input.kind != Kind::Shutdown && input.kind != Kind::Move && input.kind != Kind::Stop &&
        input.kind != Kind::StartPatrol && input.kind != Kind::Pause && input.kind != Kind::Resume)) {
        Report refused{};
        refused.status = closing_ ? Status::Closed : Status::InvalidInput;
        return refused;
    }
    return Advance(input);
}

eqemu_proof::Report Adapter::StartPatrol(int32_t ax, int32_t ay, int32_t az,
    int32_t bx, int32_t by, int32_t bz)
{
    using namespace eqemu_proof;
    auto state = Inspect();
    if (ax == bx && ay == by && az == bz) {
        Report refused{};
        refused.status = Status::InvalidInput;
        return refused;
    }
    auto report = Send({Kind::CreateBody, state.epoch, state.generation, 0, ax, ay, az});
    if (report.status != Status::Ok) { return report; }
    state = Inspect();
    return Send({Kind::StartPatrol, state.epoch, state.generation, 0, bx, by, bz});
}

void Adapter::Apply(const eqemu_proof::Report &report)
{
    using namespace eqemu_proof;
    if (report.status != Status::Ok) { return; }
    if (report.event == Event::BodyRequested) {
        auto *type = new NPCType{};
        std::strcpy(type->name, "RustActorBody");
        type->current_hp = type->max_hp = 100;
        type->race = 1;
        type->class_ = 1;
        type->level = 1;
        type->gender = 2;
        type->size = 6;
        type->runspeed = 1.25f;
        type->bodytype = 11;
        type->untargetable = 1;
        type->skip_global_loot = true;
        auto *npc = new NPC(type, nullptr,
            glm::vec4(report.x, report.y, report.z, 0), GravityBehavior::Flying);
        npc->GiveNPCTypeData(type);
        npc->actor_epoch_ = report.epoch;
        npc->actor_generation_ = report.generation;
        // Do not AI_Stop or clear IsAIControlled: the explicit gate owns decisions.
        entity_list.AddNPC(npc, false);
        id_ = npc->GetID();
    } else if (report.event == Event::MoveRequested) {
        target_x_ = report.x;
        target_y_ = report.y;
        target_z_ = report.z;
    } else if (report.event == Event::StopRequested) {
        if (auto *npc = Body()) {
            auto &movement = MobMovementManager::Get();
            // A native completion already latched before Stop remains truthful.
            if (movement.InspectActorTravel(npc) == MobMovementManager::ActorTravel::Moving) {
                movement.StopNavigation(npc);
            }
        }
    } else if (report.event == Event::RemovalRequested) {
        if (auto *npc = Body()) { npc->Depop(false); }
    }
}

void Adapter::Process()
{
    using namespace eqemu_proof;
    const auto state = Inspect();
    if (state.closed || !state.alive) { return; }
    if (state.body == eqemu_proof::Body::Present && state.phase != Phase::Idle) {
        auto *npc = Body();
        auto &movement = MobMovementManager::Get();
        if (state.phase == Phase::Requested) {
            const bool admitted = !closing_ &&
                (state.patrol == Patrol::Disabled || state.patrol == Patrol::Running) && npc &&
                movement.NavigateActorTo(npc, target_x_, target_y_, target_z_);
            Advance({admitted ? Kind::Admitted : Kind::Rejected,
                state.epoch, state.generation, state.operation, 0, 0, 0});
        } else {
            const auto travel = npc ? movement.InspectActorTravel(npc) :
                MobMovementManager::ActorTravel::Interrupted;
            if (travel == MobMovementManager::ActorTravel::Moving || (npc && npc->IsMoving())) { return; }
            // Empty native command queue is not alone proof of arrival. Observe
            // the real body coordinates, never the Rust request/snapshot.
            const bool arrived = npc && travel == MobMovementManager::ActorTravel::Completed &&
                std::hypot(npc->GetX() - target_x_, npc->GetY() - target_y_) <= 2.0f &&
                std::abs(npc->GetZ() - target_z_) <= 6.0f;
            Advance({arrived ? Kind::Completed : Kind::Interrupted,
                state.epoch, state.generation, state.operation, 0, 0, 0});
        }
        return; // retain exactly one owned fact per normal zone advance
    }
    // Observe registration/absence on a later serialized zone advance, not while
    // applying the request. Depop is NOT absence: wait for the entity loop.
    if (state.body == eqemu_proof::Body::Requested) {
        auto *npc = Body();
        const bool registered = npc && entity_list.GetNPCByID(id_) == npc &&
            entity_list.GetMob(id_) == npc;
        // Partial registration cannot be reported as rejection/absence while a
        // native body survives. Wait for normal removal instead.
        if (npc && !registered) { npc->Depop(false); return; }
        Advance({registered ? Kind::BodyPresent : Kind::BodyRejected,
            state.epoch, state.generation, 0, 0, 0, 0});
    } else if ((state.body == eqemu_proof::Body::Present || state.body == eqemu_proof::Body::Removing) && !Body()) {
        Advance({Kind::BodyAbsent, state.epoch, state.generation, 0, 0, 0, 0});
        id_ = 0;
    } else {
        Advance({}, true);
    }
}

void Adapter::BeginTeardown()
{
    closing_ = true;
    auto state = Inspect();
    if (state.alive && state.patrol != eqemu_proof::Patrol::Disabled) {
        Advance({eqemu_proof::Kind::Retire, state.epoch, state.generation, 0, 0, 0, 0});
        state = Inspect();
    }
    if (state.phase == eqemu_proof::Phase::Requested) {
        Advance({eqemu_proof::Kind::Rejected, state.epoch, state.generation, state.operation, 0, 0, 0});
    } else if (state.phase != eqemu_proof::Phase::Idle) {
        if (auto *npc = Body()) {
            auto &movement = MobMovementManager::Get();
            if (movement.InspectActorTravel(npc) == MobMovementManager::ActorTravel::Moving) {
                movement.StopNavigation(npc);
            }
        }
        Process(); // truthful latched completion, or wait for native stop/absence
    }
    state = Inspect();
    if (state.body == eqemu_proof::Body::Present && state.phase == eqemu_proof::Phase::Idle) {
        Advance({eqemu_proof::Kind::Retire, state.epoch, state.generation, 0, 0, 0, 0});
    }
    if (auto *npc = Body()) { npc->Depop(false); }
}

void Adapter::FinishTeardown()
{
    // At most one pending terminal, then absence. Both are serialized owned
    // facts, never an implicit eviction when a body disappears during travel.
    Process();
    Process(); // observes actual absence after EntityList::Clear or native drain
    const auto state = Inspect();
    if (state.alive || Body()) { std::abort(); }
    if (!state.closed) {
        Advance({eqemu_proof::Kind::Shutdown, state.epoch, state.generation, 0, 0, 0, 0});
    }
}

Adapter &Get()
{
    if (!adapter) { adapter = std::make_unique<Adapter>(); }
    return *adapter;
}
void Process() { if (adapter) { adapter->Process(); } }
void BeginTeardown() { if (adapter) { adapter->BeginTeardown(); } }
void FinishTeardown()
{
    if (adapter) {
        adapter->FinishTeardown();
#ifdef EQEMU_BUILD_TESTS
        InserverTests::ObserveActorShutdown(InserverTests::ActorShutdownStage::RuntimeClosed);
#endif
        adapter.reset();
    }
}
} // namespace ActorBody
#endif

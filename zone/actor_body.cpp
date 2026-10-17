#include "actor_body.h"
#ifdef EQEMU_BUILD_TESTS
#include "zone/inserver_tests.h"
#endif
#ifdef EQEMU_RUST_BOOTSTRAP_BRIDGE
#include "zone/entity.h"
#include "zone/npc.h"
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

eqemu_proof::Report Adapter::Advance(eqemu_proof::Input input)
{
    const auto state = Inspect();
    if (state.sequence == std::numeric_limits<uint64_t>::max() ||
        state.tick == std::numeric_limits<uint64_t>::max()) { std::abort(); }
    rust::Vec<eqemu_proof::Input> inputs;
    inputs.push_back(input);
    last_ = runtime_->advance(state.sequence + 1, state.tick + 1, std::move(inputs));
    return last_;
}

eqemu_proof::Report Adapter::Send(eqemu_proof::Input input)
{
    using namespace eqemu_proof;
    // Native facts are not caller commands. Admission closes before teardown.
    if (closing_ || (input.kind != Kind::CreateBody && input.kind != Kind::Retire &&
        input.kind != Kind::Shutdown)) {
        Report refused{};
        refused.status = closing_ ? Status::Closed : Status::InvalidInput;
        return refused;
    }
    auto report = Advance(input);
    if (report.status != Status::Ok) { return report; }
    if (report.event == Event::BodyRequested) {
        auto *type = new NPCType{};
        std::strcpy(type->name, "RustActorBody");
        type->current_hp = type->max_hp = 100;
        type->race = 1;
        type->class_ = 1;
        type->level = 1;
        type->gender = 2;
        type->size = 6;
        type->bodytype = 11;
        type->untargetable = 1;
        type->skip_global_loot = true;
        auto *npc = new NPC(type, nullptr,
            glm::vec4(input.x, input.y, input.z, 0), GravityBehavior::Flying);
        npc->GiveNPCTypeData(type);
        npc->actor_epoch_ = report.epoch;
        npc->actor_generation_ = report.generation;
        // Do not AI_Stop or clear IsAIControlled: the explicit gate owns decisions.
        entity_list.AddNPC(npc, false);
        id_ = npc->GetID();
    } else if (report.event == Event::RemovalRequested) {
        if (auto *npc = Body()) { npc->Depop(false); }
    }
    return report;
}

void Adapter::Process()
{
    using namespace eqemu_proof;
    const auto state = Inspect();
    if (state.closed || !state.alive) { return; }
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
    }
}

void Adapter::BeginTeardown()
{
    closing_ = true;
    const auto state = Inspect();
    if (state.body == eqemu_proof::Body::Present) {
        Advance({eqemu_proof::Kind::Retire, state.epoch, state.generation, 0, 0, 0, 0});
    }
    if (auto *npc = Body()) { npc->Depop(false); }
}

void Adapter::FinishTeardown()
{
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

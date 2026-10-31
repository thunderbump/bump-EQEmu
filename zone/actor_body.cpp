#include "actor_body.h"
#include "actor_demo.h"
#ifdef EQEMU_BUILD_TESTS
#include "zone/inserver_tests.h"
#endif
#ifdef EQEMU_RUST_BOOTSTRAP_BRIDGE
#include "zone/entity.h"
#include "zone/npc.h"
#include "zone/mob_movement_manager.h"
#include "zone/zone.h"
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <memory>
#include <utility>

extern EntityList entity_list;
extern Zone *zone;
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

bool Adapter::HasPendingTargetKill(uint64_t target_incarnation) const
{
    const auto s = Inspect();
    return combat_.killed && s.alive && s.attacking && s.damage > 0 &&
        combat_.epoch == s.epoch && combat_.generation == s.generation &&
        combat_.goal == s.goal && combat_.operation == s.operation && s.operation &&
        combat_.target == target_incarnation && s.target == target_incarnation;
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
    if (closing_ || (input.kind != Kind::CreateBody && input.kind != Kind::CreateMeleeBody && input.kind != Kind::Retire &&
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

eqemu_proof::Report Adapter::StartApproach(const std::vector<uint16_t> &scope)
{
    return StartGoal(scope, false);
}

eqemu_proof::Report Adapter::StartMelee(const std::vector<uint16_t> &scope)
{
    return StartGoal(scope, true);
}

eqemu_proof::Report Adapter::CreateVisibleMeleeBody(int32_t x, int32_t y, int32_t z)
{
    const auto state = Inspect();
    visible_creation_ = true;
    const auto report = Send({eqemu_proof::Kind::CreateMeleeBody,
        state.epoch, state.generation, 0, x, y, z});
    visible_creation_ = false;
    return report;
}

eqemu_proof::Report Adapter::StartGoal(const std::vector<uint16_t> &scope, bool melee)
{
    using namespace eqemu_proof;
    Report refused{};
    refused.status = closing_ ? Status::Closed : Status::InvalidInput;
    if (closing_ || scope.size() > scope_.size()) { return refused; }
    std::array<Subject, 4> subjects{};
    for (size_t i = 0; i < scope.size(); ++i) {
        auto *npc = entity_list.GetNPCByID(scope[i]);
        if (!npc || entity_list.GetMob(scope[i]) != npc) { return refused; }
        for (size_t j = 0; j < i; ++j) {
            if (scope[i] == scope[j]) { return refused; }
        }
        subjects[i] = {scope[i], npc->NativeIncarnation()};
    }
    const auto state = Inspect();
    // Only this production opt-in can install scope. Send cannot inject facts
    // or start this mode without validated registered native instances.
    auto report = Advance({melee ? Kind::StartMelee : Kind::StartApproach, state.epoch, state.generation, 0, 0, 0, 0});
    if (report.status == Status::Ok) { scope_ = subjects; scope_size_ = scope.size(); }
    return report;
}

void Adapter::Observe()
{
    using namespace eqemu_proof;
    const auto state = Inspect();
    if (closing_ || state.body != eqemu_proof::Body::Present ||
        (state.approach != Approach::Observing && state.approach != Approach::Travelling &&
         !(state.melee && state.approach == Approach::Reached))) { return; }
    auto *body = Body();
    if (!body) { return; } // pending terminal/actual absence has priority below
    Observation observation{};
    observation.epoch = state.epoch;
    observation.generation = state.generation;
    observation.goal = state.goal;
    auto position = [](NPC *npc, int32_t &x, int32_t &y, int32_t &z) {
        const double px = npc->GetX(), py = npc->GetY(), pz = npc->GetZ();
        if (!std::isfinite(px) || !std::isfinite(py) || !std::isfinite(pz) ||
            std::abs(px) > 1000000 || std::abs(py) > 1000000 || std::abs(pz) > 1000000) { return false; }
        x = static_cast<int32_t>(std::lround(px * 1000));
        y = static_cast<int32_t>(std::lround(py * 1000));
        z = static_cast<int32_t>(std::lround(pz * 1000));
        return true;
    };
    bool valid = position(body, observation.x, observation.y, observation.z);
    for (size_t i = 0; i < scope_size_; ++i) {
        Candidate fact{};
        const auto &subject = scope_[i];
        fact.incarnation = subject.incarnation;
        auto *npc = entity_list.GetNPCByID(subject.id);
        fact.present = npc && entity_list.GetMob(subject.id) == npc &&
            npc->NativeIncarnation() == subject.incarnation;
        if (fact.present) {
            valid = position(npc, fact.x, fact.y, fact.z) && valid;
            fact.alive = npc->GetHP() > 0 && !npc->IsCorpse() && !npc->GetDepop();
            fact.targetable = npc->IsTargetable();
            // Zero is Familiar, not "no pet"; ordinary NPCs use None (255).
            fact.owned = npc->GetOwnerID() != 0 || npc->GetPetType() != PetType::None ||
                npc->GetSwarmInfo() != nullptr;
            fact.engaged = npc->IsEngaged();
            fact.actor = npc->IsRustActor();
            fact.self_body = npc == body;
            fact.type_id = npc->GetNPCTypeID();
            fact.level = npc->GetLevel();
            fact.service = npc->GetClass() != 1 || npc->GetBodyType() != 21 ||
                npc->MerchantType != 0 || npc->IsQuestNPC();
            if (state.melee && state.target == fact.incarnation && npc->GetHateListCount() == 1 &&
                npc->CheckAggro(body)) { fact.engaged = false; }
        }
        observation.candidates.push_back(fact); // absence is explicit, never omitted
    }
    if (!valid) {
        Advance({Kind::Pause, state.epoch, state.generation, 0, 0, 0, 0});
        return;
    }
    if (state.sequence == std::numeric_limits<uint64_t>::max() ||
        state.tick == std::numeric_limits<uint64_t>::max()) { std::abort(); }
    last_ = runtime_->observe(state.sequence + 1, state.tick + 1, std::move(observation));
    Apply(last_);
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
        if (visible_creation_) {
            std::strcpy(type->name, "Staging_Actor");
            type->gender = 0; // rendered male human, rather than neutral gender
            type->show_name = true;
        }
        if (Inspect().combat_body) {
            // Explicit ordinary melee body; original noncombat profile is unchanged.
            type->bodytype = 1;
            type->untargetable = 0;
            type->min_dmg = 2;
            type->max_dmg = 6;
            type->attack_delay = 2000;
            type->STR = type->STA = type->DEX = type->AGI = 75;
        }
        auto *npc = new NPC(type, nullptr,
            glm::vec4(report.x, report.y, report.z, 0), GravityBehavior::Flying);
        npc->GiveNPCTypeData(type);
        npc->actor_epoch_ = report.epoch;
        npc->actor_generation_ = report.generation;
        // Do not AI_Stop or clear IsAIControlled: the explicit gate owns decisions.
        entity_list.AddNPC(npc, visible_creation_, visible_creation_);
        id_ = npc->GetID();
    } else if (report.event == Event::AttackRequested) {
        const auto state = Inspect();
        combat_ = {};
        combat_.epoch = state.epoch;
        combat_.generation = state.generation;
        combat_.goal = state.goal;
        combat_.operation = state.operation;
        combat_.target = state.target;
        if (auto *body = Body()) { combat_.actor_incarnation = body->NativeIncarnation(); }
        for (size_t i = 0; i < scope_size_; ++i) {
            if (scope_[i].incarnation == state.target) { combat_.id = scope_[i].id; }
        }
    } else if (report.event == Event::MoveRequested) {
        target_x_ = report.x;
        target_y_ = report.y;
        target_z_ = report.z;
    } else if (report.event == Event::StopRequested) {
        if (auto *npc = Body()) {
            auto &movement = MobMovementManager::Get();
            // Attack can queue FaceTarget rotation without an actor travel goal.
            // Drain it on combat pause too, even before IsMoving becomes true.
            // StopNavigation preserves an already latched travel completion.
            if (Inspect().attacking ||
                movement.InspectActorTravel(npc) == MobMovementManager::ActorTravel::Moving) {
                movement.StopNavigation(npc);
            }
        }
    } else if (report.event == Event::RemovalRequested) {
        if (auto *npc = Body()) { npc->Depop(false); }
    }
}

void Adapter::CombatFact(eqemu_proof::Kind kind, uint32_t damage)
{
    const auto state = Inspect();
    if (state.sequence == std::numeric_limits<uint64_t>::max() ||
        state.tick == std::numeric_limits<uint64_t>::max()) { std::abort(); }
    eqemu_proof::CombatFact fact{};
    fact.epoch = combat_.epoch;
    fact.generation = combat_.generation;
    fact.goal = combat_.goal;
    fact.operation = combat_.operation;
    fact.target = combat_.target;
    fact.kind = kind;
    fact.damage = damage;
    last_ = runtime_->combat(state.sequence + 1, state.tick + 1, fact);
    Apply(last_);
    if (last_.status == eqemu_proof::Status::Ok && !Inspect().attacking) {
        // Every accepted combat terminal owns cancellation, including terminals
        // that never passed through StopRequested. Never act on a reused body.
        if (auto *body = Body(); body && body->NativeIncarnation() == combat_.actor_incarnation) {
            MobMovementManager::Get().StopNavigation(body);
            body->SetTarget(nullptr);
        }
        combat_.operation = 0;
    }
}

bool Adapter::CombatAllowed(NPC *body, NPC *target) const
{
    const auto state = Inspect();
    if (!state.attacking || state.epoch != combat_.epoch || state.generation != combat_.generation ||
        state.goal != combat_.goal || state.operation != combat_.operation || state.target != combat_.target) { return false; }
    if (!zone || zone->GetZoneID() != 2 || zone->GetInstanceVersion() != 0 ||
        !zone->CanDoCombat() || !body || !target || !state.combat_body ||
        entity_list.GetNPCByID(combat_.id) != target || entity_list.GetMob(combat_.id) != target ||
        target->NativeIncarnation() != combat_.target ||
        target->GetNPCTypeID() != 2011 || target->GetLevel() != 1 ||
        target->GetClass() != 1 || target->GetBodyType() != 21 ||
        target->IsRustActor() || !target->IsTargetable() || target->GetHP() <= 0 || target->GetDepop() ||
        target->MerchantType || target->IsQuestNPC() ||
        target->GetOwnerID() || target->GetPetType() != PetType::None || target->GetSwarmInfo() ||
        entity_list.GetNPCByID(id_) != body || entity_list.GetMob(id_) != body ||
        body->NativeIncarnation() != combat_.actor_incarnation || !body->IsTargetable() ||
        !body->IsAIControlled() || body->GetHP() <= 0 || body->GetDepop() || body->IsStunned() ||
        body->IsMezzed() || body->IsFeared() || body->IsBlind() || body->IsRooted() ||
        body->IsPseudoRooted() || body->IsCasting() || body->Charmed() || body->GetOwnerID() ||
        body->GetPetType() != PetType::None || body->GetSwarmInfo() ||
        body->IsMeleeDisabled() || body->DivineAura() || body->GetAppearance() != eaStanding ||
        MobMovementManager::Get().InspectActorCombatMotion(body) ==
            MobMovementManager::ActorCombatMotion::Unsupported) { return false; }
    // Only mutual contention is permitted. Even a lower-hate third party stops us.
    if ((body->IsEngaged() && (body->GetHateListCount() != 1 || !body->CheckAggro(target))) ||
        (target->IsEngaged() && (target->GetHateListCount() != 1 || !target->CheckAggro(body)))) { return false; }
    return std::hypot(body->GetX() - target->GetX(), body->GetY() - target->GetY()) <= 5 &&
        std::abs(body->GetZ() - target->GetZ()) <= 6 &&
        body->CombatRange(target) && body->CheckLosFN(target) && body->IsAttackAllowed(target);
}

void Adapter::ObserveDeath(NPC *victim, Mob *killer)
{
    // No Rust calls, advancement or retained entity pointers in native callbacks.
    if (!combat_.operation) { return; }
    if (victim->MatchesRustActor(combat_.epoch, combat_.generation) &&
        victim->NativeIncarnation() == combat_.actor_incarnation) { combat_.defeated = true; }
    auto *body = killer && killer->IsNPC() ? killer->CastToNPC() : nullptr;
    if (victim->NativeIncarnation() == combat_.target && body &&
        body->MatchesRustActor(combat_.epoch, combat_.generation) &&
        body->NativeIncarnation() == combat_.actor_incarnation &&
        body->actor_swing_authorized_) { combat_.killed = true; }
}

void Adapter::ProcessCombat()
{
    using namespace eqemu_proof;
    const auto state = Inspect();
    if (!state.attacking || state.epoch != combat_.epoch || state.generation != combat_.generation ||
        state.goal != combat_.goal || state.operation != combat_.operation || state.target != combat_.target) { return; }
    // Latched owned native terminals outrank pause, retirement and ID reuse.
    if (combat_.killed && state.damage > 0) { CombatFact(Kind::TargetKilled); return; }
    auto *body = Body();
    if (combat_.defeated || !body || body->GetHP() <= 0) { CombatFact(Kind::ActorDefeated); return; }
    auto *target = entity_list.GetNPCByID(combat_.id);
    if (!target || entity_list.GetMob(combat_.id) != target ||
        target->NativeIncarnation() != combat_.target || target->GetHP() <= 0 || target->GetDepop()) {
        CombatFact(Kind::TargetLost); return;
    }
    const bool enabled = !closing_ && state.approach == Approach::Engaging;
    const bool allowed = enabled && CombatAllowed(body, target);
    if (state.phase == Phase::Requested) {
        if (allowed) { body->SetTarget(target); } // ordinary native timer configuration
        CombatFact(allowed ? Kind::AttackAdmitted : Kind::AttackRefused);
        return; // intent admission never bypasses the native attack timer
    }
    if (!allowed || state.phase == Phase::Stopping) {
        CombatFact(Kind::AttackInterrupted); return;
    }
    if (!body->ActorAttackTimerReady()) { return; }
    // Revalidate after timer admission immediately before the ordinary entry point.
    if (!CombatAllowed(body, target)) { CombatFact(Kind::AttackInterrupted); return; }
    const auto before = target->GetHP();
    body->SetTarget(target);
    body->actor_swing_authorized_ = true;
    body->Attack(target);
    body->actor_swing_authorized_ = false;
    const auto loss = std::max<int64_t>(0, before - std::max<int64_t>(0, target->GetHP()));
    const auto actual = static_cast<uint32_t>(std::min<int64_t>(loss, std::numeric_limits<uint32_t>::max()));
    CombatFact(actual ? Kind::SwingHit : Kind::SwingMiss, actual);
}

void Adapter::Process()
{
    using namespace eqemu_proof;
    if (Inspect().closed || !Inspect().alive) { return; }
    Observe(); // bounded fresh perception cannot be starved by active travel
    if (Inspect().attacking) { ProcessCombat(); return; }
    const auto state = Inspect();
    if (state.body == eqemu_proof::Body::Present && state.phase != Phase::Idle) {
        auto *npc = Body();
        auto &movement = MobMovementManager::Get();
        if (state.phase == Phase::Requested) {
            const bool admitted = !closing_ &&
                (state.patrol == Patrol::Disabled || state.patrol == Patrol::Running) &&
                (state.approach == Approach::Disabled || state.approach == Approach::Travelling) && npc &&
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
    if (state.alive && (state.patrol != eqemu_proof::Patrol::Disabled ||
        state.approach != eqemu_proof::Approach::Disabled)) {
        Advance({eqemu_proof::Kind::Retire, state.epoch, state.generation, 0, 0, 0, 0});
        state = Inspect();
    }
    if (state.attacking) {
        ProcessCombat(); // owns correlated pending death before cancellation/absence
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
        if (state.approach == eqemu_proof::Approach::Retiring || state.patrol == eqemu_proof::Patrol::Retiring) {
            Advance({}, true);
        } else {
            Advance({eqemu_proof::Kind::Retire, state.epoch, state.generation, 0, 0, 0, 0});
        }
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
void ObserveDeath(NPC *victim, Mob *killer)
{
    if (adapter) { adapter->ObserveDeath(victim, killer); }
}
void BeginTeardown() {
    ActorDemo::BeginTeardown();
    if (adapter) { adapter->BeginTeardown(); }
}
void FinishTeardown()
{
    if (adapter) {
        adapter->FinishTeardown();
        ActorDemo::FinishTeardown();
#ifdef EQEMU_BUILD_TESTS
        InserverTests::ObserveActorShutdown(InserverTests::ActorShutdownStage::RuntimeClosed);
#endif
        adapter.reset();
    }
}
} // namespace ActorBody
#endif

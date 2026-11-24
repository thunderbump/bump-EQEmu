#pragma once

#ifdef EQEMU_RUST_BOOTSTRAP_BRIDGE
#include "actor-bridge/src/lib.rs.h"
#include "actor_population.h"
#include <cstdint>
#include <cstddef>
#include <array>
#include <vector>

class NPC;
class Mob;
namespace ActorBody {

// Zone-thread only. Rust owns identity/policy; this adapter owns native lookup.
// No entity pointer is retained across calls and no call reenters Rust.
class Adapter {
public:
    explicit Adapter(bool population = false);
    eqemu_proof::Report Send(eqemu_proof::Input input);
    // Opt-in only: creates at A and configures B once. Normal Process drives legs.
    eqemu_proof::Report StartPatrol(int32_t ax, int32_t ay, int32_t az,
        int32_t bx, int32_t by, int32_t bz);
    // Existing Present body only. Scope names native instances, not native facts.
    // Empty scope is allowed. At most four distinct registered NPC IDs.
    eqemu_proof::Report StartApproach(const std::vector<uint16_t> &scope);
    eqemu_proof::Report StartMelee(const std::vector<uint16_t> &scope);
    eqemu_proof::Report StartPublicHunt(uint16_t target);
    // Repeat while draining. Does not remove the body or mutate public prey.
    bool ReleasePublicHunt();
    // Demo-only appearance/broadcast. Ordinary creation and test profiles stay unchanged.
    eqemu_proof::Report CreateVisibleMeleeBody(int32_t x, int32_t y, int32_t z);
    eqemu_proof::Report StartTransferMove(int32_t x, int32_t y, int32_t z);
    bool RestorePopulationBody(unsigned hunter, double x, double y, double z, int64_t hp, uint8_t level);
    // Native Death completion hook: latch values only, never reenter Rust.
    void ObserveDeath(NPC *victim, Mob *killer);
    void Process();
    void BeginTeardown();
    void FinishTeardown(); // after EntityList::Clear or observed native teardown drain
    NPC *Lookup(uint64_t epoch, uint64_t generation, uint16_t id) const;
    NPC *Body() const;
    uint16_t NativeID() const { return id_; }
    // Updated after every Rust mutation. Native callbacks only read this cache.
    eqemu_proof::Snapshot Inspect() const { return snapshot_; }
    eqemu_proof::Report LastReport() const { return last_; }
    // Native Death can be latched by this tick's swing before Rust consumes it
    // on the next tick. This is evidence to wait, never a reported victory.
    bool HasPendingTargetKill(uint64_t target_incarnation) const;
    const std::vector<ActorPopulation::FactionChange> &FactionChanges() const { return combat_.factions; }
    uint64_t KillExperience() const { return combat_.killed ? combat_.experience : 0; }
    bool HasAdmittedPublicCombat(NPC *body, NPC *prey) const;
private:
    eqemu_proof::Report Advance(eqemu_proof::Input input, bool empty = false);
    void Apply(const eqemu_proof::Report &report);
    void Observe();
    eqemu_proof::Report StartGoal(const std::vector<uint16_t> &scope, bool melee, bool public_hunt = false);
    void CombatFact(eqemu_proof::Kind kind, uint32_t damage = 0);
    void ProcessCombat();
    bool CombatAllowed(NPC *body, NPC *target) const;
    struct Combat {
        uint64_t epoch = 0, generation = 0, goal = 0, operation = 0, target = 0;
        uint64_t actor_incarnation = 0;
        uint64_t experience = 0;
        std::vector<ActorPopulation::FactionChange> factions;
        uint16_t id = 0;
        bool killed = false, defeated = false;
    } combat_;
    struct Subject { uint16_t id = 0; uint64_t incarnation = 0; };
    std::array<Subject, 4> scope_{};
    std::size_t scope_size_ = 0;
    rust::Box<eqemu_proof::Runtime> runtime_;
    eqemu_proof::Snapshot snapshot_{};
    eqemu_proof::Report last_{};
    uint16_t id_ = 0;
    bool closing_ = false;
    bool visible_creation_ = false;
    bool population_ = false;
    bool transfer_move_ = false;
    unsigned hunter_ = 0;
    double restore_x_ = 0, restore_y_ = 0, restore_z_ = 0;
    int64_t restore_hp_ = 100;
    uint8_t restore_level_ = 1;
    int32_t target_x_ = 0, target_y_ = 0, target_z_ = 0;
};
Adapter &Get();
Adapter &PopulationAdapter(std::size_t hunter);
bool LegacyAlive();
bool IsPublicHuntPrey(NPC *npc, bool population = false, NPC *actor = nullptr);
void Process();
void ObserveDeath(NPC *victim, Mob *killer);
void BeginTeardown();
void FinishTeardown();
} // namespace ActorBody
#endif

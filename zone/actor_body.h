#pragma once

#ifdef EQEMU_RUST_BOOTSTRAP_BRIDGE
#include "actor-bridge/src/lib.rs.h"
#include <cstdint>
#include <cstddef>
#include <array>
#include <vector>

class NPC;
namespace ActorBody {

// Zone-thread only. Rust owns identity/policy; this adapter owns native lookup.
// No entity pointer is retained across calls and no call reenters Rust.
class Adapter {
public:
    Adapter();
    eqemu_proof::Report Send(eqemu_proof::Input input);
    // Opt-in only: creates at A and configures B once. Normal Process drives legs.
    eqemu_proof::Report StartPatrol(int32_t ax, int32_t ay, int32_t az,
        int32_t bx, int32_t by, int32_t bz);
    // Existing Present body only. Scope names native instances, not native facts.
    // Empty scope is allowed. At most four distinct registered NPC IDs.
    eqemu_proof::Report StartApproach(const std::vector<uint16_t> &scope);
    void Process();
    void BeginTeardown();
    void FinishTeardown(); // after EntityList::Clear or observed native teardown drain
    NPC *Lookup(uint64_t epoch, uint64_t generation, uint16_t id) const;
    NPC *Body() const;
    uint16_t NativeID() const { return id_; }
    eqemu_proof::Snapshot Inspect() const { return runtime_->inspect(); }
    eqemu_proof::Report LastReport() const { return last_; }
private:
    eqemu_proof::Report Advance(eqemu_proof::Input input, bool empty = false);
    void Apply(const eqemu_proof::Report &report);
    void Observe();
    struct Subject { uint16_t id = 0; uint64_t incarnation = 0; };
    std::array<Subject, 4> scope_{};
    std::size_t scope_size_ = 0;
    rust::Box<eqemu_proof::Runtime> runtime_;
    eqemu_proof::Report last_{};
    uint16_t id_ = 0;
    bool closing_ = false;
    int32_t target_x_ = 0, target_y_ = 0, target_z_ = 0;
};
Adapter &Get();
void Process();
void BeginTeardown();
void FinishTeardown();
} // namespace ActorBody
#endif

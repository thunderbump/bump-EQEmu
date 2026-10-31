#pragma once
#include <cstdint>

namespace ActorDemo {
struct NativeIdentity {
    uint16_t id = 0;
    uint64_t incarnation = 0;
    bool Matches(const NativeIdentity &other) const {
        return id && incarnation && id == other.id && incarnation == other.incarnation;
    }
};
struct NativeView {
    uint64_t epoch = 0, generation = 0;
    bool alive = false;
    NativeIdentity actor, target;
};
struct Retirement {
    bool actor = false, target = false, drained = false;
};
struct Counters {
    uint64_t swings = 0, hits = 0, damage = 0;
};

// The controller's native seam uses observed lifetime keys, never entity IDs
// alone. Native lookups and Rust calls stay on the zone thread in actor_demo.cpp.
class Custody {
public:
    NativeIdentity actor, target;
    uint64_t epoch = 0, generation = 0;
    bool interference = false;
    Counters total, last;

    bool OwnsRuntime(const NativeView &view) const {
        return epoch && generation && epoch == view.epoch && generation == view.generation;
    }
    Retirement PlanRetirement(const NativeView &view) const {
        const bool runtime_alive = OwnsRuntime(view) && view.alive;
        const bool actor_present = actor.Matches(view.actor);
        const bool target_present = target.Matches(view.target);
        return {runtime_alive, target_present, !runtime_alive && !actor_present && !target_present};
    }
    bool CanCombat(const NativeIdentity &attacker, const NativeIdentity &victim) const {
        const bool attacker_is_actor = actor.Matches(attacker);
        const bool attacker_is_rat = target.Matches(attacker);
        const bool victim_is_actor = actor.Matches(victim);
        const bool victim_is_rat = target.Matches(victim);
        return !(attacker_is_actor || attacker_is_rat || victim_is_actor || victim_is_rat) ||
            (attacker_is_actor && victim_is_rat) || (attacker_is_rat && victim_is_actor);
    }
    bool AdmitCombat(const NativeIdentity &attacker, const NativeIdentity &victim) {
        if (CanCombat(attacker, victim)) { return true; }
        interference = true;
        return false;
    }
    void ObserveCounters(const NativeView &view, Counters current) {
        if (!OwnsRuntime(view)) { return; }
        // A counter regression cannot manufacture a huge unsigned delta.
        if (current.swings < last.swings || current.hits < last.hits || current.damage < last.damage) {
            interference = true;
            return;
        }
        total.swings += current.swings - last.swings;
        total.hits += current.hits - last.hits;
        total.damage += current.damage - last.damage;
        last = current;
    }
};
} // namespace ActorDemo

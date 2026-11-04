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
    bool target_kill_pending = false;
    uint64_t goal = 0;
};
struct Retirement {
    bool actor = false, target = false, drained = false;
};
struct Counters {
    uint64_t swings = 0, hits = 0, damage = 0;
};
inline bool NPCFactionWarWipeRequired(bool npc_aggro, bool retaliation) {
    return !npc_aggro && !retaliation;
}

// The controller's native seam uses observed lifetime keys, never entity IDs
// alone. Native lookups and Rust calls stay on the zone thread in actor_demo.cpp.
class Custody {
public:
    NativeIdentity actor, target;
    uint64_t epoch = 0, generation = 0;
    uint64_t goal = 0;
    bool interference = false;
    bool public_hunt = false, combat_enabled = false, contested = false;
    Counters total, last;
    Counters incoming;
    int64_t minimum_hp = 0, last_hp = 0;
    uint64_t rest_hp_gain = 0;
    uint64_t retaliation_hate_preserved = 0;
    int64_t incoming_last_native_result = 0;
    bool hp_observed = false;

    bool OwnsRuntime(const NativeView &view) const {
        return epoch && generation && epoch == view.epoch && generation == view.generation;
    }
    Retirement PlanRetirement(const NativeView &view) const {
        const bool runtime_alive = OwnsRuntime(view) && view.alive;
        const bool actor_present = actor.Matches(view.actor);
        const bool target_present = !public_hunt && target.Matches(view.target);
        return {runtime_alive, target_present, !runtime_alive && !actor_present && !target_present};
    }
    bool TargetLost(const NativeView &view, bool viable, bool victory) const {
        // A depop alone is loss. Only the adapter's correlated native death
        // latch permits waiting for next tick's truthful Rust terminal.
        return !OwnsRuntime(view) || (!victory && !view.target_kill_pending &&
            (!target.Matches(view.target) || !viable));
    }
    bool CanCombat(const NativeIdentity &attacker, const NativeIdentity &victim) const {
        const bool attacker_is_actor = actor.Matches(attacker);
        const bool attacker_is_rat = target.Matches(attacker);
        const bool victim_is_actor = actor.Matches(victim);
        const bool victim_is_rat = target.Matches(victim);
        if (public_hunt) {
            if (!attacker_is_actor && !victim_is_actor) { return true; }
            return combat_enabled && !contested && !interference &&
                ((attacker_is_actor && victim_is_rat) || (attacker_is_rat && victim_is_actor));
        }
        return !(attacker_is_actor || attacker_is_rat || victim_is_actor || victim_is_rat) ||
            (attacker_is_actor && victim_is_rat) || (attacker_is_rat && victim_is_actor);
    }
    bool CanRetaliate(const NativeView &view, const NativeIdentity &prey, const NativeIdentity &body,
        bool native_admitted) const {
        return public_hunt && native_admitted && OwnsRuntime(view) && view.alive && goal && view.goal == goal &&
            actor.Matches(view.actor) && target.Matches(view.target) &&
            target.Matches(prey) && actor.Matches(body) && CanCombat(prey, body);
    }
    void ObserveHP(int64_t hp, bool resting) {
        hp = hp > 0 ? hp : 0;
        if (!hp_observed) { minimum_hp = hp; hp_observed = true; }
        if (hp < minimum_hp) { minimum_hp = hp; }
        if (resting && hp > last_hp && last_hp > 0) { rest_hp_gain += uint64_t(hp - last_hp); }
        last_hp = hp;
    }
    void ObserveIncoming(const NativeView &view, const NativeIdentity &prey, const NativeIdentity &body,
        int64_t before, int64_t after, int64_t native_result = 0) {
        if (!CanRetaliate(view, prey, body, true)) { return; }
        ++incoming.swings;
        incoming_last_native_result = native_result;
        const auto loss = (before > 0 ? before : 0) - (after > 0 ? after : 0);
        if (loss > 0) { ++incoming.hits; incoming.damage += uint64_t(loss); }
        ObserveHP(after, false);
    }
    bool AdmitCombat(const NativeIdentity &attacker, const NativeIdentity &victim) {
        if (public_hunt && !actor.Matches(attacker) && !actor.Matches(victim) &&
            (target.Matches(attacker) || target.Matches(victim))) {
            contested = true;
            return true; // public prey remains ordinary combat/loot for everyone else
        }
        if (CanCombat(attacker, victim)) { return true; }
        interference = true;
        return false;
    }
    bool AdmitClientAbility(uint16_t requested_target, uint16_t actual_target,
        const NativeIdentity &attacker, const NativeIdentity &victim) {
        if (!requested_target || requested_target != actual_target) { return false; }
        return AdmitCombat(attacker, victim);
    }
    void ObserveCounters(const NativeView &view, Counters current) {
        if (!OwnsRuntime(view) || (public_hunt && view.goal != goal)) { return; }
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

#pragma once
#include "actor-bridge/src/lib.rs.h"
#include "actor-bridge/src/population.rs.h"
#include "actor_demo_native.h"
namespace ActorPopulation {
// A latched native kill can complete after retirement began. The core retains
// its correlated terminal operation/goal even while approach is Retiring.
inline eqemu_population::Outcome
NativeOutcome(const eqemu_proof::Snapshot &state, bool confirmed_death) {
  using Result = eqemu_population::Outcome;
  if (confirmed_death) {
    return Result::Defeated;
  }
  if (state.public_hunt && state.last_operation &&
      state.last_terminal == eqemu_proof::Event::TargetKilled) {
    return Result::Victory;
  }
  if (state.approach == eqemu_proof::Approach::Lost) {
    return Result::Lost;
  }
  return state.approach == eqemu_proof::Approach::Paused ? Result::Paused
                                                         : Result::None;
}
// Materialized native permission, not a second controller. Only exact current
// keys can authorize a pair; callbacks close it before Rust sees the event.
struct Permit {
  ActorDemo::NativeIdentity actor, target;
  uint64_t epoch = 0, generation = 0, goal = 0;
  bool enabled = false, inhibited = false, contested = false;
  bool Pair(ActorDemo::NativeIdentity a, ActorDemo::NativeIdentity b) const {
    return (actor.Matches(a) && target.Matches(b)) ||
           (actor.Matches(b) && target.Matches(a));
  }
  bool InvolvesActor(ActorDemo::NativeIdentity a,
                     ActorDemo::NativeIdentity b) const {
    return actor.Matches(a) || actor.Matches(b);
  }
  bool Allows(ActorDemo::NativeIdentity a, ActorDemo::NativeIdentity b) const {
    return enabled && !inhibited && Pair(a, b);
  }
  bool Lifetime(uint64_t current_epoch, uint64_t current_generation,
                uint64_t current_goal) const {
    return epoch && generation && goal && epoch == current_epoch &&
           generation == current_generation && goal == current_goal;
  }
  bool MatchesIncoming(ActorDemo::NativeIdentity prey,
                       ActorDemo::NativeIdentity body, uint64_t current_epoch,
                       uint64_t current_generation,
                       uint64_t current_goal) const {
    return Lifetime(current_epoch, current_generation, current_goal) &&
           target.Matches(prey) && actor.incarnation &&
           actor.incarnation == body.incarnation &&
           (!body.id || actor.id == body.id);
  }
  void PublicContact(ActorDemo::NativeIdentity a, ActorDemo::NativeIdentity b) {
    if (!InvolvesActor(a, b) && (target.Matches(a) || target.Matches(b))) {
      Interfere();
    }
  }
  void Death(ActorDemo::NativeIdentity victim) {
    // Native Death may clear GetID after corpse registration. Incarnation
    // remains unique; a nonzero ID must still match the bound instance.
    if (victim.incarnation && ((actor.incarnation == victim.incarnation &&
                                (!victim.id || actor.id == victim.id)) ||
                               (target.incarnation == victim.incarnation &&
                                (!victim.id || target.id == victim.id)))) {
      Close();
    }
  }
  void Close() {
    enabled = false;
    inhibited = true;
  }
  void Interfere() {
    inhibited = true;
    contested = true;
  }
};
} // namespace ActorPopulation

#include "actor_population.h"
#ifdef EQEMU_RUST_BOOTSTRAP_BRIDGE
#include "actor-bridge/src/population.rs.h"
#include "actor_body.h"
#include "actor_hunt_state.h"
#include "actor_population_native.h"
#include "entity.h"
#include "map.h"
#include "mob_movement_manager.h"
#include "npc.h"
#include "water_map.h"
#include "zone.h"
#include <array>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <memory>
#include <unistd.h>

extern EntityList entity_list;
extern Zone *zone;
namespace ActorPopulation {
namespace {
using Key = ActorDemo::NativeIdentity;
Key Identify(Mob *mob) {
  return mob && mob->IsNPC()
             ? Key{mob->GetID(), mob->CastToNPC()->NativeIncarnation()}
             : Key{};
}
NPC *Find(Key key) {
  auto *npc = entity_list.GetNPCByID(key.id);
  return key.Matches(Identify(npc)) && entity_list.GetMob(key.id) == npc
             ? npc
             : nullptr;
}
uint64_t Now() {
  return std::chrono::duration_cast<std::chrono::milliseconds>(
             std::chrono::steady_clock::now().time_since_epoch())
      .count();
}
ActorDemo::HuntPosition Position(NPC *npc) {
  return {npc->GetX(), npc->GetY(), npc->GetZ()};
}
bool InArea(NPC *npc, float radius) {
  return npc && ActorDemo::InsideHuntArea(
                    Position(npc),
                    {ActorDemo::HuntHomeX, ActorDemo::HuntHomeY, 3.75}, radius);
}
bool Unowned(NPC *npc) {
  return npc && !npc->GetOwnerID() && npc->GetPetType() == PetType::None &&
         !npc->GetSwarmInfo() && !npc->Charmed();
}
struct Entry {
  Permit permit;
  Key probe;
  bool probe_ready = false, probe_ok = false, error = false;
  bool refused = false, dead = false;
  uint64_t incoming_swings = 0, incoming_hits = 0, incoming_damage = 0;
};
class Native {
public:
  Native(const char *directory)
      : policy(eqemu_population::population_open(
            directory, std::to_string(getpid()) + "-" +
                           std::to_string(std::chrono::steady_clock::now()
                                              .time_since_epoch()
                                              .count()))) {
    for (unsigned i = 0; i < entries.size(); ++i) {
      (void)ActorBody::PopulationAdapter(i);
    }
  }
  rust::Box<eqemu_population::Population> policy;
  std::array<Entry, 3> entries;
  rust::Vec<eqemu_population::Prey> observations;
  bool overflow = false;
  bool failure_reported = false;
  uint64_t next_tick = 0;

  // No Rust calls: snapshots are cached by the adapter after every mutation.
  bool Current(unsigned i) const {
    const auto &p = entries[i].permit;
    const auto s = ActorBody::PopulationAdapter(i).Inspect();
    auto *body = Find(p.actor);
    auto *prey = Find(p.target);
    return p.enabled && !p.inhibited && body && prey && s.alive && !s.closed &&
           p.Lifetime(s.epoch, s.generation, s.goal) &&
           (!s.target || s.target == p.target.incarnation) &&
           (s.approach == eqemu_proof::Approach::Observing ||
            s.approach == eqemu_proof::Approach::Travelling ||
            s.approach == eqemu_proof::Approach::Reached ||
            s.approach == eqemu_proof::Approach::Engaging) &&
           body->MatchesRustActor(p.epoch, p.generation) && body->GetHP() > 0 &&
           !body->GetDepop() && body->IsTargetable() &&
           body->IsAIControlled() && Unowned(body) && prey->GetHP() > 0 &&
           !prey->GetDepop() && prey->IsTargetable() && Unowned(prey) &&
           !prey->GetNPCSpellsID() && !prey->GetNPCSpellsEffectsID() &&
           !prey->MerchantType && !prey->IsQuestNPC() &&
           InArea(body, ActorDemo::HuntLeash) &&
           InArea(prey, ActorDemo::HuntSearchRadius);
  }
  bool Can(Key a, Key b) const {
    for (unsigned i = 0; i < entries.size(); ++i) {
      const auto &p = entries[i].permit;
      if (p.InvolvesActor(a, b)) {
        return Current(i) && p.Allows(a, b);
      }
    }
    return true;
  }
  bool Allow(Key a, Key b) {
    for (auto &e : entries) {
      if (e.permit.InvolvesActor(a, b)) {
        if (Can(a, b)) {
          return true;
        }
        e.permit.Interfere();
        return false;
      }
    }
    // Public combat never belongs to a claim owner. Third-party attempts
    // close native permission immediately, before the next adapter swing.
    for (auto &e : entries) {
      e.permit.PublicContact(a, b);
    }
    return true;
  }
  void Detach(unsigned i) {
    auto &p = entries[i].permit;
    p.Close();
    auto *body = Find(p.actor);
    auto *prey = Find(p.target);
    if (!body) {
      return;
    }
    if (prey && prey->CheckAggro(body)) {
      prey->RemoveFromHateList(body);
    }
    if (body->IsEngaged()) {
      body->WipeHateList();
    } // only this owned actor
    body->SetTarget(nullptr);
  }
  bool Probe(unsigned i, Key target) {
    auto *body = ActorBody::PopulationAdapter(i).Body();
    auto *prey = Find(target);
    if (!body || !prey || !ActorBody::IsPublicHuntPrey(prey, true) ||
        prey->IsEngaged() || prey->IsMoving() || prey->IsCasting() ||
        prey->IsStunned() || prey->IsRooted() || prey->IsPseudoRooted() ||
        prey->IsMezzed() || prey->IsFeared() || prey->IsBlind() ||
        !InArea(body, ActorDemo::HuntLeash) ||
        !InArea(prey, ActorDemo::HuntSearchRadius)) {
      return false;
    }
    glm::vec3 position(prey->GetPosition()),
        floor_probe = position + glm::vec3(0, 0, 20);
    const auto floor = zone->zonemap->FindBestZ(floor_probe, nullptr);
    if (floor == BEST_Z_INVALID || !std::isfinite(floor) ||
        !ActorDemo::HuntFloorAllowed(prey->GetZ(), floor, prey->GetZOffset()) ||
        zone->watermap->InLiquid(position) || !body->CheckLosFN(prey)) {
      return false;
    }
    const auto distance =
        std::hypot(body->GetX() - prey->GetX(), body->GetY() - prey->GetY());
    if (distance <= 5 && body->CombatRange(prey)) {
      return true;
    }
    const auto scale = distance > 2 ? 2 / distance : 0;
    return MobMovementManager::Get().CanNavigateActorTo(
        body, std::round(prey->GetX() - (prey->GetX() - body->GetX()) * scale),
        std::round(prey->GetY() - (prey->GetY() - body->GetY()) * scale),
        std::round(prey->GetZ()), true);
  }
  void Scan() {
    observations.clear();
    overflow = false;
    for (const auto &pair : entity_list.GetNPCList()) {
      auto *npc = pair.second;
      // Projection is shared by all three controllers. The Rust profile
      // is authoritative; native service/ownership facts are refreshed.
      if (!InArea(npc, ActorDemo::HuntSearchRadius) || npc->IsRustActor()) {
        continue;
      }
      if (observations.size() >= 512) {
        overflow = true;
        observations.clear();
        return;
      }
      eqemu_population::Prey p{};
      p.id = npc->GetID();
      p.incarnation = npc->NativeIncarnation();
      p.x = npc->GetX();
      p.y = npc->GetY();
      p.z = npc->GetZ();
      p.type_id = npc->GetNPCTypeID();
      p.level = npc->GetLevel();
      p.name = npc->GetRawNPCTypeName();
      p.safe = ActorBody::IsPublicHuntPrey(npc, true);
      p.idle = !npc->IsEngaged() && !npc->IsMoving() && !npc->IsCasting() &&
               !npc->IsStunned() && !npc->IsMezzed() && !npc->IsFeared() &&
               !npc->IsBlind() && !npc->IsRooted() && !npc->IsPseudoRooted();
      observations.push_back(std::move(p));
    }
  }
  rust::Vec<eqemu_population::Body> Facts() {
    rust::Vec<eqemu_population::Body> facts;
    for (unsigned i = 0; i < entries.size(); ++i) {
      auto &e = entries[i];
      auto &adapter = ActorBody::PopulationAdapter(i);
      const auto s = adapter.Inspect();
      auto *body = adapter.Body();
      auto *prey = Find(e.permit.target);
      eqemu_population::Body b{};
      b.actor = i;
      b.id = e.permit.actor.id;
      b.incarnation = e.permit.actor.incarnation;
      b.epoch = s.epoch;
      b.generation = s.generation;
      b.goal = s.goal;
      b.operation = s.operation;
      b.alive = s.alive;
      b.present = body && s.body == eqemu_proof::Body::Present &&
                  entity_list.GetMob(body->GetID()) == body &&
                  entity_list.GetNPCByID(body->GetID()) == body;
      b.drained = !s.alive && !body;
      b.contested = e.permit.contested;
      b.error = e.error;
      b.refused = e.refused;
      if (body) {
        b.x = body->GetX();
        b.y = body->GetY();
        b.z = body->GetZ();
        b.hp = body->GetHP();
        b.moving = body->IsMoving();
        b.sitting = body->GetAppearance() == eaSitting;
        b.engaged = body->IsEngaged();
        b.error = b.error || !Unowned(body) || !body->IsAIControlled();
      }
      b.idle = s.phase == eqemu_proof::Phase::Idle &&
               s.approach == eqemu_proof::Approach::Disabled && !b.moving &&
               body &&
               MobMovementManager::Get().InspectActorCombatMotion(body) ==
                   MobMovementManager::ActorCombatMotion::Stopped;
      b.target_viable =
          prey && prey->GetHP() > 0 && !prey->GetDepop() && Unowned(prey) &&
          InArea(prey, ActorDemo::HuntSearchRadius) &&
          (!prey->IsEngaged() ||
           (body && prey->GetHateListCount() == 1 && prey->CheckAggro(body)));
      b.pending_kill =
          adapter.HasPendingTargetKill(e.permit.target.incarnation);
      if (s.approach == eqemu_proof::Approach::Victory &&
          s.last_terminal == eqemu_proof::Event::TargetKilled) {
        b.outcome = 1;
      } else if (s.approach == eqemu_proof::Approach::Lost) {
        b.outcome = 2;
      } else if (s.approach == eqemu_proof::Approach::Defeated) {
        if (e.dead) {
          b.outcome = 3;
        } else {
          b.error = true;
        }
      } else if (s.approach == eqemu_proof::Approach::Paused) {
        b.outcome = 4;
      }
      if (e.dead) {
        b.outcome = 3;
      }
      b.swings = s.swings;
      b.hits = s.hits;
      b.damage = s.damage;
      b.incoming_swings = e.incoming_swings;
      b.incoming_hits = e.incoming_hits;
      b.incoming_damage = e.incoming_damage;
      b.probe_id = e.probe.id;
      b.probe_incarnation = e.probe.incarnation;
      b.probe_ready = e.probe_ready;
      b.probe_ok = e.probe_ok;
      facts.push_back(b);
      e.permit.contested = false; // event consumed, inhibition remains closed
    }
    return facts;
  }
  void Apply(const eqemu_population::Action &a) {
    if (a.actor >= entries.size()) {
      std::abort();
    }
    const auto i = a.actor;
    auto &e = entries[i];
    auto &adapter = ActorBody::PopulationAdapter(i);
    const auto s = adapter.Inspect();
    using Kind = eqemu_population::Kind;
    if (a.kind == Kind::Spawn) {
      e.error = false;
      e.refused = e.dead = false;
      e.permit.Close();
      e.probe_ready = false;
      if (!adapter.RestorePopulationBody(i, a.x, a.y, a.z, a.hp)) {
        e.error = true;
        return;
      }
      auto *body = adapter.Body();
      const auto fresh = adapter.Inspect();
      e.permit.actor = Identify(body);
      e.permit.epoch = fresh.epoch;
      e.permit.generation = fresh.generation;
      e.permit.goal = 0;
      return;
    }
    const bool lifetime = a.id == e.permit.actor.id &&
                          a.incarnation == e.permit.actor.incarnation &&
                          a.epoch == s.epoch && a.generation == s.generation;
    // Retiring is safe even after native disappearance: the adapter checks
    // its own tags and never borrows authority from the requested key.
    if (a.kind == Kind::Retire) {
      Detach(i);
      if (s.alive) {
        adapter.Send(
            {eqemu_proof::Kind::Retire, s.epoch, s.generation, 0, 0, 0, 0});
      }
      return;
    }
    if (!lifetime) {
      e.error = true;
      e.permit.Close();
      return;
    }
    if (a.kind == Kind::Probe) {
      e.refused = false;
      e.permit.target = {a.target_id, a.target_incarnation};
      e.permit.Close();
      e.permit.contested = false;
      e.probe = e.permit.target;
      e.probe_ready = true;
      e.probe_ok = Probe(i, e.probe);
      return;
    }
    if (a.kind == Kind::Engage) {
      auto *prey = Find({a.target_id, a.target_incarnation});
      e.permit.Close();
      if (!prey || !e.probe_ok || !e.probe.Matches(Identify(prey)) ||
          e.permit.contested || !ActorBody::IsPublicHuntPrey(prey, true) ||
          prey->IsEngaged() || prey->IsMoving() ||
          !InArea(prey, ActorDemo::HuntSearchRadius)) {
        e.refused = true;
        return;
      }
      const auto report = adapter.StartPublicHunt(prey->GetID());
      const auto fresh = adapter.Inspect();
      if (report.status != eqemu_proof::Status::Ok || fresh.goal != a.goal) {
        e.refused = true;
        return;
      }
      e.permit.target = Identify(prey);
      e.permit.goal = fresh.goal;
      e.permit.enabled = true;
      e.permit.inhibited = false;
      e.error = false;
      return;
    }
    if (a.kind == Kind::Release) {
      Detach(i);
      adapter.ReleasePublicHunt();
      return;
    }
    if (auto *body = adapter.Body()) {
      if (a.kind == Kind::Rest) {
        e.permit.Close();
        e.permit.target = {};
        e.permit.contested = false;
        e.probe_ready = false;
        e.error = false;
        body->SetAppearance(eaSitting);
      }
      if (a.kind == Kind::Stand) {
        body->SetAppearance(eaStanding);
      }
    }
  }
  void Tick(bool force = false) {
    const auto now = Now();
    if (!force && now < next_tick) {
      return;
    }
    next_tick = now + 100;
    const auto scan = policy->needs_scan(now);
    if (scan) {
      Scan();
    }
    rust::Vec<eqemu_population::Prey> projection;
    for (const auto &p : observations) {
      projection.push_back(p);
    }
    auto actions =
        policy->step(now, Facts(), std::move(projection), scan, overflow);
    if (policy->failed()) {
      if (!failure_reported) {
        LogError("Actor population failed closed; inspect private "
                 "checkpoint/status evidence");
        failure_reported = true;
      }
      for (unsigned i = 0; i < entries.size(); ++i) {
        Detach(i);
      }
    }
    for (const auto &a : actions) {
      if (!policy->failed() || a.kind == eqemu_population::Kind::Retire) {
        Apply(a);
      }
    }
  }
};
std::unique_ptr<Native> native;
bool attempted = false;
} // namespace
bool Selected() {
  const auto *s = std::getenv("EQEMU_STAGING_ACTOR_SCENARIO");
  return s && std::strcmp(s, "population-hunt-v1") == 0;
}
void Process() {
  if (!Selected() || !zone || zone->GetZoneID() != 2 || zone->GetInstanceID() ||
      zone->GetInstanceVersion()) {
    return;
  }
  if (!attempted) {
    attempted = true;
    const auto *enabled = std::getenv("EQEMU_STAGING_ACTOR_DEMO");
    const auto *dir = std::getenv("EQEMU_STAGING_ACTOR_POPULATION_DIR");
    if (!enabled || std::strcmp(enabled, "1") || !dir ||
        ActorBody::LegacyAlive()) {
      return;
    }
    try {
      native = std::make_unique<Native>(dir);
    } catch (const std::exception &error) {
      LogError("Actor population startup refused [{}]", error.what());
      return;
    }
  }
  if (native) {
    native->Tick();
  }
}
bool CanCombat(Mob *a, Mob *b) {
  return !native || native->Can(Identify(a), Identify(b));
}
bool AllowCombat(Mob *a, Mob *b) {
  return !native || native->Allow(Identify(a), Identify(b));
}
bool PreserveRetaliationHate(Mob *prey) {
  if (!native) {
    return false;
  }
  for (unsigned i = 0; i < native->entries.size(); ++i) {
    auto &e = native->entries[i];
    if (!e.permit.target.Matches(Identify(prey)) || !native->Current(i)) {
      continue;
    }
    auto *body = Find(e.permit.actor);
    auto *target = Find(e.permit.target);
    return body && target && target->GetHateListCount() == 1 &&
           target->CheckAggro(body) &&
           ActorBody::PopulationAdapter(i).HasAdmittedPublicCombat(body,
                                                                   target) &&
           target->CombatRange(body) &&
           target->CheckLosFN(body->GetX(), body->GetY(), body->GetZ(),
                              body->GetSize()) &&
           target->IsAttackAllowed(body);
  }
  return false;
}
void ObserveIncomingSwing(Mob *prey, Mob *body, int64_t before, int64_t after) {
  if (!native) {
    return;
  }
  for (unsigned i = 0; i < native->entries.size(); ++i) {
    auto &e = native->entries[i];
    const auto s = ActorBody::PopulationAdapter(i).Inspect();
    if (!e.permit.MatchesIncoming(Identify(prey), Identify(body), s.epoch,
                                  s.generation, s.goal)) {
      continue;
    }
    ++e.incoming_swings;
    const auto loss =
        std::max<int64_t>(0, before) - std::max<int64_t>(0, after);
    if (loss > 0) {
      ++e.incoming_hits;
      e.incoming_damage += uint64_t(loss);
    }
    return;
  }
}
void ObserveDeath(NPC *victim) {
  if (!native) {
    return;
  }
  for (auto &e : native->entries) {
    const auto key = Identify(victim);
    if (key.incarnation && e.permit.actor.incarnation == key.incarnation &&
        (!key.id || e.permit.actor.id == key.id)) {
      e.dead = true;
    }
    e.permit.Death(Identify(victim));
  }
}
void BeginTeardown() {
  if (!native) {
    return;
  }
  for (auto &e : native->entries) {
    e.permit.Close();
  }
  native->policy->begin_shutdown();
  native->Tick(true);
}
void FinishTeardown() {
  if (native) {
    native->Tick(true);
    native.reset();
  }
  attempted = false;
}
} // namespace ActorPopulation
#endif

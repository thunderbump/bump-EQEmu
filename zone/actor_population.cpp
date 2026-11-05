#include "actor_population.h"
#include "actor_integration.h"
#ifdef EQEMU_RUST_BOOTSTRAP_BRIDGE
#include "actor-bridge/src/population.rs.h"
#include "actor_body.h"
#include "actor_inventory.h"
#include "actor_zones.h"
#include "worldserver.h"
#include "common/servertalk.h"
#include "actor_hunt_state.h"
#include "actor_population_native.h"
#include "entity.h"
#include "map.h"
#include "mob_movement_manager.h"
#include "npc.h"
#include "water_map.h"
#include "common/rulesys.h"
#include <algorithm>
#include "zone.h"
#include "zonedb.h"
#include <array>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <memory>
#include <stdexcept>
#include <map>
#include <unistd.h>

extern EntityList entity_list;
extern Zone *zone;
extern WorldServer worldserver;
extern ZoneDatabase content_db;
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
bool InArea(NPC *npc, float radius, NPC *actor = nullptr) {
  if (npc && actor && actor->UsesActorInventory() && actor->GetLevel() > 1) {
    return InsideArea(actor, npc->GetX(), npc->GetY(), npc->GetZ(), radius);
  }
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
  ActorDemo::HuntPosition home{ActorDemo::HuntHomeX, ActorDemo::HuntHomeY, 3.75};
  std::map<int32_t, int32_t> reputation;
  std::vector<ActorZones::Connection> routes;
  ActorZones::HuntBounds bounds;
  uint8_t bounds_level = 0;
  uint64_t owner_epoch = 0, travel_operation = 0;
  uint32_t boot_destination = 0, boot_connection = 0;
  uint32_t travel_connection = 0, travel_destination = 0;
  ActorDemo::HuntPosition boot_arrival{};
  bool destination_ready = false;
  Key probe;
  bool probe_ready = false, probe_ok = false, error = false;
  bool refused = false, dead = false;
  uint64_t incoming_swings = 0, incoming_hits = 0, incoming_damage = 0;
};
rust::Box<eqemu_population::Population> OpenPopulation(const char *directory) {
  ActorIntegration::Open(directory);
  return eqemu_population::population_open_zone(directory,
    std::to_string(getpid()) + "-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()),
    zone->GetZoneID());
}
class Native {
public:
  Native(const char *directory)
      : policy(OpenPopulation(directory)) {
    for (unsigned i = 0; i < entries.size(); ++i) {
      (void)ActorBody::PopulationAdapter(i);
    }
  }
  ~Native() { if (zone) { zone->quest_idle_override = prior_idle; } }
  const bool prior_idle = zone->quest_idle_override;
  uint64_t pending_boot_until = 0;
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
           (body->GetLevel() == 1 || PlayerHostile(body, prey)) &&
           InArea(body, ActorDemo::HuntLeash, body) &&
           InArea(prey, ActorDemo::HuntSearchRadius, body);
  }
  bool OwnsOrPublic(Mob *mob) const {
    if (!mob || !mob->IsNPC() || !mob->CastToNPC()->IsRustActor()) { return true; }
    for (const auto &entry : entries) { if (entry.permit.actor.Matches(Identify(mob))) { return true; } }
    return false;
  }
  bool Can(Key a, Key b) const {
    for (unsigned i = 0; i < entries.size(); ++i) {
      const auto &p = entries[i].permit;
      if (p.InvolvesActor(a, b)) {
        return Current(i) && p.Allows(a, b);
      }
    }
    const auto *first = Find(a);
    const auto *second = Find(b);
    return !(first && first->IsRustActor()) && !(second && second->IsRustActor());
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
    return Can(a, b);
  }
  void Detach(unsigned i) {
    auto &p = entries[i].permit;
    p.Close();
    auto *body = ActorBody::PopulationAdapter(i).Body();
    auto *prey = Find(p.target);
    if (!body || !p.actor.Matches(Identify(body)) ||
        !body->MatchesRustActor(p.epoch, p.generation)) {
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
    if (!body || !prey || !ActorBody::IsPublicHuntPrey(prey, true, body) ||
        prey->IsEngaged() || prey->IsMoving() || prey->IsCasting() ||
        prey->IsStunned() || prey->IsRooted() || prey->IsPseudoRooted() ||
        prey->IsMezzed() || prey->IsFeared() || prey->IsBlind() ||
        !InArea(body, ActorDemo::HuntLeash, body) ||
        !InArea(prey, ActorDemo::HuntSearchRadius, body)) {
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
    for (unsigned i = 0; i < entries.size(); ++i) {
      auto *body = ActorBody::PopulationAdapter(i).Body();
      entries[i].routes = body ? ActorZones::Nearby(*body, entries[i].reputation, entries[i].bounds) : std::vector<ActorZones::Connection>{};
    }
    observations.clear();
    overflow = false;
    for (const auto &pair : entity_list.GetNPCList()) {
      auto *npc = pair.second;
      // Projection is shared by all three controllers. The Rust profile
      // is authoritative; native service/ownership facts are refreshed.
      bool nearby = false;
      for (unsigned i = 0; i < entries.size(); ++i) {
        if (auto *body = ActorBody::PopulationAdapter(i).Body()) {
          nearby |= InArea(npc, ActorDemo::HuntSearchRadius, body);
        }
      }
      if (!nearby || npc->IsRustActor()) { continue; }
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
      for (unsigned i = 0; i < entries.size(); ++i) {
        auto *body = ActorBody::PopulationAdapter(i).Body();
        if (body && PlayerHostile(body, npc)) { p.hostile_mask |= (1u << i); }
      }
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
      ActorIntegration::ObserveDrain(i,b.drained);
      b.contested = e.permit.contested;
      b.error = e.error;
      b.refused = e.refused;
      b.travel_complete = e.travel_operation && s.phase == eqemu_proof::Phase::Idle && s.last_operation == e.travel_operation && s.last_terminal == eqemu_proof::Event::Completed;
      b.travel_failed = e.travel_operation && s.phase == eqemu_proof::Phase::Idle && s.last_operation == e.travel_operation && (s.last_terminal == eqemu_proof::Event::Interrupted || s.last_terminal == eqemu_proof::Event::Rejected);
      ActorZones::Connection current_route;
      bool resolved = e.travel_destination && body && ActorZones::Resolve(*body, e.travel_connection, e.travel_destination, current_route);
      if (!e.travel_operation && resolved && !body->IsMoving() && !body->IsEngaged() &&
          std::hypot(body->GetX()-current_route.exit.x, body->GetY()-current_route.exit.y) <= 5 && std::abs(body->GetZ()-current_route.exit.z) <= 6) { b.travel_complete = true; }
      resolved = resolved && b.travel_complete;
      if(resolved){ActorIntegration::Route(body,current_route.id,current_route.zone,current_route.exit.x,current_route.exit.y,current_route.exit.z,
        current_route.arrival.x,current_route.arrival.y,current_route.arrival.z,true);}
      b.destination_ready = e.destination_ready && resolved && e.boot_connection == current_route.id && e.boot_destination == current_route.zone &&
          std::hypot(body->GetX()-current_route.exit.x, body->GetY()-current_route.exit.y) <= 5 &&
          std::abs(body->GetZ()-current_route.exit.z) <= 6 && !body->IsMoving() && !body->IsEngaged() &&
          std::hypot(e.boot_arrival.x-current_route.arrival.x, e.boot_arrival.y-current_route.arrival.y) < 0.001 &&
          std::abs(e.boot_arrival.z-current_route.arrival.z) < 0.001;
      auto projected_routes = e.routes;
      if (b.travel_complete) { projected_routes.clear(); if (resolved) { projected_routes.push_back(current_route); } }
      for (const auto &route : projected_routes) {
        eqemu_population::Route r{};
        r.id = route.id; r.zone = route.zone;
        r.x = route.exit.x; r.y = route.exit.y; r.z = route.exit.z;
        r.arrival_x = route.arrival.x; r.arrival_y = route.arrival.y; r.arrival_z = route.arrival.z;
        r.hint_distance = route.hint_distance;
        b.routes.push_back(r);
      }
      if (body) {
        b.x = body->GetX();
        b.y = body->GetY();
        b.z = body->GetZ();
        b.hp = body->GetHP();
        b.max_hp = body->GetMaxHP();
        try { b.inventory_json = ActorInventory::Snapshot(*body); }
        catch (const std::exception &) { b.error = true; }
        b.level = body->GetLevel();
        const auto cap = RuleI(Character, MaxExpLevel) > 0 ? RuleI(Character, MaxExpLevel) : RuleI(Character, MaxLevel);
        b.level_cap = std::clamp(cap, 1, 127);
        b.next_level_at = body->GetBaseExperienceForLevel(b.level + 1);
        b.experience_cap = body->GetBaseExperienceForLevel(b.level_cap + 1);
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
          InArea(prey, ActorDemo::HuntSearchRadius, body) &&
          (!prey->IsEngaged() ||
           (body && prey->GetHateListCount() == 1 && prey->CheckAggro(body)));
      b.pending_kill =
          adapter.HasPendingTargetKill(e.permit.target.incarnation);
      b.outcome = NativeOutcome(s, e.dead);
      b.kill_experience = adapter.KillExperience();
      for (const auto &change : adapter.FactionChanges()) {
        b.factions.push_back({change.id, change.before, change.after});
      }
      if (s.approach == eqemu_proof::Approach::Defeated && !e.dead) {
        b.error = true;
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
    if (a.kind == Kind::BootDestination) {
      if (a.destination && a.owner_epoch && worldserver.Connected()) {
        ServerPacket request(ServerOP_ActorZoneBoot, sizeof(ServerActorZoneBoot_Struct));
        auto *payload = reinterpret_cast<ServerActorZoneBoot_Struct *>(request.pBuffer);
        payload->destination = a.destination; payload->actor = i; payload->owner_epoch = a.owner_epoch;
        payload->generation = payload->incarnation = 0;
        worldserver.SendPacket(&request);
        ActorIntegration::Boot(i,a.owner_epoch,a.level);
        pending_boot_until = Now() + 3000;
      }
      return;
    }
    if (a.kind == Kind::Spawn) {
      e.error = false;
      e.owner_epoch = a.owner_epoch;
      e.travel_operation = 0;
      e.travel_connection = e.travel_destination = 0;
      e.boot_destination = e.boot_connection = 0;
      e.destination_ready = false;
      e.refused = e.dead = false;
      e.permit.Close();
      e.probe_ready = false;
      if (!adapter.RestorePopulationBody(i, a.x, a.y, a.z, a.hp, a.level)) {
        e.error = true;
        return;
      }
      auto *body = adapter.Body();
      const auto fresh = adapter.Inspect();
      try {
        if (!ActorZones::MayEnter(*body, zone->GetZoneID())) { throw std::runtime_error("actor_zone_access_changed"); }
        if (body->GetBaseExperienceForLevel(a.level) > a.experience) { throw std::runtime_error("actor_experience_level_mismatch"); }
        body->RecalculateSkills();
        ActorInventory::Restore(*body, std::string(a.inventory_json));
        body->SetHP(a.hp); // item HP must be applied before restoring the wound
        if (body->GetHP() != a.hp) { throw std::runtime_error("actor_inventory_hp_changed"); }
        body->SendHPUpdate();
      }
      catch (const std::exception &) { e.error = true; }
      e.home = Position(body);
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
        ActorIntegration::Body(adapter.Body(),5);
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
    if (a.kind == Kind::Travel || a.kind == Kind::PrepareTransfer) {
      auto *body = adapter.Body();
      ActorZones::Connection route;
      e.refused = false;
      if (!body || !ActorZones::Resolve(*body, a.connection, a.destination, route) ||
          std::hypot(a.arrival_x-route.arrival.x, a.arrival_y-route.arrival.y) >= 0.001 ||
          std::abs(a.arrival_z-route.arrival.z) >= 0.001) { e.refused = true; return; }
      if (a.kind == Kind::Travel) {
        e.permit.Close();
        e.destination_ready = false;
        e.boot_destination = 0;
        e.travel_connection = a.connection; e.travel_destination = a.destination;
        if (std::hypot(body->GetX()-route.exit.x, body->GetY()-route.exit.y) <= 5 && std::abs(body->GetZ()-route.exit.z) <= 6) { e.travel_operation = 0; return; }
        const auto report = adapter.StartTransferMove(std::lround(route.exit.x), std::lround(route.exit.y), std::lround(route.exit.z));
        if (report.status != eqemu_proof::Status::Ok) { e.refused = true; return; }
        ActorIntegration::Route(body,route.id,route.zone,route.exit.x,route.exit.y,route.exit.z,route.arrival.x,route.arrival.y,route.arrival.z);
        e.travel_operation = report.operation;
        e.travel_connection = a.connection; e.travel_destination = a.destination;
      } else {
        if (body->IsMoving() || body->IsEngaged() || std::hypot(body->GetX()-route.exit.x, body->GetY()-route.exit.y) > 5 || std::abs(body->GetZ()-route.exit.z) > 6) { e.refused = true; return; }
        e.boot_destination = a.destination;
        e.boot_connection = a.connection;
        e.boot_arrival = route.arrival;
        if (worldserver.Connected()) {
          ServerPacket request(ServerOP_ActorZoneBoot, sizeof(ServerActorZoneBoot_Struct));
          auto *payload = reinterpret_cast<ServerActorZoneBoot_Struct *>(request.pBuffer);
          payload->destination = a.destination; payload->actor = i; payload->owner_epoch = e.owner_epoch;
          payload->generation = e.permit.generation; payload->incarnation = e.permit.actor.incarnation;
          worldserver.SendPacket(&request);
          ActorIntegration::Boot(body);
        }
      }
      return;
    }
    if (a.kind == Kind::LevelUp) {
      auto *body = adapter.Body();
      if (!body || a.level != body->GetLevel() + 1 || a.level > 127) {
        e.error = true;
        e.permit.Close();
        return;
      }
      const auto old_level=body->GetLevel();
      body->SetLevel(a.level);
      body->RecalculateSkills();
      body->CalcBonuses();
      ActorIntegration::Level(body,old_level,a.experience);
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
          e.permit.contested || !ActorBody::IsPublicHuntPrey(prey, true, adapter.Body()) ||
          prey->IsEngaged() || prey->IsMoving() ||
          !InArea(prey, ActorDemo::HuntSearchRadius, adapter.Body())) {
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
    for (unsigned i = 0; i < entries.size(); ++i) {
      auto *body = ActorBody::PopulationAdapter(i).Body();
      if (body && !entries[i].permit.enabled && !body->IsMoving()) { entries[i].home = Position(body); }
      if (body) {
        const auto bounds = eqemu_population::hunting_bounds(body->GetLevel());
        entries[i].bounds = {bounds.minimum, bounds.maximum};
        entries[i].bounds_level = body->GetLevel();
      }
      entries[i].reputation.clear();
      for (const auto &standing : policy->reputation(i)) {
        entries[i].reputation.emplace(standing.id, standing.value);
      }
    }
    ActorIntegration::Drain();
    const auto scan = policy->needs_scan(now);
    if (scan) {
      Scan();
    }
    rust::Vec<eqemu_population::Prey> projection;
    for (const auto &p : observations) {
      projection.push_back(p);
    }
    auto facts=Facts();
    // Facts can observe native drain. Publish those observations before Rust
    // can commit destination custody or release a lease from the same frame.
    ActorIntegration::Drain();
    auto actions =
        policy->step(now, std::move(facts), std::move(projection), scan, overflow);
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
    if(ActorIntegration::Enabled()) {
      ActorIntegration::Drain();
      auto *actor=ActorBody::PopulationAdapter(0).Body();NPC *hostile=nullptr,*friendly=nullptr;
      if(actor && actor->GetLevel()>1 && !actor->IsMoving()) {
        for(const auto &[id,npc]:entity_list.GetNPCList()) {
          if(npc->GetNPCTypeID()==2014 && npc->GetHP()>0 && !npc->GetDepop() && (!hostile || npc->NativeIncarnation()<hostile->NativeIncarnation())){hostile=npc;}
          if(npc->GetNPCTypeID()==2021 && npc->GetHP()>0 && !npc->GetDepop() && (!friendly || npc->NativeIncarnation()<friendly->NativeIncarnation())){friendly=npc;}
        }
        ActorIntegration::Geometry(actor,hostile,friendly);
      }
      ActorIntegration::Poll();
    }
    zone->quest_idle_override = prior_idle || policy->active() || now < pending_boot_until;
  }
};
std::unique_ptr<Native> native;
bool attempted = false;
uint64_t retry_at = 0;
} // namespace
eqemu_integration::Context ObservationContext(NPC *npc) {
  eqemu_integration::Context c{};c.zone=zone?zone->GetZoneID():0;c.kind=3;
  if(!npc){return c;}
  c.native_id=npc->GetID();c.incarnation=npc->NativeIncarnation();c.race=npc->GetFactionRace();
  c.class_id=npc->GetClass();c.deity=npc->GetDeity();c.level=npc->GetLevel();c.kind=2;
  if(native && npc->UsesActorInventory()) {
    for(unsigned i=0;i<native->entries.size();++i) {
      const auto state=ActorBody::PopulationAdapter(i).Inspect();
      if(npc->MatchesRustActor(state.epoch,state.generation)) {
        c.actor=i+1;c.kind=1;c.owner_epoch=native->entries[i].owner_epoch;c.epoch=state.epoch;
        c.generation=state.generation;c.goal=state.goal;c.operation=state.operation;break;
      }
    }
  }
  return c;
}
bool Selected() {
  const auto *s = std::getenv("EQEMU_STAGING_ACTOR_SCENARIO");
  return s && std::strcmp(s, "population-hunt-v1") == 0;
}
void Process() {
  if (!Selected() || !zone || zone->GetInstanceID() ||
      zone->GetInstanceVersion()) {
    return;
  }
  if (!attempted) {
    if (Now() < retry_at) { return; }
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
      if (std::string(error.what()).find("owner_lock_refused") != std::string::npos) {
        attempted = false;
        retry_at = Now() + 1000;
      }
      return;
    }
  }
  if (native) {
    native->Tick();
  }
}
void DestinationReady(uint32_t actor, uint64_t owner_epoch, uint64_t generation, uint64_t incarnation, uint32_t destination) {
  if (!native || actor >= native->entries.size()) { return; }
  auto &entry = native->entries[actor];
  if (generation && incarnation && entry.owner_epoch == owner_epoch && entry.permit.generation == generation &&
      entry.permit.actor.incarnation == incarnation && entry.boot_destination == destination && Find(entry.permit.actor)) {
    entry.destination_ready = true;
    ActorIntegration::Body(Find(entry.permit.actor),3,destination);
  }
}
bool InsideArea(NPC *actor, double x, double y, double z, float radius) {
  if (!actor || !std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z)) { return false; }
  if (!native || actor->GetLevel() == 1) {
    return ActorDemo::InsideHuntArea({x,y,z}, {ActorDemo::HuntHomeX,ActorDemo::HuntHomeY,3.75}, radius);
  }
  for (const auto &entry : native->entries) {
    if (entry.permit.actor.Matches(Identify(actor))) {
      return ActorDemo::InsideHuntArea({x,y,z}, entry.home, radius);
    }
  }
  return false;
}
std::vector<FactionChange> FactionAwards(NPC *actor, NPC *victim) {
  std::vector<FactionChange> changes;
  if (!native || !actor || !victim) { return changes; }
  for (const auto &entry : native->entries) {
    if (!entry.permit.actor.Matches(Identify(actor))) { continue; }
    auto standings = entry.reputation;
    for (const auto &hit : zone->GetNPCFactionEntries(victim->GetNPCFactionID())) {
      if (hit.faction_id <= 0 || !hit.value) { continue; }
      FactionMods mods{};
      if (!content_db.GetFactionData(&mods, actor->GetClass(), actor->GetFactionRace(), actor->GetDeity(), hit.faction_id)) { continue; }
      auto &value = standings[hit.faction_id];
      const auto before = value;
      actor->ApplyPersonalFaction(value, hit.value, std::min(0, mods.min - mods.base), std::max(0, mods.max - mods.base));
      if (value != before) { changes.push_back({int32_t(hit.faction_id), before, value}); }
    }
    break;
  }
  return changes;
}
bool PlayerHostile(NPC *actor, NPC *observer) {
  if (!native || !actor || !observer || !actor->IsRustActor() || actor->GetLevel() <= 1) { return false; }
  // Revalidation is native-only, including callbacks before another Rust tick.
  for (const auto &entry : native->entries) {
    if (!entry.permit.actor.Matches(Identify(actor))) { continue; }
    if (entry.bounds_level != actor->GetLevel() || observer->GetLevel() < entry.bounds.minimum || observer->GetLevel() > entry.bounds.maximum) { return false; }
    const auto faction = observer->GetPrimaryFaction();
    const auto found = entry.reputation.find(faction);
    const auto standing = found == entry.reputation.end() ? 0 : found->second;
    const auto reaction = actor->GetPlayerFactionLevel(observer, faction,
        actor->GetFactionRace(), actor->GetClass(), actor->GetDeity(), 0, standing);
    return reaction == FACTION_THREATENINGLY || reaction == FACTION_SCOWLS;
  }
  return false;
}
bool CanCombat(Mob *a, Mob *b) {
  return native ? native->OwnsOrPublic(a) && native->OwnsOrPublic(b) && native->Can(Identify(a), Identify(b)) : !((a && a->IsNPC() && a->CastToNPC()->IsRustActor()) || (b && b->IsNPC() && b->CastToNPC()->IsRustActor()));
}
bool AllowCombat(Mob *a, Mob *b) {
  const bool allowed=native ? native->OwnsOrPublic(a) && native->OwnsOrPublic(b) && native->Allow(Identify(a), Identify(b)) : CanCombat(a,b);
  ActorIntegration::Gate(a,b,allowed);
  return allowed;
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
      if(!e.dead){ActorIntegration::Body(victim,8);}
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
    ActorIntegration::Drain();
    ActorIntegration::Finish();
  }
  attempted = false;
}
} // namespace ActorPopulation
#endif

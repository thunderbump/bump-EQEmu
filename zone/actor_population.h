#pragma once
#include <cstdint>
#include <vector>
class Mob;
class NPC;
namespace ActorPopulation {
struct FactionChange { int32_t id = 0, before = 0, after = 0; };
std::vector<FactionChange> FactionAwards(NPC *actor, NPC *victim);
bool Selected();
bool InsideArea(NPC *actor, double x, double y, double z, float radius);
void DestinationReady(uint32_t actor, uint64_t owner_epoch, uint64_t generation, uint64_t incarnation, uint32_t destination);
// Player-equivalent reaction using the owning Rust actor reputation cache.
bool PlayerHostile(NPC *actor, NPC *observer);
void Process();
void BeginTeardown();
void FinishTeardown();
bool CanCombat(Mob *attacker, Mob *victim);
bool AllowCombat(Mob *attacker, Mob *victim);
bool PreserveRetaliationHate(Mob *prey);
void ObserveIncomingSwing(Mob *prey, Mob *body, int64_t before, int64_t after);
void ObserveDeath(NPC *victim);
} // namespace ActorPopulation

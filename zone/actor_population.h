#pragma once
#include <cstdint>
class Mob;
class NPC;
namespace ActorPopulation {
bool Selected();
void Process();
void BeginTeardown();
void FinishTeardown();
bool CanCombat(Mob *attacker, Mob *victim);
bool AllowCombat(Mob *attacker, Mob *victim);
bool PreserveRetaliationHate(Mob *prey);
void ObserveIncomingSwing(Mob *prey, Mob *body, int64_t before, int64_t after);
void ObserveDeath(NPC *victim);
} // namespace ActorPopulation

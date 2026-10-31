#pragma once

class Mob;
namespace ActorDemo {
void Process();
void BeginTeardown();
void FinishTeardown();
// Called only at real native attack/damage entry points, never perception.
bool AllowCombat(Mob *attacker, Mob *target);
// Permission queries do not count as interference. Real attempts use AllowCombat.
bool CanCombat(Mob *attacker, Mob *target);
}

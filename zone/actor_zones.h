#pragma once
#include "actor_hunt_state.h"
#include <cstdint>
#include <map>
#include <vector>
class NPC;
namespace ActorZones {
struct HuntBounds { uint8_t minimum = 1, maximum = 1; };
struct Connection {
  uint32_t id = 0, zone = 0;
  ActorDemo::HuntPosition exit{}, arrival{};
  double distance = 0;
};
bool MayEnter(NPC &actor, uint32_t destination);
bool Resolve(NPC &actor, uint32_t id, uint32_t destination, Connection &connection);
std::vector<Connection> Nearby(NPC &actor, const std::map<int32_t, int32_t> &reputation, HuntBounds bounds);
}

#pragma once
#include "actor_hunt_state.h"
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>
class NPC;
class Database;
namespace ActorZones {
struct HuntBounds { uint8_t minimum = 1, maximum = 1; };
struct Connection {
	uint32_t id = 0, zone = 0;
	ActorDemo::HuntPosition exit{}, arrival{};
	// Native geometric fact only; Rust composes the route selection cost.
	double hint_distance = 0;
};
struct SpawnHint {
	uint32_t spawn_id;
	float x, y;
	uint8_t level;
	int32_t faction;
};
// Content rows and runtime overrides live on separate connections. Failure is
// distinct from an empty projection and must never imply all rows are enabled.
std::optional<std::vector<SpawnHint>> ReadSpawnHints(Database &content, Database &runtime,
	const std::string &name, HuntBounds bounds);
bool MayEnter(NPC &actor, uint32_t destination);
bool Resolve(NPC &actor, uint32_t id, uint32_t destination, Connection &connection);
std::vector<Connection> Nearby(NPC &actor, const std::map<int32_t, int32_t> &reputation, HuntBounds bounds);
}

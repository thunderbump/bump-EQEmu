#pragma once
#include <cstdint>
class Mob;
class NPC;
namespace ActorIntegration {
// Always inert outside the explicit test fixture. Callbacks only copy POD data
// into a fixed zone-thread buffer. Drain/Poll are the only Rust entry points.
bool Enabled();
void Open(const char *directory);
void Drain();
void Poll();
void Finish();
#if defined(EQEMU_BUILD_TESTS) && defined(EQEMU_RUST_BOOTSTRAP_BRIDGE)
bool CaptureBufferControl();
#endif
void Registered(NPC *npc);
void Removed(NPC *npc);
void Death(NPC *npc);
void Reaction(Mob *actor, Mob *observer, int32_t faction, int32_t standing,
	int32_t item_bonus, int32_t spell_bonus, uint8_t reaction);
void Gate(Mob *attacker, Mob *victim, bool allowed);
uint16_t Award(NPC *actor, NPC *victim, uint64_t base, uint64_t scaled,
	uint64_t final, uint64_t threshold, uint32_t factions);
void Faction(NPC *actor, NPC *victim, uint16_t award, int32_t id, int32_t before, int32_t after);
void Level(NPC *actor, uint8_t old_level, uint64_t experience);
void Body(NPC *actor, uint8_t phase, uint64_t peer_zone = 0);
void Boot(NPC *actor);
void Boot(unsigned actor, uint64_t owner_epoch, uint8_t level);
void ObserveDrain(unsigned actor, bool drained);
void PathAdmitted(Mob *actor, double x, double y, double z);
void Route(NPC *actor, uint32_t id, uint32_t destination, double x, double y, double z,
	double arrival_x, double arrival_y, double arrival_z, bool completed = false);
void Geometry(NPC *actor, NPC *hostile, NPC *friendly);
#ifdef EQEMU_RUST_BOOTSTRAP_BRIDGE
} // namespace ActorIntegration
#include "actor-bridge/src/integration.rs.h"
namespace ActorPopulation {
eqemu_integration::Context ObservationContext(NPC *npc);
}
namespace ActorIntegration {
// RAII observes an actual native attack invocation, including nested entries.
// No pointer survives the invocation; identity is revalidated before HP read.
class Attack {
public:
	Attack(Mob *attacker, Mob *victim, bool allowed);
	~Attack();
private:
	eqemu_integration::NativeEvent event_{};
	uint16_t id_ = 0;
	uint64_t incarnation_ = 0;
	bool active_ = false;
};
#endif
} // namespace ActorIntegration

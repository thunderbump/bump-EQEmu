#pragma once
#include "actor_demo_native.h"
#include <cmath>

namespace ActorDemo {
constexpr float HuntHomeX = -917, HuntHomeY = 1224;
constexpr float HuntSearchRadius = 150, HuntLeash = 180, HuntZRange = 6;
struct HuntPosition { double x = 0, y = 0, z = 0; };
inline double DistanceSquared(HuntPosition a, HuntPosition b) {
    return (a.x - b.x) * (a.x - b.x) + (a.y - b.y) * (a.y - b.y);
}
inline bool InsideHuntArea(HuntPosition p, HuntPosition home, double radius) {
    return std::isfinite(p.x) && std::isfinite(p.y) && std::isfinite(p.z) &&
        DistanceSquared(p, home) <= radius * radius && std::abs(p.z - home.z) <= HuntZRange;
}
inline bool HuntType(uint32_t type, uint8_t level) {
    return ((type == 2011 || type == 2104) && level == 1) || (type == 2000 && level == 2);
}
struct HuntCandidate {
    NativeIdentity key;
    HuntPosition position;
    uint32_t type = 0;
    uint8_t level = 0;
    bool natural = false, alive = false, targetable = false, owned = false;
    bool engaged = false, moving = false, service = false;
};
inline bool HuntEligible(const HuntCandidate &c, HuntPosition actor, HuntPosition home) {
    return c.key.id && c.key.incarnation && HuntType(c.type, c.level) && c.natural &&
        c.alive && c.targetable && !c.owned && !c.engaged && !c.moving && !c.service &&
        InsideHuntArea(c.position, home, HuntSearchRadius) &&
        std::abs(c.position.z - actor.z) <= HuntZRange;
}
inline bool HuntNearer(const HuntCandidate &a, const HuntCandidate &b, HuntPosition actor) {
    const auto da = DistanceSquared(actor, a.position), db = DistanceSquared(actor, b.position);
    return da < db || (da == db && a.key.incarnation < b.key.incarnation);
}
inline bool HuntRested(int64_t hp, int64_t maximum) {
    return hp > 0 && maximum > 0 && hp >= maximum - maximum / 10;
}
} // namespace ActorDemo

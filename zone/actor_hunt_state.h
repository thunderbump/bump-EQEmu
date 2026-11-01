#pragma once
#include "actor_demo_native.h"
#include <array>
#include <cmath>

namespace ActorDemo {
constexpr float HuntHomeX = -917, HuntHomeY = 1224;
constexpr float HuntSearchRadius = 150, HuntLeash = 180, HuntZRange = 6;
constexpr float HuntDirectRange = 350, HuntRouteLimit = 400;
constexpr unsigned HuntRouteNodes = 64;
constexpr uint64_t HuntSearchIntervalMs = 2000, HuntRestMinimumMs = 10000;
constexpr int64_t HuntRestHPPercent = 90;
struct HuntPosition { double x = 0, y = 0, z = 0; };
// One bounded snapshot of read-only selection probes, replaced each search.
struct HuntRouteEvidence {
    const char *result = "not_checked";
    double direct_distance = 0, length = 0, endpoint_error = 0;
    unsigned nodes = 0;
};
enum class HuntRejection { None, Identity, FloorInvalid, FloorHeight, Water, Los, Route, Count };
inline const char *HuntRejectionName(HuntRejection reason) {
    switch (reason) {
    case HuntRejection::None: return "none";
    case HuntRejection::Identity: return "identity";
    case HuntRejection::FloorInvalid: return "floor_invalid";
    case HuntRejection::FloorHeight: return "floor_height";
    case HuntRejection::Water: return "water";
    case HuntRejection::Los: return "los";
    case HuntRejection::Route: return "route";
    case HuntRejection::Count: break;
    }
    return "none";
}
struct HuntSelectionEvidence {
    std::array<unsigned, static_cast<unsigned>(HuntRejection::Count)> rejected{};
    NativeIdentity nearest;
    HuntPosition position, endpoint;
    HuntRejection reason = HuntRejection::None;
    double floor = 0, z_offset = 0, height_error = 0;
    bool floor_valid = false, los = false;
    HuntRouteEvidence route;
    void Reject(HuntRejection why) { ++rejected[static_cast<unsigned>(why)]; }
};
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
inline bool ActorRouteDistanceAllowed(bool public_hunt, double distance) {
    return std::isfinite(distance) && distance >= (public_hunt ? 0.5 : 4.0) &&
        distance <= (public_hunt ? HuntDirectRange : 80.0);
}
inline bool ActorMeleeReachAllowed(bool public_hunt, double xy, double z, bool native_reach) {
    return std::isfinite(xy) && std::isfinite(z) && native_reach && std::abs(z) <= HuntZRange &&
        (public_hunt || xy <= 5);
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
    if (hp <= 0 || maximum <= 0) { return false; }
    // Ceiling of the percentage without multiplying a full int64 HP maximum.
    const auto required = maximum / 100 * HuntRestHPPercent + (maximum % 100 * HuntRestHPPercent + 99) / 100;
    return hp >= required;
}
} // namespace ActorDemo

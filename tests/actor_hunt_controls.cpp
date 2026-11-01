#include "../zone/actor_demo_state.h"
#include "../zone/actor_hunt_state.h"
#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <vector>

using namespace ActorDemo;
namespace {
void Require(bool condition, const char *why) { if (!condition) { throw std::runtime_error(why); } }

HuntCandidate Prey(uint16_t id, uint64_t incarnation, double x, double y = 0) {
    HuntCandidate c{{id, incarnation}, {x, y, 0}, 2011, 1};
    c.natural = c.alive = c.targetable = true;
    return c;
}
void NearestAndEligibility() {
    const HuntPosition home{0, 0, 0}, actor{100, 0, 0};
    auto near_home = Prey(1, 100, 20), near_actor = Prey(2, 200, 120);
    std::vector<HuntCandidate> candidates{near_home, near_actor};
    std::sort(candidates.begin(), candidates.end(), [&](const auto &a, const auto &b) { return HuntNearer(a, b, actor); });
    Require(candidates.front().key.id == 2, "nearest measured from home instead of current actor");
    auto tie = Prey(3, 150, 80);
    Require(HuntNearer(tie, near_actor, actor), "native incarnation tie break");
    Require(HuntEligible(near_actor, actor, home), "idle natural rat excluded");
    for (int exclusion = 0; exclusion < 11; ++exclusion) {
        auto c = near_actor;
        switch (exclusion) {
        case 0: c.natural = false; break;
        case 1: c.alive = false; break;
        case 2: c.targetable = false; break;
        case 3: c.owned = true; break;
        case 4: c.engaged = true; break;
        case 5: c.moving = true; break;
        case 6: c.service = true; break;
        case 7: c.type = 2030; break;
        case 8: c.level = 2; break;
        case 9: c.position.x = 151; break;
        case 10: c.position.z = 7; break;
        }
        Require(!HuntEligible(c, actor, home), "ineligible public target admitted");
    }
    auto skeleton = near_actor; skeleton.type = 2104;
    Require(HuntEligible(skeleton, actor, home), "level1 skeleton excluded");
    skeleton.type = 2000; skeleton.level = 2;
    Require(HuntEligible(skeleton, actor, home), "level2 skeleton excluded");
    Require(InsideHuntArea({180, 0, 6}, home, HuntLeash) && !InsideHuntArea({180.01, 0, 0}, home, HuntLeash), "leash boundary");
    Require(!HuntRested(89, 100) && HuntRested(90, 100) && !HuntRested(0, 100), "real HP rest threshold");
}

void PersistentRestAndNoTarget() {
    Run run;
    run.Start(1000, 1800, true);
    Require(run.Advance(1000, {}) == Action::Spawn, "initial body allocation");
    Facts f; f.ready = true;
    Require(run.Advance(1001, f) == Action::Stand && run.phase == Phase::Searching, "body ready for search");
    Require(run.Advance(641001, {}) == Action::None && run.phase == Phase::Searching && !run.failed,
        "natural640s respawn wait misclassified as stalled combat");
    f = {}; f.target_available = true;
    Require(run.Advance(641002, f) == Action::Engage && run.attempted == 1, "natural hunt admission");
    f = {}; f.victory = true;
    Require(run.Advance(641003, f) == Action::Release && run.completed == 1, "victory must release goal, not body");
    Require(run.Advance(641004, {}) == Action::None && run.phase == Phase::Releasing, "rest before correlated stop drain");
    f = {}; f.goal_idle = true;
    Require(run.Advance(641005, f) == Action::Rest, "visible rest admission");
    f = {}; f.rested = true;
    Require(run.Advance(651004, f) == Action::None, "minimum ten second rest");
    f.rested = false;
    Require(run.Advance(701005, f) == Action::None && run.phase == Phase::Resting, "low native HP treated as stall/refilled");
    f.rested = true;
    Require(run.Advance(701006, f) == Action::Stand && run.phase == Phase::Searching, "native HP recovery");
    f = {}; f.target_available = true;
    Require(run.Advance(701007, f) == Action::Engage && run.attempted == 2, "second goal replaced body");
    Require(run.completed == 1 && run.failed == 0, "persistent encounter counters");
}

void ImmediateContentionAndOnlyActorCleanup() {
    Custody c; c.public_hunt = true; c.combat_enabled = true;
    c.actor = {10, 100}; c.target = {20, 200}; c.epoch = 7; c.generation = 1;
    const NativeIdentity player{}, other_npc{30, 300};
    Require(c.CanCombat(player, c.target) && !c.contested, "passive public permission caused contention");
    Require(c.AdmitCombat(player, c.target) && c.contested, "player public damage/loot was blocked");
    // This is the actual hook ordering: an input/hate/damage attempt latches
    // contention before the earlier ActorBody::Process, without a demo tick.
    Require(!c.CanCombat(c.actor, c.target), "actor got a subsequent swing before controller noticed player");
    Require(c.CanCombat(c.target, player) && c.AdmitCombat(other_npc, c.target), "ordinary public combat blocked");
    NativeView view{7, 1, true, c.actor, c.target};
    const auto stop = c.PlanRetirement(view);
    Require(stop.actor && !stop.target && !stop.drained, "public target inherited owned retirement");
    view.actor = {}; view.alive = false;
    Require(c.PlanRetirement(view).drained, "public target presence blocked actor cleanup");
    c.contested = false;
    const NativeIdentity reused{20, 201};
    Require(c.AdmitCombat(player, reused) && !c.contested, "reused target ID inherited contention scope");
    Require(!c.CanCombat(c.actor, reused), "actor inherited reused target authority");
    Require(!c.AdmitCombat(c.actor, other_npc) && c.interference, "actor attacked a bystander");
}

void YieldRecoveryAndGoalCounters() {
    Run run; run.Start(1000, 1800, true);
    run.Advance(1000, {});
    Facts f; f.ready = true; run.Advance(1001, f);
    f = {}; f.target_available = true; run.Advance(1002, f);
    f = {}; f.yield = "target_contested";
    Require(run.Advance(1003, f) == Action::Release && run.yielded == 1 && run.failed == 0, "competition permanently failed run");
    f = {}; f.goal_idle = true; run.Advance(1004, f);
    f = {}; f.rested = true; run.Advance(11004, f);
    f = {}; f.target_available = true; run.Advance(11005, f);
    f = {}; f.progress = true;
    Require(run.Advance(71005, f) == Action::Release && run.last_outcome == "encounter_stalled", "unbounded encounter despite progress");

    Custody c; c.public_hunt = true; c.epoch = 7; c.generation = 1; c.goal = 1;
    c.actor = {10, 100}; c.target = {20, 200};
    NativeView view{7, 1, true, c.actor, c.target, false, 1};
    c.ObserveCounters(view, {3, 2, 11});
    c.goal = 2; c.last = {};
    c.ObserveCounters(view, {99, 99, 999});
    Require(c.total.damage == 11, "old goal counters inherited authority on persistent body");
    view.goal = 2; view.target = {21, 201};
    c.ObserveCounters(view, {4, 4, 24});
    Require(c.total.swings == 7 && c.total.hits == 6 && c.total.damage == 35, "native counters reset across goals");
    view.target = {};
    Require(c.TargetLost(view, false, false), "player kill credited actor");
    view.target_kill_pending = true;
    Require(!c.TargetLost(view, false, false), "correlated actor death latch lost before Rust terminal");
}

void NativeReachAndShortApproaches() {
    // A prey at5.1 produces Rust's rounded two-unit-short destination x3.
    // That is a valid public approach even though the proof requires >=4.
    const auto destination = std::round(5.1 - 2);
    Require(ActorRouteDistanceAllowed(true, destination) && !ActorRouteDistanceAllowed(false, destination),
        "nearby stationary prey skipped by proof minimum");
    Require(ActorRouteDistanceAllowed(false, 80) && !ActorRouteDistanceAllowed(false, 80.1) &&
        ActorRouteDistanceAllowed(true, 350) && !ActorRouteDistanceAllowed(true, 350.1), "route limits changed");
    // Native CombatRange includes size/flee modifiers. Do not replace that
    // native fact with the proof's stricter XY<=5 requirement during a hunt.
    Require(ActorMeleeReachAllowed(true, 7, 0, true) && !ActorMeleeReachAllowed(false, 7, 0, true),
        "moving/fleeing prey lost while native reach still permits melee");
    Require(!ActorMeleeReachAllowed(true, 7, 0, false) && !ActorMeleeReachAllowed(true, 4, 7, true),
        "out-of-reach or wrong-floor target admitted");
    Require(!HuntRested(9, 11) && HuntRested(10, 11), "fractional native HP threshold rounding");
}

void IdlePublicSpawnFloor() {
    // Root's read-only native capture: an untouched idle public rat keeps its
    // spawn Z, while the owned actor and route nodes use adjusted floor Z.
    const HuntPosition home{-917, 1224, 3.75}, actor{-917, 1224, 4.1};
    auto rat = Prey(164, 164, -935, 1194);
    rat.position.z = 9.1;
    const double floor = -0.00000190735, offset = 4.375;
    Require(HuntEligible(rat, actor, home), "captured natural idle rat excluded before geometry");
    Require(std::abs(rat.position.z - floor - offset) > 2, "capture no longer reproduces old floor rejection");
    // The production selector must admit this floor fact. It still separately
    // requires dry geometry, native LOS and the native complete-route probe.
    Require(HuntFloorAllowed(rat.position.z, floor, offset), "reachable untouched idle spawn Z rejected");
    Require(HuntFloorAllowed(offset, 0, offset), "floor-adjusted standing prey rejected");
    Require(HuntFloorAllowed(offset + 6, 0, offset) && !HuntFloorAllowed(offset + 6.001, 0, offset),
        "upward floor discrepancy exceeded native six-unit Z envelope");
    Require(HuntFloorAllowed(offset - 2, 0, offset) && !HuntFloorAllowed(offset - 2.001, 0, offset),
        "existing lower floor tolerance relaxed");
    for (const auto invalid : {std::numeric_limits<double>::quiet_NaN(), std::numeric_limits<double>::infinity()}) {
        Require(!HuntFloorAllowed(invalid, 0, offset) && !HuntFloorAllowed(offset, invalid, offset) &&
            !HuntFloorAllowed(offset, 0, invalid), "invalid native floor observation admitted");
    }
    rat.position.z = 10;
    Require(HuntFloorAllowed(rat.position.z, 0, offset) && !HuntEligible(rat, actor, home),
        "floor slack bypassed the independent home/current actor Z gate");
}

void NativeRetaliationAndIncoming() {
    Custody c; c.public_hunt = c.combat_enabled = true;
    c.epoch = 7; c.generation = 2; c.goal = 3;
    c.actor = {258, 185}; c.target = {164, 164};
    NativeView view{7, 2, true, c.actor, c.target, false, 3};
    const bool admitted = c.CanRetaliate(view, c.target, c.actor, true);
    Require(admitted && !NPCFactionWarWipeRequired(false, admitted),
        "npc_aggro0 native target check wipes admitted sole actor hate");
    Require(NPCFactionWarWipeRequired(false, false) && !NPCFactionWarWipeRequired(true, false),
        "unrelated NPC faction-war behavior changed");
    Require(!c.CanRetaliate(view, c.target, c.actor, false), "unadmitted native operation retained hate");
    for (unsigned stale = 0; stale < 6; ++stale) {
        auto old = view;
        switch (stale) {
        case 0: ++old.epoch; break;
        case 1: ++old.generation; break;
        case 2: ++old.goal; break;
        case 3: ++old.actor.incarnation; break;
        case 4: ++old.target.incarnation; break;
        case 5: old.alive = false; break;
        }
        Require(!c.CanRetaliate(old, c.target, c.actor, true), "stale native identity inherited retaliation exception");
    }
    c.contested = true;
    Require(!c.CanRetaliate(view, c.target, c.actor, true), "contention allowed another native retaliation");
    c.contested = false; c.interference = true;
    Require(!c.CanRetaliate(view, c.target, c.actor, true), "interference retained retaliation exception");
    c.interference = false; c.public_hunt = false;
    Require(!c.CanRetaliate(view, c.target, c.actor, true), "v1/proof inherited public retaliation policy");
    c.public_hunt = true;
    c.ObserveHP(100, false);
    c.ObserveIncoming(view, c.target, c.actor, 100, 100); // actual native non-damaging swing
    c.ObserveIncoming(view, c.target, c.actor, 100, 96);
    c.ObserveHP(98, true); c.ObserveHP(100, true);
    Require(c.incoming.swings == 2 && c.incoming.hits == 1 && c.incoming.damage == 4 &&
        c.minimum_hp == 96 && c.rest_hp_gain == 4, "native swing/HP/rest telemetry conflated");
    auto old = view; ++old.goal;
    c.ObserveIncoming(old, c.target, c.actor, 100, 1);
    c.combat_enabled = false;
    c.ObserveIncoming(view, c.target, c.actor, 100, 1);
    Require(c.incoming.damage == 4 && !c.CanRetaliate(view, c.target, c.actor, true), "released goal recorded incoming attacks");
    Require(c.PlanRetirement(view).actor && !c.PlanRetirement(view).target, "retaliation changed public cleanup ownership");
    c.combat_enabled = true;
    c.ObserveIncoming(view, c.target, c.actor, 100, -4);
    Require(c.incoming.damage == 104 && c.minimum_hp == 0, "fatal incoming loss did not clamp at zero HP");
}
}
int main() {
    try {
        NearestAndEligibility(); PersistentRestAndNoTarget(); ImmediateContentionAndOnlyActorCleanup(); YieldRecoveryAndGoalCounters(); NativeReachAndShortApproaches(); IdlePublicSpawnFloor(); NativeRetaliationAndIncoming();
        std::cout << "public hunt controls: 7 groups passed\n";
        return EXIT_SUCCESS;
    } catch (const std::exception &error) {
        std::cerr << "public hunt controls: " << error.what() << '\n';
        return EXIT_FAILURE;
    }
}

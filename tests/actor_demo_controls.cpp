#include "../zone/actor_demo_state.h"
#include "../zone/actor_demo_native.h"
#include <cstdlib>
#include <iostream>
#include <stdexcept>

using namespace ActorDemo;

namespace {
void Require(bool condition, const char *why)
{
    if (!condition) { throw std::runtime_error(why); }
}

void Commands()
{
    Command c;
    Require(ParseCommand("start session 1", c) && c.start && c.seconds == 1800, "default duration");
    Require(ParseCommand("start session 18446744073709551615 10", c), "uint64 maximum");
    for (const auto *text : {"start s 0", "start s -1", "start s +1", "start s 18446744073709551616",
         "start s 1 9", "start s 1 1801", "start s 1 10 extra", "stop s 1 10", "start s 1 10x", "unknown s 1",
         "start\ns\n1", "start s 1\n\n", "start s 1\rstop s 2", "start s\v1"}) {
        Require(!ParseCommand(text, c), "malformed command accepted");
    }
    Require(!ParseCommand(std::string(129, 'x'), c), "oversized command");
    Require(ParseCommand("start s 1\r\n", c) && ParseCommand("stop s 2\n", c), "final line terminator");
    Require(ParseCommand("start old-session 1 10", c), "old session syntax");
    uint64_t last = 0;
    Require(std::string(AdmitRequest(c, "new-session", last)) == "stale_session" && last == 0,
        "restart replay changed cursor");
    Require(AdmitRequest(c, "old-session", last) == nullptr && last == 1, "request admission");
    Require(std::string(AdmitRequest(c, "old-session", last)) == "replayed_request", "duplicate admission");
    Require(ParseCommand("stop old-session 2", c) && AdmitRequest(c, "old-session", last) == nullptr,
        "stop admission");
}

void EncountersAndStop()
{
    Run run;
    run.Start(1000, 1800);
    Require(run.Advance(1000, {}) == Action::Spawn && run.attempted == 1, "first allocation");
    Require(run.Advance(1001, {}) == Action::None, "engaged before native presence");
    Facts f;
    f.ready = true;
    Require(run.Advance(1002, f) == Action::Engage, "native presence admission");
    f.victory = true;
    f.drained = false;
    Require(run.Advance(1003, f) == Action::Retire && run.completed == 1, "native victory retirement");
    Require(run.Advance(1004, f) == Action::None && run.phase == Phase::Draining, "release before native absence");
    // A stop arriving between native death and removal must cancel the next cycle.
    run.Stop(1005, "operator_stop");
    Require(run.reason == "operator_stop" && run.Active(), "stop while draining");
    f.drained = true;
    run.Advance(1006, f);
    Require(run.phase == Phase::Stopped && !run.Active() && run.attempted == 1, "fully drained stop");
    Require(run.Advance(9000, {}) == Action::None, "terminal restarted itself");
    run.Start(10000, 1800);
    Require(run.attempted == 0 && run.completed == 0 && run.reason.empty(), "explicit restart resets run");
    Require(run.Advance(10000, {}) == Action::Spawn, "explicit restart allocation");
}

void Limits()
{
    Run run;
    run.Start(1000, 10);
    Require(run.Advance(11000, {}) == Action::Retire && run.reason == "duration_expired", "duration boundary");
    run.Advance(11001, {});
    Require(run.phase == Phase::Completed && run.failed == 0, "normal expiry");
    run.Start(20000, 1800);
    run.Advance(20000, {});
    Require(run.Advance(49999, {}) == Action::None, "early watchdog");
    Require(run.Advance(50000, {}) == Action::Retire && run.failed == 1, "no progress watchdog");
    run.Advance(50001, {});
    Require(run.phase == Phase::Failed && run.reason == "encounter_stalled", "failed differs from completion");
    run.Start(60000, 1800);
    run.Advance(60000, {});
    Facts f;
    f.progress = true;
    run.Advance(119999, f);
    Require(run.Advance(120000, f) == Action::Retire, "absolute encounter ceiling despite progress");
    f.drained = false;
    run.Advance(130000, f);
    Require(run.phase == Phase::Failed && run.reason == "cleanup_stalled" && run.failed == 1, "bounded cleanup");
}

void CapAndGap()
{
    Run run;
    uint64_t now = 1000;
    run.Start(now, 1800);
    for (uint32_t i = 0; i < EncounterCap; ++i) {
        Require(run.Advance(now, {}) == Action::Spawn, "bounded cycle allocation");
        Facts f;
        f.ready = true;
        Require(run.Advance(++now, f) == Action::Engage, "cycle admission");
        f.victory = true;
        Require(run.Advance(++now, f) == Action::Retire, "cycle retirement");
        run.Advance(++now, {});
        if (i + 1 < EncounterCap) {
            Require(run.Advance(now + 4999, {}) == Action::None, "five second viewing gap");
        }
        now += 5000;
    }
    Require(run.Advance(now, {}) == Action::Retire && run.reason == "encounter_cap", "allocation cap");
    run.Advance(++now, {});
    Require(run.completed == 100 && run.attempted == 100 && run.failed == 0 &&
        run.phase == Phase::Completed, "cumulative encounter counters");
}

void FailuresAndTeardown()
{
    for (const auto *why : {"actor_identity_lost", "target_identity_lost", "foreign_custody", "foreign_interference"}) {
        Run run;
        run.Start(1000, 1800);
        run.Advance(1000, {});
        Facts f;
        f.failure = why;
        f.drained = false;
        Require(run.Advance(1001, f) == Action::Retire, "native seam failure retirement");
        Require(run.reason == why && run.failed == 1, "native failure retained");
        run.Stop(1002, "operator_stop");
        Require(run.reason == why, "stop overwrote failure");
        f.drained = true;
        run.Advance(1003, f);
        Require(run.phase == Phase::Failed && run.Advance(99999, {}) == Action::None, "failure churn");
    }
    Run run;
    run.Start(1000, 1800);
    run.Advance(1000, {});
    Require(run.Stop(1001, "zone_teardown") == Action::Retire, "teardown retirement");
    run.Advance(1002, {});
    Require(run.phase == Phase::Stopped && run.reason == "zone_teardown", "teardown terminal");
}

void NativeCustody()
{
    Custody custody;
    custody.actor = {10, 100}; custody.target = {20, 200};
    custody.epoch = 7; custody.generation = 3;
    NativeView view{7, 3, true, custody.actor, custody.target};
    Require(custody.CanCombat(custody.actor, custody.target) && custody.CanCombat(custody.target, custody.actor),
        "owned native retaliation");
    const NativeIdentity foreign{30, 300}, reused_actor{10, 101}, reused_target{20, 201};
    Require(custody.CanCombat(foreign, reused_actor) && custody.CanCombat(reused_actor, reused_target),
        "unrelated replacement identities restricted");
    for (const auto &pair : {std::pair{foreign, custody.target}, std::pair{custody.actor, foreign},
         std::pair{foreign, custody.actor}, std::pair{custody.target, foreign}, std::pair{NativeIdentity{}, custody.actor}}) {
        Require(!custody.AdmitCombat(pair.first, pair.second) && custody.interference, "foreign native interaction admitted");
    }
    const auto present = custody.PlanRetirement(view);
    Require(present.actor && present.target && !present.drained, "owned retirement plan");
    view.actor = reused_actor; view.target = reused_target; view.generation = 4;
    const auto replacement = custody.PlanRetirement(view);
    Require(!replacement.actor && !replacement.target && replacement.drained, "ID reuse inherited cleanup authority");
    view = {7, 3, true, {}, {}};
    Require(!custody.PlanRetirement(view).drained, "native absence bypassed Rust custody drain");
    view.alive = false;
    Require(custody.PlanRetirement(view).drained, "fully released custody");
    // Partial setup: target registered, actor creation refused by unrelated runtime.
    Custody partial;
    partial.target = {20, 200};
    const auto cleanup = partial.PlanRetirement({9, 9, true, foreign, partial.target});
    Require(!cleanup.actor && cleanup.target && !cleanup.drained, "partial setup retired foreign actor");
    Require(partial.PlanRetirement({9, 9, true, foreign, {}}).drained, "partial setup target release");

    custody.interference = false;
    view = {7, 3, true, custody.actor, custody.target};
    custody.ObserveCounters(view, {3, 2, 6});
    custody.ObserveCounters(view, {3, 2, 6});
    Require(custody.total.swings == 3 && custody.total.hits == 2 && custody.total.damage == 6, "duplicate native samples counted");
    custody.ObserveCounters({7, 4, true, reused_actor, reused_target}, {99, 99, 999});
    Require(custody.total.damage == 6, "foreign runtime counters admitted");
    custody.generation = 4; custody.last = {}; // explicit next owned encounter
    view.generation = 4;
    custody.ObserveCounters(view, {2, 1, 4});
    Require(custody.total.swings == 5 && custody.total.hits == 3 && custody.total.damage == 10,
        "native cumulative counters across incarnations");
    custody.ObserveCounters(view, {1, 1, 1});
    Require(custody.interference && custody.total.damage == 10, "regression produced unsigned counter damage");
}

void NativeKillOrdering()
{
    Custody custody;
    custody.actor = {10, 100}; custody.target = {20, 200};
    custody.epoch = 7; custody.generation = 3;
    NativeView view{7, 3, true, custody.actor, custody.target};
    Run run;
    run.Start(1000, 1800);
    run.Advance(1000, {});
    Facts f;
    f.ready = true;
    run.Advance(1001, f);
    // Actual production order: Attack -> native Death callback -> SwingHit,
    // then demo observes the dead/depop rat while Rust is still Engaging.
    view.target_kill_pending = true;
    f.progress = true;
    f.drained = false;
    f.failure = custody.TargetLost(view, false, false) ? "target_identity_lost" : nullptr;
    Require(run.Advance(1002, f) == Action::None && run.phase == Phase::Fighting && run.completed == 0,
        "native death latch either failed early or manufactured victory");
    // Next serialized adapter tick consumes the correlated latch as TargetKilled.
    view.target = {}; view.target_kill_pending = false;
    f.victory = true;
    f.failure = custody.TargetLost(view, false, true) ? "target_identity_lost" : nullptr;
    Require(run.Advance(1003, f) == Action::Retire && run.completed == 1 && run.failed == 0,
        "delayed truthful native kill was not completed");
    Require(custody.TargetLost(view, false, false), "mere external depop treated as pending kill");
    view.target_kill_pending = true; ++view.generation;
    Require(custody.TargetLost(view, false, false), "foreign runtime pending kill inherited authority");
}

void SingleClientAbility()
{
    // Single Kick/Bash and single ranged packets share OPCombatAbility. The
    // production seam validates the packet target before passive permission.
    Custody custody;
    custody.actor = {10, 100}; custody.target = {20, 200};
    Require(!custody.AdmitClientAbility(99, 10, {}, custody.actor) && !custody.interference,
        "invalid ability packet aborted demo");
    Require(!custody.AdmitClientAbility(10, 10, {}, custody.actor) && custody.interference,
        "single client ability bypassed interference with autoattack off");
    custody.interference = false;
    Require(!custody.AdmitClientAbility(20, 20, {}, custody.target) && custody.interference,
        "single ranged ability bypassed target guard");
    custody.interference = false;
    Require(custody.AdmitClientAbility(30, 30, {}, {30, 300}) && !custody.interference,
        "ordinary NPC ability blocked");
    Require(custody.AdmitClientAbility(40, 40, {}, {}) && !custody.interference,
        "ordinary client ability blocked");
}
}

int main()
{
    try {
        Commands(); EncountersAndStop(); Limits(); CapAndGap(); FailuresAndTeardown(); NativeCustody(); NativeKillOrdering(); SingleClientAbility();
        std::cout << "actor demo controls: 8 groups passed\n";
        return EXIT_SUCCESS;
    } catch (const std::exception &error) {
        std::cerr << "actor demo controls: " << error.what() << '\n';
        return EXIT_FAILURE;
    }
}

#include "../zone/actor_demo_state.h"
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
         "start s 1 9", "start s 1 1801", "start s 1 10 extra", "stop s 1 10", "start s 1 10x", "unknown s 1"}) {
        Require(!ParseCommand(text, c), "malformed command accepted");
    }
    Require(!ParseCommand(std::string(129, 'x'), c), "oversized command");
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
}

int main()
{
    try {
        Commands(); EncountersAndStop(); Limits(); CapAndGap(); FailuresAndTeardown();
        std::cout << "actor demo controls: 5 groups passed\n";
        return EXIT_SUCCESS;
    } catch (const std::exception &error) {
        std::cerr << "actor demo controls: " << error.what() << '\n';
        return EXIT_FAILURE;
    }
}

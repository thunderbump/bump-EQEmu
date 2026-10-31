#include "actor-bridge/src/lib.rs.h"
#include "../zone/actor_demo_native.h"
#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <stdexcept>

using namespace eqemu_proof;
namespace {
void Require(bool condition, const char *why) { if (!condition) { throw std::runtime_error(why); } }

// Real CXX/Rust runtime plus the production custody policy. Native results are
// explicit fixture facts here; this is not a zone/NPC/client qualification.
void PersistentBridge() {
    auto runtime = construct(501);
    auto send = [&](Kind kind) {
        const auto s = runtime->inspect();
        rust::Vec<Input> inputs;
        inputs.push_back({kind, s.epoch, s.generation, 0, 0, 0, 0});
        return runtime->advance(s.sequence + 1, s.tick + 1, std::move(inputs));
    };
    auto fact = [&](Kind kind, uint32_t damage = 0) {
        const auto s = runtime->inspect();
        return runtime->combat(s.sequence + 1, s.tick + 1,
            {s.epoch, s.generation, s.goal, s.operation, s.target, kind, damage});
    };
    Require(send(Kind::CreateMeleeBody).event == Event::BodyRequested, "bridge body request");
    Require(send(Kind::BodyPresent).event == Event::BodyPresent, "bridge native body registration");
    const auto initial = runtime->inspect();
    ActorDemo::Custody custody;
    custody.public_hunt = true; custody.actor = {10, 100};
    custody.epoch = initial.epoch; custody.generation = initial.generation;
    uint64_t goals = 0;
    for (unsigned cycle = 0; cycle < 4; ++cycle) {
        Require(send(Kind::StartPublicHunt).status == Status::Ok, "persistent goal start");
        auto s = runtime->inspect();
        Require(s.public_hunt && s.goal > goals && s.epoch == initial.epoch && s.generation == initial.generation,
            "goal replaced actor body identity");
        goals = s.goal; custody.goal = s.goal; custody.last = {};
        custody.target = {uint16_t(20 + cycle), 200 + cycle};
        custody.combat_enabled = true;
        custody.contested = false;
        Candidate prey{};
        prey.incarnation = custody.target.incarnation;
        prey.x = 3000; prey.present = prey.alive = prey.targetable = true;
        prey.type_id = cycle % 2 ? 2104 : 2011; prey.level = 1;
        Observation o{};
        o.epoch = s.epoch; o.generation = s.generation; o.goal = s.goal; o.candidates.push_back(prey);
        Require(runtime->observe(s.sequence + 1, s.tick + 1, std::move(o)).event == Event::AttackRequested,
            "bridge real policy target selection");
        Require(fact(Kind::AttackAdmitted).event == Event::AttackAdmitted, "native attack admission");
        Require(fact(Kind::SwingHit, 6).event == Event::SwingHit, "first native fixture hit");
        if (cycle == 3) {
            Require(custody.AdmitCombat({}, custody.target) && custody.contested, "public player damage blocked");
            Require(!custody.CanCombat(custody.actor, custody.target), "post-contention actor swing admitted");
            Require(fact(Kind::TargetLost).event == Event::TargetLost && runtime->inspect().approach == Approach::Lost,
                "player death credited actor victory");
        } else {
            Require(fact(Kind::SwingHit, 5).event == Event::SwingHit, "second native fixture hit");
            Require(runtime->inspect().approach == Approach::Engaging, "HP fact manufactured kill terminal");
            Require(fact(Kind::TargetKilled).event == Event::TargetKilled, "delayed native death terminal");
        }
        s = runtime->inspect();
        ActorDemo::NativeView view{s.epoch, s.generation, s.alive, custody.actor, custody.target, false, s.goal};
        custody.ObserveCounters(view, {s.swings, s.hits, s.damage});
        custody.combat_enabled = false;
        Require(send(Kind::ReleasePublicHunt).status == Status::Ok, "goal release");
        s = runtime->inspect();
        Require(s.alive && s.body == Body::Present && s.approach == Approach::Disabled && s.target == 0 &&
            s.epoch == initial.epoch && s.generation == initial.generation, "release lost body custody");
    }
    Require(custody.total.damage == 39 && custody.total.swings == 7, "native per-goal cumulative counters");
    Require(send(Kind::Retire).event == Event::RemovalRequested, "final actor-only retirement");
    auto s = runtime->inspect();
    Require(!custody.PlanRetirement({s.epoch, s.generation, s.alive, custody.actor, custody.target}).target,
        "public target scheduled for depop");
    Require(send(Kind::BodyAbsent).event == Event::BodyAbsent, "native actor removal acknowledgement");
    s = runtime->inspect();
    Require(custody.PlanRetirement({s.epoch, s.generation, s.alive, {}, custody.target}).drained,
        "surviving public target prevented actor drain");
}
}
int main() {
    try {
        PersistentBridge();
        std::cout << "public hunt CXX/Rust bridge: 4 persistent goals passed\n";
        return EXIT_SUCCESS;
    } catch (const std::exception &error) {
        std::cerr << "public hunt CXX/Rust bridge: " << error.what() << '\n';
        return EXIT_FAILURE;
    }
}

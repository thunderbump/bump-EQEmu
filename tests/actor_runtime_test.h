#pragma once

#ifdef EQEMU_RUST_BOOTSTRAP_BRIDGE
#include "actor-bridge/src/lib.rs.h"
#include "cppunit/cpptest.h"
#include <utility>

// A real CXX round trip, not a marker/scalar test. Values below are a fake
// native adapter trace: no NPC bodies, position or gameplay claims.
class ActorRuntimeTest : public Test::Suite {
public:
	ActorRuntimeTest() {
		TEST_ADD(ActorRuntimeTest::RoundTrip);
		TEST_ADD(ActorRuntimeTest::BodyFacts);
		TEST_ADD(ActorRuntimeTest::PatrolLoop);
		TEST_ADD(ActorRuntimeTest::ApproachOnce);
        TEST_ADD(ActorRuntimeTest::MeleeOnce);
	}

private:
    void MeleeOnce()
    {
        using namespace eqemu_proof;
        auto runtime = construct(90);
        auto send = [&](Kind kind) {
            const auto s = runtime->inspect();
            rust::Vec<Input> inputs;
            inputs.push_back({kind, s.epoch, s.generation, s.operation, 0, 0, 0});
            return runtime->advance(s.sequence + 1, s.tick + 1, std::move(inputs));
        };
        auto fact = [&](Kind kind, uint32_t damage, bool stale) {
            const auto s = runtime->inspect();
            CombatFact f{};
            f.epoch = s.epoch;
            f.generation = s.generation;
            f.goal = s.goal + (stale ? 1 : 0);
            f.operation = s.operation;
            f.target = s.target;
            f.kind = kind;
            f.damage = damage;
            return runtime->combat(s.sequence + 1, s.tick + 1, f);
        };
        TEST_ASSERT(send(Kind::CreateMeleeBody).event == Event::BodyRequested);
        TEST_ASSERT(send(Kind::BodyPresent).event == Event::BodyPresent);
        TEST_ASSERT(runtime->inspect().combat_body);
        TEST_ASSERT(send(Kind::StartMelee).status == Status::Ok);
        {
            const auto s = runtime->inspect();
            Observation o{};
            o.epoch = s.epoch;
            o.generation = s.generation;
            o.goal = s.goal;
            Candidate city{}, rat{};
            city.incarnation = 1;
            city.type_id = 2030;
            city.level = 1;
            city.present = city.alive = city.targetable = true;
            rat = city;
            rat.incarnation = 2;
            rat.type_id = 2011;
            rat.x = 3000;
            o.candidates.push_back(city);
            o.candidates.push_back(rat);
            TEST_ASSERT(runtime->observe(s.sequence + 1, s.tick + 1, std::move(o)).event == Event::AttackRequested);
        }
        TEST_ASSERT(runtime->inspect().target == 2);
        {
            const auto s = runtime->inspect();
            CombatFact unknown{};
            unknown.kind = static_cast<Kind>(255);
            TEST_ASSERT(runtime->combat(s.sequence + 1, s.tick + 1, unknown).status == Status::InvalidInput);
            TEST_ASSERT(runtime->inspect().sequence == s.sequence);
        }
        TEST_ASSERT(fact(Kind::AttackAdmitted, 0, true).status == Status::InvalidResult);
        TEST_ASSERT(fact(Kind::AttackAdmitted, 0, false).event == Event::AttackAdmitted);
        TEST_ASSERT(fact(Kind::SwingMiss, 0, false).event == Event::SwingMiss);
        TEST_ASSERT(fact(Kind::TargetKilled, 0, false).status == Status::InvalidResult);
        TEST_ASSERT(fact(Kind::SwingHit, 11, false).event == Event::SwingHit);
        TEST_ASSERT(send(Kind::Pause).event == Event::StopRequested);
        TEST_ASSERT(fact(Kind::TargetKilled, 0, false).event == Event::TargetKilled);
        const auto won = runtime->inspect();
        TEST_ASSERT(won.approach == Approach::Victory && won.damage == 11 && won.hits == 1 && won.swings == 2);
        TEST_ASSERT(send(Kind::Resume).status == Status::Busy);
        TEST_ASSERT(send(Kind::Retire).status == Status::Ok);
        const auto s = runtime->inspect();
        TEST_ASSERT(runtime->advance(s.sequence + 1, s.tick + 1, {}).event == Event::RemovalRequested);
        TEST_ASSERT(send(Kind::BodyAbsent).event == Event::BodyAbsent);
        TEST_ASSERT(send(Kind::Shutdown).event == Event::Shutdown);
        TEST_ASSERT(runtime->inspect().last_terminal == Event::TargetKilled);
    }

	void ApproachOnce()
	{
		using namespace eqemu_proof;
		auto runtime = construct(89);
		auto send = [&](Kind kind) {
			const auto s = runtime->inspect();
			rust::Vec<Input> inputs;
			inputs.push_back({kind, s.epoch, s.generation, s.operation, 0, 0, 0});
			return runtime->advance(s.sequence + 1, s.tick + 1, std::move(inputs));
		};
		auto observe = [&](bool present, int32_t actor_x) {
			const auto s = runtime->inspect();
			Observation facts{};
			facts.epoch = s.epoch;
			facts.generation = s.generation;
			facts.goal = s.goal;
			facts.x = actor_x;
			Candidate target{};
			target.incarnation = 17;
			target.x = 20000;
			target.present = present;
			target.alive = target.targetable = true;
			facts.candidates.push_back(target);
			return runtime->observe(s.sequence + 1, s.tick + 1, std::move(facts));
		};
		TEST_ASSERT(send(Kind::CreateBody).event == Event::BodyRequested);
		TEST_ASSERT(send(Kind::BodyPresent).event == Event::BodyPresent);
		TEST_ASSERT(send(Kind::StartApproach).status == Status::Ok);
		TEST_ASSERT(observe(false, 0).event == Event::None);
		const auto request = observe(true, 0);
		TEST_ASSERT(request.event == Event::MoveRequested && request.x == 18);
		TEST_ASSERT(runtime->inspect().target == 17);
		TEST_ASSERT(send(Kind::Admitted).event == Event::Admitted);
		TEST_ASSERT(observe(false, 1000).event == Event::StopRequested);
		TEST_ASSERT(send(Kind::Completed).event == Event::Completed);
		TEST_ASSERT(runtime->inspect().approach == Approach::Paused);
		TEST_ASSERT(observe(true, 1000).event == Event::None);
		const auto goal = runtime->inspect().goal;
		TEST_ASSERT(send(Kind::Resume).status == Status::Ok);
		TEST_ASSERT(runtime->inspect().goal > goal);
		TEST_ASSERT(observe(true, 1000).operation > request.operation);
		TEST_ASSERT(send(Kind::Admitted).event == Event::Admitted);
		TEST_ASSERT(observe(true, 18000).event == Event::None);
		TEST_ASSERT(send(Kind::Completed).event == Event::Completed);
		TEST_ASSERT(runtime->inspect().approach == Approach::Reached);
		TEST_ASSERT(observe(false, 18000).event == Event::None);
		TEST_ASSERT(send(Kind::Move).status == Status::Busy);
	}

	void PatrolLoop()
	{
		using namespace eqemu_proof;
		auto runtime = construct(78);
		auto send = [&](Kind kind) {
			const auto s = runtime->inspect();
			rust::Vec<Input> inputs;
			inputs.push_back({kind, s.epoch, s.generation, s.operation, 40, 0, 0});
			return runtime->advance(s.sequence + 1, s.tick + 1, std::move(inputs));
		};
		auto idle = [&]() {
			const auto s = runtime->inspect();
			return runtime->advance(s.sequence + 1, s.tick + 1, {});
		};
		TEST_ASSERT(send(Kind::CreateBody).event == Event::BodyRequested);
		// A=40, B=0. Values travel through the actual owned-value bridge.
		{
			const auto s = runtime->inspect();
			rust::Vec<Input> inputs;
			inputs.push_back({Kind::StartPatrol, s.epoch, s.generation, 0, 0, 0, 0});
			TEST_ASSERT(runtime->advance(s.sequence + 1, s.tick + 1, std::move(inputs)).status == Status::Ok);
		}
		TEST_ASSERT(send(Kind::BodyPresent).event == Event::BodyPresent);
		for (uint64_t op = 1; op <= 2; ++op) {
			const auto request = idle();
			TEST_ASSERT(request.event == Event::MoveRequested && request.operation == op);
			TEST_ASSERT(request.x == (op == 1 ? 0 : 40));
			TEST_ASSERT(send(Kind::Admitted).event == Event::Admitted);
			TEST_ASSERT(send(Kind::Completed).event == Event::Completed);
			TEST_ASSERT(runtime->inspect().last_operation == op);
		}
		TEST_ASSERT(send(Kind::Pause).status == Status::Ok);
		TEST_ASSERT(idle().event == Event::None);
		TEST_ASSERT(runtime->inspect().patrol == Patrol::Paused);
		TEST_ASSERT(send(Kind::Resume).status == Status::Ok);
		TEST_ASSERT(idle().operation == 3);
		TEST_ASSERT(send(Kind::Rejected).event == Event::Rejected);
		TEST_ASSERT(idle().event == Event::None);
		TEST_ASSERT(send(Kind::Retire).status == Status::Ok);
		TEST_ASSERT(idle().event == Event::RemovalRequested);
		TEST_ASSERT(send(Kind::BodyAbsent).event == Event::BodyAbsent);
		TEST_ASSERT(send(Kind::Shutdown).event == Event::Shutdown);
	}

	void BodyFacts()
	{
		using namespace eqemu_proof;
		auto runtime = construct(32);
		auto send = [&](Kind kind, std::uint64_t operation = 0) {
			const auto state = runtime->inspect();
			rust::Vec<Input> inputs;
			inputs.push_back({kind, state.epoch, state.generation, operation, 0, 0, 0});
			return runtime->advance(state.sequence + 1, state.tick + 1, std::move(inputs));
		};
		TEST_ASSERT(send(Kind::CreateBody).event == Event::BodyRequested);
		TEST_ASSERT(runtime->inspect().body == Body::Requested);
		TEST_ASSERT(send(Kind::Retire).status == Status::Busy);
		TEST_ASSERT(send(Kind::BodyPresent).event == Event::BodyPresent);
		TEST_ASSERT(send(Kind::Move, 1).event == Event::MoveRequested);
		TEST_ASSERT(send(Kind::Completed, 1).status == Status::InvalidResult);
		TEST_ASSERT(send(Kind::Admitted, 2).status == Status::InvalidResult);
		TEST_ASSERT(send(Kind::Admitted, 1).event == Event::Admitted);
		TEST_ASSERT(send(Kind::BodyAbsent).status == Status::Busy);
		TEST_ASSERT(send(Kind::Stop, 1).event == Event::StopRequested);
		// Preserve the merged truthful completion-after-stop rule for bodies too.
		TEST_ASSERT(send(Kind::Completed, 1).event == Event::Completed);
		TEST_ASSERT(send(Kind::Interrupted, 1).status == Status::InvalidResult);
		TEST_ASSERT(runtime->inspect().last_terminal == Event::Completed);
		TEST_ASSERT(runtime->inspect().last_operation == 1);
		TEST_ASSERT(send(Kind::Retire).event == Event::RemovalRequested);
		TEST_ASSERT(runtime->inspect().alive && runtime->inspect().body == Body::Removing);
		TEST_ASSERT(send(Kind::Shutdown).status == Status::Busy);
		TEST_ASSERT(send(Kind::BodyAbsent).event == Event::BodyAbsent);
		TEST_ASSERT(!runtime->inspect().alive);
		TEST_ASSERT(send(Kind::CreateBody).event == Event::BodyRequested);
		TEST_ASSERT(send(Kind::BodyRejected).event == Event::BodyRejected);
		TEST_ASSERT(send(Kind::Shutdown).event == Event::Shutdown);
	}

	void RoundTrip()
	{
		using namespace eqemu_proof;
		auto runtime = construct(31);
		std::uint64_t sequence = 0;
		auto send = [&](Kind kind, std::uint64_t operation) {
			auto state = runtime->inspect();
			rust::Vec<Input> inputs;
			inputs.push_back({kind, state.epoch, state.generation, operation, 10, 20, 30});
			++sequence;
			return runtime->advance(sequence, sequence, std::move(inputs));
		};
		TEST_ASSERT(send(Kind::Create, 0).event == Event::Created);
		auto requested = send(Kind::Move, 1);
		TEST_ASSERT(requested.status == Status::Ok);
		TEST_ASSERT(requested.event == Event::MoveRequested);
		TEST_ASSERT(requested.operation == 1 && requested.x == 10);
		TEST_ASSERT(runtime->inspect().phase == Phase::Requested);
		TEST_ASSERT(send(Kind::Completed, 1).status == Status::InvalidResult);
		TEST_ASSERT(send(Kind::Admitted, 1).event == Event::Admitted);
		TEST_ASSERT(send(Kind::Completed, 1).event == Event::Completed);
		TEST_ASSERT(send(Kind::Move, 2).event == Event::MoveRequested);
		TEST_ASSERT(send(Kind::Admitted, 2).event == Event::Admitted);
		TEST_ASSERT(send(Kind::Stop, 2).event == Event::StopRequested);
		TEST_ASSERT(send(Kind::Interrupted, 2).event == Event::Interrupted);
		TEST_ASSERT(send(Kind::Move, 3).event == Event::MoveRequested);
		TEST_ASSERT(send(Kind::Admitted, 3).event == Event::Admitted);
		// Native has queued completion before it receives the stop request.
		auto admitted = runtime->inspect();
		rust::Vec<Input> completed;
		completed.push_back({Kind::Completed, admitted.epoch, admitted.generation, 3, 0, 0, 0});
		TEST_ASSERT(send(Kind::Stop, 3).event == Event::StopRequested);
		TEST_ASSERT(runtime->inspect().phase == Phase::Stopping);
		TEST_ASSERT(send(Kind::Retire, 0).status == Status::Busy);
		++sequence;
		auto terminal = runtime->advance(sequence, sequence, std::move(completed));
		TEST_ASSERT(terminal.status == Status::Ok && terminal.event == Event::Completed);
		TEST_ASSERT(terminal.operation == 3);
		TEST_ASSERT(runtime->inspect().phase == Phase::Idle);
		TEST_ASSERT(runtime->inspect().operation == 0);
		TEST_ASSERT(runtime->inspect().last_operation == 3);
		TEST_ASSERT(send(Kind::Interrupted, 3).status == Status::InvalidResult);
		TEST_ASSERT(send(Kind::Completed, 3).status == Status::InvalidResult);
		TEST_ASSERT(send(Kind::Retire, 0).event == Event::Retired);
		TEST_ASSERT(!runtime->inspect().alive);
		TEST_ASSERT(send(Kind::Create, 0).event == Event::Created);
		auto state = runtime->inspect();
		rust::Vec<Input> stale;
		stale.push_back({Kind::Completed, 31, state.generation - 1, 2, 0, 0, 0});
		++sequence;
		TEST_ASSERT(runtime->advance(sequence, sequence, std::move(stale)).status == Status::Stale);
		rust::Vec<Input> excess;
		excess.push_back({Kind::Retire, 31, state.generation, 0, 0, 0, 0});
		excess.push_back({Kind::Shutdown, 31, state.generation, 0, 0, 0, 0});
		++sequence;
		TEST_ASSERT(runtime->advance(sequence, sequence, std::move(excess)).status == Status::ExcessInput);
		TEST_ASSERT(runtime->inspect().alive);
		TEST_ASSERT(runtime->inspect().last_terminal == Event::Completed);
		TEST_ASSERT(send(Kind::Retire, 0).event == Event::Retired);
		TEST_ASSERT(send(Kind::Shutdown, 0).event == Event::Shutdown);
		TEST_ASSERT(runtime->inspect().closed);
		// rust::Box destructor releases Rust allocation through generated CXX.
	}
};
#endif

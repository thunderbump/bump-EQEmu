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
	}

private:
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

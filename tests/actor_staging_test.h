#pragma once

#include "cppunit/cpptest.h"

int RunActorDemoControls();
int RunActorHuntControls();
#ifdef EQEMU_RUST_BOOTSTRAP_BRIDGE
int RunActorHuntBridgeControls();
int RunActorPopulationBridgeControls();
#endif

// Reuse the standalone fixtures in the real utility runner and its verdict.
class ActorStagingTest : public Test::Suite {
public:
	ActorStagingTest()
	{
		TEST_ADD(ActorStagingTest::DemoControls);
		TEST_ADD(ActorStagingTest::HuntControls);
#ifdef EQEMU_RUST_BOOTSTRAP_BRIDGE
		TEST_ADD(ActorStagingTest::HuntBridgeControls);
		TEST_ADD(ActorStagingTest::PopulationBridgeControls);
#endif
	}

private:
	void DemoControls() { TEST_ASSERT(RunActorDemoControls() == 0); }
	void HuntControls() { TEST_ASSERT(RunActorHuntControls() == 0); }
#ifdef EQEMU_RUST_BOOTSTRAP_BRIDGE
	void HuntBridgeControls() { TEST_ASSERT(RunActorHuntBridgeControls() == 0); }
	void PopulationBridgeControls() { TEST_ASSERT(RunActorPopulationBridgeControls() == 0); }
#endif
};

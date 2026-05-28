/*	EQEmu: EQEmulator

	Copyright (C) 2001-2026 EQEmu Development Team

	This program is free software; you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation; either version 3 of the License, or
	(at your option) any later version.
*/
#pragma once

#include "common/bot_slow_target.h"
#include "cppunit/cpptest.h"

#include <algorithm>
#include <vector>

class BotSlowTargetTest : public Test::Suite {
public:
	BotSlowTargetTest()
	{
		TEST_ADD(BotSlowTargetTest::CurrentTargetSortsBeforeOtherThreats);
		TEST_ADD(BotSlowTargetTest::ThreatOrderingPrefersOwnerGroupRaidThenPetsThenProximity);
		TEST_ADD(BotSlowTargetTest::UnrelatedNearbyNpcIsNotAnEngagedHostileThreat);
	}

private:
	void CurrentTargetSortsBeforeOtherThreats()
	{
		using namespace EQ::BotSlowTarget;

		std::vector<Ordering> candidates{
			{false, ThreatPriority::OwnerGroupRaid, 1.0f, 0},
			{true, ThreatPriority::OwnerGroupRaid, 500.0f, 1},
		};

		std::sort(candidates.begin(), candidates.end(), CompareOrdering);

		TEST_ASSERT(candidates.front().current_target);
	}

	void ThreatOrderingPrefersOwnerGroupRaidThenPetsThenProximity()
	{
		using namespace EQ::BotSlowTarget;

		std::vector<Ordering> candidates{
			{false, ThreatPriority::GroupRaidPet, 4.0f, 0},
			{false, ThreatPriority::OwnerGroupRaid, 100.0f, 1},
			{false, ThreatPriority::GroupRaidPet, 1.0f, 2},
		};

		std::sort(candidates.begin(), candidates.end(), CompareOrdering);

		TEST_ASSERT(candidates[0].threat_priority == ThreatPriority::OwnerGroupRaid);
		TEST_ASSERT(candidates[1].threat_priority == ThreatPriority::GroupRaidPet);
		TEST_ASSERT(candidates[1].distance_squared == 1.0f);
		TEST_ASSERT(candidates[2].threat_priority == ThreatPriority::GroupRaidPet);
	}

	void UnrelatedNearbyNpcIsNotAnEngagedHostileThreat()
	{
		using namespace EQ::BotSlowTarget;

		const auto priority = GetThreatPriority(false, false);

		TEST_ASSERT(priority == ThreatPriority::None);
		TEST_ASSERT(!IsEngagedHostileThreat(priority));
	}
};

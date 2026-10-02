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
		TEST_ADD(BotSlowTargetTest::MaintenanceSelectionCastsOnCurrentTargetFirst);
		TEST_ADD(BotSlowTargetTest::MaintenanceSelectionCastsOnOtherEngagedHostile);
		TEST_ADD(BotSlowTargetTest::MaintenanceSelectionDoesNotCastWhenNoCandidateNeedsSlow);
		TEST_ADD(BotSlowTargetTest::MaintenanceSelectionSkipsMezzedCandidates);
		TEST_ADD(BotSlowTargetTest::MaintenanceSelectionRejectsAllInvalidCandidates);
		TEST_ADD(BotSlowTargetTest::MaintenanceSelectionHandlesOrdinaryMultiMobFightWithinScanBound);
		TEST_ADD(BotSlowTargetTest::AESlowDoesNotUseSingleTargetMaintenanceRouting);
	}

private:
	struct MaintenanceCandidate {
		int                         id = 0;
		bool                        needs_slow = false;
		bool                        mezzed = false;
		EQ::BotSlowTarget::Ordering order;
	};

	static MaintenanceCandidate SelectMaintenanceCandidate(
		std::vector<MaintenanceCandidate> candidates,
		bool spell_breaks_mez = true
	) {
		using namespace EQ::BotSlowTarget;

		std::sort(
			candidates.begin(),
			candidates.end(),
			[](const MaintenanceCandidate &left, const MaintenanceCandidate &right) {
				return CompareOrdering(left.order, right.order);
			}
		);

		return EQ::BotSlowTarget::SelectMaintenanceCandidate<MaintenanceCandidate>(
			candidates,
			spell_breaks_mez,
			[](const MaintenanceCandidate &candidate) { return candidate.mezzed; },
			[](const MaintenanceCandidate &candidate) { return candidate.needs_slow; }
		);
	}

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

	void MaintenanceSelectionCastsOnCurrentTargetFirst()
	{
		using namespace EQ::BotSlowTarget;

		const auto selected = SelectMaintenanceCandidate(
			{
				{2, true, false, {false, ThreatPriority::OwnerGroupRaid, 1.0f, 1}},
				{1, true, false, {true, ThreatPriority::OwnerGroupRaid, 500.0f, 0}},
			}
		);

		TEST_ASSERT(selected.id == 1);
	}

	void MaintenanceSelectionCastsOnOtherEngagedHostile()
	{
		using namespace EQ::BotSlowTarget;

		const auto selected = SelectMaintenanceCandidate(
			{
				{1, false, false, {true, ThreatPriority::OwnerGroupRaid, 1.0f, 0}},
				{2, true, false, {false, ThreatPriority::OwnerGroupRaid, 10.0f, 1}},
			}
		);

		TEST_ASSERT(selected.id == 2);
	}

	void MaintenanceSelectionDoesNotCastWhenNoCandidateNeedsSlow()
	{
		using namespace EQ::BotSlowTarget;

		const auto selected = SelectMaintenanceCandidate(
			{
				{1, false, false, {true, ThreatPriority::OwnerGroupRaid, 1.0f, 0}},
				{2, false, false, {false, ThreatPriority::OwnerGroupRaid, 2.0f, 1}},
			}
		);

		TEST_ASSERT(selected.id == 0);
	}

	void MaintenanceSelectionSkipsMezzedCandidates()
	{
		using namespace EQ::BotSlowTarget;

		const auto selected = SelectMaintenanceCandidate(
			{
				{1, true, true, {false, ThreatPriority::OwnerGroupRaid, 1.0f, 0}},
				{2, true, false, {false, ThreatPriority::OwnerGroupRaid, 2.0f, 1}},
			}
		);

		TEST_ASSERT(selected.id == 2);
	}

	void MaintenanceSelectionRejectsAllInvalidCandidates()
	{
		using namespace EQ::BotSlowTarget;

		struct Candidate {
			int  id = 0;
			bool mezzed = false;
			bool castable = false;
		};

		const std::vector<Candidate> candidates{
			{1, true, true},
			{2, false, false},
		};

		const auto selected = EQ::BotSlowTarget::SelectMaintenanceCandidate<Candidate>(
			candidates,
			true,
			[](const Candidate &candidate) { return candidate.mezzed; },
			[](const Candidate &candidate) { return candidate.castable; }
		);

		TEST_ASSERT(selected.id == 0);
	}

	void MaintenanceSelectionHandlesOrdinaryMultiMobFightWithinScanBound()
	{
		using namespace EQ::BotSlowTarget;

		std::vector<MaintenanceCandidate> candidates{
			{1, false, false, {true, ThreatPriority::OwnerGroupRaid, 1.0f, 0}},
		};

		for (std::size_t sequence = 1; sequence <= 48; ++sequence) {
			candidates.push_back(
				{
					static_cast<int>(sequence + 1),
					sequence == 24,
					false,
					{false, ThreatPriority::OwnerGroupRaid, static_cast<float>(sequence), sequence}
				}
			);
		}

		const auto selected = SelectMaintenanceCandidate(candidates);

		TEST_ASSERT(selected.id == 25);
	}

	void AESlowDoesNotUseSingleTargetMaintenanceRouting()
	{
		using namespace EQ::BotSlowTarget;

		TEST_ASSERT(UsesSingleTargetMaintenance(BotSpellTypes::Slow, false));
		TEST_ASSERT(!UsesSingleTargetMaintenance(BotSpellTypes::Slow, true));
		TEST_ASSERT(!UsesSingleTargetMaintenance(BotSpellTypes::AESlow, false));
	}
};

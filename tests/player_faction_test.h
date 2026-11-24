#pragma once

#include "common/faction.h"
#include "common/rulesys.h"
#include "cppunit/cpptest.h"

class PlayerFactionTest : public Test::Suite {
public:
	PlayerFactionTest()
	{
		TEST_ADD(PlayerFactionTest::SpecialFactions);
		TEST_ADD(PlayerFactionTest::Corrections);
		TEST_ADD(PlayerFactionTest::IdentityAndStanding);
	}

private:
	void SpecialFactions()
	{
		const FACTION_VALUE expected[] = {
			FACTION_INDIFFERENTLY, FACTION_DUBIOUSLY, FACTION_SCOWLS,
			FACTION_INDIFFERENTLY, FACTION_DUBIOUSLY, FACTION_SCOWLS,
			FACTION_INDIFFERENTLY, FACTION_DUBIOUSLY, FACTION_SCOWLS,
			FACTION_INDIFFERENTLY, FACTION_DUBIOUSLY, FACTION_SCOWLS
		};
		for (int32 i = 0; i < 12; ++i) {
			TEST_ASSERT(CalculatePlayerSpecialFaction(-2 - i) == expected[i]);
		}
		for (const int32 unknown : {-100, -14, -1, 0, 1}) {
			TEST_ASSERT(CalculatePlayerSpecialFaction(unknown) == FACTION_INDIFFERENTLY);
		}
	}

	void Corrections()
	{
		for (int value = FACTION_ALLY; value <= FACTION_SCOWLS; ++value) {
			const auto faction = static_cast<FACTION_VALUE>(value);
			TEST_ASSERT(AdjustPlayerFaction(faction, false, false) == faction);
			const auto merchant = value >= FACTION_THREATENINGLY ? FACTION_DUBIOUSLY : faction;
			TEST_ASSERT(AdjustPlayerFaction(faction, true, false) == merchant);
			TEST_ASSERT(AdjustPlayerFaction(faction, true, true) == FACTION_THREATENINGLY);
			TEST_ASSERT(AdjustPlayerFaction(faction, false, true) ==
				(faction == FACTION_SCOWLS ? FACTION_SCOWLS : FACTION_THREATENINGLY));
		}
	}

	void IdentityAndStanding()
	{
		// Race, class, deity and personal reputation all affect the same total.
		FactionMods modifiers{};
		modifiers.base = -300;
		modifiers.race_mod = -200;
		modifiers.class_mod = 150;
		modifiers.deity_mod = -400;
		for (const int32 standing : {-500, 0, 200, 1000, 2000}) {
			TEST_ASSERT(CalculateFaction(&modifiers, standing) ==
				CalculateFaction(nullptr, standing - 750));
		}
		// Pin the hostile boundary to the configured player threshold.
		TEST_ASSERT(CalculateFaction(nullptr, RuleI(Faction, ThreateninglyFactionMinimum) - 1) == FACTION_SCOWLS);
		TEST_ASSERT(CalculateFaction(nullptr, RuleI(Faction, ThreateninglyFactionMinimum)) == FACTION_THREATENINGLY);
	}
};

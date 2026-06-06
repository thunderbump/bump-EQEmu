/*	EQEmu: EQEmulator

	Copyright (C) 2001-2026 EQEmu Development Team

	This program is free software; you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation; either version 3 of the License, or
	(at your option) any later version.

	This program is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERMERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
	GNU General Public License for more details.

	You should have received a copy of the GNU General Public License
	along with this program. If not, see <http://www.gnu.org/licenses/>.
*/
#pragma once

#include "cppunit/cpptest.h"

#include "common/regular_heal_efficiency.h"
#include "common/rulesys.h"

class RegularHealEfficiencyTest : public Test::Suite {
public:
	RegularHealEfficiencyTest()
	{
		TEST_ADD(RegularHealEfficiencyTest::DefaultRuleDisablesEfficientRegularHeals);
		TEST_ADD(RegularHealEfficiencyTest::DisabledRuleReturnsPriorityFirstFallback);
		TEST_ADD(RegularHealEfficiencyTest::EnabledRuleChoosesSmallestSufficientOverheal);
		TEST_ADD(RegularHealEfficiencyTest::InsufficientSmallerHealsAreNotSelected);
		TEST_ADD(RegularHealEfficiencyTest::UnusableEstimatesAreNotEfficientAlternatives);
		TEST_ADD(RegularHealEfficiencyTest::NoSufficientEstimatedCandidateReturnsFallback);
		TEST_ADD(RegularHealEfficiencyTest::TieBreakersPreferManaThenPriority);
		TEST_ADD(RegularHealEfficiencyTest::EmptyCandidateListHasNoSelection);
	}

private:
	void DefaultRuleDisablesEfficientRegularHeals()
	{
		RuleManager::Instance()->ResetRules();

		const auto settings = RegularHealEfficiency::LoadSettingsFromRules();

		TEST_ASSERT(!settings.prefer_efficient_regular_heals);
	}

	void DisabledRuleReturnsPriorityFirstFallback()
	{
		const RegularHealEfficiency::Settings settings{
			.prefer_efficient_regular_heals = false
		};

		const auto result = RegularHealEfficiency::SelectRegularHealCandidate(
			settings,
			500,
			0,
			{
				{.spell_id = 101, .list_order = 1, .mana_cost = 60, .has_usable_estimated_heal = true, .estimated_heal = 500},
				{.spell_id = 102, .list_order = 0, .mana_cost = 120, .has_usable_estimated_heal = true, .estimated_heal = 900}
			}
		);

		TEST_ASSERT(result.found);
		TEST_ASSERT_EQUALS(result.spell_id, static_cast<uint16_t>(102));
		TEST_ASSERT_EQUALS(result.list_order, 0u);
		TEST_ASSERT(!result.selected_for_efficiency);
	}

	void EnabledRuleChoosesSmallestSufficientOverheal()
	{
		const RegularHealEfficiency::Settings settings{
			.prefer_efficient_regular_heals = true
		};

		const auto result = RegularHealEfficiency::SelectRegularHealCandidate(
			settings,
			500,
			0,
			{
				{.spell_id = 201, .list_order = 0, .mana_cost = 100, .has_usable_estimated_heal = true, .estimated_heal = 900},
				{.spell_id = 202, .list_order = 1, .mana_cost = 70, .has_usable_estimated_heal = true, .estimated_heal = 540},
				{.spell_id = 203, .list_order = 2, .mana_cost = 40, .has_usable_estimated_heal = true, .estimated_heal = 700}
			}
		);

		TEST_ASSERT(result.found);
		TEST_ASSERT_EQUALS(result.spell_id, static_cast<uint16_t>(202));
		TEST_ASSERT(result.selected_for_efficiency);
	}

	void InsufficientSmallerHealsAreNotSelected()
	{
		const RegularHealEfficiency::Settings settings{
			.prefer_efficient_regular_heals = true
		};

		const auto result = RegularHealEfficiency::SelectRegularHealCandidate(
			settings,
			500,
			50,
			{
				{.spell_id = 301, .list_order = 0, .mana_cost = 100, .has_usable_estimated_heal = true, .estimated_heal = 900},
				{.spell_id = 302, .list_order = 1, .mana_cost = 20, .has_usable_estimated_heal = true, .estimated_heal = 540},
				{.spell_id = 303, .list_order = 2, .mana_cost = 60, .has_usable_estimated_heal = true, .estimated_heal = 560}
			}
		);

		TEST_ASSERT(result.found);
		TEST_ASSERT_EQUALS(result.spell_id, static_cast<uint16_t>(303));
		TEST_ASSERT(result.selected_for_efficiency);
	}

	void UnusableEstimatesAreNotEfficientAlternatives()
	{
		const RegularHealEfficiency::Settings settings{
			.prefer_efficient_regular_heals = true
		};

		const auto result = RegularHealEfficiency::SelectRegularHealCandidate(
			settings,
			500,
			0,
			{
				{.spell_id = 401, .list_order = 0, .mana_cost = 100, .has_usable_estimated_heal = true, .estimated_heal = 900},
				{.spell_id = 402, .list_order = 1, .mana_cost = 10, .has_usable_estimated_heal = false, .estimated_heal = 520},
				{.spell_id = 403, .list_order = 2, .mana_cost = 10, .has_usable_estimated_heal = true, .estimated_heal = 0},
				{.spell_id = 404, .list_order = 3, .mana_cost = 10, .has_usable_estimated_heal = true, .estimated_heal = -50}
			}
		);

		TEST_ASSERT(result.found);
		TEST_ASSERT_EQUALS(result.spell_id, static_cast<uint16_t>(401));
		TEST_ASSERT(result.selected_for_efficiency);
	}

	void NoSufficientEstimatedCandidateReturnsFallback()
	{
		const RegularHealEfficiency::Settings settings{
			.prefer_efficient_regular_heals = true
		};

		const auto result = RegularHealEfficiency::SelectRegularHealCandidate(
			settings,
			500,
			50,
			{
				{.spell_id = 501, .list_order = 0, .mana_cost = 100, .has_usable_estimated_heal = false, .estimated_heal = 900},
				{.spell_id = 502, .list_order = 1, .mana_cost = 10, .has_usable_estimated_heal = true, .estimated_heal = 540}
			}
		);

		TEST_ASSERT(result.found);
		TEST_ASSERT_EQUALS(result.spell_id, static_cast<uint16_t>(501));
		TEST_ASSERT(!result.selected_for_efficiency);
	}

	void TieBreakersPreferManaThenPriority()
	{
		const RegularHealEfficiency::Settings settings{
			.prefer_efficient_regular_heals = true
		};

		const auto lower_mana = RegularHealEfficiency::SelectRegularHealCandidate(
			settings,
			500,
			0,
			{
				{.spell_id = 601, .list_order = 0, .mana_cost = 90, .has_usable_estimated_heal = true, .estimated_heal = 650},
				{.spell_id = 602, .list_order = 1, .mana_cost = 60, .has_usable_estimated_heal = true, .estimated_heal = 650}
			}
		);

		TEST_ASSERT_EQUALS(lower_mana.spell_id, static_cast<uint16_t>(602));

		const auto earlier_priority = RegularHealEfficiency::SelectRegularHealCandidate(
			settings,
			500,
			0,
			{
				{.spell_id = 603, .list_order = 2, .mana_cost = 60, .has_usable_estimated_heal = true, .estimated_heal = 650},
				{.spell_id = 604, .list_order = 1, .mana_cost = 60, .has_usable_estimated_heal = true, .estimated_heal = 650}
			}
		);

		TEST_ASSERT_EQUALS(earlier_priority.spell_id, static_cast<uint16_t>(604));
	}

	void EmptyCandidateListHasNoSelection()
	{
		const RegularHealEfficiency::Settings settings{
			.prefer_efficient_regular_heals = true
		};

		const auto result = RegularHealEfficiency::SelectRegularHealCandidate(settings, 500, 0, {});

		TEST_ASSERT(!result.found);
	}
};

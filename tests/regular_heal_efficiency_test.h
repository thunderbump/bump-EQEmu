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
#include "common/spdat.h"

#include <cstring>
#include <vector>

class RegularHealEfficiencyTest : public Test::Suite {
public:
	RegularHealEfficiencyTest()
	{
		TEST_ADD(RegularHealEfficiencyTest::DefaultRuleDisablesEfficientRegularHeals);
		TEST_ADD(RegularHealEfficiencyTest::OnlyNonRotationRegularHealsUseEfficientSelection);
		TEST_ADD(RegularHealEfficiencyTest::PetRegularHealsUseEfficientSelection);
		TEST_ADD(RegularHealEfficiencyTest::ExcludedHealTypesDoNotUseEfficientSelection);
		TEST_ADD(RegularHealEfficiencyTest::OrdinaryDirectHealProducesUsableAdjustedEstimate);
		TEST_ADD(RegularHealEfficiencyTest::NonPositiveDirectHealEstimateIsUnusable);
		TEST_ADD(RegularHealEfficiencyTest::UncertainDirectHealEstimateIsUnusable);
		TEST_ADD(RegularHealEfficiencyTest::EstimatorFedUnusableCandidateRemainsFallbackOnly);
		TEST_ADD(RegularHealEfficiencyTest::DisabledRuleReturnsPriorityFirstFromRuntimeCandidateList);
		TEST_ADD(RegularHealEfficiencyTest::EnabledRuleCanChooseLaterSmallerRuntimeCandidate);
		TEST_ADD(RegularHealEfficiencyTest::DisabledRuleReturnsPriorityFirstFallback);
		TEST_ADD(RegularHealEfficiencyTest::EnabledRuleChoosesSmallestSufficientOverheal);
		TEST_ADD(RegularHealEfficiencyTest::InsufficientSmallerHealsAreNotSelected);
		TEST_ADD(RegularHealEfficiencyTest::UnusableEstimatesAreNotEfficientAlternatives);
		TEST_ADD(RegularHealEfficiencyTest::NoSufficientEstimatedCandidateReturnsFallback);
		TEST_ADD(RegularHealEfficiencyTest::TieBreakersPreferManaThenPriority);
		TEST_ADD(RegularHealEfficiencyTest::EmptyCandidateListHasNoSelection);
	}

private:
	struct TestSpellGlobals {
		TestSpellGlobals()
		{
			previous_spells = spells;
			previous_records = SPDAT_RECORDS;
			test_spells.resize(16);
			for (auto &spell : test_spells) {
				std::strncpy(spell.player_1, "PLAYER_1", sizeof(spell.player_1) - 1);
			}
			spells = test_spells.data();
			SPDAT_RECORDS = static_cast<int32>(test_spells.size());
		}

		~TestSpellGlobals()
		{
			spells = previous_spells;
			SPDAT_RECORDS = previous_records;
		}

		void SetOrdinaryRegularHeal(uint16 spell_id, int base_value)
		{
			test_spells[spell_id].cast_time = MAX_FAST_HEAL_CASTING_TIME + 1;
			test_spells[spell_id].target_type = ST_Target;
			test_spells[spell_id].effect_id[0] = SpellEffect::CurrentHP;
			test_spells[spell_id].base_value[0] = base_value;
		}

		void SetTriggeredRegularHeal(uint16 spell_id, uint16 trigger_spell_id)
		{
			test_spells[spell_id].cast_time = MAX_FAST_HEAL_CASTING_TIME + 1;
			test_spells[spell_id].target_type = ST_Target;
			test_spells[spell_id].effect_id[0] = SpellEffect::TriggerOnCast;
			test_spells[spell_id].limit_value[0] = trigger_spell_id;
			SetOrdinaryRegularHeal(trigger_spell_id, 125);
		}

		std::vector<SPDat_Spell_Struct> test_spells;
		const SPDat_Spell_Struct *previous_spells = nullptr;
		int32 previous_records = 0;
	};

	void DefaultRuleDisablesEfficientRegularHeals()
	{
		RuleManager::Instance()->ResetRules();

		const auto settings = RegularHealEfficiency::LoadSettingsFromRules();

		TEST_ASSERT(!settings.prefer_efficient_regular_heals);
	}

	void OnlyNonRotationRegularHealsUseEfficientSelection()
	{
		const RegularHealEfficiency::Settings settings{
			.prefer_efficient_regular_heals = true
		};

		TEST_ASSERT(RegularHealEfficiency::ShouldUseEfficientSelection(
			settings,
			BotSpellTypes::RegularHeal,
			false
		));
		TEST_ASSERT(!RegularHealEfficiency::ShouldUseEfficientSelection(
			settings,
			BotSpellTypes::RegularHeal,
			true
		));
	}

	void PetRegularHealsUseEfficientSelection()
	{
		const RegularHealEfficiency::Settings settings{
			.prefer_efficient_regular_heals = true
		};

		TEST_ASSERT(RegularHealEfficiency::ShouldUseEfficientSelection(
			settings,
			BotSpellTypes::PetRegularHeals,
			false
		));
	}

	void ExcludedHealTypesDoNotUseEfficientSelection()
	{
		const RegularHealEfficiency::Settings enabled_settings{
			.prefer_efficient_regular_heals = true
		};
		const uint16_t excluded_spell_types[] = {
			BotSpellTypes::VeryFastHeals,
			BotSpellTypes::FastHeals,
			BotSpellTypes::CompleteHeal,
			BotSpellTypes::GroupCompleteHeals,
			BotSpellTypes::HoTHeals,
			BotSpellTypes::GroupHoTHeals,
			BotSpellTypes::GroupHeals,
			BotSpellTypes::PetFastHeals,
			BotSpellTypes::PetVeryFastHeals,
			BotSpellTypes::PetCompleteHeals,
			BotSpellTypes::PetHoTHeals
		};

		for (const auto spell_type : excluded_spell_types) {
			TEST_ASSERT(!RegularHealEfficiency::ShouldUseEfficientSelection(
				enabled_settings,
				spell_type,
				false
			));
		}
	}

	void OrdinaryDirectHealProducesUsableAdjustedEstimate()
	{
		TestSpellGlobals spell_globals;
		spell_globals.SetOrdinaryRegularHeal(2, 125);

		bool calculated_effect = false;
		bool adjusted_healing = false;
		uint16_t calculated_spell_id = 0;
		int calculated_effect_index = -1;
		uint16_t adjusted_spell_id = 0;
		int64_t adjusted_value = 0;

		const auto estimate = RegularHealEfficiency::EstimateRegularHealAmount(
			2,
			[&](uint16_t spell_id, int effect_index) {
				calculated_effect = true;
				calculated_spell_id = spell_id;
				calculated_effect_index = effect_index;
				return static_cast<int64_t>(450);
			},
			[&](uint16_t spell_id, int64_t value) {
				adjusted_healing = true;
				adjusted_spell_id = spell_id;
				adjusted_value = value;
				return static_cast<int64_t>(525);
			}
		);

		TEST_ASSERT(calculated_effect);
		TEST_ASSERT_EQUALS(calculated_spell_id, static_cast<uint16_t>(2));
		TEST_ASSERT_EQUALS(calculated_effect_index, 0);
		TEST_ASSERT(adjusted_healing);
		TEST_ASSERT_EQUALS(adjusted_spell_id, static_cast<uint16_t>(2));
		TEST_ASSERT_EQUALS(adjusted_value, static_cast<int64_t>(450));
		TEST_ASSERT(estimate.usable);
		TEST_ASSERT_EQUALS(estimate.amount, static_cast<int64_t>(525));
	}

	void NonPositiveDirectHealEstimateIsUnusable()
	{
		TestSpellGlobals spell_globals;
		spell_globals.SetOrdinaryRegularHeal(2, 125);
		spell_globals.SetOrdinaryRegularHeal(3, 125);

		const auto non_positive_formula = RegularHealEfficiency::EstimateRegularHealAmount(
			2,
			[](uint16_t, int) { return static_cast<int64_t>(0); },
			[](uint16_t, int64_t value) { return value; }
		);

		TEST_ASSERT(!non_positive_formula.usable);
		TEST_ASSERT_EQUALS(non_positive_formula.amount, static_cast<int64_t>(0));

		const auto non_positive_adjustment = RegularHealEfficiency::EstimateRegularHealAmount(
			3,
			[](uint16_t, int) { return static_cast<int64_t>(450); },
			[](uint16_t, int64_t) { return static_cast<int64_t>(0); }
		);

		TEST_ASSERT(!non_positive_adjustment.usable);
		TEST_ASSERT_EQUALS(non_positive_adjustment.amount, static_cast<int64_t>(0));
	}

	void UncertainDirectHealEstimateIsUnusable()
	{
		TestSpellGlobals spell_globals;
		spell_globals.SetOrdinaryRegularHeal(2, 125);
		spell_globals.test_spells[3].cast_time = MAX_FAST_HEAL_CASTING_TIME + 1;
		spell_globals.test_spells[3].target_type = ST_Target;
		spell_globals.SetTriggeredRegularHeal(4, 2);
		spell_globals.test_spells[5].cast_time = MAX_FAST_HEAL_CASTING_TIME + 1;
		spell_globals.test_spells[5].target_type = ST_Target;
		spell_globals.test_spells[5].effect_id[0] = SpellEffect::CurrentHPOnce;
		spell_globals.test_spells[5].base_value[0] = 125;

		const auto calculator = [](uint16_t, int) { return static_cast<int64_t>(450); };
		const auto adjuster = [](uint16_t, int64_t value) { return value; };

		const auto missing_current_hp = RegularHealEfficiency::EstimateRegularHealAmount(3, calculator, adjuster);
		TEST_ASSERT(!missing_current_hp.usable);

		const auto triggered_heal = RegularHealEfficiency::EstimateRegularHealAmount(4, calculator, adjuster);
		TEST_ASSERT(!triggered_heal.usable);

		const auto current_hp_once = RegularHealEfficiency::EstimateRegularHealAmount(5, calculator, adjuster);
		TEST_ASSERT(!current_hp_once.usable);
	}

	void EstimatorFedUnusableCandidateRemainsFallbackOnly()
	{
		TestSpellGlobals spell_globals;
		spell_globals.SetOrdinaryRegularHeal(2, 125);
		spell_globals.SetOrdinaryRegularHeal(3, 125);

		const auto unusable_estimate = RegularHealEfficiency::EstimateRegularHealAmount(
			2,
			[](uint16_t, int) { return static_cast<int64_t>(0); },
			[](uint16_t, int64_t value) { return value; }
		);
		const auto insufficient_estimate = RegularHealEfficiency::EstimateRegularHealAmount(
			3,
			[](uint16_t, int) { return static_cast<int64_t>(450); },
			[](uint16_t, int64_t value) { return value; }
		);

		const RegularHealEfficiency::Settings settings{
			.prefer_efficient_regular_heals = true
		};
		const auto result = RegularHealEfficiency::SelectRegularHealCandidate(
			settings,
			500,
			0,
			{
				{
					.spell_id = 2,
					.list_order = 0,
					.mana_cost = 10,
					.has_usable_estimated_heal = unusable_estimate.usable,
					.estimated_heal = unusable_estimate.amount
				},
				{
					.spell_id = 3,
					.list_order = 1,
					.mana_cost = 10,
					.has_usable_estimated_heal = insufficient_estimate.usable,
					.estimated_heal = insufficient_estimate.amount
				}
			}
		);

		TEST_ASSERT(result.found);
		TEST_ASSERT_EQUALS(result.spell_id, static_cast<uint16_t>(2));
		TEST_ASSERT(!result.selected_for_efficiency);
	}

	void DisabledRuleReturnsPriorityFirstFromRuntimeCandidateList()
	{
		const RegularHealEfficiency::Settings settings{
			.prefer_efficient_regular_heals = false
		};
		const std::vector<RegularHealEfficiency::Candidate> candidates{
			{.spell_id = 701, .list_order = 0, .mana_cost = 120, .has_usable_estimated_heal = true, .estimated_heal = 1200},
			{.spell_id = 702, .list_order = 1, .mana_cost = 40, .has_usable_estimated_heal = true, .estimated_heal = 520}
		};

		const auto result = RegularHealEfficiency::SelectRegularHealCandidate(
			settings,
			500,
			0,
			candidates
		);

		TEST_ASSERT(result.found);
		TEST_ASSERT_EQUALS(result.spell_id, static_cast<uint16_t>(701));
		TEST_ASSERT_EQUALS(result.list_order, 0u);
		TEST_ASSERT(!result.selected_for_efficiency);
	}

	void EnabledRuleCanChooseLaterSmallerRuntimeCandidate()
	{
		const RegularHealEfficiency::Settings settings{
			.prefer_efficient_regular_heals = true
		};
		const std::vector<RegularHealEfficiency::Candidate> candidates{
			{.spell_id = 801, .list_order = 0, .mana_cost = 120, .has_usable_estimated_heal = true, .estimated_heal = 1200},
			{.spell_id = 802, .list_order = 1, .mana_cost = 40, .has_usable_estimated_heal = true, .estimated_heal = 520}
		};

		const auto result = RegularHealEfficiency::SelectRegularHealCandidate(
			settings,
			500,
			0,
			candidates
		);

		TEST_ASSERT(result.found);
		TEST_ASSERT_EQUALS(result.spell_id, static_cast<uint16_t>(802));
		TEST_ASSERT_EQUALS(result.list_order, 1u);
		TEST_ASSERT(result.selected_for_efficiency);
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

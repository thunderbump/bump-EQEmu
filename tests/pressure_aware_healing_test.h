/*	EQEmu: EQEmulator

	Copyright (C) 2001-2026 EQEmu Development Team

	This program is free software; you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation; either version 3 of the License, or
	(at your option) any later version.

	This program is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
	GNU General Public License for more details.

	You should have received a copy of the GNU General Public License
	along with this program. If not, see <http://www.gnu.org/licenses/>.
*/
#pragma once

#include "cppunit/cpptest.h"

#include "common/pressure_aware_healing.h"
#include "common/rulesys.h"
#include "common/spdat.h"

class PressureAwareHealingTest : public Test::Suite {
public:
	PressureAwareHealingTest()
	{
		TEST_ADD(PressureAwareHealingTest::DefaultRulesDisablePressureAwareHealing);
		TEST_ADD(PressureAwareHealingTest::DisabledPressureAwareHealingKeepsCurrentSpellType);
		TEST_ADD(PressureAwareHealingTest::NoPressureRegularHealPrefersSingleTargetHoT);
		TEST_ADD(PressureAwareHealingTest::UnavailableHoTKeepsRegularHealFallback);
		TEST_ADD(PressureAwareHealingTest::EmergencyDirectHealsDoNotPreferHoT);
		TEST_ADD(PressureAwareHealingTest::GroupAndCompleteHealsRemainUnchanged);
		TEST_ADD(PressureAwareHealingTest::PetRegularHealPrefersPetHoT);
	}

private:
	void DefaultRulesDisablePressureAwareHealing()
	{
		RuleManager::Instance()->ResetRules();

		const auto settings = PressureAwareHealing::LoadSettingsFromRules();

		TEST_ASSERT(!settings.enabled);
		TEST_ASSERT_EQUALS(settings.pressure_sample_ms, 3000);
		TEST_ASSERT_EQUALS(settings.emergency_projection_ms, 2000);
		TEST_ASSERT_EQUALS(settings.hot_sustain_ms, 8000);
	}

	void DisabledPressureAwareHealingKeepsCurrentSpellType()
	{
		const PressureAwareHealing::Settings settings{
			.enabled = false,
			.pressure_sample_ms = 1000,
			.emergency_projection_ms = 1000,
			.hot_sustain_ms = 1000
		};

		TEST_ASSERT_EQUALS(
			PressureAwareHealing::DisabledModeSpellType(27, settings),
			27
		);
	}

	void NoPressureRegularHealPrefersSingleTargetHoT()
	{
		const PressureAwareHealing::Settings settings{
			.enabled = true,
			.pressure_sample_ms = 1000,
			.emergency_projection_ms = 1000,
			.hot_sustain_ms = 1000
		};

		TEST_ASSERT_EQUALS(
			PressureAwareHealing::SelectSustainHealSpellType(
				BotSpellTypes::RegularHeal,
				BotSpellTypes::HoTHeals,
				settings
			),
			BotSpellTypes::HoTHeals
		);
	}

	void UnavailableHoTKeepsRegularHealFallback()
	{
		const PressureAwareHealing::Settings settings{
			.enabled = true,
			.pressure_sample_ms = 1000,
			.emergency_projection_ms = 1000,
			.hot_sustain_ms = 1000
		};

		TEST_ASSERT_EQUALS(
			PressureAwareHealing::SelectSustainHealSpellType(
				BotSpellTypes::RegularHeal,
				0,
				settings
			),
			BotSpellTypes::RegularHeal
		);
	}

	void EmergencyDirectHealsDoNotPreferHoT()
	{
		const PressureAwareHealing::Settings settings{
			.enabled = true,
			.pressure_sample_ms = 1000,
			.emergency_projection_ms = 1000,
			.hot_sustain_ms = 1000
		};

		TEST_ASSERT_EQUALS(
			PressureAwareHealing::SelectSustainHealSpellType(
				BotSpellTypes::FastHeals,
				BotSpellTypes::HoTHeals,
				settings
			),
			BotSpellTypes::FastHeals
		);

		TEST_ASSERT_EQUALS(
			PressureAwareHealing::SelectSustainHealSpellType(
				BotSpellTypes::VeryFastHeals,
				BotSpellTypes::HoTHeals,
				settings
			),
			BotSpellTypes::VeryFastHeals
		);
	}

	void GroupAndCompleteHealsRemainUnchanged()
	{
		const PressureAwareHealing::Settings settings{
			.enabled = true,
			.pressure_sample_ms = 1000,
			.emergency_projection_ms = 1000,
			.hot_sustain_ms = 1000
		};

		TEST_ASSERT_EQUALS(
			PressureAwareHealing::SelectSustainHealSpellType(
				BotSpellTypes::GroupHeals,
				BotSpellTypes::HoTHeals,
				settings
			),
			BotSpellTypes::GroupHeals
		);

		TEST_ASSERT_EQUALS(
			PressureAwareHealing::SelectSustainHealSpellType(
				BotSpellTypes::GroupHoTHeals,
				BotSpellTypes::HoTHeals,
				settings
			),
			BotSpellTypes::GroupHoTHeals
		);

		TEST_ASSERT_EQUALS(
			PressureAwareHealing::SelectSustainHealSpellType(
				BotSpellTypes::CompleteHeal,
				BotSpellTypes::HoTHeals,
				settings
			),
			BotSpellTypes::CompleteHeal
		);
	}

	void PetRegularHealPrefersPetHoT()
	{
		const PressureAwareHealing::Settings settings{
			.enabled = true,
			.pressure_sample_ms = 1000,
			.emergency_projection_ms = 1000,
			.hot_sustain_ms = 1000
		};

		TEST_ASSERT_EQUALS(
			PressureAwareHealing::SelectSustainHealSpellType(
				BotSpellTypes::PetRegularHeals,
				BotSpellTypes::PetHoTHeals,
				settings
			),
			BotSpellTypes::PetHoTHeals
		);
	}
};

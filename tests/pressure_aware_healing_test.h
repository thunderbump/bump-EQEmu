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

class PressureAwareHealingTest : public Test::Suite {
public:
	PressureAwareHealingTest()
	{
		TEST_ADD(PressureAwareHealingTest::DefaultRulesDisablePressureAwareHealing);
		TEST_ADD(PressureAwareHealingTest::DisabledPressureAwareHealingKeepsCurrentSpellType);
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
};

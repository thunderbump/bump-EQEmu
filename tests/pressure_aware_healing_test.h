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
#include "common/regular_heal_efficiency.h"
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
		TEST_ADD(PressureAwareHealingTest::FreshIncomingDamageSuppressesHoTSustain);
		TEST_ADD(PressureAwareHealingTest::ExpiredIncomingDamageAllowsHoTSustain);
		TEST_ADD(PressureAwareHealingTest::HealingAndNonPositiveDamageDoNotChangePressure);
		TEST_ADD(PressureAwareHealingTest::EnvironmentalAndSelfDamageAreExcluded);
		TEST_ADD(PressureAwareHealingTest::DisabledSettingsDoNotReportActivePressure);
		TEST_ADD(PressureAwareHealingTest::RegularHealRemainsEligibleWhenDangerWindowIsSafe);
		TEST_ADD(PressureAwareHealingTest::RegularHealEscalatesToFastHealWhenDangerWindowIsUnsafe);
		TEST_ADD(PressureAwareHealingTest::FastHealEscalatesToVeryFastHealWhenDangerWindowIsUnsafe);
		TEST_ADD(PressureAwareHealingTest::DirectHealEscalationSkipsUnavailableCandidates);
		TEST_ADD(PressureAwareHealingTest::DirectHealEscalationKeepsCurrentTypeWhenNoCandidateIsAvailable);
		TEST_ADD(PressureAwareHealingTest::PressureEscalationRunsBeforeRegularHealEfficiencySelection);
		TEST_ADD(PressureAwareHealingTest::MissingPressureKeepsDirectHealType);
		TEST_ADD(PressureAwareHealingTest::DisabledSettingsKeepDirectHealTypeUnderPressure);
		TEST_ADD(PressureAwareHealingTest::EmergencyThresholdDirectHealDoesNotPreferHoT);
		TEST_ADD(PressureAwareHealingTest::ExcludedHealingCategoriesIgnoreDirectHealEscalation);
		TEST_ADD(PressureAwareHealingTest::PressureSelectionDoesNotMutateTargetPressure);
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
				false,
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
				false,
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
				false,
				settings
			),
			BotSpellTypes::FastHeals
		);

		TEST_ASSERT_EQUALS(
			PressureAwareHealing::SelectSustainHealSpellType(
				BotSpellTypes::VeryFastHeals,
				BotSpellTypes::HoTHeals,
				false,
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
				false,
				settings
			),
			BotSpellTypes::GroupHeals
		);

		TEST_ASSERT_EQUALS(
			PressureAwareHealing::SelectSustainHealSpellType(
				BotSpellTypes::GroupHoTHeals,
				BotSpellTypes::HoTHeals,
				false,
				settings
			),
			BotSpellTypes::GroupHoTHeals
		);

		TEST_ASSERT_EQUALS(
			PressureAwareHealing::SelectSustainHealSpellType(
				BotSpellTypes::CompleteHeal,
				BotSpellTypes::HoTHeals,
				false,
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
				false,
				settings
			),
			BotSpellTypes::PetHoTHeals
		);
	}

	void FreshIncomingDamageSuppressesHoTSustain()
	{
		const PressureAwareHealing::Settings settings{
			.enabled = true,
			.pressure_sample_ms = 1000,
			.emergency_projection_ms = 1000,
			.hot_sustain_ms = 1000
		};
		PressureAwareHealing::IncomingDamagePressure pressure{};

		PressureAwareHealing::RecordCombatDamage(pressure, 250, 1000);

		TEST_ASSERT(PressureAwareHealing::HasActiveDamagePressure(pressure, settings, 1400));
		TEST_ASSERT_EQUALS(
			PressureAwareHealing::SelectSustainHealSpellType(
				BotSpellTypes::RegularHeal,
				BotSpellTypes::HoTHeals,
				PressureAwareHealing::HasActiveDamagePressure(pressure, settings, 1400),
				settings
			),
			BotSpellTypes::RegularHeal
		);
	}

	void ExpiredIncomingDamageAllowsHoTSustain()
	{
		const PressureAwareHealing::Settings settings{
			.enabled = true,
			.pressure_sample_ms = 1000,
			.emergency_projection_ms = 1000,
			.hot_sustain_ms = 1000
		};
		PressureAwareHealing::IncomingDamagePressure pressure{};

		PressureAwareHealing::RecordCombatDamage(pressure, 250, 1000);

		TEST_ASSERT(!PressureAwareHealing::HasActiveDamagePressure(pressure, settings, 2001));
		TEST_ASSERT_EQUALS(
			PressureAwareHealing::SelectSustainHealSpellType(
				BotSpellTypes::RegularHeal,
				BotSpellTypes::HoTHeals,
				PressureAwareHealing::HasActiveDamagePressure(pressure, settings, 2001),
				settings
			),
			BotSpellTypes::HoTHeals
		);
	}

	void HealingAndNonPositiveDamageDoNotChangePressure()
	{
		const PressureAwareHealing::Settings settings{
			.enabled = true,
			.pressure_sample_ms = 1000,
			.emergency_projection_ms = 1000,
			.hot_sustain_ms = 1000
		};
		PressureAwareHealing::IncomingDamagePressure pressure{};

		PressureAwareHealing::RecordCombatDamage(pressure, 0, 1000);
		PressureAwareHealing::RecordCombatDamage(pressure, -250, 1100);

		TEST_ASSERT(!PressureAwareHealing::HasActiveDamagePressure(pressure, settings, 1200));
	}

	void EnvironmentalAndSelfDamageAreExcluded()
	{
		TEST_ASSERT(!PressureAwareHealing::ShouldRecordCombatDamage(250, false, false));
		TEST_ASSERT(!PressureAwareHealing::ShouldRecordCombatDamage(250, true, true));
		TEST_ASSERT(!PressureAwareHealing::ShouldRecordCombatDamage(0, true, false));
		TEST_ASSERT(PressureAwareHealing::ShouldRecordCombatDamage(250, true, false));
	}

	void DisabledSettingsDoNotReportActivePressure()
	{
		const PressureAwareHealing::Settings settings{
			.enabled = false,
			.pressure_sample_ms = 1000,
			.emergency_projection_ms = 1000,
			.hot_sustain_ms = 1000
		};
		PressureAwareHealing::IncomingDamagePressure pressure{};

		PressureAwareHealing::RecordCombatDamage(pressure, 250, 1000);

		TEST_ASSERT(!PressureAwareHealing::HasActiveDamagePressure(pressure, settings, 1200));
	}

	void RegularHealRemainsEligibleWhenDangerWindowIsSafe()
	{
		const PressureAwareHealing::Settings settings{
			.enabled = true,
			.pressure_sample_ms = 1000,
			.emergency_projection_ms = 2000,
			.hot_sustain_ms = 1000
		};
		PressureAwareHealing::IncomingDamagePressure pressure{};

		PressureAwareHealing::RecordCombatDamage(pressure, 100, 1000);

		TEST_ASSERT_EQUALS(
			PressureAwareHealing::SelectDirectHealSpellType(
				BotSpellTypes::RegularHeal,
				pressure,
				settings,
				900,
				1000,
				{
					PressureAwareHealing::DirectHealCandidate{BotSpellTypes::RegularHeal, true, 2000, 60},
					PressureAwareHealing::DirectHealCandidate{BotSpellTypes::FastHeals, true, 1000, 40},
					PressureAwareHealing::DirectHealCandidate{BotSpellTypes::VeryFastHeals, true, 0, 25}
				}
			),
			BotSpellTypes::RegularHeal
		);
	}

	void RegularHealEscalatesToFastHealWhenDangerWindowIsUnsafe()
	{
		const PressureAwareHealing::Settings settings{
			.enabled = true,
			.pressure_sample_ms = 1000,
			.emergency_projection_ms = 2000,
			.hot_sustain_ms = 1000
		};
		PressureAwareHealing::IncomingDamagePressure pressure{};

		PressureAwareHealing::RecordCombatDamage(pressure, 100, 1000);

		TEST_ASSERT_EQUALS(
			PressureAwareHealing::SelectDirectHealSpellType(
				BotSpellTypes::RegularHeal,
				pressure,
				settings,
				700,
				1000,
				{
					PressureAwareHealing::DirectHealCandidate{BotSpellTypes::RegularHeal, true, 2000, 60},
					PressureAwareHealing::DirectHealCandidate{BotSpellTypes::FastHeals, true, 1000, 40},
					PressureAwareHealing::DirectHealCandidate{BotSpellTypes::VeryFastHeals, true, 0, 25}
				}
			),
			BotSpellTypes::FastHeals
		);
	}

	void FastHealEscalatesToVeryFastHealWhenDangerWindowIsUnsafe()
	{
		const PressureAwareHealing::Settings settings{
			.enabled = true,
			.pressure_sample_ms = 1000,
			.emergency_projection_ms = 2000,
			.hot_sustain_ms = 1000
		};
		PressureAwareHealing::IncomingDamagePressure pressure{};

		PressureAwareHealing::RecordCombatDamage(pressure, 100, 1000);

		TEST_ASSERT_EQUALS(
			PressureAwareHealing::SelectDirectHealSpellType(
				BotSpellTypes::FastHeals,
				pressure,
				settings,
				500,
				1000,
				{
					PressureAwareHealing::DirectHealCandidate{BotSpellTypes::RegularHeal, true, 2000, 60},
					PressureAwareHealing::DirectHealCandidate{BotSpellTypes::FastHeals, true, 1000, 40},
					PressureAwareHealing::DirectHealCandidate{BotSpellTypes::VeryFastHeals, true, 0, 25}
				}
			),
			BotSpellTypes::VeryFastHeals
		);
	}

	void DirectHealEscalationSkipsUnavailableCandidates()
	{
		const PressureAwareHealing::Settings settings{
			.enabled = true,
			.pressure_sample_ms = 1000,
			.emergency_projection_ms = 2000,
			.hot_sustain_ms = 1000
		};
		PressureAwareHealing::IncomingDamagePressure pressure{};

		PressureAwareHealing::RecordCombatDamage(pressure, 100, 1000);

		TEST_ASSERT_EQUALS(
			PressureAwareHealing::SelectDirectHealSpellType(
				BotSpellTypes::RegularHeal,
				pressure,
				settings,
				700,
				1000,
				{
					PressureAwareHealing::DirectHealCandidate{BotSpellTypes::RegularHeal, true, 2000, 60},
					PressureAwareHealing::DirectHealCandidate{BotSpellTypes::FastHeals, false, 1000, 40},
					PressureAwareHealing::DirectHealCandidate{BotSpellTypes::VeryFastHeals, true, 0, 25}
				}
			),
			BotSpellTypes::VeryFastHeals
		);
	}

	void DirectHealEscalationKeepsCurrentTypeWhenNoCandidateIsAvailable()
	{
		const PressureAwareHealing::Settings settings{
			.enabled = true,
			.pressure_sample_ms = 1000,
			.emergency_projection_ms = 2000,
			.hot_sustain_ms = 1000
		};
		PressureAwareHealing::IncomingDamagePressure pressure{};

		PressureAwareHealing::RecordCombatDamage(pressure, 100, 1000);

		TEST_ASSERT_EQUALS(
			PressureAwareHealing::SelectDirectHealSpellType(
				BotSpellTypes::RegularHeal,
				pressure,
				settings,
				700,
				1000,
				{
					PressureAwareHealing::DirectHealCandidate{BotSpellTypes::RegularHeal, true, 2000, 60},
					PressureAwareHealing::DirectHealCandidate{BotSpellTypes::FastHeals, false, 1000, 40},
					PressureAwareHealing::DirectHealCandidate{BotSpellTypes::VeryFastHeals, false, 0, 25}
				}
			),
			BotSpellTypes::RegularHeal
		);
	}

	void PressureEscalationRunsBeforeRegularHealEfficiencySelection()
	{
		const PressureAwareHealing::Settings pressure_settings{
			.enabled = true,
			.pressure_sample_ms = 1000,
			.emergency_projection_ms = 2000,
			.hot_sustain_ms = 1000
		};
		const RegularHealEfficiency::Settings efficiency_settings{
			.prefer_efficient_regular_heals = true
		};
		PressureAwareHealing::IncomingDamagePressure pressure{};

		PressureAwareHealing::RecordCombatDamage(pressure, 100, 1000);

		const auto spell_type = PressureAwareHealing::SelectDirectHealSpellType(
			BotSpellTypes::RegularHeal,
			pressure,
			pressure_settings,
			700,
			1000,
			{
				PressureAwareHealing::DirectHealCandidate{BotSpellTypes::RegularHeal, true, 2000, 60},
				PressureAwareHealing::DirectHealCandidate{BotSpellTypes::FastHeals, true, 1000, 40},
				PressureAwareHealing::DirectHealCandidate{BotSpellTypes::VeryFastHeals, true, 0, 25}
			}
		);

		TEST_ASSERT_EQUALS(spell_type, BotSpellTypes::FastHeals);
		TEST_ASSERT(!RegularHealEfficiency::ShouldUseEfficientSelection(
			efficiency_settings,
			spell_type,
			false
		));
	}

	void MissingPressureKeepsDirectHealType()
	{
		const PressureAwareHealing::Settings settings{
			.enabled = true,
			.pressure_sample_ms = 1000,
			.emergency_projection_ms = 2000,
			.hot_sustain_ms = 1000
		};
		PressureAwareHealing::IncomingDamagePressure pressure{};

		TEST_ASSERT_EQUALS(
			PressureAwareHealing::SelectDirectHealSpellType(
				BotSpellTypes::RegularHeal,
				pressure,
				settings,
				500,
				1000,
				{
					PressureAwareHealing::DirectHealCandidate{BotSpellTypes::RegularHeal, true, 2000, 60},
					PressureAwareHealing::DirectHealCandidate{BotSpellTypes::FastHeals, true, 1000, 40},
					PressureAwareHealing::DirectHealCandidate{BotSpellTypes::VeryFastHeals, true, 0, 25}
				}
			),
			BotSpellTypes::RegularHeal
		);
	}

	void DisabledSettingsKeepDirectHealTypeUnderPressure()
	{
		const PressureAwareHealing::Settings settings{
			.enabled = false,
			.pressure_sample_ms = 1000,
			.emergency_projection_ms = 2000,
			.hot_sustain_ms = 1000
		};
		PressureAwareHealing::IncomingDamagePressure pressure{};

		PressureAwareHealing::RecordCombatDamage(pressure, 100, 1000);

		TEST_ASSERT_EQUALS(
			PressureAwareHealing::SelectDirectHealSpellType(
				BotSpellTypes::RegularHeal,
				pressure,
				settings,
				500,
				1000,
				{
					PressureAwareHealing::DirectHealCandidate{BotSpellTypes::RegularHeal, true, 2000, 60},
					PressureAwareHealing::DirectHealCandidate{BotSpellTypes::FastHeals, true, 1000, 40},
					PressureAwareHealing::DirectHealCandidate{BotSpellTypes::VeryFastHeals, true, 0, 25}
				}
			),
			BotSpellTypes::RegularHeal
		);
	}

	void EmergencyThresholdDirectHealDoesNotPreferHoT()
	{
		const PressureAwareHealing::Settings settings{
			.enabled = true,
			.pressure_sample_ms = 1000,
			.emergency_projection_ms = 2000,
			.hot_sustain_ms = 1000
		};

		TEST_ASSERT_EQUALS(
			PressureAwareHealing::SelectSustainHealSpellType(
				BotSpellTypes::FastHeals,
				BotSpellTypes::HoTHeals,
				true,
				settings
			),
			BotSpellTypes::FastHeals
		);
	}

	void ExcludedHealingCategoriesIgnoreDirectHealEscalation()
	{
		const PressureAwareHealing::Settings settings{
			.enabled = true,
			.pressure_sample_ms = 1000,
			.emergency_projection_ms = 2000,
			.hot_sustain_ms = 1000
		};
		PressureAwareHealing::IncomingDamagePressure pressure{};

		PressureAwareHealing::RecordCombatDamage(pressure, 100, 1000);

		const uint16_t excluded_spell_types[] = {
			BotSpellTypes::CompleteHeal,
			BotSpellTypes::GroupCompleteHeals,
			BotSpellTypes::PetCompleteHeals,
			BotSpellTypes::GroupHeals,
			BotSpellTypes::GroupHoTHeals,
			BotSpellTypes::HoTHeals,
			BotSpellTypes::PetRegularHeals,
			BotSpellTypes::PetFastHeals,
			BotSpellTypes::PetVeryFastHeals,
			BotSpellTypes::PetHoTHeals
		};

		for (const auto spell_type : excluded_spell_types) {
			TEST_ASSERT_EQUALS(
				PressureAwareHealing::SelectDirectHealSpellType(
					spell_type,
					pressure,
					settings,
					500,
					1000,
					{
						PressureAwareHealing::DirectHealCandidate{BotSpellTypes::RegularHeal, true, 2000, 60},
						PressureAwareHealing::DirectHealCandidate{BotSpellTypes::FastHeals, true, 1000, 40},
						PressureAwareHealing::DirectHealCandidate{BotSpellTypes::VeryFastHeals, true, 0, 25}
					}
				),
				spell_type
			);
		}
	}

	void PressureSelectionDoesNotMutateTargetPressure()
	{
		const PressureAwareHealing::Settings settings{
			.enabled = true,
			.pressure_sample_ms = 1000,
			.emergency_projection_ms = 2000,
			.hot_sustain_ms = 1000
		};
		PressureAwareHealing::IncomingDamagePressure pressure{};

		PressureAwareHealing::RecordCombatDamage(pressure, 100, 1000);

		PressureAwareHealing::SelectDirectHealSpellType(
			BotSpellTypes::RegularHeal,
			pressure,
			settings,
			700,
			1000,
			{
				PressureAwareHealing::DirectHealCandidate{BotSpellTypes::RegularHeal, true, 2000, 60},
				PressureAwareHealing::DirectHealCandidate{BotSpellTypes::FastHeals, true, 1000, 40},
				PressureAwareHealing::DirectHealCandidate{BotSpellTypes::VeryFastHeals, true, 0, 25}
			}
		);

		TEST_ASSERT_EQUALS(pressure.damage, static_cast<int64_t>(100));
		TEST_ASSERT_EQUALS(pressure.updated_at_ms, static_cast<uint32_t>(1000));
	}
};

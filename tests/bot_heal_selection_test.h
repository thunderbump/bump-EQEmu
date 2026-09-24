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

#include "common/spdat.h"
#include "zone/bot_heal_selection_result.h"

class BotHealSelectionTest : public Test::Suite {
public:
	BotHealSelectionTest()
	{
		TEST_ADD(BotHealSelectionTest::ResultCarriesSelectedSpellTypeAndConcreteSpell);
		TEST_ADD(BotHealSelectionTest::InvalidAlternativeFallsBackToRequestedSpell);
		TEST_ADD(BotHealSelectionTest::MissingAlternativeAndFallbackHasNoSelection);
		TEST_ADD(BotHealSelectionTest::CompleteHealParentFallbackWaitsForRegularHealThreshold);
		TEST_ADD(BotHealSelectionTest::CompleteHealParentFallbackPreservesLowHpEmergency);
		TEST_ADD(BotHealSelectionTest::PetCompleteHealParentFallbackWaitsForModeratePetHp);
		TEST_ADD(BotHealSelectionTest::PetCompleteHealParentFallbackPreservesLowHpEmergency);
		TEST_ADD(BotHealSelectionTest::ExplicitCompleteHealEntryIgnoresParentFallbackGate);
	}

private:
	struct TestSpell {
		uint16 SpellId = 0;
		uint16 SpellIndex = 0;
		int ManaCost = 0;
	};

	void ResultCarriesSelectedSpellTypeAndConcreteSpell()
	{
		const TestSpell selected_spell{
			.SpellId = 12,
			.SpellIndex = 34,
			.ManaCost = 56
		};
		const TestSpell fallback_spell{
			.SpellId = 78,
			.SpellIndex = 90,
			.ManaCost = 12
		};

		const auto result = BotHealSelection::PreferAlternativeOrFallback(
			BotSpellTypes::FastHeals,
			selected_spell,
			BotSpellTypes::RegularHeal,
			fallback_spell
		);

		TEST_ASSERT(result.found);
		TEST_ASSERT_EQUALS(result.selected_spell_type, static_cast<uint16>(BotSpellTypes::FastHeals));
		TEST_ASSERT_EQUALS(result.spell.SpellId, static_cast<uint16>(12));
		TEST_ASSERT_EQUALS(result.spell.SpellIndex, static_cast<uint16>(34));
		TEST_ASSERT_EQUALS(result.spell.ManaCost, 56);
	}

	void InvalidAlternativeFallsBackToRequestedSpell()
	{
		const TestSpell invalid_alternative{};
		const TestSpell fallback_spell{
			.SpellId = 78,
			.SpellIndex = 90,
			.ManaCost = 12
		};

		const auto result = BotHealSelection::PreferAlternativeOrFallback(
			BotSpellTypes::HoTHeals,
			invalid_alternative,
			BotSpellTypes::RegularHeal,
			fallback_spell
		);

		TEST_ASSERT(result.found);
		TEST_ASSERT_EQUALS(result.selected_spell_type, static_cast<uint16>(BotSpellTypes::RegularHeal));
		TEST_ASSERT_EQUALS(result.spell.SpellId, static_cast<uint16>(78));
	}

	void MissingAlternativeAndFallbackHasNoSelection()
	{
		const auto result = BotHealSelection::PreferAlternativeOrFallback(
			BotSpellTypes::HoTHeals,
			TestSpell{},
			BotSpellTypes::RegularHeal,
			TestSpell{}
		);

		TEST_ASSERT(!result.found);
		TEST_ASSERT_EQUALS(result.selected_spell_type, static_cast<uint16>(0));
		TEST_ASSERT_EQUALS(result.spell.SpellId, static_cast<uint16>(0));
	}

	void CompleteHealParentFallbackWaitsForRegularHealThreshold()
	{
		TEST_ASSERT(!BotHealSelection::AllowsCompleteHealParentFallback(
			BotSpellTypes::CompleteHeal,
			BotSpellTypes::RegularHeal,
			65,
			60
		));
	}

	void CompleteHealParentFallbackPreservesLowHpEmergency()
	{
		TEST_ASSERT(BotHealSelection::AllowsCompleteHealParentFallback(
			BotSpellTypes::CompleteHeal,
			BotSpellTypes::RegularHeal,
			40,
			60
		));
	}

	void PetCompleteHealParentFallbackWaitsForModeratePetHp()
	{
		TEST_ASSERT(!BotHealSelection::AllowsCompleteHealParentFallback(
			BotSpellTypes::PetCompleteHeals,
			BotSpellTypes::RegularHeal,
			65,
			60
		));
	}

	void PetCompleteHealParentFallbackPreservesLowHpEmergency()
	{
		TEST_ASSERT(BotHealSelection::AllowsCompleteHealParentFallback(
			BotSpellTypes::PetCompleteHeals,
			BotSpellTypes::RegularHeal,
			40,
			60
		));
	}

	void ExplicitCompleteHealEntryIgnoresParentFallbackGate()
	{
		TEST_ASSERT(BotHealSelection::AllowsCompleteHealParentFallback(
			BotSpellTypes::CompleteHeal,
			BotSpellTypes::CompleteHeal,
			75,
			60
		));
	}
};

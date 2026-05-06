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

#include "common/fallback_dialogue.h"
#include "common/rulesys.h"
#include "cppunit/cpptest.h"

class FallbackDialogueTest : public Test::Suite {
public:
	FallbackDialogueTest()
	{
		TEST_ADD(FallbackDialogueTest::DefaultRulesDisableTargetedSayFallback);
	}

private:
	void DefaultRulesDisableTargetedSayFallback()
	{
		RuleManager::Instance()->ResetRules(false);

		TEST_ASSERT(!RuleB(Chat, FallbackDialogueEnabled));
		TEST_ASSERT_EQUALS(
			RuleS(Chat, FallbackDialogueUnavailableReply),
			std::string("appears distracted.")
		);
		TEST_ASSERT_EQUALS(RuleI(Chat, FallbackDialogueCooldownSeconds), 30);

		const FallbackDialogue::TargetedSayRequest request{
			.speaker_id = 1,
			.target_id = 2,
			.message = "hello"
		};

		const auto result = FallbackDialogue::HandleTargetedSay(request);
		TEST_ASSERT(!result.handled);
	}
};

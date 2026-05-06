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
		TEST_ADD(FallbackDialogueTest::EligibleNpcTargetedSayReturnsUnavailableReplyEmote);
		TEST_ADD(FallbackDialogueTest::AuthoredNpcDialogueSuppressesUnavailableReply);
		TEST_ADD(FallbackDialogueTest::EngagedNpcDialogueSuppressesUnavailableReply);
		TEST_ADD(FallbackDialogueTest::MercenaryTargetedSayReportsSkipWithoutReply);
		TEST_ADD(FallbackDialogueTest::UnknownTargetedSayReportsSkipWithoutReply);
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

	void EligibleNpcTargetedSayReturnsUnavailableReplyEmote()
	{
		RuleManager::Instance()->ResetRules(false);
		RuleManager::Instance()->SetRule("Chat:FallbackDialogueEnabled", "true");
		RuleManager::Instance()->SetRule("Chat:FallbackDialogueUnavailableReply", "seems lost in thought.");

		const FallbackDialogue::TargetedSayRequest request{
			.speaker_id = 1,
			.target_id = 2,
			.message = "hello",
			.target_type = FallbackDialogue::TargetType::NPC,
			.authored_dialogue_handled = false
		};

		const auto result = FallbackDialogue::HandleTargetedSay(request);
		TEST_ASSERT(result.handled);
		TEST_ASSERT(result.output_type == FallbackDialogue::OutputType::Emote);
		TEST_ASSERT_EQUALS(result.message, std::string("seems lost in thought."));
	}

	void AuthoredNpcDialogueSuppressesUnavailableReply()
	{
		RuleManager::Instance()->ResetRules(false);
		RuleManager::Instance()->SetRule("Chat:FallbackDialogueEnabled", "true");

		const FallbackDialogue::TargetedSayRequest request{
			.speaker_id = 1,
			.target_id = 2,
			.message = "hail",
			.target_type = FallbackDialogue::TargetType::NPC,
			.authored_dialogue_handled = true
		};

		const auto result = FallbackDialogue::HandleTargetedSay(request);
		TEST_ASSERT(!result.handled);
		TEST_ASSERT(result.output_type == FallbackDialogue::OutputType::None);
		TEST_ASSERT(result.message.empty());
		TEST_ASSERT_EQUALS(result.debug_reason, std::string("authored_dialogue_handled"));
	}

	void EngagedNpcDialogueSuppressesUnavailableReply()
	{
		RuleManager::Instance()->ResetRules(false);
		RuleManager::Instance()->SetRule("Chat:FallbackDialogueEnabled", "true");

		const FallbackDialogue::TargetedSayRequest request{
			.speaker_id = 1,
			.target_id = 2,
			.message = "hail",
			.target_type = FallbackDialogue::TargetType::NPC,
			.authored_dialogue_handled = false,
			.target_engaged = true
		};

		const auto result = FallbackDialogue::HandleTargetedSay(request);
		TEST_ASSERT(!result.handled);
		TEST_ASSERT(result.output_type == FallbackDialogue::OutputType::None);
		TEST_ASSERT(result.message.empty());
		TEST_ASSERT_EQUALS(result.debug_reason, std::string("target_engaged"));
	}

	void MercenaryTargetedSayReportsSkipWithoutReply()
	{
		RuleManager::Instance()->ResetRules(false);
		RuleManager::Instance()->SetRule("Chat:FallbackDialogueEnabled", "true");

		const FallbackDialogue::TargetedSayRequest request{
			.speaker_id = 1,
			.target_id = 2,
			.message = "hail",
			.target_type = FallbackDialogue::TargetType::Mercenary,
			.authored_dialogue_handled = false
		};

		const auto result = FallbackDialogue::HandleTargetedSay(request);
		TEST_ASSERT(!result.handled);
		TEST_ASSERT(result.output_type == FallbackDialogue::OutputType::None);
		TEST_ASSERT(result.message.empty());
		TEST_ASSERT_EQUALS(result.debug_reason, std::string("unsupported_target_type"));
	}

	void UnknownTargetedSayReportsSkipWithoutReply()
	{
		RuleManager::Instance()->ResetRules(false);
		RuleManager::Instance()->SetRule("Chat:FallbackDialogueEnabled", "true");

		const FallbackDialogue::TargetedSayRequest request{
			.speaker_id = 1,
			.target_id = 2,
			.message = "hail",
			.target_type = FallbackDialogue::TargetType::Unknown,
			.authored_dialogue_handled = false
		};

		const auto result = FallbackDialogue::HandleTargetedSay(request);
		TEST_ASSERT(!result.handled);
		TEST_ASSERT(result.output_type == FallbackDialogue::OutputType::None);
		TEST_ASSERT(result.message.empty());
		TEST_ASSERT_EQUALS(result.debug_reason, std::string("unsupported_target_type"));
	}
};

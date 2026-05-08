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
#include "common/timer.h"
#include "cppunit/cpptest.h"

#include <chrono>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

class FallbackDialogueTest : public Test::Suite {
public:
	FallbackDialogueTest()
	{
		TEST_ADD(FallbackDialogueTest::DefaultRulesDisableTargetedSayFallback);
		TEST_ADD(FallbackDialogueTest::EligibleNpcTargetedSayReturnsUnavailableReplyEmote);
		TEST_ADD(FallbackDialogueTest::EligibleBotTargetedSayReturnsUnavailableReplyEmote);
		TEST_ADD(FallbackDialogueTest::EngagedBotTargetedSayReturnsUnavailableReplyEmote);
		TEST_ADD(FallbackDialogueTest::RepeatedNpcTargetedSayDuringCooldownShowsNoReply);
		TEST_ADD(FallbackDialogueTest::RepeatedBotTargetedSayDuringCooldownShowsNoReply);
		TEST_ADD(FallbackDialogueTest::DifferentTargetedSayBySameSpeakerBypassesCooldown);
		TEST_ADD(FallbackDialogueTest::TargetedSayAfterCooldownExpiresReturnsUnavailableReply);
		TEST_ADD(FallbackDialogueTest::AuthoredNpcDialogueSuppressesUnavailableReply);
		TEST_ADD(FallbackDialogueTest::AuthoredBotDialogueSuppressesUnavailableReply);
		TEST_ADD(FallbackDialogueTest::EngagedNpcDialogueSuppressesUnavailableReply);
		TEST_ADD(FallbackDialogueTest::MercenaryTargetedSayReportsSkipWithoutReply);
		TEST_ADD(FallbackDialogueTest::UnknownTargetedSayReportsSkipWithoutReply);
		TEST_ADD(FallbackDialogueTest::PublicGameplayContextIncludesAllowedFields);
		TEST_ADD(FallbackDialogueTest::PublicGameplayContextExcludesPrivateFields);
		TEST_ADD(FallbackDialogueTest::PublicGameplayContextFiltersNearbyEntitiesByRuleRadius);
		TEST_ADD(FallbackDialogueTest::PublicGameplayContextLimitsNearbyEntitiesByRuleCount);
		TEST_ADD(FallbackDialogueTest::PublicGameplayContextExcludesSpeakerAndTargetFromNearbyEntities);
		TEST_ADD(FallbackDialogueTest::EligibleNpcTargetedSayQueuesDelayedRequestWithPublicContext);
		TEST_ADD(FallbackDialogueTest::CompletedDelayedDialogueReturnsTargetSpeech);
		TEST_ADD(FallbackDialogueTest::CompletedDelayedDialogueStripsNewlines);
		TEST_ADD(FallbackDialogueTest::CompletedDelayedDialogueTrimsModelArtifacts);
		TEST_ADD(FallbackDialogueTest::CompletedDelayedDialogueReturnsPureAsteriskActionAsTargetEmote);
		TEST_ADD(FallbackDialogueTest::CompletedDelayedDialoguePreservesMixedSpeechAndEmoteOrder);
		TEST_ADD(FallbackDialogueTest::CompletedBotDelayedDialogueReturnsParenthesizedActionAsTargetEmote);
		TEST_ADD(FallbackDialogueTest::MalformedEmoteMarkerRemainsTargetSpeech);
		TEST_ADD(FallbackDialogueTest::CompletedDelayedDialogueSplitsLongLineAtSentenceBoundary);
		TEST_ADD(FallbackDialogueTest::CompletedDelayedDialogueSplitsLongWordAtLineLimit);
		TEST_ADD(FallbackDialogueTest::CompletedBotDelayedDialogueSplitsLongLineInOrder);
		TEST_ADD(FallbackDialogueTest::CommandLookingDelayedDialogueFallsBackToUnavailableReply);
		TEST_ADD(FallbackDialogueTest::CommandLookingEmoteFragmentFallsBackToUnavailableReply);
		TEST_ADD(FallbackDialogueTest::MetadataLookingDelayedDialogueFallsBackToUnavailableReply);
		TEST_ADD(FallbackDialogueTest::TechnicalLookingDelayedDialogueFallsBackToUnavailableReply);
		TEST_ADD(FallbackDialogueTest::OutOfCharacterDelayedDialogueFallsBackToUnavailableReply);
		TEST_ADD(FallbackDialogueTest::EmptyDelayedDialogueFallsBackToUnavailableReply);
		TEST_ADD(FallbackDialogueTest::FailedDelayedDialogueReturnsUnavailableReplyEmote);
		TEST_ADD(FallbackDialogueTest::CompletedDelayedDialogueReturnsTargetSpeechForCurrentInteraction);
		TEST_ADD(FallbackDialogueTest::CompletedDelayedDialogueDropsWhenSpeakerTargetsSomethingElse);
		TEST_ADD(FallbackDialogueTest::FailedDelayedDialogueDropsWhenSpeakerLeavesSayRange);
		TEST_ADD(FallbackDialogueTest::CompletedDelayedDialogueDropsWhenTargetIsMissing);
		TEST_ADD(FallbackDialogueTest::OllamaProviderSuccessfulResponseReturnsTargetSpeech);
		TEST_ADD(FallbackDialogueTest::OllamaProviderUsesConfiguredEndpointModelTimeoutAndPublicPrompt);
		TEST_ADD(FallbackDialogueTest::OllamaProviderTimeoutFallsBackToUnavailableReply);
		TEST_ADD(FallbackDialogueTest::OllamaProviderUnavailableServiceFallsBackToUnavailableReply);
		TEST_ADD(FallbackDialogueTest::OllamaProviderRequestFailureFallsBackToUnavailableReply);
		TEST_ADD(FallbackDialogueTest::OllamaProviderDisabledModelFallsBackToUnavailableReply);
		TEST_ADD(FallbackDialogueTest::OllamaProviderInvalidOutputFallsBackToUnavailableReply);
		TEST_ADD(FallbackDialogueTest::DiagnosticLogLineClassifiesRepresentativeResultsWithoutPrivateContent);
	}

private:
	struct FakeOllamaHttpCall {
		std::string endpoint;
		std::string body;
		int         timeout_ms = 0;
	};

	class FakeOllamaHttpTransport : public FallbackDialogue::OllamaHttpTransport {
	public:
		FallbackDialogue::OllamaHttpResponse response;

		FallbackDialogue::OllamaHttpResponse PostJson(
			const std::string &endpoint,
			const std::string &body,
			int timeout_ms
		) override
		{
			std::lock_guard lock(mutex_);
			calls_.push_back({
				.endpoint = endpoint,
				.body = body,
				.timeout_ms = timeout_ms
			});
			return response;
		}

		std::vector<FakeOllamaHttpCall> Calls()
		{
			std::lock_guard lock(mutex_);
			return calls_;
		}

	private:
		std::mutex mutex_;
		std::vector<FakeOllamaHttpCall> calls_;
	};

	void DefaultRulesDisableTargetedSayFallback()
	{
		ResetRules();

		TEST_ASSERT(!RuleB(Chat, FallbackDialogueEnabled));
		TEST_ASSERT_EQUALS(
			RuleS(Chat, FallbackDialogueUnavailableReply),
			std::string("appears distracted.")
		);
		TEST_ASSERT_EQUALS(RuleI(Chat, FallbackDialogueCooldownSeconds), 30);
		TEST_ASSERT_EQUALS(RuleI(Chat, FallbackDialogueNearbyContextRadius), 100);
		TEST_ASSERT_EQUALS(RuleI(Chat, FallbackDialogueNearbyEntityLimit), 8);
		TEST_ASSERT_EQUALS(RuleI(Chat, FallbackDialogueMaxLineLength), 200);
		TEST_ASSERT_EQUALS(
			RuleS(Chat, FallbackDialogueOllamaEndpoint),
			std::string("http://127.0.0.1:11434/api/generate")
		);
		TEST_ASSERT_EQUALS(RuleS(Chat, FallbackDialogueOllamaModel), std::string("llama3.2"));
		TEST_ASSERT_EQUALS(RuleI(Chat, FallbackDialogueOllamaTimeoutMs), 2000);

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
		ResetRules();
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

	void EligibleBotTargetedSayReturnsUnavailableReplyEmote()
	{
		ResetRules();
		RuleManager::Instance()->SetRule("Chat:FallbackDialogueEnabled", "true");
		RuleManager::Instance()->SetRule("Chat:FallbackDialogueUnavailableReply", "looks momentarily distracted.");

		const FallbackDialogue::TargetedSayRequest request{
			.speaker_id = 1,
			.target_id = 2,
			.message = "hello",
			.target_type = FallbackDialogue::TargetType::Bot,
			.authored_dialogue_handled = false
		};

		const auto result = FallbackDialogue::HandleTargetedSay(request);
		TEST_ASSERT(result.handled);
		TEST_ASSERT(result.output_type == FallbackDialogue::OutputType::Emote);
		TEST_ASSERT_EQUALS(result.message, std::string("looks momentarily distracted."));
	}

	void EngagedBotTargetedSayReturnsUnavailableReplyEmote()
	{
		ResetRules();
		RuleManager::Instance()->SetRule("Chat:FallbackDialogueEnabled", "true");
		RuleManager::Instance()->SetRule("Chat:FallbackDialogueUnavailableReply", "seems distracted by the fight.");

		const FallbackDialogue::TargetedSayRequest request{
			.speaker_id = 1,
			.target_id = 2,
			.message = "hello",
			.target_type = FallbackDialogue::TargetType::Bot,
			.authored_dialogue_handled = false,
			.target_engaged = true
		};

		const auto result = FallbackDialogue::HandleTargetedSay(request);
		TEST_ASSERT(result.handled);
		TEST_ASSERT(result.output_type == FallbackDialogue::OutputType::Emote);
		TEST_ASSERT_EQUALS(result.message, std::string("seems distracted by the fight."));
	}

	void RepeatedNpcTargetedSayDuringCooldownShowsNoReply()
	{
		ResetRules();
		RuleManager::Instance()->SetRule("Chat:FallbackDialogueEnabled", "true");
		RuleManager::Instance()->SetRule("Chat:FallbackDialogueCooldownSeconds", "30");

		const FallbackDialogue::TargetedSayRequest request{
			.speaker_id = 1,
			.target_id = 2,
			.message = "hail",
			.target_type = FallbackDialogue::TargetType::NPC,
			.authored_dialogue_handled = false
		};

		const auto first_result = FallbackDialogue::HandleTargetedSay(request);
		TEST_ASSERT(first_result.handled);

		const auto repeat_result = FallbackDialogue::HandleTargetedSay(request);
		TEST_ASSERT(!repeat_result.handled);
		TEST_ASSERT(repeat_result.output_type == FallbackDialogue::OutputType::None);
		TEST_ASSERT(repeat_result.message.empty());
		TEST_ASSERT_EQUALS(repeat_result.debug_reason, std::string("dialogue_cooldown"));
	}

	void RepeatedBotTargetedSayDuringCooldownShowsNoReply()
	{
		ResetRules();
		RuleManager::Instance()->SetRule("Chat:FallbackDialogueEnabled", "true");
		RuleManager::Instance()->SetRule("Chat:FallbackDialogueCooldownSeconds", "30");

		const FallbackDialogue::TargetedSayRequest request{
			.speaker_id = 1,
			.target_id = 2,
			.message = "hail",
			.target_type = FallbackDialogue::TargetType::Bot,
			.authored_dialogue_handled = false
		};

		const auto first_result = FallbackDialogue::HandleTargetedSay(request);
		TEST_ASSERT(first_result.handled);

		const auto repeat_result = FallbackDialogue::HandleTargetedSay(request);
		TEST_ASSERT(!repeat_result.handled);
		TEST_ASSERT(repeat_result.output_type == FallbackDialogue::OutputType::None);
		TEST_ASSERT(repeat_result.message.empty());
		TEST_ASSERT_EQUALS(repeat_result.debug_reason, std::string("dialogue_cooldown"));
	}

	void DifferentTargetedSayBySameSpeakerBypassesCooldown()
	{
		ResetRules();
		RuleManager::Instance()->SetRule("Chat:FallbackDialogueEnabled", "true");
		RuleManager::Instance()->SetRule("Chat:FallbackDialogueCooldownSeconds", "30");

		const FallbackDialogue::TargetedSayRequest first_target{
			.speaker_id = 1,
			.target_id = 2,
			.message = "hail",
			.target_type = FallbackDialogue::TargetType::NPC,
			.authored_dialogue_handled = false
		};
		const FallbackDialogue::TargetedSayRequest second_target{
			.speaker_id = 1,
			.target_id = 3,
			.message = "hail",
			.target_type = FallbackDialogue::TargetType::NPC,
			.authored_dialogue_handled = false
		};

		const auto first_result = FallbackDialogue::HandleTargetedSay(first_target);
		TEST_ASSERT(first_result.handled);

		const auto second_result = FallbackDialogue::HandleTargetedSay(second_target);
		TEST_ASSERT(second_result.handled);
		TEST_ASSERT(second_result.output_type == FallbackDialogue::OutputType::Emote);
	}

	void TargetedSayAfterCooldownExpiresReturnsUnavailableReply()
	{
		ResetRules();
		RuleManager::Instance()->SetRule("Chat:FallbackDialogueEnabled", "true");
		RuleManager::Instance()->SetRule("Chat:FallbackDialogueCooldownSeconds", "30");

		const FallbackDialogue::TargetedSayRequest request{
			.speaker_id = 1,
			.target_id = 2,
			.message = "hail",
			.target_type = FallbackDialogue::TargetType::NPC,
			.authored_dialogue_handled = false
		};

		const auto first_result = FallbackDialogue::HandleTargetedSay(request);
		TEST_ASSERT(first_result.handled);

		Timer::RollForward(31);

		const auto expired_result = FallbackDialogue::HandleTargetedSay(request);
		TEST_ASSERT(expired_result.handled);
		TEST_ASSERT(expired_result.output_type == FallbackDialogue::OutputType::Emote);
	}

	void AuthoredNpcDialogueSuppressesUnavailableReply()
	{
		ResetRules();
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

	void AuthoredBotDialogueSuppressesUnavailableReply()
	{
		ResetRules();
		RuleManager::Instance()->SetRule("Chat:FallbackDialogueEnabled", "true");

		const FallbackDialogue::TargetedSayRequest request{
			.speaker_id = 1,
			.target_id = 2,
			.message = "hail",
			.target_type = FallbackDialogue::TargetType::Bot,
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
		ResetRules();
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
		ResetRules();
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
		ResetRules();
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

	void PublicGameplayContextIncludesAllowedFields()
	{
		ResetRules();

		const auto context = FallbackDialogue::BuildPublicGameplayContext({
			.current_message = "hail friend",
			.speaker = PublicEntity("Aten", FallbackDialogue::EntityKind::Player, 12, 0.0f, 0.0f, 0.0f),
			.target = PublicEntity("Guard Teren", FallbackDialogue::EntityKind::NPC, 22, 5.0f, 0.0f, 0.0f),
			.zone = PublicZone("qeynos", "South Qeynos"),
			.nearby_entities = {
				PublicEntity("Merchant Bren", FallbackDialogue::EntityKind::NPC, 18, 10.0f, 0.0f, 0.0f),
				PublicEntity("Atenbot", FallbackDialogue::EntityKind::Bot, 12, 15.0f, 0.0f, 0.0f)
			}
		});

		TEST_ASSERT_EQUALS(context.current_message, std::string("hail friend"));
		TEST_ASSERT_EQUALS(context.speaker.name, std::string("Aten"));
		TEST_ASSERT(context.speaker.kind == FallbackDialogue::EntityKind::Player);
		TEST_ASSERT_EQUALS(context.speaker.level, 12);
		TEST_ASSERT_EQUALS(context.target.name, std::string("Guard Teren"));
		TEST_ASSERT(context.target.kind == FallbackDialogue::EntityKind::NPC);
		TEST_ASSERT_EQUALS(context.zone.short_name, std::string("qeynos"));
		TEST_ASSERT_EQUALS(context.zone.long_name, std::string("South Qeynos"));
		TEST_ASSERT_EQUALS(context.nearby_entities.size(), static_cast<size_t>(2));
		TEST_ASSERT_EQUALS(context.nearby_entities[0].name, std::string("Merchant Bren"));
		TEST_ASSERT_EQUALS(context.nearby_entities[1].name, std::string("Atenbot"));
	}

	void PublicGameplayContextExcludesPrivateFields()
	{
		ResetRules();

		auto speaker = PublicEntity("Aten", FallbackDialogue::EntityKind::Player, 12, 0.0f, 0.0f, 0.0f);
		speaker.account_name = "private_account_name";
		speaker.ip_address = "192.0.2.55";
		speaker.private_chat = "private_tell_payload";
		speaker.inventory_summary = "bag_of_private_items";
		speaker.raw_quest_globals = "raw_global_state";
		speaker.account_id = 9001;
		speaker.character_id = 8001;
		speaker.gm_status = true;

		auto target = PublicEntity("Guard Teren", FallbackDialogue::EntityKind::NPC, 22, 5.0f, 0.0f, 0.0f);
		target.raw_quest_globals = "target_raw_globals";

		auto zone = PublicZone("qeynos", "South Qeynos");
		zone.zone_id = 1;
		zone.instance_id = 2;
		zone.database_credentials = "db_password_secret";

		const auto context = FallbackDialogue::BuildPublicGameplayContext({
			.current_message = "hail friend",
			.speaker = speaker,
			.target = target,
			.zone = zone
		});

		const auto serialized = SerializePublicContext(context);
		TEST_ASSERT(serialized.find("Aten") != std::string::npos);
		TEST_ASSERT(serialized.find("Guard Teren") != std::string::npos);
		TEST_ASSERT(serialized.find("private_account_name") == std::string::npos);
		TEST_ASSERT(serialized.find("192.0.2.55") == std::string::npos);
		TEST_ASSERT(serialized.find("private_tell_payload") == std::string::npos);
		TEST_ASSERT(serialized.find("bag_of_private_items") == std::string::npos);
		TEST_ASSERT(serialized.find("raw_global_state") == std::string::npos);
		TEST_ASSERT(serialized.find("target_raw_globals") == std::string::npos);
		TEST_ASSERT(serialized.find("db_password_secret") == std::string::npos);
		TEST_ASSERT(serialized.find("9001") == std::string::npos);
		TEST_ASSERT(serialized.find("8001") == std::string::npos);
	}

	void PublicGameplayContextFiltersNearbyEntitiesByRuleRadius()
	{
		ResetRules();
		RuleManager::Instance()->SetRule("Chat:FallbackDialogueNearbyContextRadius", "20");

		const auto context = FallbackDialogue::BuildPublicGameplayContext({
			.current_message = "hail",
			.speaker = PublicEntity("Aten", FallbackDialogue::EntityKind::Player, 12, 0.0f, 0.0f, 0.0f),
			.target = PublicEntity("Guard Teren", FallbackDialogue::EntityKind::NPC, 22, 5.0f, 0.0f, 0.0f),
			.zone = PublicZone("qeynos", "South Qeynos"),
			.nearby_entities = {
				PublicEntity("Nearby Merchant", FallbackDialogue::EntityKind::NPC, 18, 10.0f, 0.0f, 0.0f),
				PublicEntity("Distant Guard", FallbackDialogue::EntityKind::NPC, 30, 25.0f, 0.0f, 0.0f)
			}
		});

		TEST_ASSERT_EQUALS(context.nearby_entities.size(), static_cast<size_t>(1));
		TEST_ASSERT_EQUALS(context.nearby_entities[0].name, std::string("Nearby Merchant"));
	}

	void PublicGameplayContextLimitsNearbyEntitiesByRuleCount()
	{
		ResetRules();
		RuleManager::Instance()->SetRule("Chat:FallbackDialogueNearbyContextRadius", "100");
		RuleManager::Instance()->SetRule("Chat:FallbackDialogueNearbyEntityLimit", "2");

		const auto context = FallbackDialogue::BuildPublicGameplayContext({
			.current_message = "hail",
			.speaker = PublicEntity("Aten", FallbackDialogue::EntityKind::Player, 12, 0.0f, 0.0f, 0.0f),
			.target = PublicEntity("Guard Teren", FallbackDialogue::EntityKind::NPC, 22, 5.0f, 0.0f, 0.0f),
			.zone = PublicZone("qeynos", "South Qeynos"),
			.nearby_entities = {
				PublicEntity("Third Closest", FallbackDialogue::EntityKind::NPC, 18, 30.0f, 0.0f, 0.0f),
				PublicEntity("Closest", FallbackDialogue::EntityKind::NPC, 18, 10.0f, 0.0f, 0.0f),
				PublicEntity("Second Closest", FallbackDialogue::EntityKind::Bot, 12, 20.0f, 0.0f, 0.0f)
			}
		});

		TEST_ASSERT_EQUALS(context.nearby_entities.size(), static_cast<size_t>(2));
		TEST_ASSERT_EQUALS(context.nearby_entities[0].name, std::string("Closest"));
		TEST_ASSERT_EQUALS(context.nearby_entities[1].name, std::string("Second Closest"));
	}

	void PublicGameplayContextExcludesSpeakerAndTargetFromNearbyEntities()
	{
		ResetRules();

		auto speaker = PublicEntity("Aten", FallbackDialogue::EntityKind::Player, 12, 0.0f, 0.0f, 0.0f);
		speaker.entity_id = 101;
		auto target = PublicEntity("Guard Teren", FallbackDialogue::EntityKind::NPC, 22, 5.0f, 0.0f, 0.0f);
		target.entity_id = 202;
		auto nearby_speaker = speaker;
		auto nearby_target = target;

		const auto context = FallbackDialogue::BuildPublicGameplayContext({
			.current_message = "hail",
			.speaker = speaker,
			.target = target,
			.zone = PublicZone("qeynos", "South Qeynos"),
			.nearby_entities = {
				nearby_speaker,
				PublicEntity("Dockhand", FallbackDialogue::EntityKind::NPC, 8, 10.0f, 0.0f, 0.0f),
				nearby_target
			}
		});

		TEST_ASSERT_EQUALS(context.nearby_entities.size(), static_cast<size_t>(1));
		TEST_ASSERT_EQUALS(context.nearby_entities[0].name, std::string("Dockhand"));
	}

	void EligibleNpcTargetedSayQueuesDelayedRequestWithPublicContext()
	{
		ResetRules();
		RuleManager::Instance()->SetRule("Chat:FallbackDialogueEnabled", "true");

		FallbackDialogue::TestDelayedDialogueProvider provider;
		FallbackDialogue::DelayedDialogueQueue queue(provider);
		FallbackDialogue::LiveContext live_context{
			.current_message = "hail captain",
			.speaker = PublicEntity("Aten", FallbackDialogue::EntityKind::Player, 12, 0.0f, 0.0f, 0.0f),
			.target = PublicEntity("Captain Rohand", FallbackDialogue::EntityKind::NPC, 35, 8.0f, 0.0f, 0.0f),
			.zone = PublicZone("qeynos", "South Qeynos"),
			.nearby_entities = {
				PublicEntity("Dockhand", FallbackDialogue::EntityKind::NPC, 8, 12.0f, 0.0f, 0.0f)
			}
		};
		const FallbackDialogue::TargetedSayRequest request{
			.speaker_id = 101,
			.target_id = 202,
			.message = "hail captain",
			.target_type = FallbackDialogue::TargetType::NPC,
			.authored_dialogue_handled = false
		};

		const auto result = queue.HandleTargetedSay(request, live_context);
		live_context.target.name = "Changed Target";

		TEST_ASSERT(result.handled);
		TEST_ASSERT(result.output_type == FallbackDialogue::OutputType::None);
		TEST_ASSERT_EQUALS(result.debug_reason, std::string("delayed_dialogue_queued"));
		TEST_ASSERT_EQUALS(provider.PendingRequests().size(), static_cast<size_t>(1));
		TEST_ASSERT_EQUALS(provider.PendingRequests()[0].speaker_id, static_cast<uint32_t>(101));
		TEST_ASSERT_EQUALS(provider.PendingRequests()[0].target_id, static_cast<uint32_t>(202));
		TEST_ASSERT(provider.PendingRequests()[0].target_type == FallbackDialogue::TargetType::NPC);
		TEST_ASSERT_EQUALS(
			provider.PendingRequests()[0].context.current_message,
			std::string("hail captain")
		);
		TEST_ASSERT_EQUALS(
			provider.PendingRequests()[0].context.target.name,
			std::string("Captain Rohand")
		);

		FallbackDialogue::TargetedSayResult ready_result;
		TEST_ASSERT(!queue.PopReadyResult(CurrentInteractionFor(101, 202, 202), ready_result));
	}

	void CompletedDelayedDialogueReturnsTargetSpeech()
	{
		ResetRules();
		RuleManager::Instance()->SetRule("Chat:FallbackDialogueEnabled", "true");

		FallbackDialogue::TestDelayedDialogueProvider provider;
		FallbackDialogue::DelayedDialogueQueue queue(provider);
		const FallbackDialogue::TargetedSayRequest request{
			.speaker_id = 101,
			.target_id = 202,
			.message = "hail",
			.target_type = FallbackDialogue::TargetType::Bot,
			.authored_dialogue_handled = false
		};
		const FallbackDialogue::LiveContext live_context{
			.current_message = "hail",
			.speaker = PublicEntity("Aten", FallbackDialogue::EntityKind::Player, 12, 0.0f, 0.0f, 0.0f),
			.target = PublicEntity("Atenbot", FallbackDialogue::EntityKind::Bot, 12, 5.0f, 0.0f, 0.0f),
			.zone = PublicZone("qeynos", "South Qeynos")
		};

		const auto queued_result = queue.HandleTargetedSay(request, live_context);
		TEST_ASSERT(queued_result.handled);
		TEST_ASSERT(provider.CompleteNextSuccess("Good day to you."));

		FallbackDialogue::TargetedSayResult ready_result;
		TEST_ASSERT(queue.PopReadyResult(CurrentInteractionFor(
			101,
			202,
			202,
			PublicEntity("Aten", FallbackDialogue::EntityKind::Player, 12, 0.0f, 0.0f, 0.0f),
			PublicEntity("Atenbot", FallbackDialogue::EntityKind::Bot, 12, 5.0f, 0.0f, 0.0f)
		), ready_result));
		TEST_ASSERT(ready_result.output_type == FallbackDialogue::OutputType::Say);
		TEST_ASSERT_EQUALS(ready_result.message, std::string("Good day to you."));
		TEST_ASSERT_EQUALS(ready_result.speaker_id, static_cast<uint32_t>(101));
		TEST_ASSERT_EQUALS(ready_result.target_id, static_cast<uint32_t>(202));
		TEST_ASSERT_EQUALS(ready_result.visible_speaker_id, static_cast<uint32_t>(202));
		TEST_ASSERT(ready_result.target_type == FallbackDialogue::TargetType::Bot);
		TEST_ASSERT_EQUALS(ready_result.debug_reason, std::string("delayed_dialogue_ready"));
	}

	void CompletedDelayedDialogueStripsNewlines()
	{
		ResetRules();
		RuleManager::Instance()->SetRule("Chat:FallbackDialogueEnabled", "true");

		FallbackDialogue::TestDelayedDialogueProvider provider;
		FallbackDialogue::DelayedDialogueQueue queue(provider);
		const FallbackDialogue::TargetedSayRequest request{
			.speaker_id = 101,
			.target_id = 202,
			.message = "hail",
			.target_type = FallbackDialogue::TargetType::NPC,
			.authored_dialogue_handled = false
		};
		const FallbackDialogue::LiveContext live_context{
			.current_message = "hail",
			.speaker = PublicEntity("Aten", FallbackDialogue::EntityKind::Player, 12, 0.0f, 0.0f, 0.0f),
			.target = PublicEntity("Guard Teren", FallbackDialogue::EntityKind::NPC, 22, 5.0f, 0.0f, 0.0f),
			.zone = PublicZone("qeynos", "South Qeynos")
		};

		const auto queued_result = queue.HandleTargetedSay(request, live_context);
		TEST_ASSERT(queued_result.handled);
		TEST_ASSERT(provider.CompleteNextSuccess("Well met.\nStay awhile.\r\nTell me of Qeynos."));

		FallbackDialogue::TargetedSayResult ready_result;
		TEST_ASSERT(queue.PopReadyResult(CurrentInteractionFor(
			101,
			202,
			202,
			PublicEntity("Aten", FallbackDialogue::EntityKind::Player, 12, 0.0f, 0.0f, 0.0f),
			PublicEntity("Guard Teren", FallbackDialogue::EntityKind::NPC, 22, 5.0f, 0.0f, 0.0f)
		), ready_result));
		TEST_ASSERT(ready_result.output_type == FallbackDialogue::OutputType::Say);
		TEST_ASSERT_EQUALS(
			ready_result.message,
			std::string("Well met. Stay awhile. Tell me of Qeynos.")
		);
	}

	void CompletedDelayedDialogueTrimsModelArtifacts()
	{
		ResetRules();
		RuleManager::Instance()->SetRule("Chat:FallbackDialogueEnabled", "true");

		FallbackDialogue::TestDelayedDialogueProvider provider;
		FallbackDialogue::DelayedDialogueQueue queue(provider);
		const FallbackDialogue::TargetedSayRequest request{
			.speaker_id = 101,
			.target_id = 202,
			.message = "hail",
			.target_type = FallbackDialogue::TargetType::NPC,
			.authored_dialogue_handled = false
		};
		const FallbackDialogue::LiveContext live_context{
			.current_message = "hail",
			.speaker = PublicEntity("Aten", FallbackDialogue::EntityKind::Player, 12, 0.0f, 0.0f, 0.0f),
			.target = PublicEntity("Guard Teren", FallbackDialogue::EntityKind::NPC, 22, 5.0f, 0.0f, 0.0f),
			.zone = PublicZone("qeynos", "South Qeynos")
		};

		const auto queued_result = queue.HandleTargetedSay(request, live_context);
		TEST_ASSERT(queued_result.handled);
		TEST_ASSERT(provider.CompleteNextSuccess(" Assistant: \"Mind the road, friend.\" "));

		FallbackDialogue::TargetedSayResult ready_result;
		TEST_ASSERT(queue.PopReadyResult(CurrentInteractionFor(
			101,
			202,
			202,
			PublicEntity("Aten", FallbackDialogue::EntityKind::Player, 12, 0.0f, 0.0f, 0.0f),
			PublicEntity("Guard Teren", FallbackDialogue::EntityKind::NPC, 22, 5.0f, 0.0f, 0.0f)
		), ready_result));
		TEST_ASSERT(ready_result.output_type == FallbackDialogue::OutputType::Say);
		TEST_ASSERT_EQUALS(ready_result.message, std::string("Mind the road, friend."));
	}

	void CompletedDelayedDialogueReturnsPureAsteriskActionAsTargetEmote()
	{
		ResetRules();
		RuleManager::Instance()->SetRule("Chat:FallbackDialogueEnabled", "true");

		FallbackDialogue::TestDelayedDialogueProvider provider;
		FallbackDialogue::DelayedDialogueQueue queue(provider);
		const FallbackDialogue::TargetedSayRequest request{
			.speaker_id = 101,
			.target_id = 202,
			.message = "hail",
			.target_type = FallbackDialogue::TargetType::NPC,
			.authored_dialogue_handled = false
		};
		const FallbackDialogue::LiveContext live_context{
			.current_message = "hail",
			.speaker = PublicEntity("Aten", FallbackDialogue::EntityKind::Player, 12, 0.0f, 0.0f, 0.0f),
			.target = PublicEntity("Guard Teren", FallbackDialogue::EntityKind::NPC, 22, 5.0f, 0.0f, 0.0f),
			.zone = PublicZone("qeynos", "South Qeynos")
		};

		TEST_ASSERT(queue.HandleTargetedSay(request, live_context).handled);
		TEST_ASSERT(provider.CompleteNextSuccess("*looks around warily*"));

		FallbackDialogue::TargetedSayResult ready_result;
		TEST_ASSERT(queue.PopReadyResult(CurrentInteractionFor(
			101,
			202,
			202,
			PublicEntity("Aten", FallbackDialogue::EntityKind::Player, 12, 0.0f, 0.0f, 0.0f),
			PublicEntity("Guard Teren", FallbackDialogue::EntityKind::NPC, 22, 5.0f, 0.0f, 0.0f)
		), ready_result));
		TEST_ASSERT(ready_result.output_type == FallbackDialogue::OutputType::Emote);
		TEST_ASSERT_EQUALS(ready_result.message, std::string("looks around warily"));
		TEST_ASSERT_EQUALS(ready_result.visible_speaker_id, static_cast<uint32_t>(202));
		TEST_ASSERT_EQUALS(ready_result.debug_reason, std::string("delayed_dialogue_ready"));
	}

	void CompletedDelayedDialoguePreservesMixedSpeechAndEmoteOrder()
	{
		ResetRules();
		RuleManager::Instance()->SetRule("Chat:FallbackDialogueEnabled", "true");

		FallbackDialogue::TestDelayedDialogueProvider provider;
		FallbackDialogue::DelayedDialogueQueue queue(provider);
		const FallbackDialogue::TargetedSayRequest request{
			.speaker_id = 101,
			.target_id = 202,
			.message = "hail",
			.target_type = FallbackDialogue::TargetType::NPC,
			.authored_dialogue_handled = false
		};
		const FallbackDialogue::LiveContext live_context{
			.current_message = "hail",
			.speaker = PublicEntity("Aten", FallbackDialogue::EntityKind::Player, 12, 0.0f, 0.0f, 0.0f),
			.target = PublicEntity("Guard Teren", FallbackDialogue::EntityKind::NPC, 22, 5.0f, 0.0f, 0.0f),
			.zone = PublicZone("qeynos", "South Qeynos")
		};

		TEST_ASSERT(queue.HandleTargetedSay(request, live_context).handled);
		TEST_ASSERT(provider.CompleteNextSuccess("Well met. *looks around warily* Keep your voice low."));

		FallbackDialogue::TargetedSayResult first_result;
		TEST_ASSERT(queue.PopReadyResult(CurrentInteractionFor(101, 202, 202), first_result));
		TEST_ASSERT(first_result.output_type == FallbackDialogue::OutputType::Say);
		TEST_ASSERT_EQUALS(first_result.message, std::string("Well met."));

		FallbackDialogue::TargetedSayResult second_result;
		TEST_ASSERT(queue.PopReadyResult(CurrentInteractionFor(101, 202, 202), second_result));
		TEST_ASSERT(second_result.output_type == FallbackDialogue::OutputType::Emote);
		TEST_ASSERT_EQUALS(second_result.message, std::string("looks around warily"));

		FallbackDialogue::TargetedSayResult third_result;
		TEST_ASSERT(queue.PopReadyResult(CurrentInteractionFor(101, 202, 202), third_result));
		TEST_ASSERT(third_result.output_type == FallbackDialogue::OutputType::Say);
		TEST_ASSERT_EQUALS(third_result.message, std::string("Keep your voice low."));

		FallbackDialogue::TargetedSayResult no_result;
		TEST_ASSERT(!queue.PopReadyResult(CurrentInteractionFor(101, 202, 202), no_result));
	}

	void CompletedBotDelayedDialogueReturnsParenthesizedActionAsTargetEmote()
	{
		ResetRules();
		RuleManager::Instance()->SetRule("Chat:FallbackDialogueEnabled", "true");

		FallbackDialogue::TestDelayedDialogueProvider provider;
		FallbackDialogue::DelayedDialogueQueue queue(provider);
		const FallbackDialogue::TargetedSayRequest request{
			.speaker_id = 101,
			.target_id = 202,
			.message = "hail",
			.target_type = FallbackDialogue::TargetType::Bot,
			.authored_dialogue_handled = false
		};
		const FallbackDialogue::LiveContext live_context{
			.current_message = "hail",
			.speaker = PublicEntity("Aten", FallbackDialogue::EntityKind::Player, 12, 0.0f, 0.0f, 0.0f),
			.target = PublicEntity("Atenbot", FallbackDialogue::EntityKind::Bot, 12, 5.0f, 0.0f, 0.0f),
			.zone = PublicZone("qeynos", "South Qeynos")
		};

		TEST_ASSERT(queue.HandleTargetedSay(request, live_context).handled);
		TEST_ASSERT(provider.CompleteNextSuccess("(checks bowstring)"));

		FallbackDialogue::TargetedSayResult ready_result;
		TEST_ASSERT(queue.PopReadyResult(CurrentInteractionFor(
			101,
			202,
			202,
			PublicEntity("Aten", FallbackDialogue::EntityKind::Player, 12, 0.0f, 0.0f, 0.0f),
			PublicEntity("Atenbot", FallbackDialogue::EntityKind::Bot, 12, 5.0f, 0.0f, 0.0f)
		), ready_result));
		TEST_ASSERT(ready_result.output_type == FallbackDialogue::OutputType::Emote);
		TEST_ASSERT_EQUALS(ready_result.message, std::string("checks bowstring"));
		TEST_ASSERT_EQUALS(ready_result.visible_speaker_id, static_cast<uint32_t>(202));
		TEST_ASSERT(ready_result.target_type == FallbackDialogue::TargetType::Bot);
	}

	void MalformedEmoteMarkerRemainsTargetSpeech()
	{
		const auto ready_result = DelayedSuccessResultFor(
			"Well met. *looks around warily",
			"appears distracted."
		);

		TEST_ASSERT(ready_result.handled);
		TEST_ASSERT(ready_result.output_type == FallbackDialogue::OutputType::Say);
		TEST_ASSERT_EQUALS(ready_result.message, std::string("Well met. *looks around warily"));
		TEST_ASSERT_EQUALS(ready_result.debug_reason, std::string("delayed_dialogue_ready"));
	}

	void CompletedDelayedDialogueSplitsLongLineAtSentenceBoundary()
	{
		ResetRules();
		RuleManager::Instance()->SetRule("Chat:FallbackDialogueEnabled", "true");
		RuleManager::Instance()->SetRule("Chat:FallbackDialogueMaxLineLength", "24");

		FallbackDialogue::TestDelayedDialogueProvider provider;
		FallbackDialogue::DelayedDialogueQueue queue(provider);
		const FallbackDialogue::TargetedSayRequest request{
			.speaker_id = 101,
			.target_id = 202,
			.message = "hail",
			.target_type = FallbackDialogue::TargetType::NPC,
			.authored_dialogue_handled = false
		};
		const FallbackDialogue::LiveContext live_context{
			.current_message = "hail",
			.speaker = PublicEntity("Aten", FallbackDialogue::EntityKind::Player, 12, 0.0f, 0.0f, 0.0f),
			.target = PublicEntity("Guard Teren", FallbackDialogue::EntityKind::NPC, 22, 5.0f, 0.0f, 0.0f),
			.zone = PublicZone("qeynos", "South Qeynos")
		};

		const auto queued_result = queue.HandleTargetedSay(request, live_context);
		TEST_ASSERT(queued_result.handled);
		TEST_ASSERT(provider.CompleteNextSuccess("First sentence. Second sentence keeps going."));

		FallbackDialogue::TargetedSayResult ready_result;
		TEST_ASSERT(queue.PopReadyResult(CurrentInteractionFor(
			101,
			202,
			202,
			PublicEntity("Aten", FallbackDialogue::EntityKind::Player, 12, 0.0f, 0.0f, 0.0f),
			PublicEntity("Guard Teren", FallbackDialogue::EntityKind::NPC, 22, 5.0f, 0.0f, 0.0f)
		), ready_result));
		TEST_ASSERT(ready_result.output_type == FallbackDialogue::OutputType::Say);
		TEST_ASSERT_EQUALS(ready_result.message, std::string("First sentence."));
		TEST_ASSERT(ready_result.message.size() <= 24);

		FallbackDialogue::TargetedSayResult second_result;
		TEST_ASSERT(queue.PopReadyResult(CurrentInteractionFor(
			101,
			202,
			202,
			PublicEntity("Aten", FallbackDialogue::EntityKind::Player, 12, 0.0f, 0.0f, 0.0f),
			PublicEntity("Guard Teren", FallbackDialogue::EntityKind::NPC, 22, 5.0f, 0.0f, 0.0f)
		), second_result));
		TEST_ASSERT(second_result.output_type == FallbackDialogue::OutputType::Say);
		TEST_ASSERT_EQUALS(second_result.message, std::string("Second sentence keeps"));
		TEST_ASSERT(second_result.message.size() <= 24);

		FallbackDialogue::TargetedSayResult third_result;
		TEST_ASSERT(queue.PopReadyResult(CurrentInteractionFor(
			101,
			202,
			202,
			PublicEntity("Aten", FallbackDialogue::EntityKind::Player, 12, 0.0f, 0.0f, 0.0f),
			PublicEntity("Guard Teren", FallbackDialogue::EntityKind::NPC, 22, 5.0f, 0.0f, 0.0f)
		), third_result));
		TEST_ASSERT(third_result.output_type == FallbackDialogue::OutputType::Say);
		TEST_ASSERT_EQUALS(third_result.message, std::string("going."));
		TEST_ASSERT(third_result.message.size() <= 24);

		FallbackDialogue::TargetedSayResult no_result;
		TEST_ASSERT(!queue.PopReadyResult(CurrentInteractionFor(101, 202, 202), no_result));
	}

	void CompletedDelayedDialogueSplitsLongWordAtLineLimit()
	{
		ResetRules();
		RuleManager::Instance()->SetRule("Chat:FallbackDialogueEnabled", "true");
		RuleManager::Instance()->SetRule("Chat:FallbackDialogueMaxLineLength", "8");

		FallbackDialogue::TestDelayedDialogueProvider provider;
		FallbackDialogue::DelayedDialogueQueue queue(provider);
		const FallbackDialogue::TargetedSayRequest request{
			.speaker_id = 101,
			.target_id = 202,
			.message = "hail",
			.target_type = FallbackDialogue::TargetType::NPC,
			.authored_dialogue_handled = false
		};
		const FallbackDialogue::LiveContext live_context{
			.current_message = "hail",
			.speaker = PublicEntity("Aten", FallbackDialogue::EntityKind::Player, 12, 0.0f, 0.0f, 0.0f),
			.target = PublicEntity("Guard Teren", FallbackDialogue::EntityKind::NPC, 22, 5.0f, 0.0f, 0.0f),
			.zone = PublicZone("qeynos", "South Qeynos")
		};

		TEST_ASSERT(queue.HandleTargetedSay(request, live_context).handled);
		TEST_ASSERT(provider.CompleteNextSuccess("supercalifragilistic"));

		FallbackDialogue::TargetedSayResult first_result;
		TEST_ASSERT(queue.PopReadyResult(CurrentInteractionFor(
			101,
			202,
			202,
			PublicEntity("Aten", FallbackDialogue::EntityKind::Player, 12, 0.0f, 0.0f, 0.0f),
			PublicEntity("Guard Teren", FallbackDialogue::EntityKind::NPC, 22, 5.0f, 0.0f, 0.0f)
		), first_result));
		TEST_ASSERT_EQUALS(first_result.message, std::string("supercal"));
		TEST_ASSERT(first_result.message.size() <= 8);

		FallbackDialogue::TargetedSayResult second_result;
		TEST_ASSERT(queue.PopReadyResult(CurrentInteractionFor(
			101,
			202,
			202,
			PublicEntity("Aten", FallbackDialogue::EntityKind::Player, 12, 0.0f, 0.0f, 0.0f),
			PublicEntity("Guard Teren", FallbackDialogue::EntityKind::NPC, 22, 5.0f, 0.0f, 0.0f)
		), second_result));
		TEST_ASSERT_EQUALS(second_result.message, std::string("ifragili"));
		TEST_ASSERT(second_result.message.size() <= 8);

		FallbackDialogue::TargetedSayResult third_result;
		TEST_ASSERT(queue.PopReadyResult(CurrentInteractionFor(
			101,
			202,
			202,
			PublicEntity("Aten", FallbackDialogue::EntityKind::Player, 12, 0.0f, 0.0f, 0.0f),
			PublicEntity("Guard Teren", FallbackDialogue::EntityKind::NPC, 22, 5.0f, 0.0f, 0.0f)
		), third_result));
		TEST_ASSERT_EQUALS(third_result.message, std::string("stic"));
		TEST_ASSERT(third_result.message.size() <= 8);
	}

	void CompletedBotDelayedDialogueSplitsLongLineInOrder()
	{
		ResetRules();
		RuleManager::Instance()->SetRule("Chat:FallbackDialogueEnabled", "true");
		RuleManager::Instance()->SetRule("Chat:FallbackDialogueMaxLineLength", "16");

		FallbackDialogue::TestDelayedDialogueProvider provider;
		FallbackDialogue::DelayedDialogueQueue queue(provider);
		const FallbackDialogue::TargetedSayRequest request{
			.speaker_id = 101,
			.target_id = 202,
			.message = "hail",
			.target_type = FallbackDialogue::TargetType::Bot,
			.authored_dialogue_handled = false
		};
		const FallbackDialogue::LiveContext live_context{
			.current_message = "hail",
			.speaker = PublicEntity("Aten", FallbackDialogue::EntityKind::Player, 12, 0.0f, 0.0f, 0.0f),
			.target = PublicEntity("Atenbot", FallbackDialogue::EntityKind::Bot, 12, 5.0f, 0.0f, 0.0f),
			.zone = PublicZone("qeynos", "South Qeynos")
		};

		TEST_ASSERT(queue.HandleTargetedSay(request, live_context).handled);
		TEST_ASSERT(provider.CompleteNextSuccess("Alpha beta gamma delta."));

		FallbackDialogue::TargetedSayResult first_result;
		TEST_ASSERT(queue.PopReadyResult(CurrentInteractionFor(
			101,
			202,
			202,
			PublicEntity("Aten", FallbackDialogue::EntityKind::Player, 12, 0.0f, 0.0f, 0.0f),
			PublicEntity("Atenbot", FallbackDialogue::EntityKind::Bot, 12, 5.0f, 0.0f, 0.0f)
		), first_result));
		TEST_ASSERT(first_result.output_type == FallbackDialogue::OutputType::Say);
		TEST_ASSERT_EQUALS(first_result.message, std::string("Alpha beta"));
		TEST_ASSERT(first_result.target_type == FallbackDialogue::TargetType::Bot);

		FallbackDialogue::TargetedSayResult second_result;
		TEST_ASSERT(queue.PopReadyResult(CurrentInteractionFor(
			101,
			202,
			202,
			PublicEntity("Aten", FallbackDialogue::EntityKind::Player, 12, 0.0f, 0.0f, 0.0f),
			PublicEntity("Atenbot", FallbackDialogue::EntityKind::Bot, 12, 5.0f, 0.0f, 0.0f)
		), second_result));
		TEST_ASSERT(second_result.output_type == FallbackDialogue::OutputType::Say);
		TEST_ASSERT_EQUALS(second_result.message, std::string("gamma delta."));
		TEST_ASSERT(second_result.target_type == FallbackDialogue::TargetType::Bot);
	}

	void CommandLookingDelayedDialogueFallsBackToUnavailableReply()
	{
		const auto ready_result = DelayedSuccessResultFor("/say follow me", "seems lost in thought.");

		TEST_ASSERT(ready_result.handled);
		TEST_ASSERT(ready_result.output_type == FallbackDialogue::OutputType::Emote);
		TEST_ASSERT_EQUALS(ready_result.message, std::string("seems lost in thought."));
		TEST_ASSERT_EQUALS(ready_result.debug_reason, std::string("delayed_dialogue_rejected"));
	}

	void CommandLookingEmoteFragmentFallsBackToUnavailableReply()
	{
		const auto ready_result = DelayedSuccessResultFor(
			"Well met. */say follow me*",
			"seems lost in thought."
		);

		TEST_ASSERT(ready_result.handled);
		TEST_ASSERT(ready_result.output_type == FallbackDialogue::OutputType::Emote);
		TEST_ASSERT_EQUALS(ready_result.message, std::string("seems lost in thought."));
		TEST_ASSERT_EQUALS(ready_result.debug_reason, std::string("delayed_dialogue_rejected"));
	}

	void MetadataLookingDelayedDialogueFallsBackToUnavailableReply()
	{
		const auto ready_result = DelayedSuccessResultFor("[metadata: target=Guard Teren]", "appears distracted.");

		TEST_ASSERT(ready_result.handled);
		TEST_ASSERT(ready_result.output_type == FallbackDialogue::OutputType::Emote);
		TEST_ASSERT_EQUALS(ready_result.message, std::string("appears distracted."));
		TEST_ASSERT_EQUALS(ready_result.debug_reason, std::string("delayed_dialogue_rejected"));
	}

	void TechnicalLookingDelayedDialogueFallsBackToUnavailableReply()
	{
		const auto ready_result = DelayedSuccessResultFor("HTTP 500: provider timeout", "looks confused.");

		TEST_ASSERT(ready_result.handled);
		TEST_ASSERT(ready_result.output_type == FallbackDialogue::OutputType::Emote);
		TEST_ASSERT_EQUALS(ready_result.message, std::string("looks confused."));
		TEST_ASSERT_EQUALS(ready_result.debug_reason, std::string("delayed_dialogue_rejected"));
	}

	void OutOfCharacterDelayedDialogueFallsBackToUnavailableReply()
	{
		const auto ready_result = DelayedSuccessResultFor(
			"As an AI, I cannot roleplay this NPC.",
			"seems distracted."
		);

		TEST_ASSERT(ready_result.handled);
		TEST_ASSERT(ready_result.output_type == FallbackDialogue::OutputType::Emote);
		TEST_ASSERT_EQUALS(ready_result.message, std::string("seems distracted."));
		TEST_ASSERT_EQUALS(ready_result.debug_reason, std::string("delayed_dialogue_rejected"));
	}

	void EmptyDelayedDialogueFallsBackToUnavailableReply()
	{
		const auto ready_result = DelayedSuccessResultFor(" \n\t ", "appears distracted.");

		TEST_ASSERT(ready_result.handled);
		TEST_ASSERT(ready_result.output_type == FallbackDialogue::OutputType::Emote);
		TEST_ASSERT_EQUALS(ready_result.message, std::string("appears distracted."));
		TEST_ASSERT_EQUALS(ready_result.debug_reason, std::string("delayed_dialogue_rejected"));
	}

	void FailedDelayedDialogueReturnsUnavailableReplyEmote()
	{
		ResetRules();
		RuleManager::Instance()->SetRule("Chat:FallbackDialogueEnabled", "true");
		RuleManager::Instance()->SetRule("Chat:FallbackDialogueUnavailableReply", "seems lost in thought.");

		FallbackDialogue::TestDelayedDialogueProvider provider;
		FallbackDialogue::DelayedDialogueQueue queue(provider);
		const FallbackDialogue::TargetedSayRequest request{
			.speaker_id = 101,
			.target_id = 202,
			.message = "hail",
			.target_type = FallbackDialogue::TargetType::NPC,
			.authored_dialogue_handled = false
		};
		const FallbackDialogue::LiveContext live_context{
			.current_message = "hail",
			.speaker = PublicEntity("Aten", FallbackDialogue::EntityKind::Player, 12, 0.0f, 0.0f, 0.0f),
			.target = PublicEntity("Guard Teren", FallbackDialogue::EntityKind::NPC, 22, 5.0f, 0.0f, 0.0f),
			.zone = PublicZone("qeynos", "South Qeynos")
		};

		const auto queued_result = queue.HandleTargetedSay(request, live_context);
		TEST_ASSERT(queued_result.handled);
		TEST_ASSERT(provider.CompleteNextFailure());

		FallbackDialogue::TargetedSayResult ready_result;
		TEST_ASSERT(queue.PopReadyResult(CurrentInteractionFor(
			101,
			202,
			202,
			PublicEntity("Aten", FallbackDialogue::EntityKind::Player, 12, 0.0f, 0.0f, 0.0f),
			PublicEntity("Guard Teren", FallbackDialogue::EntityKind::NPC, 22, 5.0f, 0.0f, 0.0f)
		), ready_result));
		TEST_ASSERT(ready_result.output_type == FallbackDialogue::OutputType::Emote);
		TEST_ASSERT_EQUALS(ready_result.message, std::string("seems lost in thought."));
		TEST_ASSERT_EQUALS(ready_result.speaker_id, static_cast<uint32_t>(101));
		TEST_ASSERT_EQUALS(ready_result.target_id, static_cast<uint32_t>(202));
		TEST_ASSERT_EQUALS(ready_result.debug_reason, std::string("delayed_dialogue_unavailable"));
	}

	void CompletedDelayedDialogueReturnsTargetSpeechForCurrentInteraction()
	{
		ResetRules();
		RuleManager::Instance()->SetRule("Chat:FallbackDialogueEnabled", "true");
		RuleManager::Instance()->SetRule("Range:Say", "15");

		FallbackDialogue::TestDelayedDialogueProvider provider;
		FallbackDialogue::DelayedDialogueQueue queue(provider);
		const FallbackDialogue::TargetedSayRequest request{
			.speaker_id = 101,
			.target_id = 202,
			.message = "hail",
			.target_type = FallbackDialogue::TargetType::NPC,
			.authored_dialogue_handled = false
		};
		const FallbackDialogue::LiveContext live_context{
			.current_message = "hail",
			.speaker = PublicEntity("Aten", FallbackDialogue::EntityKind::Player, 12, 0.0f, 0.0f, 0.0f),
			.target = PublicEntity("Guard Teren", FallbackDialogue::EntityKind::NPC, 22, 10.0f, 0.0f, 5.0f),
			.zone = PublicZone("qeynos", "South Qeynos")
		};

		const auto queued_result = queue.HandleTargetedSay(request, live_context);
		TEST_ASSERT(queued_result.handled);
		TEST_ASSERT(provider.CompleteNextSuccess("Well met."));

		FallbackDialogue::TargetedSayResult ready_result;
		TEST_ASSERT(queue.PopReadyResult({
			.speaker_id = 101,
			.target_id = 202,
			.speaker_target_id = 202,
			.speaker_present = true,
			.target_present = true,
			.speaker = PublicEntity("Aten", FallbackDialogue::EntityKind::Player, 12, 0.0f, 0.0f, 0.0f),
			.target = PublicEntity("Guard Teren", FallbackDialogue::EntityKind::NPC, 22, 10.0f, 0.0f, 80.0f)
		}, ready_result));
		TEST_ASSERT(ready_result.output_type == FallbackDialogue::OutputType::Say);
		TEST_ASSERT_EQUALS(ready_result.message, std::string("Well met."));
		TEST_ASSERT_EQUALS(ready_result.speaker_id, static_cast<uint32_t>(101));
		TEST_ASSERT_EQUALS(ready_result.target_id, static_cast<uint32_t>(202));
		TEST_ASSERT_EQUALS(ready_result.visible_speaker_id, static_cast<uint32_t>(202));
		TEST_ASSERT_EQUALS(ready_result.debug_reason, std::string("delayed_dialogue_ready"));
	}

	void CompletedDelayedDialogueDropsWhenSpeakerTargetsSomethingElse()
	{
		ResetRules();
		RuleManager::Instance()->SetRule("Chat:FallbackDialogueEnabled", "true");
		RuleManager::Instance()->SetRule("Range:Say", "15");

		FallbackDialogue::TestDelayedDialogueProvider provider;
		FallbackDialogue::DelayedDialogueQueue queue(provider);
		const FallbackDialogue::TargetedSayRequest request{
			.speaker_id = 101,
			.target_id = 202,
			.message = "hail",
			.target_type = FallbackDialogue::TargetType::NPC,
			.authored_dialogue_handled = false
		};
		const FallbackDialogue::LiveContext live_context{
			.current_message = "hail",
			.speaker = PublicEntity("Aten", FallbackDialogue::EntityKind::Player, 12, 0.0f, 0.0f, 0.0f),
			.target = PublicEntity("Guard Teren", FallbackDialogue::EntityKind::NPC, 22, 5.0f, 0.0f, 0.0f),
			.zone = PublicZone("qeynos", "South Qeynos")
		};

		const auto queued_result = queue.HandleTargetedSay(request, live_context);
		TEST_ASSERT(queued_result.handled);
		TEST_ASSERT(provider.CompleteNextSuccess("Well met."));

		FallbackDialogue::TargetedSayResult ready_result;
		TEST_ASSERT(!queue.PopReadyResult({
			.speaker_id = 101,
			.target_id = 202,
			.speaker_target_id = 303,
			.speaker_present = true,
			.target_present = true,
			.speaker = PublicEntity("Aten", FallbackDialogue::EntityKind::Player, 12, 0.0f, 0.0f, 0.0f),
			.target = PublicEntity("Guard Teren", FallbackDialogue::EntityKind::NPC, 22, 5.0f, 0.0f, 0.0f)
		}, ready_result));
		TEST_ASSERT(!ready_result.handled);
		TEST_ASSERT(ready_result.output_type == FallbackDialogue::OutputType::None);
		TEST_ASSERT(ready_result.message.empty());
		TEST_ASSERT_EQUALS(ready_result.debug_reason, std::string("delayed_dialogue_dropped_target_changed"));
	}

	void FailedDelayedDialogueDropsWhenSpeakerLeavesSayRange()
	{
		ResetRules();
		RuleManager::Instance()->SetRule("Chat:FallbackDialogueEnabled", "true");
		RuleManager::Instance()->SetRule("Chat:FallbackDialogueUnavailableReply", "seems lost in thought.");
		RuleManager::Instance()->SetRule("Range:Say", "15");

		FallbackDialogue::TestDelayedDialogueProvider provider;
		FallbackDialogue::DelayedDialogueQueue queue(provider);
		const FallbackDialogue::TargetedSayRequest request{
			.speaker_id = 101,
			.target_id = 202,
			.message = "hail",
			.target_type = FallbackDialogue::TargetType::NPC,
			.authored_dialogue_handled = false
		};
		const FallbackDialogue::LiveContext live_context{
			.current_message = "hail",
			.speaker = PublicEntity("Aten", FallbackDialogue::EntityKind::Player, 12, 0.0f, 0.0f, 0.0f),
			.target = PublicEntity("Guard Teren", FallbackDialogue::EntityKind::NPC, 22, 5.0f, 0.0f, 0.0f),
			.zone = PublicZone("qeynos", "South Qeynos")
		};

		const auto queued_result = queue.HandleTargetedSay(request, live_context);
		TEST_ASSERT(queued_result.handled);
		TEST_ASSERT(provider.CompleteNextFailure());

		FallbackDialogue::TargetedSayResult ready_result;
		TEST_ASSERT(!queue.PopReadyResult({
			.speaker_id = 101,
			.target_id = 202,
			.speaker_target_id = 202,
			.speaker_present = true,
			.target_present = true,
			.speaker = PublicEntity("Aten", FallbackDialogue::EntityKind::Player, 12, 0.0f, 0.0f, 90.0f),
			.target = PublicEntity("Guard Teren", FallbackDialogue::EntityKind::NPC, 22, 16.0f, 0.0f, 0.0f)
		}, ready_result));
		TEST_ASSERT(!ready_result.handled);
		TEST_ASSERT(ready_result.output_type == FallbackDialogue::OutputType::None);
		TEST_ASSERT(ready_result.message.empty());
		TEST_ASSERT_EQUALS(ready_result.debug_reason, std::string("delayed_dialogue_dropped_out_of_say_range"));
	}

	void CompletedDelayedDialogueDropsWhenTargetIsMissing()
	{
		ResetRules();
		RuleManager::Instance()->SetRule("Chat:FallbackDialogueEnabled", "true");

		FallbackDialogue::TestDelayedDialogueProvider provider;
		FallbackDialogue::DelayedDialogueQueue queue(provider);
		const FallbackDialogue::TargetedSayRequest request{
			.speaker_id = 101,
			.target_id = 202,
			.message = "hail",
			.target_type = FallbackDialogue::TargetType::NPC,
			.authored_dialogue_handled = false
		};
		const FallbackDialogue::LiveContext live_context{
			.current_message = "hail",
			.speaker = PublicEntity("Aten", FallbackDialogue::EntityKind::Player, 12, 0.0f, 0.0f, 0.0f),
			.target = PublicEntity("Guard Teren", FallbackDialogue::EntityKind::NPC, 22, 5.0f, 0.0f, 0.0f),
			.zone = PublicZone("qeynos", "South Qeynos")
		};

		const auto queued_result = queue.HandleTargetedSay(request, live_context);
		TEST_ASSERT(queued_result.handled);
		TEST_ASSERT(provider.CompleteNextSuccess("Well met."));

		FallbackDialogue::TargetedSayResult ready_result;
		TEST_ASSERT(!queue.PopReadyResult({
			.speaker_id = 101,
			.target_id = 0,
			.speaker_target_id = 202,
			.speaker_present = true,
			.target_present = false,
			.speaker = PublicEntity("Aten", FallbackDialogue::EntityKind::Player, 12, 0.0f, 0.0f, 0.0f)
		}, ready_result));
		TEST_ASSERT(!ready_result.handled);
		TEST_ASSERT(ready_result.output_type == FallbackDialogue::OutputType::None);
		TEST_ASSERT(ready_result.message.empty());
		TEST_ASSERT_EQUALS(ready_result.debug_reason, std::string("delayed_dialogue_dropped_missing_target"));
	}

	void OllamaProviderSuccessfulResponseReturnsTargetSpeech()
	{
		FakeOllamaHttpTransport transport;
		transport.response = {
			.completed = true,
			.status = 200,
			.body = "{\"response\":\"Mind the docks after sundown.\"}"
		};

		const auto ready_result = OllamaResultFor(transport);

		TEST_ASSERT(ready_result.handled);
		TEST_ASSERT(ready_result.output_type == FallbackDialogue::OutputType::Say);
		TEST_ASSERT_EQUALS(ready_result.message, std::string("Mind the docks after sundown."));
		TEST_ASSERT_EQUALS(ready_result.debug_reason, std::string("delayed_dialogue_ready"));
	}

	void OllamaProviderUsesConfiguredEndpointModelTimeoutAndPublicPrompt()
	{
		FakeOllamaHttpTransport transport;
		transport.response = {
			.completed = true,
			.status = 200,
			.body = "{\"response\":\"Well met.\"}"
		};

		auto speaker = PublicEntity("Aten", FallbackDialogue::EntityKind::Player, 12, 0.0f, 0.0f, 0.0f);
		speaker.account_name = "private_account_name";
		speaker.ip_address = "192.0.2.55";
		speaker.private_chat = "private_tell_payload";
		speaker.inventory_summary = "bag_of_private_items";
		speaker.raw_quest_globals = "raw_global_state";
		speaker.account_id = 9001;
		speaker.character_id = 8001;
		speaker.gm_status = true;

		auto target = PublicEntity("Guard Teren", FallbackDialogue::EntityKind::NPC, 22, 5.0f, 0.0f, 0.0f);
		target.raw_quest_globals = "target_raw_globals";

		auto zone = PublicZone("qeynos", "South Qeynos");
		zone.zone_id = 1;
		zone.instance_id = 2;
		zone.database_credentials = "db_password_secret";

		const auto ready_result = OllamaResultFor(transport, {
			.current_message = "hail friend",
			.speaker = speaker,
			.target = target,
			.zone = zone,
			.nearby_entities = {
				PublicEntity("Dockhand", FallbackDialogue::EntityKind::NPC, 8, 10.0f, 0.0f, 0.0f)
			}
		});

		TEST_ASSERT(ready_result.handled);
		TEST_ASSERT_EQUALS(transport.Calls().size(), static_cast<size_t>(1));
		TEST_ASSERT_EQUALS(transport.Calls()[0].endpoint, std::string("http://ollama.test:11434/api/generate"));
		TEST_ASSERT_EQUALS(transport.Calls()[0].timeout_ms, 1234);
		TEST_ASSERT(transport.Calls()[0].body.find("\"model\":\"test-model\"") != std::string::npos);
		TEST_ASSERT(transport.Calls()[0].body.find("hail friend") != std::string::npos);
		TEST_ASSERT(transport.Calls()[0].body.find("Aten") != std::string::npos);
		TEST_ASSERT(transport.Calls()[0].body.find("Guard Teren") != std::string::npos);
		TEST_ASSERT(transport.Calls()[0].body.find("Dockhand") != std::string::npos);
		TEST_ASSERT(transport.Calls()[0].body.find("private_account_name") == std::string::npos);
		TEST_ASSERT(transport.Calls()[0].body.find("192.0.2.55") == std::string::npos);
		TEST_ASSERT(transport.Calls()[0].body.find("private_tell_payload") == std::string::npos);
		TEST_ASSERT(transport.Calls()[0].body.find("bag_of_private_items") == std::string::npos);
		TEST_ASSERT(transport.Calls()[0].body.find("raw_global_state") == std::string::npos);
		TEST_ASSERT(transport.Calls()[0].body.find("target_raw_globals") == std::string::npos);
		TEST_ASSERT(transport.Calls()[0].body.find("db_password_secret") == std::string::npos);
		TEST_ASSERT(transport.Calls()[0].body.find("9001") == std::string::npos);
		TEST_ASSERT(transport.Calls()[0].body.find("8001") == std::string::npos);
	}

	void OllamaProviderTimeoutFallsBackToUnavailableReply()
	{
		FakeOllamaHttpTransport transport;
		transport.response = {
			.completed = false
		};

		const auto ready_result = OllamaResultFor(transport);

		TEST_ASSERT(ready_result.handled);
		TEST_ASSERT(ready_result.output_type == FallbackDialogue::OutputType::Emote);
		TEST_ASSERT_EQUALS(ready_result.message, std::string("seems lost in thought."));
		TEST_ASSERT_EQUALS(ready_result.debug_reason, std::string("delayed_dialogue_unavailable"));
	}

	void OllamaProviderUnavailableServiceFallsBackToUnavailableReply()
	{
		FakeOllamaHttpTransport transport;
		transport.response = {
			.completed = true,
			.status = 503,
			.body = "service unavailable"
		};

		const auto ready_result = OllamaResultFor(transport);

		TEST_ASSERT(ready_result.handled);
		TEST_ASSERT(ready_result.output_type == FallbackDialogue::OutputType::Emote);
		TEST_ASSERT_EQUALS(ready_result.message, std::string("seems lost in thought."));
		TEST_ASSERT_EQUALS(ready_result.debug_reason, std::string("delayed_dialogue_unavailable"));
	}

	void OllamaProviderRequestFailureFallsBackToUnavailableReply()
	{
		FakeOllamaHttpTransport transport;
		transport.response = {
			.completed = true,
			.status = 200,
			.body = "{\"not_response\":\"missing dialogue\"}"
		};

		const auto ready_result = OllamaResultFor(transport);

		TEST_ASSERT(ready_result.handled);
		TEST_ASSERT(ready_result.output_type == FallbackDialogue::OutputType::Emote);
		TEST_ASSERT_EQUALS(ready_result.message, std::string("seems lost in thought."));
		TEST_ASSERT_EQUALS(ready_result.debug_reason, std::string("delayed_dialogue_unavailable"));
	}

	void OllamaProviderDisabledModelFallsBackToUnavailableReply()
	{
		FakeOllamaHttpTransport transport;

		const auto ready_result = OllamaResultFor(
			transport,
			DefaultLiveContext(),
			"   "
		);

		TEST_ASSERT(ready_result.handled);
		TEST_ASSERT(ready_result.output_type == FallbackDialogue::OutputType::Emote);
		TEST_ASSERT_EQUALS(ready_result.message, std::string("seems lost in thought."));
		TEST_ASSERT_EQUALS(ready_result.debug_reason, std::string("delayed_dialogue_unavailable"));
		TEST_ASSERT_EQUALS(transport.Calls().size(), static_cast<size_t>(0));
	}

	void OllamaProviderInvalidOutputFallsBackToUnavailableReply()
	{
		FakeOllamaHttpTransport transport;
		transport.response = {
			.completed = true,
			.status = 200,
			.body = "{\"response\":\"/say Follow me.\"}"
		};

		const auto ready_result = OllamaResultFor(transport);

		TEST_ASSERT(ready_result.handled);
		TEST_ASSERT(ready_result.output_type == FallbackDialogue::OutputType::Emote);
		TEST_ASSERT_EQUALS(ready_result.message, std::string("seems lost in thought."));
		TEST_ASSERT_EQUALS(ready_result.debug_reason, std::string("delayed_dialogue_rejected"));
	}

	void DiagnosticLogLineClassifiesRepresentativeResultsWithoutPrivateContent()
	{
		const std::vector<FallbackDialogue::TargetedSayResult> results = {
			{
				.handled = true,
				.debug_reason = "delayed_dialogue_queued",
				.speaker_id = 101,
				.target_id = 202,
				.target_type = FallbackDialogue::TargetType::NPC
			},
			{
				.debug_reason = "authored_dialogue_handled",
				.speaker_id = 101,
				.target_id = 202,
				.target_type = FallbackDialogue::TargetType::NPC
			},
			{
				.debug_reason = "dialogue_cooldown",
				.speaker_id = 101,
				.target_id = 202,
				.target_type = FallbackDialogue::TargetType::Bot
			},
			{
				.debug_reason = "delayed_dialogue_dropped_target_changed",
				.speaker_id = 101,
				.target_id = 202,
				.target_type = FallbackDialogue::TargetType::NPC
			},
			{
				.handled = true,
				.output_type = FallbackDialogue::OutputType::Emote,
				.message = "private unavailable text",
				.debug_reason = "delayed_dialogue_unavailable",
				.speaker_id = 101,
				.target_id = 202,
				.target_type = FallbackDialogue::TargetType::NPC
			},
			{
				.handled = true,
				.output_type = FallbackDialogue::OutputType::Emote,
				.message = "private rejected fallback text",
				.debug_reason = "delayed_dialogue_rejected",
				.speaker_id = 101,
				.target_id = 202,
				.target_type = FallbackDialogue::TargetType::NPC
			},
			{
				.handled = true,
				.output_type = FallbackDialogue::OutputType::Say,
				.message = "generated private dialogue line",
				.debug_reason = "delayed_dialogue_ready",
				.speaker_id = 101,
				.target_id = 202,
				.visible_speaker_id = 202,
				.target_type = FallbackDialogue::TargetType::NPC
			},
			{
				.handled = true,
				.output_type = FallbackDialogue::OutputType::Emote,
				.message = "legacy private fallback text",
				.speaker_id = 101,
				.target_id = 202,
				.visible_speaker_id = 202,
				.target_type = FallbackDialogue::TargetType::NPC
			}
		};

		const std::vector<std::string> expected_events = {
			"event=queued_generation",
			"event=authored_dialogue_suppression",
			"event=cooldown_skip",
			"event=stale_drop",
			"event=unavailable_reply",
			"event=rejected_output",
			"event=delivered_say",
			"event=delivered_emote"
		};

		for (size_t index = 0; index < results.size(); ++index) {
			const auto line = FallbackDialogue::BuildDiagnosticLogLine(results[index]);

			TEST_ASSERT(line.find("Fallback Dialogue diagnostic") != std::string::npos);
			TEST_ASSERT(line.find(expected_events[index]) != std::string::npos);
			TEST_ASSERT(line.find("speaker_id=101") != std::string::npos);
			TEST_ASSERT(line.find("target_id=202") != std::string::npos);
			if (results[index].output_type == FallbackDialogue::OutputType::Say) {
				TEST_ASSERT(line.find("delivery=delivered_say") != std::string::npos);
			} else if (results[index].output_type == FallbackDialogue::OutputType::Emote) {
				TEST_ASSERT(line.find("delivery=delivered_emote") != std::string::npos);
			}
			TEST_ASSERT(line.find("message=") == std::string::npos);
			TEST_ASSERT(line.find("prompt=") == std::string::npos);
			TEST_ASSERT(line.find("private") == std::string::npos);
			TEST_ASSERT(line.find("generated private dialogue line") == std::string::npos);
		}
	}

	void ResetRules()
	{
		RuleManager::Instance()->ResetRules(false);
		FallbackDialogue::ResetDialogueCooldowns();
	}

	FallbackDialogue::LiveEntity PublicEntity(
		const std::string &name,
		FallbackDialogue::EntityKind kind,
		int level,
		float x,
		float y,
		float z
	)
	{
		return {
			.name = name,
			.kind = kind,
			.level = level,
			.x = x,
			.y = y,
			.z = z
		};
	}

	FallbackDialogue::LiveZone PublicZone(const std::string &short_name, const std::string &long_name)
	{
		return {
			.short_name = short_name,
			.long_name = long_name
		};
	}

	FallbackDialogue::LiveContext DefaultLiveContext()
	{
		return {
			.current_message = "hail",
			.speaker = PublicEntity("Aten", FallbackDialogue::EntityKind::Player, 12, 0.0f, 0.0f, 0.0f),
			.target = PublicEntity("Guard Teren", FallbackDialogue::EntityKind::NPC, 22, 5.0f, 0.0f, 0.0f),
			.zone = PublicZone("qeynos", "South Qeynos")
		};
	}

	FallbackDialogue::CurrentInteraction CurrentInteractionFor(
		uint32_t speaker_id,
		uint32_t target_id,
		uint32_t speaker_target_id
	)
	{
		return CurrentInteractionFor(
			speaker_id,
			target_id,
			speaker_target_id,
			PublicEntity("Aten", FallbackDialogue::EntityKind::Player, 12, 0.0f, 0.0f, 0.0f),
			PublicEntity("Guard Teren", FallbackDialogue::EntityKind::NPC, 22, 5.0f, 0.0f, 0.0f)
		);
	}

	FallbackDialogue::CurrentInteraction CurrentInteractionFor(
		uint32_t speaker_id,
		uint32_t target_id,
		uint32_t speaker_target_id,
		const FallbackDialogue::LiveEntity &speaker,
		const FallbackDialogue::LiveEntity &target
	)
	{
		return {
			.speaker_id = speaker_id,
			.target_id = target_id,
			.speaker_target_id = speaker_target_id,
			.speaker_present = true,
			.target_present = true,
			.speaker = speaker,
			.target = target
		};
	}

	FallbackDialogue::TargetedSayResult DelayedSuccessResultFor(
		const std::string &dialogue_line,
		const std::string &unavailable_reply
	)
	{
		ResetRules();
		RuleManager::Instance()->SetRule("Chat:FallbackDialogueEnabled", "true");
		RuleManager::Instance()->SetRule("Chat:FallbackDialogueUnavailableReply", unavailable_reply);

		FallbackDialogue::TestDelayedDialogueProvider provider;
		FallbackDialogue::DelayedDialogueQueue queue(provider);
		const FallbackDialogue::TargetedSayRequest request{
			.speaker_id = 101,
			.target_id = 202,
			.message = "hail",
			.target_type = FallbackDialogue::TargetType::NPC,
			.authored_dialogue_handled = false
		};
		const FallbackDialogue::LiveContext live_context{
			.current_message = "hail",
			.speaker = PublicEntity("Aten", FallbackDialogue::EntityKind::Player, 12, 0.0f, 0.0f, 0.0f),
			.target = PublicEntity("Guard Teren", FallbackDialogue::EntityKind::NPC, 22, 5.0f, 0.0f, 0.0f),
			.zone = PublicZone("qeynos", "South Qeynos")
		};

		const auto queued_result = queue.HandleTargetedSay(request, live_context);
		if (!queued_result.handled || !provider.CompleteNextSuccess(dialogue_line)) {
			return {};
		}

		FallbackDialogue::TargetedSayResult ready_result;
		if (!queue.PopReadyResult(CurrentInteractionFor(
			101,
			202,
			202,
			PublicEntity("Aten", FallbackDialogue::EntityKind::Player, 12, 0.0f, 0.0f, 0.0f),
			PublicEntity("Guard Teren", FallbackDialogue::EntityKind::NPC, 22, 5.0f, 0.0f, 0.0f)
		), ready_result)) {
			return ready_result;
		}

		return ready_result;
	}

	FallbackDialogue::TargetedSayResult OllamaResultFor(
		FakeOllamaHttpTransport &transport,
		const FallbackDialogue::LiveContext &live_context = FallbackDialogue::LiveContext{},
		const std::string &model = "test-model"
	)
	{
		ResetRules();
		RuleManager::Instance()->SetRule("Chat:FallbackDialogueEnabled", "true");
		RuleManager::Instance()->SetRule("Chat:FallbackDialogueUnavailableReply", "seems lost in thought.");
		RuleManager::Instance()->SetRule("Chat:FallbackDialogueOllamaEndpoint", "http://ollama.test:11434/api/generate");
		RuleManager::Instance()->SetRule("Chat:FallbackDialogueOllamaModel", model);
		RuleManager::Instance()->SetRule("Chat:FallbackDialogueOllamaTimeoutMs", "1234");

		FallbackDialogue::OllamaDelayedDialogueProvider provider(transport);
		FallbackDialogue::DelayedDialogueQueue queue(provider);
		const FallbackDialogue::TargetedSayRequest request{
			.speaker_id = 101,
			.target_id = 202,
			.message = "hail",
			.target_type = FallbackDialogue::TargetType::NPC,
			.authored_dialogue_handled = false
		};

		const auto context = live_context.current_message.empty() ? DefaultLiveContext() : live_context;
		const auto queued_result = queue.HandleTargetedSay(request, context);
		if (!queued_result.handled) {
			return {};
		}

		FallbackDialogue::TargetedSayResult ready_result;
		for (int attempt = 0; attempt < 100; ++attempt) {
			if (queue.PopReadyResult(CurrentInteractionFor(
				101,
				202,
				202,
				PublicEntity("Aten", FallbackDialogue::EntityKind::Player, 12, 0.0f, 0.0f, 0.0f),
				PublicEntity("Guard Teren", FallbackDialogue::EntityKind::NPC, 22, 5.0f, 0.0f, 0.0f)
			), ready_result)) {
				return ready_result;
			}

			std::this_thread::sleep_for(std::chrono::milliseconds(10));
		}

		return ready_result;
	}

	std::string SerializePublicContext(const FallbackDialogue::PublicGameplayContext &context)
	{
		std::stringstream serialized;
		serialized
			<< context.current_message << ' '
			<< context.speaker.name << ' '
			<< context.speaker.level << ' '
			<< context.target.name << ' '
			<< context.target.level << ' '
			<< context.zone.short_name << ' '
			<< context.zone.long_name;

		for (const auto &entity : context.nearby_entities) {
			serialized
				<< ' '
				<< entity.name
				<< ' '
				<< entity.level;
		}

		return serialized.str();
	}
};

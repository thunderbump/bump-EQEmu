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
		TEST_ADD(FallbackDialogueTest::PublicGameplayContextExcludesLocalOnlyDerivationFields);
		TEST_ADD(FallbackDialogueTest::PublicGameplayContextFiltersNearbyEntitiesBySettingsRadius);
		TEST_ADD(FallbackDialogueTest::PublicGameplayContextLimitsNearbyEntitiesBySettingsCount);
		TEST_ADD(FallbackDialogueTest::PublicGameplayContextExcludesSpeakerAndTargetFromNearbyEntities);
		TEST_ADD(FallbackDialogueTest::DialogueResponseProcessingReturnsSpeechFragment);
		TEST_ADD(FallbackDialogueTest::DialogueResponseProcessingReturnsStageDirectionFragments);
		TEST_ADD(FallbackDialogueTest::DialogueResponseProcessingKeepsMixedParentheticalSpeech);
		TEST_ADD(FallbackDialogueTest::DialogueResponseProcessingStripsTargetNameFromEmotesOnly);
		TEST_ADD(FallbackDialogueTest::DialogueResponseProcessingRejectsUnsafeFragment);
		TEST_ADD(FallbackDialogueTest::DialogueResponseProcessingKeepsMalformedMarkerAsSpeech);
		TEST_ADD(FallbackDialogueTest::DialogueDeliveryPlanningReturnsOrderedDeliveredMessages);
		TEST_ADD(FallbackDialogueTest::DialogueDeliveryPlanningSplitsLongSpeech);
		TEST_ADD(FallbackDialogueTest::DialogueDeliveryPlanningRejectsLongEmote);
		TEST_ADD(FallbackDialogueTest::EligibleNpcTargetedSayQueuesDelayedRequestWithPublicContext);
		TEST_ADD(FallbackDialogueTest::CompletedDelayedDialogueReturnsTargetSpeech);
		TEST_ADD(FallbackDialogueTest::CompletedDelayedDialogueStripsNewlines);
		TEST_ADD(FallbackDialogueTest::CompletedDelayedDialogueTrimsModelArtifacts);
		TEST_ADD(FallbackDialogueTest::CompletedDelayedDialogueReturnsPureAsteriskActionAsTargetEmote);
		TEST_ADD(FallbackDialogueTest::CompletedNpcDelayedDialogueStripsTargetNameFromEmote);
		TEST_ADD(FallbackDialogueTest::CompletedDelayedDialoguePreservesMixedSpeechAndEmoteOrder);
		TEST_ADD(FallbackDialogueTest::CompletedMixedDelayedDialogueStripsTargetNameFromEmoteOnly);
		TEST_ADD(FallbackDialogueTest::CompletedBotDelayedDialogueReturnsParenthesizedActionAsTargetEmote);
		TEST_ADD(FallbackDialogueTest::CompletedBotDelayedDialogueStripsTargetNameFromEmote);
		TEST_ADD(FallbackDialogueTest::CompletedDelayedDialogueDoesNotStripTargetNameFromSpeech);
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
		TEST_ADD(FallbackDialogueTest::LongEmoteDelayedDialogueFallsBackToUnavailableReply);
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
		TEST_ADD(FallbackDialogueTest::OllamaProviderInvalidEndpointFallsBackToUnavailableReply);
		TEST_ADD(FallbackDialogueTest::OllamaProviderInvalidTimeoutFallsBackToUnavailableReply);
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

	FallbackDialogue::FallbackDialogueSettings DisabledFallbackDialogueSettings()
	{
		return {};
	}

	FallbackDialogue::FallbackDialogueSettings EnabledFallbackDialogueSettings(
		const std::string &unavailable_reply = "appears distracted.",
		int cooldown_seconds = 30,
		int max_delivered_line_length = 200,
		int imported_game_rule_say_range = 15,
		FallbackDialogue::OllamaProviderSettings ollama_provider = {}
	)
	{
		return {
			.immediate = {
				.enabled = true,
				.cooldown_seconds = cooldown_seconds,
				.unavailable_reply = unavailable_reply
			},
			.delivery = {
				.max_delivered_line_length = max_delivered_line_length
			},
			.current_interaction = {
				.imported_game_rule_say_range = imported_game_rule_say_range
			},
			.ollama_provider = {
				.endpoint = ollama_provider.endpoint,
				.model = ollama_provider.model,
				.timeout_ms = ollama_provider.timeout_ms
			}
		};
	}

	FallbackDialogue::OllamaProviderSettings OllamaProviderSettings(
		const std::string &endpoint = "http://ollama.test:11434/api/generate",
		const std::string &model = "test-model",
		int timeout_ms = 1234
	)
	{
		return {
			.endpoint = endpoint,
			.model = model,
			.timeout_ms = timeout_ms
		};
	}

	FallbackDialogue::PublicGameplayContextSettings DefaultPublicGameplayContextSettings()
	{
		return {};
	}

	FallbackDialogue::DialogueDeliverySettings DeliverySettings(int max_delivered_line_length)
	{
		return {
			.max_delivered_line_length = max_delivered_line_length
		};
	}

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
	}

	void EligibleNpcTargetedSayReturnsUnavailableReplyEmote()
	{
		ResetRules();
		const auto settings = EnabledFallbackDialogueSettings("seems lost in thought.");

		const FallbackDialogue::TargetedSayRequest request{
			.speaker_id = 1,
			.target_id = 2,
			.message = "hello",
			.target_type = FallbackDialogue::TargetType::NPC,
			.authored_dialogue_handled = false
		};

		const auto result = FallbackDialogue::HandleTargetedSay(request, settings);
		TEST_ASSERT(result.handled);
		TEST_ASSERT(result.output_type == FallbackDialogue::OutputType::Emote);
		TEST_ASSERT_EQUALS(result.message, std::string("seems lost in thought."));
	}

	void EligibleBotTargetedSayReturnsUnavailableReplyEmote()
	{
		ResetRules();
		const auto settings = EnabledFallbackDialogueSettings("looks momentarily distracted.");

		const FallbackDialogue::TargetedSayRequest request{
			.speaker_id = 1,
			.target_id = 2,
			.message = "hello",
			.target_type = FallbackDialogue::TargetType::Bot,
			.authored_dialogue_handled = false
		};

		const auto result = FallbackDialogue::HandleTargetedSay(request, settings);
		TEST_ASSERT(result.handled);
		TEST_ASSERT(result.output_type == FallbackDialogue::OutputType::Emote);
		TEST_ASSERT_EQUALS(result.message, std::string("looks momentarily distracted."));
	}

	void EngagedBotTargetedSayReturnsUnavailableReplyEmote()
	{
		ResetRules();
		const auto settings = EnabledFallbackDialogueSettings("seems distracted by the fight.");

		const FallbackDialogue::TargetedSayRequest request{
			.speaker_id = 1,
			.target_id = 2,
			.message = "hello",
			.target_type = FallbackDialogue::TargetType::Bot,
			.authored_dialogue_handled = false,
			.target_engaged = true
		};

		const auto result = FallbackDialogue::HandleTargetedSay(request, settings);
		TEST_ASSERT(result.handled);
		TEST_ASSERT(result.output_type == FallbackDialogue::OutputType::Emote);
		TEST_ASSERT_EQUALS(result.message, std::string("seems distracted by the fight."));
	}

	void RepeatedNpcTargetedSayDuringCooldownShowsNoReply()
	{
		ResetRules();
		const auto settings = EnabledFallbackDialogueSettings("appears distracted.", 30);

		const FallbackDialogue::TargetedSayRequest request{
			.speaker_id = 1,
			.target_id = 2,
			.message = "hail",
			.target_type = FallbackDialogue::TargetType::NPC,
			.authored_dialogue_handled = false
		};

		const auto first_result = FallbackDialogue::HandleTargetedSay(request, settings);
		TEST_ASSERT(first_result.handled);

		const auto repeat_result = FallbackDialogue::HandleTargetedSay(request, settings);
		TEST_ASSERT(!repeat_result.handled);
		TEST_ASSERT(repeat_result.output_type == FallbackDialogue::OutputType::None);
		TEST_ASSERT(repeat_result.message.empty());
		TEST_ASSERT_EQUALS(repeat_result.debug_reason, std::string("dialogue_cooldown"));
	}

	void RepeatedBotTargetedSayDuringCooldownShowsNoReply()
	{
		ResetRules();
		const auto settings = EnabledFallbackDialogueSettings("appears distracted.", 30);

		const FallbackDialogue::TargetedSayRequest request{
			.speaker_id = 1,
			.target_id = 2,
			.message = "hail",
			.target_type = FallbackDialogue::TargetType::Bot,
			.authored_dialogue_handled = false
		};

		const auto first_result = FallbackDialogue::HandleTargetedSay(request, settings);
		TEST_ASSERT(first_result.handled);

		const auto repeat_result = FallbackDialogue::HandleTargetedSay(request, settings);
		TEST_ASSERT(!repeat_result.handled);
		TEST_ASSERT(repeat_result.output_type == FallbackDialogue::OutputType::None);
		TEST_ASSERT(repeat_result.message.empty());
		TEST_ASSERT_EQUALS(repeat_result.debug_reason, std::string("dialogue_cooldown"));
	}

	void DifferentTargetedSayBySameSpeakerBypassesCooldown()
	{
		ResetRules();
		const auto settings = EnabledFallbackDialogueSettings("appears distracted.", 30);

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

		const auto first_result = FallbackDialogue::HandleTargetedSay(first_target, settings);
		TEST_ASSERT(first_result.handled);

		const auto second_result = FallbackDialogue::HandleTargetedSay(second_target, settings);
		TEST_ASSERT(second_result.handled);
		TEST_ASSERT(second_result.output_type == FallbackDialogue::OutputType::Emote);
	}

	void TargetedSayAfterCooldownExpiresReturnsUnavailableReply()
	{
		ResetRules();
		const auto settings = EnabledFallbackDialogueSettings("appears distracted.", 30);

		const FallbackDialogue::TargetedSayRequest request{
			.speaker_id = 1,
			.target_id = 2,
			.message = "hail",
			.target_type = FallbackDialogue::TargetType::NPC,
			.authored_dialogue_handled = false
		};

		const auto first_result = FallbackDialogue::HandleTargetedSay(request, settings);
		TEST_ASSERT(first_result.handled);

		Timer::RollForward(31);

		const auto expired_result = FallbackDialogue::HandleTargetedSay(request, settings);
		TEST_ASSERT(expired_result.handled);
		TEST_ASSERT(expired_result.output_type == FallbackDialogue::OutputType::Emote);
	}

	void AuthoredNpcDialogueSuppressesUnavailableReply()
	{
		ResetRules();
		const auto settings = EnabledFallbackDialogueSettings();

		const FallbackDialogue::TargetedSayRequest request{
			.speaker_id = 1,
			.target_id = 2,
			.message = "hail",
			.target_type = FallbackDialogue::TargetType::NPC,
			.authored_dialogue_handled = true
		};

		const auto result = FallbackDialogue::HandleTargetedSay(request, settings);
		TEST_ASSERT(!result.handled);
		TEST_ASSERT(result.output_type == FallbackDialogue::OutputType::None);
		TEST_ASSERT(result.message.empty());
		TEST_ASSERT_EQUALS(result.debug_reason, std::string("authored_dialogue_handled"));
	}

	void AuthoredBotDialogueSuppressesUnavailableReply()
	{
		ResetRules();
		const auto settings = EnabledFallbackDialogueSettings();

		const FallbackDialogue::TargetedSayRequest request{
			.speaker_id = 1,
			.target_id = 2,
			.message = "hail",
			.target_type = FallbackDialogue::TargetType::Bot,
			.authored_dialogue_handled = true
		};

		const auto result = FallbackDialogue::HandleTargetedSay(request, settings);
		TEST_ASSERT(!result.handled);
		TEST_ASSERT(result.output_type == FallbackDialogue::OutputType::None);
		TEST_ASSERT(result.message.empty());
		TEST_ASSERT_EQUALS(result.debug_reason, std::string("authored_dialogue_handled"));
	}

	void EngagedNpcDialogueSuppressesUnavailableReply()
	{
		ResetRules();
		const auto settings = EnabledFallbackDialogueSettings();

		const FallbackDialogue::TargetedSayRequest request{
			.speaker_id = 1,
			.target_id = 2,
			.message = "hail",
			.target_type = FallbackDialogue::TargetType::NPC,
			.authored_dialogue_handled = false,
			.target_engaged = true
		};

		const auto result = FallbackDialogue::HandleTargetedSay(request, settings);
		TEST_ASSERT(!result.handled);
		TEST_ASSERT(result.output_type == FallbackDialogue::OutputType::None);
		TEST_ASSERT(result.message.empty());
		TEST_ASSERT_EQUALS(result.debug_reason, std::string("target_engaged"));
	}

	void MercenaryTargetedSayReportsSkipWithoutReply()
	{
		ResetRules();
		const auto settings = EnabledFallbackDialogueSettings();

		const FallbackDialogue::TargetedSayRequest request{
			.speaker_id = 1,
			.target_id = 2,
			.message = "hail",
			.target_type = FallbackDialogue::TargetType::Mercenary,
			.authored_dialogue_handled = false
		};

		const auto result = FallbackDialogue::HandleTargetedSay(request, settings);
		TEST_ASSERT(!result.handled);
		TEST_ASSERT(result.output_type == FallbackDialogue::OutputType::None);
		TEST_ASSERT(result.message.empty());
		TEST_ASSERT_EQUALS(result.debug_reason, std::string("unsupported_target_type"));
	}

	void UnknownTargetedSayReportsSkipWithoutReply()
	{
		ResetRules();
		const auto settings = EnabledFallbackDialogueSettings();

		const FallbackDialogue::TargetedSayRequest request{
			.speaker_id = 1,
			.target_id = 2,
			.message = "hail",
			.target_type = FallbackDialogue::TargetType::Unknown,
			.authored_dialogue_handled = false
		};

		const auto result = FallbackDialogue::HandleTargetedSay(request, settings);
		TEST_ASSERT(!result.handled);
		TEST_ASSERT(result.output_type == FallbackDialogue::OutputType::None);
		TEST_ASSERT(result.message.empty());
		TEST_ASSERT_EQUALS(result.debug_reason, std::string("unsupported_target_type"));
	}

	void PublicGameplayContextIncludesAllowedFields()
	{
		ResetRules();

		auto target = PublicEntity("Guard Teren", FallbackDialogue::EntityKind::NPC, 22, 5.0f, 0.0f, 0.0f);
		target.engaged = true;

		const auto context = FallbackDialogue::BuildPublicGameplayContext({
			.current_message = "hail friend",
			.speaker = PublicEntity("Aten", FallbackDialogue::EntityKind::Player, 12, 0.0f, 0.0f, 0.0f),
			.target = target,
			.zone = PublicZone("qeynos", "South Qeynos"),
			.nearby_entities = {
				PublicEntity("Merchant Bren", FallbackDialogue::EntityKind::NPC, 18, 10.0f, 0.0f, 0.0f),
				PublicEntity("Atenbot", FallbackDialogue::EntityKind::Bot, 12, 15.0f, 0.0f, 0.0f)
			}
		}, DefaultPublicGameplayContextSettings());

		TEST_ASSERT_EQUALS(context.current_message, std::string("hail friend"));
		TEST_ASSERT_EQUALS(context.speaker.name, std::string("Aten"));
		TEST_ASSERT(context.speaker.kind == FallbackDialogue::EntityKind::Player);
		TEST_ASSERT_EQUALS(context.speaker.level, 12);
		TEST_ASSERT_EQUALS(context.target.name, std::string("Guard Teren"));
		TEST_ASSERT(context.target.kind == FallbackDialogue::EntityKind::NPC);
		TEST_ASSERT_EQUALS(context.target.level, 22);
		TEST_ASSERT(context.target.engaged);
		TEST_ASSERT_EQUALS(context.target.distance, 5.0f);
		TEST_ASSERT_EQUALS(context.zone.short_name, std::string("qeynos"));
		TEST_ASSERT_EQUALS(context.zone.long_name, std::string("South Qeynos"));
		TEST_ASSERT_EQUALS(context.nearby_entities.size(), static_cast<size_t>(2));
		TEST_ASSERT_EQUALS(context.nearby_entities[0].name, std::string("Merchant Bren"));
		TEST_ASSERT(context.nearby_entities[0].kind == FallbackDialogue::EntityKind::NPC);
		TEST_ASSERT_EQUALS(context.nearby_entities[0].level, 18);
		TEST_ASSERT_EQUALS(context.nearby_entities[0].distance, 10.0f);
		TEST_ASSERT_EQUALS(context.nearby_entities[1].name, std::string("Atenbot"));
		TEST_ASSERT(context.nearby_entities[1].kind == FallbackDialogue::EntityKind::Bot);
		TEST_ASSERT_EQUALS(context.nearby_entities[1].level, 12);
		TEST_ASSERT_EQUALS(context.nearby_entities[1].distance, 15.0f);
	}

	void PublicGameplayContextExcludesLocalOnlyDerivationFields()
	{
		ResetRules();

		auto speaker = PublicEntity("Aten", FallbackDialogue::EntityKind::Player, 12, 1111.0f, 2222.0f, 3333.0f);
		speaker.entity_id = 91001;
		auto target = PublicEntity("Guard Teren", FallbackDialogue::EntityKind::NPC, 22, 1114.0f, 2226.0f, 3333.0f);
		target.entity_id = 92002;
		auto nearby = PublicEntity("Dockhand", FallbackDialogue::EntityKind::NPC, 8, 1111.0f, 2228.0f, 3333.0f);
		nearby.entity_id = 93003;

		const auto context = FallbackDialogue::BuildPublicGameplayContext({
			.current_message = "hail friend",
			.speaker = speaker,
			.target = target,
			.zone = PublicZone("qeynos", "South Qeynos"),
			.nearby_entities = {nearby}
		}, DefaultPublicGameplayContextSettings());

		const auto serialized = SerializePublicContext(context);
		TEST_ASSERT(serialized.find("Aten") != std::string::npos);
		TEST_ASSERT(serialized.find("Guard Teren") != std::string::npos);
		TEST_ASSERT(serialized.find("Dockhand") != std::string::npos);
		TEST_ASSERT(serialized.find("91001") == std::string::npos);
		TEST_ASSERT(serialized.find("92002") == std::string::npos);
		TEST_ASSERT(serialized.find("93003") == std::string::npos);
		TEST_ASSERT(serialized.find("1111") == std::string::npos);
		TEST_ASSERT(serialized.find("1114") == std::string::npos);
		TEST_ASSERT(serialized.find("2222") == std::string::npos);
		TEST_ASSERT(serialized.find("2226") == std::string::npos);
		TEST_ASSERT(serialized.find("2228") == std::string::npos);
		TEST_ASSERT(serialized.find("3333") == std::string::npos);
	}

	void PublicGameplayContextFiltersNearbyEntitiesBySettingsRadius()
	{
		ResetRules();

		const auto context = FallbackDialogue::BuildPublicGameplayContext({
			.current_message = "hail",
			.speaker = PublicEntity("Aten", FallbackDialogue::EntityKind::Player, 12, 0.0f, 0.0f, 0.0f),
			.target = PublicEntity("Guard Teren", FallbackDialogue::EntityKind::NPC, 22, 5.0f, 0.0f, 0.0f),
			.zone = PublicZone("qeynos", "South Qeynos"),
			.nearby_entities = {
				PublicEntity("Nearby Merchant", FallbackDialogue::EntityKind::NPC, 18, 10.0f, 0.0f, 0.0f),
				PublicEntity("Distant Guard", FallbackDialogue::EntityKind::NPC, 30, 25.0f, 0.0f, 0.0f)
			}
		}, {
			.nearby_context_radius = 20,
			.nearby_entity_limit = 8
		});

		TEST_ASSERT_EQUALS(context.nearby_entities.size(), static_cast<size_t>(1));
		TEST_ASSERT_EQUALS(context.nearby_entities[0].name, std::string("Nearby Merchant"));
	}

	void PublicGameplayContextLimitsNearbyEntitiesBySettingsCount()
	{
		ResetRules();

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
		}, {
			.nearby_context_radius = 100,
			.nearby_entity_limit = 2
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
		}, DefaultPublicGameplayContextSettings());

		TEST_ASSERT_EQUALS(context.nearby_entities.size(), static_cast<size_t>(1));
		TEST_ASSERT_EQUALS(context.nearby_entities[0].name, std::string("Dockhand"));
	}

	void DialogueResponseProcessingReturnsSpeechFragment()
	{
		const auto result = FallbackDialogue::ProcessDialogueResponse(
			" Assistant: \"Mind the road, friend.\" ",
			"Guard Teren"
		);

		TEST_ASSERT(result.accepted);
		TEST_ASSERT(result.rejection_reason.empty());
		TEST_ASSERT_EQUALS(result.fragments.size(), static_cast<size_t>(1));
		TEST_ASSERT(result.fragments[0].output_type == FallbackDialogue::OutputType::Say);
		TEST_ASSERT_EQUALS(result.fragments[0].message, std::string("Mind the road, friend."));
	}

	void DialogueResponseProcessingReturnsStageDirectionFragments()
	{
		const auto pure_result = FallbackDialogue::ProcessDialogueResponse(
			"(checks bowstring)",
			"Atenbot"
		);

		TEST_ASSERT(pure_result.accepted);
		TEST_ASSERT_EQUALS(pure_result.fragments.size(), static_cast<size_t>(1));
		TEST_ASSERT(pure_result.fragments[0].output_type == FallbackDialogue::OutputType::Emote);
		TEST_ASSERT_EQUALS(pure_result.fragments[0].message, std::string("checks bowstring"));

		const auto mixed_result = FallbackDialogue::ProcessDialogueResponse(
			"Well met. *looks around warily* Keep your voice low.",
			"Guard Teren"
		);

		TEST_ASSERT(mixed_result.accepted);
		TEST_ASSERT_EQUALS(mixed_result.fragments.size(), static_cast<size_t>(3));
		TEST_ASSERT(mixed_result.fragments[0].output_type == FallbackDialogue::OutputType::Say);
		TEST_ASSERT_EQUALS(mixed_result.fragments[0].message, std::string("Well met."));
		TEST_ASSERT(mixed_result.fragments[1].output_type == FallbackDialogue::OutputType::Emote);
		TEST_ASSERT_EQUALS(mixed_result.fragments[1].message, std::string("looks around warily"));
		TEST_ASSERT(mixed_result.fragments[2].output_type == FallbackDialogue::OutputType::Say);
		TEST_ASSERT_EQUALS(mixed_result.fragments[2].message, std::string("Keep your voice low."));
	}

	void DialogueResponseProcessingKeepsMixedParentheticalSpeech()
	{
		const auto result = FallbackDialogue::ProcessDialogueResponse(
			"Well met (if you can call it that).",
			"Guard Teren"
		);

		TEST_ASSERT(result.accepted);
		TEST_ASSERT_EQUALS(result.fragments.size(), static_cast<size_t>(1));
		TEST_ASSERT(result.fragments[0].output_type == FallbackDialogue::OutputType::Say);
		TEST_ASSERT_EQUALS(result.fragments[0].message, std::string("Well met (if you can call it that)."));
	}

	void DialogueResponseProcessingStripsTargetNameFromEmotesOnly()
	{
		const auto result = FallbackDialogue::ProcessDialogueResponse(
			"Guard Teren speaks softly. *Guard Teren checks the road*",
			"Guard Teren"
		);

		TEST_ASSERT(result.accepted);
		TEST_ASSERT_EQUALS(result.fragments.size(), static_cast<size_t>(2));
		TEST_ASSERT(result.fragments[0].output_type == FallbackDialogue::OutputType::Say);
		TEST_ASSERT_EQUALS(result.fragments[0].message, std::string("Guard Teren speaks softly."));
		TEST_ASSERT(result.fragments[1].output_type == FallbackDialogue::OutputType::Emote);
		TEST_ASSERT_EQUALS(result.fragments[1].message, std::string("checks the road"));
	}

	void DialogueResponseProcessingRejectsUnsafeFragment()
	{
		const auto result = FallbackDialogue::ProcessDialogueResponse(
			"Well met. */say follow me* Keep watch.",
			"Guard Teren"
		);

		TEST_ASSERT(!result.accepted);
		TEST_ASSERT_EQUALS(result.rejection_reason, std::string("unsafe_dialogue_fragment"));
		TEST_ASSERT(result.fragments.empty());
	}

	void DialogueResponseProcessingKeepsMalformedMarkerAsSpeech()
	{
		const auto result = FallbackDialogue::ProcessDialogueResponse(
			"Well met. *looks around warily",
			"Guard Teren"
		);

		TEST_ASSERT(result.accepted);
		TEST_ASSERT_EQUALS(result.fragments.size(), static_cast<size_t>(1));
		TEST_ASSERT(result.fragments[0].output_type == FallbackDialogue::OutputType::Say);
		TEST_ASSERT_EQUALS(result.fragments[0].message, std::string("Well met. *looks around warily"));
	}

	void DialogueDeliveryPlanningReturnsOrderedDeliveredMessages()
	{
		ResetRules();

		const auto result = FallbackDialogue::PlanDialogueDelivery({
			{.output_type = FallbackDialogue::OutputType::Say, .message = "Well met."},
			{.output_type = FallbackDialogue::OutputType::Emote, .message = "looks around warily"},
			{.output_type = FallbackDialogue::OutputType::Say, .message = "Keep your voice low."}
		}, DeliverySettings(200));

		TEST_ASSERT(result.accepted);
		TEST_ASSERT(result.rejection_reason.empty());
		TEST_ASSERT_EQUALS(result.messages.size(), static_cast<size_t>(3));
		TEST_ASSERT(result.messages[0].output_type == FallbackDialogue::OutputType::Say);
		TEST_ASSERT_EQUALS(result.messages[0].message, std::string("Well met."));
		TEST_ASSERT(result.messages[1].output_type == FallbackDialogue::OutputType::Emote);
		TEST_ASSERT_EQUALS(result.messages[1].message, std::string("looks around warily"));
		TEST_ASSERT(result.messages[2].output_type == FallbackDialogue::OutputType::Say);
		TEST_ASSERT_EQUALS(result.messages[2].message, std::string("Keep your voice low."));
	}

	void DialogueDeliveryPlanningSplitsLongSpeech()
	{
		ResetRules();

		const auto result = FallbackDialogue::PlanDialogueDelivery({
			{.output_type = FallbackDialogue::OutputType::Say, .message = "First sentence. Second sentence keeps going."}
		}, DeliverySettings(24));

		TEST_ASSERT(result.accepted);
		TEST_ASSERT_EQUALS(result.messages.size(), static_cast<size_t>(3));
		TEST_ASSERT(result.messages[0].output_type == FallbackDialogue::OutputType::Say);
		TEST_ASSERT_EQUALS(result.messages[0].message, std::string("First sentence."));
		TEST_ASSERT(result.messages[1].output_type == FallbackDialogue::OutputType::Say);
		TEST_ASSERT_EQUALS(result.messages[1].message, std::string("Second sentence keeps"));
		TEST_ASSERT(result.messages[2].output_type == FallbackDialogue::OutputType::Say);
		TEST_ASSERT_EQUALS(result.messages[2].message, std::string("going."));
	}

	void DialogueDeliveryPlanningRejectsLongEmote()
	{
		ResetRules();

		const auto result = FallbackDialogue::PlanDialogueDelivery({
			{.output_type = FallbackDialogue::OutputType::Emote, .message = "looks around warily"}
		}, DeliverySettings(16));

		TEST_ASSERT(!result.accepted);
		TEST_ASSERT_EQUALS(result.rejection_reason, std::string("long_emote_dialogue_fragment"));
		TEST_ASSERT(result.messages.empty());
	}

	void EligibleNpcTargetedSayQueuesDelayedRequestWithPublicContext()
	{
		ResetRules();

		FallbackDialogue::TestDelayedDialogueProvider provider;
		FallbackDialogue::DelayedDialogueQueue queue(provider, EnabledFallbackDialogueSettings());
		FallbackDialogue::PublicGameplayContextInput public_context_input{
			.current_message = "hail captain",
			.speaker = PublicEntity("Aten", FallbackDialogue::EntityKind::Player, 12, 0.0f, 0.0f, 0.0f),
			.target = PublicEntity("Captain Rohand", FallbackDialogue::EntityKind::NPC, 35, 8.0f, 0.0f, 0.0f),
			.zone = PublicZone("qeynos", "South Qeynos"),
			.nearby_entities = {
				PublicEntity("Dockhand", FallbackDialogue::EntityKind::NPC, 8, 12.0f, 0.0f, 0.0f)
			}
		};
		public_context_input.speaker.entity_id = 91001;
		public_context_input.target.entity_id = 92002;
		public_context_input.nearby_entities[0].entity_id = 93003;
		const FallbackDialogue::TargetedSayRequest request{
			.speaker_id = 101,
			.target_id = 202,
			.message = "hail captain",
			.target_type = FallbackDialogue::TargetType::NPC,
			.authored_dialogue_handled = false
		};

		const auto result = queue.HandleTargetedSay(request, public_context_input);
		public_context_input.target.name = "Changed Target";

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
		TEST_ASSERT_EQUALS(
			provider.PendingRequests()[0].context.nearby_entities.size(),
			static_cast<size_t>(1)
		);
		TEST_ASSERT_EQUALS(
			provider.PendingRequests()[0].context.nearby_entities[0].name,
			std::string("Dockhand")
		);
		const auto serialized = SerializePublicContext(provider.PendingRequests()[0].context);
		TEST_ASSERT(serialized.find("91001") == std::string::npos);
		TEST_ASSERT(serialized.find("92002") == std::string::npos);
		TEST_ASSERT(serialized.find("93003") == std::string::npos);

		FallbackDialogue::TargetedSayResult ready_result;
		TEST_ASSERT(!queue.PopReadyResult(CurrentInteractionFor(101, 202, 202), ready_result));
	}

	void CompletedDelayedDialogueReturnsTargetSpeech()
	{
		ResetRules();

		FallbackDialogue::TestDelayedDialogueProvider provider;
		FallbackDialogue::DelayedDialogueQueue queue(provider, EnabledFallbackDialogueSettings());
		const FallbackDialogue::TargetedSayRequest request{
			.speaker_id = 101,
			.target_id = 202,
			.message = "hail",
			.target_type = FallbackDialogue::TargetType::Bot,
			.authored_dialogue_handled = false
		};
		const FallbackDialogue::PublicGameplayContextInput public_context_input{
			.current_message = "hail",
			.speaker = PublicEntity("Aten", FallbackDialogue::EntityKind::Player, 12, 0.0f, 0.0f, 0.0f),
			.target = PublicEntity("Atenbot", FallbackDialogue::EntityKind::Bot, 12, 5.0f, 0.0f, 0.0f),
			.zone = PublicZone("qeynos", "South Qeynos")
		};

		const auto queued_result = queue.HandleTargetedSay(request, public_context_input);
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

		FallbackDialogue::TestDelayedDialogueProvider provider;
		FallbackDialogue::DelayedDialogueQueue queue(provider, EnabledFallbackDialogueSettings());
		const FallbackDialogue::TargetedSayRequest request{
			.speaker_id = 101,
			.target_id = 202,
			.message = "hail",
			.target_type = FallbackDialogue::TargetType::NPC,
			.authored_dialogue_handled = false
		};
		const FallbackDialogue::PublicGameplayContextInput public_context_input{
			.current_message = "hail",
			.speaker = PublicEntity("Aten", FallbackDialogue::EntityKind::Player, 12, 0.0f, 0.0f, 0.0f),
			.target = PublicEntity("Guard Teren", FallbackDialogue::EntityKind::NPC, 22, 5.0f, 0.0f, 0.0f),
			.zone = PublicZone("qeynos", "South Qeynos")
		};

		const auto queued_result = queue.HandleTargetedSay(request, public_context_input);
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

		FallbackDialogue::TestDelayedDialogueProvider provider;
		FallbackDialogue::DelayedDialogueQueue queue(provider, EnabledFallbackDialogueSettings());
		const FallbackDialogue::TargetedSayRequest request{
			.speaker_id = 101,
			.target_id = 202,
			.message = "hail",
			.target_type = FallbackDialogue::TargetType::NPC,
			.authored_dialogue_handled = false
		};
		const FallbackDialogue::PublicGameplayContextInput public_context_input{
			.current_message = "hail",
			.speaker = PublicEntity("Aten", FallbackDialogue::EntityKind::Player, 12, 0.0f, 0.0f, 0.0f),
			.target = PublicEntity("Guard Teren", FallbackDialogue::EntityKind::NPC, 22, 5.0f, 0.0f, 0.0f),
			.zone = PublicZone("qeynos", "South Qeynos")
		};

		const auto queued_result = queue.HandleTargetedSay(request, public_context_input);
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

		FallbackDialogue::TestDelayedDialogueProvider provider;
		FallbackDialogue::DelayedDialogueQueue queue(provider, EnabledFallbackDialogueSettings());
		const FallbackDialogue::TargetedSayRequest request{
			.speaker_id = 101,
			.target_id = 202,
			.message = "hail",
			.target_type = FallbackDialogue::TargetType::NPC,
			.authored_dialogue_handled = false
		};
		const FallbackDialogue::PublicGameplayContextInput public_context_input{
			.current_message = "hail",
			.speaker = PublicEntity("Aten", FallbackDialogue::EntityKind::Player, 12, 0.0f, 0.0f, 0.0f),
			.target = PublicEntity("Guard Teren", FallbackDialogue::EntityKind::NPC, 22, 5.0f, 0.0f, 0.0f),
			.zone = PublicZone("qeynos", "South Qeynos")
		};

		TEST_ASSERT(queue.HandleTargetedSay(request, public_context_input).handled);
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

	void CompletedNpcDelayedDialogueStripsTargetNameFromEmote()
	{
		ResetRules();

		FallbackDialogue::TestDelayedDialogueProvider provider;
		FallbackDialogue::DelayedDialogueQueue queue(provider, EnabledFallbackDialogueSettings());
		const FallbackDialogue::TargetedSayRequest request{
			.speaker_id = 101,
			.target_id = 202,
			.message = "hail",
			.target_type = FallbackDialogue::TargetType::NPC,
			.authored_dialogue_handled = false
		};
		const FallbackDialogue::PublicGameplayContextInput public_context_input{
			.current_message = "hail",
			.speaker = PublicEntity("Aten", FallbackDialogue::EntityKind::Player, 12, 0.0f, 0.0f, 0.0f),
			.target = PublicEntity("Guard Teren", FallbackDialogue::EntityKind::NPC, 22, 5.0f, 0.0f, 0.0f),
			.zone = PublicZone("qeynos", "South Qeynos")
		};

		TEST_ASSERT(queue.HandleTargetedSay(request, public_context_input).handled);
		TEST_ASSERT(provider.CompleteNextSuccess("*Guard Teren looks around warily*"));

		FallbackDialogue::TargetedSayResult ready_result;
		TEST_ASSERT(queue.PopReadyResult(CurrentInteractionFor(
			101,
			202,
			202,
			PublicEntity("Aten", FallbackDialogue::EntityKind::Player, 12, 0.0f, 0.0f, 0.0f),
			PublicEntity("Guard Teren", FallbackDialogue::EntityKind::NPC, 22, 5.0f, 0.0f, 0.0f)
		), ready_result));
		TEST_ASSERT(ready_result.output_type == FallbackDialogue::OutputType::Emote);
		TEST_ASSERT_EQUALS(std::string("looks around warily"), ready_result.message);
	}

	void CompletedDelayedDialoguePreservesMixedSpeechAndEmoteOrder()
	{
		ResetRules();

		FallbackDialogue::TestDelayedDialogueProvider provider;
		FallbackDialogue::DelayedDialogueQueue queue(provider, EnabledFallbackDialogueSettings());
		const FallbackDialogue::TargetedSayRequest request{
			.speaker_id = 101,
			.target_id = 202,
			.message = "hail",
			.target_type = FallbackDialogue::TargetType::NPC,
			.authored_dialogue_handled = false
		};
		const FallbackDialogue::PublicGameplayContextInput public_context_input{
			.current_message = "hail",
			.speaker = PublicEntity("Aten", FallbackDialogue::EntityKind::Player, 12, 0.0f, 0.0f, 0.0f),
			.target = PublicEntity("Guard Teren", FallbackDialogue::EntityKind::NPC, 22, 5.0f, 0.0f, 0.0f),
			.zone = PublicZone("qeynos", "South Qeynos")
		};

		TEST_ASSERT(queue.HandleTargetedSay(request, public_context_input).handled);
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

	void CompletedMixedDelayedDialogueStripsTargetNameFromEmoteOnly()
	{
		ResetRules();

		FallbackDialogue::TestDelayedDialogueProvider provider;
		FallbackDialogue::DelayedDialogueQueue queue(provider, EnabledFallbackDialogueSettings());
		const FallbackDialogue::TargetedSayRequest request{
			.speaker_id = 101,
			.target_id = 202,
			.message = "hail",
			.target_type = FallbackDialogue::TargetType::NPC,
			.authored_dialogue_handled = false
		};
		const FallbackDialogue::PublicGameplayContextInput public_context_input{
			.current_message = "hail",
			.speaker = PublicEntity("Aten", FallbackDialogue::EntityKind::Player, 12, 0.0f, 0.0f, 0.0f),
			.target = PublicEntity("Guard Teren", FallbackDialogue::EntityKind::NPC, 22, 5.0f, 0.0f, 0.0f),
			.zone = PublicZone("qeynos", "South Qeynos")
		};

		TEST_ASSERT(queue.HandleTargetedSay(request, public_context_input).handled);
		TEST_ASSERT(provider.CompleteNextSuccess("Stand near. *Guard Teren checks the road* Keep watch."));

		FallbackDialogue::TargetedSayResult first_result;
		TEST_ASSERT(queue.PopReadyResult(CurrentInteractionFor(101, 202, 202), first_result));
		TEST_ASSERT(first_result.output_type == FallbackDialogue::OutputType::Say);
		TEST_ASSERT_EQUALS(first_result.message, std::string("Stand near."));

		FallbackDialogue::TargetedSayResult second_result;
		TEST_ASSERT(queue.PopReadyResult(CurrentInteractionFor(101, 202, 202), second_result));
		TEST_ASSERT(second_result.output_type == FallbackDialogue::OutputType::Emote);
		TEST_ASSERT_EQUALS(std::string("checks the road"), second_result.message);

		FallbackDialogue::TargetedSayResult third_result;
		TEST_ASSERT(queue.PopReadyResult(CurrentInteractionFor(101, 202, 202), third_result));
		TEST_ASSERT(third_result.output_type == FallbackDialogue::OutputType::Say);
		TEST_ASSERT_EQUALS(third_result.message, std::string("Keep watch."));
	}

	void CompletedBotDelayedDialogueReturnsParenthesizedActionAsTargetEmote()
	{
		ResetRules();

		FallbackDialogue::TestDelayedDialogueProvider provider;
		FallbackDialogue::DelayedDialogueQueue queue(provider, EnabledFallbackDialogueSettings());
		const FallbackDialogue::TargetedSayRequest request{
			.speaker_id = 101,
			.target_id = 202,
			.message = "hail",
			.target_type = FallbackDialogue::TargetType::Bot,
			.authored_dialogue_handled = false
		};
		const FallbackDialogue::PublicGameplayContextInput public_context_input{
			.current_message = "hail",
			.speaker = PublicEntity("Aten", FallbackDialogue::EntityKind::Player, 12, 0.0f, 0.0f, 0.0f),
			.target = PublicEntity("Atenbot", FallbackDialogue::EntityKind::Bot, 12, 5.0f, 0.0f, 0.0f),
			.zone = PublicZone("qeynos", "South Qeynos")
		};

		TEST_ASSERT(queue.HandleTargetedSay(request, public_context_input).handled);
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

	void CompletedBotDelayedDialogueStripsTargetNameFromEmote()
	{
		ResetRules();

		FallbackDialogue::TestDelayedDialogueProvider provider;
		FallbackDialogue::DelayedDialogueQueue queue(provider, EnabledFallbackDialogueSettings());
		const FallbackDialogue::TargetedSayRequest request{
			.speaker_id = 101,
			.target_id = 202,
			.message = "hail",
			.target_type = FallbackDialogue::TargetType::Bot,
			.authored_dialogue_handled = false
		};
		const FallbackDialogue::PublicGameplayContextInput public_context_input{
			.current_message = "hail",
			.speaker = PublicEntity("Aten", FallbackDialogue::EntityKind::Player, 12, 0.0f, 0.0f, 0.0f),
			.target = PublicEntity("Atenbot", FallbackDialogue::EntityKind::Bot, 12, 5.0f, 0.0f, 0.0f),
			.zone = PublicZone("qeynos", "South Qeynos")
		};

		TEST_ASSERT(queue.HandleTargetedSay(request, public_context_input).handled);
		TEST_ASSERT(provider.CompleteNextSuccess("(Atenbot checks bowstring)"));

		FallbackDialogue::TargetedSayResult ready_result;
		TEST_ASSERT(queue.PopReadyResult(CurrentInteractionFor(
			101,
			202,
			202,
			PublicEntity("Aten", FallbackDialogue::EntityKind::Player, 12, 0.0f, 0.0f, 0.0f),
			PublicEntity("Atenbot", FallbackDialogue::EntityKind::Bot, 12, 5.0f, 0.0f, 0.0f)
		), ready_result));
		TEST_ASSERT(ready_result.output_type == FallbackDialogue::OutputType::Emote);
		TEST_ASSERT_EQUALS(std::string("checks bowstring"), ready_result.message);
		TEST_ASSERT(ready_result.target_type == FallbackDialogue::TargetType::Bot);
	}

	void CompletedDelayedDialogueDoesNotStripTargetNameFromSpeech()
	{
		const auto ready_result = DelayedSuccessResultFor(
			"Guard Teren says the west gate is watched.",
			"appears distracted."
		);

		TEST_ASSERT(ready_result.handled);
		TEST_ASSERT(ready_result.output_type == FallbackDialogue::OutputType::Say);
		TEST_ASSERT_EQUALS(ready_result.message, std::string("Guard Teren says the west gate is watched."));
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

		FallbackDialogue::TestDelayedDialogueProvider provider;
		FallbackDialogue::DelayedDialogueQueue queue(provider, EnabledFallbackDialogueSettings(
			"appears distracted.",
			30,
			24
		));
		const FallbackDialogue::TargetedSayRequest request{
			.speaker_id = 101,
			.target_id = 202,
			.message = "hail",
			.target_type = FallbackDialogue::TargetType::NPC,
			.authored_dialogue_handled = false
		};
		const FallbackDialogue::PublicGameplayContextInput public_context_input{
			.current_message = "hail",
			.speaker = PublicEntity("Aten", FallbackDialogue::EntityKind::Player, 12, 0.0f, 0.0f, 0.0f),
			.target = PublicEntity("Guard Teren", FallbackDialogue::EntityKind::NPC, 22, 5.0f, 0.0f, 0.0f),
			.zone = PublicZone("qeynos", "South Qeynos")
		};

		const auto queued_result = queue.HandleTargetedSay(request, public_context_input);
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

		FallbackDialogue::TestDelayedDialogueProvider provider;
		FallbackDialogue::DelayedDialogueQueue queue(provider, EnabledFallbackDialogueSettings(
			"appears distracted.",
			30,
			8
		));
		const FallbackDialogue::TargetedSayRequest request{
			.speaker_id = 101,
			.target_id = 202,
			.message = "hail",
			.target_type = FallbackDialogue::TargetType::NPC,
			.authored_dialogue_handled = false
		};
		const FallbackDialogue::PublicGameplayContextInput public_context_input{
			.current_message = "hail",
			.speaker = PublicEntity("Aten", FallbackDialogue::EntityKind::Player, 12, 0.0f, 0.0f, 0.0f),
			.target = PublicEntity("Guard Teren", FallbackDialogue::EntityKind::NPC, 22, 5.0f, 0.0f, 0.0f),
			.zone = PublicZone("qeynos", "South Qeynos")
		};

		TEST_ASSERT(queue.HandleTargetedSay(request, public_context_input).handled);
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

		FallbackDialogue::TestDelayedDialogueProvider provider;
		FallbackDialogue::DelayedDialogueQueue queue(provider, EnabledFallbackDialogueSettings(
			"appears distracted.",
			30,
			16
		));
		const FallbackDialogue::TargetedSayRequest request{
			.speaker_id = 101,
			.target_id = 202,
			.message = "hail",
			.target_type = FallbackDialogue::TargetType::Bot,
			.authored_dialogue_handled = false
		};
		const FallbackDialogue::PublicGameplayContextInput public_context_input{
			.current_message = "hail",
			.speaker = PublicEntity("Aten", FallbackDialogue::EntityKind::Player, 12, 0.0f, 0.0f, 0.0f),
			.target = PublicEntity("Atenbot", FallbackDialogue::EntityKind::Bot, 12, 5.0f, 0.0f, 0.0f),
			.zone = PublicZone("qeynos", "South Qeynos")
		};

		TEST_ASSERT(queue.HandleTargetedSay(request, public_context_input).handled);
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

	void LongEmoteDelayedDialogueFallsBackToUnavailableReply()
	{
		ResetRules();

		FallbackDialogue::TestDelayedDialogueProvider provider;
		FallbackDialogue::DelayedDialogueQueue queue(
			provider,
			EnabledFallbackDialogueSettings("appears distracted.", 30, 16)
		);
		const FallbackDialogue::TargetedSayRequest request{
			.speaker_id = 101,
			.target_id = 202,
			.message = "hail",
			.target_type = FallbackDialogue::TargetType::NPC,
			.authored_dialogue_handled = false
		};

		const auto queued_result = queue.HandleTargetedSay(request, DefaultPublicGameplayContextInput());
		TEST_ASSERT(queued_result.handled);
		TEST_ASSERT(provider.CompleteNextSuccess("*looks around warily*"));

		FallbackDialogue::TargetedSayResult ready_result;
		TEST_ASSERT(queue.PopReadyResult(CurrentInteractionFor(101, 202, 202), ready_result));
		TEST_ASSERT(ready_result.output_type == FallbackDialogue::OutputType::Emote);
		TEST_ASSERT_EQUALS(ready_result.message, std::string("appears distracted."));
		TEST_ASSERT_EQUALS(ready_result.debug_reason, std::string("delayed_dialogue_rejected"));
	}

	void FailedDelayedDialogueReturnsUnavailableReplyEmote()
	{
		ResetRules();

		FallbackDialogue::TestDelayedDialogueProvider provider;
		FallbackDialogue::DelayedDialogueQueue queue(
			provider,
			EnabledFallbackDialogueSettings("seems lost in thought.")
		);
		const FallbackDialogue::TargetedSayRequest request{
			.speaker_id = 101,
			.target_id = 202,
			.message = "hail",
			.target_type = FallbackDialogue::TargetType::NPC,
			.authored_dialogue_handled = false
		};
		const FallbackDialogue::PublicGameplayContextInput public_context_input{
			.current_message = "hail",
			.speaker = PublicEntity("Aten", FallbackDialogue::EntityKind::Player, 12, 0.0f, 0.0f, 0.0f),
			.target = PublicEntity("Guard Teren", FallbackDialogue::EntityKind::NPC, 22, 5.0f, 0.0f, 0.0f),
			.zone = PublicZone("qeynos", "South Qeynos")
		};

		const auto queued_result = queue.HandleTargetedSay(request, public_context_input);
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

		FallbackDialogue::TestDelayedDialogueProvider provider;
		FallbackDialogue::DelayedDialogueQueue queue(provider, EnabledFallbackDialogueSettings());
		const FallbackDialogue::TargetedSayRequest request{
			.speaker_id = 101,
			.target_id = 202,
			.message = "hail",
			.target_type = FallbackDialogue::TargetType::NPC,
			.authored_dialogue_handled = false
		};
		const FallbackDialogue::PublicGameplayContextInput public_context_input{
			.current_message = "hail",
			.speaker = PublicEntity("Aten", FallbackDialogue::EntityKind::Player, 12, 0.0f, 0.0f, 0.0f),
			.target = PublicEntity("Guard Teren", FallbackDialogue::EntityKind::NPC, 22, 10.0f, 0.0f, 5.0f),
			.zone = PublicZone("qeynos", "South Qeynos")
		};

		const auto queued_result = queue.HandleTargetedSay(request, public_context_input);
		TEST_ASSERT(queued_result.handled);
		TEST_ASSERT(provider.CompleteNextSuccess("Well met."));

		FallbackDialogue::TargetedSayResult ready_result;
		TEST_ASSERT(queue.PopReadyResult({
			.speaker_id = 101,
			.target_id = 202,
			.speaker_target_id = 202,
			.speaker_present = true,
			.target_present = true,
			.speaker = BuildCurrentInteractionEntityState(0.0f, 0.0f, 0.0f),
			.target = BuildCurrentInteractionEntityState(10.0f, 0.0f, 80.0f)
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

		FallbackDialogue::TestDelayedDialogueProvider provider;
		FallbackDialogue::DelayedDialogueQueue queue(provider, EnabledFallbackDialogueSettings());
		const FallbackDialogue::TargetedSayRequest request{
			.speaker_id = 101,
			.target_id = 202,
			.message = "hail",
			.target_type = FallbackDialogue::TargetType::NPC,
			.authored_dialogue_handled = false
		};
		const FallbackDialogue::PublicGameplayContextInput public_context_input{
			.current_message = "hail",
			.speaker = PublicEntity("Aten", FallbackDialogue::EntityKind::Player, 12, 0.0f, 0.0f, 0.0f),
			.target = PublicEntity("Guard Teren", FallbackDialogue::EntityKind::NPC, 22, 5.0f, 0.0f, 0.0f),
			.zone = PublicZone("qeynos", "South Qeynos")
		};

		const auto queued_result = queue.HandleTargetedSay(request, public_context_input);
		TEST_ASSERT(queued_result.handled);
		TEST_ASSERT(provider.CompleteNextSuccess("Well met."));

		FallbackDialogue::TargetedSayResult ready_result;
		TEST_ASSERT(!queue.PopReadyResult({
			.speaker_id = 101,
			.target_id = 202,
			.speaker_target_id = 303,
			.speaker_present = true,
			.target_present = true,
			.speaker = BuildCurrentInteractionEntityState(0.0f, 0.0f, 0.0f),
			.target = BuildCurrentInteractionEntityState(5.0f, 0.0f, 0.0f)
		}, ready_result));
		TEST_ASSERT(!ready_result.handled);
		TEST_ASSERT(ready_result.output_type == FallbackDialogue::OutputType::None);
		TEST_ASSERT(ready_result.message.empty());
		TEST_ASSERT_EQUALS(ready_result.debug_reason, std::string("delayed_dialogue_dropped_target_changed"));
	}

	void FailedDelayedDialogueDropsWhenSpeakerLeavesSayRange()
	{
		ResetRules();

		FallbackDialogue::TestDelayedDialogueProvider provider;
		FallbackDialogue::DelayedDialogueQueue queue(
			provider,
			EnabledFallbackDialogueSettings("seems lost in thought.", 30, 200, 15)
		);
		const FallbackDialogue::TargetedSayRequest request{
			.speaker_id = 101,
			.target_id = 202,
			.message = "hail",
			.target_type = FallbackDialogue::TargetType::NPC,
			.authored_dialogue_handled = false
		};
		const FallbackDialogue::PublicGameplayContextInput public_context_input{
			.current_message = "hail",
			.speaker = PublicEntity("Aten", FallbackDialogue::EntityKind::Player, 12, 0.0f, 0.0f, 0.0f),
			.target = PublicEntity("Guard Teren", FallbackDialogue::EntityKind::NPC, 22, 5.0f, 0.0f, 0.0f),
			.zone = PublicZone("qeynos", "South Qeynos")
		};

		const auto queued_result = queue.HandleTargetedSay(request, public_context_input);
		TEST_ASSERT(queued_result.handled);
		TEST_ASSERT(provider.CompleteNextFailure());

		FallbackDialogue::TargetedSayResult ready_result;
		TEST_ASSERT(!queue.PopReadyResult({
			.speaker_id = 101,
			.target_id = 202,
			.speaker_target_id = 202,
			.speaker_present = true,
			.target_present = true,
			.speaker = BuildCurrentInteractionEntityState(0.0f, 0.0f, 90.0f),
			.target = BuildCurrentInteractionEntityState(16.0f, 0.0f, 0.0f)
		}, ready_result));
		TEST_ASSERT(!ready_result.handled);
		TEST_ASSERT(ready_result.output_type == FallbackDialogue::OutputType::None);
		TEST_ASSERT(ready_result.message.empty());
		TEST_ASSERT_EQUALS(ready_result.debug_reason, std::string("delayed_dialogue_dropped_out_of_say_range"));
	}

	void CompletedDelayedDialogueDropsWhenTargetIsMissing()
	{
		ResetRules();

		FallbackDialogue::TestDelayedDialogueProvider provider;
		FallbackDialogue::DelayedDialogueQueue queue(provider, EnabledFallbackDialogueSettings());
		const FallbackDialogue::TargetedSayRequest request{
			.speaker_id = 101,
			.target_id = 202,
			.message = "hail",
			.target_type = FallbackDialogue::TargetType::NPC,
			.authored_dialogue_handled = false
		};
		const FallbackDialogue::PublicGameplayContextInput public_context_input{
			.current_message = "hail",
			.speaker = PublicEntity("Aten", FallbackDialogue::EntityKind::Player, 12, 0.0f, 0.0f, 0.0f),
			.target = PublicEntity("Guard Teren", FallbackDialogue::EntityKind::NPC, 22, 5.0f, 0.0f, 0.0f),
			.zone = PublicZone("qeynos", "South Qeynos")
		};

		const auto queued_result = queue.HandleTargetedSay(request, public_context_input);
		TEST_ASSERT(queued_result.handled);
		TEST_ASSERT(provider.CompleteNextSuccess("Well met."));

		FallbackDialogue::TargetedSayResult ready_result;
		TEST_ASSERT(!queue.PopReadyResult({
			.speaker_id = 101,
			.target_id = 0,
			.speaker_target_id = 202,
			.speaker_present = true,
			.target_present = false,
			.speaker = BuildCurrentInteractionEntityState(0.0f, 0.0f, 0.0f)
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

		const auto ready_result = OllamaResultFor(transport, {
			.current_message = "hail friend",
			.speaker = PublicEntityWithId(91001, "Aten", FallbackDialogue::EntityKind::Player, 12, 1111.0f, 2222.0f, 3333.0f),
			.target = PublicEntityWithId(92002, "Guard Teren", FallbackDialogue::EntityKind::NPC, 22, 1114.0f, 2226.0f, 3333.0f),
			.zone = PublicZone("qeynos", "South Qeynos"),
			.nearby_entities = {
				PublicEntityWithId(93003, "Dockhand", FallbackDialogue::EntityKind::NPC, 8, 1111.0f, 2228.0f, 3333.0f)
			}
		});

		TEST_ASSERT(ready_result.handled);
		TEST_ASSERT_EQUALS(transport.Calls().size(), static_cast<size_t>(1));
		TEST_ASSERT_EQUALS(transport.Calls()[0].endpoint, std::string("http://ollama.test:11434/api/generate"));
		TEST_ASSERT_EQUALS(transport.Calls()[0].timeout_ms, 1234);
		TEST_ASSERT(transport.Calls()[0].body.find("\"model\":\"test-model\"") != std::string::npos);
		TEST_ASSERT(transport.Calls()[0].body.find("Write one short in-character Dialogue Response") != std::string::npos);
		TEST_ASSERT(transport.Calls()[0].body.find("simple stage directions in *...*") != std::string::npos);
		TEST_ASSERT(transport.Calls()[0].body.find("Do not include metadata, commands, JSON, or explanations") != std::string::npos);
		TEST_ASSERT(transport.Calls()[0].body.find("Dialogue Line") == std::string::npos);
		TEST_ASSERT(transport.Calls()[0].body.find("multiple messages") == std::string::npos);
		TEST_ASSERT(transport.Calls()[0].body.find("hail friend") != std::string::npos);
		TEST_ASSERT(transport.Calls()[0].body.find("Aten") != std::string::npos);
		TEST_ASSERT(transport.Calls()[0].body.find("Guard Teren") != std::string::npos);
		TEST_ASSERT(transport.Calls()[0].body.find("Dockhand") != std::string::npos);
		TEST_ASSERT(transport.Calls()[0].body.find("level 12") != std::string::npos);
		TEST_ASSERT(transport.Calls()[0].body.find("level 22") != std::string::npos);
		TEST_ASSERT(transport.Calls()[0].body.find("South Qeynos") != std::string::npos);
		TEST_ASSERT(transport.Calls()[0].body.find("91001") == std::string::npos);
		TEST_ASSERT(transport.Calls()[0].body.find("92002") == std::string::npos);
		TEST_ASSERT(transport.Calls()[0].body.find("93003") == std::string::npos);
		TEST_ASSERT(transport.Calls()[0].body.find("1111") == std::string::npos);
		TEST_ASSERT(transport.Calls()[0].body.find("1114") == std::string::npos);
		TEST_ASSERT(transport.Calls()[0].body.find("2222") == std::string::npos);
		TEST_ASSERT(transport.Calls()[0].body.find("2226") == std::string::npos);
		TEST_ASSERT(transport.Calls()[0].body.find("2228") == std::string::npos);
		TEST_ASSERT(transport.Calls()[0].body.find("3333") == std::string::npos);
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
			DefaultPublicGameplayContextInput(),
			OllamaProviderSettings(
				"http://ollama.test:11434/api/generate",
				"   ",
				1234
			)
		);

		TEST_ASSERT(ready_result.handled);
		TEST_ASSERT(ready_result.output_type == FallbackDialogue::OutputType::Emote);
		TEST_ASSERT_EQUALS(ready_result.message, std::string("seems lost in thought."));
		TEST_ASSERT_EQUALS(ready_result.debug_reason, std::string("delayed_dialogue_unavailable"));
		TEST_ASSERT_EQUALS(transport.Calls().size(), static_cast<size_t>(0));
	}

	void OllamaProviderInvalidEndpointFallsBackToUnavailableReply()
	{
		FakeOllamaHttpTransport transport;

		const auto ready_result = OllamaResultFor(
			transport,
			DefaultPublicGameplayContextInput(),
			OllamaProviderSettings("", "test-model", 1234)
		);

		TEST_ASSERT(ready_result.handled);
		TEST_ASSERT(ready_result.output_type == FallbackDialogue::OutputType::Emote);
		TEST_ASSERT_EQUALS(ready_result.message, std::string("seems lost in thought."));
		TEST_ASSERT_EQUALS(ready_result.debug_reason, std::string("delayed_dialogue_unavailable"));
		TEST_ASSERT_EQUALS(transport.Calls().size(), static_cast<size_t>(0));
	}

	void OllamaProviderInvalidTimeoutFallsBackToUnavailableReply()
	{
		FakeOllamaHttpTransport transport;

		const auto ready_result = OllamaResultFor(
			transport,
			DefaultPublicGameplayContextInput(),
			OllamaProviderSettings(
				"http://ollama.test:11434/api/generate",
				"test-model",
				0
			)
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

	FallbackDialogue::PublicEntityInput PublicEntity(
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

	FallbackDialogue::PublicEntityInput PublicEntityWithId(
		uint32_t entity_id,
		const std::string &name,
		FallbackDialogue::EntityKind kind,
		int level,
		float x,
		float y,
		float z
	)
	{
		auto entity = PublicEntity(name, kind, level, x, y, z);
		entity.entity_id = entity_id;
		return entity;
	}

	FallbackDialogue::PublicZoneInput PublicZone(const std::string &short_name, const std::string &long_name)
	{
		return {
			.short_name = short_name,
			.long_name = long_name
		};
	}

	FallbackDialogue::PublicGameplayContextInput DefaultPublicGameplayContextInput()
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
		const FallbackDialogue::PublicEntityInput &speaker,
		const FallbackDialogue::PublicEntityInput &target
	)
	{
		return {
			.speaker_id = speaker_id,
			.target_id = target_id,
			.speaker_target_id = speaker_target_id,
			.speaker_present = true,
			.target_present = true,
			.speaker = BuildCurrentInteractionEntityState(speaker),
			.target = BuildCurrentInteractionEntityState(target)
		};
	}

	FallbackDialogue::CurrentInteractionEntityState BuildCurrentInteractionEntityState(
		const FallbackDialogue::PublicEntityInput &entity
	)
	{
		return BuildCurrentInteractionEntityState(entity.x, entity.y, entity.z);
	}

	FallbackDialogue::CurrentInteractionEntityState BuildCurrentInteractionEntityState(
		float x,
		float y,
		float z
	)
	{
		return {
			.x = x,
			.y = y,
			.z = z
		};
	}

	FallbackDialogue::TargetedSayResult DelayedSuccessResultFor(
		const std::string &dialogue_response,
		const std::string &unavailable_reply
	)
	{
		ResetRules();

		FallbackDialogue::TestDelayedDialogueProvider provider;
		FallbackDialogue::DelayedDialogueQueue queue(
			provider,
			EnabledFallbackDialogueSettings(unavailable_reply)
		);
		const FallbackDialogue::TargetedSayRequest request{
			.speaker_id = 101,
			.target_id = 202,
			.message = "hail",
			.target_type = FallbackDialogue::TargetType::NPC,
			.authored_dialogue_handled = false
		};
		const FallbackDialogue::PublicGameplayContextInput public_context_input{
			.current_message = "hail",
			.speaker = PublicEntity("Aten", FallbackDialogue::EntityKind::Player, 12, 0.0f, 0.0f, 0.0f),
			.target = PublicEntity("Guard Teren", FallbackDialogue::EntityKind::NPC, 22, 5.0f, 0.0f, 0.0f),
			.zone = PublicZone("qeynos", "South Qeynos")
		};

		const auto queued_result = queue.HandleTargetedSay(request, public_context_input);
		if (!queued_result.handled || !provider.CompleteNextSuccess(dialogue_response)) {
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
		const FallbackDialogue::PublicGameplayContextInput &public_context_input = FallbackDialogue::PublicGameplayContextInput{},
		FallbackDialogue::OllamaProviderSettings provider_settings = FallbackDialogue::OllamaProviderSettings{
			"http://ollama.test:11434/api/generate",
			"test-model",
			1234
		}
	)
	{
		ResetRules();

		FallbackDialogue::OllamaDelayedDialogueProvider provider(transport);
		FallbackDialogue::DelayedDialogueQueue queue(
			provider,
			EnabledFallbackDialogueSettings(
				"seems lost in thought.",
				30,
				200,
				15,
				provider_settings
			)
		);
		const FallbackDialogue::TargetedSayRequest request{
			.speaker_id = 101,
			.target_id = 202,
			.message = "hail",
			.target_type = FallbackDialogue::TargetType::NPC,
			.authored_dialogue_handled = false
		};

		const auto context = public_context_input.current_message.empty() ? DefaultPublicGameplayContextInput() : public_context_input;
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
			<< context.speaker.engaged << ' '
			<< context.speaker.distance << ' '
			<< context.target.name << ' '
			<< context.target.level << ' '
			<< context.target.engaged << ' '
			<< context.target.distance << ' '
			<< context.zone.short_name << ' '
			<< context.zone.long_name;

		for (const auto &entity : context.nearby_entities) {
			serialized
				<< ' '
				<< entity.name
				<< ' '
				<< entity.level
				<< ' '
				<< entity.engaged
				<< ' '
				<< entity.distance;
		}

		return serialized.str();
	}
};

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

#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

namespace FallbackDialogue {

enum class EntityKind {
	Unknown,
	Player,
	NPC,
	Bot,
	Mercenary
};

enum class TargetType {
	Unknown,
	Mercenary,
	NPC,
	Bot
};

enum class OutputType {
	None,
	Say,
	Emote
};

struct DialogueFragment {
	OutputType  output_type = OutputType::None;
	std::string message;
};

struct DialogueResponseProcessingResult {
	bool                          accepted = false;
	std::string                   rejection_reason;
	std::vector<DialogueFragment> fragments;
};

struct DeliveredDialogueMessage {
	OutputType  output_type = OutputType::None;
	std::string message;
};

struct DialogueDeliveryPlan {
	bool                                  accepted = false;
	std::string                           rejection_reason;
	std::vector<DeliveredDialogueMessage> messages;
};

struct TargetedSayRequest {
	uint32_t    speaker_id = 0;
	uint32_t    target_id = 0;
	std::string message;
	TargetType  target_type = TargetType::Unknown;
	bool        authored_dialogue_handled = false;
	bool        target_engaged = false;
};

struct TargetedSayResult {
	bool        handled = false;
	OutputType  output_type = OutputType::None;
	std::string message;
	std::string debug_reason;
	uint32_t    speaker_id = 0;
	uint32_t    target_id = 0;
	uint32_t    visible_speaker_id = 0;
	TargetType  target_type = TargetType::Unknown;
};

struct ImmediateFallbackDialogueSettings {
	bool        enabled = false;
	int         cooldown_seconds = 30;
	std::string unavailable_reply = "appears distracted.";
};

struct PublicGameplayContextSettings {
	int nearby_context_radius = 100;
	int nearby_entity_limit = 8;
};

struct DialogueDeliverySettings {
	int max_delivered_line_length = 200;
};

struct FallbackDialogueSettings {
	ImmediateFallbackDialogueSettings immediate;
	PublicGameplayContextSettings     public_context;
	DialogueDeliverySettings          delivery;
};

struct PublicEntityInput {
	// Local-only derivation input for exclusion and interaction checks; never prompt output.
	uint32_t    entity_id = 0;
	std::string name;
	EntityKind  kind = EntityKind::Unknown;
	int         level = 0;
	// Local-only derivation inputs for distance, sorting, and radius filtering; never prompt output.
	float       x = 0.0f;
	float       y = 0.0f;
	float       z = 0.0f;
	bool        engaged = false;
};

struct PublicZoneInput {
	std::string short_name;
	std::string long_name;
};

struct PublicGameplayContextInput {
	std::string                    current_message;
	PublicEntityInput              speaker;
	PublicEntityInput              target;
	PublicZoneInput                zone;
	std::vector<PublicEntityInput> nearby_entities;
};

struct PublicEntitySummary {
	std::string name;
	EntityKind  kind = EntityKind::Unknown;
	int         level = 0;
	bool        engaged = false;
	float       distance = 0.0f;
};

struct PublicZoneIdentity {
	std::string short_name;
	std::string long_name;
};

struct PublicGameplayContext {
	std::string                      current_message;
	PublicEntitySummary             speaker;
	PublicEntitySummary             target;
	PublicZoneIdentity              zone;
	std::vector<PublicEntitySummary> nearby_entities;
};

struct DelayedDialogueRequest {
	uint64_t              request_id = 0;
	uint32_t              speaker_id = 0;
	uint32_t              target_id = 0;
	TargetType            target_type = TargetType::Unknown;
	PublicGameplayContext context;
	std::string           unavailable_reply;
};

struct DelayedDialogueCompletion {
	uint64_t    request_id = 0;
	bool        succeeded = false;
	std::string dialogue_response;
};

struct CurrentInteractionEntityState {
	float x = 0.0f;
	float y = 0.0f;
	float z = 0.0f;
};

struct CurrentInteraction {
	uint32_t          speaker_id = 0;
	uint32_t          target_id = 0;
	uint32_t          speaker_target_id = 0;
	bool              speaker_present = false;
	bool              target_present = false;
	CurrentInteractionEntityState speaker;
	CurrentInteractionEntityState target;
};

class DelayedDialogueProvider {
public:
	virtual ~DelayedDialogueProvider() = default;

	virtual void Enqueue(const DelayedDialogueRequest &request) = 0;
	virtual bool PopCompletion(DelayedDialogueCompletion &completion) = 0;
};

struct OllamaHttpResponse {
	bool        completed = false;
	int         status = 0;
	std::string body;
};

class OllamaHttpTransport {
public:
	virtual ~OllamaHttpTransport() = default;

	virtual OllamaHttpResponse PostJson(
		const std::string &endpoint,
		const std::string &body,
		int timeout_ms
	) = 0;
};

class OllamaDelayedDialogueProvider : public DelayedDialogueProvider {
public:
	OllamaDelayedDialogueProvider();
	explicit OllamaDelayedDialogueProvider(OllamaHttpTransport &transport);
	~OllamaDelayedDialogueProvider() override;

	void Enqueue(const DelayedDialogueRequest &request) override;
	bool PopCompletion(DelayedDialogueCompletion &completion) override;

private:
	void PushCompletion(DelayedDialogueCompletion completion);

	std::unique_ptr<OllamaHttpTransport> owned_transport_;
	OllamaHttpTransport                  &transport_;
	std::mutex                           completions_mutex_;
	std::deque<DelayedDialogueCompletion> completions_;
	std::vector<std::thread>             workers_;
};

class DelayedDialogueQueue {
public:
	using CurrentInteractionResolver = std::function<CurrentInteraction(const DelayedDialogueRequest &request)>;

	DelayedDialogueQueue(DelayedDialogueProvider &provider, FallbackDialogueSettings settings);

	TargetedSayResult HandleTargetedSay(
		const TargetedSayRequest &request,
		const PublicGameplayContextInput &public_context_input
	);
	bool PopReadyResult(const CurrentInteraction &interaction, TargetedSayResult &result);
	bool PopReadyResult(const CurrentInteractionResolver &resolver, TargetedSayResult &result);

private:
	DelayedDialogueProvider                         &provider_;
	FallbackDialogueSettings                         settings_;
	std::unordered_map<uint64_t, DelayedDialogueRequest> pending_requests_;
	std::deque<TargetedSayResult>                   ready_results_;
	uint64_t                                         next_request_id_ = 1;
};

class TestDelayedDialogueProvider : public DelayedDialogueProvider {
public:
	void Enqueue(const DelayedDialogueRequest &request) override;
	bool PopCompletion(DelayedDialogueCompletion &completion) override;

	const std::vector<DelayedDialogueRequest> &PendingRequests() const;
	bool CompleteNextSuccess(const std::string &dialogue_response);
	bool CompleteNextFailure();

private:
	std::vector<DelayedDialogueRequest> pending_requests_;
	std::deque<DelayedDialogueCompletion> completions_;
};

TargetedSayResult HandleTargetedSay(
	const TargetedSayRequest &request,
	const FallbackDialogueSettings &settings
);
PublicGameplayContext BuildPublicGameplayContext(
	const PublicGameplayContextInput &public_context_input,
	const PublicGameplayContextSettings &settings
);
DialogueResponseProcessingResult ProcessDialogueResponse(
	const std::string &natural_dialogue_response,
	const std::string &target_name
);
DialogueDeliveryPlan PlanDialogueDelivery(
	const std::vector<DialogueFragment> &fragments,
	const DialogueDeliverySettings &settings
);
std::string BuildDiagnosticLogLine(const TargetedSayResult &result);
void LogDiagnostic(const TargetedSayResult &result);
void ResetDialogueCooldowns();

}

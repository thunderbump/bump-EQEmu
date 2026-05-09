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
#include "fallback_dialogue.h"

#include "common/eqemu_logsys_log_aliases.h"
#include "common/http/httplib.h"
#include "common/json/json.h"
#include "common/rulesys.h"
#include "common/timer.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <sstream>
#include <utility>
#include <unordered_map>

namespace FallbackDialogue {

namespace {

uint64_t CooldownKey(uint32_t speaker_id, uint32_t target_id)
{
	return (static_cast<uint64_t>(speaker_id) << 32) | target_id;
}

std::unordered_map<uint64_t, uint32_t> dialogue_cooldowns;

void RemoveExpiredDialogueCooldowns(uint32_t current_time, uint32_t cooldown_ms)
{
	for (auto cooldown = dialogue_cooldowns.begin(); cooldown != dialogue_cooldowns.end();) {
		if (current_time - cooldown->second >= cooldown_ms) {
			cooldown = dialogue_cooldowns.erase(cooldown);
		} else {
			++cooldown;
		}
	}
}

bool IsDialogueCooldownActive(const TargetedSayRequest &request)
{
	const int cooldown_seconds = RuleI(Chat, FallbackDialogueCooldownSeconds);
	if (cooldown_seconds <= 0) {
		return false;
	}

	const auto cooldown_ms = static_cast<uint32_t>(cooldown_seconds) * 1000;
	const auto current_time = Timer::GetCurrentTime();
	const auto key = CooldownKey(request.speaker_id, request.target_id);
	const auto cooldown = dialogue_cooldowns.find(key);

	if (cooldown == dialogue_cooldowns.end()) {
		return false;
	}

	return current_time - cooldown->second < cooldown_ms;
}

void StartDialogueCooldown(const TargetedSayRequest &request)
{
	const int cooldown_seconds = RuleI(Chat, FallbackDialogueCooldownSeconds);
	if (cooldown_seconds <= 0) {
		return;
	}

	const auto cooldown_ms = static_cast<uint32_t>(cooldown_seconds) * 1000;
	const auto current_time = Timer::GetCurrentTime();

	RemoveExpiredDialogueCooldowns(current_time, cooldown_ms);
	dialogue_cooldowns[CooldownKey(request.speaker_id, request.target_id)] = current_time;
}

float DistanceBetween(const LiveEntity &first, const LiveEntity &second)
{
	const auto x = first.x - second.x;
	const auto y = first.y - second.y;
	const auto z = first.z - second.z;

	return std::sqrt((x * x) + (y * y) + (z * z));
}

float DistanceNoZBetween(const LiveEntity &first, const LiveEntity &second)
{
	const auto x = first.x - second.x;
	const auto y = first.y - second.y;

	return std::sqrt((x * x) + (y * y));
}

PublicEntitySummary BuildPublicEntitySummary(const LiveEntity &entity, float distance = 0.0f)
{
	return {
		.name = entity.name,
		.kind = entity.kind,
		.level = entity.level,
		.engaged = entity.engaged,
		.distance = distance
	};
}

std::string TrimCopy(const std::string &value)
{
	const auto first = std::find_if(
		value.begin(),
		value.end(),
		[](unsigned char character) {
			return !std::isspace(character);
		}
	);

	if (first == value.end()) {
		return {};
	}

	const auto last = std::find_if(
		value.rbegin(),
		value.rend(),
		[](unsigned char character) {
			return !std::isspace(character);
		}
	).base();

	return std::string(first, last);
}

std::string ToLowerCopy(std::string value)
{
	std::transform(
		value.begin(),
		value.end(),
		value.begin(),
		[](unsigned char character) {
			return static_cast<char>(std::tolower(character));
		}
	);

	return value;
}

bool StartsWithInsensitive(const std::string &value, const std::string &prefix)
{
	if (value.size() < prefix.size()) {
		return false;
	}

	return ToLowerCopy(value.substr(0, prefix.size())) == prefix;
}

TargetedSayResult EligibleTargetedSayResult(const TargetedSayRequest &request)
{
	if (!RuleB(Chat, FallbackDialogueEnabled)) {
		return {};
	}

	if (request.authored_dialogue_handled) {
		return {
			.debug_reason = "authored_dialogue_handled"
		};
	}

	if (request.target_engaged && request.target_type == TargetType::NPC) {
		return {
			.debug_reason = "target_engaged"
		};
	}

	if (request.target_type != TargetType::NPC && request.target_type != TargetType::Bot) {
		return {
			.debug_reason = "unsupported_target_type"
		};
	}

	if (IsDialogueCooldownActive(request)) {
		return {
			.debug_reason = "dialogue_cooldown"
		};
	}

	return {
		.handled = true,
		.speaker_id = request.speaker_id,
		.target_id = request.target_id,
		.target_type = request.target_type
	};
}

std::string ValidateCurrentInteraction(
	const DelayedDialogueRequest &request,
	const CurrentInteraction &interaction
)
{
	if (!interaction.speaker_present || interaction.speaker_id != request.speaker_id) {
		return "delayed_dialogue_dropped_missing_speaker";
	}

	if (!interaction.target_present || interaction.target_id != request.target_id) {
		return "delayed_dialogue_dropped_missing_target";
	}

	if (interaction.speaker_target_id != request.target_id) {
		return "delayed_dialogue_dropped_target_changed";
	}

	if (DistanceNoZBetween(interaction.speaker, interaction.target) > RuleI(Range, Say)) {
		return "delayed_dialogue_dropped_out_of_say_range";
	}

	return {};
}

std::string NormalizeDialogueLine(const std::string &dialogue_line)
{
	std::string normalized;
	normalized.reserve(dialogue_line.size());

	bool pending_space = false;
	for (const auto character : dialogue_line) {
		if (std::isspace(static_cast<unsigned char>(character))) {
			pending_space = !normalized.empty();
			continue;
		}

		if (pending_space) {
			normalized.push_back(' ');
			pending_space = false;
		}

		normalized.push_back(character);
	}

	normalized = TrimCopy(normalized);
	for (const auto *prefix : {"assistant:", "response:", "dialogue:", "dialogue line:"}) {
		if (StartsWithInsensitive(normalized, prefix)) {
			normalized = TrimCopy(normalized.substr(std::string(prefix).size()));
			break;
		}
	}

	if (normalized.size() >= 2 && normalized.front() == '"' && normalized.back() == '"') {
		normalized = TrimCopy(normalized.substr(1, normalized.size() - 2));
	}

	return normalized;
}

std::vector<std::string> SplitDialogueLine(const std::string &dialogue_line)
{
	const auto max_line_length = RuleI(Chat, FallbackDialogueMaxLineLength);
	if (max_line_length <= 0 || dialogue_line.size() <= static_cast<size_t>(max_line_length)) {
		return dialogue_line.empty() ? std::vector<std::string>{} : std::vector<std::string>{dialogue_line};
	}

	const auto limit = static_cast<size_t>(max_line_length);
	std::vector<std::string> fragments;
	std::string remaining = dialogue_line;

	while (remaining.size() > limit) {
		auto split_at = std::string::npos;

		for (auto index = limit; index > 0; --index) {
			const auto character = remaining[index - 1];
			if ((character == '.' || character == '!' || character == '?') &&
				(index == remaining.size() || std::isspace(static_cast<unsigned char>(remaining[index])))) {
				split_at = index;
				break;
			}
		}

		if (split_at == std::string::npos) {
			for (auto index = limit; index > 0; --index) {
				if (std::isspace(static_cast<unsigned char>(remaining[index - 1]))) {
					split_at = index - 1;
					break;
				}
			}
		}

		if (split_at == std::string::npos || split_at == 0) {
			split_at = limit;
		}

		const auto fragment = TrimCopy(remaining.substr(0, split_at));
		if (!fragment.empty()) {
			fragments.push_back(fragment);
		}

		remaining = TrimCopy(remaining.substr(split_at));
	}

	if (!remaining.empty()) {
		fragments.push_back(remaining);
	}

	return fragments;
}

bool IsRejectedDialogueLine(const std::string &dialogue_line)
{
	if (dialogue_line.empty()) {
		return true;
	}

	if (dialogue_line.front() == '/' || dialogue_line.front() == '#' || dialogue_line.front() == '!') {
		return true;
	}

	const auto lower_dialogue_line = ToLowerCopy(dialogue_line);
	return StartsWithInsensitive(lower_dialogue_line, "metadata:") ||
		StartsWithInsensitive(lower_dialogue_line, "[metadata:") ||
		StartsWithInsensitive(lower_dialogue_line, "json:") ||
		StartsWithInsensitive(lower_dialogue_line, "{") ||
		StartsWithInsensitive(lower_dialogue_line, "http ") ||
		StartsWithInsensitive(lower_dialogue_line, "error:") ||
		StartsWithInsensitive(lower_dialogue_line, "exception:") ||
		StartsWithInsensitive(lower_dialogue_line, "as an ai") ||
		lower_dialogue_line.find("i cannot roleplay") != std::string::npos ||
		lower_dialogue_line.find("i can't roleplay") != std::string::npos ||
		lower_dialogue_line.find("provider timeout") != std::string::npos;
}

void PushDialogueFragment(
	std::vector<DialogueFragment> &fragments,
	OutputType output_type,
	const std::string &message
)
{
	const auto trimmed_message = TrimCopy(message);
	if (trimmed_message.empty()) {
		return;
	}

	fragments.push_back({
		.output_type = output_type,
		.message = trimmed_message
	});
}

std::vector<DialogueFragment> BuildDialogueFragments(const std::string &dialogue_response)
{
	std::vector<DialogueFragment> fragments;
	std::string speech;

	for (size_t index = 0; index < dialogue_response.size(); ++index) {
		const auto character = dialogue_response[index];
		if (character != '*' && character != '(') {
			speech.push_back(character);
			continue;
		}

		if (character == '(' && !TrimCopy(speech).empty()) {
			speech.push_back(character);
			continue;
		}

		const auto closing_character = character == '*' ? '*' : ')';
		const auto closing_index = dialogue_response.find(closing_character, index + 1);
		if (closing_index == std::string::npos) {
			speech.append(dialogue_response.substr(index));
			break;
		}

		if (character == '(' && !TrimCopy(dialogue_response.substr(closing_index + 1)).empty()) {
			speech.push_back(character);
			continue;
		}

		const auto emote_text = TrimCopy(dialogue_response.substr(index + 1, closing_index - index - 1));
		if (emote_text.empty()) {
			speech.append(dialogue_response.substr(index, closing_index - index + 1));
			index = closing_index;
			continue;
		}

		PushDialogueFragment(fragments, OutputType::Say, speech);
		speech.clear();
		PushDialogueFragment(fragments, OutputType::Emote, emote_text);
		index = closing_index;
	}

	PushDialogueFragment(fragments, OutputType::Say, speech);
	return fragments;
}

std::string StripLeadingTargetNameFromEmote(
	const std::string &emote_message,
	const std::string &target_name
)
{
	const auto trimmed_message = TrimCopy(emote_message);
	const auto trimmed_target_name = TrimCopy(target_name);
	if (trimmed_message.size() <= trimmed_target_name.size() || trimmed_target_name.empty()) {
		return trimmed_message;
	}

	if (!StartsWithInsensitive(trimmed_message, ToLowerCopy(trimmed_target_name))) {
		return trimmed_message;
	}

	const auto boundary = trimmed_message[trimmed_target_name.size()];
	if (!std::isspace(static_cast<unsigned char>(boundary))) {
		return trimmed_message;
	}

	return TrimCopy(trimmed_message.substr(trimmed_target_name.size()));
}

const char *EntityKindName(EntityKind kind)
{
	switch (kind) {
	case EntityKind::Player:
		return "player";
	case EntityKind::NPC:
		return "npc";
	case EntityKind::Bot:
		return "bot";
	case EntityKind::Mercenary:
		return "mercenary";
	case EntityKind::Unknown:
	default:
		return "unknown";
	}
}

const char *TargetTypeName(TargetType target_type)
{
	switch (target_type) {
	case TargetType::NPC:
		return "npc";
	case TargetType::Bot:
		return "bot";
	case TargetType::Mercenary:
		return "mercenary";
	case TargetType::Unknown:
	default:
		return "unknown";
	}
}

const char *OutputTypeName(OutputType output_type)
{
	switch (output_type) {
	case OutputType::Say:
		return "say";
	case OutputType::Emote:
		return "emote";
	case OutputType::None:
	default:
		return "none";
	}
}

const char *DeliveryName(OutputType output_type)
{
	switch (output_type) {
	case OutputType::Say:
		return "delivered_say";
	case OutputType::Emote:
		return "delivered_emote";
	case OutputType::None:
	default:
		return "none";
	}
}

bool IsStaleDropReason(const std::string &debug_reason)
{
	return StartsWithInsensitive(debug_reason, "delayed_dialogue_dropped_");
}

const char *DiagnosticEventName(const TargetedSayResult &result)
{
	if (result.debug_reason == "delayed_dialogue_queued") {
		return "queued_generation";
	}

	if (result.debug_reason == "authored_dialogue_handled") {
		return "authored_dialogue_suppression";
	}

	if (result.debug_reason == "dialogue_cooldown") {
		return "cooldown_skip";
	}

	if (IsStaleDropReason(result.debug_reason)) {
		return "stale_drop";
	}

	if (result.debug_reason == "delayed_dialogue_unavailable") {
		return "unavailable_reply";
	}

	if (result.debug_reason == "delayed_dialogue_rejected") {
		return "rejected_output";
	}

	if (result.output_type == OutputType::Say) {
		return "delivered_say";
	}

	if (result.output_type == OutputType::Emote) {
		return "delivered_emote";
	}

	return "skip";
}

std::string BuildOllamaPrompt(const PublicGameplayContext &context)
{
	std::ostringstream prompt;
	prompt
		<< "Write one short in-character Dialogue Response for this EverQuest interaction. "
		<< "Use ordinary speech, with simple stage directions in *...* when appropriate. "
		<< "Use only the public gameplay context below. Do not include metadata, commands, JSON, or explanations.\n"
		<< "Message: " << context.current_message << "\n"
		<< "Speaker: " << context.speaker.name
		<< " (" << EntityKindName(context.speaker.kind)
		<< ", level " << context.speaker.level << ")\n"
		<< "Target: " << context.target.name
		<< " (" << EntityKindName(context.target.kind)
		<< ", level " << context.target.level
		<< ", distance " << context.target.distance
		<< ", engaged " << (context.target.engaged ? "yes" : "no") << ")\n"
		<< "Zone: " << context.zone.short_name << " - " << context.zone.long_name;

	if (!context.nearby_entities.empty()) {
		prompt << "\nNearby:";
		for (const auto &entity : context.nearby_entities) {
			prompt
				<< "\n- " << entity.name
				<< " (" << EntityKindName(entity.kind)
				<< ", level " << entity.level
				<< ", distance " << entity.distance
				<< ", engaged " << (entity.engaged ? "yes" : "no") << ")";
		}
	}

	return prompt.str();
}

std::string BuildOllamaRequestBody(const std::string &model, const PublicGameplayContext &context)
{
	Json::Value request;
	request["model"] = model;
	request["prompt"] = BuildOllamaPrompt(context);
	request["stream"] = false;

	Json::StreamWriterBuilder builder;
	builder["indentation"] = "";
	return Json::writeString(builder, request);
}

bool ParseOllamaDialogueLine(const std::string &body, std::string &dialogue_line)
{
	Json::Value root;
	Json::CharReaderBuilder builder;
	std::string errors;
	std::istringstream stream(body);
	if (!Json::parseFromStream(builder, stream, &root, &errors)) {
		return false;
	}

	if (!root.isObject() || !root["response"].isString()) {
		return false;
	}

	dialogue_line = root["response"].asString();
	return true;
}

struct EndpointParts {
	std::string base_url;
	std::string path;
};

bool ParseEndpoint(const std::string &endpoint, EndpointParts &parts)
{
	const auto scheme_position = endpoint.find("://");
	if (scheme_position == std::string::npos) {
		return false;
	}

	const auto path_position = endpoint.find('/', scheme_position + 3);
	if (path_position == std::string::npos) {
		parts.base_url = endpoint;
		parts.path = "/";
		return true;
	}

	parts.base_url = endpoint.substr(0, path_position);
	parts.path = endpoint.substr(path_position);
	return !parts.base_url.empty() && !parts.path.empty();
}

class HttplibOllamaHttpTransport final : public OllamaHttpTransport {
public:
	OllamaHttpResponse PostJson(
		const std::string &endpoint,
		const std::string &body,
		int timeout_ms
	) override
	{
		EndpointParts endpoint_parts;
		if (!ParseEndpoint(endpoint, endpoint_parts)) {
			return {};
		}

		httplib::Client client(endpoint_parts.base_url);
		const auto timeout = std::chrono::milliseconds(std::max(timeout_ms, 1));
		client.set_connection_timeout(timeout);
		client.set_read_timeout(timeout);
		client.set_write_timeout(timeout);

		auto result = client.Post(endpoint_parts.path, body, "application/json");
		if (!result) {
			return {};
		}

		return {
			.completed = true,
			.status = result->status,
			.body = result->body
		};
	}
};

}

TargetedSayResult HandleTargetedSay(const TargetedSayRequest &request)
{
	auto result = EligibleTargetedSayResult(request);
	if (!result.handled) {
		return result;
	}

	StartDialogueCooldown(request);

	result.output_type = OutputType::Emote;
	result.message = RuleS(Chat, FallbackDialogueUnavailableReply);
	return result;
}

PublicGameplayContext BuildPublicGameplayContext(const LiveContext &context)
{
	const auto nearby_radius = RuleI(Chat, FallbackDialogueNearbyContextRadius);
	const auto nearby_limit = RuleI(Chat, FallbackDialogueNearbyEntityLimit);
	PublicGameplayContext public_context{
		.current_message = context.current_message,
		.speaker = BuildPublicEntitySummary(context.speaker),
		.target = BuildPublicEntitySummary(
			context.target,
			DistanceBetween(context.speaker, context.target)
		),
		.zone = {
			.short_name = context.zone.short_name,
			.long_name = context.zone.long_name
		}
	};

	if (nearby_radius <= 0 || nearby_limit <= 0) {
		return public_context;
	}

	for (const auto &entity : context.nearby_entities) {
		if (entity.entity_id != 0 &&
			(entity.entity_id == context.speaker.entity_id || entity.entity_id == context.target.entity_id)) {
			continue;
		}

		const auto distance = DistanceBetween(context.speaker, entity);
		if (distance > nearby_radius) {
			continue;
		}

		public_context.nearby_entities.push_back(
			BuildPublicEntitySummary(entity, distance)
		);
	}

	std::sort(
		public_context.nearby_entities.begin(),
		public_context.nearby_entities.end(),
		[](const PublicEntitySummary &first, const PublicEntitySummary &second) {
			return first.distance < second.distance;
		}
	);

	if (public_context.nearby_entities.size() > static_cast<size_t>(nearby_limit)) {
		public_context.nearby_entities.resize(static_cast<size_t>(nearby_limit));
	}

	return public_context;
}

DialogueResponseProcessingResult ProcessDialogueResponse(
	const std::string &natural_dialogue_response,
	const std::string &target_name
)
{
	const auto normalized_response = NormalizeDialogueLine(natural_dialogue_response);
	if (IsRejectedDialogueLine(normalized_response)) {
		return {
			.rejection_reason = "unsafe_dialogue_response"
		};
	}

	auto fragments = BuildDialogueFragments(normalized_response);
	if (fragments.empty()) {
		return {
			.rejection_reason = "empty_dialogue_response"
		};
	}

	for (auto &fragment : fragments) {
		if (fragment.output_type == OutputType::Emote) {
			fragment.message = StripLeadingTargetNameFromEmote(fragment.message, target_name);
		}

		if (IsRejectedDialogueLine(fragment.message)) {
			return {
				.rejection_reason = "unsafe_dialogue_fragment"
			};
		}
	}

	return {
		.accepted = true,
		.fragments = std::move(fragments)
	};
}

DialogueDeliveryPlan PlanDialogueDelivery(const std::vector<DialogueFragment> &fragments)
{
	DialogueDeliveryPlan plan;

	for (const auto &fragment : fragments) {
		if (fragment.output_type == OutputType::Say) {
			for (const auto &message : SplitDialogueLine(fragment.message)) {
				plan.messages.push_back({
					.output_type = OutputType::Say,
					.message = message
				});
			}
			continue;
		}

		if (fragment.output_type == OutputType::Emote) {
			const auto max_line_length = RuleI(Chat, FallbackDialogueMaxLineLength);
			if (max_line_length > 0 && fragment.message.size() > static_cast<size_t>(max_line_length)) {
				return {
					.rejection_reason = "long_emote_dialogue_fragment"
				};
			}

			plan.messages.push_back({
				.output_type = OutputType::Emote,
				.message = fragment.message
			});
		}
	}

	if (plan.messages.empty()) {
		return {
			.rejection_reason = "empty_dialogue_delivery"
		};
	}

	plan.accepted = true;
	return plan;
}

TargetedSayResult BuildDelayedDialogueUnavailableResult(
	const DelayedDialogueRequest &request,
	const std::string &debug_reason
)
{
	return {
		.handled = true,
		.output_type = OutputType::Emote,
		.message = request.unavailable_reply,
		.debug_reason = debug_reason,
		.speaker_id = request.speaker_id,
		.target_id = request.target_id,
		.visible_speaker_id = request.target_id,
		.target_type = request.target_type
	};
}

TargetedSayResult BuildDelayedDialogueReadyResult(
	const DelayedDialogueRequest &request,
	const DeliveredDialogueMessage &message
)
{
	return {
		.handled = true,
		.output_type = message.output_type,
		.message = message.message,
		.debug_reason = "delayed_dialogue_ready",
		.speaker_id = request.speaker_id,
		.target_id = request.target_id,
		.visible_speaker_id = request.target_id,
		.target_type = request.target_type
	};
}

std::vector<TargetedSayResult> BuildDelayedDialogueResults(
	const DelayedDialogueRequest &request,
	const DelayedDialogueCompletion &completion
)
{
	if (!completion.succeeded) {
		return {
			BuildDelayedDialogueUnavailableResult(request, "delayed_dialogue_unavailable")
		};
	}

	const auto processed_response = ProcessDialogueResponse(
		completion.dialogue_line,
		request.context.target.name
	);
	if (!processed_response.accepted) {
		return {
			BuildDelayedDialogueUnavailableResult(request, "delayed_dialogue_rejected")
		};
	}

	const auto delivery_plan = PlanDialogueDelivery(processed_response.fragments);
	if (!delivery_plan.accepted) {
		return {
			BuildDelayedDialogueUnavailableResult(request, "delayed_dialogue_rejected")
		};
	}

	std::vector<TargetedSayResult> results;
	results.reserve(delivery_plan.messages.size());
	for (const auto &message : delivery_plan.messages) {
		results.push_back(BuildDelayedDialogueReadyResult(request, message));
	}

	if (results.empty()) {
		return {
			BuildDelayedDialogueUnavailableResult(request, "delayed_dialogue_rejected")
		};
	}

	return results;
}

DelayedDialogueQueue::DelayedDialogueQueue(DelayedDialogueProvider &provider)
	: provider_(provider)
{
}

TargetedSayResult DelayedDialogueQueue::HandleTargetedSay(
	const TargetedSayRequest &request,
	const LiveContext &context
)
{
	auto result = EligibleTargetedSayResult(request);
	if (!result.handled) {
		return result;
	}

	StartDialogueCooldown(request);

	DelayedDialogueRequest delayed_request{
		.request_id = next_request_id_++,
		.speaker_id = request.speaker_id,
		.target_id = request.target_id,
		.target_type = request.target_type,
		.context = BuildPublicGameplayContext(context),
		.unavailable_reply = RuleS(Chat, FallbackDialogueUnavailableReply)
	};

	pending_requests_[delayed_request.request_id] = delayed_request;
	provider_.Enqueue(delayed_request);

	result.output_type = OutputType::None;
	result.message.clear();
	result.debug_reason = "delayed_dialogue_queued";
	return result;
}

bool DelayedDialogueQueue::PopReadyResult(
	const CurrentInteraction &interaction,
	TargetedSayResult &result
)
{
	return PopReadyResult(
		[&interaction](const DelayedDialogueRequest &) {
			return interaction;
		},
		result
	);
}

bool DelayedDialogueQueue::PopReadyResult(
	const CurrentInteractionResolver &resolver,
	TargetedSayResult &result
)
{
	if (!ready_results_.empty()) {
		result = std::move(ready_results_.front());
		ready_results_.pop_front();
		return true;
	}

	DelayedDialogueCompletion completion;
	if (!provider_.PopCompletion(completion)) {
		return false;
	}

	const auto pending_request = pending_requests_.find(completion.request_id);
	if (pending_request == pending_requests_.end()) {
		return false;
	}

	const auto request = std::move(pending_request->second);
	pending_requests_.erase(pending_request);

	const auto stale_reason = ValidateCurrentInteraction(request, resolver(request));
	if (!stale_reason.empty()) {
		result = {
			.debug_reason = stale_reason,
			.speaker_id = request.speaker_id,
			.target_id = request.target_id,
			.target_type = request.target_type
		};
		LogDiagnostic(result);
		return false;
	}

	auto ready_results = BuildDelayedDialogueResults(request, completion);
	result = std::move(ready_results.front());
	for (auto ready_result = ready_results.begin() + 1; ready_result != ready_results.end(); ++ready_result) {
		ready_results_.push_back(std::move(*ready_result));
	}

	return true;
}

std::string BuildDiagnosticLogLine(const TargetedSayResult &result)
{
	std::ostringstream diagnostic;
	diagnostic
		<< "Fallback Dialogue diagnostic"
		<< " event=" << DiagnosticEventName(result)
		<< " speaker_id=" << result.speaker_id
		<< " target_id=" << result.target_id
		<< " visible_speaker_id=" << result.visible_speaker_id
		<< " target_type=" << TargetTypeName(result.target_type)
		<< " output_type=" << OutputTypeName(result.output_type)
		<< " delivery=" << DeliveryName(result.output_type)
		<< " handled=" << (result.handled ? "true" : "false");

	if (!result.debug_reason.empty()) {
		diagnostic << " reason=" << result.debug_reason;
	}

	return diagnostic.str();
}

void LogDiagnostic(const TargetedSayResult &result)
{
	if (!result.handled && result.debug_reason.empty()) {
		return;
	}

	LogDebug("{}", BuildDiagnosticLogLine(result));
}

OllamaDelayedDialogueProvider::OllamaDelayedDialogueProvider()
	: owned_transport_(std::make_unique<HttplibOllamaHttpTransport>()),
	transport_(*owned_transport_)
{
}

OllamaDelayedDialogueProvider::OllamaDelayedDialogueProvider(OllamaHttpTransport &transport)
	: transport_(transport)
{
}

OllamaDelayedDialogueProvider::~OllamaDelayedDialogueProvider()
{
	for (auto &worker : workers_) {
		if (worker.joinable()) {
			worker.join();
		}
	}
}

void OllamaDelayedDialogueProvider::Enqueue(const DelayedDialogueRequest &request)
{
	const auto endpoint = RuleS(Chat, FallbackDialogueOllamaEndpoint);
	const auto model = TrimCopy(RuleS(Chat, FallbackDialogueOllamaModel));
	const auto timeout_ms = RuleI(Chat, FallbackDialogueOllamaTimeoutMs);

	workers_.emplace_back([this, request, endpoint, model, timeout_ms]() {
		if (endpoint.empty() || model.empty() || timeout_ms <= 0) {
			PushCompletion({
				.request_id = request.request_id,
				.succeeded = false
			});
			return;
		}

		const auto response = transport_.PostJson(
			endpoint,
			BuildOllamaRequestBody(model, request.context),
			timeout_ms
		);

		if (!response.completed || response.status < 200 || response.status >= 300) {
			PushCompletion({
				.request_id = request.request_id,
				.succeeded = false
			});
			return;
		}

		std::string dialogue_line;
		if (!ParseOllamaDialogueLine(response.body, dialogue_line)) {
			PushCompletion({
				.request_id = request.request_id,
				.succeeded = false
			});
			return;
		}

		PushCompletion({
			.request_id = request.request_id,
			.succeeded = true,
			.dialogue_line = dialogue_line
		});
	});
}

bool OllamaDelayedDialogueProvider::PopCompletion(DelayedDialogueCompletion &completion)
{
	std::lock_guard lock(completions_mutex_);
	if (completions_.empty()) {
		return false;
	}

	completion = completions_.front();
	completions_.pop_front();
	return true;
}

void OllamaDelayedDialogueProvider::PushCompletion(DelayedDialogueCompletion completion)
{
	std::lock_guard lock(completions_mutex_);
	completions_.push_back(std::move(completion));
}

void TestDelayedDialogueProvider::Enqueue(const DelayedDialogueRequest &request)
{
	pending_requests_.push_back(request);
}

bool TestDelayedDialogueProvider::PopCompletion(DelayedDialogueCompletion &completion)
{
	if (completions_.empty()) {
		return false;
	}

	completion = completions_.front();
	completions_.pop_front();
	return true;
}

const std::vector<DelayedDialogueRequest> &TestDelayedDialogueProvider::PendingRequests() const
{
	return pending_requests_;
}

bool TestDelayedDialogueProvider::CompleteNextSuccess(const std::string &dialogue_line)
{
	if (pending_requests_.empty()) {
		return false;
	}

	completions_.push_back({
		.request_id = pending_requests_.front().request_id,
		.succeeded = true,
		.dialogue_line = dialogue_line
	});
	pending_requests_.erase(pending_requests_.begin());
	return true;
}

bool TestDelayedDialogueProvider::CompleteNextFailure()
{
	if (pending_requests_.empty()) {
		return false;
	}

	completions_.push_back({
		.request_id = pending_requests_.front().request_id,
		.succeeded = false
	});
	pending_requests_.erase(pending_requests_.begin());
	return true;
}

void ResetDialogueCooldowns()
{
	dialogue_cooldowns.clear();
}

}

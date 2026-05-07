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
#include "common/rulesys.h"
#include "common/timer.h"

#include <algorithm>
#include <cmath>
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

	const auto stale_reason = ValidateCurrentInteraction(request, interaction);
	if (!stale_reason.empty()) {
		LogDebug(
			"Fallback Dialogue dropped delayed result for speaker [{}] target [{}]: {}",
			request.speaker_id,
			request.target_id,
			stale_reason
		);

		result = {
			.debug_reason = stale_reason,
			.speaker_id = request.speaker_id,
			.target_id = request.target_id,
			.target_type = request.target_type
		};
		return false;
	}

	result = {
		.handled = true,
		.output_type = completion.succeeded ? OutputType::Say : OutputType::Emote,
		.message = completion.succeeded ? completion.dialogue_line : request.unavailable_reply,
		.debug_reason = completion.succeeded ? "delayed_dialogue_ready" : "delayed_dialogue_unavailable",
		.speaker_id = request.speaker_id,
		.target_id = request.target_id,
		.target_type = request.target_type
	};
	return true;
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

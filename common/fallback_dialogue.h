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
#include <string>
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
	TargetType  target_type = TargetType::Unknown;
};

struct LiveEntity {
	std::string name;
	EntityKind  kind = EntityKind::Unknown;
	int         level = 0;
	float       x = 0.0f;
	float       y = 0.0f;
	float       z = 0.0f;
	std::string account_name;
	std::string ip_address;
	std::string private_chat;
	std::string inventory_summary;
	std::string raw_quest_globals;
	uint32_t    account_id = 0;
	uint32_t    character_id = 0;
	bool        gm_status = false;
	bool        engaged = false;
};

struct LiveZone {
	std::string short_name;
	std::string long_name;
	uint32_t    zone_id = 0;
	uint32_t    instance_id = 0;
	std::string database_credentials;
};

struct LiveContext {
	std::string             current_message;
	LiveEntity             speaker;
	LiveEntity             target;
	LiveZone               zone;
	std::vector<LiveEntity> nearby_entities;
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
	std::string dialogue_line;
};

struct CurrentInteraction {
	uint32_t   speaker_id = 0;
	uint32_t   target_id = 0;
	uint32_t   speaker_target_id = 0;
	bool       speaker_present = false;
	bool       target_present = false;
	LiveEntity speaker;
	LiveEntity target;
};

class DelayedDialogueProvider {
public:
	virtual ~DelayedDialogueProvider() = default;

	virtual void Enqueue(const DelayedDialogueRequest &request) = 0;
	virtual bool PopCompletion(DelayedDialogueCompletion &completion) = 0;
};

class DelayedDialogueQueue {
public:
	explicit DelayedDialogueQueue(DelayedDialogueProvider &provider);

	TargetedSayResult HandleTargetedSay(
		const TargetedSayRequest &request,
		const LiveContext &context
	);
	bool PopReadyResult(const CurrentInteraction &interaction, TargetedSayResult &result);

private:
	DelayedDialogueProvider                         &provider_;
	std::unordered_map<uint64_t, DelayedDialogueRequest> pending_requests_;
	uint64_t                                         next_request_id_ = 1;
};

class TestDelayedDialogueProvider : public DelayedDialogueProvider {
public:
	void Enqueue(const DelayedDialogueRequest &request) override;
	bool PopCompletion(DelayedDialogueCompletion &completion) override;

	const std::vector<DelayedDialogueRequest> &PendingRequests() const;
	bool CompleteNextSuccess(const std::string &dialogue_line);
	bool CompleteNextFailure();

private:
	std::vector<DelayedDialogueRequest> pending_requests_;
	std::deque<DelayedDialogueCompletion> completions_;
};

TargetedSayResult HandleTargetedSay(const TargetedSayRequest &request);
PublicGameplayContext BuildPublicGameplayContext(const LiveContext &context);
void ResetDialogueCooldowns();

}

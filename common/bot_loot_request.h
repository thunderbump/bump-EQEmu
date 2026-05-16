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

#include "item_data.h"

#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace EQ {
	class ItemInstance;
}

namespace BotLootRequest {

enum class DeliveryChannel {
	None = 0,
	GroupChat
};

struct Settings {
	bool enabled = false;
	int cooldown_seconds = 0;
	uint32_t current_time_ms = 0;
};

struct GearValueBreakdown {
	int survivability = 0;
	int resources = 0;
	int melee_offense = 0;
	int spell_offense = 0;
	int healing_support = 0;
	int ranged = 0;
	int effects = 0;

	int Total() const;
};

struct ItemSnapshot {
	const EQ::ItemData *item = nullptr;
	const EQ::ItemInstance *item_instance = nullptr;
	int slot_id = -1;
};

struct GroupedBotSnapshot {
	uint32_t name_stable_id = 0;
	std::string name;
	uint16_t race_id = 0;
	uint8_t class_id = 0;
	uint8_t level = 0;
	bool ranged_mode = false;
	std::vector<ItemSnapshot> equipped_items;
};

struct SuccessfulLootEvent {
	uint32_t looter_stable_id = 0;
	uint64_t loot_event_id = 0;
	std::string looter_name;
	const EQ::ItemData *looted_item = nullptr;
	const EQ::ItemInstance *looted_item_instance = nullptr;
	std::string looted_item_link;
	std::vector<GroupedBotSnapshot> grouped_bots;
};

struct Request {
	bool produced = false;
	uint32_t requesting_bot_stable_id = 0;
	std::string requesting_bot_name;
	std::string message;
	DeliveryChannel delivery_channel = DeliveryChannel::None;
	int target_slot = -1;
	std::string target_slot_name;
	std::string plain_item_name;
	std::string reason_summary;
	int upgrade_score = 0;
	GearValueBreakdown score_breakdown;
};

struct DeliveryState {
	std::unordered_set<uint64_t> delivered_loot_events;
	std::unordered_map<uint64_t, uint32_t> cooldown_start_ms_by_looter_bot;
};

struct DialogueProviderSettings {
	std::string endpoint = "http://127.0.0.1:11434/api/generate";
	std::string model = "llama3.2";
	int timeout_ms = 2000;
	int max_generated_line_length = 160;
};

struct RequestDialogueIntent {
	std::string looter_name;
	std::string requesting_bot_name;
	std::string plain_item_name;
	std::string item_link;
	std::string target_slot_name;
	std::string reason_summary;
	std::string deterministic_template;
};

struct DelayedDialogueRequest {
	uint64_t request_id = 0;
	uint32_t looter_stable_id = 0;
	uint32_t requesting_bot_stable_id = 0;
	RequestDialogueIntent intent;
};

struct DelayedDialogueCompletion {
	uint64_t request_id = 0;
	bool succeeded = false;
	std::string dialogue_response;
};

struct CurrentGroupState {
	bool looter_present = false;
	bool requesting_bot_present = false;
	bool still_grouped = false;
};

struct DialogueResult {
	bool produced = false;
	uint32_t looter_stable_id = 0;
	uint32_t requesting_bot_stable_id = 0;
	std::string requesting_bot_name;
	std::string message;
	std::string debug_reason;
	DeliveryChannel delivery_channel = DeliveryChannel::None;
};

struct EnqueueResult {
	bool queued = false;
	std::string message;
	std::string debug_reason;
};

class DelayedDialogueProvider {
public:
	virtual ~DelayedDialogueProvider() = default;

	virtual void Enqueue(
		const DelayedDialogueRequest &request,
		const DialogueProviderSettings &provider_settings
	) = 0;
	virtual bool PopCompletion(DelayedDialogueCompletion &completion) = 0;
};

struct OllamaHttpResponse {
	bool completed = false;
	int status = 0;
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

class OllamaDelayedLootRequestDialogueProvider : public DelayedDialogueProvider {
public:
	OllamaDelayedLootRequestDialogueProvider();
	explicit OllamaDelayedLootRequestDialogueProvider(OllamaHttpTransport &transport);
	~OllamaDelayedLootRequestDialogueProvider() override;

	void Enqueue(
		const DelayedDialogueRequest &request,
		const DialogueProviderSettings &provider_settings
	) override;
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
	using CurrentGroupResolver = std::function<CurrentGroupState(const DelayedDialogueRequest &request)>;

	DelayedDialogueQueue(DelayedDialogueProvider &provider, DialogueProviderSettings settings);

	EnqueueResult Enqueue(
		const Request &request,
		const SuccessfulLootEvent &event,
		const DialogueProviderSettings &provider_settings
	);
	bool PopReadyResult(const CurrentGroupResolver &resolver, DialogueResult &result);

private:
	DelayedDialogueProvider &provider_;
	DialogueProviderSettings settings_;
	std::unordered_map<uint64_t, DelayedDialogueRequest> pending_requests_;
	uint64_t next_request_id_ = 1;
};

class TestDelayedDialogueProvider : public DelayedDialogueProvider {
public:
	void Enqueue(
		const DelayedDialogueRequest &request,
		const DialogueProviderSettings &provider_settings
	) override;
	bool PopCompletion(DelayedDialogueCompletion &completion) override;

	const std::vector<DelayedDialogueRequest> &PendingRequests() const;
	bool CompleteNextSuccess(const std::string &dialogue_response);
	bool CompleteNextFailure();

private:
	std::vector<DelayedDialogueRequest> pending_requests_;
	std::deque<DelayedDialogueCompletion> completions_;
};

Request BuildRequestForSuccessfulLoot(const SuccessfulLootEvent &event, const Settings &settings);
Request PlanVisibleRequestForSuccessfulLoot(
	const SuccessfulLootEvent &event,
	const Settings &settings,
	DeliveryState &delivery_state
);
std::string BuildLootRequestDialoguePrompt(const RequestDialogueIntent &intent);

} // namespace BotLootRequest

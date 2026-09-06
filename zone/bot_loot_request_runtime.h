#pragma once

#include "common/bot_loot_request.h"

#include <functional>
#include <string>
#include <vector>

class Client;
class Group;

namespace EQ {
	class ItemInstance;
}

namespace ZoneBotLootRequestRuntime {

struct StructuredDecision {
	bool produced = false;
	uint32_t looter_stable_id = 0;
	uint64_t loot_event_id = 0;
	uint32_t requesting_bot_stable_id = 0;
	std::string requesting_bot_name;
	uint32_t item_id = 0;
	std::string item_name;
	int target_slot = -1;
	std::string target_slot_name;
	int upgrade_score = 0;
	std::string reason;
	std::string deterministic_message;
};

using DecisionObserver = std::function<void(const StructuredDecision &)>;

// The corpse path reports deterministic gameplay facts here before dialogue is queued.
// Harness observers are process-local and never influence request eligibility.
void SetDecisionObserver(DecisionObserver observer);
void ClearDecisionObserver();
DecisionObserver CaptureDecisionObserver();
BotLootRequest::DeliveryState CaptureDeliveryState();
void RestoreDeliveryState(BotLootRequest::DeliveryState state);
BotLootRequest::Request EvaluateSuccessfulLoot(
	Client *looter,
	const EQ::ItemInstance *inst,
	Group *group,
	const std::string &item_link,
	uint64_t loot_event_id
);

void EnqueueLootRequestDialogue(
	const BotLootRequest::Request &request,
	const BotLootRequest::SuccessfulLootEvent &event
);
void ProcessReadyLootRequestDialogue();
void CancelLootRequestDialogue(uint32_t looter_stable_id, const std::vector<uint32_t> &requesting_bot_stable_ids);

// Harness-only seam for proving that corpse loot completion does not wait for dialogue.
// The caller owns the provider and must clear the override before destroying it.
void SetDialogueProviderForTesting(BotLootRequest::DelayedDialogueProvider *provider);
void ClearDialogueProviderForTesting();
BotLootRequest::DelayedDialogueProvider *CaptureDialogueProviderForTesting();

}

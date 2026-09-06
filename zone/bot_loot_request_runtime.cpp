#include "bot_loot_request_runtime.h"

#include "bot.h"
#include "client.h"
#include "common/rulesys.h"
#include "entity.h"
#include "groups.h"

#include <memory>

extern EntityList entity_list;

namespace {

BotLootRequest::OllamaDelayedLootRequestDialogueProvider bot_loot_request_dialogue_provider;

BotLootRequest::DialogueProviderSettings LoadBotLootRequestDialogueSettings()
{
	return {
		.endpoint = RuleS(Chat, FallbackDialogueOllamaEndpoint),
		.model = RuleS(Chat, FallbackDialogueOllamaModel),
		.timeout_ms = RuleI(Chat, FallbackDialogueOllamaTimeoutMs),
		.max_generated_line_length = RuleI(Chat, FallbackDialogueMaxLineLength)
	};
}

std::unique_ptr<BotLootRequest::DelayedDialogueQueue> test_dialogue_queue;
BotLootRequest::DelayedDialogueProvider *test_dialogue_provider = nullptr;

BotLootRequest::DelayedDialogueQueue &ZoneBotLootRequestDialogueQueue()
{
	static BotLootRequest::DelayedDialogueQueue queue(
		bot_loot_request_dialogue_provider,
		LoadBotLootRequestDialogueSettings()
	);

	return test_dialogue_queue ? *test_dialogue_queue : queue;
}

BotLootRequest::CurrentGroupState CurrentLootRequestGroupState(
	const BotLootRequest::DelayedDialogueRequest &request
)
{
	auto *looter = entity_list.GetClientByCharID(request.looter_stable_id);
	auto *requesting_bot = entity_list.GetBotByBotID(request.requesting_bot_stable_id);
	auto *group = looter ? looter->GetGroup() : nullptr;

	return {
		.looter_present = looter != nullptr,
		.requesting_bot_present = requesting_bot != nullptr,
		.still_grouped = group && requesting_bot && group->IsGroupMember(requesting_bot)
	};
}

void DeliverLootRequestDialogue(const BotLootRequest::DialogueResult &result)
{
	if (!result.produced || result.delivery_channel != BotLootRequest::DeliveryChannel::GroupChat) {
		return;
	}

	auto *looter = entity_list.GetClientByCharID(result.looter_stable_id);
	auto *requesting_bot = entity_list.GetBotByBotID(result.requesting_bot_stable_id);
	auto *group = looter ? looter->GetGroup() : nullptr;
	if (!group || !requesting_bot || !group->IsGroupMember(requesting_bot)) {
		return;
	}

	group->GroupMessage(
		requesting_bot,
		Language::CommonTongue,
		Language::MaxValue,
		result.message.c_str()
	);
}

}

namespace ZoneBotLootRequestRuntime {

void EnqueueLootRequestDialogue(
	const BotLootRequest::Request &request,
	const BotLootRequest::SuccessfulLootEvent &event
)
{
	ZoneBotLootRequestDialogueQueue().Enqueue(
		request,
		event,
		LoadBotLootRequestDialogueSettings()
	);
}

void ProcessReadyLootRequestDialogue()
{
	BotLootRequest::DialogueResult result;
	while (ZoneBotLootRequestDialogueQueue().PopReadyResult(CurrentLootRequestGroupState, result)) {
		DeliverLootRequestDialogue(result);
	}
}

void CancelLootRequestDialogue(
	uint32_t looter_stable_id,
	const std::vector<uint32_t> &requesting_bot_stable_ids
)
{
	ZoneBotLootRequestDialogueQueue().CancelRequests(looter_stable_id, requesting_bot_stable_ids);
}

void SetDialogueProviderForTesting(BotLootRequest::DelayedDialogueProvider *provider)
{
	test_dialogue_provider = provider;
	test_dialogue_queue = provider ? std::make_unique<BotLootRequest::DelayedDialogueQueue>(
		*provider,
		LoadBotLootRequestDialogueSettings()
	) : nullptr;
}

void ClearDialogueProviderForTesting()
{
	test_dialogue_provider = nullptr;
	test_dialogue_queue.reset();
}

DialogueOverrideState CaptureDialogueOverrideStateForTesting()
{
	DialogueOverrideState state{
		.provider = test_dialogue_provider,
		.queue = std::move(test_dialogue_queue),
	};
	test_dialogue_provider = nullptr;
	return state;
}

void RestoreDialogueOverrideStateForTesting(DialogueOverrideState state)
{
	test_dialogue_provider = state.provider;
	test_dialogue_queue = std::move(state.queue);
}

BotLootRequest::DelayedDialogueProvider *CurrentDialogueProviderForTesting()
{
	return test_dialogue_provider;
}

}

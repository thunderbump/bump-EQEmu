#pragma once

#include "common/bot_loot_request.h"

namespace ZoneBotLootRequestRuntime {

void EnqueueLootRequestDialogue(
	const BotLootRequest::Request &request,
	const BotLootRequest::SuccessfulLootEvent &event
);
void ProcessReadyLootRequestDialogue();

}

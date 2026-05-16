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
#include "bot_loot_request.h"

#include "emu_constants.h"
#include "http/httplib.h"
#include "json/json.h"

#include <fmt/format.h>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <sstream>
#include <utility>

namespace BotLootRequest {

namespace {

uint64_t LooterBotKey(uint32_t looter_stable_id, uint32_t bot_stable_id)
{
	return (static_cast<uint64_t>(looter_stable_id) << 32) | bot_stable_id;
}

bool IsEquipmentSlot(int slot_id)
{
	return slot_id >= EQ::invslot::EQUIPMENT_BEGIN && slot_id <= EQ::invslot::EQUIPMENT_END;
}

bool ItemCanEquipInSlot(const EQ::ItemData &item, int slot_id)
{
	return IsEquipmentSlot(slot_id) && (item.Slots & (1u << slot_id));
}

bool IsV1GearItemType(const EQ::ItemData &item)
{
	switch (item.ItemType) {
	case EQ::item::ItemType1HSlash:
	case EQ::item::ItemType2HSlash:
	case EQ::item::ItemType1HPiercing:
	case EQ::item::ItemType1HBlunt:
	case EQ::item::ItemType2HBlunt:
	case EQ::item::ItemTypeBow:
	case EQ::item::ItemTypeLargeThrowing:
	case EQ::item::ItemTypeShield:
	case EQ::item::ItemTypeArmor:
	case EQ::item::ItemTypeWindInstrument:
	case EQ::item::ItemTypeStringedInstrument:
	case EQ::item::ItemTypeBrassInstrument:
	case EQ::item::ItemTypePercussionInstrument:
	case EQ::item::ItemTypeSmallThrowing:
	case EQ::item::ItemTypeJewelry:
	case EQ::item::ItemType2HPiercing:
	case EQ::item::ItemTypeMartial:
	case EQ::item::ItemTypeSinging:
	case EQ::item::ItemTypeAllInstrumentTypes:
	case EQ::item::ItemTypeCharm:
		return true;
	default:
		return false;
	}
}

bool IsGearCandidateForBot(const EQ::ItemData &item, const GroupedBotSnapshot &bot)
{
	if (
		item.ItemClass != EQ::item::ItemClassCommon ||
		item.BagSlots > 0 ||
		item.Slots == 0 ||
		item.IsQuestItem() ||
		!IsV1GearItemType(item) ||
		!item.IsEquipable(bot.race_id, bot.class_id)
	) {
		return false;
	}

	for (int slot_id = EQ::invslot::EQUIPMENT_BEGIN; slot_id <= EQ::invslot::EQUIPMENT_END; ++slot_id) {
		if (ItemCanEquipInSlot(item, slot_id)) {
			return true;
		}
	}

	return false;
}

int GearScore(const EQ::ItemData *item)
{
	if (!item) {
		return 0;
	}

	return item->AC + item->HP + item->Mana + item->Endur + item->AStr + item->ASta +
		item->AAgi + item->ADex + item->ACha + item->AInt + item->AWis;
}

const EQ::ItemData *EquippedItemForSlot(const GroupedBotSnapshot &bot, int slot_id)
{
	for (const auto &equipped_item : bot.equipped_items) {
		if (equipped_item.item && equipped_item.slot_id == slot_id) {
			return equipped_item.item;
		}
	}

	return nullptr;
}

bool HasLoreConflict(const GroupedBotSnapshot &bot, const EQ::ItemData &item)
{
	if (!item.LoreFlag) {
		return false;
	}

	for (const auto &equipped_item : bot.equipped_items) {
		if (EQ::ItemData::CheckLoreConflict(equipped_item.item, &item)) {
			return true;
		}
	}

	return false;
}

std::string SlotName(int slot_id)
{
	switch (slot_id) {
	case EQ::invslot::slotCharm:
		return "charm";
	case EQ::invslot::slotEar1:
	case EQ::invslot::slotEar2:
		return "ear";
	case EQ::invslot::slotHead:
		return "head";
	case EQ::invslot::slotFace:
		return "face";
	case EQ::invslot::slotNeck:
		return "neck";
	case EQ::invslot::slotShoulders:
		return "shoulders";
	case EQ::invslot::slotArms:
		return "arms";
	case EQ::invslot::slotBack:
		return "back";
	case EQ::invslot::slotWrist1:
	case EQ::invslot::slotWrist2:
		return "wrist";
	case EQ::invslot::slotRange:
		return "range";
	case EQ::invslot::slotHands:
		return "hands";
	case EQ::invslot::slotPrimary:
		return "primary";
	case EQ::invslot::slotSecondary:
		return "secondary";
	case EQ::invslot::slotFinger1:
	case EQ::invslot::slotFinger2:
		return "finger";
	case EQ::invslot::slotChest:
		return "chest";
	case EQ::invslot::slotLegs:
		return "legs";
	case EQ::invslot::slotFeet:
		return "feet";
	case EQ::invslot::slotWaist:
		return "waist";
	case EQ::invslot::slotPowerSource:
		return "power source";
	case EQ::invslot::slotAmmo:
		return "ammo";
	default:
		return "gear";
	}
}

std::string ItemDisplayName(const SuccessfulLootEvent &event)
{
	if (!event.looted_item_link.empty()) {
		return event.looted_item_link;
	}

	if (event.looted_item) {
		return event.looted_item->Name;
	}

	return "that item";
}

std::string PlainItemName(const SuccessfulLootEvent &event)
{
	if (event.looted_item && event.looted_item->Name[0]) {
		return event.looted_item->Name;
	}

	return "that item";
}

std::string TrimCopy(const std::string &value)
{
	const auto first = std::find_if_not(value.begin(), value.end(), [](unsigned char character) {
		return std::isspace(character);
	});
	const auto last = std::find_if_not(value.rbegin(), value.rend(), [](unsigned char character) {
		return std::isspace(character);
	}).base();

	if (first >= last) {
		return {};
	}

	return std::string(first, last);
}

std::string ToLowerCopy(std::string value)
{
	std::transform(value.begin(), value.end(), value.begin(), [](unsigned char character) {
		return static_cast<char>(std::tolower(character));
	});
	return value;
}

bool StartsWithInsensitive(const std::string &value, const std::string &prefix)
{
	if (value.size() < prefix.size()) {
		return false;
	}

	for (size_t index = 0; index < prefix.size(); ++index) {
		if (std::tolower(static_cast<unsigned char>(value[index])) !=
			std::tolower(static_cast<unsigned char>(prefix[index]))) {
			return false;
		}
	}

	return true;
}

bool ContainsItemLinkMarkup(const std::string &value)
{
	return value.find('[') != std::string::npos ||
		value.find(']') != std::string::npos ||
		value.find('\x12') != std::string::npos;
}

bool MentionsLooter(const std::string &line, const RequestDialogueIntent &intent)
{
	if (intent.looter_name.empty()) {
		return false;
	}

	return ToLowerCopy(line).find(ToLowerCopy(intent.looter_name)) != std::string::npos;
}

bool IsUnsafeGeneratedSpeech(const std::string &line)
{
	if (line.empty() || line.front() == '/' || line.front() == '#' || line.front() == '!') {
		return true;
	}

	if (line.find('\n') != std::string::npos || line.find('\r') != std::string::npos) {
		return true;
	}

	if (line.find('*') != std::string::npos || line.find('(') != std::string::npos || line.find(')') != std::string::npos) {
		return true;
	}

	if (ContainsItemLinkMarkup(line)) {
		return true;
	}

	const auto lower_line = ToLowerCopy(line);
	return StartsWithInsensitive(lower_line, "metadata:") ||
		StartsWithInsensitive(lower_line, "[metadata:") ||
		StartsWithInsensitive(lower_line, "json:") ||
		StartsWithInsensitive(lower_line, "{") ||
		StartsWithInsensitive(lower_line, "http ") ||
		StartsWithInsensitive(lower_line, "error:") ||
		StartsWithInsensitive(lower_line, "exception:") ||
		StartsWithInsensitive(lower_line, "as an ai") ||
		lower_line.find("i cannot roleplay") != std::string::npos ||
		lower_line.find("i can't roleplay") != std::string::npos ||
		lower_line.find("provider timeout") != std::string::npos;
}

DialogueResult TemplateDialogueResult(const DelayedDialogueRequest &request, const std::string &debug_reason)
{
	return {
		.produced = true,
		.looter_stable_id = request.looter_stable_id,
		.requesting_bot_stable_id = request.requesting_bot_stable_id,
		.requesting_bot_name = request.intent.requesting_bot_name,
		.message = request.intent.deterministic_template,
		.debug_reason = debug_reason,
		.delivery_channel = DeliveryChannel::GroupChat
	};
}

DialogueResult GeneratedDialogueResult(const DelayedDialogueRequest &request, const std::string &speech_line)
{
	return {
		.produced = true,
		.looter_stable_id = request.looter_stable_id,
		.requesting_bot_stable_id = request.requesting_bot_stable_id,
		.requesting_bot_name = request.intent.requesting_bot_name,
		.message = fmt::format("{} {}", speech_line, request.intent.item_link),
		.debug_reason = "loot_request_dialogue_ready",
		.delivery_channel = DeliveryChannel::GroupChat
	};
}

bool ParseOllamaDialogueResponse(const std::string &body, std::string &dialogue_response)
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

	dialogue_response = root["response"].asString();
	return true;
}

std::string BuildOllamaRequestBody(const std::string &model, const RequestDialogueIntent &intent)
{
	Json::Value request;
	request["model"] = model;
	request["prompt"] = BuildLootRequestDialoguePrompt(intent);
	request["stream"] = false;

	Json::StreamWriterBuilder builder;
	builder["indentation"] = "";
	return Json::writeString(builder, request);
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

} // namespace

Request PlanVisibleRequestForSuccessfulLoot(
	const SuccessfulLootEvent &event,
	const Settings &settings,
	DeliveryState &delivery_state
)
{
	if (event.loot_event_id != 0 && delivery_state.delivered_loot_events.count(event.loot_event_id) != 0) {
		return {};
	}

	auto request = BuildRequestForSuccessfulLoot(event, settings);
	if (!request.produced) {
		return {};
	}

	const auto cooldown_key = LooterBotKey(event.looter_stable_id, request.requesting_bot_stable_id);
	if (settings.cooldown_seconds > 0) {
		const auto cooldown_ms = static_cast<uint32_t>(settings.cooldown_seconds) * 1000;
		const auto cooldown = delivery_state.cooldown_start_ms_by_looter_bot.find(cooldown_key);
		if (
			cooldown != delivery_state.cooldown_start_ms_by_looter_bot.end() &&
			settings.current_time_ms - cooldown->second < cooldown_ms
		) {
			return {};
		}
	}

	if (event.loot_event_id != 0) {
		delivery_state.delivered_loot_events.insert(event.loot_event_id);
	}

	if (settings.cooldown_seconds > 0) {
		delivery_state.cooldown_start_ms_by_looter_bot[cooldown_key] = settings.current_time_ms;
	}

	return request;
}

Request BuildRequestForSuccessfulLoot(const SuccessfulLootEvent &event, const Settings &settings)
{
	if (!settings.enabled || !event.looted_item || event.grouped_bots.empty()) {
		return {};
	}

	Request best_request{};

	for (const auto &bot : event.grouped_bots) {
		if (!IsGearCandidateForBot(*event.looted_item, bot) || HasLoreConflict(bot, *event.looted_item)) {
			continue;
		}

		for (int slot_id = EQ::invslot::EQUIPMENT_BEGIN; slot_id <= EQ::invslot::EQUIPMENT_END; ++slot_id) {
			if (!ItemCanEquipInSlot(*event.looted_item, slot_id)) {
				continue;
			}

			const int upgrade_score = GearScore(event.looted_item) - GearScore(EquippedItemForSlot(bot, slot_id));
			if (upgrade_score <= 0) {
				continue;
			}

			if (!best_request.produced || upgrade_score > best_request.upgrade_score) {
				best_request.produced = true;
				best_request.requesting_bot_stable_id = bot.name_stable_id;
				best_request.requesting_bot_name = bot.name;
				best_request.target_slot = slot_id;
				best_request.target_slot_name = SlotName(slot_id);
				best_request.plain_item_name = PlainItemName(event);
				best_request.reason_summary = fmt::format("upgrade for {}", SlotName(slot_id));
				best_request.upgrade_score = upgrade_score;
			}
		}
	}

	if (!best_request.produced) {
		return {};
	}

	best_request.message = fmt::format(
		"{}, could I use {}? It looks like an upgrade for my {}.",
		event.looter_name,
		ItemDisplayName(event),
		SlotName(best_request.target_slot)
	);
	best_request.delivery_channel = DeliveryChannel::GroupChat;

	return best_request;
}

std::string BuildLootRequestDialoguePrompt(const RequestDialogueIntent &intent)
{
	std::ostringstream prompt;
	prompt
		<< "Write one short in-character group chat speech line for an EverQuest bot asking for loot. "
		<< "The server has already decided the bot wants the item; the bot is the only intended recipient. "
		<< "Write in first person as the bot, and do not say or imply that the looter needs the item. "
		<< "Use only the compact request intent below. Do not include item links, markup, emotes, commands, JSON, metadata, or explanations.\n"
		<< "Bot: " << intent.requesting_bot_name << "\n"
		<< "Looter: " << intent.looter_name << "\n"
		<< "Plain item: " << intent.plain_item_name << "\n"
		<< "Target slot: " << intent.target_slot_name << "\n"
		<< "Reason: " << intent.reason_summary;

	return prompt.str();
}

DelayedDialogueQueue::DelayedDialogueQueue(DelayedDialogueProvider &provider, DialogueProviderSettings settings)
	: provider_(provider),
	settings_(std::move(settings))
{
}

EnqueueResult DelayedDialogueQueue::Enqueue(
	const Request &request,
	const SuccessfulLootEvent &event,
	const DialogueProviderSettings &provider_settings
)
{
	if (!request.produced) {
		return {.debug_reason = "loot_request_dialogue_no_request"};
	}

	DelayedDialogueRequest delayed_request{
		.request_id = next_request_id_++,
		.looter_stable_id = event.looter_stable_id,
		.requesting_bot_stable_id = request.requesting_bot_stable_id,
		.intent = {
			.looter_name = event.looter_name,
			.requesting_bot_name = request.requesting_bot_name,
			.plain_item_name = request.plain_item_name,
			.item_link = event.looted_item_link,
			.target_slot_name = request.target_slot_name,
			.reason_summary = request.reason_summary,
			.deterministic_template = request.message
		}
	};

	pending_requests_[delayed_request.request_id] = delayed_request;
	provider_.Enqueue(delayed_request, provider_settings);

	return {
		.queued = true,
		.debug_reason = "loot_request_dialogue_queued"
	};
}

bool DelayedDialogueQueue::PopReadyResult(const CurrentGroupResolver &resolver, DialogueResult &result)
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

	const auto group_state = resolver(request);
	if (!group_state.looter_present || !group_state.requesting_bot_present || !group_state.still_grouped) {
		result = {
			.produced = false,
			.looter_stable_id = request.looter_stable_id,
			.requesting_bot_stable_id = request.requesting_bot_stable_id,
			.requesting_bot_name = request.intent.requesting_bot_name,
			.debug_reason = "loot_request_dialogue_dropped_stale_group",
		};
		return false;
	}

	if (!completion.succeeded) {
		result = TemplateDialogueResult(request, "loot_request_dialogue_unavailable");
		return true;
	}

	const auto speech_line = TrimCopy(completion.dialogue_response);
	if (
		IsUnsafeGeneratedSpeech(speech_line) ||
		MentionsLooter(speech_line, request.intent) ||
		(settings_.max_generated_line_length > 0 && speech_line.size() > static_cast<size_t>(settings_.max_generated_line_length))
	) {
		result = TemplateDialogueResult(request, "loot_request_dialogue_rejected");
		return true;
	}

	result = GeneratedDialogueResult(request, speech_line);
	return true;
}

OllamaDelayedLootRequestDialogueProvider::OllamaDelayedLootRequestDialogueProvider()
	: owned_transport_(std::make_unique<HttplibOllamaHttpTransport>()),
	transport_(*owned_transport_)
{
}

OllamaDelayedLootRequestDialogueProvider::OllamaDelayedLootRequestDialogueProvider(OllamaHttpTransport &transport)
	: transport_(transport)
{
}

OllamaDelayedLootRequestDialogueProvider::~OllamaDelayedLootRequestDialogueProvider()
{
	for (auto &worker : workers_) {
		if (worker.joinable()) {
			worker.join();
		}
	}
}

void OllamaDelayedLootRequestDialogueProvider::Enqueue(
	const DelayedDialogueRequest &request,
	const DialogueProviderSettings &provider_settings
)
{
	const auto endpoint = provider_settings.endpoint;
	const auto model = TrimCopy(provider_settings.model);
	const auto timeout_ms = provider_settings.timeout_ms;

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
			BuildOllamaRequestBody(model, request.intent),
			timeout_ms
		);

		if (!response.completed || response.status < 200 || response.status >= 300) {
			PushCompletion({
				.request_id = request.request_id,
				.succeeded = false
			});
			return;
		}

		std::string dialogue_response;
		if (!ParseOllamaDialogueResponse(response.body, dialogue_response)) {
			PushCompletion({
				.request_id = request.request_id,
				.succeeded = false
			});
			return;
		}

		PushCompletion({
			.request_id = request.request_id,
			.succeeded = true,
			.dialogue_response = dialogue_response
		});
	});
}

bool OllamaDelayedLootRequestDialogueProvider::PopCompletion(DelayedDialogueCompletion &completion)
{
	std::lock_guard lock(completions_mutex_);
	if (completions_.empty()) {
		return false;
	}

	completion = completions_.front();
	completions_.pop_front();
	return true;
}

void OllamaDelayedLootRequestDialogueProvider::PushCompletion(DelayedDialogueCompletion completion)
{
	std::lock_guard lock(completions_mutex_);
	completions_.push_back(std::move(completion));
}

void TestDelayedDialogueProvider::Enqueue(
	const DelayedDialogueRequest &request,
	const DialogueProviderSettings&
)
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

bool TestDelayedDialogueProvider::CompleteNextSuccess(const std::string &dialogue_response)
{
	if (pending_requests_.empty()) {
		return false;
	}

	completions_.push_back({
		.request_id = pending_requests_.front().request_id,
		.succeeded = true,
		.dialogue_response = dialogue_response
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

} // namespace BotLootRequest

#include "bot_aided_tracking_runtime.h"

#include "bot.h"
#include "bot_command.h"
#include "client.h"
#include "common/bot_aided_tracking.h"
#include "entity.h"
#include "mob.h"

#include <string>
#include <vector>

extern EntityList entity_list;

namespace {
constexpr size_t BotAidedTrackingResultLimit = 50;

EQ::BotAidedTracking::TrackingBotClass TrackingClassForBot(Bot *bot)
{
	if (!bot) {
		return EQ::BotAidedTracking::TrackingBotClass::Other;
	}

	switch (bot->GetClass()) {
		case Class::Ranger:
			return EQ::BotAidedTracking::TrackingBotClass::Ranger;
		case Class::Druid:
			return EQ::BotAidedTracking::TrackingBotClass::Druid;
		case Class::Bard:
			return EQ::BotAidedTracking::TrackingBotClass::Bard;
		default:
			return EQ::BotAidedTracking::TrackingBotClass::Other;
	}
}

const char *BotAidedTrackingConColor(uint32 con)
{
	switch (con) {
		case ConsiderColor::Green:
			return "#00FF00";
		case ConsiderColor::LightBlue:
			return "#8080FF";
		case ConsiderColor::DarkBlue:
			return "#2020FF";
		case ConsiderColor::Yellow:
			return "#FFFF00";
		case ConsiderColor::Red:
			return "#FF0000";
		default:
			return "#FFFFFF";
	}
}

void SendBotAidedTrackingUsage(Client *client, const char *command_name)
{
	client->Message(Chat::White, "usage: %s (Ranger: [option=all: all | rare | local])", command_name);
}

void SendBotAidedTrackingReport(Client *client, uint32 max_range, EQ::BotAidedTracking::ReportScope scope)
{
	const char *window_title = "Bot Tracking Window";
	std::vector<EQ::BotAidedTracking::CandidateSnapshot> candidates;

	for (const auto &mob_entry : entity_list.GetMobList()) {
		auto *mob = mob_entry.second;
		if (
			!mob ||
			!mob->IsTrackable() ||
			!mob->IsNPC() ||
			mob->IsInvisible(client) ||
			mob->IsBot() ||
			mob->IsPet() ||
			mob->IsFamiliar() ||
			mob->IsMerc()
		) {
			continue;
		}

		const auto distance = static_cast<uint32>(DistanceNoZ(mob->GetPosition(), client->GetPosition()) + 0.5f);
		candidates.push_back(
			{
				mob->GetCleanName(),
				distance,
				static_cast<uint32>(client->GetLevelCon(mob->GetLevel())),
				mob->IsRareSpawn()
			}
		);
	}

	const auto report = EQ::BotAidedTracking::SelectReport(
		std::move(candidates),
		scope,
		max_range,
		BotAidedTrackingResultLimit
	);

	if (report.entries.empty()) {
		client->Message(Chat::White, "No trackable spawns found");
		return;
	}

	std::string window_text;
	for (const auto &entry : report.entries) {
		window_text += fmt::format(
			"<c \"{}\">{} - {}</c><br>",
			BotAidedTrackingConColor(entry.presentation),
			entry.clean_name,
			entry.rounded_horizontal_distance
		);
	}

	if (report.truncated) {
		window_text += "<br>List truncated... too many mobs to display";
	}

	client->SendPopupToClient(window_title, window_text.c_str());
}

}

namespace ZoneBotAidedTrackingRuntime {

void RunBotAidedTracking(
	Client *client,
	const char *command_name,
	const char *requested_scope
)
{
	EQ::BotAidedTracking::ReportScope report_scope = EQ::BotAidedTracking::ReportScope::All;
	const std::string scope_text = requested_scope ? requested_scope : "";
	if (!EQ::BotAidedTracking::TryParseReportScope(scope_text, report_scope)) {
		SendBotAidedTrackingUsage(client, command_name);
		return;
	}

	std::vector<Bot *> spawned_bots;
	MyBots::PopulateSBL_BySpawnedBots(client, spawned_bots);

	const uint16 class_mask = (
		player_class_bitmasks[Class::Ranger] |
		player_class_bitmasks[Class::Druid] |
		player_class_bitmasks[Class::Bard]
	);
	ActionableBots::Filter_ByClasses(client, spawned_bots, class_mask);

	std::vector<EQ::BotAidedTracking::CapabilityCandidate> capability_candidates;
	capability_candidates.reserve(spawned_bots.size());
	for (auto *bot : spawned_bots) {
		capability_candidates.push_back(
			{
				TrackingClassForBot(bot),
				static_cast<uint8_t>(bot->GetLevel())
			}
		);
	}

	const auto capability = EQ::BotAidedTracking::ResolveCapability(capability_candidates, scope_text);
	if (!capability.capable || capability.selected_candidate_index >= spawned_bots.size()) {
		client->Message(Chat::White, "No bots are capable of performing this action");
		return;
	}

	if (!capability.base_distance_per_level) {
		client->Message(Chat::White, "An unknown codition has occurred");
		return;
	}

	auto *tracking_bot = spawned_bots[capability.selected_candidate_index];
	tracking_bot->RaidGroupSay(capability.tracking_message);
	SendBotAidedTrackingReport(
		client,
		client->GetLevel() * capability.base_distance_per_level,
		capability.report_scope
	);
}

}

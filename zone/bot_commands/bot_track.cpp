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
#include "zone/bot_command.h"

#include "common/bot_aided_tracking.h"

namespace
{
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
}

void bot_command_track(Client *c, const Seperator *sep)
{
	if (helper_command_alias_fail(c, "bot_command_track", sep->arg[0], "track"))
		return;
	if (helper_is_help_or_usage(sep->arg[1])) {
		c->Message(Chat::White, "usage: %s (Ranger: [option=all: all | rare | local])", sep->arg[0]);
		c->Message(Chat::White, "requires one of the following bot classes:");
		c->Message(Chat::White, "Ranger(1), Druid(20) or Bard(35)");
		return;
	}

	std::string tracking_scope = sep->arg[1];

	std::vector<Bot*> sbl;
	MyBots::PopulateSBL_BySpawnedBots(c, sbl);

	uint16 class_mask = (player_class_bitmasks[Class::Ranger] | player_class_bitmasks[Class::Druid] | player_class_bitmasks[Class::Bard]);
	ActionableBots::Filter_ByClasses(c, sbl, class_mask);

	std::vector<EQ::BotAidedTracking::CapabilityCandidate> capability_candidates;
	capability_candidates.reserve(sbl.size());
	for (auto bot_iter : sbl) {
		capability_candidates.push_back(
			{
				TrackingClassForBot(bot_iter),
				static_cast<uint8_t>(bot_iter->GetLevel())
			}
		);
	}

	const auto capability = EQ::BotAidedTracking::ResolveCapability(capability_candidates, tracking_scope);
	if (!capability.capable || capability.selected_candidate_index >= sbl.size()) {
		c->Message(Chat::White, "No bots are capable of performing this action");
		return;
	}

	if (!capability.base_distance_per_level) {
		c->Message(Chat::White, "An unknown codition has occurred");
		return;
	}

	Bot *my_bot = sbl[capability.selected_candidate_index];
	my_bot->RaidGroupSay(capability.tracking_message);
	entity_list.ShowSpawnWindow(c, (c->GetLevel() * capability.base_distance_per_level), capability.report_scope);
}

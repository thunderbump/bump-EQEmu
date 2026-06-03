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

#include "zone/bot_aided_tracking_runtime.h"

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

	ZoneBotAidedTrackingRuntime::RunBotAidedTracking(c, sep->arg[0], sep->arg[1]);
}

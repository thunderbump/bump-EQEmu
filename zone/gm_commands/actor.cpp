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
#include "zone/actor_lifecycle.h"
#include "zone/client.h"
#include "zone/zone.h"

// One privileged production caller; no automatic population or persistent actor state.
void command_actor(Client *c, const Seperator *sep)
{
	static Actors::Handle handle;
	auto &actors = Actors::Lifecycle::Get();
	if (sep->argnum != 1) {
		c->Message(Chat::White, "Usage: #actor create|inspect|retire");
		return;
	}
	if (!strcasecmp(sep->arg[1], "create")) {
		auto result = actors.Create(Actors::FirstDefinition(c->GetX(), c->GetY(), c->GetZ(), c->GetHeading()));
		if (result.outcome == Actors::Outcome::Created || result.outcome == Actors::Outcome::Existing) handle = result.handle;
		c->Message(Chat::White, "Actor: %s %s", Actors::StateName(result.snapshot.state), result.reason.c_str());
	} else if (!strcasecmp(sep->arg[1], "retire")) {
		auto result = actors.Retire(handle);
		c->Message(Chat::White, "Actor: %s %s", Actors::StateName(result.snapshot.state), result.reason.c_str());
	} else if (!strcasecmp(sep->arg[1], "inspect")) {
		auto s = actors.Inspect(handle);
		c->Message(Chat::White, "Actor: %s %s", Actors::StateName(s.state), s.display_name.c_str());
	} else c->Message(Chat::White, "Usage: #actor create|inspect|retire");
}

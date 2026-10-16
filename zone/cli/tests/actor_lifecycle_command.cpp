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
#include "actor_lifecycle_scenario.h"
#include "common/json/json.hpp"
#include <cstring>
#include <algorithm>
#include <array>

namespace ActorScenario {
bool Selected(int argc, char **argv) { return argc > 1 && !std::strcmp(argv[1], "tests:actor-lifecycle"); }
bool Arguments(int argc, char **argv, Control &control)
{
	if (!Selected(argc, argv) || argc > 3) return false;
	control = Control::None;
	if (argc == 2) return true;
	if (!std::strcmp(argv[2], "--force-failure-after-create")) control = Control::Assertion;
	else if (!std::strcmp(argv[2], "--wait-for-cancellation-after-create")) control = Control::Cancel;
	else return false;
	return true;
}
void Result::Finalize(bool interrupted)
{
	if (interrupted) status = "cancelled";
	else if (!native_cleanup) status = "refused";
}
int Result::ExitCode() const
{
	if (!native_cleanup) return 2;
	if (status == "assertion-failed") return 1;
	const std::array<const char *, 6> cases = {"create-duplicate", "name-collision", "native-processing", "retire-recreate", "external-removal-id-reuse", "save-fresh-zone"};
	const bool completed = completed_cases.size() == cases.size() && std::all_of(cases.begin(), cases.end(), [&](const char *name) {
		return std::count(completed_cases.begin(), completed_cases.end(), name) == 1;
	});
	return status == "passed" && control == Control::None && completed && cycles == 3 && ticks > 0 && id_reuse && save_restore ? 0 : 2;
}
std::string Result::Json() const
{
	nlohmann::json control_value = nullptr;
	if (control == Control::Assertion) control_value = "assertion";
	if (control == Control::Cancel) control_value = "cancel";
	return nlohmann::json{{"version", 1}, {"scenario", "actor-lifecycle-v1"}, {"control", control_value},
		{"status", status}, {"completed_cases", completed_cases}, {"cycles", cycles}, {"ticks", ticks},
		{"id_reuse", id_reuse}, {"save_restore", save_restore}, {"native_cleanup", native_cleanup},
		{"elapsed_seconds", {{"boot", boot}, {"processing", processing}, {"shutdown", shutdown}}}}.dump();
}

}

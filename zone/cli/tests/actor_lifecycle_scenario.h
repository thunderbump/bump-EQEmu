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
#include <csignal>
#include <functional>
#include <stdexcept>
#include <string>
#include <vector>

// Fixed standalone actor command; main owns initialization and final orderly shutdown.
namespace ActorScenario {
enum class Control { None, Assertion, Cancel };
// Private scenario scope; the setter seam lets cheap controls verify restoration on every exit.
class StateSavingScope {
public:
	StateSavingScope(std::function<bool(bool)> setter, bool original, bool &restored)
		: setter(std::move(setter)), original(original), restored(restored)
	{
		restored = false;
		if (!this->setter(true)) throw std::runtime_error("cannot enable native state-saving proof");
	}
	~StateSavingScope()
	{
		try { restored = setter(original); } catch (...) { restored = false; }
	}
private:
	std::function<bool(bool)> setter;
	bool original;
	bool &restored;
};
struct Result {
	Control control = Control::None;
	std::string status = "refused";
	std::vector<std::string> completed_cases;
	unsigned cycles = 0;
	unsigned long long ticks = 0;
	bool id_reuse = false, save_restore = false, native_cleanup = false;
	double boot = 0, processing = 0, shutdown = 0;
	int ExitCode() const;
	std::string Json() const;
};
bool Selected(int argc, char **argv);
bool Arguments(int argc, char **argv, Control &control);
void Run(Result &result, volatile std::sig_atomic_t &interrupted);
}

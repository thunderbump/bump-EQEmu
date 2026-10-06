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

#include <cstdint>
#include <memory>
#include <string>

class Zone;

// Zone-thread-only lifecycle. Handles never carry a native entity ID or pointer.
namespace Actors {
struct Definition {
	std::string key;
	std::string display_name;
	uint16_t race = 1;
	uint8_t gender = 0;
	uint8_t texture = 0;
	uint8_t class_id = 1;
	uint8_t level = 1;
	float size = 6.0f;
	float x = 0, y = 0, z = 0, heading = 0;
	bool operator==(const Definition &) const = default;
};

class Handle {
public:
	bool operator==(const Handle &) const = default;
	explicit operator bool() const { return zone_incarnation && incarnation; }
private:
	friend class Lifecycle;
	uint64_t zone_incarnation = 0;
	uint64_t incarnation = 0;
};

enum class State { Absent, Live, Retiring };
enum class Outcome { Created, Existing, RetirementRequested, Retired, Refused, Conflict, Busy };
struct Snapshot {
	State state = State::Absent;
	Definition definition;
	std::string display_name;
	uint16_t race = 0;
	uint8_t gender = 0, texture = 0, class_id = 0, level = 0;
	int64_t health = 0;
	uint64_t native_ticks = 0;
};
struct Result {
	Outcome outcome = Outcome::Refused;
	Handle handle;
	Snapshot snapshot;
	std::string reason;
};

class Lifecycle {
public:
	static Lifecycle &Get();
	Result Create(const Definition &definition);
	Snapshot Inspect(Handle handle);
	Result Retire(Handle handle);
private:
	friend class ::Zone;
	// Native lifetime hooks stay outside the public actor interface.
	void BeginZone();
	void EndZone();
	Lifecycle();
	~Lifecycle();
	struct Impl;
	std::unique_ptr<Impl> impl;
};

// One authored first-slice identity shared by the privileged caller and scenario.
Definition FirstDefinition(float x, float y, float z, float heading = 0);
const char *StateName(State state);
}

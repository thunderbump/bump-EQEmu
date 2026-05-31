/*	EQEmu: EQEmulator

	Copyright (C) 2001-2026 EQEmu Development Team

	This program is free software; you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation; either version 3 of the License, or
	(at your option) any later version.
*/

#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace EQ::ZoneHarness {

struct ActorEvent {
	uint64_t sequence = 0;
	std::string type;
	std::string message;
};

class ActorEventRecorder {
public:
	void Record(const std::string &type, const std::string &message);
	std::vector<ActorEvent> Drain();
	uint64_t PendingCount() const;

private:
	uint64_t next_sequence = 1;
	std::vector<ActorEvent> events;
};

}

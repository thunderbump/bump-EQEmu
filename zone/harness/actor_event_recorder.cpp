/*	EQEmu: EQEmulator

	Copyright (C) 2001-2026 EQEmu Development Team

	This program is free software; you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation; either version 3 of the License, or
	(at your option) any later version.
*/

#include "actor_event_recorder.h"

namespace EQ::ZoneHarness {

void ActorEventRecorder::Record(const std::string &type, const std::string &message)
{
	events.push_back({
		.sequence = next_sequence++,
		.type = type,
		.message = message,
	});
}

std::vector<ActorEvent> ActorEventRecorder::Drain()
{
	auto drained = events;
	events.clear();
	return drained;
}

uint64_t ActorEventRecorder::PendingCount() const
{
	return events.size();
}

}

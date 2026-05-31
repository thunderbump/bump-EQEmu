/*	EQEmu: EQEmulator

	Copyright (C) 2001-2026 EQEmu Development Team

	This program is free software; you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation; either version 3 of the License, or
	(at your option) any later version.
*/

#pragma once

#include <cstddef>
#include <cstdint>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

class Mob;

namespace EQ::ZoneHarness {

struct ActorEventEntity {
	uint16_t entity_id = 0;
	std::string entity_ref;
	std::string name;
	std::string kind;
};

struct ActorEventSpell {
	uint16_t id = 0;
	std::string name;
	std::string category;
	std::string targeting;
	uint32_t target_type = 0;
};

struct ActorEventCast {
	std::string slot;
	int32_t cast_time_ms = 0;
	int32_t original_cast_time_ms = 0;
};

struct ActorEvent {
	uint64_t id = 0;
	uint64_t time_ms = 0;
	std::string type;
	std::string message;
	ActorEventEntity caster;
	std::optional<ActorEventEntity> target;
	ActorEventSpell spell;
	ActorEventCast cast;
};

class ActorEventRecorder {
public:
	static void RegisterActiveRecorder(ActorEventRecorder *recorder);
	static void ClearActiveRecorder(ActorEventRecorder *recorder);
	static void ObserveSpellCastStarted(
		Mob *caster,
		Mob *target,
		uint16_t spell_id,
		uint32_t slot,
		int32_t cast_time_ms,
		int32_t original_cast_time_ms
	);

	void Record(const std::string &type, const std::string &message);
	void RecordSpellCastStarted(
		Mob *caster,
		Mob *target,
		uint16_t spell_id,
		uint32_t slot,
		int32_t cast_time_ms,
		int32_t original_cast_time_ms
	);
	std::vector<ActorEvent> Drain();
	std::vector<ActorEvent> Since(uint64_t since_id, size_t limit) const;
	uint64_t PendingCount() const;
	uint64_t MaxEventID() const;

private:
	mutable std::mutex state_mutex;
	uint64_t next_sequence = 1;
	std::vector<ActorEvent> events;
	size_t max_events = 512;
};

}

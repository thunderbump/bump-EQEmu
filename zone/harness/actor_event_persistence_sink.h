/*	EQEmu: EQEmulator

	Copyright (C) 2001-2026 EQEmu Development Team

	This program is free software; you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation; either version 3 of the License, or
	(at your option) any later version.
*/

#pragma once

#include "zone/harness/actor_event_recorder.h"

class Mob;

namespace EQ::ZoneHarness {

class ActorEventPersistenceSink {
public:
	virtual ~ActorEventPersistenceSink() = default;

	virtual void PersistSpeechEmitted(Mob *actor, const ActorEvent &event) = 0;
};

class ActorEventRepositoryPersistenceSink final : public ActorEventPersistenceSink {
public:
	void PersistSpeechEmitted(Mob *actor, const ActorEvent &event) override;
};

}

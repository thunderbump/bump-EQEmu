/*	EQEmu: EQEmulator

	Copyright (C) 2001-2026 EQEmu Development Team

	This program is free software; you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation; either version 3 of the License, or
	(at your option) any later version.
*/

#include "actor_event_persistence_sink.h"

#include "common/repositories/actor_events_repository.h"
#include "common/repositories/actor_profiles_repository.h"
#include "zone/bot.h"
#include "zone/zone.h"
#include "zone/zonedb.h"

#include <optional>

extern Zone *zone;

namespace EQ::ZoneHarness {

void ActorEventRepositoryPersistenceSink::PersistSpeechEmitted(Mob *actor, const ActorEvent &event)
{
	if (!actor || !actor->IsBot()) {
		return;
	}

	const auto bot_id = actor->CastToBot()->GetBotID();
	if (!bot_id) {
		return;
	}

	const auto profile = ActorProfilesRepository::FindByBotId(database, bot_id);
	if (!profile.has_value() || !profile->enabled) {
		return;
	}

	ActorEventsRepository::AppendObservedSpeechEmitted(
		database,
		{
			.actor_id = profile->actor_id,
			.bot_id = profile->bot_id,
			.owner_character_id = profile->owner_character_id,
			.zone_id = zone ? std::optional<uint32_t>(zone->GetZoneID()) : std::nullopt,
			.instance_id = zone ? std::optional<uint32_t>(zone->GetInstanceID()) : std::nullopt,
			.entity_id = static_cast<uint32_t>(actor->GetID()),
			.channel = event.speech.channel,
			.text = event.speech.text,
			.audible_radius = event.speech.audible_radius,
		}
	);
}

}

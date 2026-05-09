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
#include "fallback_dialogue_runtime.h"

#include "entity.h"
#include "mob.h"
#include "zone.h"

extern Zone *zone;

namespace {

FallbackDialogue::OllamaDelayedDialogueProvider fallback_dialogue_provider;
FallbackDialogue::DelayedDialogueQueue fallback_dialogue_queue(fallback_dialogue_provider);

FallbackDialogue::EntityKind FallbackDialogueEntityKind(Mob *mob)
{
	if (!mob) {
		return FallbackDialogue::EntityKind::Unknown;
	}

	if (mob->IsClient()) {
		return FallbackDialogue::EntityKind::Player;
	}

	if (mob->IsBot()) {
		return FallbackDialogue::EntityKind::Bot;
	}

	if (mob->IsMerc()) {
		return FallbackDialogue::EntityKind::Mercenary;
	}

	if (mob->IsNPC()) {
		return FallbackDialogue::EntityKind::NPC;
	}

	return FallbackDialogue::EntityKind::Unknown;
}

FallbackDialogue::LiveEntity FallbackDialogueLiveEntity(Mob *mob)
{
	if (!mob) {
		return {};
	}

	return {
		.entity_id = mob->GetID(),
		.name = mob->GetCleanName(),
		.kind = FallbackDialogueEntityKind(mob),
		.level = mob->GetLevel(),
		.x = mob->GetX(),
		.y = mob->GetY(),
		.z = mob->GetZ(),
		.engaged = mob->IsEngaged()
	};
}

std::vector<FallbackDialogue::LiveEntity> FallbackDialogueNearbyEntities(Mob *speaker, Mob *target)
{
	std::vector<FallbackDialogue::LiveEntity> nearby_entities;
	for (const auto &[entity_id, mob] : entity_list.GetMobList()) {
		if (!mob || mob == speaker || mob == target) {
			continue;
		}

		nearby_entities.push_back(FallbackDialogueLiveEntity(mob));
	}

	return nearby_entities;
}

FallbackDialogue::CurrentInteraction CurrentFallbackDialogueInteraction(
	const FallbackDialogue::DelayedDialogueRequest &request
)
{
	auto *speaker = entity_list.GetMob(static_cast<uint16>(request.speaker_id));
	auto *target = entity_list.GetMob(static_cast<uint16>(request.target_id));
	auto *speaker_target = speaker ? speaker->GetTarget() : nullptr;

	return {
		.speaker_id = request.speaker_id,
		.target_id = request.target_id,
		.speaker_target_id = speaker_target ? static_cast<uint32_t>(speaker_target->GetID()) : 0,
		.speaker_present = speaker != nullptr,
		.target_present = target != nullptr,
		.speaker = FallbackDialogueLiveEntity(speaker),
		.target = FallbackDialogueLiveEntity(target)
	};
}

}

FallbackDialogue::DelayedDialogueQueue &ZoneFallbackDialogueQueue()
{
	return fallback_dialogue_queue;
}

FallbackDialogue::LiveContext BuildZoneFallbackDialogueContext(
	Mob *speaker,
	Mob *target,
	const std::string &message
)
{
	return {
		.current_message = message,
		.speaker = FallbackDialogueLiveEntity(speaker),
		.target = FallbackDialogueLiveEntity(target),
		.zone = {
			.short_name = zone ? zone->GetShortName() : "",
			.long_name = zone ? zone->GetLongName() : ""
		},
		.nearby_entities = FallbackDialogueNearbyEntities(speaker, target)
	};
}

namespace ZoneFallbackDialogueRuntime {

void Process()
{
	FallbackDialogue::TargetedSayResult result;
	while (ZoneFallbackDialogueQueue().PopReadyResult(CurrentFallbackDialogueInteraction, result)) {
		auto *target = entity_list.GetMob(static_cast<uint16>(result.target_id));
		if (!target) {
			continue;
		}

		if (result.output_type == FallbackDialogue::OutputType::Say) {
			target->Say("%s", result.message.c_str());
		} else if (result.output_type == FallbackDialogue::OutputType::Emote) {
			target->Emote("%s", result.message.c_str());
		}

		FallbackDialogue::LogDiagnostic(result);
	}
}

}

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

#include "client.h"
#include "common/fallback_dialogue.h"
#include "common/rulesys.h"
#include "entity.h"
#include "mob.h"
#include "zone.h"

#include <vector>

extern Zone *zone;

namespace {

FallbackDialogue::OllamaDelayedDialogueProvider fallback_dialogue_provider;

FallbackDialogue::FallbackDialogueSettings LoadFallbackDialogueSettings()
{
	return {
		.immediate = {
			.enabled = RuleB(Chat, FallbackDialogueEnabled),
			.cooldown_seconds = RuleI(Chat, FallbackDialogueCooldownSeconds),
			.unavailable_reply = RuleS(Chat, FallbackDialogueUnavailableReply)
		},
		.public_context = {
			.nearby_context_radius = RuleI(Chat, FallbackDialogueNearbyContextRadius),
			.nearby_entity_limit = RuleI(Chat, FallbackDialogueNearbyEntityLimit)
		},
		.delivery = {
			.max_delivered_line_length = RuleI(Chat, FallbackDialogueMaxLineLength)
		},
		.current_interaction = {
			.imported_game_rule_say_range = RuleI(Range, Say)
		}
	};
}

FallbackDialogue::EntityKind PublicGameplayContextEntityKind(Mob *mob)
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

FallbackDialogue::TargetType FallbackDialogueTargetType(Mob *mob)
{
	if (!mob) {
		return FallbackDialogue::TargetType::Unknown;
	}

	if (mob->IsMerc()) {
		return FallbackDialogue::TargetType::Mercenary;
	}

	if (mob->IsNPC()) {
		return FallbackDialogue::TargetType::NPC;
	}

	if (mob->IsBot()) {
		return FallbackDialogue::TargetType::Bot;
	}

	return FallbackDialogue::TargetType::Unknown;
}

FallbackDialogue::PublicEntityInput PublicGameplayContextEntityInput(Mob *mob)
{
	if (!mob) {
		return {};
	}

	return {
		.entity_id = mob->GetID(),
		.name = mob->GetCleanName(),
		.kind = PublicGameplayContextEntityKind(mob),
		.level = mob->GetLevel(),
		.x = mob->GetX(),
		.y = mob->GetY(),
		.z = mob->GetZ(),
		.engaged = mob->IsEngaged()
	};
}

FallbackDialogue::CurrentInteractionEntityState BuildCurrentInteractionEntityState(Mob *mob)
{
	if (!mob) {
		return {};
	}

	return {
		.x = mob->GetX(),
		.y = mob->GetY(),
		.z = mob->GetZ()
	};
}

std::vector<FallbackDialogue::PublicEntityInput> FallbackDialogueNearbyEntityInputs(Mob *speaker, Mob *target)
{
	std::vector<FallbackDialogue::PublicEntityInput> nearby_entities;
	for (const auto &[entity_id, mob] : entity_list.GetMobList()) {
		if (!mob || mob == speaker || mob == target) {
			continue;
		}

		nearby_entities.push_back(PublicGameplayContextEntityInput(mob));
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
		.speaker = BuildCurrentInteractionEntityState(speaker),
		.target = BuildCurrentInteractionEntityState(target)
	};
}

FallbackDialogue::DelayedDialogueQueue &ZoneFallbackDialogueQueue()
{
	static FallbackDialogue::DelayedDialogueQueue fallback_dialogue_queue(
		fallback_dialogue_provider,
		LoadFallbackDialogueSettings()
	);

	return fallback_dialogue_queue;
}

FallbackDialogue::PublicGameplayContextInput BuildZoneFallbackDialoguePublicGameplayContextInput(
	Mob *speaker,
	Mob *target,
	const std::string &message
)
{
	return {
		.current_message = message,
		.speaker = PublicGameplayContextEntityInput(speaker),
		.target = PublicGameplayContextEntityInput(target),
		.zone = {
			.short_name = zone ? zone->GetShortName() : "",
			.long_name = zone ? zone->GetLongName() : ""
		},
		.nearby_entities = FallbackDialogueNearbyEntityInputs(speaker, target)
	};
}

void DeliverFallbackDialogueResult(Mob *target, const FallbackDialogue::TargetedSayResult &result)
{
	if (!target || !result.handled) {
		return;
	}

	if (result.output_type == FallbackDialogue::OutputType::Say) {
		target->Say("%s", result.message.c_str());
	} else if (result.output_type == FallbackDialogue::OutputType::Emote) {
		target->Emote("%s", result.message.c_str());
	}
}

}

namespace ZoneFallbackDialogueRuntime {

void HandleTargetedSay(
	Client *speaker,
	Mob *target,
	const std::string &message,
	bool authored_dialogue_handled,
	bool target_engaged
)
{
	if (!speaker || !target) {
		return;
	}

	const auto result = ZoneFallbackDialogueQueue().HandleTargetedSay({
		.speaker_id = speaker->GetID(),
		.target_id = target->GetID(),
		.message = message,
		.target_type = FallbackDialogueTargetType(target),
		.authored_dialogue_handled = authored_dialogue_handled,
		.target_engaged = target_engaged
	}, BuildZoneFallbackDialoguePublicGameplayContextInput(speaker, target, message));

	DeliverFallbackDialogueResult(target, result);
	FallbackDialogue::LogDiagnostic(result);
}

void ProcessReadyDelayedDialogue()
{
	FallbackDialogue::TargetedSayResult result;
	while (ZoneFallbackDialogueQueue().PopReadyResult(CurrentFallbackDialogueInteraction, result)) {
		auto *target = entity_list.GetMob(static_cast<uint16>(result.target_id));
		if (!target) {
			continue;
		}

		DeliverFallbackDialogueResult(target, result);
		FallbackDialogue::LogDiagnostic(result);
	}
}

}

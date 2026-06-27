/*	EQEmu: EQEmulator

	Copyright (C) 2001-2026 EQEmu Development Team

	This program is free software; you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation; either version 3 of the License, or
	(at your option) any later version.
*/

#include "actor_event_recorder.h"
#include "actor_event_persistence_sink.h"

#include "common/spdat.h"
#include "common/timer.h"
#include "zone/mob.h"

#include <algorithm>
#include <mutex>

namespace EQ::ZoneHarness {

namespace {

std::mutex active_recorder_mutex;
ActorEventRecorder *active_recorder = nullptr;

std::string MobKind(Mob *mob)
{
	if (!mob) {
		return "unknown";
	}

	if (mob->IsBot()) {
		return "bot";
	}

	if (mob->IsClient()) {
		return "client";
	}

	if (mob->IsMerc()) {
		return "merc";
	}

	if (mob->IsNPC()) {
		return mob->IsPet() ? "pet" : "npc";
	}

	return "mob";
}

std::string TruncateText(const std::string &text, size_t max_length)
{
	if (text.size() <= max_length) {
		return text;
	}

	return text.substr(0, max_length);
}

ActorEventEntity EntityFor(Mob *mob)
{
	if (!mob) {
		return {};
	}

	return {
		.entity_id = mob->GetID(),
		.entity_ref = "mob:" + std::to_string(mob->GetID()),
		.name = mob->GetCleanName(),
		.kind = MobKind(mob),
	};
}

std::string SpellCategory(uint16_t spell_id)
{
	if (!IsValidSpell(spell_id)) {
		return "unknown";
	}

	if (IsSlowSpell(spell_id)) {
		return "Slow";
	}

	if (IsBeneficialSpell(spell_id)) {
		return "beneficial";
	}

	if (IsDetrimentalSpell(spell_id)) {
		return "detrimental";
	}

	return "other";
}

std::string SpellTargeting(uint16_t spell_id)
{
	if (!IsValidSpell(spell_id)) {
		return "unknown";
	}

	if (IsGroupSpell(spell_id)) {
		return "group";
	}

	if (IsAnyAESpell(spell_id)) {
		return "ae";
	}

	switch (::spells[spell_id].target_type) {
		case ST_Self:
			return "self";
		case ST_Target:
		case ST_TargetOptional:
		case ST_Animal:
		case ST_Undead:
		case ST_Summoned:
		case ST_Pet:
		case ST_SummonedPet:
		case ST_TargetsTarget:
		case ST_PetMaster:
			return "single";
		default:
			return "other";
	}
}

std::string CastingSlotName(uint32_t slot)
{
	if (slot < static_cast<uint32_t>(EQ::spells::CastingSlot::MaxGems)) {
		return "Gem" + std::to_string(slot + 1);
	}

	if (slot == static_cast<uint32_t>(EQ::spells::CastingSlot::Item)) {
		return "Item";
	}

	if (slot == static_cast<uint32_t>(EQ::spells::CastingSlot::Discipline)) {
		return "Discipline";
	}

	if (slot == static_cast<uint32_t>(EQ::spells::CastingSlot::Ability)) {
		return "Ability";
	}

	if (slot == static_cast<uint32_t>(EQ::spells::CastingSlot::PotionBelt)) {
		return "PotionBelt";
	}

	return std::to_string(slot);
}

}

ActorEventEntity DescribeMobEntity(Mob *mob)
{
	return EntityFor(mob);
}

void ActorEventRecorder::RegisterActiveRecorder(ActorEventRecorder *recorder)
{
	std::lock_guard lock(active_recorder_mutex);
	active_recorder = recorder;
}

void ActorEventRecorder::ClearActiveRecorder(ActorEventRecorder *recorder)
{
	std::lock_guard lock(active_recorder_mutex);
	if (active_recorder == recorder) {
		active_recorder = nullptr;
	}
}

void ActorEventRecorder::ObserveSpellCastStarted(
	Mob *caster,
	Mob *target,
	uint16_t spell_id,
	uint32_t slot,
	int32_t cast_time_ms,
	int32_t original_cast_time_ms
)
{
	ActorEventRecorder *recorder = nullptr;
	{
		std::lock_guard lock(active_recorder_mutex);
		recorder = active_recorder;
	}

	if (recorder) {
		recorder->RecordSpellCastStarted(caster, target, spell_id, slot, cast_time_ms, original_cast_time_ms);
	}
}

void ActorEventRecorder::ObserveTargetChanged(Mob *actor, Mob *previous_target, Mob *target)
{
	ActorEventRecorder *recorder = nullptr;
	{
		std::lock_guard lock(active_recorder_mutex);
		recorder = active_recorder;
	}

	if (recorder) {
		recorder->RecordTargetChanged(actor, previous_target, target);
	}
}

void ActorEventRecorder::ObserveSpeechEmitted(
	Mob *actor,
	const std::string &channel,
	const std::string &text,
	uint32_t audible_radius
)
{
	ActorEventRecorder *recorder = nullptr;
	{
		std::lock_guard lock(active_recorder_mutex);
		recorder = active_recorder;
	}

	if (recorder) {
		recorder->RecordSpeechEmitted(actor, channel, text, audible_radius);
	}
}

void ActorEventRecorder::SetPersistenceSink(ActorEventPersistenceSink *sink)
{
	std::lock_guard lock(state_mutex);
	persistence_sink = sink;
}

void ActorEventRecorder::Record(const std::string &type, const std::string &message)
{
	std::lock_guard lock(state_mutex);
	events.push_back({
		.id = next_sequence++,
		.time_ms = ::Timer::GetCurrentTime(),
		.type = type,
		.message = message,
	});

	if (events.size() > max_events) {
		events.erase(events.begin(), events.begin() + static_cast<std::ptrdiff_t>(events.size() - max_events));
	}
}

void ActorEventRecorder::RecordTargetChanged(Mob *actor, Mob *previous_target, Mob *target)
{
	std::lock_guard lock(state_mutex);
	ActorEvent event{
		.id = next_sequence++,
		.time_ms = ::Timer::GetCurrentTime(),
		.type = "target_changed",
		.message = target ? "target_set" : "target_cleared",
		.caster = EntityFor(actor),
	};

	if (previous_target) {
		event.previous_target = EntityFor(previous_target);
	}

	if (target) {
		event.target = EntityFor(target);
	}

	events.push_back(event);
	if (events.size() > max_events) {
		events.erase(events.begin(), events.begin() + static_cast<std::ptrdiff_t>(events.size() - max_events));
	}
}

void ActorEventRecorder::RecordSpeechEmitted(
	Mob *actor,
	const std::string &channel,
	const std::string &text,
	uint32_t audible_radius
)
{
	ActorEvent event{
		.time_ms = ::Timer::GetCurrentTime(),
		.type = "speech_emitted",
		.message = TruncateText(text, 160),
		.caster = EntityFor(actor),
		.speech = {
			.channel = channel,
			.text = TruncateText(text, 160),
			.audible_radius = audible_radius,
		},
	};

	ActorEventPersistenceSink *sink = nullptr;
	{
		std::lock_guard lock(state_mutex);
		event.id = next_sequence++;
		events.push_back(event);
		if (events.size() > max_events) {
			events.erase(events.begin(), events.begin() + static_cast<std::ptrdiff_t>(events.size() - max_events));
		}
		sink = persistence_sink;
	}

	if (sink) {
		sink->PersistSpeechEmitted(actor, event);
	}
}

void ActorEventRecorder::RecordSpellCastStarted(
	Mob *caster,
	Mob *target,
	uint16_t spell_id,
	uint32_t slot,
	int32_t cast_time_ms,
	int32_t original_cast_time_ms
)
{
	std::lock_guard lock(state_mutex);
	ActorEvent event{
		.id = next_sequence++,
		.time_ms = ::Timer::GetCurrentTime(),
		.type = "spell_cast_started",
		.caster = EntityFor(caster),
		.spell = {
			.id = spell_id,
			.name = IsValidSpell(spell_id) ? ::spells[spell_id].name : "UNKNOWN SPELL",
			.category = SpellCategory(spell_id),
			.targeting = SpellTargeting(spell_id),
			.target_type = IsValidSpell(spell_id) ? static_cast<uint32_t>(::spells[spell_id].target_type) : 0,
		},
		.cast = {
			.slot = CastingSlotName(slot),
			.cast_time_ms = cast_time_ms,
			.original_cast_time_ms = original_cast_time_ms,
		},
	};

	if (target) {
		event.target = EntityFor(target);
	}

	events.push_back(event);
	if (events.size() > max_events) {
		events.erase(events.begin(), events.begin() + static_cast<std::ptrdiff_t>(events.size() - max_events));
	}
}

std::vector<ActorEvent> ActorEventRecorder::Drain()
{
	std::lock_guard lock(state_mutex);
	auto drained = events;
	events.clear();
	return drained;
}

std::vector<ActorEvent> ActorEventRecorder::Since(uint64_t since_id, size_t limit) const
{
	std::lock_guard lock(state_mutex);
	std::vector<ActorEvent> result;
	const auto bounded_limit = std::clamp<size_t>(limit, 1, 1000);

	for (const auto &event: events) {
		if (event.id > since_id) {
			result.push_back(event);
			if (result.size() >= bounded_limit) {
				break;
			}
		}
	}

	return result;
}

uint64_t ActorEventRecorder::PendingCount() const
{
	std::lock_guard lock(state_mutex);
	return events.size();
}

uint64_t ActorEventRecorder::MaxEventID() const
{
	std::lock_guard lock(state_mutex);
	return next_sequence > 1 ? next_sequence - 1 : 0;
}

}

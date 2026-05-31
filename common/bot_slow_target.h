/*	EQEmu: EQEmulator

	Copyright (C) 2001-2026 EQEmu Development Team

	This program is free software; you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation; either version 3 of the License, or
	(at your option) any later version.
*/
#pragma once

#include "spdat.h"

#include <cstddef>
#include <vector>

namespace EQ
{
namespace BotSlowTarget
{
enum class ThreatPriority : int {
	OwnerGroupRaid = 0,
	GroupRaidPet = 1,
	None = 2
};

struct Ordering {
	bool           current_target = false;
	ThreatPriority threat_priority = ThreatPriority::None;
	float          distance_squared = 0.0f;
	std::size_t    sequence = 0;
};

inline ThreatPriority GetThreatPriority(bool threatens_owner_group_raid, bool threatens_group_raid_pet)
{
	if (threatens_owner_group_raid) {
		return ThreatPriority::OwnerGroupRaid;
	}

	if (threatens_group_raid_pet) {
		return ThreatPriority::GroupRaidPet;
	}

	return ThreatPriority::None;
}

inline bool IsEngagedHostileThreat(ThreatPriority priority)
{
	return priority != ThreatPriority::None;
}

inline bool CompareOrdering(const Ordering &left, const Ordering &right)
{
	if (left.current_target != right.current_target) {
		return left.current_target;
	}

	if (left.threat_priority != right.threat_priority) {
		return static_cast<int>(left.threat_priority) < static_cast<int>(right.threat_priority);
	}

	if (left.distance_squared != right.distance_squared) {
		return left.distance_squared < right.distance_squared;
	}

	return left.sequence < right.sequence;
}

inline bool UsesSingleTargetMaintenance(uint16 spell_type, bool commanded_spell)
{
	return !commanded_spell && spell_type == BotSpellTypes::Slow;
}

template <typename Candidate, typename IsMezzed, typename CastChecks>
Candidate SelectMaintenanceCandidate(
	const std::vector<Candidate> &candidates,
	bool spell_breaks_mez,
	IsMezzed is_mezzed,
	CastChecks cast_checks
) {
	for (const auto& candidate : candidates) {
		if (spell_breaks_mez && is_mezzed(candidate)) {
			continue;
		}

		if (cast_checks(candidate)) {
			return candidate;
		}
	}

	return Candidate{};
}
}
}

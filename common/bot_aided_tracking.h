/*	EQEmu: EQEmulator

	Copyright (C) 2001-2026 EQEmu Development Team

	This program is free software; you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation; either version 3 of the License, or
	(at your option) any later version.
*/
#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace EQ
{
namespace BotAidedTracking
{
enum class ReportScope {
	All,
	Rare,
	Local
};

struct CandidateSnapshot {
	std::string clean_name;
	uint32_t    rounded_horizontal_distance = 0;
	uint32_t    presentation = 0;
	bool        rare_spawn = false;
};

struct ReportEntry {
	std::string clean_name;
	uint32_t    rounded_horizontal_distance = 0;
	uint32_t    presentation = 0;
	bool        rare_spawn = false;
};

struct Report {
	std::vector<ReportEntry> entries;
	bool                     truncated = false;
};

enum class TrackingBotClass {
	Ranger,
	Druid,
	Bard,
	Other
};

struct CapabilityCandidate {
	TrackingBotClass tracking_class = TrackingBotClass::Other;
	uint8_t          level = 0;
};

struct Capability {
	bool        capable = false;
	std::size_t selected_candidate_index = 0;
	ReportScope report_scope = ReportScope::All;
	uint32_t    base_distance_per_level = 0;
	const char *tracking_message = "";
};

inline bool IsCandidateInScope(const CandidateSnapshot &candidate, ReportScope scope)
{
	return scope != ReportScope::Rare || candidate.rare_spawn;
}

inline Capability CapabilityForRanger(std::size_t candidate_index, const std::string &requested_scope)
{
	if (requested_scope == "local") {
		return {true, candidate_index, ReportScope::Local, 30, "Local tracking..."};
	}

	if (requested_scope == "rare") {
		return {true, candidate_index, ReportScope::Rare, 80, "Master tracking..."};
	}

	return {true, candidate_index, ReportScope::All, 80, "Advanced tracking..."};
}

inline Capability ResolveCapability(
	const std::vector<CapabilityCandidate> &candidates,
	const std::string &requested_scope
) {
	for (std::size_t candidate_index = 0; candidate_index < candidates.size(); ++candidate_index) {
		const auto &candidate = candidates[candidate_index];
		if (candidate.tracking_class == TrackingBotClass::Ranger && candidate.level >= 1) {
			return CapabilityForRanger(candidate_index, requested_scope);
		}
	}

	if (!requested_scope.empty()) {
		return {};
	}

	for (std::size_t candidate_index = 0; candidate_index < candidates.size(); ++candidate_index) {
		const auto &candidate = candidates[candidate_index];
		if (candidate.tracking_class == TrackingBotClass::Druid && candidate.level >= 20) {
			return {true, candidate_index, ReportScope::All, 30, "Local tracking..."};
		}
	}

	for (std::size_t candidate_index = 0; candidate_index < candidates.size(); ++candidate_index) {
		const auto &candidate = candidates[candidate_index];
		if (candidate.tracking_class == TrackingBotClass::Bard && candidate.level >= 35) {
			return {true, candidate_index, ReportScope::All, 20, "Near tracking..."};
		}
	}

	return {};
}

inline Report SelectReport(
	std::vector<CandidateSnapshot> candidates,
	ReportScope scope,
	uint32_t max_range,
	std::size_t result_limit
) {
	std::vector<ReportEntry> eligible_entries;
	eligible_entries.reserve(candidates.size());

	for (const auto &candidate : candidates) {
		if (candidate.rounded_horizontal_distance > max_range) {
			continue;
		}

		if (!IsCandidateInScope(candidate, scope)) {
			continue;
		}

		eligible_entries.push_back(
			{
				candidate.clean_name,
				candidate.rounded_horizontal_distance,
				candidate.presentation,
				candidate.rare_spawn
			}
		);
	}

	std::sort(
		eligible_entries.begin(),
		eligible_entries.end(),
		[](const ReportEntry &left, const ReportEntry &right) {
			if (left.rounded_horizontal_distance != right.rounded_horizontal_distance) {
				return left.rounded_horizontal_distance < right.rounded_horizontal_distance;
			}

			return left.clean_name < right.clean_name;
		}
	);

	Report report;
	report.truncated = eligible_entries.size() > result_limit;

	if (report.truncated) {
		eligible_entries.resize(result_limit);
	}

	report.entries = std::move(eligible_entries);
	return report;
}
}
}

/*	EQEmu: EQEmulator

	Copyright (C) 2001-2026 EQEmu Development Team

	This program is free software; you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation; either version 3 of the License, or
	(at your option) any later version.
*/
#pragma once

#include "common/bot_aided_tracking.h"
#include "cppunit/cpptest.h"

#include <vector>

class BotAidedTrackingTest : public Test::Suite {
public:
	BotAidedTrackingTest()
	{
		TEST_ADD(BotAidedTrackingTest::ReportSelectionExcludesCandidatesOutsideRange);
		TEST_ADD(BotAidedTrackingTest::RareScopeIncludesOnlyRareSpawns);
		TEST_ADD(BotAidedTrackingTest::AllAndLocalScopesKeepOrdinarySpawns);
		TEST_ADD(BotAidedTrackingTest::ReportSelectionSortsByRoundedDistanceThenCleanName);
		TEST_ADD(BotAidedTrackingTest::ReportSelectionCapsEntriesAndReportsTruncation);
		TEST_ADD(BotAidedTrackingTest::ReportSelectionAllowsEmptyReports);
		TEST_ADD(BotAidedTrackingTest::ReportEntriesCarryPresentationValues);
		TEST_ADD(BotAidedTrackingTest::RangerCapabilitySupportsAllScopes);
		TEST_ADD(BotAidedTrackingTest::ReportScopeParsingRejectsUnknownScope);
		TEST_ADD(BotAidedTrackingTest::EmptyScopeFallsBackToDruidThenBard);
		TEST_ADD(BotAidedTrackingTest::NonEmptyScopeRequiresRanger);
		TEST_ADD(BotAidedTrackingTest::CapabilityRejectsUnderleveledTrackingBots);
	}

private:
	using CapabilityCandidate = EQ::BotAidedTracking::CapabilityCandidate;
	using CandidateSnapshot = EQ::BotAidedTracking::CandidateSnapshot;
	using ReportScope = EQ::BotAidedTracking::ReportScope;
	using TrackingBotClass = EQ::BotAidedTracking::TrackingBotClass;

	static CandidateSnapshot Candidate(
		const char *clean_name,
		uint32_t rounded_horizontal_distance,
		bool rare_spawn = false,
		uint32_t presentation = 0
	) {
		return {clean_name, rounded_horizontal_distance, presentation, rare_spawn};
	}

	void ReportSelectionExcludesCandidatesOutsideRange()
	{
		const auto report = EQ::BotAidedTracking::SelectReport(
			{
				Candidate("within range", 50),
				Candidate("at edge", 100),
				Candidate("outside range", 101),
			},
			ReportScope::All,
			100,
			50
		);

		TEST_ASSERT(report.entries.size() == 2);
		TEST_ASSERT(report.entries[0].clean_name == "within range");
		TEST_ASSERT(report.entries[1].clean_name == "at edge");
		TEST_ASSERT(!report.truncated);
	}

	void RareScopeIncludesOnlyRareSpawns()
	{
		const auto report = EQ::BotAidedTracking::SelectReport(
			{
				Candidate("ordinary spawn", 10, false),
				Candidate("rare spawn", 20, true),
			},
			ReportScope::Rare,
			100,
			50
		);

		TEST_ASSERT(report.entries.size() == 1);
		TEST_ASSERT(report.entries[0].clean_name == "rare spawn");
		TEST_ASSERT(report.entries[0].rare_spawn);
	}

	void AllAndLocalScopesKeepOrdinarySpawns()
	{
		const std::vector<CandidateSnapshot> candidates{
			Candidate("ordinary spawn", 10, false),
			Candidate("rare spawn", 20, true),
		};

		const auto all_report = EQ::BotAidedTracking::SelectReport(candidates, ReportScope::All, 100, 50);
		const auto local_report = EQ::BotAidedTracking::SelectReport(candidates, ReportScope::Local, 100, 50);

		TEST_ASSERT(all_report.entries.size() == 2);
		TEST_ASSERT(local_report.entries.size() == 2);
		TEST_ASSERT(!all_report.entries[0].rare_spawn);
		TEST_ASSERT(!local_report.entries[0].rare_spawn);
	}

	void ReportSelectionSortsByRoundedDistanceThenCleanName()
	{
		const auto report = EQ::BotAidedTracking::SelectReport(
			{
				Candidate("zeta", 20),
				Candidate("beta", 10),
				Candidate("alpha", 10),
			},
			ReportScope::All,
			100,
			50
		);

		TEST_ASSERT(report.entries.size() == 3);
		TEST_ASSERT(report.entries[0].clean_name == "alpha");
		TEST_ASSERT(report.entries[1].clean_name == "beta");
		TEST_ASSERT(report.entries[2].clean_name == "zeta");
	}

	void ReportSelectionCapsEntriesAndReportsTruncation()
	{
		const auto report = EQ::BotAidedTracking::SelectReport(
			{
				Candidate("first", 10),
				Candidate("second", 20),
				Candidate("third", 30),
			},
			ReportScope::All,
			100,
			2
		);

		TEST_ASSERT(report.entries.size() == 2);
		TEST_ASSERT(report.entries[0].clean_name == "first");
		TEST_ASSERT(report.entries[1].clean_name == "second");
		TEST_ASSERT(report.truncated);
	}

	void ReportSelectionAllowsEmptyReports()
	{
		const auto report = EQ::BotAidedTracking::SelectReport(
			{
				Candidate("outside range", 101),
				Candidate("ordinary spawn", 10, false),
			},
			ReportScope::Rare,
			100,
			50
		);

		TEST_ASSERT(report.entries.empty());
		TEST_ASSERT(!report.truncated);
	}

	void ReportEntriesCarryPresentationValues()
	{
		const auto report = EQ::BotAidedTracking::SelectReport(
			{
				Candidate("presented spawn", 10, true, 7),
			},
			ReportScope::Rare,
			100,
			50
		);

		TEST_ASSERT(report.entries.size() == 1);
		TEST_ASSERT(report.entries[0].presentation == 7);
	}

	void RangerCapabilitySupportsAllScopes()
	{
		const std::vector<CapabilityCandidate> candidates{
			{TrackingBotClass::Ranger, 1},
		};

		const auto all_capability = EQ::BotAidedTracking::ResolveCapability(candidates, "");
		const auto rare_capability = EQ::BotAidedTracking::ResolveCapability(candidates, "rare");
		const auto local_capability = EQ::BotAidedTracking::ResolveCapability(candidates, "local");

		TEST_ASSERT(all_capability.capable);
		TEST_ASSERT(all_capability.selected_candidate_index == 0);
		TEST_ASSERT(all_capability.report_scope == ReportScope::All);
		TEST_ASSERT(all_capability.base_distance_per_level == 80);
		TEST_ASSERT(std::string(all_capability.tracking_message) == "Advanced tracking...");

		TEST_ASSERT(rare_capability.capable);
		TEST_ASSERT(rare_capability.report_scope == ReportScope::Rare);
		TEST_ASSERT(rare_capability.base_distance_per_level == 80);
		TEST_ASSERT(std::string(rare_capability.tracking_message) == "Master tracking...");

		TEST_ASSERT(local_capability.capable);
		TEST_ASSERT(local_capability.report_scope == ReportScope::Local);
		TEST_ASSERT(local_capability.base_distance_per_level == 30);
		TEST_ASSERT(std::string(local_capability.tracking_message) == "Local tracking...");
	}

	void ReportScopeParsingRejectsUnknownScope()
	{
		ReportScope scope = ReportScope::All;

		TEST_ASSERT(EQ::BotAidedTracking::TryParseReportScope("", scope));
		TEST_ASSERT(scope == ReportScope::All);

		TEST_ASSERT(EQ::BotAidedTracking::TryParseReportScope("all", scope));
		TEST_ASSERT(scope == ReportScope::All);

		TEST_ASSERT(EQ::BotAidedTracking::TryParseReportScope("rare", scope));
		TEST_ASSERT(scope == ReportScope::Rare);

		TEST_ASSERT(EQ::BotAidedTracking::TryParseReportScope("local", scope));
		TEST_ASSERT(scope == ReportScope::Local);

		TEST_ASSERT(!EQ::BotAidedTracking::TryParseReportScope("banana", scope));
	}

	void EmptyScopeFallsBackToDruidThenBard()
	{
		const auto druid_capability = EQ::BotAidedTracking::ResolveCapability(
			{
				{TrackingBotClass::Bard, 35},
				{TrackingBotClass::Druid, 20},
			},
			""
		);
		const auto bard_capability = EQ::BotAidedTracking::ResolveCapability(
			{
				{TrackingBotClass::Bard, 35},
			},
			""
		);

		TEST_ASSERT(druid_capability.capable);
		TEST_ASSERT(druid_capability.selected_candidate_index == 1);
		TEST_ASSERT(druid_capability.report_scope == ReportScope::All);
		TEST_ASSERT(druid_capability.base_distance_per_level == 30);
		TEST_ASSERT(std::string(druid_capability.tracking_message) == "Local tracking...");

		TEST_ASSERT(bard_capability.capable);
		TEST_ASSERT(bard_capability.selected_candidate_index == 0);
		TEST_ASSERT(bard_capability.report_scope == ReportScope::All);
		TEST_ASSERT(bard_capability.base_distance_per_level == 20);
		TEST_ASSERT(std::string(bard_capability.tracking_message) == "Near tracking...");
	}

	void NonEmptyScopeRequiresRanger()
	{
		const auto rare_capability = EQ::BotAidedTracking::ResolveCapability(
			{
				{TrackingBotClass::Druid, 20},
				{TrackingBotClass::Bard, 35},
			},
			"rare"
		);
		const auto local_capability = EQ::BotAidedTracking::ResolveCapability(
			{
				{TrackingBotClass::Druid, 20},
			},
			"local"
		);

		TEST_ASSERT(!rare_capability.capable);
		TEST_ASSERT(!local_capability.capable);
	}

	void CapabilityRejectsUnderleveledTrackingBots()
	{
		const auto capability = EQ::BotAidedTracking::ResolveCapability(
			{
				{TrackingBotClass::Druid, 19},
				{TrackingBotClass::Bard, 34},
			},
			""
		);

		TEST_ASSERT(!capability.capable);
		TEST_ASSERT(capability.base_distance_per_level == 0);
		TEST_ASSERT(std::string(capability.tracking_message).empty());
	}
};

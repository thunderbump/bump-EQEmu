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

#include "zone/zone_cli.h"

#include "common/eqemu_logsys.h"
#include "common/json/json.h"
#include "common/repositories/actor_events_repository.h"
#include "common/repositories/actor_profiles_repository.h"
#include "common/repositories/player_event_logs_repository.h"
#include "common/strings.h"
#include "zone/bot.h"
#include "zone/harness/actor_event_persistence_sink.h"
#include "zone/harness/actor_event_recorder.h"
#include "zone/harness/owned_bot_actor_fixture.h"
#include "zone/zone.h"
#include "zone/zonedb.h"

#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

class TestFailure final : public std::runtime_error {
public:
	using std::runtime_error::runtime_error;
};

[[noreturn]] void Fail(const std::string &message)
{
	throw TestFailure(message);
}

void Expect(bool condition, const std::string &message)
{
	if (!condition) {
		Fail(message);
	}
}

template <typename T>
void ExpectEqual(const T &actual, const T &expected, const std::string &message)
{
	if (actual != expected) {
		Fail(message);
	}
}

uint32_t BuildRunNonce()
{
	const auto now = static_cast<uint64_t>(
		std::chrono::system_clock::now().time_since_epoch().count()
	);

	return static_cast<uint32_t>((now ^ (now >> 32)) & 0x0fffffff);
}

Json::Value ParseJson(const std::string &document)
{
	Json::CharReaderBuilder builder;
	std::unique_ptr<Json::CharReader> reader(builder.newCharReader());
	Json::Value root;
	std::string errors;
	const bool ok = reader->parse(
		document.data(),
		document.data() + document.size(),
		&root,
		&errors
	);

	Expect(ok, "actor event payload should be valid JSON: " + errors);
	return root;
}

int64_t CountPlayerEventLogRowsWithMarker(const std::string &marker)
{
	auto results = database.QueryDatabase(
		fmt::format(
			"SELECT COUNT(*) FROM player_event_logs WHERE event_data LIKE '%{}%'",
			Strings::Escape(marker)
		)
	);

	Expect(results.Success() && results.RowCount() == 1 && results.begin()[0], "player_event_logs marker count query should succeed");
	return strtoll(results.begin()[0], nullptr, 10);
}

class ActorEventPersistenceCleanup {
public:
	void TrackActorId(uint32_t actor_id)
	{
		if (actor_id > 0) {
			actor_ids_.push_back(actor_id);
		}
	}

	~ActorEventPersistenceCleanup()
	{
		for (auto actor_id: actor_ids_) {
			ActorEventsRepository::DeleteByActorId(database, actor_id);
		}

		for (auto it = actor_ids_.rbegin(); it != actor_ids_.rend(); ++it) {
			ActorProfilesRepository::DeleteOne(database, *it);
		}
	}

private:
	std::vector<uint32_t> actor_ids_;
};

} // namespace

void ZoneCLI::TestActorEvents(int argc, char **argv, argh::parser &cmd, std::string &description)
{
	description = "Validates runtime speech_emitted actor event persistence through the harness recorder path";

	if (cmd[{"-h", "--help"}]) {
		return;
	}

	EQEmuLogSys::Instance()->SilenceConsoleLogging();

	try {
		const auto run_nonce = BuildRunNonce();

		Zone::Bootup(ZoneID("qrg"), 0, false);
		zone->StopShutdownTimer();
		entity_list.Process();
		entity_list.MobProcess();

		EQ::ZoneHarness::ActorEventRecorder recorder;
		EQ::ZoneHarness::ActorEventRepositoryPersistenceSink persistence_sink;
		recorder.SetPersistenceSink(&persistence_sink);
		EQ::ZoneHarness::ActorEventRecorder::RegisterActiveRecorder(&recorder);

		ActorEventPersistenceCleanup cleanup;
		EQ::ZoneHarness::OwnedBotActorFixture fixture;
		Expect(fixture.SetUpOwnedBotSolo(), "owned bot harness fixture should boot");
		Expect(fixture.OwnedBot() != nullptr, "owned bot harness fixture should create a bot actor");

		const auto next_free_bot_id = [&](uint32_t salt) -> uint32_t {
			for (uint32_t attempt = 0; attempt < 1000; ++attempt) {
				const auto candidate = 420000000u + ((run_nonce + salt + attempt) % 80000000u);
				if (!ActorProfilesRepository::FindByBotId(database, candidate).has_value()) {
					return candidate;
				}
			}

			Fail("failed to find a collision-safe bot_id for runtime actor event persistence test");
		};

		const auto actor_bot_id = next_free_bot_id(0);
		const auto owner_character_id = 410000000u + ((run_nonce + 77u) % 90000000u);
		fixture.AssignBotID(fixture.OwnedBot(), actor_bot_id);

		ActorProfilesRepository::ActorProfileRecord profile{};
		profile.actor_type = "autonomous_actor";
		profile.actor_substrate = "bot";
		profile.bot_id = actor_bot_id;
		profile.owner_character_id = owner_character_id;
		profile.enabled = true;
		const auto inserted_profile = ActorProfilesRepository::UpsertBotBackedProfile(database, profile);
		Expect(inserted_profile.actor_id > 0, "actor profile insert should allocate an actor_id");
		cleanup.TrackActorId(inserted_profile.actor_id);

		const auto speech_marker = fmt::format("runtime-actor-events-{}", run_nonce);
		fixture.OwnedBot()->Say("%s", speech_marker.c_str());

		const auto observed_events = recorder.Since(0, 8);
		ExpectEqual(observed_events.size(), static_cast<size_t>(1), "runtime actor event recorder should observe one speech_emitted event");
		ExpectEqual(observed_events[0].type, std::string("speech_emitted"), "runtime recorder should observe speech_emitted");
		ExpectEqual(observed_events[0].speech.channel, std::string("say"), "runtime recorder should preserve speech channel");
		ExpectEqual(observed_events[0].speech.text, speech_marker, "runtime recorder should preserve emitted speech text");
		ExpectEqual(observed_events[0].caster.entity_id, fixture.OwnedBot()->GetID(), "runtime recorder should preserve emitting entity id");

		const auto persisted_events = ActorEventsRepository::ReadCursor(database, inserted_profile.actor_id, 0, 8);
		ExpectEqual(persisted_events.size(), static_cast<size_t>(1), "runtime speech_emitted path should persist one actor event row");
		ExpectEqual(persisted_events[0].actor_id, inserted_profile.actor_id, "persisted runtime actor event should target the actor profile");
		ExpectEqual(persisted_events[0].bot_id, std::optional<uint32_t>(actor_bot_id), "persisted runtime actor event should keep bot_id");
		ExpectEqual(persisted_events[0].owner_character_id, std::optional<uint32_t>(owner_character_id), "persisted runtime actor event should keep owner_character_id");
		ExpectEqual(persisted_events[0].entity_id, std::optional<uint32_t>(fixture.OwnedBot()->GetID()), "persisted runtime actor event should keep entity_id");
		ExpectEqual(persisted_events[0].event_type, std::string("speech_emitted"), "persisted runtime actor event should keep speech_emitted type");

		const auto payload = ParseJson(persisted_events[0].event_json);
		ExpectEqual(payload["channel"].asString(), std::string("say"), "persisted runtime actor event should keep speech channel");
		ExpectEqual(payload["text"].asString(), speech_marker, "persisted runtime actor event should keep speech text");
		ExpectEqual(payload["audible_radius"].asUInt(), 200u, "persisted runtime actor event should keep say audible radius");

		ExpectEqual(
			CountPlayerEventLogRowsWithMarker(speech_marker),
			int64_t(0),
			"runtime actor event persistence should not write marker rows to player_event_logs"
		);

		EQ::ZoneHarness::ActorEventRecorder::ClearActiveRecorder(&recorder);
		std::cout << "[PASS] actor-events-runtime\n";
	}
	catch (const TestFailure &e) {
		std::cerr << "[FAIL] " << e.what() << "\n";
		std::exit(1);
	}
}

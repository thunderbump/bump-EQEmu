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
#include "world/world_server_cli.h"

#include "common/actor_reserved_owners.h"
#include "common/eqemu_logsys.h"
#include "common/json/json.h"
#include "common/repositories/actor_events_repository.h"
#include "common/repositories/actor_profiles_repository.h"
#include "common/repositories/player_event_logs_repository.h"
#include "common/strings.h"
#include "world/worlddb.h"

#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <optional>
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

int64_t CountPlayerEventLogRowsWithMarker(Database &database, const std::string &marker)
{
	auto results = database.QueryDatabase(
		fmt::format(
			"SELECT COUNT(*) FROM player_event_logs WHERE event_data LIKE '%{}%'",
			Strings::Escape(marker)
		)
	);

	if (!results.Success() || results.RowCount() != 1 || !results.begin()[0]) {
		return -1;
	}

	return strtoll(results.begin()[0], nullptr, 10);
}

class ActorEventPersistenceCleanup {
public:
	explicit ActorEventPersistenceCleanup(Database &database)
	: database_(database)
	{
	}

	void TrackActorId(uint32_t actor_id)
	{
		if (actor_id > 0) {
			actor_ids_.push_back(actor_id);
		}
	}

	void TrackReservedOwnerId(uint32_t character_id)
	{
		if (character_id > 0) {
			reserved_owner_ids_.push_back(character_id);
		}
	}

	~ActorEventPersistenceCleanup()
	{
		for (auto actor_id: actor_ids_) {
			ActorEventsRepository::DeleteByActorId(database_, actor_id);
		}

		for (auto it = actor_ids_.rbegin(); it != actor_ids_.rend(); ++it) {
			ActorProfilesRepository::DeleteOne(database_, *it);
		}

		for (auto it = reserved_owner_ids_.rbegin(); it != reserved_owner_ids_.rend(); ++it) {
			std::string unused_reason;
			EQ::Actor::ReservedOwners::Rollback(database_, *it, &unused_reason);
		}
	}

private:
	Database &database_;
	std::vector<uint32_t> actor_ids_;
	std::vector<uint32_t> reserved_owner_ids_;
};

ActorProfilesRepository::ActorProfileRecord CreateProfile(
	Database &database,
	ActorEventPersistenceCleanup &cleanup,
	uint32_t bot_id,
	time_t created_at,
	time_t updated_at
)
{
	ActorProfilesRepository::ActorProfileRecord profile{};
	profile.actor_type = "autonomous_actor";
	profile.actor_substrate = "bot";
	profile.bot_id = bot_id;
	const auto reserved_owner = EQ::Actor::ReservedOwners::Provision(
		database,
		fmt::format("ActorownerEvent{}{}", bot_id, created_at)
	);
	if (!reserved_owner.character_id) {
		Fail("reserved owner provisioning should succeed for actor event profile");
	}
	cleanup.TrackReservedOwnerId(reserved_owner.character_id);
	profile.owner_character_id = reserved_owner.character_id;
	profile.enabled = true;
	profile.created_at = created_at;
	profile.updated_at = updated_at;

	const auto inserted = ActorProfilesRepository::UpsertBotBackedProfile(database, profile);
	cleanup.TrackActorId(inserted.actor_id);
	return inserted;
}

} // namespace

void WorldserverCLI::TestActorEvents(int argc, char **argv, argh::parser &cmd, std::string &description)
{
	description = "Validates actor event persistence and actor-scoped cursor reads";

	if (cmd[{"-h", "--help"}]) {
		return;
	}

	EQEmuLogSys::Instance()->SilenceConsoleLogging();

	constexpr time_t event_one_created_at = 1719446400;
	constexpr time_t event_two_created_at = 1719446700;
	constexpr time_t event_three_created_at = 1719446500;
	constexpr time_t event_four_created_at = 1719446600;

	try {
		ActorEventPersistenceCleanup cleanup(database);
		const auto run_nonce = BuildRunNonce();

		const auto next_free_bot_id = [&](uint32_t salt) -> uint32_t {
			for (uint32_t attempt = 0; attempt < 1000; ++attempt) {
				const auto candidate = 410000000u + ((run_nonce + salt + attempt) % 90000000u);
				if (!ActorProfilesRepository::FindByBotId(database, candidate).has_value()) {
					return candidate;
				}
			}

			Fail("failed to find a collision-safe bot_id for actor event persistence test");
		};

		const auto actor_a = CreateProfile(
			database,
			cleanup,
			next_free_bot_id(0),
			event_one_created_at,
			event_one_created_at
		);
		const auto actor_b = CreateProfile(
			database,
			cleanup,
			next_free_bot_id(1000),
			event_one_created_at,
			event_one_created_at
		);

		const auto player_event_log_count_before = PlayerEventLogsRepository::Count(database);

		const auto first_event = ActorEventsRepository::AppendObservedSpeechEmitted(
			database,
			{
				.actor_id = actor_a.actor_id,
				.bot_id = actor_a.bot_id,
				.channel = "say",
				.text = fmt::format("actor-events-{}-one", run_nonce),
				.audible_radius = 50,
				.created_at = event_one_created_at,
			}
		);
		Expect(first_event.event_id > 0, "first actor event insert should allocate an event_id");
		ExpectEqual(first_event.actor_id, actor_a.actor_id, "first actor event should persist actor_id");
		ExpectEqual(first_event.event_type, std::string("speech_emitted"), "first actor event should persist speech_emitted type");

		const auto first_payload = ParseJson(first_event.event_json);
		ExpectEqual(first_payload["channel"].asString(), std::string("say"), "first actor event should persist speech channel");
		ExpectEqual(first_payload["text"].asString(), fmt::format("actor-events-{}-one", run_nonce), "first actor event should persist speech text");
		ExpectEqual(first_payload["audible_radius"].asUInt(), 50u, "first actor event should persist speech audible radius");

		const auto second_event = ActorEventsRepository::AppendObservedSpeechEmitted(
			database,
			{
				.actor_id = actor_a.actor_id,
				.bot_id = actor_a.bot_id,
				.channel = "group",
				.text = fmt::format("actor-events-{}-two", run_nonce),
				.audible_radius = 100,
				.created_at = event_two_created_at,
			}
		);
		Expect(second_event.event_id > first_event.event_id, "second actor event should increase event_id monotonically");

		const auto third_event = ActorEventsRepository::AppendObservedSpeechEmitted(
			database,
			{
				.actor_id = actor_b.actor_id,
				.bot_id = actor_b.bot_id,
				.channel = "say",
				.text = fmt::format("actor-events-{}-other-actor", run_nonce),
				.audible_radius = 25,
				.created_at = event_three_created_at,
			}
		);
		Expect(third_event.event_id > second_event.event_id, "third actor event should increase event_id monotonically across actors");

		const auto fourth_event = ActorEventsRepository::AppendObservedSpeechEmitted(
			database,
			{
				.actor_id = actor_a.actor_id,
				.bot_id = actor_a.bot_id,
				.channel = "raid",
				.text = fmt::format("actor-events-{}-three", run_nonce),
				.audible_radius = 150,
				.created_at = event_four_created_at,
			}
		);
		Expect(fourth_event.event_id > third_event.event_id, "fourth actor event should increase event_id monotonically");

		const auto actor_a_events = ActorEventsRepository::ReadCursor(database, actor_a.actor_id, 0, 10);
		ExpectEqual(actor_a_events.size(), static_cast<size_t>(3), "actor cursor should return only the selected actor events");
		ExpectEqual(actor_a_events[0].event_id, first_event.event_id, "actor cursor should order by event_id ascending");
		ExpectEqual(actor_a_events[1].event_id, second_event.event_id, "actor cursor should preserve second actor event ordering");
		ExpectEqual(actor_a_events[2].event_id, fourth_event.event_id, "actor cursor should skip other actors and preserve later ordering");

		const auto actor_a_limited = ActorEventsRepository::ReadCursor(database, actor_a.actor_id, 0, 2);
		ExpectEqual(actor_a_limited.size(), static_cast<size_t>(2), "actor cursor should honor limit");
		ExpectEqual(actor_a_limited[0].event_id, first_event.event_id, "limited cursor should return the earliest matching actor event first");
		ExpectEqual(actor_a_limited[1].event_id, second_event.event_id, "limited cursor should stop at the requested boundary");

		const auto actor_a_zero_limit = ActorEventsRepository::ReadCursor(database, actor_a.actor_id, 0, 0);
		ExpectEqual(actor_a_zero_limit.size(), static_cast<size_t>(0), "actor cursor limit=0 should return an empty page");

		const auto actor_a_after_second = ActorEventsRepository::ReadCursor(database, actor_a.actor_id, second_event.event_id, 10);
		ExpectEqual(actor_a_after_second.size(), static_cast<size_t>(1), "actor cursor should resume after a prior event_id");
		ExpectEqual(actor_a_after_second[0].event_id, fourth_event.event_id, "actor cursor should resume from the next actor-scoped event");

		ExpectEqual(
			CountPlayerEventLogRowsWithMarker(database, fmt::format("actor-events-{}", run_nonce)),
			int64_t(0),
			"persisting actor events should not write marker rows to player_event_logs"
		);
		ExpectEqual(
			PlayerEventLogsRepository::Count(database),
			player_event_log_count_before,
			"persisting actor events should not write to player_event_logs"
		);

		std::cout << "[PASS] actor-events\n";
	}
	catch (const TestFailure &e) {
		std::cerr << "[FAIL] " << e.what() << "\n";
		std::exit(1);
	}
}

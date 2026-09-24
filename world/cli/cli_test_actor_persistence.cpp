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
#include "common/repositories/actor_profiles_repository.h"
#include "common/repositories/actor_status_repository.h"
#include "world/worlddb.h"

#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <iostream>
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

template <typename T>
void ExpectOptionalEqual(
	const std::optional<T> &actual,
	const std::optional<T> &expected,
	const std::string &message
)
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

class ActorPersistenceCleanup {
public:
	explicit ActorPersistenceCleanup(Database &database)
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

	~ActorPersistenceCleanup()
	{
		for (auto it = actor_ids_.rbegin(); it != actor_ids_.rend(); ++it) {
			ActorStatusRepository::DeleteOne(database_, *it);
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

} // namespace

void WorldserverCLI::TestActorPersistence(int argc, char **argv, argh::parser &cmd, std::string &description)
{
	description = "Validates actor profile and actor status persistence helpers";

	if (cmd[{"-h", "--help"}]) {
		return;
	}

	EQEmuLogSys::Instance()->SilenceConsoleLogging();

	constexpr uint32_t first_zone_id       = 203;
	constexpr uint32_t second_zone_id      = 219;
	constexpr uint32_t first_entity_id     = 99001;
	constexpr uint32_t first_instance_id   = 77;
	constexpr std::time_t created_at       = 1719446400;
	constexpr std::time_t first_updated_at = 1719446460;
	constexpr std::time_t second_updated_at = 1719446520;
	constexpr std::time_t third_updated_at = 1719446580;
	constexpr std::time_t epoch_heartbeat_at = 0;

	try {
		ActorPersistenceCleanup cleanup(database);
		const auto run_nonce = BuildRunNonce();

		const auto next_free_bot_id = [&](uint32_t salt) -> uint32_t {
			for (uint32_t attempt = 0; attempt < 1000; ++attempt) {
				const auto candidate = 400000000u + ((run_nonce + salt + attempt) % 100000000u);
				if (!ActorProfilesRepository::FindByBotId(database, candidate).has_value()) {
					return candidate;
				}
			}

			Fail("failed to find a collision-safe bot_id for actor persistence test");
		};

		const auto actor_bot_id = next_free_bot_id(0);
		const auto mismatched_owner_character_id = 401000000u + ((run_nonce + 77u) % 100000000u);
		const auto null_profile_tag = fmt::format("base_null_{}", run_nonce);
		const auto reserved_owner = EQ::Actor::ReservedOwners::Provision(
			database,
			fmt::format("ActorownerPersistence{}", run_nonce)
		);
		Expect(reserved_owner.character_id > 0, "reserved owner provisioning should succeed for actor profile persistence");
		cleanup.TrackReservedOwnerId(reserved_owner.character_id);

		ActorProfilesRepository::ActorProfileRecord first_profile{};
		first_profile.actor_type      = "autonomous_actor";
		first_profile.actor_substrate = "bot";
		first_profile.bot_id          = actor_bot_id;
		first_profile.enabled         = true;
		first_profile.created_at      = created_at;
		first_profile.updated_at      = first_updated_at;

		const auto inserted_profile = ActorProfilesRepository::UpsertBotBackedProfile(database, first_profile);
		ExpectEqual(inserted_profile.actor_id, uint32_t(0), "bot-backed profile insert should reject a missing reserved owner binding");
		Expect(
			!ActorProfilesRepository::FindByBotId(database, actor_bot_id).has_value(),
			"bot-backed profile insert without reserved owner binding should not persist a row"
		);

		first_profile.owner_character_id = reserved_owner.character_id;
		const auto bound_profile = ActorProfilesRepository::UpsertBotBackedProfile(database, first_profile);
		cleanup.TrackActorId(bound_profile.actor_id);

		Expect(bound_profile.actor_id > 0, "bound actor profile insert should allocate an actor_id");
		ExpectEqual(bound_profile.actor_type, first_profile.actor_type, "profile actor_type should round-trip");
		ExpectEqual(bound_profile.actor_substrate, first_profile.actor_substrate, "profile actor_substrate should round-trip");
		ExpectOptionalEqual(bound_profile.bot_id, std::optional<uint32_t>(actor_bot_id), "profile bot_id should round-trip");
		ExpectOptionalEqual(bound_profile.owner_character_id, std::optional<uint32_t>(reserved_owner.character_id), "profile owner_character_id should store the reserved owner binding");
		ExpectEqual(bound_profile.enabled, true, "profile enabled flag should round-trip");
		ExpectEqual(bound_profile.created_at, created_at, "profile created_at should round-trip");
		ExpectEqual(bound_profile.updated_at, first_updated_at, "profile updated_at should round-trip");

		const auto loaded_profile_by_id = ActorProfilesRepository::FindByActorId(database, bound_profile.actor_id);
		Expect(loaded_profile_by_id.has_value(), "profile lookup by actor_id should succeed");
		ExpectEqual(loaded_profile_by_id->actor_id, bound_profile.actor_id, "lookup by actor_id should return the inserted profile");

		auto updated_profile = bound_profile;
		updated_profile.enabled            = false;
		updated_profile.updated_at         = second_updated_at;

		const auto upserted_profile = ActorProfilesRepository::UpsertBotBackedProfile(database, updated_profile);

		ExpectEqual(upserted_profile.actor_id, bound_profile.actor_id, "profile upsert should reuse the existing actor_id");
		ExpectOptionalEqual(upserted_profile.owner_character_id, std::optional<uint32_t>(reserved_owner.character_id), "profile upsert should preserve owner_character_id");
		ExpectEqual(upserted_profile.enabled, false, "profile upsert should update enabled");
		ExpectEqual(upserted_profile.created_at, created_at, "profile upsert should preserve created_at");
		ExpectEqual(upserted_profile.updated_at, second_updated_at, "profile upsert should update updated_at");
		ExpectEqual(
			ActorProfilesRepository::Count(database, fmt::format("bot_id = {}", actor_bot_id)),
			static_cast<int64>(1),
			"profile upsert should remain idempotent by bot_id"
		);

		auto cleared_profile = upserted_profile;
		cleared_profile.owner_character_id = std::nullopt;
		cleared_profile.enabled            = true;
		cleared_profile.updated_at         = third_updated_at;

		const auto cleared_upserted_profile = ActorProfilesRepository::UpsertBotBackedProfile(database, cleared_profile);

		ExpectEqual(cleared_upserted_profile.actor_id, uint32_t(0), "profile upsert should reject clearing the reserved owner binding");
		const auto persisted_after_clear_attempt = ActorProfilesRepository::FindByBotId(database, actor_bot_id);
		Expect(persisted_after_clear_attempt.has_value(), "existing bot-backed profile should remain after a rejected clear attempt");
		ExpectOptionalEqual(persisted_after_clear_attempt->owner_character_id, std::optional<uint32_t>(reserved_owner.character_id), "rejected clear attempt should preserve the stored reserved owner binding");
		ExpectEqual(persisted_after_clear_attempt->enabled, false, "rejected clear attempt should leave prior stored enabled state intact");
		ExpectEqual(persisted_after_clear_attempt->updated_at, second_updated_at, "rejected clear attempt should leave prior stored updated_at intact");

		auto mismatched_profile = upserted_profile;
		mismatched_profile.owner_character_id = mismatched_owner_character_id;
		mismatched_profile.updated_at         = third_updated_at;
		const auto mismatched_upserted_profile = ActorProfilesRepository::UpsertBotBackedProfile(database, mismatched_profile);
		ExpectEqual(mismatched_upserted_profile.actor_id, uint32_t(0), "profile upsert should reject an arbitrary non-reserved owner binding");
		const auto persisted_after_mismatch_attempt = ActorProfilesRepository::FindByBotId(database, actor_bot_id);
		Expect(persisted_after_mismatch_attempt.has_value(), "existing bot-backed profile should remain after a rejected owner mismatch");
		ExpectOptionalEqual(persisted_after_mismatch_attempt->owner_character_id, std::optional<uint32_t>(reserved_owner.character_id), "rejected owner mismatch should preserve the stored reserved owner binding");

		auto first_base_profile = ActorProfilesRepository::NewEntity();
		first_base_profile.actor_type         = "autonomous_actor";
		first_base_profile.actor_substrate    = null_profile_tag;
		first_base_profile.bot_id             = std::nullopt;
		first_base_profile.owner_character_id = std::nullopt;
		first_base_profile.enabled            = 1;
		first_base_profile.created_at         = created_at;
		first_base_profile.updated_at         = first_updated_at;

		first_base_profile = ActorProfilesRepository::InsertOne(database, first_base_profile);
		cleanup.TrackActorId(first_base_profile.actor_id);

		Expect(first_base_profile.actor_id > 0, "base profile insert with null bot_id should allocate an actor_id");
		ExpectOptionalEqual(first_base_profile.bot_id, std::optional<uint32_t>{}, "base profile insert should preserve null bot_id");
		ExpectOptionalEqual(first_base_profile.owner_character_id, std::optional<uint32_t>{}, "base profile insert should preserve null owner_character_id");

		auto second_base_profile = first_base_profile;
		second_base_profile.actor_id    = 0;
		second_base_profile.created_at  = second_updated_at;
		second_base_profile.updated_at  = second_updated_at;

		second_base_profile = ActorProfilesRepository::InsertOne(database, second_base_profile);
		cleanup.TrackActorId(second_base_profile.actor_id);

		Expect(second_base_profile.actor_id > 0, "second base profile insert with null bot_id should allocate an actor_id");
		Expect(
			second_base_profile.actor_id != first_base_profile.actor_id,
			"unique null bot_id handling should allow multiple unbound base profiles"
		);

		const auto loaded_second_base_profile = ActorProfilesRepository::FindOne(database, second_base_profile.actor_id);
		ExpectOptionalEqual(loaded_second_base_profile.bot_id, std::optional<uint32_t>{}, "base profile find should preserve null bot_id");
		ExpectOptionalEqual(loaded_second_base_profile.owner_character_id, std::optional<uint32_t>{}, "base profile find should preserve null owner_character_id");

		ActorStatusRepository::ActorStatusRecord first_status{};
		first_status.actor_id     = bound_profile.actor_id;
		first_status.zone_id      = first_zone_id;
		first_status.entity_id    = first_entity_id;
		first_status.state        = "active";
		first_status.status_json  = R"({"summary":"engaged"})";
		first_status.heartbeat_at = first_updated_at;
		first_status.updated_at   = first_updated_at;

		const auto inserted_status = ActorStatusRepository::UpsertOne(database, first_status);

		ExpectEqual(inserted_status.actor_id, bound_profile.actor_id, "status insert should target the actor profile");
		ExpectOptionalEqual(inserted_status.zone_id, std::optional<uint32_t>(first_zone_id), "status zone_id should round-trip");
		ExpectOptionalEqual(inserted_status.instance_id, std::optional<uint32_t>{}, "status instance_id should start empty");
		ExpectOptionalEqual(inserted_status.entity_id, std::optional<uint32_t>(first_entity_id), "status entity_id should round-trip");
		ExpectEqual(inserted_status.state, std::string("active"), "status state should round-trip");
		ExpectOptionalEqual(inserted_status.status_json, std::optional<std::string>(R"({"summary":"engaged"})"), "status_json should round-trip");
		ExpectOptionalEqual(inserted_status.heartbeat_at, std::optional<std::time_t>(first_updated_at), "heartbeat_at should round-trip");
		ExpectEqual(inserted_status.updated_at, first_updated_at, "status updated_at should round-trip");

		auto updated_status = inserted_status;
		updated_status.zone_id      = second_zone_id;
		updated_status.instance_id  = first_instance_id;
		updated_status.entity_id    = std::nullopt;
		updated_status.state        = "idle";
		updated_status.status_json  = std::nullopt;
		updated_status.heartbeat_at = second_updated_at;
		updated_status.updated_at   = second_updated_at;

		const auto upserted_status = ActorStatusRepository::UpsertOne(database, updated_status);

		ExpectEqual(upserted_status.actor_id, bound_profile.actor_id, "status upsert should preserve actor_id");
		ExpectOptionalEqual(upserted_status.zone_id, std::optional<uint32_t>(second_zone_id), "status upsert should update zone_id");
		ExpectOptionalEqual(upserted_status.instance_id, std::optional<uint32_t>(first_instance_id), "status upsert should update instance_id");
		ExpectOptionalEqual(upserted_status.entity_id, std::optional<uint32_t>{}, "status upsert should clear entity_id");
		ExpectEqual(upserted_status.state, std::string("idle"), "status upsert should update state");
		ExpectOptionalEqual(upserted_status.status_json, std::optional<std::string>{}, "status upsert should clear status_json");
		ExpectOptionalEqual(upserted_status.heartbeat_at, std::optional<std::time_t>(second_updated_at), "status upsert should update heartbeat_at");
		ExpectEqual(upserted_status.updated_at, second_updated_at, "status upsert should update updated_at");
		ExpectEqual(
			ActorStatusRepository::Count(database, fmt::format("actor_id = {}", bound_profile.actor_id)),
			static_cast<int64>(1),
			"status upsert should remain idempotent by actor_id"
		);

		auto epoch_status = upserted_status;
		epoch_status.status_json  = R"({"summary":"epoch"})";
		epoch_status.heartbeat_at = epoch_heartbeat_at;
		epoch_status.updated_at   = third_updated_at;

		const auto epoch_upserted_status = ActorStatusRepository::UpsertOne(database, epoch_status);

		ExpectOptionalEqual(epoch_upserted_status.heartbeat_at, std::optional<std::time_t>(epoch_heartbeat_at), "status upsert should preserve Unix epoch heartbeat_at");
		ExpectOptionalEqual(epoch_upserted_status.status_json, std::optional<std::string>(R"({"summary":"epoch"})"), "status upsert should preserve valid status_json while storing epoch heartbeat");

		auto base_status = ActorStatusRepository::NewEntity();
		base_status.actor_id     = second_base_profile.actor_id;
		base_status.zone_id      = std::nullopt;
		base_status.instance_id  = std::nullopt;
		base_status.entity_id    = std::nullopt;
		base_status.state        = "base_null";
		base_status.status_json  = std::nullopt;
		base_status.heartbeat_at = std::nullopt;
		base_status.updated_at   = third_updated_at;

		ActorStatusRepository::InsertOne(database, base_status);

		const auto loaded_base_status = ActorStatusRepository::FindOne(database, second_base_profile.actor_id);
		ExpectEqual(loaded_base_status.actor_id, second_base_profile.actor_id, "base status insert should persist the requested actor_id");
		ExpectOptionalEqual(loaded_base_status.zone_id, std::optional<uint32_t>{}, "base status find should preserve null zone_id");
		ExpectOptionalEqual(loaded_base_status.instance_id, std::optional<uint32_t>{}, "base status find should preserve null instance_id");
		ExpectOptionalEqual(loaded_base_status.entity_id, std::optional<uint32_t>{}, "base status find should preserve null entity_id");
		ExpectOptionalEqual(loaded_base_status.status_json, std::optional<std::string>{}, "base status find should preserve null status_json");
		ExpectOptionalEqual(loaded_base_status.heartbeat_at, std::optional<std::time_t>{}, "base status find should preserve null heartbeat_at");

		std::cout << "[PASS] actor-persistence\n";
	}
	catch (const TestFailure &e) {
		std::cerr << "[FAIL] " << e.what() << "\n";
		std::exit(1);
	}
}

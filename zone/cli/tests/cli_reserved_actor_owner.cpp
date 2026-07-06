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

#include "common/actor_reserved_owners.h"
#include "common/eqemu_logsys.h"
#include "common/repositories/actor_profiles_repository.h"
#include "zone/bot.h"
#include "zone/client.h"
#include "zone/harness/owned_bot_actor_fixture.h"
#include "zone/zone.h"

#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

#include "fmt/format.h"

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

class ReservedActorOwnerCleanup {
public:
	uint32_t actor_id = 0;
	uint32_t bot_id = 0;
	uint32_t reserved_owner_character_id = 0;
	uint32_t reprovisioned_reserved_owner_character_id = 0;
	uint32_t soft_deleted_reserved_owner_character_id = 0;
	uint32_t soft_deleted_collision_character_id = 0;
	uint32_t reprovisioned_collision_character_id = 0;
	uint32_t colliding_character_id = 0;
	uint32_t playable_marker_character_id = 0;
	uint32_t stale_actor_id = 0;
	std::vector<uint32_t> exhausted_soft_deleted_collision_character_ids;

	~ReservedActorOwnerCleanup()
	{
		if (stale_actor_id > 0) {
			ActorProfilesRepository::DeleteOne(database, stale_actor_id);
		}

		if (actor_id > 0) {
			ActorProfilesRepository::DeleteOne(database, actor_id);
		}

		if (bot_id > 0) {
			database.botdb.DeleteItems(bot_id);
			database.botdb.DeleteTimers(bot_id);
			database.botdb.DeleteBuffs(bot_id);
			database.botdb.DeleteStance(bot_id);
			database.botdb.DeleteBotSettings(bot_id);
			database.botdb.DeleteBotBlockedBuffs(bot_id);
			database.botdb.DeletePetItems(bot_id);
			database.botdb.DeletePetBuffs(bot_id);
			database.botdb.DeletePetStats(bot_id);
			database.botdb.DeleteBot(bot_id);
		}

		if (reserved_owner_character_id > 0) {
			std::string unused_reason;
			EQ::Actor::ReservedOwners::Rollback(database, reserved_owner_character_id, &unused_reason);
		}

		if (reprovisioned_reserved_owner_character_id > 0) {
			std::string unused_reason;
			EQ::Actor::ReservedOwners::Rollback(database, reprovisioned_reserved_owner_character_id, &unused_reason);
		}

		if (reprovisioned_collision_character_id > 0) {
			std::string unused_reason;
			EQ::Actor::ReservedOwners::Rollback(database, reprovisioned_collision_character_id, &unused_reason);
		}

		if (soft_deleted_reserved_owner_character_id > 0) {
			CharacterDataRepository::DeleteOne(database, soft_deleted_reserved_owner_character_id);
		}

		if (soft_deleted_collision_character_id > 0) {
			CharacterDataRepository::DeleteOne(database, soft_deleted_collision_character_id);
		}

		if (colliding_character_id > 0) {
			CharacterDataRepository::DeleteOne(database, colliding_character_id);
		}

		if (playable_marker_character_id > 0) {
			CharacterDataRepository::DeleteOne(database, playable_marker_character_id);
		}

		for (const auto character_id : exhausted_soft_deleted_collision_character_ids) {
			if (character_id > 0) {
				CharacterDataRepository::DeleteOne(database, character_id);
			}
		}
	}
};

} // namespace

void ZoneCLI::TestReservedActorOwner(int argc, char **argv, argh::parser &cmd, std::string &description)
{
	description = "Validates reserved owner-character provisioning plus bot load/spawn/save through the owned-bot path";

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

		ReservedActorOwnerCleanup cleanup;

		const auto colliding_name = fmt::format("ActorownerCollision{}", run_nonce);
		auto colliding_record = CharacterDataRepository::NewEntity();
		colliding_record.name = colliding_name;
		colliding_record = CharacterDataRepository::InsertOne(database, colliding_record);
		Expect(colliding_record.id > 0, "prefix-only collision row should insert for reserved-owner proof");
		cleanup.colliding_character_id = colliding_record.id;
		Expect(
			!EQ::Actor::ReservedOwners::FindByCharacterId(database, colliding_record.id).has_value(),
			"prefix-only collision row should not resolve as a reserved owner without provisioning marker and actor profile"
		);
		Expect(
			!EQ::Actor::ReservedOwners::Provision(database, colliding_name).character_id,
			"provisioning should refuse to adopt an existing Actorowner* row without the reserved-owner marker"
		);
		std::string collision_rollback_failure;
		Expect(
			!EQ::Actor::ReservedOwners::Rollback(database, colliding_record.id, &collision_rollback_failure),
			"rollback should refuse a prefix-only collision row"
		);
		ExpectEqual(collision_rollback_failure, std::string("reserved_owner_not_found"), "prefix-only collision row should fail rollback as not a reserved owner");

		const auto playable_marker_name = fmt::format("ActorownerPlayable{}", run_nonce);
		auto playable_marker_record = CharacterDataRepository::NewEntity();
		playable_marker_record.name = playable_marker_name;
		playable_marker_record.last_name = std::string(EQ::Actor::ReservedOwners::kReservedOwnerLastNameMarker);
		playable_marker_record.level = 60;
		playable_marker_record.race = 1;
		playable_marker_record.class_ = 1;
		playable_marker_record = CharacterDataRepository::InsertOne(database, playable_marker_record);
		Expect(playable_marker_record.id > 0, "marker-bearing playable collision row should insert for reserved-owner proof");
		cleanup.playable_marker_character_id = playable_marker_record.id;
		Expect(
			!EQ::Actor::ReservedOwners::FindProvisionedByCharacterId(database, playable_marker_record.id).has_value(),
			"marker-bearing playable collision row should not resolve as a provisioned reserved owner"
		);
		Expect(
			!EQ::Actor::ReservedOwners::Provision(database, playable_marker_name).character_id,
			"provisioning should refuse to adopt a marker-bearing playable Actorowner* row"
		);

		const auto soft_deleted_collision_name = fmt::format("ActorownerSoftDeletedCollision{}", run_nonce);
		auto soft_deleted_collision_record = CharacterDataRepository::NewEntity();
		soft_deleted_collision_record.name = soft_deleted_collision_name;
		soft_deleted_collision_record = CharacterDataRepository::InsertOne(database, soft_deleted_collision_record);
		Expect(soft_deleted_collision_record.id > 0, "soft-deleted collision row should insert for reprovision proof");
		soft_deleted_collision_record.deleted_at = static_cast<time_t>(run_nonce);
		ExpectEqual(
			CharacterDataRepository::UpdateOne(database, soft_deleted_collision_record),
			int(1),
			"soft-deleted collision row should update deleted_at for reprovision proof"
		);
		cleanup.soft_deleted_collision_character_id = soft_deleted_collision_record.id;
		const auto reprovisioned_collision = EQ::Actor::ReservedOwners::Provision(database, soft_deleted_collision_name);
		Expect(
			reprovisioned_collision.character_id > 0,
			"soft-deleted Actorowner* collision row should not block reprovisioning"
		);
		Expect(
			reprovisioned_collision.character_id != soft_deleted_collision_record.id,
			"soft-deleted Actorowner* collision row should not be adopted as an active reserved owner"
		);
		Expect(
			reprovisioned_collision.name != soft_deleted_collision_name,
			"soft-deleted Actorowner* collision row should force a new unique reserved owner name"
		);
		Expect(
			EQ::Actor::ReservedOwners::IsReservedOwnerName(reprovisioned_collision.name),
			"soft-deleted Actorowner* collision reprovisioning should keep the reserved owner prefix"
		);
		cleanup.reprovisioned_collision_character_id = reprovisioned_collision.character_id;

		const auto soft_deleted_reserved_name = fmt::format("ActorownerSoftDeletedReserved{}", run_nonce);
		auto soft_deleted_reserved_owner = EQ::Actor::ReservedOwners::Provision(database, soft_deleted_reserved_name);
		Expect(
			soft_deleted_reserved_owner.character_id > 0,
			"soft-deleted reserved owner seed should provision before reprovision proof"
		);
		auto soft_deleted_reserved_record = CharacterDataRepository::FindOne(database, soft_deleted_reserved_owner.character_id);
		soft_deleted_reserved_record.deleted_at = static_cast<time_t>(run_nonce);
		ExpectEqual(
			CharacterDataRepository::UpdateOne(database, soft_deleted_reserved_record),
			int(1),
			"soft-deleted reserved owner seed should update deleted_at for reprovision proof"
		);
		cleanup.soft_deleted_reserved_owner_character_id = soft_deleted_reserved_owner.character_id;
		const auto reprovisioned_reserved_owner = EQ::Actor::ReservedOwners::Provision(database, soft_deleted_reserved_name);
		Expect(
			reprovisioned_reserved_owner.character_id > 0,
			"soft-deleted reserved owner should not block reprovisioning"
		);
		Expect(
			reprovisioned_reserved_owner.character_id != soft_deleted_reserved_owner.character_id,
			"soft-deleted reserved owner should not be adopted as an active owner"
		);
		Expect(
			reprovisioned_reserved_owner.name != soft_deleted_reserved_name,
			"soft-deleted reserved owner should force a new unique reserved owner name"
		);
		Expect(
			EQ::Actor::ReservedOwners::IsReservedOwnerName(reprovisioned_reserved_owner.name),
			"soft-deleted reserved owner reprovisioning should keep the reserved owner prefix"
		);
		const auto found_reprovisioned_reserved_owner =
			EQ::Actor::ReservedOwners::FindProvisionedByName(database, reprovisioned_reserved_owner.name);
		Expect(
			found_reprovisioned_reserved_owner.has_value(),
			"reserved owner lookup should resolve the reprovisioned active owner by its final provisioned name"
		);
		ExpectEqual(
			found_reprovisioned_reserved_owner->character_id,
			reprovisioned_reserved_owner.character_id,
			"reserved owner lookup should resolve the reprovisioned active reserved owner"
		);
		cleanup.reprovisioned_reserved_owner_character_id = reprovisioned_reserved_owner.character_id;

		const auto exhausted_soft_deleted_name = fmt::format("ActorownerExhausted{}", run_nonce);
		auto exhausted_soft_deleted_record = CharacterDataRepository::NewEntity();
		exhausted_soft_deleted_record.name = exhausted_soft_deleted_name;
		exhausted_soft_deleted_record = CharacterDataRepository::InsertOne(database, exhausted_soft_deleted_record);
		Expect(exhausted_soft_deleted_record.id > 0, "soft-deleted exhaustion seed should insert for reprovision proof");
		exhausted_soft_deleted_record.deleted_at = static_cast<time_t>(run_nonce);
		ExpectEqual(
			CharacterDataRepository::UpdateOne(database, exhausted_soft_deleted_record),
			int(1),
			"soft-deleted exhaustion seed should update deleted_at for reprovision proof"
		);
		cleanup.exhausted_soft_deleted_collision_character_ids.push_back(exhausted_soft_deleted_record.id);
		for (uint32_t suffix = 1; suffix <= 32; ++suffix) {
			auto exhausted_collision_record = CharacterDataRepository::NewEntity();
			exhausted_collision_record.name = fmt::format("{}R{}", exhausted_soft_deleted_name, suffix);
			exhausted_collision_record = CharacterDataRepository::InsertOne(database, exhausted_collision_record);
			Expect(
				exhausted_collision_record.id > 0,
				fmt::format("soft-deleted exhaustion collision row R{} should insert for reprovision proof", suffix)
			);
			cleanup.exhausted_soft_deleted_collision_character_ids.push_back(exhausted_collision_record.id);
		}
		const auto reprovisioned_exhausted_reserved_owner =
			EQ::Actor::ReservedOwners::Provision(database, exhausted_soft_deleted_name);
		Expect(
			reprovisioned_exhausted_reserved_owner.character_id > 0,
			"soft-deleted reserved owner with 32 occupied fallback names should still reprovision"
		);
		ExpectEqual(
			reprovisioned_exhausted_reserved_owner.name,
			fmt::format("{}R33", exhausted_soft_deleted_name),
			"soft-deleted reserved owner should deterministically continue past the first 32 fallback collisions"
		);
		cleanup.exhausted_soft_deleted_collision_character_ids.push_back(reprovisioned_exhausted_reserved_owner.character_id);

		const auto reserved_owner_name = fmt::format("Actorowner{}", run_nonce);
		const auto reserved_owner = EQ::Actor::ReservedOwners::Provision(database, reserved_owner_name);
		Expect(reserved_owner.character_id > 0, "reserved owner provisioning should create a character_data row");
		ExpectEqual(reserved_owner.name, reserved_owner_name, "reserved owner provisioning should preserve reserved owner name");
		const auto reserved_owner_record = CharacterDataRepository::FindOne(database, reserved_owner.character_id);
		ExpectEqual(
			reserved_owner_record.last_name,
			std::string(EQ::Actor::ReservedOwners::kReservedOwnerLastNameMarker),
			"reserved owner provisioning should stamp the non-secret reserved-owner marker"
		);
		ExpectEqual(reserved_owner_record.level, uint32_t(0), "reserved owner provisioning should keep the row non-playable");
		ExpectEqual(reserved_owner_record.class_, uint8_t(0), "reserved owner provisioning should keep the row classless");
		ExpectEqual(reserved_owner_record.race, uint16_t(0), "reserved owner provisioning should keep the row raceless");
		cleanup.reserved_owner_character_id = reserved_owner.character_id;

		auto stale_profile = ActorProfilesRepository::NewEntity();
		stale_profile.actor_type = "autonomous_actor";
		stale_profile.actor_substrate = "synthetic_owner";
		stale_profile.bot_id = std::nullopt;
		stale_profile.owner_character_id = reserved_owner.character_id;
		stale_profile.enabled = 1;
		stale_profile.created_at = static_cast<time_t>(run_nonce);
		stale_profile.updated_at = static_cast<time_t>(run_nonce);
		stale_profile = ActorProfilesRepository::InsertOne(database, stale_profile);
		Expect(stale_profile.actor_id > 0, "stale non-bot actor profile association should insert for reserved-owner proof");
		cleanup.stale_actor_id = stale_profile.actor_id;
		Expect(
			!EQ::Actor::ReservedOwners::FindByCharacterId(database, reserved_owner.character_id).has_value(),
			"reserved owner lookup should ignore a non-bot actor profile association"
		);

		EQ::ZoneHarness::OwnedBotActorFixture fixture;
		Expect(
			fixture.SetUpOwnedBotSolo({
				.owner_name = reserved_owner.name,
				.owner_character_id = reserved_owner.character_id,
				.bot_name = fmt::format("HarnessReservedBot{}", run_nonce),
			}),
			"owned bot fixture should create a harness-only synthetic owner bound to the reserved owner id"
		);
		Expect(fixture.Owner() != nullptr, "reserved owner fixture should create a synthetic owner client");
		Expect(fixture.OwnedBot() != nullptr, "reserved owner fixture should create an owned bot");
		ExpectEqual(fixture.Owner()->CharacterID(), reserved_owner.character_id, "synthetic owner should carry reserved owner character id");
		ExpectEqual(fixture.OwnedBot()->GetBotOwnerCharacterID(), reserved_owner.character_id, "new bot should keep reserved owner character id");
		Expect(fixture.OwnedBot()->GetBotOwner() == fixture.Owner(), "new bot should keep normal client owner pointer invariant");
		Expect(fixture.OwnedBot()->Save(), "new owned bot should save through reserved owner path");
		Expect(fixture.OwnedBot()->GetBotID() > 0, "reserved owner save should allocate a bot_id");

		cleanup.bot_id = fixture.OwnedBot()->GetBotID();

		ActorProfilesRepository::ActorProfileRecord profile{};
		profile.actor_type = "autonomous_actor";
		profile.actor_substrate = "bot";
		profile.bot_id = cleanup.bot_id;
		profile.owner_character_id = reserved_owner.character_id;
		profile.enabled = true;
		const auto stored_profile = ActorProfilesRepository::UpsertBotBackedProfile(database, profile);
		Expect(stored_profile.actor_id > 0, "actor profile upsert should allocate an actor_id");
		ExpectEqual(stored_profile.owner_character_id, std::optional<uint32_t>(reserved_owner.character_id), "actor profile should keep reserved owner character id");
		cleanup.actor_id = stored_profile.actor_id;
		ActorProfilesRepository::DeleteOne(database, cleanup.stale_actor_id);
		cleanup.stale_actor_id = 0;

		fixture.Reset();
		Expect(entity_list.GetClientByCharID(reserved_owner.character_id) == nullptr, "fixture reset should remove harness-only synthetic owner client");

		auto *loaded_bot = Bot::LoadBot(cleanup.bot_id);
		Expect(loaded_bot != nullptr, "saved reserved-owner bot should load from bot_data");
		ExpectEqual(loaded_bot->GetBotOwnerCharacterID(), reserved_owner.character_id, "loaded bot should preserve reserved owner character id");
		Expect(loaded_bot->GetBotOwner() == nullptr, "loaded bot should stay owner-pointer null until a matching owner client exists");

		auto *spawn_owner = new Client();
		spawn_owner->TempName(reserved_owner.name.c_str());
		spawn_owner->SetCharacterId(reserved_owner.character_id);
		spawn_owner->Mob::SetLevel(60);
		spawn_owner->SetHP(10000);
		spawn_owner->SetMana(10000);
		spawn_owner->GMMove(0.0f, 0.0f, 0.0f, 0.0f);
		entity_list.AddClient(spawn_owner);

		Expect(loaded_bot->Spawn(spawn_owner), "loaded reserved-owner bot should spawn through the normal owner-checked path");
		Expect(loaded_bot->GetBotOwner() == spawn_owner, "spawn should restore the normal bot owner pointer invariant");
		Expect(loaded_bot->HasOwner(), "spawned reserved-owner bot should report a normal owner");
		ExpectEqual(loaded_bot->GetBotOwnerCharacterID(), reserved_owner.character_id, "spawn should preserve reserved owner character id");
		Expect(loaded_bot->Save(), "spawned reserved-owner bot should update-save through the normal path");

		const auto persisted_profile = ActorProfilesRepository::FindByBotId(database, cleanup.bot_id);
		Expect(persisted_profile.has_value(), "bot-backed actor profile should remain queryable by bot_id");
		ExpectEqual(persisted_profile->owner_character_id, std::optional<uint32_t>(reserved_owner.character_id), "persisted actor profile should still point at reserved owner character id");
		Expect(
			EQ::Actor::ReservedOwners::FindByCharacterId(database, reserved_owner.character_id).has_value(),
			"reserved owner lookup should resolve the provisioned owner record without secrets"
		);

		Expect(loaded_bot->DeleteBot(), "reserved-owner bot cleanup should delete the bot_data row");
		cleanup.bot_id = 0;
		entity_list.RemoveMob(loaded_bot->GetID());
		entity_list.RemoveMob(spawn_owner->GetID());

		ActorProfilesRepository::DeleteOne(database, cleanup.actor_id);
		cleanup.actor_id = 0;

		std::string rollback_failure;
		Expect(
			EQ::Actor::ReservedOwners::Rollback(database, cleanup.reserved_owner_character_id, &rollback_failure),
			"reserved owner rollback should succeed after actor profile and bot cleanup: " + rollback_failure
		);
		cleanup.reserved_owner_character_id = 0;

		std::cout << "[PASS] reserved-actor-owner\n";
	}
	catch (const TestFailure &e) {
		std::cerr << "[FAIL] " << e.what() << "\n";
		std::exit(1);
	}
}

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

	~ReservedActorOwnerCleanup()
	{
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

		const auto reserved_owner_name = fmt::format("Actorowner{}", run_nonce);
		const auto reserved_owner = EQ::Actor::ReservedOwners::Provision(database, reserved_owner_name);
		Expect(reserved_owner.character_id > 0, "reserved owner provisioning should create a character_data row");
		ExpectEqual(reserved_owner.name, reserved_owner_name, "reserved owner provisioning should preserve reserved owner name");

		ReservedActorOwnerCleanup cleanup;
		cleanup.reserved_owner_character_id = reserved_owner.character_id;

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

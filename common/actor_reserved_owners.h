#pragma once

#include "common/repositories/actor_profiles_repository.h"
#include "common/repositories/bot_data_repository.h"
#include "common/repositories/character_data_repository.h"

#include "fmt/format.h"

#include <optional>
#include <string>
#include <string_view>

namespace EQ::Actor::ReservedOwners {

inline constexpr std::string_view kReservedOwnerNamePrefix = "Actorowner";

struct ReservedOwnerRecord {
	uint32_t character_id = 0;
	int32_t account_id = 0;
	std::string name;
	bool created = false;
};

inline bool IsReservedOwnerName(std::string_view name)
{
	return name.size() > kReservedOwnerNamePrefix.size() &&
		name.substr(0, kReservedOwnerNamePrefix.size()) == kReservedOwnerNamePrefix;
}

inline std::optional<ReservedOwnerRecord> FindByCharacterId(Database &db, uint32_t character_id)
{
	if (!character_id) {
		return std::nullopt;
	}

	const auto record = CharacterDataRepository::FindOne(db, character_id);
	if (!record.id || !IsReservedOwnerName(record.name)) {
		return std::nullopt;
	}

	return ReservedOwnerRecord{
		.character_id = record.id,
		.account_id = record.account_id,
		.name = record.name,
		.created = false,
	};
}

inline std::optional<ReservedOwnerRecord> FindByName(Database &db, const std::string &name)
{
	const auto record = CharacterDataRepository::FindByName(db, name);
	if (!record.id || !IsReservedOwnerName(record.name)) {
		return std::nullopt;
	}

	return ReservedOwnerRecord{
		.character_id = record.id,
		.account_id = record.account_id,
		.name = record.name,
		.created = false,
	};
}

inline ReservedOwnerRecord Provision(Database &db, const std::string &name, int32_t account_id = 0)
{
	if (!IsReservedOwnerName(name)) {
		return {};
	}

	if (const auto existing = FindByName(db, name)) {
		return *existing;
	}

	auto record = CharacterDataRepository::NewEntity();
	record.account_id = account_id;
	record.name = name;
	record = CharacterDataRepository::InsertOne(db, record);
	if (!record.id) {
		return {};
	}

	return ReservedOwnerRecord{
		.character_id = record.id,
		.account_id = record.account_id,
		.name = record.name,
		.created = true,
	};
}

inline bool Rollback(Database &db, uint32_t character_id, std::string *failure_reason = nullptr)
{
	const auto reserved_owner = FindByCharacterId(db, character_id);
	if (!reserved_owner.has_value()) {
		if (failure_reason) {
			*failure_reason = "reserved_owner_not_found";
		}
		return false;
	}

	const auto linked_actor_profiles = ActorProfilesRepository::Count(
		db,
		fmt::format("owner_character_id = {}", character_id)
	);
	if (linked_actor_profiles > 0) {
		if (failure_reason) {
			*failure_reason = "reserved_owner_still_referenced_by_actor_profiles";
		}
		return false;
	}

	const auto linked_bots = BotDataRepository::Count(
		db,
		fmt::format("owner_id = {}", character_id)
	);
	if (linked_bots > 0) {
		if (failure_reason) {
			*failure_reason = "reserved_owner_still_referenced_by_bot_data";
		}
		return false;
	}

	if (CharacterDataRepository::DeleteOne(db, character_id) != 1) {
		if (failure_reason) {
			*failure_reason = "reserved_owner_delete_failed";
		}
		return false;
	}

	return true;
}

} // namespace EQ::Actor::ReservedOwners

#pragma once

#include "common/repositories/bot_data_repository.h"
#include "common/repositories/character_data_repository.h"

#include "fmt/format.h"

#include <cstdlib>
#include <optional>
#include <string>
#include <string_view>

namespace EQ::Actor::ReservedOwners {

inline constexpr std::string_view kReservedOwnerNamePrefix = "Actorowner";
inline constexpr std::string_view kReservedOwnerLastNameMarker = "ReservedActorOwner";

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

inline bool HasBotBackedActorProfileAssociation(Database &db, uint32_t character_id)
{
	if (!character_id) {
		return false;
	}

	auto results = db.QueryDatabase(
		fmt::format(
			"SELECT COUNT(*) FROM actor_profiles WHERE owner_character_id = {} AND actor_substrate = 'bot' AND bot_id IS NOT NULL",
			character_id
		)
	);
	if (!results.Success() || results.RowCount() != 1) {
		return false;
	}

	auto row = results.begin();
	return row[0] && strtoull(row[0], nullptr, 10) > 0;
}

inline bool IsProvisionedReservedOwnerRecord(const CharacterDataRepository::CharacterData &record)
{
	return record.id > 0 &&
		IsReservedOwnerName(record.name) &&
		record.last_name == kReservedOwnerLastNameMarker &&
		record.level == 0 &&
		record.class_ == 0 &&
		record.race == 0 &&
		record.deleted_at <= 0;
}

inline ReservedOwnerRecord ToReservedOwnerRecord(const CharacterDataRepository::CharacterData &record, bool created = false)
{
	return ReservedOwnerRecord{
		.character_id = record.id,
		.account_id = record.account_id,
		.name = record.name,
		.created = created,
	};
}

inline std::optional<ReservedOwnerRecord> FindProvisionedByCharacterId(Database &db, uint32_t character_id)
{
	if (!character_id) {
		return std::nullopt;
	}

	const auto record = CharacterDataRepository::FindOne(db, character_id);
	if (!IsProvisionedReservedOwnerRecord(record)) {
		return std::nullopt;
	}

	return ToReservedOwnerRecord(record);
}

inline std::optional<ReservedOwnerRecord> FindProvisionedByName(Database &db, const std::string &name)
{
	const auto record = CharacterDataRepository::FindByName(db, name);
	if (!IsProvisionedReservedOwnerRecord(record)) {
		return std::nullopt;
	}

	return ToReservedOwnerRecord(record);
}

inline std::optional<ReservedOwnerRecord> FindByCharacterId(Database &db, uint32_t character_id)
{
	const auto reserved_owner = FindProvisionedByCharacterId(db, character_id);
	if (!reserved_owner.has_value() || !HasBotBackedActorProfileAssociation(db, reserved_owner->character_id)) {
		return std::nullopt;
	}

	return reserved_owner;
}

inline std::optional<ReservedOwnerRecord> FindByName(Database &db, const std::string &name)
{
	const auto reserved_owner = FindProvisionedByName(db, name);
	if (!reserved_owner.has_value() || !HasBotBackedActorProfileAssociation(db, reserved_owner->character_id)) {
		return std::nullopt;
	}

	return reserved_owner;
}

inline ReservedOwnerRecord Provision(Database &db, const std::string &name, int32_t account_id = 0)
{
	if (!IsReservedOwnerName(name)) {
		return {};
	}

	const auto existing = CharacterDataRepository::FindByName(db, name);
	if (existing.id) {
		return IsProvisionedReservedOwnerRecord(existing) ? ToReservedOwnerRecord(existing) : ReservedOwnerRecord{};
	}

	auto record = CharacterDataRepository::NewEntity();
	record.account_id = account_id;
	record.name = name;
	record.last_name = std::string(kReservedOwnerLastNameMarker);
	record = CharacterDataRepository::InsertOne(db, record);
	if (!record.id) {
		return {};
	}

	return ToReservedOwnerRecord(record, true);
}

inline bool Rollback(Database &db, uint32_t character_id, std::string *failure_reason = nullptr)
{
	const auto reserved_owner = CharacterDataRepository::FindOne(db, character_id);
	if (!IsProvisionedReservedOwnerRecord(reserved_owner)) {
		if (failure_reason) {
			*failure_reason = "reserved_owner_not_found";
		}
		return false;
	}

	auto linked_actor_profiles = db.QueryDatabase(
		fmt::format("SELECT COUNT(*) FROM actor_profiles WHERE owner_character_id = {}", character_id)
	);
	if (
		linked_actor_profiles.Success() &&
		linked_actor_profiles.RowCount() == 1 &&
		linked_actor_profiles.begin()[0] &&
		strtoull(linked_actor_profiles.begin()[0], nullptr, 10) > 0
	) {
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

#pragma once

#include "common/repositories/base/base_actor_profiles_repository.h"

#include "common/database.h"
#include "common/strings.h"

#include <ctime>
#include <optional>
#include <utility>

class ActorProfilesRepository: public BaseActorProfilesRepository {
public:
	struct ActorProfileRecord {
		uint32_t                actor_id = 0;
		std::string             actor_type;
		std::string             actor_substrate;
		std::optional<uint32_t> bot_id;
		std::optional<uint32_t> owner_character_id;
		bool                    enabled = true;
		time_t                  created_at = 0;
		time_t                  updated_at = 0;
	};

    /**
     * This file was auto generated and can be modified and extended upon
     *
     * Base repository methods are automatically
     * generated in the "base" version of this repository. The base repository
     * is immutable and to be left untouched, while methods in this class
     * are used as extension methods for more specific persistence-layer
     * accessors or mutators.
     *
     * Base Methods (Subject to be expanded upon in time)
     *
     * Note: Not all tables are designed appropriately to fit functionality with all base methods
     *
     * InsertOne
     * UpdateOne
     * DeleteOne
     * FindOne
     * GetWhere(std::string where_filter)
     * DeleteWhere(std::string where_filter)
     * InsertMany
     * All
     *
     * Example custom methods in a repository
     *
     * ActorProfilesRepository::GetByZoneAndVersion(int zone_id, int zone_version)
     * ActorProfilesRepository::GetWhereNeverExpires()
     * ActorProfilesRepository::GetWhereXAndY()
     * ActorProfilesRepository::DeleteWhereXAndY()
     *
     * Most of the above could be covered by base methods, but if you as a developer
     * find yourself re-using logic for other parts of the code, its best to just make a
	 * method that can be re-used easily elsewhere especially if it can use a base repository
	 * method and encapsulate filters there
	 */

	static std::optional<ActorProfileRecord> FindByActorId(Database &db, uint32_t actor_id)
	{
		auto results = db.QueryDatabase(
			fmt::format(
				"{} WHERE {} = {} LIMIT 1",
				BaseSelect(),
				PrimaryKey(),
				actor_id
			)
		);

		if (!results.Success() || results.RowCount() != 1) {
			return std::nullopt;
		}

		return FromRow(results.begin());
	}

	static std::optional<ActorProfileRecord> FindByBotId(Database &db, uint32_t bot_id)
	{
		auto results = db.QueryDatabase(
			fmt::format(
				"{} WHERE bot_id = {} LIMIT 1",
				BaseSelect(),
				bot_id
			)
		);

		if (!results.Success() || results.RowCount() != 1) {
			return std::nullopt;
		}

		return FromRow(results.begin());
	}

	static ActorProfileRecord UpsertBotBackedProfile(Database &db, ActorProfileRecord record)
	{
		if (!record.bot_id.has_value()) {
			return {};
		}

		record.created_at = record.created_at > 0 ? record.created_at : std::time(nullptr);
		record.updated_at = record.updated_at > 0 ? record.updated_at : record.created_at;

		auto results = db.QueryDatabase(
			fmt::format(
				R"SQL(
INSERT INTO {} (
	actor_id,
	actor_type,
	actor_substrate,
	bot_id,
	owner_character_id,
	enabled,
	created_at,
	updated_at
) VALUES (
	{},
	'{}',
	'{}',
	{},
	{},
	{},
	FROM_UNIXTIME({}),
	FROM_UNIXTIME({})
)
ON DUPLICATE KEY UPDATE
	actor_type = VALUES(actor_type),
	actor_substrate = VALUES(actor_substrate),
	owner_character_id = VALUES(owner_character_id),
	enabled = VALUES(enabled),
	updated_at = VALUES(updated_at)
)SQL",
				TableName(),
				record.actor_id > 0 ? std::to_string(record.actor_id) : "NULL",
				Strings::Escape(record.actor_type),
				Strings::Escape(record.actor_substrate),
				*record.bot_id,
				NullableUintSql(record.owner_character_id),
				record.enabled ? 1 : 0,
				record.created_at,
				record.updated_at
			)
		);

		if (!results.Success()) {
			return {};
		}

		auto stored = FindByBotId(db, *record.bot_id);
		return stored.value_or(ActorProfileRecord{});
	}

private:
	template <typename RowType>
	static ActorProfileRecord FromRow(RowType &row)
	{
		return {
			.actor_id = row[0] ? static_cast<uint32_t>(strtoul(row[0], nullptr, 10)) : 0,
			.actor_type = row[1] ? row[1] : "",
			.actor_substrate = row[2] ? row[2] : "",
			.bot_id = row[3] ? std::optional<uint32_t>(static_cast<uint32_t>(strtoul(row[3], nullptr, 10))) : std::nullopt,
			.owner_character_id = row[4] ? std::optional<uint32_t>(static_cast<uint32_t>(strtoul(row[4], nullptr, 10))) : std::nullopt,
			.enabled = row[5] ? Strings::ToInt(row[5]) != 0 : true,
			.created_at = strtoll(row[6] ? row[6] : "0", nullptr, 10),
			.updated_at = strtoll(row[7] ? row[7] : "0", nullptr, 10),
		};
	}

	static std::string NullableUintSql(const std::optional<uint32_t> &value)
	{
		return value.has_value() ? std::to_string(*value) : "NULL";
	}
};

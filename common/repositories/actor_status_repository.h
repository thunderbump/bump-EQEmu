#pragma once

#include "common/repositories/base/base_actor_status_repository.h"

#include "common/database.h"
#include "common/strings.h"

#include <ctime>
#include <optional>
#include <utility>

class ActorStatusRepository: public BaseActorStatusRepository {
public:
	struct ActorStatusRecord {
		uint32_t                       actor_id = 0;
		std::optional<uint32_t>        zone_id;
		std::optional<uint32_t>        instance_id;
		std::optional<uint32_t>        entity_id;
		std::string                    state;
		std::optional<std::string>     status_json;
		std::optional<std::time_t>     heartbeat_at;
		time_t                         updated_at = 0;
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
     * ActorStatusRepository::GetByZoneAndVersion(int zone_id, int zone_version)
     * ActorStatusRepository::GetWhereNeverExpires()
     * ActorStatusRepository::GetWhereXAndY()
     * ActorStatusRepository::DeleteWhereXAndY()
     *
     * Most of the above could be covered by base methods, but if you as a developer
     * find yourself re-using logic for other parts of the code, its best to just make a
	 * method that can be re-used easily elsewhere especially if it can use a base repository
	 * method and encapsulate filters there
	 */

	static std::optional<ActorStatusRecord> FindByActorId(Database &db, uint32_t actor_id)
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

	static ActorStatusRecord UpsertOne(Database &db, ActorStatusRecord record)
	{
		if (!record.actor_id) {
			return {};
		}

		record.updated_at = record.updated_at > 0 ? record.updated_at : std::time(nullptr);

		auto results = db.QueryDatabase(
			fmt::format(
				R"SQL(
INSERT INTO {} (
	actor_id,
	zone_id,
	instance_id,
	entity_id,
	state,
	status_json,
	heartbeat_at,
	updated_at
) VALUES (
	{},
	{},
	{},
	{},
	'{}',
	{},
	{},
	FROM_UNIXTIME({})
)
ON DUPLICATE KEY UPDATE
	zone_id = VALUES(zone_id),
	instance_id = VALUES(instance_id),
	entity_id = VALUES(entity_id),
	state = VALUES(state),
	status_json = VALUES(status_json),
	heartbeat_at = VALUES(heartbeat_at),
	updated_at = VALUES(updated_at)
)SQL",
				TableName(),
				record.actor_id,
				NullableUintSql(record.zone_id),
				NullableUintSql(record.instance_id),
				NullableUintSql(record.entity_id),
				Strings::Escape(record.state),
				NullableStringSql(record.status_json),
				NullableTimeSql(record.heartbeat_at),
				record.updated_at
			)
		);

		if (!results.Success()) {
			return {};
		}

		auto stored = FindByActorId(db, record.actor_id);
		return stored.value_or(ActorStatusRecord{});
	}

private:
	template <typename RowType>
	static ActorStatusRecord FromRow(RowType &row)
	{
		return {
			.actor_id = row[0] ? static_cast<uint32_t>(strtoul(row[0], nullptr, 10)) : 0,
			.zone_id = row[1] ? std::optional<uint32_t>(static_cast<uint32_t>(strtoul(row[1], nullptr, 10))) : std::nullopt,
			.instance_id = row[2] ? std::optional<uint32_t>(static_cast<uint32_t>(strtoul(row[2], nullptr, 10))) : std::nullopt,
			.entity_id = row[3] ? std::optional<uint32_t>(static_cast<uint32_t>(strtoul(row[3], nullptr, 10))) : std::nullopt,
			.state = row[4] ? row[4] : "",
			.status_json = row[5] ? std::optional<std::string>(row[5]) : std::nullopt,
			.heartbeat_at = row[6] ? std::optional<std::time_t>(strtoll(row[6], nullptr, 10)) : std::nullopt,
			.updated_at = strtoll(row[7] ? row[7] : "0", nullptr, 10),
		};
	}

	static std::string NullableUintSql(const std::optional<uint32_t> &value)
	{
		return value.has_value() ? std::to_string(*value) : "NULL";
	}

	static std::string NullableStringSql(const std::optional<std::string> &value)
	{
		return value.has_value() ? "'" + Strings::Escape(*value) + "'" : "NULL";
	}

	static std::string NullableTimeSql(const std::optional<std::time_t> &value)
	{
		return value.has_value() && *value > 0
			? "FROM_UNIXTIME(" + std::to_string(*value) + ")"
			: "NULL";
	}
};

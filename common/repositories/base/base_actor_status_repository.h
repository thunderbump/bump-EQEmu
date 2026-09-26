/**
 * DO NOT MODIFY THIS FILE
 *
 * This repository was automatically generated and is NOT to be modified directly.
 * Any repository modifications are meant to be made to the repository extending the base.
 * Any modifications to base repositories are to be made by the generator only
 *
 * @generator ./utils/scripts/generators/repository-generator.pl
 * @docs https://docs.eqemu.dev/developer/repositories
 */

#pragma once

#include "common/database.h"
#include "common/strings.h"

#include <optional>
#include <ctime>

class BaseActorStatusRepository {
public:
	struct ActorStatus {
		uint32_t                    actor_id;
		std::optional<uint32_t>     zone_id;
		std::optional<uint32_t>     instance_id;
		std::optional<uint32_t>     entity_id;
		std::string                 state;
		std::optional<std::string>  status_json;
		std::optional<time_t>       heartbeat_at;
		time_t                      updated_at;
	};

	static std::string PrimaryKey()
	{
		return std::string("actor_id");
	}

	static std::vector<std::string> Columns()
	{
		return {
			"actor_id",
			"zone_id",
			"instance_id",
			"entity_id",
			"state",
			"status_json",
			"heartbeat_at",
			"updated_at",
		};
	}

	static std::vector<std::string> SelectColumns()
	{
		return {
			"actor_id",
			"zone_id",
			"instance_id",
			"entity_id",
			"state",
			"status_json",
			"UNIX_TIMESTAMP(heartbeat_at)",
			"UNIX_TIMESTAMP(updated_at)",
		};
	}

	static std::string ColumnsRaw()
	{
		return std::string(Strings::Implode(", ", Columns()));
	}

	static std::string SelectColumnsRaw()
	{
		return std::string(Strings::Implode(", ", SelectColumns()));
	}

	static std::string TableName()
	{
		return std::string("actor_status");
	}

	static std::string BaseSelect()
	{
		return fmt::format(
			"SELECT {} FROM {}",
			SelectColumnsRaw(),
			TableName()
		);
	}

	static std::string BaseInsert()
	{
		return fmt::format(
			"INSERT INTO {} ({}) ",
			TableName(),
			ColumnsRaw()
		);
	}

	static ActorStatus NewEntity()
	{
		ActorStatus e{};

		e.actor_id     = 0;
		e.zone_id      = std::nullopt;
		e.instance_id  = std::nullopt;
		e.entity_id    = std::nullopt;
		e.state        = "";
		e.status_json  = std::nullopt;
		e.heartbeat_at = std::nullopt;
		e.updated_at   = 0;

		return e;
	}

	static ActorStatus GetActorStatus(
		const std::vector<ActorStatus> &actor_statuss,
		int actor_status_id
	)
	{
		for (auto &actor_status : actor_statuss) {
			if (actor_status.actor_id == actor_status_id) {
				return actor_status;
			}
		}

		return NewEntity();
	}

	static ActorStatus FindOne(
		Database& db,
		int actor_status_id
	)
	{
		auto results = db.QueryDatabase(
			fmt::format(
				"{} WHERE {} = {} LIMIT 1",
				BaseSelect(),
				PrimaryKey(),
				actor_status_id
			)
		);

		auto row = results.begin();
		if (results.RowCount() == 1) {
			ActorStatus e{};

			e.actor_id     = row[0] ? static_cast<uint32_t>(strtoul(row[0], nullptr, 10)) : 0;
			e.zone_id      = row[1] ? std::optional<uint32_t>(static_cast<uint32_t>(strtoul(row[1], nullptr, 10))) : std::nullopt;
			e.instance_id  = row[2] ? std::optional<uint32_t>(static_cast<uint32_t>(strtoul(row[2], nullptr, 10))) : std::nullopt;
			e.entity_id    = row[3] ? std::optional<uint32_t>(static_cast<uint32_t>(strtoul(row[3], nullptr, 10))) : std::nullopt;
			e.state        = row[4] ? row[4] : "";
			e.status_json  = row[5] ? std::optional<std::string>(row[5]) : std::nullopt;
			e.heartbeat_at = row[6] ? std::optional<time_t>(strtoll(row[6], nullptr, 10)) : std::nullopt;
			e.updated_at   = strtoll(row[7] ? row[7] : "-1", nullptr, 10);

			return e;
		}

		return NewEntity();
	}

	static int DeleteOne(
		Database& db,
		int actor_status_id
	)
	{
		auto results = db.QueryDatabase(
			fmt::format(
				"DELETE FROM {} WHERE {} = {}",
				TableName(),
				PrimaryKey(),
				actor_status_id
			)
		);

		return (results.Success() ? results.RowsAffected() : 0);
	}

	static int UpdateOne(
		Database& db,
		const ActorStatus &e
	)
	{
		std::vector<std::string> v;

		auto columns = Columns();

		v.push_back(columns[0] + " = " + std::to_string(e.actor_id));
		v.push_back(columns[1] + " = " + NullableUintSql(e.zone_id));
		v.push_back(columns[2] + " = " + NullableUintSql(e.instance_id));
		v.push_back(columns[3] + " = " + NullableUintSql(e.entity_id));
		v.push_back(columns[4] + " = '" + Strings::Escape(e.state) + "'");
		v.push_back(columns[5] + " = " + NullableStringSql(e.status_json));
		v.push_back(columns[6] + " = " + NullableTimeSql(e.heartbeat_at));
		v.push_back(columns[7] + " = FROM_UNIXTIME(" + (e.updated_at > 0 ? std::to_string(e.updated_at) : "null") + ")");

		auto results = db.QueryDatabase(
			fmt::format(
				"UPDATE {} SET {} WHERE {} = {}",
				TableName(),
				Strings::Implode(", ", v),
				PrimaryKey(),
				e.actor_id
			)
		);

		return (results.Success() ? results.RowsAffected() : 0);
	}

	static ActorStatus InsertOne(
		Database& db,
		ActorStatus e
	)
	{
		std::vector<std::string> v;

		v.push_back(std::to_string(e.actor_id));
		v.push_back(NullableUintSql(e.zone_id));
		v.push_back(NullableUintSql(e.instance_id));
		v.push_back(NullableUintSql(e.entity_id));
		v.push_back("'" + Strings::Escape(e.state) + "'");
		v.push_back(NullableStringSql(e.status_json));
		v.push_back(NullableTimeSql(e.heartbeat_at));
		v.push_back("FROM_UNIXTIME(" + (e.updated_at > 0 ? std::to_string(e.updated_at) : "null") + ")");

		auto results = db.QueryDatabase(
			fmt::format(
				"{} VALUES ({})",
				BaseInsert(),
				Strings::Implode(",", v)
			)
		);

		if (results.Success()) {
			e.actor_id = results.LastInsertedID();
			return e;
		}

		e = NewEntity();

		return e;
	}

	static int InsertMany(
		Database& db,
		const std::vector<ActorStatus> &entries
	)
	{
		std::vector<std::string> insert_chunks;

		for (auto &e: entries) {
			std::vector<std::string> v;

			v.push_back(std::to_string(e.actor_id));
			v.push_back(NullableUintSql(e.zone_id));
			v.push_back(NullableUintSql(e.instance_id));
			v.push_back(NullableUintSql(e.entity_id));
			v.push_back("'" + Strings::Escape(e.state) + "'");
			v.push_back(NullableStringSql(e.status_json));
			v.push_back(NullableTimeSql(e.heartbeat_at));
			v.push_back("FROM_UNIXTIME(" + (e.updated_at > 0 ? std::to_string(e.updated_at) : "null") + ")");

			insert_chunks.push_back("(" + Strings::Implode(",", v) + ")");
		}

		std::vector<std::string> v;

		auto results = db.QueryDatabase(
			fmt::format(
				"{} VALUES {}",
				BaseInsert(),
				Strings::Implode(",", insert_chunks)
			)
		);

		return (results.Success() ? results.RowsAffected() : 0);
	}

	static std::vector<ActorStatus> All(Database& db)
	{
		std::vector<ActorStatus> all_entries;

		auto results = db.QueryDatabase(
			fmt::format(
				"{}",
				BaseSelect()
			)
		);

		all_entries.reserve(results.RowCount());

		for (auto row = results.begin(); row != results.end(); ++row) {
			ActorStatus e{};

			e.actor_id     = row[0] ? static_cast<uint32_t>(strtoul(row[0], nullptr, 10)) : 0;
			e.zone_id      = row[1] ? std::optional<uint32_t>(static_cast<uint32_t>(strtoul(row[1], nullptr, 10))) : std::nullopt;
			e.instance_id  = row[2] ? std::optional<uint32_t>(static_cast<uint32_t>(strtoul(row[2], nullptr, 10))) : std::nullopt;
			e.entity_id    = row[3] ? std::optional<uint32_t>(static_cast<uint32_t>(strtoul(row[3], nullptr, 10))) : std::nullopt;
			e.state        = row[4] ? row[4] : "";
			e.status_json  = row[5] ? std::optional<std::string>(row[5]) : std::nullopt;
			e.heartbeat_at = row[6] ? std::optional<time_t>(strtoll(row[6], nullptr, 10)) : std::nullopt;
			e.updated_at   = strtoll(row[7] ? row[7] : "-1", nullptr, 10);

			all_entries.push_back(e);
		}

		return all_entries;
	}

	static std::vector<ActorStatus> GetWhere(Database& db, const std::string &where_filter)
	{
		std::vector<ActorStatus> all_entries;

		auto results = db.QueryDatabase(
			fmt::format(
				"{} WHERE {}",
				BaseSelect(),
				where_filter
			)
		);

		all_entries.reserve(results.RowCount());

		for (auto row = results.begin(); row != results.end(); ++row) {
			ActorStatus e{};

			e.actor_id     = row[0] ? static_cast<uint32_t>(strtoul(row[0], nullptr, 10)) : 0;
			e.zone_id      = row[1] ? std::optional<uint32_t>(static_cast<uint32_t>(strtoul(row[1], nullptr, 10))) : std::nullopt;
			e.instance_id  = row[2] ? std::optional<uint32_t>(static_cast<uint32_t>(strtoul(row[2], nullptr, 10))) : std::nullopt;
			e.entity_id    = row[3] ? std::optional<uint32_t>(static_cast<uint32_t>(strtoul(row[3], nullptr, 10))) : std::nullopt;
			e.state        = row[4] ? row[4] : "";
			e.status_json  = row[5] ? std::optional<std::string>(row[5]) : std::nullopt;
			e.heartbeat_at = row[6] ? std::optional<time_t>(strtoll(row[6], nullptr, 10)) : std::nullopt;
			e.updated_at   = strtoll(row[7] ? row[7] : "-1", nullptr, 10);

			all_entries.push_back(e);
		}

		return all_entries;
	}

	static int DeleteWhere(Database& db, const std::string &where_filter)
	{
		auto results = db.QueryDatabase(
			fmt::format(
				"DELETE FROM {} WHERE {}",
				TableName(),
				where_filter
			)
		);

		return (results.Success() ? results.RowsAffected() : 0);
	}

	static int Truncate(Database& db)
	{
		auto results = db.QueryDatabase(
			fmt::format(
				"TRUNCATE TABLE {}",
				TableName()
			)
		);

		return (results.Success() ? results.RowsAffected() : 0);
	}

	static int64 GetMaxId(Database& db)
	{
		auto results = db.QueryDatabase(
			fmt::format(
				"SELECT COALESCE(MAX({}), 0) FROM {}",
				PrimaryKey(),
				TableName()
			)
		);

		return (results.Success() && results.begin()[0] ? strtoll(results.begin()[0], nullptr, 10) : 0);
	}

	static int64 Count(Database& db, const std::string &where_filter = "")
	{
		auto results = db.QueryDatabase(
			fmt::format(
				"SELECT COUNT(*) FROM {} {}",
				TableName(),
				(where_filter.empty() ? "" : "WHERE " + where_filter)
			)
		);

		return (results.Success() && results.begin()[0] ? strtoll(results.begin()[0], nullptr, 10) : 0);
	}

	static std::string BaseReplace()
	{
		return fmt::format(
			"REPLACE INTO {} ({}) ",
			TableName(),
			ColumnsRaw()
		);
	}

	static int ReplaceOne(
		Database& db,
		const ActorStatus &e
	)
	{
		std::vector<std::string> v;

		v.push_back(std::to_string(e.actor_id));
		v.push_back(NullableUintSql(e.zone_id));
		v.push_back(NullableUintSql(e.instance_id));
		v.push_back(NullableUintSql(e.entity_id));
		v.push_back("'" + Strings::Escape(e.state) + "'");
		v.push_back(NullableStringSql(e.status_json));
		v.push_back(NullableTimeSql(e.heartbeat_at));
		v.push_back("FROM_UNIXTIME(" + (e.updated_at > 0 ? std::to_string(e.updated_at) : "null") + ")");

		auto results = db.QueryDatabase(
			fmt::format(
				"{} VALUES ({})",
				BaseReplace(),
				Strings::Implode(",", v)
			)
		);

		return (results.Success() ? results.RowsAffected() : 0);
	}

	static int ReplaceMany(
		Database& db,
		const std::vector<ActorStatus> &entries
	)
	{
		std::vector<std::string> insert_chunks;

		for (auto &e: entries) {
			std::vector<std::string> v;

			v.push_back(std::to_string(e.actor_id));
			v.push_back(NullableUintSql(e.zone_id));
			v.push_back(NullableUintSql(e.instance_id));
			v.push_back(NullableUintSql(e.entity_id));
			v.push_back("'" + Strings::Escape(e.state) + "'");
			v.push_back(NullableStringSql(e.status_json));
			v.push_back(NullableTimeSql(e.heartbeat_at));
			v.push_back("FROM_UNIXTIME(" + (e.updated_at > 0 ? std::to_string(e.updated_at) : "null") + ")");

			insert_chunks.push_back("(" + Strings::Implode(",", v) + ")");
		}

		std::vector<std::string> v;

		auto results = db.QueryDatabase(
			fmt::format(
				"{} VALUES {}",
				BaseReplace(),
				Strings::Implode(",", insert_chunks)
			)
		);

		return (results.Success() ? results.RowsAffected() : 0);
	}

private:
	static std::string NullableUintSql(const std::optional<uint32_t> &value)
	{
		return value.has_value() ? std::to_string(*value) : "null";
	}

	static std::string NullableStringSql(const std::optional<std::string> &value)
	{
		return value.has_value() ? "'" + Strings::Escape(*value) + "'" : "null";
	}

	static std::string NullableTimeSql(const std::optional<time_t> &value)
	{
		return value.has_value() ? "FROM_UNIXTIME(" + std::to_string(*value) + ")" : "null";
	}
};

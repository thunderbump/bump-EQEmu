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

#include <ctime>
#include <optional>

class BaseActorEventsRepository {
public:
	struct ActorEvents {
		uint64_t                event_id;
		uint32_t                actor_id;
		std::optional<uint32_t> bot_id;
		std::optional<uint32_t> owner_character_id;
		std::optional<uint32_t> zone_id;
		std::optional<uint32_t> instance_id;
		std::optional<uint32_t> entity_id;
		std::string             event_type;
		std::string             event_json;
		time_t                  created_at;
	};

	static std::string PrimaryKey()
	{
		return std::string("event_id");
	}

	static std::vector<std::string> Columns()
	{
		return {
			"event_id",
			"actor_id",
			"bot_id",
			"owner_character_id",
			"zone_id",
			"instance_id",
			"entity_id",
			"event_type",
			"event_json",
			"created_at",
		};
	}

	static std::vector<std::string> SelectColumns()
	{
		return {
			"event_id",
			"actor_id",
			"bot_id",
			"owner_character_id",
			"zone_id",
			"instance_id",
			"entity_id",
			"event_type",
			"event_json",
			"UNIX_TIMESTAMP(created_at)",
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
		return std::string("actor_events");
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

	static ActorEvents NewEntity()
	{
		ActorEvents e{};

		e.event_id = 0;
		e.actor_id = 0;
		e.bot_id = std::nullopt;
		e.owner_character_id = std::nullopt;
		e.zone_id = std::nullopt;
		e.instance_id = std::nullopt;
		e.entity_id = std::nullopt;
		e.event_type = "";
		e.event_json = "";
		e.created_at = 0;

		return e;
	}

	static ActorEvents GetActorEvents(const std::vector<ActorEvents> &actor_eventss, uint64_t actor_events_id)
	{
		for (auto &actor_events : actor_eventss) {
			if (actor_events.event_id == actor_events_id) {
				return actor_events;
			}
		}

		return NewEntity();
	}

	static ActorEvents FindOne(Database &db, uint64_t actor_events_id)
	{
		auto results = db.QueryDatabase(
			fmt::format(
				"{} WHERE {} = {} LIMIT 1",
				BaseSelect(),
				PrimaryKey(),
				actor_events_id
			)
		);

		auto row = results.begin();
		if (results.RowCount() == 1) {
			ActorEvents e{};

			e.event_id = row[0] ? strtoull(row[0], nullptr, 10) : 0;
			e.actor_id = row[1] ? static_cast<uint32_t>(strtoul(row[1], nullptr, 10)) : 0;
			e.bot_id = row[2] ? std::optional<uint32_t>(static_cast<uint32_t>(strtoul(row[2], nullptr, 10))) : std::nullopt;
			e.owner_character_id = row[3] ? std::optional<uint32_t>(static_cast<uint32_t>(strtoul(row[3], nullptr, 10))) : std::nullopt;
			e.zone_id = row[4] ? std::optional<uint32_t>(static_cast<uint32_t>(strtoul(row[4], nullptr, 10))) : std::nullopt;
			e.instance_id = row[5] ? std::optional<uint32_t>(static_cast<uint32_t>(strtoul(row[5], nullptr, 10))) : std::nullopt;
			e.entity_id = row[6] ? std::optional<uint32_t>(static_cast<uint32_t>(strtoul(row[6], nullptr, 10))) : std::nullopt;
			e.event_type = row[7] ? row[7] : "";
			e.event_json = row[8] ? row[8] : "";
			e.created_at = strtoll(row[9] ? row[9] : "-1", nullptr, 10);

			return e;
		}

		return NewEntity();
	}

	static int DeleteOne(Database &db, uint64_t actor_events_id)
	{
		auto results = db.QueryDatabase(
			fmt::format(
				"DELETE FROM {} WHERE {} = {}",
				TableName(),
				PrimaryKey(),
				actor_events_id
			)
		);

		return (results.Success() ? results.RowsAffected() : 0);
	}

	static int UpdateOne(Database &db, const ActorEvents &e)
	{
		std::vector<std::string> v;
		auto columns = Columns();

		v.push_back(columns[0] + " = " + std::to_string(e.event_id));
		v.push_back(columns[1] + " = " + std::to_string(e.actor_id));
		v.push_back(columns[2] + " = " + NullableUintSql(e.bot_id));
		v.push_back(columns[3] + " = " + NullableUintSql(e.owner_character_id));
		v.push_back(columns[4] + " = " + NullableUintSql(e.zone_id));
		v.push_back(columns[5] + " = " + NullableUintSql(e.instance_id));
		v.push_back(columns[6] + " = " + NullableUintSql(e.entity_id));
		v.push_back(columns[7] + " = '" + Strings::Escape(e.event_type) + "'");
		v.push_back(columns[8] + " = '" + Strings::Escape(e.event_json) + "'");
		v.push_back(columns[9] + " = FROM_UNIXTIME(" + (e.created_at > 0 ? std::to_string(e.created_at) : "null") + ")");

		auto results = db.QueryDatabase(
			fmt::format(
				"UPDATE {} SET {} WHERE {} = {}",
				TableName(),
				Strings::Implode(", ", v),
				PrimaryKey(),
				e.event_id
			)
		);

		return (results.Success() ? results.RowsAffected() : 0);
	}

	static ActorEvents InsertOne(Database &db, ActorEvents e)
	{
		std::vector<std::string> v;

		v.push_back(std::to_string(e.event_id));
		v.push_back(std::to_string(e.actor_id));
		v.push_back(NullableUintSql(e.bot_id));
		v.push_back(NullableUintSql(e.owner_character_id));
		v.push_back(NullableUintSql(e.zone_id));
		v.push_back(NullableUintSql(e.instance_id));
		v.push_back(NullableUintSql(e.entity_id));
		v.push_back("'" + Strings::Escape(e.event_type) + "'");
		v.push_back("'" + Strings::Escape(e.event_json) + "'");
		v.push_back("FROM_UNIXTIME(" + (e.created_at > 0 ? std::to_string(e.created_at) : "null") + ")");

		auto results = db.QueryDatabase(
			fmt::format(
				"{} VALUES ({})",
				BaseInsert(),
				Strings::Implode(",", v)
			)
		);

		if (results.Success()) {
			e.event_id = results.LastInsertedID();
			return e;
		}

		return NewEntity();
	}

	static int InsertMany(
		Database& db,
		const std::vector<ActorEvents> &entries
	)
	{
		std::vector<std::string> insert_chunks;

		for (auto &e: entries) {
			std::vector<std::string> v;

			v.push_back(std::to_string(e.event_id));
			v.push_back(std::to_string(e.actor_id));
			v.push_back(NullableUintSql(e.bot_id));
			v.push_back(NullableUintSql(e.owner_character_id));
			v.push_back(NullableUintSql(e.zone_id));
			v.push_back(NullableUintSql(e.instance_id));
			v.push_back(NullableUintSql(e.entity_id));
			v.push_back("'" + Strings::Escape(e.event_type) + "'");
			v.push_back("'" + Strings::Escape(e.event_json) + "'");
			v.push_back("FROM_UNIXTIME(" + (e.created_at > 0 ? std::to_string(e.created_at) : "null") + ")");

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

	static std::vector<ActorEvents> All(Database &db)
	{
		std::vector<ActorEvents> all_entries;
		auto results = db.QueryDatabase(BaseSelect());

		all_entries.reserve(results.RowCount());
		for (auto row = results.begin(); row != results.end(); ++row) {
			all_entries.push_back(FindOneFromRow(row));
		}

		return all_entries;
	}

	static std::vector<ActorEvents> GetWhere(Database &db, const std::string &where_filter)
	{
		std::vector<ActorEvents> all_entries;
		auto results = db.QueryDatabase(
			fmt::format("{} WHERE {}", BaseSelect(), where_filter)
		);

		all_entries.reserve(results.RowCount());
		for (auto row = results.begin(); row != results.end(); ++row) {
			all_entries.push_back(FindOneFromRow(row));
		}

		return all_entries;
	}

	static int DeleteWhere(Database &db, const std::string &where_filter)
	{
		auto results = db.QueryDatabase(
			fmt::format("DELETE FROM {} WHERE {}", TableName(), where_filter)
		);

		return (results.Success() ? results.RowsAffected() : 0);
	}

	static int Truncate(Database &db)
	{
		auto results = db.QueryDatabase(fmt::format("TRUNCATE TABLE {}", TableName()));
		return (results.Success() ? results.RowsAffected() : 0);
	}

	static int64 GetMaxId(Database &db)
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

	static int64 Count(Database &db, const std::string &where_filter = "")
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

private:
	template <typename RowType>
	static ActorEvents FindOneFromRow(RowType &row)
	{
		return {
			.event_id = row[0] ? strtoull(row[0], nullptr, 10) : 0,
			.actor_id = row[1] ? static_cast<uint32_t>(strtoul(row[1], nullptr, 10)) : 0,
			.bot_id = row[2] ? std::optional<uint32_t>(static_cast<uint32_t>(strtoul(row[2], nullptr, 10))) : std::nullopt,
			.owner_character_id = row[3] ? std::optional<uint32_t>(static_cast<uint32_t>(strtoul(row[3], nullptr, 10))) : std::nullopt,
			.zone_id = row[4] ? std::optional<uint32_t>(static_cast<uint32_t>(strtoul(row[4], nullptr, 10))) : std::nullopt,
			.instance_id = row[5] ? std::optional<uint32_t>(static_cast<uint32_t>(strtoul(row[5], nullptr, 10))) : std::nullopt,
			.entity_id = row[6] ? std::optional<uint32_t>(static_cast<uint32_t>(strtoul(row[6], nullptr, 10))) : std::nullopt,
			.event_type = row[7] ? row[7] : "",
			.event_json = row[8] ? row[8] : "",
			.created_at = strtoll(row[9] ? row[9] : "-1", nullptr, 10),
		};
	}

	static std::string NullableUintSql(const std::optional<uint32_t> &value)
	{
		return value.has_value() ? std::to_string(*value) : "null";
	}
};

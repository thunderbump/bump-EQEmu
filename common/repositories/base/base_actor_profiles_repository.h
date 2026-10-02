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

class BaseActorProfilesRepository {
public:
	struct ActorProfiles {
		uint32_t                actor_id;
		std::string             actor_type;
		std::string             actor_substrate;
		std::optional<uint32_t> bot_id;
		std::optional<uint32_t> owner_character_id;
		uint8_t                 enabled;
		time_t                  created_at;
		time_t                  updated_at;
	};

	static std::string PrimaryKey()
	{
		return std::string("actor_id");
	}

	static std::vector<std::string> Columns()
	{
		return {
			"actor_id",
			"actor_type",
			"actor_substrate",
			"bot_id",
			"owner_character_id",
			"enabled",
			"created_at",
			"updated_at",
		};
	}

	static std::vector<std::string> SelectColumns()
	{
		return {
			"actor_id",
			"actor_type",
			"actor_substrate",
			"bot_id",
			"owner_character_id",
			"enabled",
			"UNIX_TIMESTAMP(created_at)",
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
		return std::string("actor_profiles");
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

	static ActorProfiles NewEntity()
	{
		ActorProfiles e{};

		e.actor_id           = 0;
		e.actor_type         = "";
		e.actor_substrate    = "";
		e.bot_id             = std::nullopt;
		e.owner_character_id = std::nullopt;
		e.enabled            = 1;
		e.created_at         = 0;
		e.updated_at         = 0;

		return e;
	}

	static ActorProfiles GetActorProfiles(
		const std::vector<ActorProfiles> &actor_profiless,
		int actor_profiles_id
	)
	{
		for (auto &actor_profiles : actor_profiless) {
			if (actor_profiles.actor_id == actor_profiles_id) {
				return actor_profiles;
			}
		}

		return NewEntity();
	}

	static ActorProfiles FindOne(
		Database& db,
		int actor_profiles_id
	)
	{
		auto results = db.QueryDatabase(
			fmt::format(
				"{} WHERE {} = {} LIMIT 1",
				BaseSelect(),
				PrimaryKey(),
				actor_profiles_id
			)
		);

		auto row = results.begin();
		if (results.RowCount() == 1) {
			ActorProfiles e{};

			e.actor_id           = row[0] ? static_cast<uint32_t>(strtoul(row[0], nullptr, 10)) : 0;
			e.actor_type         = row[1] ? row[1] : "";
			e.actor_substrate    = row[2] ? row[2] : "";
			e.bot_id             = row[3] ? std::optional<uint32_t>(static_cast<uint32_t>(strtoul(row[3], nullptr, 10))) : std::nullopt;
			e.owner_character_id = row[4] ? std::optional<uint32_t>(static_cast<uint32_t>(strtoul(row[4], nullptr, 10))) : std::nullopt;
			e.enabled            = row[5] ? static_cast<uint8_t>(strtoul(row[5], nullptr, 10)) : 1;
			e.created_at         = strtoll(row[6] ? row[6] : "-1", nullptr, 10);
			e.updated_at         = strtoll(row[7] ? row[7] : "-1", nullptr, 10);

			return e;
		}

		return NewEntity();
	}

	static int DeleteOne(
		Database& db,
		int actor_profiles_id
	)
	{
		auto results = db.QueryDatabase(
			fmt::format(
				"DELETE FROM {} WHERE {} = {}",
				TableName(),
				PrimaryKey(),
				actor_profiles_id
			)
		);

		return (results.Success() ? results.RowsAffected() : 0);
	}

	static int UpdateOne(
		Database& db,
		const ActorProfiles &e
	)
	{
		std::vector<std::string> v;

		auto columns = Columns();

		v.push_back(columns[1] + " = '" + Strings::Escape(e.actor_type) + "'");
		v.push_back(columns[2] + " = '" + Strings::Escape(e.actor_substrate) + "'");
		v.push_back(columns[3] + " = " + NullableUintSql(e.bot_id));
		v.push_back(columns[4] + " = " + NullableUintSql(e.owner_character_id));
		v.push_back(columns[5] + " = " + std::to_string(e.enabled));
		v.push_back(columns[6] + " = FROM_UNIXTIME(" + (e.created_at > 0 ? std::to_string(e.created_at) : "null") + ")");
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

	static ActorProfiles InsertOne(
		Database& db,
		ActorProfiles e
	)
	{
		std::vector<std::string> v;

		v.push_back(std::to_string(e.actor_id));
		v.push_back("'" + Strings::Escape(e.actor_type) + "'");
		v.push_back("'" + Strings::Escape(e.actor_substrate) + "'");
		v.push_back(NullableUintSql(e.bot_id));
		v.push_back(NullableUintSql(e.owner_character_id));
		v.push_back(std::to_string(e.enabled));
		v.push_back("FROM_UNIXTIME(" + (e.created_at > 0 ? std::to_string(e.created_at) : "null") + ")");
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
		const std::vector<ActorProfiles> &entries
	)
	{
		std::vector<std::string> insert_chunks;

		for (auto &e: entries) {
			std::vector<std::string> v;

			v.push_back(std::to_string(e.actor_id));
			v.push_back("'" + Strings::Escape(e.actor_type) + "'");
			v.push_back("'" + Strings::Escape(e.actor_substrate) + "'");
			v.push_back(NullableUintSql(e.bot_id));
			v.push_back(NullableUintSql(e.owner_character_id));
			v.push_back(std::to_string(e.enabled));
			v.push_back("FROM_UNIXTIME(" + (e.created_at > 0 ? std::to_string(e.created_at) : "null") + ")");
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

	static std::vector<ActorProfiles> All(Database& db)
	{
		std::vector<ActorProfiles> all_entries;

		auto results = db.QueryDatabase(
			fmt::format(
				"{}",
				BaseSelect()
			)
		);

		all_entries.reserve(results.RowCount());

		for (auto row = results.begin(); row != results.end(); ++row) {
			ActorProfiles e{};

			e.actor_id           = row[0] ? static_cast<uint32_t>(strtoul(row[0], nullptr, 10)) : 0;
			e.actor_type         = row[1] ? row[1] : "";
			e.actor_substrate    = row[2] ? row[2] : "";
			e.bot_id             = row[3] ? std::optional<uint32_t>(static_cast<uint32_t>(strtoul(row[3], nullptr, 10))) : std::nullopt;
			e.owner_character_id = row[4] ? std::optional<uint32_t>(static_cast<uint32_t>(strtoul(row[4], nullptr, 10))) : std::nullopt;
			e.enabled            = row[5] ? static_cast<uint8_t>(strtoul(row[5], nullptr, 10)) : 1;
			e.created_at         = strtoll(row[6] ? row[6] : "-1", nullptr, 10);
			e.updated_at         = strtoll(row[7] ? row[7] : "-1", nullptr, 10);

			all_entries.push_back(e);
		}

		return all_entries;
	}

	static std::vector<ActorProfiles> GetWhere(Database& db, const std::string &where_filter)
	{
		std::vector<ActorProfiles> all_entries;

		auto results = db.QueryDatabase(
			fmt::format(
				"{} WHERE {}",
				BaseSelect(),
				where_filter
			)
		);

		all_entries.reserve(results.RowCount());

		for (auto row = results.begin(); row != results.end(); ++row) {
			ActorProfiles e{};

			e.actor_id           = row[0] ? static_cast<uint32_t>(strtoul(row[0], nullptr, 10)) : 0;
			e.actor_type         = row[1] ? row[1] : "";
			e.actor_substrate    = row[2] ? row[2] : "";
			e.bot_id             = row[3] ? std::optional<uint32_t>(static_cast<uint32_t>(strtoul(row[3], nullptr, 10))) : std::nullopt;
			e.owner_character_id = row[4] ? std::optional<uint32_t>(static_cast<uint32_t>(strtoul(row[4], nullptr, 10))) : std::nullopt;
			e.enabled            = row[5] ? static_cast<uint8_t>(strtoul(row[5], nullptr, 10)) : 1;
			e.created_at         = strtoll(row[6] ? row[6] : "-1", nullptr, 10);
			e.updated_at         = strtoll(row[7] ? row[7] : "-1", nullptr, 10);

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
		const ActorProfiles &e
	)
	{
		std::vector<std::string> v;

		v.push_back(std::to_string(e.actor_id));
		v.push_back("'" + Strings::Escape(e.actor_type) + "'");
		v.push_back("'" + Strings::Escape(e.actor_substrate) + "'");
		v.push_back(NullableUintSql(e.bot_id));
		v.push_back(NullableUintSql(e.owner_character_id));
		v.push_back(std::to_string(e.enabled));
		v.push_back("FROM_UNIXTIME(" + (e.created_at > 0 ? std::to_string(e.created_at) : "null") + ")");
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
		const std::vector<ActorProfiles> &entries
	)
	{
		std::vector<std::string> insert_chunks;

		for (auto &e: entries) {
			std::vector<std::string> v;

			v.push_back(std::to_string(e.actor_id));
			v.push_back("'" + Strings::Escape(e.actor_type) + "'");
			v.push_back("'" + Strings::Escape(e.actor_substrate) + "'");
			v.push_back(NullableUintSql(e.bot_id));
			v.push_back(NullableUintSql(e.owner_character_id));
			v.push_back(std::to_string(e.enabled));
			v.push_back("FROM_UNIXTIME(" + (e.created_at > 0 ? std::to_string(e.created_at) : "null") + ")");
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
};

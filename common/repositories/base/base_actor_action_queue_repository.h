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

class BaseActorActionQueueRepository {
public:
	struct ActorActionQueue {
		uint64_t                   action_id;
		uint32_t                   actor_id;
		std::string                source;
		std::optional<std::string> source_metadata_json;
		std::string                action_type;
		std::string                action_json;
		std::string                idempotency_key;
		std::string                state;
		std::optional<time_t>      not_before;
		std::optional<time_t>      expires_at;
		std::optional<std::string> claimed_by;
		std::optional<time_t>      claimed_at;
		std::optional<time_t>      completed_at;
		std::optional<std::string> failure_reason;
		std::optional<std::string> result_json;
		time_t                     created_at;
		time_t                     updated_at;
	};

	static std::string PrimaryKey()
	{
		return std::string("action_id");
	}

	static std::vector<std::string> Columns()
	{
		return {
			"action_id",
			"actor_id",
			"source",
			"source_metadata_json",
			"action_type",
			"action_json",
			"idempotency_key",
			"state",
			"not_before",
			"expires_at",
			"claimed_by",
			"claimed_at",
			"completed_at",
			"failure_reason",
			"result_json",
			"created_at",
			"updated_at",
		};
	}

	static std::vector<std::string> SelectColumns()
	{
		return {
			"action_id",
			"actor_id",
			"source",
			"source_metadata_json",
			"action_type",
			"action_json",
			"idempotency_key",
			"state",
			"UNIX_TIMESTAMP(not_before)",
			"UNIX_TIMESTAMP(expires_at)",
			"claimed_by",
			"UNIX_TIMESTAMP(claimed_at)",
			"UNIX_TIMESTAMP(completed_at)",
			"failure_reason",
			"result_json",
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
		return std::string("actor_action_queue");
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

	static ActorActionQueue NewEntity()
	{
		ActorActionQueue e{};

		e.action_id = 0;
		e.actor_id = 0;
		e.source = "";
		e.source_metadata_json = std::nullopt;
		e.action_type = "";
		e.action_json = "";
		e.idempotency_key = "";
		e.state = "";
		e.not_before = std::nullopt;
		e.expires_at = std::nullopt;
		e.claimed_by = std::nullopt;
		e.claimed_at = std::nullopt;
		e.completed_at = std::nullopt;
		e.failure_reason = std::nullopt;
		e.result_json = std::nullopt;
		e.created_at = 0;
		e.updated_at = 0;

		return e;
	}

	static ActorActionQueue GetActorActionQueue(const std::vector<ActorActionQueue> &entries, uint64_t action_id)
	{
		for (auto &entry : entries) {
			if (entry.action_id == action_id) {
				return entry;
			}
		}

		return NewEntity();
	}

	static ActorActionQueue FindOne(Database &db, uint64_t action_id)
	{
		auto results = db.QueryDatabase(
			fmt::format(
				"{} WHERE {} = {} LIMIT 1",
				BaseSelect(),
				PrimaryKey(),
				action_id
			)
		);

		auto row = results.begin();
		if (results.RowCount() == 1) {
			ActorActionQueue e{};

			e.action_id = row[0] ? strtoull(row[0], nullptr, 10) : 0;
			e.actor_id = row[1] ? static_cast<uint32_t>(strtoul(row[1], nullptr, 10)) : 0;
			e.source = row[2] ? row[2] : "";
			e.source_metadata_json = row[3] ? std::optional<std::string>(row[3]) : std::nullopt;
			e.action_type = row[4] ? row[4] : "";
			e.action_json = row[5] ? row[5] : "";
			e.idempotency_key = row[6] ? row[6] : "";
			e.state = row[7] ? row[7] : "";
			e.not_before = row[8] ? std::optional<time_t>(strtoll(row[8], nullptr, 10)) : std::nullopt;
			e.expires_at = row[9] ? std::optional<time_t>(strtoll(row[9], nullptr, 10)) : std::nullopt;
			e.claimed_by = row[10] ? std::optional<std::string>(row[10]) : std::nullopt;
			e.claimed_at = row[11] ? std::optional<time_t>(strtoll(row[11], nullptr, 10)) : std::nullopt;
			e.completed_at = row[12] ? std::optional<time_t>(strtoll(row[12], nullptr, 10)) : std::nullopt;
			e.failure_reason = row[13] ? std::optional<std::string>(row[13]) : std::nullopt;
			e.result_json = row[14] ? std::optional<std::string>(row[14]) : std::nullopt;
			e.created_at = strtoll(row[15] ? row[15] : "-1", nullptr, 10);
			e.updated_at = strtoll(row[16] ? row[16] : "-1", nullptr, 10);

			return e;
		}

		return NewEntity();
	}

	static int DeleteOne(Database &db, uint64_t action_id)
	{
		auto results = db.QueryDatabase(
			fmt::format(
				"DELETE FROM {} WHERE {} = {}",
				TableName(),
				PrimaryKey(),
				action_id
			)
		);

		return (results.Success() ? results.RowsAffected() : 0);
	}

	static int UpdateOne(Database &db, const ActorActionQueue &e)
	{
		std::vector<std::string> v;
		auto columns = Columns();

		v.push_back(columns[0] + " = " + std::to_string(e.action_id));
		v.push_back(columns[1] + " = " + std::to_string(e.actor_id));
		v.push_back(columns[2] + " = '" + Strings::Escape(e.source) + "'");
		v.push_back(columns[3] + " = " + NullableStringSql(e.source_metadata_json));
		v.push_back(columns[4] + " = '" + Strings::Escape(e.action_type) + "'");
		v.push_back(columns[5] + " = '" + Strings::Escape(e.action_json) + "'");
		v.push_back(columns[6] + " = '" + Strings::Escape(e.idempotency_key) + "'");
		v.push_back(columns[7] + " = '" + Strings::Escape(e.state) + "'");
		v.push_back(columns[8] + " = " + NullableTimeSql(e.not_before));
		v.push_back(columns[9] + " = " + NullableTimeSql(e.expires_at));
		v.push_back(columns[10] + " = " + NullableStringSql(e.claimed_by));
		v.push_back(columns[11] + " = " + NullableTimeSql(e.claimed_at));
		v.push_back(columns[12] + " = " + NullableTimeSql(e.completed_at));
		v.push_back(columns[13] + " = " + NullableStringSql(e.failure_reason));
		v.push_back(columns[14] + " = " + NullableStringSql(e.result_json));
		v.push_back(columns[15] + " = FROM_UNIXTIME(" + (e.created_at > 0 ? std::to_string(e.created_at) : "null") + ")");
		v.push_back(columns[16] + " = FROM_UNIXTIME(" + (e.updated_at > 0 ? std::to_string(e.updated_at) : "null") + ")");

		auto results = db.QueryDatabase(
			fmt::format(
				"UPDATE {} SET {} WHERE {} = {}",
				TableName(),
				Strings::Implode(", ", v),
				PrimaryKey(),
				e.action_id
			)
		);

		return (results.Success() ? results.RowsAffected() : 0);
	}

	static ActorActionQueue InsertOne(Database &db, ActorActionQueue e)
	{
		std::vector<std::string> v;

		v.push_back(std::to_string(e.action_id));
		v.push_back(std::to_string(e.actor_id));
		v.push_back("'" + Strings::Escape(e.source) + "'");
		v.push_back(NullableStringSql(e.source_metadata_json));
		v.push_back("'" + Strings::Escape(e.action_type) + "'");
		v.push_back("'" + Strings::Escape(e.action_json) + "'");
		v.push_back("'" + Strings::Escape(e.idempotency_key) + "'");
		v.push_back("'" + Strings::Escape(e.state) + "'");
		v.push_back(NullableTimeSql(e.not_before));
		v.push_back(NullableTimeSql(e.expires_at));
		v.push_back(NullableStringSql(e.claimed_by));
		v.push_back(NullableTimeSql(e.claimed_at));
		v.push_back(NullableTimeSql(e.completed_at));
		v.push_back(NullableStringSql(e.failure_reason));
		v.push_back(NullableStringSql(e.result_json));
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
			e.action_id = results.LastInsertedID();
			return e;
		}

		return NewEntity();
	}

	static int InsertMany(Database &db, const std::vector<ActorActionQueue> &entries)
	{
		std::vector<std::string> insert_chunks;

		for (auto &e : entries) {
			std::vector<std::string> v;

			v.push_back(std::to_string(e.action_id));
			v.push_back(std::to_string(e.actor_id));
			v.push_back("'" + Strings::Escape(e.source) + "'");
			v.push_back(NullableStringSql(e.source_metadata_json));
			v.push_back("'" + Strings::Escape(e.action_type) + "'");
			v.push_back("'" + Strings::Escape(e.action_json) + "'");
			v.push_back("'" + Strings::Escape(e.idempotency_key) + "'");
			v.push_back("'" + Strings::Escape(e.state) + "'");
			v.push_back(NullableTimeSql(e.not_before));
			v.push_back(NullableTimeSql(e.expires_at));
			v.push_back(NullableStringSql(e.claimed_by));
			v.push_back(NullableTimeSql(e.claimed_at));
			v.push_back(NullableTimeSql(e.completed_at));
			v.push_back(NullableStringSql(e.failure_reason));
			v.push_back(NullableStringSql(e.result_json));
			v.push_back("FROM_UNIXTIME(" + (e.created_at > 0 ? std::to_string(e.created_at) : "null") + ")");
			v.push_back("FROM_UNIXTIME(" + (e.updated_at > 0 ? std::to_string(e.updated_at) : "null") + ")");

			insert_chunks.push_back("(" + Strings::Implode(",", v) + ")");
		}

		auto results = db.QueryDatabase(
			fmt::format(
				"{} VALUES {}",
				BaseInsert(),
				Strings::Implode(",", insert_chunks)
			)
		);

		return (results.Success() ? results.RowsAffected() : 0);
	}

	static std::vector<ActorActionQueue> All(Database &db)
	{
		std::vector<ActorActionQueue> all_entries;
		auto results = db.QueryDatabase(
			fmt::format(
				"{}",
				BaseSelect()
			)
		);

		all_entries.reserve(results.RowCount());
		for (auto row = results.begin(); row != results.end(); ++row) {
			all_entries.push_back(FromRow(row));
		}

		return all_entries;
	}

	static std::vector<ActorActionQueue> GetWhere(Database &db, const std::string &where_filter)
	{
		std::vector<ActorActionQueue> all_entries;
		auto results = db.QueryDatabase(
			fmt::format(
				"{} WHERE {}",
				BaseSelect(),
				where_filter
			)
		);

		all_entries.reserve(results.RowCount());
		for (auto row = results.begin(); row != results.end(); ++row) {
			all_entries.push_back(FromRow(row));
		}

		return all_entries;
	}

	static int DeleteWhere(Database &db, const std::string &where_filter)
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

	static int Truncate(Database &db)
	{
		auto results = db.QueryDatabase(
			fmt::format(
				"TRUNCATE TABLE {}",
				TableName()
			)
		);

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

		auto row = results.begin();
		return (results.Success() && results.RowCount() == 1 && row[0]) ? strtoll(row[0], nullptr, 10) : 0;
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

		auto row = results.begin();
		return (results.Success() && results.RowCount() == 1 && row[0]) ? strtoll(row[0], nullptr, 10) : 0;
	}

private:
	template <typename RowType>
	static ActorActionQueue FromRow(RowType &row)
	{
		return {
			.action_id = row[0] ? strtoull(row[0], nullptr, 10) : 0,
			.actor_id = row[1] ? static_cast<uint32_t>(strtoul(row[1], nullptr, 10)) : 0,
			.source = row[2] ? row[2] : "",
			.source_metadata_json = row[3] ? std::optional<std::string>(row[3]) : std::nullopt,
			.action_type = row[4] ? row[4] : "",
			.action_json = row[5] ? row[5] : "",
			.idempotency_key = row[6] ? row[6] : "",
			.state = row[7] ? row[7] : "",
			.not_before = row[8] ? std::optional<time_t>(strtoll(row[8], nullptr, 10)) : std::nullopt,
			.expires_at = row[9] ? std::optional<time_t>(strtoll(row[9], nullptr, 10)) : std::nullopt,
			.claimed_by = row[10] ? std::optional<std::string>(row[10]) : std::nullopt,
			.claimed_at = row[11] ? std::optional<time_t>(strtoll(row[11], nullptr, 10)) : std::nullopt,
			.completed_at = row[12] ? std::optional<time_t>(strtoll(row[12], nullptr, 10)) : std::nullopt,
			.failure_reason = row[13] ? std::optional<std::string>(row[13]) : std::nullopt,
			.result_json = row[14] ? std::optional<std::string>(row[14]) : std::nullopt,
			.created_at = strtoll(row[15] ? row[15] : "-1", nullptr, 10),
			.updated_at = strtoll(row[16] ? row[16] : "-1", nullptr, 10),
		};
	}

	static std::string NullableStringSql(const std::optional<std::string> &value)
	{
		return value.has_value() ? "'" + Strings::Escape(*value) + "'" : "null";
	}

	static std::string NullableTimeSql(const std::optional<time_t> &value)
	{
		return value.has_value()
			? "FROM_UNIXTIME(" + std::to_string(*value) + ")"
			: "null";
	}
};

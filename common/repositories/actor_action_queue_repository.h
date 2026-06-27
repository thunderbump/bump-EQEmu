#pragma once

#include "common/json/json.h"
#include "common/repositories/base/base_actor_action_queue_repository.h"

#include <ctime>
#include <memory>
#include <optional>
#include <string>

class ActorActionQueueRepository : public BaseActorActionQueueRepository {
public:
	using ActorActionRecord = BaseActorActionQueueRepository::ActorActionQueue;

	static constexpr size_t kSourceMaxLength = 64;
	static constexpr size_t kActionTypeMaxLength = 64;
	static constexpr size_t kIdempotencyKeyMaxLength = 128;
	static constexpr size_t kClaimedByMaxLength = 128;
	static constexpr size_t kFailureReasonMaxLength = 255;
	static constexpr size_t kSourceMetadataJsonMaxLength = 4096;
	static constexpr size_t kActionJsonMaxLength = 16384;
	static constexpr size_t kResultJsonMaxLength = 16384;

	struct EnqueueRecord {
		uint32_t                   actor_id = 0;
		std::string                source;
		std::optional<std::string> source_metadata_json;
		std::string                action_type;
		std::string                action_json;
		std::string                idempotency_key;
		std::optional<time_t>      not_before;
		std::optional<time_t>      expires_at;
		time_t                     created_at = 0;
	};

	struct ClaimRequest {
		std::optional<uint32_t> actor_id;
		std::string             claimed_by;
		time_t                  now = 0;
	};

	struct CompletionRecord {
		uint64_t                   action_id = 0;
		std::optional<std::string> result_json;
		time_t                     completed_at = 0;
	};

	struct FailureRecord {
		uint64_t    action_id = 0;
		std::string failure_reason;
		time_t      completed_at = 0;
	};

	static ActorActionRecord Enqueue(Database &db, EnqueueRecord record)
	{
		if (!IsValidEnqueueRecord(record)) {
			return NewEntity();
		}

		const auto created_at = record.created_at > 0 ? record.created_at : std::time(nullptr);
		auto results = db.QueryDatabase(
			fmt::format(
				R"SQL(
INSERT INTO {} (
	actor_id,
	source,
	source_metadata_json,
	action_type,
	action_json,
	idempotency_key,
	state,
	not_before,
	expires_at,
	claimed_by,
	claimed_at,
	completed_at,
	failure_reason,
	result_json,
	created_at,
	updated_at
) VALUES (
	{},
	'{}',
	{},
	'{}',
	'{}',
	'{}',
	'pending',
	{},
	{},
	NULL,
	NULL,
	NULL,
	NULL,
	NULL,
	FROM_UNIXTIME({}),
	FROM_UNIXTIME({})
)
ON DUPLICATE KEY UPDATE
	action_id = LAST_INSERT_ID(action_id)
)SQL",
				TableName(),
				record.actor_id,
				Strings::Escape(record.source),
				NullableStringSql(record.source_metadata_json),
				Strings::Escape(record.action_type),
				Strings::Escape(record.action_json),
				Strings::Escape(record.idempotency_key),
				NullableTimeSql(record.not_before),
				NullableTimeSql(record.expires_at),
				created_at,
				created_at
			)
		);

		if (!results.Success()) {
			return NewEntity();
		}

		return FindOne(db, results.LastInsertedID());
	}

	static std::optional<ActorActionRecord> FindByActionId(Database &db, uint64_t action_id)
	{
		const auto action = FindOne(db, action_id);
		if (!action.action_id) {
			return std::nullopt;
		}

		return action;
	}

	static std::optional<ActorActionRecord> FindByActorAndIdempotencyKey(
		Database &db,
		uint32_t actor_id,
		const std::string &idempotency_key
	)
	{
		if (!actor_id || idempotency_key.empty()) {
			return std::nullopt;
		}

		auto results = db.QueryDatabase(
			fmt::format(
				"{} WHERE actor_id = {} AND idempotency_key = '{}' LIMIT 1",
				BaseSelect(),
				actor_id,
				Strings::Escape(idempotency_key)
			)
		);

		if (!results.Success() || results.RowCount() != 1) {
			return std::nullopt;
		}

		return FromRow(results.begin());
	}

	static std::optional<ActorActionRecord> ClaimNextPending(Database &db, ClaimRequest request)
	{
		if (request.claimed_by.empty() || request.claimed_by.size() > kClaimedByMaxLength) {
			return std::nullopt;
		}

		const auto now = request.now > 0 ? request.now : std::time(nullptr);
		const auto actor_filter = request.actor_id.has_value()
			? fmt::format(" AND actor_id = {}", *request.actor_id)
			: "";

		auto results = db.QueryDatabase(
			fmt::format(
				R"SQL(
UPDATE {}
SET
	action_id = LAST_INSERT_ID(action_id),
	state = 'claimed',
	claimed_by = '{}',
	claimed_at = FROM_UNIXTIME({}),
	updated_at = FROM_UNIXTIME({})
WHERE state = 'pending'
  {}
  AND (not_before IS NULL OR not_before <= FROM_UNIXTIME({}))
  AND (expires_at IS NULL OR expires_at > FROM_UNIXTIME({}))
ORDER BY COALESCE(not_before, FROM_UNIXTIME(0)) ASC, action_id ASC
LIMIT 1
)SQL",
				TableName(),
				Strings::Escape(request.claimed_by),
				now,
				now,
				actor_filter,
				now,
				now
			)
		);
		// First-slice portability: the current validation DB contract cannot assume a
		// portable SKIP LOCKED path here, so concurrent claimers may block and lose a
		// turn instead of immediately skipping to another eligible row. central-lhy.14
		// can revisit this once execution ownership semantics and DB compatibility are
		// advanced together.

		if (!results.Success() || results.RowsAffected() < 1 || results.LastInsertedID() == 0) {
			return std::nullopt;
		}

		const auto claimed = FindOne(db, results.LastInsertedID());
		if (!claimed.action_id) {
			return std::nullopt;
		}

		return claimed;
	}

	static int ExpireDue(Database &db, time_t now, std::optional<uint32_t> actor_id = std::nullopt)
	{
		if (now <= 0) {
			now = std::time(nullptr);
		}

		const auto actor_filter = actor_id.has_value()
			? fmt::format(" AND actor_id = {}", *actor_id)
			: "";

		auto results = db.QueryDatabase(
			fmt::format(
				R"SQL(
UPDATE {}
SET
	state = 'expired',
	completed_at = FROM_UNIXTIME({}),
	updated_at = FROM_UNIXTIME({})
WHERE state IN ('pending', 'claimed')
  {}
  AND expires_at IS NOT NULL
  AND expires_at <= FROM_UNIXTIME({})
)SQL",
				TableName(),
				now,
				now,
				actor_filter,
				now
			)
		);

		return results.Success() ? results.RowsAffected() : 0;
	}

	static std::optional<ActorActionRecord> MarkCompleted(Database &db, CompletionRecord record)
	{
		if (!record.action_id || (record.result_json.has_value() && !IsValidJson(*record.result_json, kResultJsonMaxLength))) {
			return std::nullopt;
		}

		const auto completed_at = record.completed_at > 0 ? record.completed_at : std::time(nullptr);
		const auto transitioned = UpdateClaimedTerminalState(
			db,
			record.action_id,
			fmt::format(
				R"SQL(
UPDATE {}
SET
	state = CASE
		WHEN expires_at IS NOT NULL AND expires_at <= FROM_UNIXTIME({}) THEN 'expired'
		ELSE 'completed'
	END,
	result_json = CASE
		WHEN expires_at IS NOT NULL AND expires_at <= FROM_UNIXTIME({}) THEN NULL
		ELSE {}
	END,
	failure_reason = NULL,
	completed_at = FROM_UNIXTIME({}),
	updated_at = FROM_UNIXTIME({})
WHERE action_id = {}
  AND state = 'claimed'
)SQL",
				TableName(),
				completed_at,
				completed_at,
				NullableStringSql(record.result_json),
				completed_at,
				completed_at,
				record.action_id
			),
			"completed"
		);

		if (!transitioned.has_value()) {
			return std::nullopt;
		}

		return transitioned;
	}

	static std::optional<ActorActionRecord> MarkFailed(Database &db, FailureRecord record)
	{
		if (!record.action_id || record.failure_reason.empty() || record.failure_reason.size() > kFailureReasonMaxLength) {
			return std::nullopt;
		}

		const auto completed_at = record.completed_at > 0 ? record.completed_at : std::time(nullptr);
		const auto transitioned = UpdateClaimedTerminalState(
			db,
			record.action_id,
			fmt::format(
				R"SQL(
UPDATE {}
SET
	state = CASE
		WHEN expires_at IS NOT NULL AND expires_at <= FROM_UNIXTIME({}) THEN 'expired'
		ELSE 'failed'
	END,
	result_json = NULL,
	failure_reason = CASE
		WHEN expires_at IS NOT NULL AND expires_at <= FROM_UNIXTIME({}) THEN NULL
		ELSE '{}'
	END,
	completed_at = FROM_UNIXTIME({}),
	updated_at = FROM_UNIXTIME({})
WHERE action_id = {}
  AND state = 'claimed'
)SQL",
				TableName(),
				completed_at,
				completed_at,
				Strings::Escape(record.failure_reason),
				completed_at,
				completed_at,
				record.action_id
			),
			"failed"
		);

		if (!transitioned.has_value()) {
			return std::nullopt;
		}

		return transitioned;
	}

	static int DeleteByActorId(Database &db, uint32_t actor_id)
	{
		return DeleteWhere(db, fmt::format("actor_id = {}", actor_id));
	}

private:
	template <typename RowType>
	static ActorActionRecord FromRow(RowType &row)
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
			.created_at = strtoll(row[15] ? row[15] : "0", nullptr, 10),
			.updated_at = strtoll(row[16] ? row[16] : "0", nullptr, 10),
		};
	}

	static bool IsValidEnqueueRecord(const EnqueueRecord &record)
	{
		if (
			!record.actor_id ||
			record.source.empty() ||
			record.action_type.empty() ||
			record.action_json.empty() ||
			record.idempotency_key.empty()
		) {
			return false;
		}

		if (
			record.source.size() > kSourceMaxLength ||
			record.action_type.size() > kActionTypeMaxLength ||
			record.idempotency_key.size() > kIdempotencyKeyMaxLength
		) {
			return false;
		}

		if (record.source_metadata_json.has_value() && !IsValidJson(*record.source_metadata_json, kSourceMetadataJsonMaxLength)) {
			return false;
		}

		if (!IsValidJson(record.action_json, kActionJsonMaxLength)) {
			return false;
		}

		if (record.not_before.has_value() && record.expires_at.has_value() && *record.expires_at <= *record.not_before) {
			return false;
		}

		return true;
	}

	static bool IsValidJson(const std::string &document, size_t max_length)
	{
		size_t utf8_code_points = 0;
		if (
			document.empty() ||
			!TryCountUtf8CodePoints(document, utf8_code_points) ||
			utf8_code_points > max_length
		) {
			return false;
		}

		Json::CharReaderBuilder builder;
		std::unique_ptr<Json::CharReader> reader(builder.newCharReader());
		Json::Value root;
		std::string errors;
		return reader->parse(
			document.data(),
			document.data() + document.size(),
			&root,
			&errors
		);
	}

	static bool TryCountUtf8CodePoints(const std::string &document, size_t &count)
	{
		count = 0;

		for (size_t i = 0; i < document.size();) {
			const auto lead = static_cast<unsigned char>(document[i]);
			size_t sequence_length = 0;

			if ((lead & 0x80) == 0x00) {
				sequence_length = 1;
			}
			else if ((lead & 0xe0) == 0xc0) {
				if (lead < 0xc2) {
					return false;
				}
				sequence_length = 2;
			}
			else if ((lead & 0xf0) == 0xe0) {
				sequence_length = 3;
			}
			else if ((lead & 0xf8) == 0xf0) {
				if (lead > 0xf4) {
					return false;
				}
				sequence_length = 4;
			}
			else {
				return false;
			}

			if (i + sequence_length > document.size()) {
				return false;
			}

			if (sequence_length == 3) {
				const auto second = static_cast<unsigned char>(document[i + 1]);
				if ((lead == 0xe0 && second < 0xa0) || (lead == 0xed && second >= 0xa0)) {
					return false;
				}
			}
			else if (sequence_length == 4) {
				const auto second = static_cast<unsigned char>(document[i + 1]);
				if ((lead == 0xf0 && second < 0x90) || (lead == 0xf4 && second > 0x8f)) {
					return false;
				}
			}

			for (size_t offset = 1; offset < sequence_length; ++offset) {
				const auto continuation = static_cast<unsigned char>(document[i + offset]);
				if ((continuation & 0xc0) != 0x80) {
					return false;
				}
			}

			++count;
			i += sequence_length;
		}

		return true;
	}

	static std::optional<ActorActionRecord> UpdateClaimedTerminalState(
		Database &db,
		uint64_t action_id,
		const std::string &sql,
		const std::string &expected_state
	)
	{
		auto results = db.QueryDatabase(sql);
		if (!results.Success() || results.RowsAffected() != 1) {
			return std::nullopt;
		}

		const auto updated = FindOne(db, action_id);
		if (!updated.action_id || (updated.state != expected_state && updated.state != "expired")) {
			return std::nullopt;
		}

		return updated;
	}

	static std::string NullableStringSql(const std::optional<std::string> &value)
	{
		return value.has_value() ? "'" + Strings::Escape(*value) + "'" : "NULL";
	}

	static std::string NullableTimeSql(const std::optional<time_t> &value)
	{
		return value.has_value()
			? "FROM_UNIXTIME(" + std::to_string(*value) + ")"
			: "NULL";
	}
};

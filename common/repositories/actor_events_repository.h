#pragma once

#include "common/json/json.h"
#include "common/repositories/base/base_actor_events_repository.h"

#include <algorithm>
#include <ctime>
#include <optional>
#include <string>
#include <vector>

class ActorEventsRepository: public BaseActorEventsRepository {
public:
	using ActorEventRecord = BaseActorEventsRepository::ActorEvents;

	struct SpeechEmittedEventRecord {
		uint32_t                actor_id = 0;
		std::optional<uint32_t> bot_id;
		std::optional<uint32_t> owner_character_id;
		std::optional<uint32_t> zone_id;
		std::optional<uint32_t> instance_id;
		std::optional<uint32_t> entity_id;
		std::string             channel;
		std::string             text;
		uint32_t                audible_radius = 0;
		time_t                  created_at = 0;
	};

	static ActorEventRecord AppendObservedSpeechEmitted(Database &db, const SpeechEmittedEventRecord &record)
	{
		Json::Value payload;
		payload["channel"] = record.channel;
		payload["text"] = record.text;
		payload["audible_radius"] = record.audible_radius;

		ActorEventRecord event{};
		event.actor_id = record.actor_id;
		event.bot_id = record.bot_id;
		event.owner_character_id = record.owner_character_id;
		event.zone_id = record.zone_id;
		event.instance_id = record.instance_id;
		event.entity_id = record.entity_id;
		event.event_type = "speech_emitted";
		event.event_json = ToCompactJson(payload);
		event.created_at = record.created_at;
		return AppendEvent(db, event);
	}

	static ActorEventRecord AppendEvent(Database &db, ActorEventRecord record)
	{
		if (!record.actor_id || record.event_type.empty() || record.event_json.empty()) {
			return {};
		}

		record.created_at = record.created_at > 0 ? record.created_at : std::time(nullptr);
		record.event_id = 0;
		return InsertOne(db, record);
	}

	static std::optional<ActorEventRecord> FindByEventId(Database &db, uint64_t event_id)
	{
		const auto event = FindOne(db, event_id);
		if (!event.event_id) {
			return std::nullopt;
		}

		return event;
	}

	static std::vector<ActorEventRecord> ReadCursor(Database &db, uint32_t actor_id, uint64_t after_event_id, size_t limit)
	{
		if (limit == 0) {
			return {};
		}

		const auto bounded_limit = std::clamp<size_t>(limit, 1, 1000);
		auto results = db.QueryDatabase(
			fmt::format(
				R"SQL(
SELECT {}
FROM actor_events
WHERE actor_id = {}
  AND event_id > {}
ORDER BY event_id ASC
LIMIT {}
)SQL",
				SelectColumnsRaw(),
				actor_id,
				after_event_id,
				bounded_limit
			)
		);

		std::vector<ActorEventRecord> events;
		if (!results.Success()) {
			return events;
		}

		events.reserve(results.RowCount());
		for (auto row = results.begin(); row != results.end(); ++row) {
			events.push_back(FromRow(row));
		}

		return events;
	}

	static int DeleteByActorId(Database &db, uint32_t actor_id)
	{
		return DeleteWhere(db, fmt::format("actor_id = {}", actor_id));
	}

	static uint64_t LatestGameplayEventId(Database &db, uint32_t actor_id)
	{
		auto results = db.QueryDatabase(fmt::format(
			"SELECT COALESCE(MAX(event_id), 0) FROM actor_events WHERE actor_id = {} "
			"AND event_type NOT IN ('action_completed', 'action_rejected')", actor_id));
		if (!results.Success() || results.RowCount() != 1) {
			return 0;
		}
		auto row = results.begin();
		return row[0] ? strtoull(row[0], nullptr, 10) : 0;
	}

private:
	template <typename RowType>
	static ActorEventRecord FromRow(RowType &row)
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
			.created_at = strtoll(row[9] ? row[9] : "0", nullptr, 10),
		};
	}

	static std::string ToCompactJson(const Json::Value &value)
	{
		Json::StreamWriterBuilder builder;
		builder["indentation"] = "";
		return Json::writeString(builder, value);
	}
};

#include "actor_helper.h"

#include "common/json/json.h"
#include "common/repositories/actor_action_queue_repository.h"
#include "common/repositories/actor_events_repository.h"
#include "common/strings.h"

#include <algorithm>
#include <cstdlib>
#include <ctime>
#include <fstream>
#include <limits>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace {

struct DiscoveredActor {
	uint32_t actor_id = 0;
};

bool IsGameplayEvent(const std::string& type) {
	return type != "action_completed" && type != "action_rejected";
}

std::optional<uint64_t> JsonUInt64(const std::string& document, const char* member) {
	Json::CharReaderBuilder builder;
	std::unique_ptr<Json::CharReader> reader(builder.newCharReader());
	Json::Value root;
	std::string errors;
	if (!reader->parse(document.data(), document.data() + document.size(), &root, &errors) ||
		!root.isObject() || !root.isMember(member) || !root[member].isUInt64()) {
		return std::nullopt;
	}
	return root[member].asUInt64();
}

} // namespace

ActorHelper::ActorHelper(Database& database, Options options) : database_(database), options_(std::move(options)) {
	options_.freshness_seconds = std::clamp<uint32_t>(options_.freshness_seconds, 1, 3600);
	options_.event_limit = std::clamp<size_t>(options_.event_limit, 1, 128);
	options_.discovery_limit = std::clamp<size_t>(options_.discovery_limit, 1, 256);
	discovery_cursor_ = LoadDiscoveryCursor();
}

uint64_t ActorHelper::LoadCursor(uint32_t actor_id) const {
	std::ifstream input(options_.state_directory / (std::to_string(actor_id) + ".cursor"));
	uint64_t cursor = 0;
	if (!(input >> cursor)) {
		return 0;
	}
	return cursor;
}

bool ActorHelper::StoreCursor(uint32_t actor_id, uint64_t cursor) const {
	std::error_code error;
	std::filesystem::create_directories(options_.state_directory, error);
	if (error) {
		return false;
	}
	const auto destination = options_.state_directory / (std::to_string(actor_id) + ".cursor");
	const auto temporary = destination.string() + ".tmp";
	{
		std::ofstream output(temporary, std::ios::trunc);
		if (!(output << cursor << "\n")) {
			return false;
		}
	}
	std::filesystem::rename(temporary, destination, error);
	return !error;
}

uint32_t ActorHelper::LoadDiscoveryCursor() const {
	std::ifstream input(options_.state_directory / "discovery.cursor");
	uint64_t cursor = 0;
	if (!(input >> cursor) || cursor > std::numeric_limits<uint32_t>::max()) {
		return 0;
	}
	return static_cast<uint32_t>(cursor);
}

bool ActorHelper::StoreDiscoveryCursor(uint32_t cursor) const {
	std::error_code error;
	std::filesystem::create_directories(options_.state_directory, error);
	if (error) {
		return false;
	}
	const auto destination = options_.state_directory / "discovery.cursor";
	const auto temporary = destination.string() + ".tmp";
	{
		std::ofstream output(temporary, std::ios::trunc);
		if (!(output << cursor << "\n")) {
			return false;
		}
	}
	std::filesystem::rename(temporary, destination, error);
	return !error;
}

ActorHelper::CycleResult ActorHelper::RunCycle(time_t now) {
	CycleResult result;
	now = now > 0 ? now : std::time(nullptr);
	const auto actor_filter = options_.actor_id.has_value()
		? fmt::format(" AND p.actor_id = {}", *options_.actor_id)
		: std::string();
	const auto zone_filter = options_.zone_id.has_value()
		? fmt::format(" AND s.zone_id = {} AND COALESCE(s.instance_id, 0) = {}",
			*options_.zone_id, options_.instance_id)
		: std::string();
	auto discovery = database_.QueryDatabase(fmt::format(R"SQL(
SELECT p.actor_id
FROM actor_profiles p
JOIN actor_status s ON s.actor_id = p.actor_id
WHERE p.enabled = 1 AND p.actor_type = 'autonomous_actor' AND p.actor_substrate = 'bot'
  AND p.bot_id IS NOT NULL AND p.owner_character_id IS NOT NULL
  AND s.zone_id IS NOT NULL AND s.entity_id IS NOT NULL AND s.state IN ('active', 'idle')
  AND s.heartbeat_at >= FROM_UNIXTIME({} - {}){}{}
ORDER BY (p.actor_id > {}) DESC, p.actor_id
LIMIT {}
)SQL", now, options_.freshness_seconds, actor_filter, zone_filter, discovery_cursor_, options_.discovery_limit));
	if (!discovery.Success()) {
		return result;
	}

	std::vector<DiscoveredActor> actors;
	for (auto row = discovery.begin(); row != discovery.end(); ++row) {
		actors.push_back({static_cast<uint32_t>(strtoul(row[0], nullptr, 10))});
	}
	result.discovered = actors.size();
	if (!actors.empty()) {
		// Persist the rotating scan position before processing actors. A helper
		// that repeatedly restarts after one bounded cycle must still reach later
		// IDs; the cyclic ordering above wraps after reaching the end.
		const auto next_discovery_cursor = actors.back().actor_id;
		if (StoreDiscoveryCursor(next_discovery_cursor)) {
			discovery_cursor_ = next_discovery_cursor;
		}
	}

	for (const auto actor : actors) {
		uint64_t cursor = LoadCursor(actor.actor_id);

		// Completion is the commit point for the cursor. Keeping it behind while an
		// action is pending makes a killed helper recreate the same idempotency key.
		auto terminals = database_.QueryDatabase(fmt::format(R"SQL(
SELECT q.source_metadata_json
FROM actor_action_queue q
WHERE q.actor_id = {} AND q.source = 'actor-helper' AND q.state IN ('completed', 'failed')
  AND EXISTS (
    SELECT 1 FROM actor_events e WHERE e.actor_id = q.actor_id
      AND e.event_type IN ('action_completed', 'action_rejected')
      AND JSON_UNQUOTE(JSON_EXTRACT(e.event_json, '$.action_id')) = CAST(q.action_id AS CHAR)
  )
ORDER BY q.action_id DESC LIMIT 16
)SQL", actor.actor_id));
		uint64_t completed_cursor = cursor;
		if (terminals.Success()) {
			for (auto row = terminals.begin(); row != terminals.end(); ++row) {
				if (row[0]) {
					const auto trigger = JsonUInt64(row[0], "trigger_event_id");
					if (trigger.has_value()) {
						completed_cursor = std::max(completed_cursor, *trigger);
					}
				}
			}
		}
		if (completed_cursor > cursor && StoreCursor(actor.actor_id, completed_cursor)) {
			cursor = completed_cursor;
			++result.outcomes_observed;
		}

		const auto latest = ActorEventsRepository::LatestGameplayEventId(database_, actor.actor_id);
		if (!latest.has_value()) {
			continue;
		}
		if (cursor > 0) {
			auto retained = database_.QueryDatabase(fmt::format(
				"SELECT 1 FROM actor_events WHERE actor_id = {} AND event_id = {} LIMIT 1",
				actor.actor_id, cursor));
			if (!retained.Success()) {
				continue;
			}
			if (retained.RowCount() == 0) {
				StoreCursor(actor.actor_id, *latest);
				++result.retained_event_losses;
				continue;
			}
		}

		const auto events = ActorEventsRepository::ReadCursor(database_, actor.actor_id, cursor, options_.event_limit);
		if (events.empty()) {
			continue;
		}
		if (events.size() == options_.event_limit) {
			const auto more = ActorEventsRepository::ReadCursor(database_, actor.actor_id, events.back().event_id, 1);
			if (!more.empty()) {
				// Catch up a bounded page without deciding from a partial view. This
				// is an explicit cursor-gap outcome, not unbounded replay.
				StoreCursor(actor.actor_id, events.back().event_id);
				++result.cursor_gaps;
				continue;
			}
		}
		auto trigger = std::find_if(events.rbegin(), events.rend(), [](const auto& event) {
			return IsGameplayEvent(event.event_type);
		});
		if (trigger == events.rend()) {
			StoreCursor(actor.actor_id, events.back().event_id);
			continue;
		}

		Json::Value metadata;
		// The executor watermark must describe the gameplay snapshot that selected
		// this action. A separate "latest" read can precede a concurrent append and
		// make a newly selected trigger stale against an older watermark.
		metadata["expected_event_id"] = Json::UInt64(trigger->event_id);
		metadata["trigger_event_id"] = Json::UInt64(trigger->event_id);
		Json::Value action(Json::objectValue);
		Json::StreamWriterBuilder writer;
		writer["indentation"] = "";
		const auto metadata_json = Json::writeString(writer, metadata);
		const auto action_json = Json::writeString(writer, action);
		const auto idempotency_key = fmt::format("actor-helper:stand:{}:{}", actor.actor_id, trigger->event_id);

		// Serialize helper admission on the durable actor profile row. This keeps
		// the active-count check and insert/retry in one database critical section,
		// so helpers with different cursor views cannot both admit work.
		database_.TransactionBegin();
		auto actor_lock = database_.QueryDatabase(fmt::format(
			"SELECT actor_id FROM actor_profiles WHERE actor_id = {} FOR UPDATE", actor.actor_id));
		if (!actor_lock.Success() || actor_lock.RowCount() != 1) {
			database_.TransactionRollback();
			continue;
		}
		ActorActionQueueRepository::ExpireDue(database_, now, actor.actor_id);
		auto active = database_.QueryDatabase(fmt::format(
			"SELECT COUNT(*) FROM actor_action_queue WHERE actor_id = {} AND source = 'actor-helper' "
			"AND state IN ('pending', 'claimed')", actor.actor_id));
		if (!active.Success() || active.RowCount() != 1 || !active.begin()[0]) {
			database_.TransactionRollback();
			continue;
		}
		if (strtoull(active.begin()[0], nullptr, 10) != 0) {
			if (!database_.TransactionCommit().Success()) {
				database_.TransactionRollback();
			}
			++result.busy;
			continue;
		}

		bool admitted = false;
		const auto existing = ActorActionQueueRepository::FindByActorAndIdempotencyKey(
			database_, actor.actor_id, idempotency_key);
		if (existing.has_value() && existing->state == "expired") {
			// Expiration is guaranteed to precede gameplay application in the zone
			// executor, so the same durable request can safely become pending again.
			auto retried = database_.QueryDatabase(fmt::format(R"SQL(
UPDATE actor_action_queue
SET state = 'pending', source_metadata_json = '{}', action_json = '{}',
    not_before = NULL, expires_at = FROM_UNIXTIME({}), claimed_by = NULL,
    claimed_at = NULL, completed_at = NULL, failure_reason = NULL,
    result_json = NULL, updated_at = FROM_UNIXTIME({})
WHERE action_id = {} AND state = 'expired'
)SQL", Strings::Escape(metadata_json), Strings::Escape(action_json), now + 15, now, existing->action_id));
			admitted = retried.Success() && retried.RowsAffected() == 1;
		} else if (!existing.has_value()) {
			const auto queued = ActorActionQueueRepository::Enqueue(database_, {
				.actor_id = actor.actor_id,
				.source = "actor-helper",
				.source_metadata_json = metadata_json,
				.action_type = "stand",
				.action_json = action_json,
				.idempotency_key = idempotency_key,
				.expires_at = now + 15,
				.created_at = now,
			});
			admitted = queued.action_id != 0;
		}
		if (!database_.TransactionCommit().Success()) {
			database_.TransactionRollback();
			admitted = false;
		}
		if (admitted) {
			++result.enqueued;
		}
	}
	return result;
}

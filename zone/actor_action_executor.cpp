#include "actor_action_executor.h"

#include "bot.h"
#include "entity.h"
#include "zonedb.h"

#include "common/json/json.h"
#include "common/repositories/actor_action_queue_repository.h"
#include "common/repositories/actor_events_repository.h"
#include "common/repositories/actor_profiles_repository.h"
#include "common/repositories/actor_status_repository.h"

#include <ctime>
#include <memory>
#include <utility>

extern EntityList entity_list;

namespace {

bool ParseObject(const std::string& document, Json::Value& root) {
	Json::CharReaderBuilder builder;
	std::unique_ptr<Json::CharReader> reader(builder.newCharReader());
	std::string errors;
	return reader->parse(document.data(), document.data() + document.size(), &root, &errors) && root.isObject();
}

void AppendOutcome(ZoneDatabase& database, const ActorActionQueueRepository::ActorActionRecord& action,
				   const ActorProfilesRepository::ActorProfileRecord& profile,
				   const ActorStatusRepository::ActorStatusRecord& status, const std::string& event_type,
				   const std::string& reason, time_t now) {
	Json::Value payload;
	payload["action_id"] = Json::UInt64(action.action_id);
	payload["action_type"] = action.action_type;
	payload["reason"] = reason;
	Json::StreamWriterBuilder writer;
	writer["indentation"] = "";
	ActorEventsRepository::AppendEvent(database, {
		.actor_id = action.actor_id,
		.bot_id = profile.bot_id,
		.owner_character_id = profile.owner_character_id,
		.zone_id = status.zone_id,
		.instance_id = status.instance_id,
		.entity_id = status.entity_id,
		.event_type = event_type,
		.event_json = Json::writeString(writer, payload),
		.created_at = now,
	});
}

} // namespace

ActorActionExecutor::ActorActionExecutor(ZoneDatabase& database, uint32_t zone_id, uint32_t instance_id,
										 uint32_t zone_server_id, Clock clock)
	: database_(database), zone_id_(zone_id), instance_id_(instance_id),
	  claimant_(fmt::format("zone:{}:{}:{}", zone_server_id, zone_id, instance_id)), clock_(std::move(clock)) {
}

void ActorActionExecutor::ProcessOne() {
	const auto now = clock_();
	ActorActionQueueRepository::ExpireDue(database_, now);
	std::optional<ActorActionQueueRepository::ActorActionRecord> action;
	for (const auto &[entity_id, bot]: entity_list.GetBotList()) {
		if (!bot) {
			continue;
		}
		const auto profile = ActorProfilesRepository::FindByBotId(database_, bot->GetBotID());
		if (!profile.has_value() || !profile->enabled || profile->actor_type != "autonomous_actor" ||
			profile->actor_substrate != "bot" || !profile->owner_character_id.has_value() ||
			bot->GetBotOwnerCharacterID() != *profile->owner_character_id) {
			continue;
		}
		const auto status = ActorStatusRepository::FindByActorId(database_, profile->actor_id);
		if (!status.has_value() || status->zone_id != zone_id_ || status->instance_id.value_or(0) != instance_id_ ||
			status->entity_id != entity_id || !status->heartbeat_at.has_value() || *status->heartbeat_at < now - 30 ||
			(status->state != "active" && status->state != "idle")) {
			continue;
		}
		action = ActorActionQueueRepository::ClaimNextEligibleForZone(database_, {
			.actor_id = profile->actor_id,
			.bot_id = bot->GetBotID(),
			.owner_character_id = *profile->owner_character_id,
			.zone_id = zone_id_,
			.instance_id = instance_id_,
			.entity_id = entity_id,
			.claimed_by = claimant_,
			.now = now,
		});
		if (action.has_value()) {
			break;
		}
	}
	if (!action.has_value()) {
		return;
	}

	const auto profile = ActorProfilesRepository::FindByActorId(database_, action->actor_id);
	const auto status = ActorStatusRepository::FindByActorId(database_, action->actor_id);
	auto reject = [&](const std::string& reason) {
		const auto terminal_at = clock_();
		const auto terminal = ActorActionQueueRepository::MarkFailed(database_, {action->action_id, reason, terminal_at});
		if (terminal.has_value() && terminal->state == "failed" && profile.has_value() && status.has_value()) {
			AppendOutcome(database_, *action, *profile, *status, "action_rejected", reason, terminal_at);
		}
	};

	if (!profile.has_value() || !status.has_value() || !profile->enabled ||
		profile->actor_type != "autonomous_actor" || profile->actor_substrate != "bot" ||
		!profile->bot_id.has_value() || !profile->owner_character_id.has_value() || !status->zone_id.has_value() ||
		!status->entity_id.has_value() || !status->heartbeat_at.has_value() || *status->heartbeat_at < now - 30 ||
		(status->state != "active" && status->state != "idle")) {
		reject("actor_binding_changed");
		return;
	}
	auto* bot = entity_list.GetBotByBotID(*profile->bot_id);
	if (!bot || bot->GetID() != *status->entity_id || bot->GetBotOwnerCharacterID() != *profile->owner_character_id ||
		*status->zone_id != zone_id_ || status->instance_id.value_or(0) != instance_id_) {
		reject("actor_not_owned_by_zone");
		return;
	}

	if (action->source_metadata_json.has_value()) {
		Json::Value metadata;
		if (!ParseObject(*action->source_metadata_json, metadata)) {
			reject("invalid_source_metadata");
			return;
		}
		if (metadata.isMember("expected_event_id") &&
			metadata["expected_event_id"].asUInt64() !=
				ActorEventsRepository::LatestGameplayEventId(database_, action->actor_id)) {
			reject("stale_event_watermark");
			return;
		}
	}

	Json::Value body;
	if (!ParseObject(action->action_json, body)) {
		reject("invalid_action_json");
		return;
	}
	Mob* target = nullptr;
	if (action->action_type == "target") {
		const auto target_id = body.get("entity_id", 0).asUInt();
		target = target_id <= UINT16_MAX ? entity_list.GetMob(static_cast<uint16_t>(target_id)) : nullptr;
		if (!target || target == bot) {
			reject("illegal_target");
			return;
		}
	} else if (action->action_type != "stand") {
		reject("unsupported_action_type");
		return;
	}

	Json::Value result;
	result["applied"] = true;
	Json::StreamWriterBuilder writer;
	writer["indentation"] = "";
	const auto terminal_at = clock_();
	const auto terminal = ActorActionQueueRepository::MarkCompleted(database_, {
		action->action_id,
		Json::writeString(writer, result),
		terminal_at,
	});
	if (!terminal.has_value() || terminal->state != "completed") {
		return;
	}

	if (action->action_type == "target") {
		bot->SetTarget(target);
	} else {
		bot->Stand();
	}
	AppendOutcome(database_, *action, *profile, *status, "action_completed", "applied", terminal_at);
}

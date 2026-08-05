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

bool AppendOutcome(ZoneDatabase& database, const ActorActionQueueRepository::ActorActionRecord& action,
				   const ActorProfilesRepository::ActorProfileRecord* profile,
				   const ActorStatusRepository::ActorStatusRecord* status, const std::string& event_type,
				   const std::string& reason, time_t now) {
	Json::Value payload;
	payload["action_id"] = Json::UInt64(action.action_id);
	payload["action_type"] = action.action_type;
	payload["reason"] = reason;
	Json::StreamWriterBuilder writer;
	writer["indentation"] = "";
	return ActorEventsRepository::AppendEvent(
			   database,
			   {
				   .actor_id = action.actor_id,
				   .bot_id = profile ? profile->bot_id : std::nullopt,
				   .owner_character_id = profile ? profile->owner_character_id : std::nullopt,
				   .zone_id = status ? status->zone_id : std::nullopt,
				   .instance_id = status ? status->instance_id : std::nullopt,
				   .entity_id = status ? status->entity_id : std::nullopt,
				   .event_type = event_type,
				   .event_json = Json::writeString(writer, payload),
				   .created_at = now,
			   })
			   .event_id != 0;
}

} // namespace

ActorActionExecutor::ActorActionExecutor(ZoneDatabase& database, uint32_t zone_id, uint32_t instance_id,
										 uint32_t zone_server_id, Clock clock,
										 GameplayEventWatermarkReader watermark_reader)
	: database_(database), zone_id_(zone_id), instance_id_(instance_id),
	  claimant_(fmt::format("zone:{}:{}:{}", zone_server_id, zone_id, instance_id)), clock_(std::move(clock)),
	  watermark_reader_(std::move(watermark_reader)) {
}

void ActorActionExecutor::ProcessOne() {
	const auto now = clock_();
	ActorActionQueueRepository::ExpireDue(database_, now);
	std::optional<ActorActionQueueRepository::ActorActionRecord> action;
	const auto candidates =
		ActorActionQueueRepository::FindEligibleForZone(database_, zone_id_, instance_id_, now, 32, candidate_offset_);
	for (const auto& candidate : candidates) {
		const auto candidate_profile = ActorProfilesRepository::FindByActorId(database_, candidate.actor_id);
		const auto candidate_status = ActorStatusRepository::FindByActorId(database_, candidate.actor_id);
		if (!candidate_profile.has_value() || !candidate_status.has_value() || !candidate_profile->bot_id.has_value() ||
			!candidate_profile->owner_character_id.has_value() || !candidate_status->entity_id.has_value()) {
			continue;
		}
		auto* candidate_bot = entity_list.GetBotByBotID(*candidate_profile->bot_id);
		if (!candidate_bot || candidate_bot->GetID() != *candidate_status->entity_id ||
			candidate_bot->GetBotOwnerCharacterID() != *candidate_profile->owner_character_id) {
			continue;
		}
		action = ActorActionQueueRepository::ClaimNextEligibleForZone(
			database_, {
						   .actor_id = candidate.actor_id,
						   .bot_id = *candidate_profile->bot_id,
						   .owner_character_id = *candidate_profile->owner_character_id,
						   .zone_id = zone_id_,
						   .instance_id = instance_id_,
						   .entity_id = *candidate_status->entity_id,
						   .claimed_by = claimant_,
						   .now = now,
					   });
		if (action.has_value()) {
			break;
		}
	}
	if (!action.has_value()) {
		candidate_offset_ = candidates.empty() ? 0 : candidate_offset_ + candidates.size();
		return;
	}
	candidate_offset_ = 0;

	const auto profile = ActorProfilesRepository::FindByActorId(database_, action->actor_id);
	const auto status = ActorStatusRepository::FindByActorId(database_, action->actor_id);
	auto reject = [&](const std::string& reason) {
		const auto terminal_at = clock_();
		database_.TransactionBegin();
		const auto terminal =
			ActorActionQueueRepository::MarkFailed(database_, {action->action_id, reason, terminal_at});
		const auto outcome_persisted =
			terminal.has_value() &&
			(terminal->state == "expired" ||
			 (terminal->state == "failed" &&
			  (ActorEventsRepository::HasActionOutcome(database_, action->actor_id, action->action_id) ||
			   AppendOutcome(database_, *action, profile ? &*profile : nullptr, status ? &*status : nullptr,
							 "action_rejected", reason, terminal_at))));
		if (!outcome_persisted || !database_.TransactionCommit().Success()) {
			database_.TransactionRollback();
			ActorActionQueueRepository::ReleaseClaim(database_, action->action_id, claimant_);
		}
	};

	if (!profile.has_value() || !status.has_value() || !profile->enabled || profile->actor_type != "autonomous_actor" ||
		profile->actor_substrate != "bot" || !profile->bot_id.has_value() || !profile->owner_character_id.has_value() ||
		!status->zone_id.has_value() || !status->entity_id.has_value() || !status->heartbeat_at.has_value() ||
		*status->heartbeat_at < now - 30 || (status->state != "active" && status->state != "idle")) {
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
		if (metadata.isMember("expected_event_id")) {
			if (!metadata["expected_event_id"].isUInt64()) {
				reject("stale_event_watermark");
				return;
			}
			const auto latest_event_id =
				watermark_reader_ ? watermark_reader_(action->actor_id)
								  : ActorEventsRepository::LatestGameplayEventId(database_, action->actor_id);
			if (!latest_event_id.has_value()) {
				ActorActionQueueRepository::ReleaseClaim(database_, action->action_id, claimant_);
				return;
			}
			if (metadata["expected_event_id"].asUInt64() != *latest_event_id) {
				reject("stale_event_watermark");
				return;
			}
		}
	}

	Json::Value body;
	if (!ParseObject(action->action_json, body)) {
		reject("invalid_action_json");
		return;
	}
	Mob* target = nullptr;
	if (action->action_type == "target") {
		if (!body.isMember("entity_id") || !body["entity_id"].isUInt()) {
			reject("invalid_action_json");
			return;
		}
		const auto target_id = body["entity_id"].asUInt();
		if (target_id == 0 || target_id > UINT16_MAX) {
			reject("invalid_action_json");
			return;
		}
		target = entity_list.GetMob(static_cast<uint16_t>(target_id));
		if (!target || target == bot || !target->IsTargetable() || target->IsInvisible(bot)) {
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
	const auto lock_requested_at = clock_();
	if (action->expires_at.has_value() && *action->expires_at <= lock_requested_at) {
		ActorActionQueueRepository::ExpireDue(database_, lock_requested_at, action->actor_id);
		return;
	}
	database_.TransactionBegin();
	if (!ActorActionQueueRepository::LockClaimForExecution(database_,
														   {
															   .action_id = action->action_id,
															   .actor_id = action->actor_id,
															   .bot_id = *profile->bot_id,
															   .owner_character_id = *profile->owner_character_id,
															   .zone_id = zone_id_,
															   .instance_id = instance_id_,
															   .entity_id = *status->entity_id,
															   .claimed_by = claimant_,
															   .now = lock_requested_at,
														   })) {
		database_.TransactionRollback();
		ActorActionQueueRepository::ExpireDue(database_, lock_requested_at, action->actor_id);
		ActorActionQueueRepository::ReleaseClaim(database_, action->action_id, claimant_);
		return;
	}
	const auto applied_at = clock_();
	if (action->expires_at.has_value() && *action->expires_at <= applied_at) {
		const auto expired = ActorActionQueueRepository::ExpireDue(database_, applied_at, action->actor_id) > 0;
		if (!expired || !database_.TransactionCommit().Success()) {
			database_.TransactionRollback();
			ActorActionQueueRepository::ReleaseClaim(database_, action->action_id, claimant_);
		}
		return;
	}
	if (action->action_type == "target") {
		bot->SetTarget(target);
	} else {
		bot->Stand();
	}
	const auto terminal = ActorActionQueueRepository::MarkCompleted(
		database_, {action->action_id, Json::writeString(writer, result), applied_at});
	const auto outcome_persisted =
		terminal.has_value() && terminal->state == "completed" &&
		(ActorEventsRepository::HasActionOutcome(database_, action->actor_id, action->action_id) ||
		 AppendOutcome(database_, *action, &*profile, &*status, "action_completed", "applied", applied_at));
	if (!outcome_persisted || !database_.TransactionCommit().Success()) {
		database_.TransactionRollback();
		ActorActionQueueRepository::ReleaseClaim(database_, action->action_id, claimant_);
	}
}

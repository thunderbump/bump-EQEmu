#include "actor_action_executor.h"

#include "bot.h"
#include "client.h"
#include "entity.h"
#include "groups.h"
#include "npc.h"
#include "zonedb.h"

#include "common/json/json.h"
#include "common/repositories/actor_action_queue_repository.h"
#include "common/repositories/actor_events_repository.h"
#include "common/repositories/actor_profiles_repository.h"
#include "common/repositories/actor_status_repository.h"

#include <algorithm>
#include <cmath>
#include <ctime>
#include <list>
#include <memory>
#include <utility>
#include <vector>

extern EntityList entity_list;

namespace {

bool ParseObject(const std::string& document, Json::Value& root) {
	Json::CharReaderBuilder builder;
	std::unique_ptr<Json::CharReader> reader(builder.newCharReader());
	std::string errors;
	return reader->parse(document.data(), document.data() + document.size(), &root, &errors) && root.isObject();
}

std::vector<ActorActionExecutor*> active_executors;

constexpr uint32_t kMistyZoneId = 33;
constexpr float kMistyHuntMinX = -2400.0f;
constexpr float kMistyHuntMaxX = -1900.0f;
constexpr float kMistyHuntMinY = 100.0f;
constexpr float kMistyHuntMaxY = 900.0f;
constexpr float kMistyHuntRadius = 600.0f;

bool IsMistyHuntNpcType(uint32_t npc_type_id) {
	return npc_type_id == 33005 || npc_type_id == 33160 || npc_type_id == 33024;
}

bool IsWithinMistyHuntBounds(const Mob* mob) {
	return mob && mob->GetX() >= kMistyHuntMinX && mob->GetX() <= kMistyHuntMaxX &&
		mob->GetY() >= kMistyHuntMinY && mob->GetY() <= kMistyHuntMaxY;
}

bool IsClaimedByPlayer(NPC* target) {
	if (!target) {
		return false;
	}
	for (const auto* entry : target->GetHateList()) {
		if (!entry || !entry->entity_on_hatelist) {
			continue;
		}
		auto* player_owner = entry->entity_on_hatelist->GetUltimateOwner();
		// Existing player-owned hate is contention even when it belongs to the
		// Actor's owner. Ownership does not prove that the owner, one of their
		// pets, or another Bot is participating in this hunt.
		if (player_owner && player_owner->IsClient()) {
			return true;
		}
	}
	return false;
}

bool AppendOutcome(ZoneDatabase& database, const ActorActionQueueRepository::ActorActionRecord& action,
				   const ActorProfilesRepository::ActorProfileRecord* profile,
				   const ActorStatusRepository::ActorStatusRecord* status, const std::string& event_type,
				   const std::string& reason, time_t now, NPC* target = nullptr, uint16_t target_entity_id = 0,
				   uint32_t target_npc_type_id = 0, uint16_t killer_entity_id = 0,
				   uint64_t target_runtime_instance_id = 0) {
	Json::Value payload;
	payload["action_id"] = Json::UInt64(action.action_id);
	payload["action_type"] = action.action_type;
	payload["reason"] = reason;
	if (target || target_entity_id) {
		payload["target_entity_id"] = target ? target->GetID() : target_entity_id;
		payload["target_npc_type_id"] = target ? target->GetNPCTypeID() : target_npc_type_id;
		payload["target_runtime_instance_id"] = Json::UInt64(
			target ? target->GetRuntimeInstanceID() : target_runtime_instance_id);
	}
	if (killer_entity_id) {
		payload["killer_entity_id"] = killer_entity_id;
	}
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

struct ActorActionExecutor::HuntEngagement {
	struct PartyBotIdentity {
		uint16_t entity_id = 0;
		uint32_t bot_id = 0;
		uint32_t owner_character_id = 0;
		uint64_t runtime_instance_id = 0;
	};

	ActorActionQueueRepository::ActorActionRecord action;
	ActorProfilesRepository::ActorProfileRecord profile;
	ActorStatusRepository::ActorStatusRecord status;
	uint16_t target_entity_id = 0;
	uint32_t target_npc_type_id = 0;
	uint64_t target_runtime_instance_id = 0;
	std::vector<PartyBotIdentity> party_bots;
	bool target_attack_flag_added = false;
	std::optional<time_t> death_observed_at;
	uint16_t killer_entity_id = 0;
};

ActorActionExecutor::ActorActionExecutor(ZoneDatabase& database, uint32_t zone_id, uint32_t instance_id,
										 uint32_t zone_server_id, Clock clock,
										 GameplayEventWatermarkReader watermark_reader)
	: database_(database), zone_id_(zone_id), instance_id_(instance_id),
	  claimant_(fmt::format("zone:{}:{}:{}", zone_server_id, zone_id, instance_id)), clock_(std::move(clock)),
	  watermark_reader_(std::move(watermark_reader)) {
	active_executors.push_back(this);
}

ActorActionExecutor::~ActorActionExecutor() {
	CancelHuntCombat();
	active_executors.erase(std::remove(active_executors.begin(), active_executors.end(), this), active_executors.end());
}

void ActorActionExecutor::CancelHuntCombat() {
	if (!hunt_engagement_) {
		return;
	}
	const auto target_id = hunt_engagement_->target_entity_id;
	auto* target_candidate = entity_list.GetNPCByID(target_id);
	auto* target = target_candidate &&
			target_candidate->GetRuntimeInstanceID() == hunt_engagement_->target_runtime_instance_id
		? target_candidate
		: nullptr;
	auto remove_hunt_aggro = [target](Mob* attacker) {
		if (!attacker) {
			return;
		}
		if (target) {
			attacker->RemoveFromHateList(target);
			attacker->RemoveFromRampageList(target);
			target->RemoveFromHateList(attacker);
			target->RemoveFromRampageList(attacker);
		}
		if (target && attacker->GetTarget() == target) {
			attacker->SetTarget(nullptr);
		}
	};
	for (const auto& identity : hunt_engagement_->party_bots) {
		auto* party_member = entity_list.GetMob(identity.entity_id);
		auto* party_bot = party_member && party_member->IsBot() ? party_member->CastToBot() : nullptr;
		if (!party_bot || party_bot->GetBotID() != identity.bot_id ||
			party_bot->GetBotOwnerCharacterID() != identity.owner_character_id ||
			party_bot->GetRuntimeInstanceID() != identity.runtime_instance_id) {
			continue;
		}
		remove_hunt_aggro(party_bot);
		// Bot::SetOwnerTarget can enlist a controllable pet in the same ordinary
		// combat. Remove only this hunt target so unrelated pet hate is retained.
		remove_hunt_aggro(party_bot->GetPet());
		if (hunt_engagement_->status.entity_id.has_value() &&
			party_bot->IsCommandTargetSource(*hunt_engagement_->status.entity_id)) {
			party_bot->ClearAttackCommandFlags();
			party_bot->ClearCommandTargetSource();
		}
	}
	if (hunt_engagement_->target_attack_flag_added && target) {
		target->RemoveBotAttackFlag(*hunt_engagement_->profile.owner_character_id);
	}
}

void ActorActionExecutor::ObserveNpcDeath(
	uint16_t entity_id, uint32_t npc_type_id, uint64_t runtime_instance_id, uint16_t killer_entity_id) {
	if (!entity_id) {
		return;
	}
	for (auto* executor : active_executors) {
		if (!executor || !executor->hunt_engagement_ || executor->hunt_engagement_->target_entity_id != entity_id ||
			executor->hunt_engagement_->target_npc_type_id != npc_type_id ||
			executor->hunt_engagement_->target_runtime_instance_id != runtime_instance_id) {
			continue;
		}
		const auto observed_at = executor->clock_();
		if (executor->hunt_engagement_->action.expires_at.has_value() &&
			*executor->hunt_engagement_->action.expires_at <= observed_at) {
			continue;
		}
		if (!executor->hunt_engagement_->death_observed_at.has_value() ||
			observed_at < *executor->hunt_engagement_->death_observed_at) {
			executor->hunt_engagement_->death_observed_at = observed_at;
			executor->hunt_engagement_->killer_entity_id = killer_entity_id;
		}
	}
}

void ActorActionExecutor::ProcessHuntEngagement(time_t now) {
	if (!hunt_engagement_) {
		return;
	}
	auto engagement = *hunt_engagement_;
	const auto lookup = ActorActionQueueRepository::LookupByActionId(database_, engagement.action.action_id);
	if (!lookup.succeeded) {
		// The durable owner is unknown, so retain the engagement for retry. The
		// locally known deadline still bounds gameplay during a database outage.
		if (engagement.action.expires_at.has_value() && *engagement.action.expires_at <= now) {
			CancelHuntCombat();
		}
		return;
	}
	if (!lookup.action.has_value() || lookup.action->state != "claimed") {
		CancelHuntCombat();
		hunt_engagement_.reset();
		return;
	}

	const bool death_observed = engagement.death_observed_at.has_value();
	std::string failure_reason;
	if (!death_observed && engagement.action.expires_at.has_value() &&
		*engagement.action.expires_at <= now) {
		const auto expired = ActorActionQueueRepository::ExpireDue(database_, now, engagement.action.actor_id) > 0;
		// Stop gameplay at the deadline even when persistence is temporarily
		// unavailable. Retain the engagement so terminalization can retry.
		CancelHuntCombat();
		if (expired) {
			hunt_engagement_.reset();
		}
		return;
	}

	auto* target = entity_list.GetNPCByID(engagement.target_entity_id);
	if (!death_observed && (!target || target->GetNPCTypeID() != engagement.target_npc_type_id ||
		target->GetRuntimeInstanceID() != engagement.target_runtime_instance_id)) {
		failure_reason = "hunt_target_lost";
	} else if (!death_observed && target && !target->HasDied() && target->GetHP() > 0) {
		bool selected_target_combat_active = false;
		for (const auto& identity : engagement.party_bots) {
			auto* member = entity_list.GetMob(identity.entity_id);
			auto* party_bot = member && member->IsBot() ? member->CastToBot() : nullptr;
			if (!party_bot || party_bot->GetBotID() != identity.bot_id ||
				party_bot->GetBotOwnerCharacterID() != identity.owner_character_id ||
				party_bot->GetRuntimeInstanceID() != identity.runtime_instance_id) {
				continue;
			}
			auto* bot_owner = party_bot->GetBotOwner();
			auto* command_source = party_bot->GetCommandTargetSource(
				bot_owner && bot_owner->IsClient() ? bot_owner->CastToClient() : nullptr);
			const bool hunt_command_pending = party_bot->GetAttackFlag() && command_source &&
				engagement.status.entity_id.has_value() && command_source->GetID() == *engagement.status.entity_id;
			if (hunt_command_pending || (party_bot->IsEngaged() && party_bot->CheckAggro(target))) {
				selected_target_combat_active = true;
				break;
			}
		}
		if (selected_target_combat_active) {
			return;
		}
		failure_reason = "hunt_combat_ended";
	} else if (!death_observed) {
		// A zero-HP target is not success until NPC::Death reports its authoritative completion.
		return;
	}

	database_.TransactionBegin();
	bool outcome_persisted = false;
	if (failure_reason.empty()) {
		Json::Value result;
		result["applied"] = true;
		result["outcome"] = "succeeded";
		result["target_entity_id"] = engagement.target_entity_id;
		result["target_npc_type_id"] = engagement.target_npc_type_id;
		result["target_runtime_instance_id"] = Json::UInt64(engagement.target_runtime_instance_id);
		result["killer_entity_id"] = engagement.killer_entity_id;
		Json::StreamWriterBuilder writer;
		writer["indentation"] = "";
		// Completion is governed by the authoritative death time, not by a later
		// executor retry. An in-deadline kill remains success across zone delay or
		// a transient persistence outage after NPC::Death has reported it.
		const auto completed_at = *engagement.death_observed_at;
		const auto completed = ActorActionQueueRepository::MarkCompleted(
			database_, {engagement.action.action_id, Json::writeString(writer, result), completed_at});
		outcome_persisted =
			completed.has_value() && completed->state == "completed" &&
			AppendOutcome(database_, engagement.action, &engagement.profile, &engagement.status, "hunt_succeeded",
						  "selected_target_death", completed_at, nullptr, engagement.target_entity_id,
						  engagement.target_npc_type_id, engagement.killer_entity_id,
						  engagement.target_runtime_instance_id);
	} else {
		const auto failed =
			ActorActionQueueRepository::MarkFailed(database_, {engagement.action.action_id, failure_reason, now});
		outcome_persisted =
			failed.has_value() && failed->state == "failed" &&
			AppendOutcome(database_, engagement.action, &engagement.profile, &engagement.status, "hunt_failed",
						  failure_reason, now, nullptr, engagement.target_entity_id, engagement.target_npc_type_id, 0,
						  engagement.target_runtime_instance_id);
	}
	if (!outcome_persisted || !database_.TransactionCommit().Success()) {
		database_.TransactionRollback();
		return;
	}
	CancelHuntCombat();
	hunt_engagement_.reset();
}

void ActorActionExecutor::ProcessOne() {
	const auto now = clock_();
	ProcessHuntEngagement(now);
	// An authoritative in-deadline death may still be claimed while its atomic
	// completion/outcome transaction retries. Expire unrelated work, but do not
	// let the generic sweep destroy the retained success evidence.
	const auto completion_retry_action_id =
		hunt_engagement_ && hunt_engagement_->death_observed_at.has_value()
			? std::optional<uint64_t>(hunt_engagement_->action.action_id)
			: std::nullopt;
	ActorActionQueueRepository::ExpireDue(database_, now, std::nullopt, completion_retry_action_id);
	if (hunt_engagement_) {
		return;
	}
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
		if (metadata.isMember("expected_binding")) {
			const auto& binding = metadata["expected_binding"];
			if (!binding.isObject() || !binding["actor_id"].isUInt() || !binding["bot_id"].isUInt() ||
				!binding["owner_character_id"].isUInt() || !binding["zone_id"].isUInt() ||
				!binding["instance_id"].isUInt() || !binding["entity_id"].isUInt()) {
				reject("invalid_source_metadata");
				return;
			}
			if (binding["actor_id"].asUInt() != action->actor_id || binding["bot_id"].asUInt() != *profile->bot_id ||
				binding["owner_character_id"].asUInt() != *profile->owner_character_id ||
				binding["zone_id"].asUInt() != *status->zone_id ||
				binding["instance_id"].asUInt() != status->instance_id.value_or(0) ||
				binding["entity_id"].asUInt() != *status->entity_id) {
				reject("actor_binding_changed");
				return;
			}
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
	NPC* hunt_target = nullptr;
	std::list<Bot*> hunt_party_bots;
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
	} else if (action->action_type == "hunt_one_allowlisted_target") {
		if (zone_id_ != kMistyZoneId || body.size() != 1 || !body["area"].isString() ||
			body["area"].asString() != "misty-local-v1" || !action->expires_at.has_value()) {
			reject("illegal_hunt_request");
			return;
		}
		auto* hunt_group = bot->GetGroup();
		if (!hunt_group) {
			reject("actor_not_ready");
			return;
		}
		hunt_group->GetBotList(hunt_party_bots);
		const auto actor_member = std::find(hunt_party_bots.begin(), hunt_party_bots.end(), bot);
		const bool owned_materialized_party = actor_member != hunt_party_bots.end() && hunt_party_bots.size() >= 2 &&
			std::all_of(hunt_party_bots.begin(), hunt_party_bots.end(), [&](Bot* party_bot) {
				return party_bot && party_bot->GetGroup() == hunt_group &&
					party_bot->GetBotOwnerCharacterID() == *profile->owner_character_id &&
					IsWithinMistyHuntBounds(party_bot);
			});
		if (!owned_materialized_party ||
			std::any_of(hunt_party_bots.begin(), hunt_party_bots.end(), [](Bot* party_bot) {
				auto* controllable_pet = party_bot->HasControllablePet(BotAnimEmpathy::Attack)
					? party_bot->GetPet()
					: nullptr;
				return party_bot->HasDied() || party_bot->GetHP() <= 0 || party_bot->IsEngaged() ||
					party_bot->GetAttackFlag() || party_bot->GetAttackingFlag() || party_bot->GetPullFlag() ||
					party_bot->GetPullingFlag() || party_bot->GetReturningFlag() ||
					(controllable_pet && controllable_pet->IsEngaged());
			})) {
			reject("actor_not_ready");
			return;
		}

		bool claimed_candidate = false;
		float nearest_distance_squared = kMistyHuntRadius * kMistyHuntRadius;
		for (const auto& [entity_id, candidate] : entity_list.GetNPCList()) {
			if (!candidate || !IsMistyHuntNpcType(candidate->GetNPCTypeID()) || candidate->HasDied() ||
				candidate->GetHP() <= 0 || !candidate->IsTargetable() || candidate->IsInvisible(bot) ||
				!bot->IsAttackAllowed(candidate) || candidate->GetX() < kMistyHuntMinX ||
				candidate->GetX() > kMistyHuntMaxX || candidate->GetY() < kMistyHuntMinY ||
				candidate->GetY() > kMistyHuntMaxY) {
				continue;
			}
			const auto dx = candidate->GetX() - bot->GetX();
			const auto dy = candidate->GetY() - bot->GetY();
			const auto distance_squared = dx * dx + dy * dy;
			if (distance_squared > nearest_distance_squared) {
				continue;
			}
			if (IsClaimedByPlayer(candidate)) {
				claimed_candidate = true;
				continue;
			}
			if (!hunt_target || distance_squared < nearest_distance_squared ||
				(distance_squared == nearest_distance_squared && entity_id < hunt_target->GetID())) {
				hunt_target = candidate;
				nearest_distance_squared = distance_squared;
			}
		}
		if (!hunt_target) {
			reject(claimed_candidate ? "target_claimed_by_player" : "no_eligible_hunt_target");
			return;
		}
		target = hunt_target;
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
	} else if (action->action_type == "hunt_one_allowlisted_target") {
		// Persist the execution state before mutating combat. A failed/uncertain
		// transaction must never leave the party attacking without durable evidence.
		const auto committed = AppendOutcome(database_, *action, &*profile, &*status, "hunt_engagement_committed",
											 "ordinary_bot_combat", applied_at, hunt_target);
		if (!committed || !database_.TransactionCommit().Success()) {
			database_.TransactionRollback();
			ActorActionQueueRepository::ReleaseClaim(database_, action->action_id, claimant_);
			return;
		}

		// The transaction can finish after the bounded request expires. Do not
		// install combat intent merely because engagement evidence committed.
		const auto combat_started_at = clock_();
		if (action->expires_at.has_value() && *action->expires_at <= combat_started_at) {
			ActorActionQueueRepository::ExpireDue(database_, combat_started_at, action->actor_id);
			return;
		}

		const auto attack_flags = hunt_target->GetBotAttackFlags();
		const bool target_attack_flag_added =
			std::find(attack_flags.begin(), attack_flags.end(), *profile->owner_character_id) == attack_flags.end();
		std::vector<HuntEngagement::PartyBotIdentity> party_bots;
		party_bots.reserve(hunt_party_bots.size());
		for (auto* party_bot : hunt_party_bots) {
			party_bots.push_back({
				.entity_id = party_bot->GetID(),
				.bot_id = party_bot->GetBotID(),
				.owner_character_id = party_bot->GetBotOwnerCharacterID(),
				.runtime_instance_id = party_bot->GetRuntimeInstanceID(),
			});
		}
		hunt_engagement_ = std::make_unique<HuntEngagement>(HuntEngagement{
			.action = *action,
			.profile = *profile,
			.status = *status,
			.target_entity_id = hunt_target->GetID(),
			.target_npc_type_id = hunt_target->GetNPCTypeID(),
			.target_runtime_instance_id = hunt_target->GetRuntimeInstanceID(),
			.party_bots = std::move(party_bots),
			.target_attack_flag_added = target_attack_flag_added,
		});

		// This is the same target/attack intent consumed by ordinary Bot AI. Combat,
		// movement, spell checks, damage and death remain authoritative gameplay.
		if (target_attack_flag_added) {
			hunt_target->SetBotAttackFlag(*profile->owner_character_id);
		}
		bot->SetCommandTargetSource(bot);
		bot->SetTarget(hunt_target);
		bot->SetAttackFlag();
		for (auto* party_bot : hunt_party_bots) {
			if (party_bot != bot) {
				party_bot->SetCommandTargetSource(bot);
				party_bot->SetAttackFlag();
			}
		}
		return;
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

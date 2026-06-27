/*	EQEmu: EQEmulator

	Copyright (C) 2001-2026 EQEmu Development Team

	This program is free software; you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation; either version 3 of the License, or
	(at your option) any later version.

	This program is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
	GNU General Public License for more details.

	You should have received a copy of the GNU General Public License
	along with this program. If not, see <http://www.gnu.org/licenses/>.
*/
#include "world/world_server_cli.h"

#include "common/eqemu_logsys.h"
#include "common/repositories/actor_action_queue_repository.h"
#include "common/repositories/actor_profiles_repository.h"
#include "world/worlddb.h"

#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

class TestFailure final : public std::runtime_error {
public:
	using std::runtime_error::runtime_error;
};

[[noreturn]] void Fail(const std::string &message)
{
	throw TestFailure(message);
}

void Expect(bool condition, const std::string &message)
{
	if (!condition) {
		Fail(message);
	}
}

template <typename T>
void ExpectEqual(const T &actual, const T &expected, const std::string &message)
{
	if (actual != expected) {
		Fail(message);
	}
}

template <typename T>
void ExpectOptionalEqual(
	const std::optional<T> &actual,
	const std::optional<T> &expected,
	const std::string &message
)
{
	if (actual != expected) {
		Fail(message);
	}
}

uint32_t BuildRunNonce()
{
	const auto now = static_cast<uint64_t>(
		std::chrono::system_clock::now().time_since_epoch().count()
	);

	return static_cast<uint32_t>((now ^ (now >> 32)) & 0x0fffffff);
}

class ActorActionQueueCleanup {
public:
	explicit ActorActionQueueCleanup(Database &database)
	: database_(database)
	{
	}

	void TrackActorId(uint32_t actor_id)
	{
		if (actor_id > 0) {
			actor_ids_.push_back(actor_id);
		}
	}

	~ActorActionQueueCleanup()
	{
		for (auto actor_id: actor_ids_) {
			ActorActionQueueRepository::DeleteByActorId(database_, actor_id);
		}

		for (auto it = actor_ids_.rbegin(); it != actor_ids_.rend(); ++it) {
			ActorProfilesRepository::DeleteOne(database_, *it);
		}
	}

private:
	Database &database_;
	std::vector<uint32_t> actor_ids_;
};

ActorProfilesRepository::ActorProfileRecord CreateProfile(
	Database &database,
	ActorActionQueueCleanup &cleanup,
	uint32_t bot_id,
	time_t created_at,
	time_t updated_at
)
{
	ActorProfilesRepository::ActorProfileRecord profile{};
	profile.actor_type = "autonomous_actor";
	profile.actor_substrate = "bot";
	profile.bot_id = bot_id;
	profile.enabled = true;
	profile.created_at = created_at;
	profile.updated_at = updated_at;

	const auto inserted = ActorProfilesRepository::UpsertBotBackedProfile(database, profile);
	cleanup.TrackActorId(inserted.actor_id);
	return inserted;
}

std::string BuildOversizedJson(size_t payload_size)
{
	return fmt::format(R"({{"text":"{}"}})", std::string(payload_size, 'x'));
}

size_t CountUtf8CodePoints(const std::string &document)
{
	size_t count = 0;

	for (const unsigned char byte: document) {
		if ((byte & 0xc0) != 0x80) {
			++count;
		}
	}

	return count;
}

std::string BuildUtf8BoundaryJson(size_t total_character_length)
{
	constexpr auto prefix = R"({"text":")";
	constexpr auto suffix = R"("})";
	const std::string repeated_code_point = "\xc3\xa9";

	const auto framing_length = std::char_traits<char>::length(prefix) + std::char_traits<char>::length(suffix);
	if (total_character_length <= framing_length) {
		return std::string(prefix) + suffix;
	}

	const auto payload_character_length = total_character_length - framing_length;
	std::string payload;
	payload.reserve(payload_character_length * repeated_code_point.size());

	for (size_t i = 0; i < payload_character_length; ++i) {
		payload += repeated_code_point;
	}

	return std::string(prefix) + payload + suffix;
}

std::string BuildInvalidUtf8Json()
{
	std::string document = R"({"text":")";
	document.push_back(static_cast<char>(0xc3));
	document += R"("})";
	return document;
}

} // namespace

void WorldserverCLI::TestActorActionQueue(int argc, char **argv, argh::parser &cmd, std::string &description)
{
	description = "Validates actor action queue persistence and claim-state helpers";

	if (cmd[{"-h", "--help"}]) {
		return;
	}

	EQEmuLogSys::Instance()->SilenceConsoleLogging();

	constexpr time_t created_at = 1719446400;
	constexpr time_t first_not_before = 1719446460;
	constexpr time_t first_expires_at = 1719446760;
	constexpr time_t claim_now = 1719446500;
	constexpr time_t future_not_before = 1719446800;
	constexpr time_t future_expires_at = 1719447000;
	constexpr time_t stale_pending_expires_at = 1719446450;
	constexpr time_t expiring_claim_not_before = 1719446465;
	constexpr time_t expiring_claim_expires_at = 1719446520;
	constexpr time_t stale_terminal_not_before = 1719446466;
	constexpr time_t stale_complete_expires_at = 1719446510;
	constexpr time_t stale_fail_expires_at = 1719446515;
	constexpr time_t stale_complete_attempt_at = 1719446530;
	constexpr time_t stale_fail_attempt_at = 1719446540;
	constexpr time_t expire_sweep_at = 1719446600;
	constexpr time_t completed_at = 1719446620;
	constexpr time_t failed_at = 1719446640;

	try {
		ActorActionQueueCleanup cleanup(database);
		const auto run_nonce = BuildRunNonce();

		const auto next_free_bot_id = [&](uint32_t salt) -> uint32_t {
			for (uint32_t attempt = 0; attempt < 1000; ++attempt) {
				const auto candidate = 420000000u + ((run_nonce + salt + attempt) % 90000000u);
				if (!ActorProfilesRepository::FindByBotId(database, candidate).has_value()) {
					return candidate;
				}
			}

			Fail("failed to find a collision-safe bot_id for actor action queue test");
		};

		const auto actor_a = CreateProfile(
			database,
			cleanup,
			next_free_bot_id(0),
			created_at,
			created_at
		);
		const auto actor_b = CreateProfile(
			database,
			cleanup,
			next_free_bot_id(1000),
			created_at,
			created_at
		);
		const auto actor_c = CreateProfile(
			database,
			cleanup,
			next_free_bot_id(2000),
			created_at,
			created_at
		);

		const auto enqueue_action = [&](
			uint32_t actor_id,
			const std::string &key_suffix,
			const std::string &action_text,
			std::optional<time_t> not_before,
			std::optional<time_t> expires_at
		) {
			return ActorActionQueueRepository::Enqueue(
				database,
				{
					.actor_id = actor_id,
					.source = "planner",
					.source_metadata_json = fmt::format(
						R"({{"planner_run":"actor-action-queue-{}","key":"{}"}})",
						run_nonce,
						key_suffix
					),
					.action_type = "say",
					.action_json = fmt::format(
						R"({{"text":"{}"}})",
						action_text
					),
					.idempotency_key = fmt::format("actor-action-queue-{}-{}", run_nonce, key_suffix),
					.not_before = not_before,
					.expires_at = expires_at,
					.created_at = created_at,
				}
			);
		};

		const auto enqueued = enqueue_action(
			actor_a.actor_id,
			"enqueue",
			fmt::format("actor-action-queue-{}-hello", run_nonce),
			first_not_before,
			first_expires_at
		);

		Expect(enqueued.action_id > 0, "enqueue should allocate an action_id");
		ExpectEqual(enqueued.actor_id, actor_a.actor_id, "enqueue should persist actor_id");
		ExpectEqual(enqueued.source, std::string("planner"), "enqueue should persist source");
		ExpectOptionalEqual(
			enqueued.source_metadata_json,
			std::optional<std::string>(
				fmt::format(
					R"({{"planner_run":"actor-action-queue-{}","key":"enqueue"}})",
					run_nonce
				)
			),
			"enqueue should persist source metadata"
		);
		ExpectEqual(enqueued.action_type, std::string("say"), "enqueue should persist action_type");
		ExpectEqual(
			enqueued.action_json,
			fmt::format(R"({{"text":"actor-action-queue-{}-hello"}})", run_nonce),
			"enqueue should persist action_json"
		);
		ExpectEqual(
			enqueued.idempotency_key,
			fmt::format("actor-action-queue-{}-enqueue", run_nonce),
			"enqueue should persist the duplicate guard key"
		);
		ExpectEqual(enqueued.state, std::string("pending"), "enqueue should default new rows to pending");
		ExpectOptionalEqual(enqueued.not_before, std::optional<time_t>(first_not_before), "enqueue should persist not_before");
		ExpectOptionalEqual(enqueued.expires_at, std::optional<time_t>(first_expires_at), "enqueue should persist expires_at");
		ExpectEqual(enqueued.created_at, created_at, "enqueue should persist created_at");
		ExpectEqual(enqueued.updated_at, created_at, "enqueue should initialize updated_at from created_at");

		const auto loaded_enqueued = ActorActionQueueRepository::FindByActionId(database, enqueued.action_id);
		Expect(loaded_enqueued.has_value(), "find by action_id should return the enqueued row");
		ExpectEqual(loaded_enqueued->action_id, enqueued.action_id, "find by action_id should round-trip action_id");

		const auto duplicate_enqueue = enqueue_action(
			actor_a.actor_id,
			"enqueue",
			fmt::format("actor-action-queue-{}-hello", run_nonce),
			first_not_before,
			first_expires_at
		);
		ExpectEqual(duplicate_enqueue.action_id, enqueued.action_id, "duplicate enqueue should return the existing action");
		ExpectEqual(
			ActorActionQueueRepository::Count(
				database,
				fmt::format(
					"actor_id = {} AND idempotency_key = '{}'",
					actor_a.actor_id,
					fmt::format("actor-action-queue-{}-enqueue", run_nonce)
				)
			),
			int64_t(1),
			"duplicate enqueue should not create a second row"
		);

		const auto actor_a_count_before_invalid = ActorActionQueueRepository::Count(
			database,
			fmt::format("actor_id = {}", actor_a.actor_id)
		);
		const auto invalid_json_enqueue = ActorActionQueueRepository::Enqueue(
			database,
			{
				.actor_id = actor_a.actor_id,
				.source = "planner",
				.action_type = "say",
				.action_json = "{\"text\":",
				.idempotency_key = fmt::format("actor-action-queue-{}-invalid-json", run_nonce),
				.created_at = created_at,
			}
		);
		ExpectEqual(invalid_json_enqueue.action_id, uint64_t(0), "invalid action JSON should be rejected before insert");

		const auto invalid_utf8_json = BuildInvalidUtf8Json();
		const auto invalid_utf8_enqueue = ActorActionQueueRepository::Enqueue(
			database,
			{
				.actor_id = actor_a.actor_id,
				.source = "planner",
				.action_type = "say",
				.action_json = invalid_utf8_json,
				.idempotency_key = fmt::format("actor-action-queue-{}-invalid-utf8-json", run_nonce),
				.created_at = created_at,
			}
		);
		ExpectEqual(
			invalid_utf8_enqueue.action_id,
			uint64_t(0),
			"invalid utf8 action JSON should be rejected before insert"
		);

		const auto invalid_utf8_metadata_enqueue = ActorActionQueueRepository::Enqueue(
			database,
			{
				.actor_id = actor_a.actor_id,
				.source = "planner",
				.source_metadata_json = invalid_utf8_json,
				.action_type = "say",
				.action_json = R"({"text":"valid"})",
				.idempotency_key = fmt::format("actor-action-queue-{}-invalid-utf8-metadata", run_nonce),
				.created_at = created_at,
			}
		);
		ExpectEqual(
			invalid_utf8_metadata_enqueue.action_id,
			uint64_t(0),
			"invalid utf8 metadata JSON should be rejected before insert"
		);

		const auto oversized_json_enqueue = ActorActionQueueRepository::Enqueue(
			database,
			{
				.actor_id = actor_a.actor_id,
				.source = "planner",
				.action_type = "say",
				.action_json = BuildOversizedJson(
					ActorActionQueueRepository::kActionJsonMaxLength + 32
				),
				.idempotency_key = fmt::format("actor-action-queue-{}-oversized-json", run_nonce),
				.created_at = created_at,
			}
		);
		ExpectEqual(oversized_json_enqueue.action_id, uint64_t(0), "oversized action JSON should be rejected before insert");
		ExpectEqual(
			ActorActionQueueRepository::Count(
				database,
				fmt::format("actor_id = {}", actor_a.actor_id)
			),
			actor_a_count_before_invalid,
			"invalid or oversized JSON should not create actor queue rows"
		);

		const auto utf8_boundary_action_json = BuildUtf8BoundaryJson(
			ActorActionQueueRepository::kActionJsonMaxLength
		);
		ExpectEqual(
			CountUtf8CodePoints(utf8_boundary_action_json),
			ActorActionQueueRepository::kActionJsonMaxLength,
			"utf8 boundary helper should build action JSON at the character limit"
		);
		Expect(
			utf8_boundary_action_json.size() > ActorActionQueueRepository::kActionJsonMaxLength,
			"utf8 boundary helper should exceed the byte limit while staying within the character limit"
		);
		const auto utf8_boundary_enqueue = ActorActionQueueRepository::Enqueue(
			database,
			{
				.actor_id = actor_a.actor_id,
				.source = "planner",
				.action_type = "say",
				.action_json = utf8_boundary_action_json,
				.idempotency_key = fmt::format("actor-action-queue-{}-utf8-boundary", run_nonce),
				.not_before = future_not_before,
				.expires_at = future_expires_at,
				.created_at = created_at,
			}
		);
		Expect(
			utf8_boundary_enqueue.action_id > 0,
			"multibyte action JSON within the character limit should be accepted"
		);

		const auto actor_a_future = enqueue_action(
			actor_a.actor_id,
			"future",
			fmt::format("actor-action-queue-{}-future", run_nonce),
			future_not_before,
			future_expires_at
		);
		const auto actor_a_stale_pending = enqueue_action(
			actor_a.actor_id,
			"stale-pending",
			fmt::format("actor-action-queue-{}-stale-pending", run_nonce),
			std::nullopt,
			stale_pending_expires_at
		);
		const auto actor_b_due = enqueue_action(
			actor_b.actor_id,
			"due-any",
			fmt::format("actor-action-queue-{}-due-any", run_nonce),
			first_not_before - 1,
			first_expires_at
		);
		const auto actor_b_stale_complete = enqueue_action(
			actor_b.actor_id,
			"stale-complete",
			fmt::format("actor-action-queue-{}-stale-complete", run_nonce),
			stale_terminal_not_before,
			stale_complete_expires_at
		);
		const auto actor_b_stale_fail = enqueue_action(
			actor_b.actor_id,
			"stale-fail",
			fmt::format("actor-action-queue-{}-stale-fail", run_nonce),
			stale_terminal_not_before,
			stale_fail_expires_at
		);
		const auto actor_c_expiring_claim = enqueue_action(
			actor_c.actor_id,
			"expiring-claim",
			fmt::format("actor-action-queue-{}-expiring-claim", run_nonce),
			expiring_claim_not_before,
			expiring_claim_expires_at
		);

		const auto claimed_actor_a = ActorActionQueueRepository::ClaimNextPending(
			database,
			{
				.actor_id = actor_a.actor_id,
				.claimed_by = "zone-a",
				.now = claim_now,
			}
		);
		Expect(claimed_actor_a.has_value(), "actor-specific claim should claim the due action");
		ExpectEqual(claimed_actor_a->action_id, enqueued.action_id, "actor-specific claim should select the oldest due row");
		ExpectEqual(claimed_actor_a->state, std::string("claimed"), "claimed action should transition to claimed");
		ExpectOptionalEqual(claimed_actor_a->claimed_by, std::optional<std::string>("zone-a"), "claim should persist claimed_by");
		ExpectOptionalEqual(claimed_actor_a->claimed_at, std::optional<time_t>(claim_now), "claim should persist claimed_at");

		const auto actor_a_second_claim = ActorActionQueueRepository::ClaimNextPending(
			database,
			{
				.actor_id = actor_a.actor_id,
				.claimed_by = "zone-a",
				.now = claim_now,
			}
		);
		Expect(!actor_a_second_claim.has_value(), "claim should skip not-yet-due and already-expired actor rows");

		const auto claimed_any_actor = ActorActionQueueRepository::ClaimNextPending(
			database,
			{
				.claimed_by = "zone-global",
				.now = claim_now,
			}
		);
		Expect(claimed_any_actor.has_value(), "claim without actor filter should claim the next due action");
		ExpectEqual(claimed_any_actor->action_id, actor_b_due.action_id, "global claim should select the other actor's due row");

		const auto claimed_stale_complete = ActorActionQueueRepository::ClaimNextPending(
			database,
			{
				.actor_id = actor_b.actor_id,
				.claimed_by = "zone-b",
				.now = claim_now,
			}
		);
		Expect(claimed_stale_complete.has_value(), "claim should support stale-completion coverage rows");
		ExpectEqual(
			claimed_stale_complete->action_id,
			actor_b_stale_complete.action_id,
			"claim should select the older stale-completion row before later due actor rows"
		);

		const auto claimed_stale_fail = ActorActionQueueRepository::ClaimNextPending(
			database,
			{
				.actor_id = actor_b.actor_id,
				.claimed_by = "zone-b",
				.now = claim_now,
			}
		);
		Expect(claimed_stale_fail.has_value(), "claim should support stale-failure coverage rows");
		ExpectEqual(
			claimed_stale_fail->action_id,
			actor_b_stale_fail.action_id,
			"claim should select the stale-failure row after the stale-completion row"
		);

		const auto claimed_actor_c = ActorActionQueueRepository::ClaimNextPending(
			database,
			{
				.actor_id = actor_c.actor_id,
				.claimed_by = "zone-c",
				.now = claim_now,
			}
		);
		Expect(claimed_actor_c.has_value(), "claim should support additional due rows for other actors");
		ExpectEqual(
			claimed_actor_c->action_id,
			actor_c_expiring_claim.action_id,
			"claim should return the expiring claimed row for actor C"
		);

		const auto completed_action = ActorActionQueueRepository::MarkCompleted(
			database,
			{
				.action_id = claimed_actor_a->action_id,
				.result_json = R"({"delivered":true})",
				.completed_at = completed_at,
			}
		);
		Expect(completed_action.has_value(), "completion should succeed from claimed state");
		ExpectEqual(completed_action->state, std::string("completed"), "completion should transition the action to completed");
		ExpectOptionalEqual(
			completed_action->result_json,
			std::optional<std::string>(R"({"delivered":true})"),
			"completion should persist result_json"
		);
		ExpectOptionalEqual(
			completed_action->completed_at,
			std::optional<time_t>(completed_at),
			"completion should persist completed_at"
		);
		Expect(!completed_action->failure_reason.has_value(), "completion should clear failure_reason");

		const auto completed_again = ActorActionQueueRepository::MarkCompleted(
			database,
			{
				.action_id = claimed_actor_a->action_id,
				.result_json = R"({"delivered":true})",
				.completed_at = completed_at,
			}
		);
		Expect(!completed_again.has_value(), "completion should not run twice on the same action");

		const auto stale_completed_action = ActorActionQueueRepository::MarkCompleted(
			database,
			{
				.action_id = claimed_stale_complete->action_id,
				.result_json = R"({"delivered":true})",
				.completed_at = stale_complete_attempt_at,
			}
		);
		Expect(
			stale_completed_action.has_value(),
			"completion should expose the expired row when a claimed action goes stale"
		);
		ExpectEqual(
			stale_completed_action->state,
			std::string("expired"),
			"stale completion attempts should return the expired terminal row"
		);
		ExpectEqual(
			stale_completed_action->completed_at,
			std::optional<time_t>(stale_complete_attempt_at),
			"stale completion attempts should stamp the terminal time when expiring the row"
		);
		Expect(
			!stale_completed_action->result_json.has_value(),
			"stale completion attempts should not persist result JSON"
		);

		const auto failed_action = ActorActionQueueRepository::MarkFailed(
			database,
			{
				.action_id = claimed_any_actor->action_id,
				.failure_reason = "speech delivery rejected",
				.completed_at = failed_at,
			}
		);
		Expect(failed_action.has_value(), "failure should succeed from claimed state");
		ExpectEqual(failed_action->state, std::string("failed"), "failure should transition the action to failed");
		ExpectOptionalEqual(
			failed_action->failure_reason,
			std::optional<std::string>("speech delivery rejected"),
			"failure should persist failure_reason"
		);
		ExpectOptionalEqual(
			failed_action->completed_at,
			std::optional<time_t>(failed_at),
			"failure should persist completed_at"
		);
		Expect(!failed_action->result_json.has_value(), "failure should clear result_json");

		const auto stale_failed_action = ActorActionQueueRepository::MarkFailed(
			database,
			{
				.action_id = claimed_stale_fail->action_id,
				.failure_reason = "expired-before-failure",
				.completed_at = stale_fail_attempt_at,
			}
		);
		Expect(
			stale_failed_action.has_value(),
			"failure should expose the expired row when a claimed action goes stale"
		);
		ExpectEqual(
			stale_failed_action->state,
			std::string("expired"),
			"stale failure attempts should return the expired terminal row"
		);
		ExpectOptionalEqual(
			stale_failed_action->completed_at,
			std::optional<time_t>(stale_fail_attempt_at),
			"stale failure attempts should stamp the terminal time when expiring the row"
		);
		Expect(
			!stale_failed_action->failure_reason.has_value(),
			"stale failure attempts should not persist failure metadata"
		);

		const auto utf8_boundary_result_json = BuildUtf8BoundaryJson(
			ActorActionQueueRepository::kResultJsonMaxLength
		);
		ExpectEqual(
			CountUtf8CodePoints(utf8_boundary_result_json),
			ActorActionQueueRepository::kResultJsonMaxLength,
			"utf8 boundary helper should build result JSON at the character limit"
		);
		Expect(
			utf8_boundary_result_json.size() > ActorActionQueueRepository::kResultJsonMaxLength,
			"utf8 boundary helper should exceed the byte limit for result JSON while staying within the character limit"
		);
		const auto utf8_result_action = enqueue_action(
			actor_c.actor_id,
			"utf8-result",
			fmt::format("actor-action-queue-{}-utf8-result", run_nonce),
			first_not_before - 2,
			first_expires_at
		);
		const auto claimed_utf8_result = ActorActionQueueRepository::ClaimNextPending(
			database,
			{
				.actor_id = actor_c.actor_id,
				.claimed_by = "zone-c",
				.now = claim_now,
			}
		);
		Expect(claimed_utf8_result.has_value(), "claim should support unicode result coverage rows");
		ExpectEqual(
			claimed_utf8_result->action_id,
			utf8_result_action.action_id,
			"claim should pick the older unicode-result row before later actor-C work"
		);
		const auto completed_utf8_result = ActorActionQueueRepository::MarkCompleted(
			database,
			{
				.action_id = claimed_utf8_result->action_id,
				.result_json = utf8_boundary_result_json,
				.completed_at = completed_at - 5,
			}
		);
		Expect(
			completed_utf8_result.has_value(),
			"multibyte result JSON within the character limit should be accepted"
		);
		ExpectOptionalEqual(
			completed_utf8_result->result_json,
			std::optional<std::string>(utf8_boundary_result_json),
			"completion should persist unicode result JSON at the character limit"
		);

		const auto invalid_utf8_result_action = enqueue_action(
			actor_c.actor_id,
			"invalid-utf8-result",
			fmt::format("actor-action-queue-{}-invalid-utf8-result", run_nonce),
			first_not_before - 3,
			first_expires_at
		);
		const auto claimed_invalid_utf8_result = ActorActionQueueRepository::ClaimNextPending(
			database,
			{
				.actor_id = actor_c.actor_id,
				.claimed_by = "zone-c",
				.now = claim_now,
			}
		);
		Expect(
			claimed_invalid_utf8_result.has_value(),
			"claim should support invalid utf8 result-json coverage rows"
		);
		ExpectEqual(
			claimed_invalid_utf8_result->action_id,
			invalid_utf8_result_action.action_id,
			"claim should select the invalid utf8 result-json row first when it is oldest"
		);
		const auto invalid_utf8_result = ActorActionQueueRepository::MarkCompleted(
			database,
			{
				.action_id = claimed_invalid_utf8_result->action_id,
				.result_json = invalid_utf8_json,
				.completed_at = completed_at - 6,
			}
		);
		Expect(
			!invalid_utf8_result.has_value(),
			"invalid utf8 result JSON should be rejected before completion update"
		);

		const auto fail_pending_action = ActorActionQueueRepository::MarkFailed(
			database,
			{
				.action_id = actor_a_future.action_id,
				.failure_reason = "should-not-fail-pending",
				.completed_at = failed_at,
			}
		);
		Expect(!fail_pending_action.has_value(), "failure should reject non-claimed actions");

		ExpectEqual(
			ActorActionQueueRepository::ExpireDue(database, expire_sweep_at),
			2,
			"expiry sweep should expire stale pending and stale claimed actions"
		);

		const auto expired_pending = ActorActionQueueRepository::FindByActionId(database, actor_a_stale_pending.action_id);
		Expect(expired_pending.has_value(), "expired pending action should still be queryable");
		ExpectEqual(expired_pending->state, std::string("expired"), "expiry sweep should expire pending rows");

		const auto expired_claimed = ActorActionQueueRepository::FindByActionId(database, actor_c_expiring_claim.action_id);
		Expect(expired_claimed.has_value(), "expired claimed action should still be queryable");
		ExpectEqual(expired_claimed->state, std::string("expired"), "expiry sweep should expire claimed rows");
		ExpectOptionalEqual(
			expired_claimed->completed_at,
			std::optional<time_t>(expire_sweep_at),
			"expiry sweep should stamp terminal time on expired claimed rows"
		);

		const auto still_future = ActorActionQueueRepository::FindByActionId(database, actor_a_future.action_id);
		Expect(still_future.has_value(), "future action should remain queryable");
		ExpectEqual(still_future->state, std::string("pending"), "expiry sweep should not expire future rows that have not reached expires_at");

		std::cout << "[PASS] actor-action-queue\n";
	}
	catch (const TestFailure &e) {
		std::cerr << "[FAIL] " << e.what() << "\n";
		std::exit(1);
	}
}

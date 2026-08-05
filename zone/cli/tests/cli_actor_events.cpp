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

#include "zone/zone_cli.h"

#include "common/actor_reserved_owners.h"
#include "common/eqemu_logsys.h"
#include "common/json/json.h"
#include "common/repositories/actor_action_queue_repository.h"
#include "common/repositories/actor_events_repository.h"
#include "common/repositories/actor_profiles_repository.h"
#include "common/repositories/actor_status_repository.h"
#include "common/repositories/player_event_logs_repository.h"
#include "common/strings.h"
#include "zone/bot.h"
#include "zone/actor_action_executor.h"
#include "zone/harness/actor_event_persistence_sink.h"
#include "zone/harness/actor_event_recorder.h"
#include "zone/harness/owned_bot_actor_fixture.h"
#include "zone/zone.h"
#include "zone/zonedb.h"

#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <cstdlib>
#include <future>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace {

class TestFailure final : public std::runtime_error {
public:
	using std::runtime_error::runtime_error;
};

[[noreturn]] void Fail(const std::string& message) {
	throw TestFailure(message);
}

void Expect(bool condition, const std::string& message) {
	if (!condition) {
		Fail(message);
	}
}

template <typename T> void ExpectEqual(const T& actual, const T& expected, const std::string& message) {
	if (actual != expected) {
		Fail(message);
	}
}

uint32_t BuildRunNonce() {
	const auto now = static_cast<uint64_t>(std::chrono::system_clock::now().time_since_epoch().count());

	return static_cast<uint32_t>((now ^ (now >> 32)) & 0x0fffffff);
}

Json::Value ParseJson(const std::string& document) {
	Json::CharReaderBuilder builder;
	std::unique_ptr<Json::CharReader> reader(builder.newCharReader());
	Json::Value root;
	std::string errors;
	const bool ok = reader->parse(document.data(), document.data() + document.size(), &root, &errors);

	Expect(ok, "actor event payload should be valid JSON: " + errors);
	return root;
}

int64_t CountPlayerEventLogRowsWithMarker(const std::string& marker) {
	auto results = database.QueryDatabase(
		fmt::format("SELECT COUNT(*) FROM player_event_logs WHERE event_data LIKE '%{}%'", Strings::Escape(marker)));

	Expect(results.Success() && results.RowCount() == 1 && results.begin()[0],
		   "player_event_logs marker count query should succeed");
	return strtoll(results.begin()[0], nullptr, 10);
}

class ActorEventPersistenceCleanup {
public:
	void TrackActorId(uint32_t actor_id) {
		if (actor_id > 0) {
			actor_ids_.push_back(actor_id);
		}
	}

	uint32_t reserved_owner_character_id = 0;

	~ActorEventPersistenceCleanup() {
		for (auto actor_id : actor_ids_) {
			ActorActionQueueRepository::DeleteByActorId(database, actor_id);
			ActorEventsRepository::DeleteByActorId(database, actor_id);
			ActorStatusRepository::DeleteOne(database, actor_id);
		}

		for (auto it = actor_ids_.rbegin(); it != actor_ids_.rend(); ++it) {
			ActorProfilesRepository::DeleteOne(database, *it);
		}

		if (reserved_owner_character_id > 0) {
			std::string unused_reason;
			EQ::Actor::ReservedOwners::Rollback(database, reserved_owner_character_id, &unused_reason);
		}
	}

private:
	std::vector<uint32_t> actor_ids_;
};

class BlockingPersistenceSink final : public EQ::ZoneHarness::ActorEventPersistenceSink {
public:
	void PersistSpeechEmitted(Mob*, const EQ::ZoneHarness::ActorEvent&) override {
		std::unique_lock lock(mutex_);
		persist_started_ = true;
		persist_started_cv_.notify_all();
		release_persist_cv_.wait(lock, [this]() { return allow_persist_to_finish_; });
		persist_finished_ = true;
		persist_finished_cv_.notify_all();
	}

	bool WaitUntilPersistStarted(std::chrono::milliseconds timeout) {
		std::unique_lock lock(mutex_);
		return persist_started_cv_.wait_for(lock, timeout, [this]() { return persist_started_; });
	}

	void AllowPersistToFinish() {
		std::lock_guard lock(mutex_);
		allow_persist_to_finish_ = true;
		release_persist_cv_.notify_all();
	}

	bool WaitUntilPersistFinished(std::chrono::milliseconds timeout) {
		std::unique_lock lock(mutex_);
		return persist_finished_cv_.wait_for(lock, timeout, [this]() { return persist_finished_; });
	}

private:
	std::mutex mutex_;
	std::condition_variable persist_started_cv_;
	std::condition_variable release_persist_cv_;
	std::condition_variable persist_finished_cv_;
	bool persist_started_ = false;
	bool allow_persist_to_finish_ = false;
	bool persist_finished_ = false;
};

void ExpectRecorderShutdownWaitsForInFlightCallbacks() {
	using namespace std::chrono_literals;

	EQ::ZoneHarness::ActorEventRecorder recorder;
	BlockingPersistenceSink blocking_sink;
	recorder.SetPersistenceSink(&blocking_sink);
	EQ::ZoneHarness::ActorEventRecorder::RegisterActiveRecorder(&recorder);

	std::thread observe_thread(
		[]() { EQ::ZoneHarness::ActorEventRecorder::ObserveSpeechEmitted(nullptr, "say", "teardown-sync", 200); });

	const bool persist_started = blocking_sink.WaitUntilPersistStarted(1s);
	if (!persist_started) {
		observe_thread.join();
		EQ::ZoneHarness::ActorEventRecorder::ClearActiveRecorder(&recorder);
		recorder.SetPersistenceSink(nullptr);
		Fail("blocking persistence sink should observe an in-flight speech callback");
	}

	auto clear_future = std::async(
		std::launch::async, [&recorder]() { EQ::ZoneHarness::ActorEventRecorder::ClearActiveRecorder(&recorder); });

	const auto clear_status_while_blocked = clear_future.wait_for(100ms);
	blocking_sink.AllowPersistToFinish();
	const bool persist_finished = blocking_sink.WaitUntilPersistFinished(1s);
	observe_thread.join();
	const auto clear_status_after_release = clear_future.wait_for(1s);
	clear_future.get();
	recorder.SetPersistenceSink(nullptr);

	Expect(clear_status_while_blocked == std::future_status::timeout,
		   "active recorder teardown should wait for in-flight callbacks before returning");
	Expect(persist_finished, "blocking persistence sink should finish after release");
	Expect(clear_status_after_release == std::future_status::ready,
		   "active recorder teardown should finish once in-flight callbacks drain");
	ExpectEqual(recorder.Since(0, 4).size(), static_cast<size_t>(1),
				"blocked speech callback should still record one actor event before teardown completes");
}

} // namespace

void ZoneCLI::TestActorEvents(int argc, char** argv, argh::parser& cmd, std::string& description) {
	description = "Validates runtime speech_emitted actor event persistence through the harness recorder path";

	if (cmd[{"-h", "--help"}]) {
		return;
	}

	EQEmuLogSys::Instance()->SilenceConsoleLogging();

	try {
		ExpectRecorderShutdownWaitsForInFlightCallbacks();

		const auto run_nonce = BuildRunNonce();

		Zone::Bootup(ZoneID("qrg"), 0, false);
		zone->StopShutdownTimer();
		entity_list.Process();
		entity_list.MobProcess();

		EQ::ZoneHarness::ActorEventRecorder recorder;
		EQ::ZoneHarness::ActorEventRepositoryPersistenceSink persistence_sink;
		recorder.SetPersistenceSink(&persistence_sink);
		EQ::ZoneHarness::ActorEventRecorder::RegisterActiveRecorder(&recorder);

		ActorEventPersistenceCleanup cleanup;
		const auto reserved_owner =
			EQ::Actor::ReservedOwners::Provision(database, fmt::format("ActorownerRuntime{}", run_nonce));
		Expect(reserved_owner.character_id > 0,
			   "reserved owner provisioning should succeed for runtime actor event persistence");
		cleanup.reserved_owner_character_id = reserved_owner.character_id;
		EQ::ZoneHarness::OwnedBotActorFixture fixture;
		Expect(fixture.SetUpOwnedBotSolo({
				   .owner_name = reserved_owner.name,
				   .owner_character_id = reserved_owner.character_id,
			   }),
			   "owned bot harness fixture should boot");
		Expect(fixture.OwnedBot() != nullptr, "owned bot harness fixture should create a bot actor");

		const auto next_free_bot_id = [&](uint32_t salt) -> uint32_t {
			for (uint32_t attempt = 0; attempt < 1000; ++attempt) {
				const auto candidate = 420000000u + ((run_nonce + salt + attempt) % 80000000u);
				if (!ActorProfilesRepository::FindByBotId(database, candidate).has_value()) {
					return candidate;
				}
			}

			Fail("failed to find a collision-safe bot_id for runtime actor event persistence test");
		};

		const auto actor_bot_id = next_free_bot_id(0);
		fixture.AssignBotID(fixture.OwnedBot(), actor_bot_id);

		ActorProfilesRepository::ActorProfileRecord profile{};
		profile.actor_type = "autonomous_actor";
		profile.actor_substrate = "bot";
		profile.bot_id = actor_bot_id;
		profile.owner_character_id = reserved_owner.character_id;
		profile.enabled = true;
		const auto inserted_profile = ActorProfilesRepository::UpsertBotBackedProfile(database, profile);
		Expect(inserted_profile.actor_id > 0, "actor profile insert should allocate an actor_id");
		cleanup.TrackActorId(inserted_profile.actor_id);

		const auto speech_marker = fmt::format("runtime-actor-events-{}", run_nonce);
		fixture.OwnedBot()->Say("%s", speech_marker.c_str());

		const auto observed_events = recorder.Since(0, 8);
		ExpectEqual(observed_events.size(), static_cast<size_t>(1),
					"runtime actor event recorder should observe one speech_emitted event");
		ExpectEqual(observed_events[0].type, std::string("speech_emitted"),
					"runtime recorder should observe speech_emitted");
		ExpectEqual(observed_events[0].speech.channel, std::string("say"),
					"runtime recorder should preserve speech channel");
		ExpectEqual(observed_events[0].speech.text, speech_marker,
					"runtime recorder should preserve emitted speech text");
		ExpectEqual(observed_events[0].caster.entity_id, fixture.OwnedBot()->GetID(),
					"runtime recorder should preserve emitting entity id");

		const auto persisted_events = ActorEventsRepository::ReadCursor(database, inserted_profile.actor_id, 0, 8);
		ExpectEqual(persisted_events.size(), static_cast<size_t>(1),
					"runtime speech_emitted path should persist one actor event row");
		ExpectEqual(persisted_events[0].actor_id, inserted_profile.actor_id,
					"persisted runtime actor event should target the actor profile");
		ExpectEqual(persisted_events[0].bot_id, std::optional<uint32_t>(actor_bot_id),
					"persisted runtime actor event should keep bot_id");
		ExpectEqual(persisted_events[0].owner_character_id, std::optional<uint32_t>(reserved_owner.character_id),
					"persisted runtime actor event should keep owner_character_id");
		ExpectEqual(persisted_events[0].entity_id, std::optional<uint32_t>(fixture.OwnedBot()->GetID()),
					"persisted runtime actor event should keep entity_id");
		ExpectEqual(persisted_events[0].event_type, std::string("speech_emitted"),
					"persisted runtime actor event should keep speech_emitted type");

		const auto payload = ParseJson(persisted_events[0].event_json);
		ExpectEqual(payload["channel"].asString(), std::string("say"),
					"persisted runtime actor event should keep speech channel");
		ExpectEqual(payload["text"].asString(), speech_marker, "persisted runtime actor event should keep speech text");
		ExpectEqual(payload["audible_radius"].asUInt(), 200u,
					"persisted runtime actor event should keep say audible radius");

		const auto now = std::time(nullptr);
		const auto status = ActorStatusRepository::UpsertOne(database, {
																		   .actor_id = inserted_profile.actor_id,
																		   .zone_id = zone->GetZoneID(),
																		   .instance_id = zone->GetInstanceID(),
																		   .entity_id = fixture.OwnedBot()->GetID(),
																		   .state = "active",
																		   .heartbeat_at = now,
																	   });
		Expect(status.actor_id == inserted_profile.actor_id, "actor status should persist for queue execution");

		const auto enqueue = [&](const std::string& type, const Json::Value& body, const std::string& key,
								 std::optional<time_t> expires_at = std::nullopt) {
			Json::StreamWriterBuilder writer;
			writer["indentation"] = "";
			Json::Value metadata;
			metadata["expected_event_id"] = Json::UInt64(persisted_events[0].event_id);
			return ActorActionQueueRepository::Enqueue(database,
													   {
														   .actor_id = inserted_profile.actor_id,
														   .source = "actor-events-test",
														   .source_metadata_json = Json::writeString(writer, metadata),
														   .action_type = type,
														   .action_json = Json::writeString(writer, body),
														   .idempotency_key = key,
														   .expires_at = expires_at,
														   .created_at = now,
													   });
		};

		Json::Value stand_body(Json::objectValue);
		ActorProfilesRepository::ActorProfileRecord stale_profile{};
		stale_profile.actor_type = "autonomous_actor";
		stale_profile.actor_substrate = "bot";
		stale_profile.bot_id = next_free_bot_id(1);
		stale_profile.owner_character_id = reserved_owner.character_id;
		stale_profile.enabled = true;
		stale_profile = ActorProfilesRepository::UpsertBotBackedProfile(database, stale_profile);
		Expect(stale_profile.actor_id > 0, "stale live-binding profile should persist");
		cleanup.TrackActorId(stale_profile.actor_id);
		ActorStatusRepository::UpsertOne(database, {
													   .actor_id = stale_profile.actor_id,
													   .zone_id = zone->GetZoneID(),
													   .instance_id = zone->GetInstanceID(),
													   .entity_id = fixture.OwnedBot()->GetID(),
													   .state = "active",
													   .heartbeat_at = now,
												   });
		Json::StreamWriterBuilder stale_writer;
		stale_writer["indentation"] = "";
		Json::Value stale_metadata;
		stale_metadata["expected_event_id"] = Json::UInt64(0);
		const auto stale_first = ActorActionQueueRepository::Enqueue(
			database, {
						  .actor_id = stale_profile.actor_id,
						  .source = "actor-events-test",
						  .source_metadata_json = Json::writeString(stale_writer, stale_metadata),
						  .action_type = "stand",
						  .action_json = Json::writeString(stale_writer, stand_body),
						  .idempotency_key = fmt::format("stale-first-{}", run_nonce),
						  .created_at = now,
					  });
		const auto first_action = enqueue("stand", stand_body, fmt::format("stand-a-{}", run_nonce));
		const auto second_action = enqueue("stand", stand_body, fmt::format("stand-b-{}", run_nonce));
		Expect(stale_first.action_id && first_action.action_id && second_action.action_id,
			   "actor actions should enqueue");
		ActorActionExecutor executor(database, zone->GetZoneID(), zone->GetInstanceID(), zone->GetZoneServerId());
		executor.ProcessOne();
		ExpectEqual(ActorActionQueueRepository::FindOne(database, stale_first.action_id).state, std::string("pending"),
					"a stale live binding should remain unclaimed");
		ExpectEqual(ActorActionQueueRepository::FindOne(database, first_action.action_id).state,
					std::string("completed"), "a stale first row must not starve eligible work");
		executor.ProcessOne();
		ExpectEqual(ActorActionQueueRepository::FindOne(database, first_action.action_id).state,
					std::string("completed"), "first action should complete");
		ExpectEqual(ActorActionQueueRepository::FindOne(database, second_action.action_id).state,
					std::string("completed"), "lifecycle events must not stale a sibling action watermark");
		executor.ProcessOne();
		const auto completed_events =
			ActorEventsRepository::ReadCursor(database, inserted_profile.actor_id, persisted_events[0].event_id, 8);
		ExpectEqual(completed_events.size(), static_cast<size_t>(2),
					"duplicate processing should not emit another completion");
		for (const auto& event : completed_events) {
			ExpectEqual(event.event_type, std::string("action_completed"),
						"successful actions should emit completion events");
			Expect(ParseJson(event.event_json)["action_id"].asUInt64() != 0,
				   "completion event should correlate action_id");
		}

		const auto rejected = enqueue("unsupported", stand_body, fmt::format("reject-{}", run_nonce));
		executor.ProcessOne();
		ExpectEqual(ActorActionQueueRepository::FindOne(database, rejected.action_id).state, std::string("failed"),
					"illegal actions should be rejected");
		const auto rejected_events =
			ActorEventsRepository::ReadCursor(database, inserted_profile.actor_id, completed_events.back().event_id, 8);
		ExpectEqual(rejected_events.size(), static_cast<size_t>(1), "rejection should emit one event");
		ExpectEqual(ParseJson(rejected_events[0].event_json)["action_id"].asUInt64(), rejected.action_id,
					"rejection event should correlate action_id");

		const auto expiring_rejection =
			enqueue("unsupported", stand_body, fmt::format("expiring-rejection-{}", run_nonce), now + 1);
		int rejection_clock_read = 0;
		ActorActionExecutor rejection_expiry_executor(database, zone->GetZoneID(), zone->GetInstanceID(),
													  zone->GetZoneServerId(),
													  [&]() { return rejection_clock_read++ == 0 ? now : now + 1; });
		rejection_expiry_executor.ProcessOne();
		ExpectEqual(ActorActionQueueRepository::FindOne(database, expiring_rejection.action_id).state,
					std::string("expired"), "an action expiring at rejection time should remain terminally expired");
		ExpectEqual(
			ActorEventsRepository::ReadCursor(database, inserted_profile.actor_id, rejected_events[0].event_id, 8)
				.size(),
			static_cast<size_t>(0), "expiry at rejection time should not emit a contradictory rejection event");

		fixture.OwnedBot()->Sit();
		Expect(fixture.OwnedBot()->IsSitting(), "expiry race precondition should put the actor in a sitting state");
		const auto expiring = enqueue("stand", stand_body, fmt::format("expiring-{}", run_nonce), now + 1);
		Expect(expiring.action_id != 0, "action expiring after claim should enqueue");
		int clock_read = 0;
		ActorActionExecutor expiring_executor(database, zone->GetZoneID(), zone->GetInstanceID(),
											  zone->GetZoneServerId(),
											  [&]() { return clock_read++ == 0 ? now : now + 1; });
		expiring_executor.ProcessOne();
		ExpectEqual(ActorActionQueueRepository::FindOne(database, expiring.action_id).state, std::string("expired"),
					"action expiring between claim and execution should expire");
		Expect(fixture.OwnedBot()->IsSitting(), "expired action should not mutate actor gameplay state");
		ExpectEqual(
			ActorEventsRepository::ReadCursor(database, inserted_profile.actor_id, rejected_events[0].event_id, 8)
				.size(),
			static_cast<size_t>(0), "claim-to-execution expiry should not emit a contradictory lifecycle event");

		const auto lock_wait_expiring =
			enqueue("stand", stand_body, fmt::format("lock-wait-expiring-{}", run_nonce), now + 1);
		int lock_wait_clock_read = 0;
		ActorActionExecutor lock_wait_executor(database, zone->GetZoneID(), zone->GetInstanceID(),
											   zone->GetZoneServerId(),
											   [&]() { return lock_wait_clock_read++ < 2 ? now : now + 1; });
		lock_wait_executor.ProcessOne();
		ExpectEqual(ActorActionQueueRepository::FindOne(database, lock_wait_expiring.action_id).state,
					std::string("expired"), "execution lock wait must not allow post-expiry completion");
		Expect(fixture.OwnedBot()->IsSitting(), "expiry while acquiring the execution lock must not mutate gameplay");
		ExpectEqual(
			ActorEventsRepository::ReadCursor(database, inserted_profile.actor_id, rejected_events[0].event_id, 8)
				.size(),
			static_cast<size_t>(0), "execution-lock expiry should not emit a contradictory lifecycle event");

		const auto expired = enqueue("stand", stand_body, fmt::format("expired-{}", run_nonce), now - 1);
		executor.ProcessOne();
		ExpectEqual(ActorActionQueueRepository::FindOne(database, expired.action_id).state, std::string("expired"),
					"expired actions should never be claimed or completed");
		ExpectEqual(
			ActorEventsRepository::ReadCursor(database, inserted_profile.actor_id, rejected_events[0].event_id, 8)
				.size(),
			static_cast<size_t>(0), "expiry should not emit a contradictory lifecycle event");

		fixture.OwnedBot()->Sit();
		const auto ownership_race = enqueue("stand", stand_body, fmt::format("ownership-race-{}", run_nonce));
		Expect(ownership_race.action_id != 0, "ownership-race action should enqueue");
		auto disabled_profile = inserted_profile;
		disabled_profile.enabled = false;
		int ownership_clock_read = 0;
		ActorActionExecutor ownership_race_executor(
			database, zone->GetZoneID(), zone->GetInstanceID(), zone->GetZoneServerId(), [&]() {
				if (ownership_clock_read++ == 1) {
					ActorProfilesRepository::UpsertBotBackedProfile(database, disabled_profile);
				}
				return now;
			});
		ownership_race_executor.ProcessOne();
		ExpectEqual(ActorActionQueueRepository::FindOne(database, ownership_race.action_id).state,
					std::string("pending"), "an ownership change before execution should release the claim");
		Expect(fixture.OwnedBot()->IsSitting(), "an ownership change before execution must not mutate gameplay state");
		ActorActionQueueRepository::DeleteOne(database, ownership_race.action_id);
		ActorProfilesRepository::UpsertBotBackedProfile(database, inserted_profile);

		const auto enqueue_raw = [&](const std::string& metadata_json, const std::string& action_json,
									 const std::string& key) {
			return ActorActionQueueRepository::Enqueue(database, {
				.actor_id = inserted_profile.actor_id,
				.source = "actor-events-test",
				.source_metadata_json = metadata_json,
				.action_type = "target",
				.action_json = action_json,
				.idempotency_key = key,
				.created_at = now,
			});
		};
		const auto malformed_watermark = enqueue_raw(
			R"json({"expected_event_id":"not-a-number"})json", R"json({"entity_id":1})json",
			fmt::format("bad-watermark-{}", run_nonce));
		executor.ProcessOne();
		ExpectEqual(ActorActionQueueRepository::FindOne(database, malformed_watermark.action_id).failure_reason,
			std::optional<std::string>("stale_event_watermark"),
			"non-numeric event watermarks should reject without coercion or exceptions");

		Json::Value valid_metadata;
		valid_metadata["expected_event_id"] = Json::UInt64(persisted_events[0].event_id);
		const auto metadata_json = Json::writeString(stale_writer, valid_metadata);
		for (const auto& [body_json, suffix] : std::vector<std::pair<std::string, std::string>>{
				 {R"json({"entity_id":"1"})json", "string"},
				 {R"json({"entity_id":-1})json", "negative"},
				 {R"json({"entity_id":65536})json", "oversized"}}) {
			const auto malformed_target = enqueue_raw(
				metadata_json, body_json, fmt::format("bad-target-{}-{}", suffix, run_nonce));
			executor.ProcessOne();
			ExpectEqual(ActorActionQueueRepository::FindOne(database, malformed_target.action_id).failure_reason,
				std::optional<std::string>("invalid_action_json"),
				"malformed target entity ids should reject without coercion or exceptions");
		}

		const auto rollback_action = enqueue("stand", stand_body, fmt::format("rollback-{}", run_nonce));
		const auto rollback_claim = ActorActionQueueRepository::ClaimNextEligibleForZone(
			database, {
						  .actor_id = inserted_profile.actor_id,
						  .bot_id = *inserted_profile.bot_id,
						  .owner_character_id = *inserted_profile.owner_character_id,
						  .zone_id = zone->GetZoneID(),
						  .instance_id = zone->GetInstanceID(),
						  .entity_id = fixture.OwnedBot()->GetID(),
						  .claimed_by = "actor-events-rollback-test",
						  .now = now,
					  });
		Expect(rollback_claim.has_value() && rollback_claim->action_id == rollback_action.action_id,
			   "failure protocol test should claim its action");
		database.TransactionBegin();
		Expect(ActorActionQueueRepository::MarkCompleted(
				   database, {rollback_action.action_id, std::string(R"json({"applied":true})json"), now})
				   .has_value(),
			   "failure protocol test should stage completion");
		Json::Value rollback_payload;
		rollback_payload["action_id"] = Json::UInt64(rollback_action.action_id);
		Expect(ActorEventsRepository::AppendEvent(database,
												  {
													  .actor_id = inserted_profile.actor_id,
													  .event_type = "action_completed",
													  .event_json = Json::writeString(stale_writer, rollback_payload),
													  .created_at = now,
												  })
					   .event_id != 0,
			   "failure protocol test should stage its correlated event");
		database.TransactionRollback();
		ExpectEqual(ActorActionQueueRepository::FindOne(database, rollback_action.action_id).state,
					std::string("claimed"), "rolled-back terminalization should remain recoverable");
		Expect(!ActorEventsRepository::HasActionOutcome(database, inserted_profile.actor_id, rollback_action.action_id),
			   "rolled-back completion must not leave a contradictory event");
		Expect(
			ActorActionQueueRepository::ReleaseClaim(database, rollback_action.action_id, "actor-events-rollback-test"),
			"rolled-back terminalization should release for retry");

		ExpectEqual(CountPlayerEventLogRowsWithMarker(speech_marker), int64_t(0),
					"runtime actor event persistence should not write marker rows to player_event_logs");

		EQ::ZoneHarness::ActorEventRecorder::ClearActiveRecorder(&recorder);
		std::cout << "[PASS] actor-events-runtime\n";
	} catch (const TestFailure& e) {
		std::cerr << "[FAIL] " << e.what() << "\n";
		std::exit(1);
	}
}

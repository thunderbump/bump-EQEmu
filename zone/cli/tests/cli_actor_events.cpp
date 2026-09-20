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
#include "common/eqemu_config.h"
#include "common/json/json.h"
#include "common/repositories/actor_action_queue_repository.h"
#include "common/repositories/actor_events_repository.h"
#include "common/repositories/actor_profiles_repository.h"
#include "common/repositories/actor_status_repository.h"
#include "common/repositories/player_event_logs_repository.h"
#include "common/rulesys.h"
#include "common/strings.h"
#include "common/timer.h"
#include "zone/bot.h"
#include "zone/client.h"
#include "zone/fallback_dialogue_runtime.h"
#include "zone/actor_action_executor.h"
#include "zone/actor_helper.h"
#include "zone/harness/actor_event_persistence_sink.h"
#include "zone/harness/actor_event_recorder.h"
#include "zone/harness/owned_bot_actor_fixture.h"
#include "zone/npc.h"
#include "zone/zone.h"
#include "zone/zonedb.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <cstdlib>
#include <exception>
#include <future>
#include <filesystem>
#include <fstream>
#ifndef _WIN32
#include <signal.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#endif
#include <iostream>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

extern double frame_time;

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

class ScopedTestDirectory {
public:
	explicit ScopedTestDirectory(std::filesystem::path path) : path_(std::move(path)) {
		std::filesystem::remove_all(path_);
		std::filesystem::create_directories(path_);
	}
	~ScopedTestDirectory() { std::filesystem::remove_all(path_); }
	const std::filesystem::path& Path() const { return path_; }
private:
	std::filesystem::path path_;
};

#ifndef _WIN32
pid_t SpawnActorHelper(uint32_t actor_id, const std::filesystem::path& state_directory, uint32_t max_cycles = 200,
					   uint32_t poll_ms = 20, bool enabled = true) {
	const auto actor = std::to_string(actor_id);
	const auto cycles = std::to_string(max_cycles);
	const auto poll = std::to_string(poll_ms);
	const auto zone_id = std::to_string(zone->GetZoneID());
	const auto instance_id = std::to_string(zone->GetInstanceID());
	const auto state = state_directory.string();
	const auto child = fork();
	if (child == 0) {
		if (enabled) {
			execl("/proc/self/exe", "zone", "actor-helper:run", "--enabled", "--actor-id", actor.c_str(),
				  "--zone-id", zone_id.c_str(), "--instance-id", instance_id.c_str(), "--state-dir", state.c_str(),
				  "--max-cycles", cycles.c_str(), "--poll-ms", poll.c_str(), "--exit-after-outcome",
				  static_cast<char*>(nullptr));
		} else {
			execl("/proc/self/exe", "zone", "actor-helper:run", "--actor-id", actor.c_str(),
				  "--state-dir", state.c_str(), static_cast<char*>(nullptr));
		}
		std::_Exit(127);
	}
	return child;
}

bool WaitForChild(pid_t child, std::chrono::milliseconds timeout, int* status = nullptr) {
	const auto deadline = std::chrono::steady_clock::now() + timeout;
	int local_status = 0;
	while (std::chrono::steady_clock::now() < deadline) {
		const auto result = waitpid(child, &local_status, WNOHANG);
		if (result == child) {
			if (status) *status = local_status;
			return true;
		}
		std::this_thread::sleep_for(std::chrono::milliseconds(10));
	}
	return false;
}
#endif

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
		if (actor_id > 0 && std::find(actor_ids_.begin(), actor_ids_.end(), actor_id) == actor_ids_.end()) {
			actor_ids_.push_back(actor_id);
		}
	}

	uint32_t reserved_owner_character_id = 0;

	bool Cleanup(std::string* failure_reason = nullptr) {
		bool ok = true;
		std::string failures;
		const auto remove = [&](const std::string& label, const std::string& statement) {
			auto result = database.QueryDatabase(statement);
			if (!result.Success()) {
				ok = false;
				failures += (failures.empty() ? std::string() : ",") + label;
			}
		};

		for (auto actor_id : actor_ids_) {
			remove("actor_action_queue", fmt::format("DELETE FROM actor_action_queue WHERE actor_id = {}", actor_id));
			remove("actor_events", fmt::format("DELETE FROM actor_events WHERE actor_id = {}", actor_id));
			remove("actor_status", fmt::format("DELETE FROM actor_status WHERE actor_id = {}", actor_id));
		}
		for (auto it = actor_ids_.rbegin(); it != actor_ids_.rend(); ++it) {
			remove("actor_profiles", fmt::format("DELETE FROM actor_profiles WHERE actor_id = {}", *it));
		}

		if (reserved_owner_character_id > 0) {
			auto owner_result = database.QueryDatabase(
				fmt::format("SELECT COUNT(*) FROM character_data WHERE id = {}", reserved_owner_character_id));
			if (!owner_result.Success() || owner_result.RowCount() != 1 || !owner_result.begin()[0]) {
				ok = false;
				failures += (failures.empty() ? std::string() : ",") + "reserved_owner_lookup";
			} else if (ok && strtoull(owner_result.begin()[0], nullptr, 10) > 0) {
				std::string rollback_reason;
				if (!EQ::Actor::ReservedOwners::Rollback(database, reserved_owner_character_id, &rollback_reason)) {
					ok = false;
					failures += (failures.empty() ? std::string() : ",") + "reserved_owner:" + rollback_reason;
				}
			}
		}
		if (ok) {
			actor_ids_.clear();
			reserved_owner_character_id = 0;
		}
		if (failure_reason) {
			*failure_reason = failures;
		}
		return ok;
	}

	~ActorEventPersistenceCleanup() {
		std::string failure_reason;
		if (!Cleanup(&failure_reason)) {
			std::cerr << "[CLEANUP-FAIL] actor-events-runtime: " << failure_reason << "\n";
		}
	}

private:
	std::vector<uint32_t> actor_ids_;
};

class ConfigurablePersistenceSink final : public EQ::ZoneHarness::ActorEventPersistenceSink {
public:
	EQ::ZoneHarness::ActorEventCaptureResult PersistSpeechEmitted(
		Mob*,
		const EQ::ZoneHarness::ActorEvent& event
	) override {
		attempted_texts.push_back(event.speech.text);
		return result;
	}

	EQ::ZoneHarness::ActorEventCaptureResult result =
		EQ::ZoneHarness::ActorEventCaptureResult::Saturated;
	std::vector<std::string> attempted_texts;
};

class ScopedRuleOverride {
public:
	ScopedRuleOverride(const std::string& name, const std::string& value) : name_(name) {
		if (RuleManager::Instance()->GetRule(name_, original_)) {
			changed_ = RuleManager::Instance()->SetRule(name_, value, nullptr, false, false);
		}
	}

	~ScopedRuleOverride() {
		if (changed_) {
			RuleManager::Instance()->SetRule(name_, original_, nullptr, false, false);
		}
	}

	bool Changed() const { return changed_; }

private:
	std::string name_;
	std::string original_;
	bool changed_ = false;
};

class BlockingPersistenceSink final : public EQ::ZoneHarness::ActorEventPersistenceSink {
public:
	EQ::ZoneHarness::ActorEventCaptureResult PersistSpeechEmitted(Mob*, const EQ::ZoneHarness::ActorEvent&) override {
		std::unique_lock lock(mutex_);
		persist_started_ = true;
		persist_started_cv_.notify_all();
		release_persist_cv_.wait(lock, [this]() { return allow_persist_to_finish_; });
		persist_finished_ = true;
		persist_finished_cv_.notify_all();
		return EQ::ZoneHarness::ActorEventCaptureResult::Accepted;
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

	// Keep the objects alive in the timeout path. Detached workers let this proof
	// report a bounded failure instead of blocking in thread::join or an async
	// future destructor if recorder teardown regresses.
	auto recorder = std::make_shared<EQ::ZoneHarness::ActorEventRecorder>();
	auto blocking_sink = std::make_shared<BlockingPersistenceSink>();
	recorder->SetPersistenceSink(blocking_sink.get());
	EQ::ZoneHarness::ActorEventRecorder::RegisterActiveRecorder(recorder.get());

	std::promise<void> observe_done_promise;
	auto observe_done = observe_done_promise.get_future();
	std::thread([recorder, blocking_sink, done = std::move(observe_done_promise)]() mutable {
		(void)blocking_sink;
		try {
			EQ::ZoneHarness::ActorEventRecorder::ObserveSpeechEmitted(nullptr, "say", "teardown-sync", 200);
			done.set_value();
		} catch (...) {
			done.set_exception(std::current_exception());
		}
	}).detach();

	const bool persist_started = blocking_sink->WaitUntilPersistStarted(1s);
	if (!persist_started) {
		// The observer may enter the sink immediately after the deadline. Always
		// release it before starting teardown so neither worker can deadlock.
		blocking_sink->AllowPersistToFinish();
	}

	std::promise<void> clear_done_promise;
	auto clear_done = clear_done_promise.get_future();
	std::thread([recorder, blocking_sink, done = std::move(clear_done_promise)]() mutable {
		(void)blocking_sink;
		try {
			EQ::ZoneHarness::ActorEventRecorder::ClearActiveRecorder(recorder.get());
			done.set_value();
		} catch (...) {
			done.set_exception(std::current_exception());
		}
	}).detach();

	const auto clear_status_while_blocked = clear_done.wait_for(100ms);
	blocking_sink->AllowPersistToFinish();
	const bool persist_finished = blocking_sink->WaitUntilPersistFinished(1s);
	const auto observe_status_after_release = observe_done.wait_for(1s);
	const auto clear_status_after_release = clear_done.wait_for(1s);

	// get() is safe only after the corresponding deadline reports ready.
	if (observe_status_after_release == std::future_status::ready) {
		observe_done.get();
	}
	if (clear_status_after_release == std::future_status::ready) {
		clear_done.get();
	}
	if (observe_status_after_release == std::future_status::ready &&
		clear_status_after_release == std::future_status::ready) {
		recorder->SetPersistenceSink(nullptr);
	}

	Expect(persist_started, "blocking persistence sink should observe an in-flight speech callback");
	Expect(clear_status_while_blocked == std::future_status::timeout,
		   "active recorder teardown should wait for in-flight callbacks before returning");
	Expect(persist_finished, "blocking persistence sink should finish after release");
	Expect(observe_status_after_release == std::future_status::ready,
		   "in-flight speech callback should finish within the teardown deadline");
	Expect(clear_status_after_release == std::future_status::ready,
		   "active recorder teardown should finish once in-flight callbacks drain");
	ExpectEqual(recorder->Since(0, 4).size(), static_cast<size_t>(1),
				"blocked speech callback should still record one actor event before teardown completes");
}

void ExpectSpeechDefersAndRetriesThroughProductionPath(
	Bot* actor,
	EQ::ZoneHarness::ActorEventRecorder& recorder,
	EQ::ZoneHarness::ActorEventPersistenceSink* original_sink
) {
	using EQ::ZoneHarness::ActorEventCaptureResult;

	ConfigurablePersistenceSink controlled_sink;
	recorder.SetPersistenceSink(&controlled_sink);
	ScopedRuleOverride saylink_rule("Chat:AutoInjectSaylinksToSay", "false");

	{
		ScopedRuleOverride dialogue_rule("Chat:QuestDialogueUsesDialogueWindow", "false");
		const auto cursor = recorder.MaxEventID();
		Expect(!actor->Say("%s", "deferred-normal-speech"),
			   "normal speech should report evidence saturation to its queued caller");
		Expect(recorder.Since(cursor, 4).empty(),
			   "saturated normal speech must not be recorded as emitted");

		controlled_sink.result = ActorEventCaptureResult::Accepted;
		Expect(actor->Say("%s", "deferred-normal-speech"),
			   "normal speech should succeed when evidence capacity recovers");
		const auto recovered = recorder.Since(cursor, 4);
		ExpectEqual(recovered.size(), static_cast<size_t>(1),
					"retried normal speech should produce exactly one emitted event");
	}

	{
		ScopedRuleOverride dialogue_rule("Chat:QuestDialogueUsesDialogueWindow", "true");
		controlled_sink.result = ActorEventCaptureResult::Saturated;
		const auto cursor = recorder.MaxEventID();
		Expect(!actor->Say("%s", "deferred-dialogue-window-speech"),
			   "dialogue-window speech should defer before rendering when evidence is saturated");
		Expect(recorder.Since(cursor, 4).empty(),
			   "saturated dialogue-window speech must not bypass required evidence");

		controlled_sink.result = ActorEventCaptureResult::Accepted;
		Expect(actor->Say("%s", "deferred-dialogue-window-speech"),
			   "dialogue-window speech should retry after evidence capacity recovers");
		const auto recovered = recorder.Since(cursor, 4);
		ExpectEqual(recovered.size(), static_cast<size_t>(1),
					"retried dialogue-window speech should produce exactly one emitted event");
		ExpectEqual(recovered[0].speech.text, std::string("deferred-dialogue-window-speech"),
					"dialogue-window evidence should preserve the rendered speech text");
	}

	{
		controlled_sink.result = ActorEventCaptureResult::Saturated;
		const auto cursor = recorder.MaxEventID();
		Expect(!actor->Emote("%s", "deferred-emote"),
			   "emotes should report evidence saturation before emission");
		Expect(recorder.Since(cursor, 4).empty(), "saturated emotes must not be recorded as emitted");

		controlled_sink.result = ActorEventCaptureResult::Accepted;
		Expect(actor->Emote("%s", "deferred-emote"), "emotes should retry after evidence capacity recovers");
		const auto recovered = recorder.Since(cursor, 4);
		ExpectEqual(recovered.size(), static_cast<size_t>(1),
					"retried emotes should produce exactly one emitted event");
		ExpectEqual(recovered[0].speech.channel, std::string("emote"),
					"retried emote evidence should preserve its channel");
	}

	ExpectEqual(controlled_sink.attempted_texts.size(), static_cast<size_t>(6),
				"speech and emote paths should capture before each deferred or emitted action");
	recorder.Drain();
	recorder.SetPersistenceSink(original_sink);
}

void ExpectBoundedPersistenceOverloadAndRecovery() {
	using namespace std::chrono_literals;
	using EQ::ZoneHarness::ActorEventCaptureResult;
	using PersistenceSink = EQ::ZoneHarness::ActorEventRepositoryPersistenceSink;

	const auto pending = [](uint32_t sequence) {
		return PersistenceSink::PendingSpeechEvent{
			.bot_id = sequence,
			.entity_id = sequence,
			.zone_id = 2,
			.instance_id = 0,
			.channel = "say",
			.text = fmt::format("bounded-event-{}", sequence),
			.audible_radius = 200,
		};
	};

	std::mutex slow_mutex;
	std::condition_variable slow_started_cv;
	std::condition_variable slow_release_cv;
	bool slow_started = false;
	bool slow_released = false;
	std::vector<uint32_t> persisted_order;
	PersistenceSink burst_sink(3, 512, [&](const PersistenceSink::PendingSpeechEvent& event) {
		std::unique_lock lock(slow_mutex);
		if (!slow_started) {
			slow_started = true;
			slow_started_cv.notify_all();
			if (!slow_release_cv.wait_for(lock, 1s, [&]() { return slow_released; })) {
				return false;
			}
		}
		persisted_order.push_back(event.bot_id);
		return true;
	});

	const auto capture_started = std::chrono::steady_clock::now();
	ExpectEqual(burst_sink.Enqueue(pending(1)), ActorEventCaptureResult::Accepted,
				"normal evidence capture should enter the bounded queue");
	const auto capture_elapsed = std::chrono::steady_clock::now() - capture_started;
	{
		std::unique_lock lock(slow_mutex);
		Expect(slow_started_cv.wait_for(lock, 1s, [&]() { return slow_started; }),
			   "slow persistence probe should begin flushing");
	}
	ExpectEqual(burst_sink.Enqueue(pending(2)), ActorEventCaptureResult::Accepted,
				"burst evidence should use remaining bounded capacity");
	ExpectEqual(burst_sink.Enqueue(pending(3)), ActorEventCaptureResult::Accepted,
				"burst evidence should fill the bounded capacity");
	ExpectEqual(burst_sink.Enqueue(pending(4)), ActorEventCaptureResult::Saturated,
				"saturation must visibly defer new consequential evidence instead of evicting required proof");
	const auto saturated = burst_sink.GetMetrics();
	ExpectEqual(saturated.queue_records, uint64_t(3), "slow persistence must retain at most the configured records");
	Expect(saturated.queue_bytes <= 512, "slow persistence must retain at most the configured bytes");
	ExpectEqual(saturated.saturated_records, uint64_t(1), "overload should be counted explicitly");
	Expect(capture_elapsed < 100ms, "capture should not wait for slow persistence on the zone-facing path");
	{
		std::lock_guard lock(slow_mutex);
		slow_released = true;
		slow_release_cv.notify_all();
	}
	Expect(burst_sink.FlushFor(1s), "slow persistence should drain after it recovers");
	const auto burst_complete = burst_sink.GetMetrics();
	ExpectEqual(persisted_order, std::vector<uint32_t>({1, 2, 3}),
				"recovery should flush retained required evidence in capture order");

	std::atomic<bool> persistence_available{false};
	PersistenceSink unavailable_sink(
		2, 512, [&](const PersistenceSink::PendingSpeechEvent&) { return persistence_available.load(); });
	ExpectEqual(unavailable_sink.Enqueue(pending(10)), ActorEventCaptureResult::Accepted,
				"unavailable persistence should retain the first event");
	ExpectEqual(unavailable_sink.Enqueue(pending(11)), ActorEventCaptureResult::Accepted,
				"unavailable persistence should retain bounded recovery work");
	std::this_thread::sleep_for(75ms);
	ExpectEqual(unavailable_sink.Enqueue(pending(12)), ActorEventCaptureResult::Saturated,
				"unavailable persistence should reject rather than grow without bound");
	Expect(!unavailable_sink.FlushFor(75ms), "unavailable persistence must not claim a successful flush");
	const auto unavailable = unavailable_sink.GetMetrics();
	Expect(unavailable.persistence_failures > 0, "persistence failures should be counted");
	ExpectEqual(unavailable.queue_records, uint64_t(2), "failed writes should remain queued for recovery");
	Expect(unavailable.queue_bytes <= 512, "failed writes should remain within the byte bound");
	persistence_available.store(true);
	Expect(unavailable_sink.FlushFor(1s), "retained evidence should flush after persistence recovery");
	const auto recovered = unavailable_sink.GetMetrics();
	ExpectEqual(recovered.persisted_records, uint64_t(2), "recovery should retain and persist both accepted events");
	ExpectEqual(recovered.queue_records, uint64_t(0), "recovery should empty the queue");

	std::cout << "[METRIC] actor-evidence attempted_records="
			  << burst_complete.attempted_records + recovered.attempted_records
			  << " attempted_bytes=" << burst_complete.attempted_bytes + recovered.attempted_bytes
			  << " accepted_records=" << burst_complete.accepted_records + recovered.accepted_records
			  << " persisted_records=" << burst_complete.persisted_records + recovered.persisted_records
			  << " queue_high_water_records="
			  << std::max(burst_complete.queue_high_water_records, recovered.queue_high_water_records)
			  << " queue_high_water_bytes="
			  << std::max(burst_complete.queue_high_water_bytes, recovered.queue_high_water_bytes)
			  << " saturation=" << burst_complete.saturated_records + recovered.saturated_records
			  << " failures=" << burst_complete.persistence_failures + recovered.persistence_failures
			  << " capture_ns=" << burst_complete.capture_nanoseconds + recovered.capture_nanoseconds
			  << " flush_ns=" << burst_complete.flush_nanoseconds + recovered.flush_nanoseconds << "\n";
}


// Use the normal delayed-reply processor and Mob emission paths, with only
// provider completion and evidence capacity controlled by the scenario.
void ExpectFallbackRepliesRetryInOrder(
	EQ::ZoneHarness::OwnedBotActorFixture& fixture,
	EQ::ZoneHarness::ActorEventRecorder& recorder,
	EQ::ZoneHarness::ActorEventPersistenceSink* original_sink
) {
	using EQ::ZoneHarness::ActorEventCaptureResult;
	FallbackDialogue::ResetDialogueCooldowns();
	FallbackDialogue::TestDelayedDialogueProvider provider;
	FallbackDialogue::FallbackDialogueSettings settings;
	settings.immediate.enabled = true;
	settings.immediate.cooldown_seconds = 0;
	settings.current_interaction.imported_game_rule_say_range = RuleI(Range, Say);
	FallbackDialogue::DelayedDialogueQueue queue(provider, settings);
	ZoneFallbackDialogueRuntime::DelayedDialogueDelivery delivery;
	ConfigurablePersistenceSink sink;
	struct RestorePersistenceSink {
		EQ::ZoneHarness::ActorEventRecorder& recorder;
		EQ::ZoneHarness::ActorEventPersistenceSink* original;
		~RestorePersistenceSink() { recorder.SetPersistenceSink(original); }
	} restore_sink{recorder, original_sink};
	recorder.SetPersistenceSink(&sink);
	fixture.OwnerTargets(fixture.OwnedBot());
	const auto enqueue = [&](const std::string& response) {
		const auto queued = queue.HandleTargetedSay({
			.speaker_id = fixture.Owner()->GetID(),
			.target_id = fixture.OwnedBot()->GetID(),
			.message = "hello",
			.target_type = FallbackDialogue::TargetType::Bot,
		}, {
			.current_message = "hello",
			.speaker = {.name = fixture.Owner()->GetCleanName()},
			.target = {.name = fixture.OwnedBot()->GetCleanName()},
		});
		Expect(queued.handled, "fallback probe must queue an eligible interaction");
		Expect(provider.CompleteNextSuccess(response), "fallback provider must complete the queued request");
	};

	for (const auto& response : {std::string("First reply."), std::string("*nods quietly*")}) {
		recorder.Drain();
		sink.attempted_texts.clear();
		sink.result = ActorEventCaptureResult::Saturated;
		enqueue(response);
		enqueue("Later reply.");
		delivery.Process(queue);
		delivery.Process(queue);
		ExpectEqual(sink.attempted_texts.size(), size_t(2), "each blocked tick must attempt only the retained reply");
		ExpectEqual(sink.attempted_texts[0], sink.attempted_texts[1], "retry must keep the same reply ahead of later replies");
		Expect(recorder.Since(0, 8).empty(), "deferred fallback must not be recorded as emitted");
		sink.result = ActorEventCaptureResult::Accepted;
		delivery.Process(queue);
		delivery.Process(queue);
		const auto events = recorder.Since(0, 8);
		ExpectEqual(events.size(), size_t(2), "recovery must deliver both replies exactly once");
		ExpectEqual(events[0].speech.text, sink.attempted_texts[0], "recovered reply must retain its text and order");
		ExpectEqual(events[1].speech.text, std::string("Later reply."), "later reply must follow the deferred reply");
	}

	recorder.Drain();
	sink.result = ActorEventCaptureResult::Saturated;
	enqueue("Stale reply.");
	delivery.Process(queue);
	fixture.OwnerTargets(nullptr);
	recorder.Drain(); // Exclude the setup target-change event from delivery observations.
	sink.result = ActorEventCaptureResult::Accepted;
	delivery.Process(queue);
	Expect(recorder.Since(0, 8).empty(), "a deferred reply must be discarded if the speaker changes target");
	fixture.OwnerTargets(fixture.OwnedBot());
	recorder.Drain();
	delivery.Process(queue);
	Expect(recorder.Since(0, 8).empty(), "discarded stale replies must not reappear");

	sink.result = ActorEventCaptureResult::Saturated;
	enqueue("Out of range reply.");
	delivery.Process(queue);
	sink.result = ActorEventCaptureResult::Accepted;
	{
		ScopedRuleOverride range("Range:Say", "1");
		Expect(range.Changed(), "range override must apply for the stale delivery probe");
		delivery.Process(queue);
	}
	delivery.Process(queue);
	Expect(recorder.Since(0, 8).empty(), "out-of-range replies must not reappear after range recovers");

	for (size_t i = 0; i < 10; ++i) {
		enqueue("Bounded reply.");
	}
	delivery.Process(queue);
	ExpectEqual(recorder.Since(0, 32).size(), size_t(8), "a tick must deliver at most eight ready replies");
	delivery.Process(queue);
	ExpectEqual(recorder.Since(0, 32).size(), size_t(10), "later ticks must deliver the remaining replies");
	recorder.Drain();
	recorder.SetPersistenceSink(original_sink);
	FallbackDialogue::ResetDialogueCooldowns();
}

// A non-cooperative persistence operation must not own the sink's lifetime.
void ExpectShutdownRetainsRecoverablePayloads() {
#ifndef _WIN32
	using namespace std::chrono_literals;
	using Sink = EQ::ZoneHarness::ActorEventRepositoryPersistenceSink;
	std::string pattern = (std::filesystem::temp_directory_path() / "actor-recovery-test-XXXXXX").string();
	std::vector<char> name(pattern.begin(), pattern.end());
	name.push_back(0);
	Expect(mkdtemp(name.data()) != nullptr, "recovery test must create a private directory");
	const std::filesystem::path directory(name.data());
	struct CleanupDirectory {
		std::filesystem::path path;
		~CleanupDirectory() { std::error_code error; std::filesystem::remove_all(path, error); }
	} cleanup{directory};
	struct Gate {
		std::promise<void> entered;
		std::promise<void> release;
		std::shared_future<void> released = release.get_future().share();
		std::promise<void> finished;
	};
	auto gate = std::make_shared<Gate>();
	auto entered = gate->entered.get_future();
	auto finished = gate->finished.get_future();
	Sink::ShutdownResult result;
	{
		Sink sink(2, 1024, [gate](const auto&) {
			gate->entered.set_value();
			gate->released.wait_for(5s);
			gate->finished.set_value();
			return true; // A late success must not invalidate the uncertain recovery receipt.
		});
		Expect(sink.Enqueue({.bot_id = 42, .channel = "say", .text = "in-flight payload"}) ==
			EQ::ZoneHarness::ActorEventCaptureResult::Accepted, "shutdown probe must admit first payload");
		Expect(entered.wait_for(2s) == std::future_status::ready, "shutdown probe must block inside persistence");
		Expect(sink.Enqueue({.bot_id = 43, .channel = "emote", .text = "queued payload"}) ==
			EQ::ZoneHarness::ActorEventCaptureResult::Accepted, "shutdown probe must admit queued payload");
		const auto started = std::chrono::steady_clock::now();
		result = sink.ShutdownFor(50ms, directory.string());
		Expect(std::chrono::steady_clock::now() - started < 500ms, "shutdown must not wait for the blocked callback");
		Expect(!result.drained && !result.worker_stopped, "shutdown must report incomplete persistence");
		Expect(result.in_flight_outcome_unknown, "in-flight write must be marked uncertain");
		ExpectEqual(result.retained_records, size_t(2), "shutdown must retain both accepted records");
		Expect(result.error.empty() && !result.recovery_path.empty(), "shutdown must publish a recovery path");
		Expect(sink.Enqueue({.text = "late"}) == EQ::ZoneHarness::ActorEventCaptureResult::Stopped,
			"shutdown must stop admission");
		ExpectEqual(sink.ShutdownFor(1ms, directory.string()).recovery_path, result.recovery_path,
			"repeated shutdown must return the same recovery file");
	}
	std::ifstream input(result.recovery_path);
	const std::string saved((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
	const auto document = ParseJson(saved);
	ExpectEqual(document["records"].size(), Json::ArrayIndex(2), "payloads must remain readable after sink destruction");
	ExpectEqual(document["records"][0]["text"].asString(), std::string("in-flight payload"), "recovery must retain in-flight text");
	ExpectEqual(document["records"][1]["text"].asString(), std::string("queued payload"), "recovery must retain queued text");
	Expect(!document["automatic_replay_safe"].asBool(), "recovery must not permit blind replay of uncertain writes");
	struct stat permissions{};
	Expect(stat(result.recovery_path.c_str(), &permissions) == 0 && (permissions.st_mode & 0777) == 0600,
		"recovery file must be private to its owner");
	gate->release.set_value();
	Expect(finished.wait_for(2s) == std::future_status::ready, "detached operation must be able to finish safely");

	// A local filesystem failure must be explicit and retryable before destruction.
	const auto blocked_path = directory / "not-a-directory";
	{ std::ofstream file(blocked_path); file << "occupied"; }
	Sink unavailable(1, 1024, [](const auto&) { return false; });
	Expect(unavailable.Enqueue({.bot_id = 44, .channel = "say", .text = "retry-file"}) ==
		EQ::ZoneHarness::ActorEventCaptureResult::Accepted, "file failure probe must admit evidence");
	const auto failed = unavailable.ShutdownFor(20ms, blocked_path.string());
	Expect(!failed.error.empty() && !failed.drained, "file failure must not claim successful recovery");
	const auto recovered = unavailable.ShutdownFor(100ms, directory.string());
	Expect(recovered.error.empty() && !recovered.recovery_path.empty(), "file publication must retry without repeating persistence");

	Sink empty;
	const auto drained = empty.ShutdownFor(100ms, directory.string());
	Expect(drained.drained && drained.recovery_path.empty() && drained.error.empty(), "empty shutdown must need no recovery file");
#endif
}

// Only terminate a test-owned connection in the disposable scenario database.
void ExpectBorrowedConnectionReconnects() {
	const auto* config = EQEmuConfig::get();
	Expect(config != nullptr, "reconnect probe needs database configuration");
	Database owner;
	owner.SetConnectionTimeouts(1, 1, 1);
	Expect(owner.Connect(config->DatabaseHost, config->DatabaseUsername, config->DatabasePassword,
		config->DatabaseDB, config->DatabasePort, "borrowed-reconnect-probe"), "probe owner must connect");
	{
		Database borrower;
		borrower.SetMySQL(owner);
		for (const bool via_borrower : {true, false}) {
			auto before = owner.QueryDatabase("SELECT CONNECTION_ID()");
			Expect(before.Success() && before.RowCount() == 1, "probe owner needs connection id");
			const auto old_id = std::stoull(before.begin()[0]);
			Expect(database.QueryDatabase(fmt::format("KILL CONNECTION {}", old_id)).Success(),
				"probe must disconnect only its test-owned connection");
			auto recovered = (via_borrower ? borrower : owner).QueryDatabase("SELECT CONNECTION_ID()");
			Expect(recovered.Success() && recovered.RowCount() == 1, "owner and borrower must recover a lost connection");
			const auto new_id = std::stoull(recovered.begin()[0]);
			Expect(new_id != old_id, "recovery must establish a new connection");
			auto shared = (via_borrower ? owner : borrower).QueryDatabase("SELECT CONNECTION_ID()");
			Expect(shared.Success() && shared.RowCount() == 1 && std::stoull(shared.begin()[0]) == new_id,
				"both wrappers must use the same recovered owner connection");
		}
	}
	Expect(owner.QueryDatabase("SELECT 1").Success(), "borrower destruction must preserve owner connection");
}

// Exercise the same connection factory used by the real persistence adapter.
void ExpectPersistenceConnectionIsolated(
	const ActorProfilesRepository::ActorProfileRecord& profile
) {
	using namespace std::chrono_literals;
	using Sink = EQ::ZoneHarness::ActorEventRepositoryPersistenceSink;
	auto zone_id_result = database.QueryDatabase("SELECT CONNECTION_ID()");
	Expect(zone_id_result.Success() && zone_id_result.RowCount() == 1, "zone connection must be available");
	const auto zone_connection_id = std::stoull(zone_id_result.begin()[0]);
	struct Probe {
		std::promise<uint64_t> connection_id;
		std::atomic<bool> first{true};
		std::atomic<bool> slow_query_failed{false};
	};
	auto probe = std::make_shared<Probe>();
	auto connection_future = probe->connection_id.get_future();
	Sink sink(1, 1024, [probe](const auto& event) {
		if (probe->first.exchange(false)) {
			auto* connection = Sink::RepositoryConnection();
			if (!connection) {
				probe->connection_id.set_value(0);
				return false;
			}
			auto id = connection->QueryDatabase("SELECT CONNECTION_ID()");
			probe->connection_id.set_value(id.Success() && id.RowCount() == 1 ? std::stoull(id.begin()[0]) : 0);
			// A five-second server wait must hit the dedicated client's read deadline.
			probe->slow_query_failed = !connection->QueryDatabase(std::string("SELECT SLEEP(5)"), false).Success();
		}
		return Sink::PersistToRepository(event);
	});
	const auto marker = fmt::format("isolated-persistence-{}", profile.actor_id);
	Expect(sink.Enqueue({.bot_id = *profile.bot_id, .channel = "say", .text = marker}) ==
		EQ::ZoneHarness::ActorEventCaptureResult::Accepted, "isolation probe must enqueue");
	Expect(connection_future.wait_for(2s) == std::future_status::ready, "worker must connect promptly");
	const auto worker_id = connection_future.get();
	Expect(worker_id != 0 && worker_id != zone_connection_id, "persistence must own a separate server connection");
	bool observed_sleep = false;
	const auto observation_deadline = std::chrono::steady_clock::now() + 2s;
	while (!observed_sleep && std::chrono::steady_clock::now() < observation_deadline) {
		auto active = database.QueryDatabase(fmt::format(
			"SELECT COUNT(*) FROM information_schema.PROCESSLIST WHERE ID = {} AND INFO LIKE 'SELECT SLEEP%'", worker_id));
		Expect(active.Success() && active.RowCount() == 1, "zone must inspect worker activity independently");
		observed_sleep = std::stoull(active.begin()[0]) == 1;
		if (!observed_sleep) { std::this_thread::sleep_for(5ms); }
	}
	Expect(observed_sleep, "probe must observe a slow production persistence connection");
	const auto started = std::chrono::steady_clock::now();
	Expect(database.QueryDatabase("SELECT 1").Success(), "zone query must succeed during persistence I/O");
	Expect(std::chrono::steady_clock::now() - started < 500ms, "slow persistence must not hold the zone query mutex");
	Expect(sink.FlushFor(4s), "persistence must reconnect and recover after its read deadline");
	Expect(probe->slow_query_failed, "slow query must time out rather than waiting its full server duration");
	const auto rows = ActorEventsRepository::ReadCursor(database, profile.actor_id, 0, 32);
	ExpectEqual(rows.size(), size_t(1), "isolated worker must persist exactly one event");
	ExpectEqual(ParseJson(rows[0].event_json)["text"].asString(), marker, "isolated persistence must preserve payload");
	Expect(database.QueryDatabase(fmt::format("DELETE FROM actor_events WHERE actor_id = {}", profile.actor_id)).Success(),
		   "isolation probe must clean its row");
}

// Hold the worker before its real repository lookup, then change the profile.
// The retry cases inject one unavailable-store result before that lookup.
void ExpectProfileChangesRetainAcceptedEvidence(
	const ActorProfilesRepository::ActorProfileRecord& profile
) {
	using namespace std::chrono_literals;
	using Sink = EQ::ZoneHarness::ActorEventRepositoryPersistenceSink;
	using EQ::ZoneHarness::ActorEventCaptureResult;

	for (const bool retry : {false, true}) {
		for (const bool deleted : {false, true}) {
			struct LookupGate {
				std::promise<void> reached;
				std::promise<void> release;
				std::shared_future<void> released = release.get_future().share();
				std::atomic<unsigned> attempts{0};
			};
			auto gate = std::make_shared<LookupGate>();
			auto reached_future = gate->reached.get_future();
			Sink sink(1, 1024, [gate, retry](const auto& event) {
				const auto attempt = gate->attempts.fetch_add(1);
				if (retry && attempt == 0) {
					return false;
				}
				if (attempt == (retry ? 1u : 0u)) {
					gate->reached.set_value();
					if (gate->released.wait_for(2s) != std::future_status::ready) {
						return false;
					}
				}
				return Sink::PersistToRepository(event);
			});
			const auto marker = fmt::format("profile-retention-{}-{}-{}", profile.actor_id, retry, deleted);
			Sink::PendingSpeechEvent event{
				.bot_id = *profile.bot_id,
				.channel = "say",
				.text = marker,
			};
			Expect(sink.Enqueue(event) == ActorEventCaptureResult::Accepted,
				   "profile lifecycle probe must admit evidence before changing the profile");
			Expect(reached_future.wait_for(2s) == std::future_status::ready,
				   "worker must reach the controlled first lookup or retry");
			const auto changed = database.QueryDatabase(deleted
				? fmt::format("DELETE FROM actor_profiles WHERE actor_id = {}", profile.actor_id)
				: fmt::format("UPDATE actor_profiles SET enabled = 0 WHERE actor_id = {}", profile.actor_id));
			Expect(changed.Success(), "profile lifecycle mutation must succeed");
			gate->release.set_value();
			Expect(!sink.FlushFor(100ms), "missing/disabled profile must not acknowledge accepted evidence");
			const auto retained = sink.GetMetrics();
			ExpectEqual(retained.queue_records, uint64_t(1), "accepted payload must remain queued");
			ExpectEqual(retained.persisted_records, uint64_t(0), "unwritten payload must not count as persisted");
			Expect(retained.persistence_failures > (retry ? 1u : 0u),
				   "real repository lookup must record a failure after the profile change");
			Expect(sink.Enqueue(event) == ActorEventCaptureResult::Saturated,
				   "retained evidence must keep the bounded queue full rather than being evicted");
			Expect(sink.GetMetrics().queue_bytes <= 1024, "profile failure must preserve the byte bound");

			const auto restored = ActorProfilesRepository::UpsertBotBackedProfile(database, profile);
			ExpectEqual(restored.actor_id, profile.actor_id, "recovery must restore the same actor identity");
			Expect(sink.FlushFor(2s), "retained evidence must persist when its profile recovers");
			const auto rows = ActorEventsRepository::ReadCursor(database, profile.actor_id, 0, 32);
			const auto matches = std::count_if(rows.begin(), rows.end(), [&](const auto& row) {
				return ParseJson(row.event_json)["text"].asString() == marker;
			});
			ExpectEqual(matches, decltype(matches)(1), "recovery must store the original payload exactly once");
			ExpectEqual(sink.GetMetrics().persisted_records, uint64_t(1), "only the recovered write counts");
			ExpectEqual(sink.GetMetrics().queue_records, uint64_t(0), "recovery must release queue capacity");
			Expect(database.QueryDatabase(fmt::format("DELETE FROM actor_events WHERE actor_id = {}", profile.actor_id)).Success(),
				   "lifecycle probe must remove its persisted rows before the next case");
		}
	}
}

void ExpectAllowlistedMistyHunt(EQ::ZoneHarness::OwnedBotActorFixture& fixture,
	const ActorProfilesRepository::ActorProfileRecord& profile, uint32_t run_nonce) {
	fixture.MoveParty(glm::vec4(-2100.0f, 400.0f, -3.0f, 0.0f));
	// Isolate scenario-owned candidates without changing production selection.
	// Restore the zone population even when an assertion throws.
	struct ScopedNaturalCandidateMask {
		std::vector<uint16_t> candidate_ids;
		~ScopedNaturalCandidateMask() {
			for (auto candidate_id : candidate_ids) {
				if (auto* candidate = entity_list.GetNPCByID(candidate_id)) candidate->SetTargetable(true);
			}
		}
	} natural_candidate_mask;
	for (const auto& [id, candidate] : entity_list.GetNPCList()) {
		if (candidate && (candidate->GetNPCTypeID() == 33005 || candidate->GetNPCTypeID() == 33160 ||
			candidate->GetNPCTypeID() == 33024)) {
			candidate->SetTargetable(false);
			natural_candidate_mask.candidate_ids.push_back(id);
		}
	}
	const auto now = std::time(nullptr);
	ActorStatusRepository::UpsertOne(database, {
		.actor_id = profile.actor_id, .zone_id = zone->GetZoneID(), .instance_id = zone->GetInstanceID(),
		.entity_id = fixture.OwnedBot()->GetID(), .state = "active", .heartbeat_at = now,
	});

	Json::StreamWriterBuilder writer;
	writer["indentation"] = "";
	Json::Value valid_body;
	valid_body["area"] = "misty-local-v1";
	const auto enqueue = [&](const std::string& suffix, const Json::Value& body,
		std::optional<time_t> expires_at = std::nullopt) {
		return ActorActionQueueRepository::Enqueue(database, {
			.actor_id = profile.actor_id, .source = "actor-hunt-runtime-test",
			.action_type = "hunt_one_allowlisted_target", .action_json = Json::writeString(writer, body),
			.idempotency_key = fmt::format("hunt-{}-{}", suffix, run_nonce),
			.expires_at = expires_at.value_or(now + 30), .created_at = now,
		});
	};
	const auto clear_combat = [&]() {
		fixture.OwnedBot()->WipeHateList();
		fixture.OwnedBot()->SetTarget(nullptr);
		fixture.OwnedBot()->SetAttackFlag(false);
		fixture.OwnedBot()->ClearCommandTargetSource();
		for (auto* follower : fixture.FollowerBots()) {
			follower->WipeHateList();
			follower->SetTarget(nullptr);
			follower->SetAttackFlag(false);
			follower->ClearCommandTargetSource();
		}
	};

	ActorActionExecutor executor(database, zone->GetZoneID(), zone->GetInstanceID(), zone->GetZoneServerId());
	Json::Value illegal_body;
	illegal_body["area"] = "unbounded-caller-area";
	const auto illegal = enqueue("illegal", illegal_body);
	executor.ProcessOne();
	ExpectEqual(ActorActionQueueRepository::FindOne(database, illegal.action_id).failure_reason,
		std::optional<std::string>("illegal_hunt_request"), "unapproved hunt areas must be visibly rejected");

	const auto unbounded = ActorActionQueueRepository::Enqueue(database, {
		.actor_id = profile.actor_id, .source = "actor-hunt-runtime-test",
		.action_type = "hunt_one_allowlisted_target", .action_json = Json::writeString(writer, valid_body),
		.idempotency_key = fmt::format("hunt-unbounded-{}", run_nonce), .created_at = now,
	});
	executor.ProcessOne();
	ExpectEqual(ActorActionQueueRepository::FindOne(database, unbounded.action_id).failure_reason,
		std::optional<std::string>("illegal_hunt_request"),
		"hunt requests without a finite deadline must be visibly rejected");

	auto* busy_target = fixture.AddHostileNPC({
		.name = "HarnessFollowerBusyTarget", .position = glm::vec4(-2080.0f, 400.0f, -3.0f, 0.0f),
	});
	Expect(busy_target && !fixture.FollowerBots().empty(), "follower readiness fixture should materialize");
	auto* busy_follower = fixture.FollowerBots().front();
	busy_follower->AddToHateList(busy_target, 1);
	Expect(busy_follower->IsEngaged(), "follower readiness setup must establish ordinary combat state");
	const auto follower_busy = enqueue("follower-busy", valid_body);
	executor.ProcessOne();
	ExpectEqual(ActorActionQueueRepository::FindOne(database, follower_busy.action_id).failure_reason,
		std::optional<std::string>("actor_not_ready"),
		"an engaged follower must prevent the Party hunt from being committed");
	Expect(busy_follower->CheckAggro(busy_target),
		"readiness rejection must not overwrite a follower's unrelated combat intent");
	busy_follower->WipeHateList();
	busy_follower->SetTarget(nullptr);
	fixture.RemoveMob(busy_target);

	busy_follower->SetHP(0);
	const auto follower_dead = enqueue("follower-dead", valid_body);
	executor.ProcessOne();
	ExpectEqual(ActorActionQueueRepository::FindOne(database, follower_dead.action_id).failure_reason,
		std::optional<std::string>("actor_not_ready"),
		"a dead follower must prevent the Party hunt from being committed");
	busy_follower->SetHP(busy_follower->GetMaxHP());

	auto* non_allowlisted = fixture.AddHostileNPC({
		.name = "HarnessNonAllowlistedTarget", .position = glm::vec4(-2080.0f, 400.0f, -3.0f, 0.0f),
	});
	Expect(non_allowlisted, "non-allowlisted hunt fixture should create an NPC");
	const auto non_allowlisted_action = enqueue("non-allowlisted", valid_body);
	executor.ProcessOne();
	ExpectEqual(ActorActionQueueRepository::FindOne(database, non_allowlisted_action.action_id).failure_reason,
		std::optional<std::string>("no_eligible_hunt_target"), "non-allowlisted NPC types must not be selected");
	fixture.RemoveMob(non_allowlisted);

	auto* claimed = fixture.AddHostileNPC({
		.name = "HarnessClaimedLargeRat", .position = glm::vec4(-2080.0f, 400.0f, -3.0f, 0.0f),
		.npc_type_id = 33005,
	});
	auto* contender = fixture.AddSyntheticPlayer("HarnessHuntContender", profile.owner_character_id.value() + 1000000,
		3, glm::vec4(-2082.0f, 400.0f, -3.0f, 0.0f));
	auto* contender_pet = fixture.AddHostileNPC({
		.name = "HarnessHuntContenderPet", .position = glm::vec4(-2083.0f, 400.0f, -3.0f, 0.0f),
	});
	Expect(claimed && contender && contender_pet, "player-contention hunt fixtures should materialize");
	Expect(contender->Connected() && contender->InZone(),
		"synthetic contention player must represent a connected client");
	contender->SetPet(contender_pet);
	claimed->AddToHateList(contender_pet, 100, 1, false);
	Expect(claimed->CheckAggro(contender_pet),
		"contention setup must establish ordinary player-owned pet hate before hunting");
	const auto contended = enqueue("contended", valid_body);
	executor.ProcessOne();
	ExpectEqual(ActorActionQueueRepository::FindOne(database, contended.action_id).failure_reason,
		std::optional<std::string>("target_claimed_by_player"), "a synthetic player's target must not be engaged");
	Expect(fixture.OwnedBot()->GetTarget() != claimed, "contention must not mutate the Actor leader's target");
	fixture.RemoveMob(claimed);
	contender->SetPet(nullptr);
	fixture.RemoveMob(contender_pet);
	fixture.RemoveMob(contender);

	auto* lost = fixture.AddHostileNPC({
		.name = "HarnessLostLargeRat", .position = glm::vec4(-2080.0f, 400.0f, -3.0f, 0.0f),
		.npc_type_id = 33005,
	});
	Expect(lost, "lost-target fixture should create an allowlisted NPC");
	const auto lost_action = enqueue("lost", valid_body);
	executor.ProcessOne();
	ExpectEqual(ActorActionQueueRepository::FindOne(database, lost_action.action_id).state, std::string("claimed"),
		"accepted hunts should remain claimed during Committed Engagement");
	fixture.RemoveMob(lost);
	executor.ProcessOne();
	ExpectEqual(ActorActionQueueRepository::FindOne(database, lost_action.action_id).failure_reason,
		std::optional<std::string>("hunt_target_lost"), "lost targets need a visible bounded outcome");
	clear_combat();

	auto* ended_target = fixture.AddHostileNPC({
		.name = "HarnessCombatEndedLargeRat", .position = glm::vec4(-2080.0f, 400.0f, -3.0f, 0.0f),
		.npc_type_id = 33005,
	});
	Expect(ended_target, "combat-ended fixture should create an allowlisted NPC");
	const auto combat_ended = enqueue("combat-ended", valid_body);
	executor.ProcessOne();
	clear_combat();
	executor.ProcessOne();
	ExpectEqual(ActorActionQueueRepository::FindOne(database, combat_ended.action_id).failure_reason,
		std::optional<std::string>("hunt_combat_ended"),
		"combat ending while the selected target lives must produce a visible failed outcome");
	fixture.RemoveMob(ended_target);

	auto* timeout_target = fixture.AddHostileNPC({
		.name = "HarnessTimeoutLargeRat", .position = glm::vec4(-2080.0f, 400.0f, -3.0f, 0.0f),
		.npc_type_id = 33005,
	});
	Expect(timeout_target, "timeout fixture should create an allowlisted NPC");
	time_t hunt_clock = now;
	const auto timed = enqueue("timeout", valid_body, hunt_clock + 1);
	ActorActionExecutor timeout_executor(database, zone->GetZoneID(), zone->GetInstanceID(), zone->GetZoneServerId(),
		[&]() { return hunt_clock; });
	timeout_executor.ProcessOne();
	hunt_clock += 2;
	timeout_executor.ProcessOne();
	ExpectEqual(ActorActionQueueRepository::FindOne(database, timed.action_id).state, std::string("expired"),
		"a hunt exceeding its deadline must expire without a forced result");
	Expect(fixture.OwnedBot()->GetTarget() != timeout_target && !fixture.OwnedBot()->GetAttackFlag() &&
		!fixture.OwnedBot()->CheckAggro(timeout_target),
		"expiry must cancel the Actor leader's selected-target combat intent");
	for (auto* follower : fixture.FollowerBots()) {
		Expect(follower->GetTarget() != timeout_target && !follower->GetAttackFlag() &&
			!follower->CheckAggro(timeout_target),
			"expiry must cancel each follower's selected-target combat intent");
	}
	const auto retained_attack_flags = timeout_target->GetBotAttackFlags();
	Expect(std::find(retained_attack_flags.begin(), retained_attack_flags.end(), *profile.owner_character_id) ==
		retained_attack_flags.end(), "expiry must remove the hunt's target authorization");
	fixture.RemoveMob(timeout_target);
	clear_combat();

	auto* kill_target = fixture.AddHostileNPC({
		.name = "HarnessAllowlistedLargeRat", .position = glm::vec4(-2080.0f, 400.0f, -3.0f, 0.0f),
		.npc_type_id = 33005,
	});
	Expect(kill_target, "success fixture should create an allowlisted NPC");
	// The shared fixture is a high-level caster configured for slow spells.
	// Enable its ordinary melee policy for this combat scenario, not forced damage.
	struct RestoreCombatSettings {
		Bot* actor;
		uint8_t stop_melee_level;
		double previous_frame_time;
		~RestoreCombatSettings() {
			actor->SetStopMeleeLevel(stop_melee_level);
			frame_time = previous_frame_time;
		}
	} restore_combat{fixture.OwnedBot(), fixture.OwnedBot()->GetStopMeleeLevel(), frame_time};
	fixture.OwnedBot()->SetStopMeleeLevel(255);
	const auto target_id = kill_target->GetID();
	const auto succeeded = enqueue("success", valid_body, std::time(nullptr) + 15);
	executor.ProcessOne();
	auto previous_tick = std::chrono::steady_clock::now();
	for (uint32_t tick = 0; tick < 300 &&
		ActorActionQueueRepository::FindOne(database, succeeded.action_id).state == "claimed"; ++tick) {
		const auto current_tick = std::chrono::steady_clock::now();
		frame_time = std::chrono::duration<double>(current_tick - previous_tick).count();
		previous_tick = current_tick;
		Timer::SetCurrentTime();
		entity_list.Process();
		entity_list.MobProcess();
		Expect(zone->Process(), "hunt combat tick must keep its zone alive");
		executor.ProcessOne();
		std::this_thread::sleep_for(std::chrono::milliseconds(20));
	}
	const auto terminal = ActorActionQueueRepository::FindOne(database, succeeded.action_id);
	ExpectEqual(terminal.state, std::string("completed"),
		"ordinary Bot combat should authoritatively complete the bounded hunt: state=" + terminal.state +
		", reason=" + terminal.failure_reason.value_or("none"));
	Expect(entity_list.GetNPCByID(target_id) == nullptr && entity_list.GetCorpseByID(target_id) != nullptr,
		"hunt completion must retain the selected identity after NPC-to-corpse transfer");
	const auto result = ParseJson(terminal.result_json.value_or("{}"));
	ExpectEqual(result["outcome"].asString(), std::string("succeeded"),
		"only selected-target death should satisfy the hunt");
	ExpectEqual(result["target_entity_id"].asUInt(), static_cast<unsigned>(target_id),
		"hunt success must correlate the selected target");
	const auto events = ActorEventsRepository::ReadCursor(database, profile.actor_id, 0, 1000);
	Expect(std::any_of(events.begin(), events.end(), [&](const auto& event) {
		if (event.event_type != "hunt_succeeded") return false;
		const auto payload = ParseJson(event.event_json);
		return payload["action_id"].asUInt64() == succeeded.action_id &&
			payload["target_entity_id"].asUInt() == target_id;
	}), "durable authoritative death evidence must correlate action and selected target");
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
		ExpectBoundedPersistenceOverloadAndRecovery();
		ExpectShutdownRetainsRecoverablePayloads();

		const auto run_nonce = BuildRunNonce();

		Zone::Bootup(ZoneID("misty"), 0, false);
		zone->StopShutdownTimer();
		entity_list.Process();
		entity_list.MobProcess();

		EQ::ZoneHarness::ActorEventRecorder recorder;
		EQ::ZoneHarness::ActorEventRepositoryPersistenceSink persistence_sink;
		recorder.SetPersistenceSink(&persistence_sink);
		EQ::ZoneHarness::ActorEventRecorder::RegisterActiveRecorder(&recorder);
		struct ClearRecorderOnExit {
			EQ::ZoneHarness::ActorEventRecorder& recorder;
			~ClearRecorderOnExit() {
				EQ::ZoneHarness::ActorEventRecorder::ClearActiveRecorder(&recorder);
				recorder.SetPersistenceSink(nullptr);
			}
		} clear_recorder{recorder};

		ActorEventPersistenceCleanup cleanup;
		const auto reserved_owner = EQ::Actor::ReservedOwners::Provision(database, "ActorownerRuntime" + std::to_string(run_nonce));
		Expect(reserved_owner.character_id > 0,
			   "reserved owner provisioning should succeed for runtime actor event persistence");
		cleanup.reserved_owner_character_id = reserved_owner.character_id;
		EQ::ZoneHarness::OwnedBotActorFixture fixture;
		Expect(fixture.SetUpOwnedBotParty({
				   .owner_name = reserved_owner.name,
				   .owner_character_id = reserved_owner.character_id,
				   .follower_count = 1,
			   }),
			   "owned bot Actor-led Party harness fixture should boot");
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
		ExpectSpeechDefersAndRetriesThroughProductionPath(fixture.OwnedBot(), recorder, &persistence_sink);
		ExpectFallbackRepliesRetryInOrder(fixture, recorder, &persistence_sink);

		ActorProfilesRepository::ActorProfileRecord profile{};
		profile.actor_type = "autonomous_actor";
		profile.actor_substrate = "bot";
		profile.bot_id = actor_bot_id;
		profile.owner_character_id = reserved_owner.character_id;
		profile.enabled = true;
		const auto inserted_profile = ActorProfilesRepository::UpsertBotBackedProfile(database, profile);
		Expect(inserted_profile.actor_id > 0, "actor profile insert should allocate an actor_id");
		cleanup.TrackActorId(inserted_profile.actor_id);
		ExpectBorrowedConnectionReconnects();
		ExpectPersistenceConnectionIsolated(inserted_profile);
		ExpectProfileChangesRetainAcceptedEvidence(inserted_profile);

		const auto speech_marker = fmt::format("runtime-actor-events-{}", run_nonce);
		fixture.OwnedBot()->Say("%s", speech_marker.c_str());
		Expect(persistence_sink.FlushFor(std::chrono::seconds(2)),
			   "runtime actor evidence should flush outside the speech callback");

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

#ifndef _WIN32
		// Launch the production helper executable beside the harness zone. Kill it
		// with its request pending, then restart it to prove durable idempotency and
		// correlated outcome observation through the real zone executor.
		ScopedTestDirectory helper_state(
			std::filesystem::temp_directory_path() / fmt::format("eqemu-actor-helper-{}", run_nonce));
		const auto helper_action_count = [&]() -> uint64_t {
			auto rows = database.QueryDatabase(fmt::format(
				"SELECT COUNT(*) FROM actor_action_queue WHERE actor_id = {} AND source = 'actor-helper'",
				inserted_profile.actor_id));
			Expect(rows.Success() && rows.RowCount() == 1 && rows.begin()[0],
				   "helper action count should be readable");
			return strtoull(rows.begin()[0], nullptr, 10);
		};
		const auto wait_for_helper_action = [&]() {
			const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
			while (std::chrono::steady_clock::now() < deadline) {
				if (helper_action_count() == 1) return true;
				std::this_thread::sleep_for(std::chrono::milliseconds(20));
			}
			return false;
		};
		const auto disabled_helper = SpawnActorHelper(inserted_profile.actor_id, helper_state.Path(), 1, 10, false);
		int disabled_status = 0;
		Expect(disabled_helper > 0 && WaitForChild(disabled_helper, std::chrono::seconds(10), &disabled_status) &&
			   WIFEXITED(disabled_status) && WEXITSTATUS(disabled_status) == 0,
			   "helper should remain disabled unless explicitly enabled");
		ExpectEqual(helper_action_count(), uint64_t(0), "disabled helper must not enqueue work");

		const auto first_helper = SpawnActorHelper(inserted_profile.actor_id, helper_state.Path());
		Expect(first_helper > 0, "real actor helper should launch");
		Expect(wait_for_helper_action(), "helper should discover the fresh actor and queue one bounded action");
		kill(first_helper, SIGTERM);
		int first_status = 0;
		Expect(WaitForChild(first_helper, std::chrono::seconds(2), &first_status),
			   "killed helper should terminate within the scenario deadline");
		ExpectEqual(helper_action_count(), uint64_t(1), "pending helper work should survive process restart");

		const auto restarted_helper = SpawnActorHelper(inserted_profile.actor_id, helper_state.Path());
		Expect(restarted_helper > 0, "actor helper should restart with work pending");
		ActorActionExecutor helper_executor(database, zone->GetZoneID(), zone->GetInstanceID(), zone->GetZoneServerId());
		helper_executor.ProcessOne();
		int restarted_status = 0;
		Expect(WaitForChild(restarted_helper, std::chrono::seconds(10), &restarted_status),
			   "restarted helper should observe the correlated zone outcome");
		Expect(WIFEXITED(restarted_status) && WEXITSTATUS(restarted_status) == 0,
			   "restarted helper should exit successfully after observing the outcome");
		ExpectEqual(helper_action_count(), uint64_t(1), "restart must not duplicate the queued action or its effect");
		auto helper_action = database.QueryDatabase(fmt::format(
			"SELECT action_id, state FROM actor_action_queue WHERE actor_id = {} AND source = 'actor-helper'",
			inserted_profile.actor_id));
		Expect(helper_action.Success() && helper_action.RowCount() == 1 && helper_action.begin()[0] &&
			   helper_action.begin()[1] && std::string(helper_action.begin()[1]) == "completed",
			   "zone executor should complete the helper's production action");
		Expect(ActorEventsRepository::HasActionOutcome(database, inserted_profile.actor_id,
			strtoull(helper_action.begin()[0], nullptr, 10)),
			"helper action should have one correlated zone outcome");

		const auto idle_started = std::chrono::steady_clock::now();
		const auto idle_helper = SpawnActorHelper(inserted_profile.actor_id, helper_state.Path(), 2, 50);
		int idle_status = 0;
		Expect(WaitForChild(idle_helper, std::chrono::seconds(10), &idle_status),
			   "no-work helper should stop at its configured cycle bound");
		Expect(std::chrono::steady_clock::now() - idle_started >= std::chrono::milliseconds(50),
			   "no-work helper cycles should back off instead of spinning");
		ExpectEqual(helper_action_count(), uint64_t(1), "no-work polling must not queue a duplicate action");

		// An expired request has not applied gameplay. Reopen that same durable row
		// instead of letting its idempotency key strand the triggering event.
		const auto expiry_trigger = ActorEventsRepository::AppendEvent(database, {
			.actor_id = inserted_profile.actor_id,
			.event_type = "test_helper_expiry_delta",
			.event_json = R"json({"sequence":1})json",
			.created_at = now,
		});
		Expect(expiry_trigger.event_id != 0, "helper expiry trigger should persist");
		ActorHelper expiry_helper(database, {.state_directory = helper_state.Path(),
			.actor_id = inserted_profile.actor_id, .zone_id = zone->GetZoneID(),
			.instance_id = zone->GetInstanceID()});
		ExpectEqual(expiry_helper.RunCycle(now).enqueued, size_t(1),
			"helper should enqueue the expiry regression request");
		auto expiry_action = database.QueryDatabase(fmt::format(
			"SELECT action_id FROM actor_action_queue WHERE actor_id = {} AND source = 'actor-helper' "
			"AND state = 'pending'", inserted_profile.actor_id));
		Expect(expiry_action.Success() && expiry_action.RowCount() == 1 && expiry_action.begin()[0],
			"expiry regression request should be pending");
		const auto expiry_action_id = strtoull(expiry_action.begin()[0], nullptr, 10);
		Expect(database.QueryDatabase(fmt::format(
			"UPDATE actor_action_queue SET state = 'expired', completed_at = FROM_UNIXTIME({}) "
			"WHERE action_id = {}", now + 15, expiry_action_id)).Success(),
			"expiry regression request should transition to expired");
		const auto expiry_retry = expiry_helper.RunCycle(now + 16);
		ExpectEqual(expiry_retry.enqueued, size_t(1), "expired helper work should be safely retried");
		ExpectEqual(ActorActionQueueRepository::FindOne(database, expiry_action_id).state, std::string("pending"),
			"expiry retry should reopen the same idempotent row");
		ExpectEqual(helper_action_count(), uint64_t(2), "expiry retry must not create a duplicate queue row");

		// A helper with a different cursor view derives a newer key, but admission
		// remains serialized per actor while the retry is pending.
		ScopedTestDirectory competing_helper_state(
			std::filesystem::temp_directory_path() / fmt::format("eqemu-actor-helper-competing-{}", run_nonce));
		Expect(ActorEventsRepository::AppendEvent(database, {
			.actor_id = inserted_profile.actor_id,
			.event_type = "test_helper_competing_delta",
			.event_json = R"json({"sequence":2})json",
			.created_at = now,
		}).event_id != 0, "competing helper trigger should persist");
		ActorHelper competing_helper(database, {.state_directory = competing_helper_state.Path(),
			.actor_id = inserted_profile.actor_id, .zone_id = zone->GetZoneID(),
			.instance_id = zone->GetInstanceID()});
		const auto competing = competing_helper.RunCycle(now + 16);
		ExpectEqual(competing.enqueued, size_t(0), "a second cursor view must not admit concurrent helper work");
		ExpectEqual(competing.busy, size_t(1), "competing helper admission should observe the durable request");
		ExpectEqual(helper_action_count(), uint64_t(2), "one-in-flight enforcement must not add a newer-key row");
		Expect(ActorActionQueueRepository::DeleteOne(database, expiry_action_id) == 1,
			"expiry regression request should clean up");
		Expect(database.QueryDatabase(fmt::format(
			"DELETE FROM actor_events WHERE actor_id = {} AND event_type IN "
			"('test_helper_expiry_delta','test_helper_competing_delta')", inserted_profile.actor_id)).Success(),
			"helper expiry and contention events should clean up");

		// Hold an uncommitted profile change after discovery can see the old row.
		// The helper must block at admission, then revalidate the now-disabled actor
		// rather than enqueueing from its stale discovery result.
		const auto admission_trigger = ActorEventsRepository::AppendEvent(database, {
			.actor_id = inserted_profile.actor_id, .event_type = "test_helper_admission_race",
			.event_json = R"json({"sequence":3})json", .created_at = now,
		});
		Expect(admission_trigger.event_id != 0, "admission-race trigger should persist");
		const auto* helper_config = EQEmuConfig::get();
		Expect(helper_config != nullptr, "admission-race helper needs database configuration");
		Database admission_database;
		Expect(admission_database.Connect(helper_config->DatabaseHost, helper_config->DatabaseUsername,
			helper_config->DatabasePassword, helper_config->DatabaseDB, helper_config->DatabasePort,
			"actor-helper-admission-race"), "admission-race helper should connect independently");
		auto admission_connection = admission_database.QueryDatabase("SELECT CONNECTION_ID()");
		Expect(admission_connection.Success() && admission_connection.RowCount() == 1 && admission_connection.begin()[0],
			"admission-race helper connection id should be readable");
		const auto admission_connection_id = strtoull(admission_connection.begin()[0], nullptr, 10);
		ScopedTestDirectory admission_state(
			std::filesystem::temp_directory_path() / fmt::format("eqemu-actor-helper-admission-{}", run_nonce));
		database.TransactionBegin();
		Expect(database.QueryDatabase(fmt::format(
			"UPDATE actor_profiles SET enabled = 0 WHERE actor_id = {}", inserted_profile.actor_id)).Success(),
			"admission-race profile change should hold the actor lock");
		auto admission_future = std::async(std::launch::async, [&]() {
			ActorHelper admission_helper(admission_database, {.state_directory = admission_state.Path(),
				.actor_id = inserted_profile.actor_id, .zone_id = zone->GetZoneID(),
				.instance_id = zone->GetInstanceID()});
			return admission_helper.RunCycle(now);
		});
		bool admission_waiting = false;
		bool admission_observation_ok = true;
		const auto admission_deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
		while (!admission_waiting && std::chrono::steady_clock::now() < admission_deadline) {
			auto process = database.QueryDatabase(fmt::format(
				"SELECT COUNT(*) FROM information_schema.PROCESSLIST WHERE ID = {} "
				"AND INFO LIKE '%FROM actor_profiles p%' AND INFO LIKE '%FOR UPDATE%'",
				admission_connection_id));
			if (!process.Success() || process.RowCount() != 1 || !process.begin()[0]) {
				admission_observation_ok = false;
				break;
			}
			admission_waiting = strtoull(process.begin()[0], nullptr, 10) == 1;
			if (!admission_waiting) std::this_thread::sleep_for(std::chrono::milliseconds(10));
		}
		const bool admission_committed = database.TransactionCommit().Success();
		const auto admission_result = admission_future.get();
		Expect(admission_committed, "admission-race profile change should commit");
		Expect(admission_observation_ok && admission_waiting,
			"helper should reach admission while the stale discovery row is locked");
		ExpectEqual(admission_result.discovered, size_t(1),
			"admission-race helper should have discovered the actor before disable committed");
		ExpectEqual(admission_result.enqueued, size_t(0),
			"admission must reject an actor disabled after discovery");
		Expect(database.QueryDatabase(fmt::format(
			"UPDATE actor_profiles SET enabled = 1 WHERE actor_id = {}", inserted_profile.actor_id)).Success(),
			"admission-race profile should be restored");
		Expect(database.QueryDatabase(fmt::format(
			"DELETE FROM actor_events WHERE event_id = {}", admission_trigger.event_id)).Success(),
			"admission-race trigger should clean up");

		// A decision admitted for this zone must not become executable merely because
		// the actor's current binding makes the destination zone eligible to claim it.
		const auto moved_after_admission_trigger = ActorEventsRepository::AppendEvent(database, {
			.actor_id = inserted_profile.actor_id, .event_type = "test_helper_move_after_admission",
			.event_json = R"json({"sequence":4})json", .created_at = now,
		});
		Expect(moved_after_admission_trigger.event_id != 0, "move-after-admission trigger should persist");
		ActorHelper moved_after_admission_helper(database, {.state_directory = helper_state.Path(),
			.actor_id = inserted_profile.actor_id, .zone_id = zone->GetZoneID(),
			.instance_id = zone->GetInstanceID()});
		ExpectEqual(moved_after_admission_helper.RunCycle(now).enqueued, size_t(1),
			"helper should admit work against the source-zone binding");
		fixture.OwnedBot()->Sit();
		Expect(database.QueryDatabase(fmt::format(
			"UPDATE actor_status SET zone_id = {} WHERE actor_id = {}", zone->GetZoneID() + 1,
			inserted_profile.actor_id)).Success(), "actor should move after helper admission");
		ActorActionExecutor moved_destination_executor(
			database, zone->GetZoneID() + 1, zone->GetInstanceID(), zone->GetZoneServerId());
		moved_destination_executor.ProcessOne();
		auto moved_after_admission_action = database.QueryDatabase(fmt::format(
			"SELECT action_id, state, failure_reason FROM actor_action_queue WHERE actor_id = {} "
			"AND source = 'actor-helper' AND state = 'failed'", inserted_profile.actor_id));
		Expect(moved_after_admission_action.Success() && moved_after_admission_action.RowCount() == 1 &&
			moved_after_admission_action.begin()[0] && moved_after_admission_action.begin()[1] &&
			moved_after_admission_action.begin()[2] &&
			std::string(moved_after_admission_action.begin()[2]) == "actor_binding_changed",
			"destination executor should reject the helper's source-zone binding");
		Expect(fixture.OwnedBot()->IsSitting(), "a moved helper decision must not apply in the destination zone");
		Expect(database.QueryDatabase(fmt::format(
			"UPDATE actor_status SET zone_id = {} WHERE actor_id = {}", zone->GetZoneID(),
			inserted_profile.actor_id)).Success(), "move-after-admission actor binding should be restored");
		Expect(database.QueryDatabase(fmt::format(
			"DELETE FROM actor_events WHERE actor_id = {} AND (event_id = {} OR "
			"(event_type = 'action_rejected' AND JSON_UNQUOTE(JSON_EXTRACT(event_json, '$.action_id')) = '{}'))",
			inserted_profile.actor_id, moved_after_admission_trigger.event_id,
			moved_after_admission_action.begin()[0])).Success(),
			"move-after-admission events should clean up");
		Expect(database.QueryDatabase(fmt::format(
			"DELETE FROM actor_action_queue WHERE action_id = {}", moved_after_admission_action.begin()[0])).Success(),
			"move-after-admission action should clean up");

		// Bounded discovery rotates rather than permanently selecting the lowest IDs.
		ScopedTestDirectory fair_helper_state(
			std::filesystem::temp_directory_path() / fmt::format("eqemu-actor-helper-fair-{}", run_nonce));
		uint32_t fair_target_actor_id = 0;
		for (uint32_t index = 0; index < 3; ++index) {
			ActorProfilesRepository::ActorProfileRecord fair_profile{};
			fair_profile.actor_type = "autonomous_actor";
			fair_profile.actor_substrate = "bot";
			fair_profile.bot_id = next_free_bot_id(100 + index);
			fair_profile.owner_character_id = reserved_owner.character_id;
			fair_profile.enabled = true;
			fair_profile = ActorProfilesRepository::UpsertBotBackedProfile(database, fair_profile);
			Expect(fair_profile.actor_id != 0, "fair-discovery actor should persist");
			cleanup.TrackActorId(fair_profile.actor_id);
			ActorStatusRepository::UpsertOne(database, {
				.actor_id = fair_profile.actor_id, .zone_id = zone->GetZoneID(),
				.instance_id = zone->GetInstanceID(), .entity_id = 60000 + index,
				.state = "active", .heartbeat_at = now,
			});
			if (index == 2) fair_target_actor_id = fair_profile.actor_id;
		}
		const auto fair_trigger = ActorEventsRepository::AppendEvent(database, {
			.actor_id = fair_target_actor_id, .event_type = "test_fair_discovery",
			.event_json = R"json({"sequence":1})json", .created_at = now,
		});
		Expect(fair_trigger.event_id != 0, "fair-discovery target event should persist");
		ActorHelper fair_helper(database, {.state_directory = fair_helper_state.Path(),
			.zone_id = zone->GetZoneID(), .instance_id = zone->GetInstanceID(), .discovery_limit = 2});
		ExpectEqual(fair_helper.RunCycle(now).enqueued, size_t(0),
			"first bounded discovery page should not reach the later actor");
		ActorHelper restarted_fair_helper(database, {.state_directory = fair_helper_state.Path(),
			.zone_id = zone->GetZoneID(), .instance_id = zone->GetInstanceID(), .discovery_limit = 2});
		ExpectEqual(restarted_fair_helper.RunCycle(now).enqueued, size_t(1),
			"persisted bounded discovery should reach later actors after restart");
		auto fair_action = database.QueryDatabase(fmt::format(
			"SELECT source_metadata_json FROM actor_action_queue WHERE actor_id = {} "
			"AND source = 'actor-helper'", fair_target_actor_id));
		Expect(fair_action.Success() && fair_action.RowCount() == 1 && fair_action.begin()[0],
			"fair-discovery helper action metadata should be readable");
		const auto fair_metadata = ParseJson(fair_action.begin()[0]);
		ExpectEqual(fair_metadata["expected_event_id"].asUInt64(), fair_trigger.event_id,
			"helper watermark should come from the gameplay trigger snapshot");
		ExpectEqual(fair_metadata["schema_version"].asUInt(), 1u,
			"helper decision metadata should identify its schema");
		ExpectEqual(fair_metadata["decision"]["policy"].asString(),
			std::string("stand_on_latest_gameplay_event"),
			"helper metadata should identify the deterministic decision policy");
		ExpectEqual(fair_metadata["decision"]["policy_version"].asUInt(), 1u,
			"helper metadata should identify the deterministic policy version");
		ExpectEqual(fair_metadata["decision"]["inputs"]["actor_id"].asUInt(), fair_target_actor_id,
			"helper metadata should retain the actor decision input");
		ExpectEqual(fair_metadata["decision"]["inputs"]["event_id"].asUInt64(), fair_trigger.event_id,
			"helper metadata should retain the selected event identity");
		ExpectEqual(fair_metadata["decision"]["inputs"]["event_type"].asString(),
			std::string("test_fair_discovery"),
			"helper metadata should retain the event type used by the policy");
		ExpectEqual(fair_metadata["expected_binding"]["actor_id"].asUInt(), fair_target_actor_id,
			"helper metadata should retain the admitted actor binding");
		ExpectEqual(fair_metadata["expected_binding"]["zone_id"].asUInt(), zone->GetZoneID(),
			"helper metadata should retain the admitted zone binding");
		ExpectEqual(fair_metadata["expected_binding"]["instance_id"].asUInt(), zone->GetInstanceID(),
			"helper metadata should retain the admitted instance binding");
		ExpectEqual(fair_metadata["expected_binding"]["entity_id"].asUInt(), 60002u,
			"helper metadata should retain the admitted entity binding");
		Expect(database.QueryDatabase(fmt::format(
			"DELETE FROM actor_action_queue WHERE actor_id = {} AND source = 'actor-helper'", fair_target_actor_id)).Success(),
			"fair-discovery request should clean up");
		Expect(database.QueryDatabase(fmt::format(
			"DELETE FROM actor_events WHERE actor_id = {} AND event_type = 'test_fair_discovery'", fair_target_actor_id)).Success(),
			"fair-discovery event should clean up");

		// Exercise bounded cursor catch-up and retained-event loss recovery without
		// manufacturing a gameplay success; the helper only submits through the queue.
		Expect(database.QueryDatabase(fmt::format(
			"DELETE FROM actor_events WHERE actor_id = {} AND event_type IN ('action_completed','action_rejected') "
			"AND JSON_UNQUOTE(JSON_EXTRACT(event_json, '$.action_id')) = '{}'",
			inserted_profile.actor_id, helper_action.begin()[0])).Success(),
			"helper outcome fixture should clean up before cursor assertions");
		std::filesystem::remove(helper_state.Path() /
			(std::to_string(inserted_profile.actor_id) + ".cursor"));
		ActorHelper retained_outcome_helper(database, {.state_directory = helper_state.Path(),
			.actor_id = inserted_profile.actor_id, .zone_id = zone->GetZoneID(),
			.instance_id = zone->GetInstanceID()});
		const auto retained_outcome_recovery = retained_outcome_helper.RunCycle(now);
		ExpectEqual(retained_outcome_recovery.retained_outcome_losses, size_t(1),
			"terminal queue state should recover progress after its correlated outcome is pruned");
		ExpectEqual(retained_outcome_recovery.enqueued, size_t(0),
			"retained-outcome recovery must not recreate an already completed effect");
		ExpectEqual(helper_action_count(), uint64_t(1),
			"retained-outcome recovery must preserve the single idempotent queue row");
		Expect(database.QueryDatabase(fmt::format(
			"DELETE FROM actor_action_queue WHERE actor_id = {} AND source = 'actor-helper'",
			inserted_profile.actor_id)).Success(), "helper queue fixture should clean up before cursor assertions");
		ActorHelper restarted_retained_outcome_helper(database, {.state_directory = helper_state.Path(),
			.actor_id = inserted_profile.actor_id, .zone_id = zone->GetZoneID(),
			.instance_id = zone->GetInstanceID()});
		ExpectEqual(restarted_retained_outcome_helper.RunCycle(now).enqueued, size_t(0),
			"retained-outcome recovery should durably suppress the completed effect after restart");
		for (int index = 0; index < 3; ++index) {
			Expect(ActorEventsRepository::AppendEvent(database, {
				.actor_id = inserted_profile.actor_id,
				.event_type = "test_gameplay_delta",
				.event_json = fmt::format("{{\"sequence\":{}}}", index),
				.created_at = now,
			}).event_id != 0, "cursor-gap fixture event should persist");
		}
		ActorHelper bounded_helper(database, {.state_directory = helper_state.Path(),
			.actor_id = inserted_profile.actor_id, .zone_id = zone->GetZoneID(),
			.instance_id = zone->GetInstanceID(), .event_limit = 2});
		const auto blocked_state = std::filesystem::temp_directory_path() /
			fmt::format("eqemu-actor-helper-blocked-state-{}", run_nonce);
		std::filesystem::remove_all(blocked_state);
		{ std::ofstream occupied(blocked_state); occupied << "not-a-directory"; }
		ActorHelper unwritable_helper(database, {.state_directory = blocked_state,
			.actor_id = inserted_profile.actor_id, .zone_id = zone->GetZoneID(),
			.instance_id = zone->GetInstanceID(), .event_limit = 2});
		const auto unwritable_gap = unwritable_helper.RunCycle(now);
		ExpectEqual(unwritable_gap.state_persistence_errors, size_t(1),
			"failed durable cursor writes should produce a fatal cycle result");
		ExpectEqual(unwritable_gap.cursor_gaps, size_t(0),
			"failed durable cursor writes must not report a cursor gap as recovered");
		std::filesystem::remove(blocked_state);

		const auto gap = bounded_helper.RunCycle(now);
		ExpectEqual(gap.cursor_gaps, size_t(1), "helper should report and boundedly catch up a cursor gap");
		ExpectEqual(gap.state_persistence_errors, size_t(0),
			"durable cursor catch-up should not report a persistence failure");
		ExpectEqual(gap.enqueued, size_t(0), "helper must not decide from a partial cursor page");
		auto cursor_input = std::ifstream(helper_state.Path() / (std::to_string(inserted_profile.actor_id) + ".cursor"));
		uint64_t gap_cursor = 0;
		cursor_input >> gap_cursor;
		Expect(gap_cursor != 0, "cursor-gap catch-up should durably advance its cursor");
		Expect(database.QueryDatabase(fmt::format(
			"DELETE FROM actor_events WHERE actor_id = {} AND event_id = {}", inserted_profile.actor_id, gap_cursor)).Success(),
			"retained-event-loss fixture should remove the saved cursor event");
		const auto retained_loss = bounded_helper.RunCycle(now);
		ExpectEqual(retained_loss.retained_event_losses, size_t(1),
			"helper should explicitly resynchronize when its retained cursor event is lost");
		Expect(database.QueryDatabase(fmt::format(
			"DELETE FROM actor_events WHERE actor_id = {} AND event_type = 'test_gameplay_delta'",
			inserted_profile.actor_id)).Success(), "cursor fixture events should clean up before queue assertions");
#endif

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
		Json::StreamWriterBuilder stale_writer;
		stale_writer["indentation"] = "";
		Json::Value stale_metadata;
		stale_metadata["expected_event_id"] = Json::UInt64(0);
		const auto enqueue_ineligible = [&](Bot* materialized_bot, bool enabled, std::optional<uint32_t> status_zone_id,
											const std::string& suffix, time_t heartbeat_at) {
			ActorProfilesRepository::ActorProfileRecord ineligible_profile{};
			ineligible_profile.actor_type = "autonomous_actor";
			ineligible_profile.actor_substrate = "bot";
			ineligible_profile.bot_id =
				next_free_bot_id(static_cast<uint32_t>(cleanup.reserved_owner_character_id + suffix.size()));
			if (materialized_bot) {
				fixture.AssignBotID(materialized_bot, *ineligible_profile.bot_id);
			}
			ineligible_profile.owner_character_id = reserved_owner.character_id;
			ineligible_profile.enabled = enabled;
			ineligible_profile = ActorProfilesRepository::UpsertBotBackedProfile(database, ineligible_profile);
			cleanup.TrackActorId(ineligible_profile.actor_id);
			if (status_zone_id.has_value()) {
				Expect(materialized_bot != nullptr,
					   "disabled, stale, and moved actor fixtures should have a matching materialized bot");
				Expect(entity_list.GetBotByBotID(*ineligible_profile.bot_id) == materialized_bot,
					   "disabled, stale, and moved actor profiles should identify their materialized bot");
				ActorStatusRepository::UpsertOne(database, {
															   .actor_id = ineligible_profile.actor_id,
															   .zone_id = *status_zone_id,
															   .instance_id = zone->GetInstanceID(),
															   .entity_id = materialized_bot->GetID(),
															   .state = "active",
															   .heartbeat_at = heartbeat_at,
														   });
			}
			return ActorActionQueueRepository::Enqueue(
				database, {
							  .actor_id = ineligible_profile.actor_id,
							  .source = "actor-events-test",
							  .source_metadata_json = Json::writeString(stale_writer, stale_metadata),
							  .action_type = "stand",
							  .action_json = Json::writeString(stale_writer, stand_body),
							  .idempotency_key = fmt::format("{}-{}", suffix, run_nonce),
							  .created_at = now,
						  });
		};
		EQ::ZoneHarness::OwnedBotActorFixture disabled_fixture;
		EQ::ZoneHarness::OwnedBotActorFixture stale_fixture;
		EQ::ZoneHarness::OwnedBotActorFixture moved_fixture;
		const auto set_up_ineligible_fixture = [&](EQ::ZoneHarness::OwnedBotActorFixture& ineligible_fixture,
												   const std::string& suffix) {
			return ineligible_fixture.SetUpOwnedBotSolo({
				.owner_name = fmt::format("{}{}", reserved_owner.name, suffix),
				.owner_character_id = reserved_owner.character_id,
				.bot_name = fmt::format("{}{}", suffix, run_nonce),
			});
		};
		Expect(set_up_ineligible_fixture(disabled_fixture, "Disabled") &&
				   set_up_ineligible_fixture(stale_fixture, "Stale") &&
				   set_up_ineligible_fixture(moved_fixture, "Moved"),
			   "ineligible actor fixtures should create materialized bots");
		auto* disabled_bot = disabled_fixture.OwnedBot();
		auto* stale_bot = stale_fixture.OwnedBot();
		auto* moved_bot = moved_fixture.OwnedBot();
		const auto missing_actor = enqueue_ineligible(nullptr, true, std::nullopt, "missing-live-actor", now);
		const auto disabled_actor = enqueue_ineligible(disabled_bot, false, zone->GetZoneID(), "disabled-actor", now);
		const auto stale_actor = enqueue_ineligible(stale_bot, true, zone->GetZoneID(), "stale-actor", now - 31);
		const auto moved_actor = enqueue_ineligible(moved_bot, true, zone->GetZoneID() + 1, "moved-actor", now);
		ScopedTestDirectory ineligible_helper_state(
			std::filesystem::temp_directory_path() / fmt::format("eqemu-actor-helper-ineligible-{}", run_nonce));
		for (const auto actor_id : {disabled_actor.actor_id, stale_actor.actor_id, moved_actor.actor_id}) {
			ActorHelper ineligible_helper(database, {
				.state_directory = ineligible_helper_state.Path(), .actor_id = actor_id,
				.zone_id = zone->GetZoneID(), .instance_id = zone->GetInstanceID()});
			const auto ineligible = ineligible_helper.RunCycle(now);
			ExpectEqual(ineligible.discovered, size_t(0),
				"helper discovery must skip disabled, stale, and moved actors");
			ExpectEqual(ineligible.enqueued, size_t(0),
				"ineligible actor discovery must not submit helper work");
		}
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
		for (const auto action_id :
			 {missing_actor.action_id, disabled_actor.action_id, stale_actor.action_id, moved_actor.action_id}) {
			ExpectEqual(ActorActionQueueRepository::FindOne(database, action_id).state, std::string("pending"),
						"missing, disabled, stale, and moved actors must remain unclaimed");
		}
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

		Json::Value zero_watermark_metadata;
		zero_watermark_metadata["expected_event_id"] = Json::UInt64(0);
		Json::StreamWriterBuilder zero_watermark_writer;
		zero_watermark_writer["indentation"] = "";
		const auto watermark_read_failure = ActorActionQueueRepository::Enqueue(
			database, {
						  .actor_id = inserted_profile.actor_id,
						  .source = "actor-events-test",
						  .source_metadata_json = Json::writeString(zero_watermark_writer, zero_watermark_metadata),
						  .action_type = "stand",
						  .action_json = Json::writeString(zero_watermark_writer, stand_body),
						  .idempotency_key = fmt::format("watermark-read-failure-{}", run_nonce),
						  .created_at = now,
					  });
		ActorActionExecutor watermark_failure_executor(
			database, zone->GetZoneID(), zone->GetInstanceID(), zone->GetZoneServerId(), [&]() { return now; },
			[](uint32_t) -> std::optional<uint64_t> { return std::nullopt; });
		watermark_failure_executor.ProcessOne();
		ExpectEqual(ActorActionQueueRepository::FindOne(database, watermark_read_failure.action_id).state,
					std::string("pending"), "watermark read failure should release the claim for retry");
		Expect(fixture.OwnedBot()->IsSitting(), "watermark read failure must not mutate actor gameplay state");
		ExpectEqual(
			ActorEventsRepository::ReadCursor(database, inserted_profile.actor_id, rejected_events[0].event_id, 8)
				.size(),
			static_cast<size_t>(0), "watermark read failure should not emit a terminal outcome");
		ActorActionQueueRepository::DeleteOne(database, watermark_read_failure.action_id);

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
		const auto malformed_watermark =
			enqueue_raw(R"json({"expected_event_id":"not-a-number"})json", R"json({"entity_id":1})json",
						fmt::format("bad-watermark-{}", run_nonce));
		executor.ProcessOne();
		ExpectEqual(ActorActionQueueRepository::FindOne(database, malformed_watermark.action_id).failure_reason,
					std::optional<std::string>("stale_event_watermark"),
					"non-numeric event watermarks should reject without coercion or exceptions");

		Json::Value valid_metadata;
		valid_metadata["expected_event_id"] = Json::UInt64(persisted_events[0].event_id);
		const auto metadata_json = Json::writeString(stale_writer, valid_metadata);
		for (const auto& [body_json, suffix] :
			 std::vector<std::pair<std::string, std::string>>{{R"json({"entity_id":"1"})json", "string"},
															  {R"json({"entity_id":-1})json", "negative"},
															  {R"json({"entity_id":65536})json", "oversized"}}) {
			const auto malformed_target =
				enqueue_raw(metadata_json, body_json, fmt::format("bad-target-{}-{}", suffix, run_nonce));
			executor.ProcessOne();
			ExpectEqual(ActorActionQueueRepository::FindOne(database, malformed_target.action_id).failure_reason,
						std::optional<std::string>("invalid_action_json"),
						"malformed target entity ids should reject without coercion or exceptions");
		}

		auto* ineligible_target = fixture.AddHostileNPC({
			.name = fmt::format("IneligibleTarget{}", run_nonce),
			.position = glm::vec4(12.0f, 0.0f, 0.0f, 0.0f),
		});
		Expect(ineligible_target != nullptr, "target eligibility fixture should create an NPC");
		Json::Value ineligible_target_body;
		ineligible_target_body["entity_id"] = ineligible_target->GetID();
		const auto expect_illegal_target_rejection = [&](const std::string& suffix) {
			const auto prior_events = ActorEventsRepository::ReadCursor(database, inserted_profile.actor_id, 0, 1000);
			const auto prior_event_id = prior_events.back().event_id;
			fixture.OwnedBot()->SetTarget(nullptr);
			const auto action =
				enqueue("target", ineligible_target_body, fmt::format("ineligible-target-{}-{}", suffix, run_nonce));
			executor.ProcessOne();
			const auto terminal = ActorActionQueueRepository::FindOne(database, action.action_id);
			ExpectEqual(terminal.state, std::string("failed"), "ineligible queued target should be rejected");
			ExpectEqual(terminal.failure_reason, std::optional<std::string>("illegal_target"),
						"ineligible queued target should record the rejection reason");
			Expect(fixture.OwnedBot()->GetTarget() == nullptr,
				   "ineligible queued target must not mutate actor target state");
			const auto outcomes =
				ActorEventsRepository::ReadCursor(database, inserted_profile.actor_id, prior_event_id, 8);
			ExpectEqual(outcomes.size(), static_cast<size_t>(1), "ineligible target should emit one rejection event");
			ExpectEqual(outcomes[0].event_type, std::string("action_rejected"),
						"ineligible target outcome should be a rejection");
			ExpectEqual(ParseJson(outcomes[0].event_json)["action_id"].asUInt64(), action.action_id,
						"ineligible target rejection should correlate action_id");
		};

		ineligible_target->SetTargetable(false);
		expect_illegal_target_rejection("untargetable");
		ineligible_target->SetTargetable(true);
		ineligible_target->SetInvisible(255);
		Expect(ineligible_target->IsInvisible(fixture.OwnedBot()),
			   "invisible target fixture should be outside actor perception");
		expect_illegal_target_rejection("invisible");
		ineligible_target->SetInvisible(0);

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
		ActorActionQueueRepository::DeleteOne(database, rollback_action.action_id);

		auto* duplicate_probe_target = fixture.AddHostileNPC({
			.name = fmt::format("DuplicateProbeTarget{}", run_nonce),
			.position = glm::vec4(10.0f, 0.0f, 0.0f, 0.0f),
		});
		Expect(duplicate_probe_target != nullptr, "duplicate application probe should create a target");
		Json::Value duplicate_probe_body;
		duplicate_probe_body["entity_id"] = duplicate_probe_target->GetID();
		const auto duplicate_probe =
			enqueue("target", duplicate_probe_body, fmt::format("duplicate-probe-{}", run_nonce));
		executor.ProcessOne();
		executor.ProcessOne();
		const auto duplicate_probe_events = recorder.Since(0, 32);
		const auto duplicate_applications = std::count_if(
			duplicate_probe_events.begin(), duplicate_probe_events.end(),
			[actor_id = fixture.OwnedBot()->GetID(), target_id = duplicate_probe_target->GetID()](const auto& event) {
				return event.type == "target_changed" && event.caster.entity_id == actor_id &&
					   event.target.has_value() && event.target->entity_id == target_id;
			});
		ExpectEqual(ActorActionQueueRepository::FindOne(database, duplicate_probe.action_id).state,
					std::string("completed"), "duplicate application probe should complete");
		ExpectEqual(duplicate_applications, static_cast<decltype(duplicate_applications)>(1),
					"retrying a completed action must not apply its gameplay effect twice");

		// Exercise saturation through the production Mob::Say API, not just the
		// queue primitive. The false result is what keeps an autonomous action
		// pending until evidence capacity recovers.
		std::mutex blocked_mutex;
		std::condition_variable blocked_started_cv;
		std::condition_variable blocked_release_cv;
		bool blocked_started = false;
		bool blocked_released = false;
		EQ::ZoneHarness::ActorEventRepositoryPersistenceSink blocked_sink(
			1, 512, [&](const auto&) {
				std::unique_lock lock(blocked_mutex);
				if (!blocked_started) {
					blocked_started = true;
					blocked_started_cv.notify_all();
					if (!blocked_release_cv.wait_for(
							lock, std::chrono::seconds(1), [&]() { return blocked_released; })) {
						return false;
					}
				}
				return true;
			});
		recorder.SetPersistenceSink(&blocked_sink);
		const auto overload_cursor = recorder.MaxEventID();
		const auto retained_marker = fmt::format("retained-speech-{}", run_nonce);
		const auto deferred_marker = fmt::format("deferred-speech-{}", run_nonce);
		Expect(fixture.OwnedBot()->Say("%s", retained_marker.c_str()),
			   "first production speech should reserve the bounded evidence slot");
		{
			std::unique_lock lock(blocked_mutex);
			Expect(blocked_started_cv.wait_for(lock, std::chrono::seconds(1), [&]() { return blocked_started; }),
				   "production speech evidence should begin the blocked flush");
		}
		Expect(!fixture.OwnedBot()->Say("%s", deferred_marker.c_str()),
			   "production speech should visibly defer when required evidence is saturated");
		ExpectEqual(recorder.Since(overload_cursor, 8).size(), static_cast<size_t>(1),
					"deferred speech must not be reported as emitted");
		{
			std::lock_guard lock(blocked_mutex);
			blocked_released = true;
			blocked_release_cv.notify_all();
		}
		Expect(blocked_sink.FlushFor(std::chrono::seconds(1)),
			   "retained production speech evidence should flush after recovery");
		Expect(fixture.OwnedBot()->Say("%s", deferred_marker.c_str()),
			   "the deferred production speech should be accepted on retry");
		Expect(blocked_sink.FlushFor(std::chrono::seconds(1)),
			   "retried production speech evidence should flush");
		ExpectEqual(recorder.Since(overload_cursor, 8).size(), static_cast<size_t>(2),
					"recovery should record the deferred speech exactly once");
		recorder.SetPersistenceSink(&persistence_sink);

		// The dialogue-window rendering branch must pass through the same
		// evidence gate and persist the same speech payload.
		const auto dialogue_marker = fmt::format("dialogue-speech-{}", run_nonce);
		{
			ScopedRuleOverride dialogue_rule("Chat:QuestDialogueUsesDialogueWindow", "true");
			Expect(dialogue_rule.Changed(), "dialogue-window rule override should apply");
			Expect(fixture.OwnedBot()->Say("%s", dialogue_marker.c_str()),
				   "dialogue-window speech should reserve required evidence before rendering");
		}
		Expect(persistence_sink.FlushFor(std::chrono::seconds(2)),
			   "dialogue-window speech evidence should flush");
		const auto dialogue_events = ActorEventsRepository::ReadCursor(
			database, inserted_profile.actor_id, persisted_events[0].event_id, 32);
		Expect(std::any_of(dialogue_events.begin(), dialogue_events.end(), [&](const auto& event) {
			return ParseJson(event.event_json)["text"].asString() == dialogue_marker;
		}), "dialogue-window speech should persist through the production evidence path");

		ExpectAllowlistedMistyHunt(fixture, inserted_profile, run_nonce);

		ExpectEqual(CountPlayerEventLogRowsWithMarker(speech_marker), int64_t(0),
					"runtime actor event persistence should not write marker rows to player_event_logs");

		EQ::ZoneHarness::ActorEventRecorder::ClearActiveRecorder(&recorder);
		Expect(persistence_sink.FlushFor(std::chrono::seconds(2)),
			   "runtime actor evidence queue should be empty before fixture cleanup");
		recorder.SetPersistenceSink(nullptr);
		fixture.Cleanup();
		std::string cleanup_failure;
		const bool cleanup_succeeded = cleanup.Cleanup(&cleanup_failure);
		Expect(cleanup_succeeded, "actor event persistence cleanup should succeed: " + cleanup_failure);
		std::cout << "[PASS] actor-events-runtime\n";
	} catch (const TestFailure& e) {
		std::cerr << "[FAIL] " << e.what() << "\n";
		std::exit(1);
	}
}

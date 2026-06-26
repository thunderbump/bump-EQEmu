/*	EQEmu: EQEmulator

	Copyright (C) 2001-2026 EQEmu Development Team

	This program is free software; you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation; either version 3 of the License, or
	(at your option) any later version.
*/

#include "zone_harness_http.h"

#include "common/eqemu_logsys.h"
#include "common/http/httplib.h"
#include "common/json/json.hpp"
#include "common/strings.h"
#include "zone/harness/zone_harness_runtime.h"

#include <condition_variable>
#include <mutex>
#include <thread>

namespace EQ::ZoneHarness {

namespace {

constexpr static int HTTP_RESPONSE_UNAUTHORIZED = 401;

nlohmann::json ToJson(const ZoneIdentitySnapshot &snapshot)
{
	return {
		{"loaded", snapshot.loaded},
		{"zone_id", snapshot.zone_id},
		{"short_name", snapshot.short_name},
		{"long_name", snapshot.long_name},
		{"instance_id", snapshot.instance_id},
		{"instance_version", snapshot.instance_version},
	};
}

nlohmann::json ToJson(const EntityCountsSnapshot &snapshot)
{
	return {
		{"mobs", snapshot.mobs},
		{"npcs", snapshot.npcs},
		{"clients", snapshot.clients},
		{"bots", snapshot.bots},
		{"corpses", snapshot.corpses},
		{"doors", snapshot.doors},
		{"objects", snapshot.objects},
	};
}

nlohmann::json ToJson(const EntitySummary &entity)
{
	return {
		{"entity_id", entity.entity_id},
		{"npc_type_id", entity.npc_type_id},
		{"type", entity.type},
		{"name", entity.name},
		{"level", entity.level},
		{"class_id", entity.class_id},
		{"race_id", entity.race_id},
		{"position", {
			{"x", entity.x},
			{"y", entity.y},
			{"z", entity.z},
		}},
	};
}

nlohmann::json ToJson(const EntitySnapshot &snapshot)
{
	nlohmann::json sample = nlohmann::json::array();
	for (const auto &entity: snapshot.sample) {
		sample.push_back(ToJson(entity));
	}

	return {
		{"counts", ToJson(snapshot.counts)},
		{"sample", sample},
	};
}

nlohmann::json ToJson(const RuntimeSnapshot &snapshot)
{
	return {
		{"booted", snapshot.booted},
		{"shutdown_requested", snapshot.shutdown_requested},
		{"uptime_ms", snapshot.uptime_ms},
		{"process_ticks", snapshot.process_ticks},
		{"pending_events", snapshot.pending_events},
		{"max_event_id", snapshot.max_event_id},
		{"zone", ToJson(snapshot.zone)},
	};
}

nlohmann::json ToJson(const ActorEventEntity &entity)
{
	return {
		{"entity_id", entity.entity_id},
		{"entity_ref", entity.entity_ref},
		{"name", entity.name},
		{"kind", entity.kind},
	};
}

nlohmann::json ToJson(const ActorEventSpell &spell)
{
	return {
		{"id", spell.id},
		{"name", spell.name},
		{"category", spell.category},
		{"targeting", spell.targeting},
		{"target_type", spell.target_type},
	};
}

nlohmann::json ToJson(const ActorEventCast &cast)
{
	return {
		{"slot", cast.slot},
		{"cast_time_ms", cast.cast_time_ms},
		{"original_cast_time_ms", cast.original_cast_time_ms},
	};
}

nlohmann::json ToJson(const ActorEventSpeech &speech)
{
	return {
		{"channel", speech.channel},
		{"text", speech.text},
		{"audible_radius", speech.audible_radius},
	};
}

nlohmann::json ToJson(const HealthSnapshot &snapshot)
{
	return {
		{"healthy", snapshot.healthy},
		{"status", snapshot.status},
		{"runtime", ToJson(snapshot.runtime)},
	};
}

nlohmann::json ToJson(const ProcessResult &result)
{
	return {
		{"ticks_requested", result.ticks_requested},
		{"ticks_processed", result.ticks_processed},
		{"runtime", ToJson(result.runtime)},
	};
}

nlohmann::json ToJson(const ActorEvent &event)
{
	auto payload = nlohmann::json{
		{"id", event.id},
		{"time_ms", event.time_ms},
		{"type", event.type},
		{"message", event.message},
		{"actor", ToJson(event.caster)},
		{"caster", ToJson(event.caster)},
		{"spell", ToJson(event.spell)},
		{"cast", ToJson(event.cast)},
		{"speech", ToJson(event.speech)},
	};

	if (event.previous_target.has_value()) {
		payload["previous_target"] = ToJson(*event.previous_target);
	}
	else {
		payload["previous_target"] = nullptr;
	}

	if (event.target.has_value()) {
		payload["target"] = ToJson(*event.target);
	}
	else {
		payload["target"] = nullptr;
	}

	return payload;
}

nlohmann::json ToJson(const SpellCastStartScenarioResult &result)
{
	return {
		{"started", result.started},
		{"reason", result.reason},
		{"caster_id", result.caster_id},
		{"target_id", result.target_id},
		{"spell_id", result.spell_id},
		{"runtime", ToJson(result.runtime)},
	};
}

nlohmann::json ToJson(const HeadlessClientTargetScenarioResult &result)
{
	nlohmann::json events = nlohmann::json::array();
	for (const auto &event: result.events) {
		events.push_back(ToJson(event));
	}

	return {
		{"completed", result.completed},
		{"observed", result.observed},
		{"reason", result.reason},
		{"action", result.action},
		{"database_mutation", result.database_mutation},
		{"eqstream_backed", result.eqstream_backed},
		{"completed_connect", result.completed_connect},
		{"event_cursor_start", result.event_cursor_start},
		{"event_cursor_end", result.event_cursor_end},
		{"actor", ToJson(result.actor)},
		{"target", ToJson(result.target)},
		{"events", events},
		{"runtime", ToJson(result.runtime)},
	};
}

nlohmann::json ToJson(const BotSlowMaintenanceScenarioResult &result)
{
	nlohmann::json events = nlohmann::json::array();
	for (const auto &event: result.events) {
		events.push_back(ToJson(event));
	}

	return {
		{"observed", result.observed},
		{"reason", result.reason},
		{"scenario", result.scenario},
		{"ticks_processed", result.ticks_processed},
		{"elapsed_ms", result.elapsed_ms},
		{"database_mutation", result.database_mutation},
		{"owner", ToJson(result.owner)},
		{"bot", ToJson(result.bot)},
		{"current_target", ToJson(result.current_target)},
		{"expected_target", ToJson(result.expected_target)},
		{"secondary_hostile", ToJson(result.secondary_hostile)},
		{"mezzed_hostile", ToJson(result.mezzed_hostile)},
		{"slow_spell_id", result.slow_spell_id},
		{"current_target_slowed", result.current_target_slowed},
		{"mezzed_hostile_mezzed", result.mezzed_hostile_mezzed},
		{"events", events},
		{"runtime", ToJson(result.runtime)},
	};
}

nlohmann::json ToJson(const PerceivedEntitySnapshot &snapshot)
{
	return {
		{"entity", ToJson(snapshot.entity)},
		{"relation_tags", snapshot.relation_tags},
		{"distance_bucket", snapshot.distance_bucket},
		{"alive", snapshot.alive},
		{"hp_percent", snapshot.hp_percent},
	};
}

nlohmann::json ToJson(const ActorPerceptionSnapshot &snapshot)
{
	nlohmann::json nearby_entities = nlohmann::json::array();
	for (const auto &entity: snapshot.nearby_entities) {
		nearby_entities.push_back(ToJson(entity));
	}

	auto payload = nlohmann::json{
		{"available", snapshot.available},
		{"reason", snapshot.reason},
		{"self", ToJson(snapshot.self)},
		{"nearby_entities", nearby_entities},
	};

	if (snapshot.current_target.has_value()) {
		payload["current_target"] = ToJson(*snapshot.current_target);
	}
	else {
		payload["current_target"] = nullptr;
	}

	return payload;
}

nlohmann::json ToJson(const AutonomousActorStatusSnapshot &snapshot)
{
	return {
		{"actor", ToJson(snapshot.actor)},
		{"owner", ToJson(snapshot.owner)},
		{"alive", snapshot.alive},
		{"hp_percent", snapshot.hp_percent},
		{"mana_percent", snapshot.mana_percent},
		{"has_target", snapshot.has_target},
		{"current_target_id", snapshot.current_target_id},
	};
}

nlohmann::json ToJson(const AutonomousActorActionResult &result)
{
	return {
		{"kind", result.kind},
		{"detail", result.detail},
		{"accepted", result.accepted},
		{"observed", result.observed},
		{"reason", result.reason},
	};
}

nlohmann::json ToJson(const AutonomousActorLoopScenarioResult &result)
{
	nlohmann::json actions = nlohmann::json::array();
	for (const auto &action: result.actions) {
		actions.push_back(ToJson(action));
	}

	nlohmann::json events = nlohmann::json::array();
	for (const auto &event: result.events) {
		events.push_back(ToJson(event));
	}

	return {
		{"completed", result.completed},
		{"reason", result.reason},
		{"failure_output", result.failure_output},
		{"database_mutation", result.database_mutation},
		{"persistent_actor", result.persistent_actor},
		{"tick_budget", result.tick_budget},
		{"ticks_processed", result.ticks_processed},
		{"event_cursor_start", result.event_cursor_start},
		{"event_cursor_end", result.event_cursor_end},
		{"owner", ToJson(result.owner)},
		{"actor", ToJson(result.actor)},
		{"target", ToJson(result.target)},
		{"status", ToJson(result.status)},
		{"perception", ToJson(result.perception)},
		{"actions", actions},
		{"events", events},
		{"runtime", ToJson(result.runtime)},
	};
}

nlohmann::json ToJson(const AutonomousActorPrototypeSessionSnapshot &snapshot)
{
	return {
		{"enabled", snapshot.enabled},
		{"active", snapshot.active},
		{"reason", snapshot.reason},
		{"session_id", snapshot.session_id},
		{"database_mutation", snapshot.database_mutation},
		{"queue_depth", snapshot.queue_depth},
		{"max_queue_depth", snapshot.max_queue_depth},
		{"last_event_cursor", snapshot.last_event_cursor},
		{"owner", ToJson(snapshot.owner)},
		{"actor", ToJson(snapshot.actor)},
		{"target", ToJson(snapshot.target)},
		{"status", ToJson(snapshot.status)},
		{"perception", ToJson(snapshot.perception)},
		{"runtime", ToJson(snapshot.runtime)},
	};
}

nlohmann::json ToJson(const AutonomousActorPrototypeActionAck &ack)
{
	return {
		{"session_id", ack.session_id},
		{"request_id", ack.request_id},
		{"kind", ack.kind},
		{"detail", ack.detail},
		{"accepted", ack.accepted},
		{"reason", ack.reason},
		{"queue_depth", ack.queue_depth},
		{"max_queue_depth", ack.max_queue_depth},
		{"event_cursor_start", ack.event_cursor_start},
		{"process_ticks_hint", ack.process_ticks_hint},
		{"poll_after_ms", ack.poll_after_ms},
		{"event_limit_hint", ack.event_limit_hint},
	};
}

void SetJson(httplib::Response &res, const nlohmann::json &payload)
{
	res.set_content(payload.dump(), "application/json");
}

uint32_t ParseTicks(const httplib::Request &req)
{
	if (req.has_param("ticks")) {
		return Strings::ToUnsignedInt(req.get_param_value("ticks"));
	}

	if (req.body.empty()) {
		return 1;
	}

	try {
		const auto payload = nlohmann::json::parse(req.body);
		if (payload.contains("ticks") && payload["ticks"].is_number()) {
			return payload["ticks"].get<uint32_t>();
		}
	}
	catch (const std::exception &) {
		return 1;
	}

	return 1;
}

uint16_t ParseSpellID(const httplib::Request &req)
{
	if (req.has_param("spell_id")) {
		return static_cast<uint16_t>(Strings::ToUnsignedInt(req.get_param_value("spell_id")));
	}

	if (req.body.empty()) {
		return 200;
	}

	try {
		const auto payload = nlohmann::json::parse(req.body);
		if (payload.contains("spell_id") && payload["spell_id"].is_number()) {
			return payload["spell_id"].get<uint16_t>();
		}
	}
	catch (const std::exception &) {
		return 200;
	}

	return 200;
}

std::string ParseActionStringField(const httplib::Request &req, const std::string &field_name)
{
	if (req.has_param(field_name)) {
		return req.get_param_value(field_name);
	}

	if (req.body.empty()) {
		return {};
	}

	try {
		const auto payload = nlohmann::json::parse(req.body);
		if (payload.contains(field_name) && payload[field_name].is_string()) {
			return payload[field_name].get<std::string>();
		}
	}
	catch (const std::exception &) {
		return {};
	}

	return {};
}

bool IsAuthorized(const httplib::Request &req, const std::string &bearer_token)
{
	if (bearer_token.empty()) {
		return true;
	}

	auto authorization = req.get_header_value("Authorization");
	if (authorization.empty()) {
		return false;
	}

	Strings::FindReplace(authorization, "Bearer", "");
	Strings::Trim(authorization);
	return authorization == bearer_token;
}

}

bool ServeHttp(const HttpServerOptions &options)
{
	ZoneHarnessRuntime runtime;
	if (!runtime.Boot(options.zone_short_name, options.instance_id)) {
		LogError("Zone Harness failed to boot zone [{}]", options.zone_short_name);
		return false;
	}
	runtime.EnableAutonomousActorPrototype(options.enable_autonomous_actor_prototype);

	httplib::Server api;
	std::mutex finished_mutex;
	std::condition_variable finished_cv;
	bool finished = false;

	api.set_pre_routing_handler(
		[&options](const auto &req, auto &res) {
			if (IsAuthorized(req, options.bearer_token)) {
				return httplib::Server::HandlerResponse::Unhandled;
			}

			res.status = HTTP_RESPONSE_UNAUTHORIZED;
			SetJson(res, {{"error", "Authorization key not valid"}});
			return httplib::Server::HandlerResponse::Handled;
		}
	);

	api.Get("/api/v1/harness/health", [&runtime](const auto &, auto &res) {
		SetJson(res, ToJson(runtime.Health()));
	});

	api.Get("/api/v1/harness/runtime", [&runtime](const auto &, auto &res) {
		SetJson(res, ToJson(runtime.Runtime()));
	});

	api.Get("/api/v1/harness/zone", [&runtime](const auto &, auto &res) {
		SetJson(res, ToJson(runtime.ZoneIdentity()));
	});

	api.Get("/api/v1/harness/entities", [&runtime](const auto &req, auto &res) {
		uint32_t sample_limit = 25;
		if (req.has_param("sample_limit")) {
			sample_limit = Strings::ToUnsignedInt(req.get_param_value("sample_limit"));
		}

		SetJson(res, ToJson(runtime.Entities(sample_limit)));
	});

	api.Post("/api/v1/harness/process", [&runtime](const auto &req, auto &res) {
		SetJson(res, ToJson(runtime.ProcessWorld(ParseTicks(req))));
	});

	api.Get("/api/v1/harness/events", [&runtime](const auto &req, auto &res) {
		uint64_t since = 0;
		size_t limit = 100;
		if (req.has_param("since")) {
			since = Strings::ToUnsignedBigInt(req.get_param_value("since"));
		}
		if (req.has_param("limit")) {
			limit = Strings::ToUnsignedInt(req.get_param_value("limit"));
		}

		nlohmann::json events = nlohmann::json::array();
		for (const auto &event: runtime.EventsSince(since, limit)) {
			events.push_back(ToJson(event));
		}

		SetJson(res, {{"events", events}});
	});

	api.Post("/api/v1/harness/events/drain", [&runtime](const auto &, auto &res) {
		nlohmann::json events = nlohmann::json::array();
		for (const auto &event: runtime.DrainEvents()) {
			events.push_back(ToJson(event));
		}

		SetJson(res, {{"events", events}});
	});

	api.Post("/api/v1/harness/scenarios/spell-cast-start", [&runtime](const auto &req, auto &res) {
		SetJson(res, ToJson(runtime.StartKnownSpellCast(ParseSpellID(req))));
	});

	api.Post("/api/v1/harness/scenarios/headless-client/target", [&runtime](const auto &, auto &res) {
		SetJson(res, ToJson(runtime.RunHeadlessClientTarget()));
	});

	api.Post("/api/v1/harness/scenarios/bot-slow-maintenance/current-target", [&runtime](const auto &, auto &res) {
		SetJson(res, ToJson(runtime.RunBotSlowMaintenanceCurrentTarget()));
	});

	api.Post("/api/v1/harness/scenarios/bot-slow-maintenance/fallback", [&runtime](const auto &, auto &res) {
		SetJson(res, ToJson(runtime.RunBotSlowMaintenanceFallback()));
	});

	api.Post("/api/v1/harness/scenarios/bot-slow-maintenance/mezzed", [&runtime](const auto &, auto &res) {
		SetJson(res, ToJson(runtime.RunBotSlowMaintenanceMezzed()));
	});

	api.Post("/api/v1/harness/scenarios/autonomous-actor-loop", [&runtime](const auto &, auto &res) {
		SetJson(res, ToJson(runtime.RunAutonomousActorLoop()));
	});

	if (options.enable_autonomous_actor_prototype) {
		api.Get("/api/v1/harness/autonomous-actors/prototype/session", [&runtime](const auto &, auto &res) {
			SetJson(res, ToJson(runtime.AutonomousActorPrototypeSession()));
		});

		api.Post("/api/v1/harness/autonomous-actors/prototype/session/start", [&runtime](const auto &, auto &res) {
			SetJson(res, ToJson(runtime.StartAutonomousActorPrototypeSession()));
		});

		api.Post("/api/v1/harness/autonomous-actors/prototype/actions", [&runtime](const auto &req, auto &res) {
			SetJson(
				res,
				ToJson(
					runtime.EnqueueAutonomousActorPrototypeAction(
						ParseActionStringField(req, "kind"),
						ParseActionStringField(req, "detail")
					)
				)
			);
		});

		api.Post("/api/v1/harness/autonomous-actors/prototype/session/stop", [&runtime](const auto &, auto &res) {
			SetJson(res, ToJson(runtime.StopAutonomousActorPrototypeSession()));
		});
	}

	api.Post("/api/v1/harness/shutdown", [&runtime, &api](const auto &, auto &res) {
		runtime.RequestShutdown();
		SetJson(res, {{"shutdown_requested", true}});
		std::thread([&api]() { api.stop(); }).detach();
	});

	std::thread watchdog;
	if (options.max_runtime_seconds > 0) {
		watchdog = std::thread([&]() {
			std::unique_lock lock(finished_mutex);
			const bool stopped = finished_cv.wait_for(
				lock,
				std::chrono::seconds(options.max_runtime_seconds),
				[&finished]() { return finished; }
			);

			if (!stopped) {
				runtime.RequestShutdown();
				api.stop();
			}
		});
	}

	LogInfo("Zone Harness HTTP listening on localhost port [{}]", options.port);
	const bool listened = api.listen("localhost", options.port);

	{
		std::lock_guard lock(finished_mutex);
		finished = true;
	}
	finished_cv.notify_all();
	if (watchdog.joinable()) {
		watchdog.join();
	}

	runtime.Shutdown();
	return listened;
}

}

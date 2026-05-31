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
		{"zone", ToJson(snapshot.zone)},
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
	return {
		{"sequence", event.sequence},
		{"type", event.type},
		{"message", event.message},
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

	api.Get("/api/v1/harness/events", [&runtime](const auto &, auto &res) {
		nlohmann::json events = nlohmann::json::array();
		for (const auto &event: runtime.DrainEvents()) {
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

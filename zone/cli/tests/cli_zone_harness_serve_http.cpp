/*	EQEmu: EQEmulator

	Copyright (C) 2001-2026 EQEmu Development Team

	This program is free software; you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation; either version 3 of the License, or
	(at your option) any later version.
*/

#include "zone/zone_cli.h"

#include "common/strings.h"
#include "zone/harness/zone_harness_http.h"

#include <cstdlib>

void ZoneCLI::TestServeHttp(int argc, char **argv, argh::parser &cmd, std::string &description)
{
	description = "Serve Zone Harness HTTP endpoints for one-off runtime validation";

	if (cmd[{"-h", "--help"}]) {
		return;
	}

	EQ::ZoneHarness::HttpServerOptions options;

	if (!cmd("--zone").str().empty()) {
		options.zone_short_name = cmd("--zone").str();
	}

	if (!cmd("--port").str().empty()) {
		options.port = Strings::ToInt(cmd("--port").str());
	}

	if (!cmd("--key").str().empty()) {
		options.bearer_token = cmd("--key").str();
	}

	if (!cmd("--max-runtime-seconds").str().empty()) {
		options.max_runtime_seconds = Strings::ToUnsignedInt(cmd("--max-runtime-seconds").str());
	}

	if (cmd["--enable-autonomous-actor-prototype"]) {
		options.enable_autonomous_actor_prototype = true;
	}

	if (!EQ::ZoneHarness::ServeHttp(options)) {
		std::exit(1);
	}
}

/*	EQEmu: EQEmulator

	Copyright (C) 2001-2026 EQEmu Development Team

	This program is free software; you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation; either version 3 of the License, or
	(at your option) any later version.
*/

#pragma once

#include <cstdint>
#include <string>

namespace EQ::ZoneHarness {

struct HttpServerOptions {
	std::string zone_short_name = "qrg";
	uint32_t instance_id = 0;
	int port = 9099;
	std::string bearer_token;
	uint32_t max_runtime_seconds = 0;
	bool enable_autonomous_actor_prototype = false;
};

bool ServeHttp(const HttpServerOptions &options);

}

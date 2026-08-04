#pragma once

#include <cstdint>
#include <string>

class ZoneDatabase;

class ActorActionExecutor {
public:
	ActorActionExecutor(ZoneDatabase& database, uint32_t zone_id, uint32_t instance_id, uint32_t zone_server_id);
	void ProcessOne();

private:
	ZoneDatabase& database_;
	uint32_t zone_id_;
	uint32_t instance_id_;
	std::string claimant_;
};

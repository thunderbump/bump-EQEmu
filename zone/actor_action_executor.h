#pragma once

#include <cstdint>
#include <ctime>
#include <functional>
#include <optional>
#include <string>

class ZoneDatabase;

class ActorActionExecutor {
public:
	using Clock = std::function<time_t()>;
	using GameplayEventWatermarkReader = std::function<std::optional<uint64_t>(uint32_t)>;

	ActorActionExecutor(
		ZoneDatabase& database, uint32_t zone_id, uint32_t instance_id, uint32_t zone_server_id,
		Clock clock = []() { return std::time(nullptr); }, GameplayEventWatermarkReader watermark_reader = {});
	void ProcessOne();

private:
	ZoneDatabase& database_;
	uint32_t zone_id_;
	uint32_t instance_id_;
	std::string claimant_;
	Clock clock_;
	GameplayEventWatermarkReader watermark_reader_;
	uint32_t candidate_offset_ = 0;
};

#pragma once

#include <cstdint>
#include <ctime>
#include <functional>
#include <memory>
#include <optional>
#include <string>

class Bot;
class Mob;
class NPC;
class ZoneDatabase;

class ActorActionExecutor {
public:
	using Clock = std::function<time_t()>;
	using GameplayEventWatermarkReader = std::function<std::optional<uint64_t>(uint32_t)>;

	ActorActionExecutor(
		ZoneDatabase& database, uint32_t zone_id, uint32_t instance_id, uint32_t zone_server_id,
		Clock clock = []() { return std::time(nullptr); }, GameplayEventWatermarkReader watermark_reader = {});
	~ActorActionExecutor();
	void ProcessOne();
	static void ObserveNpcDeath(
		uint16_t entity_id, uint32_t npc_type_id, uint64_t runtime_instance_id, uint16_t killer_entity_id);

private:
	struct HuntEngagement;
	void CancelHuntCombat();
	void ProcessHuntEngagement(time_t now);
	ZoneDatabase& database_;
	uint32_t zone_id_;
	uint32_t instance_id_;
	std::string claimant_;
	Clock clock_;
	GameplayEventWatermarkReader watermark_reader_;
	uint32_t candidate_offset_ = 0;
	std::unique_ptr<HuntEngagement> hunt_engagement_;
};

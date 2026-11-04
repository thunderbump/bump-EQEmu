#include "actor_zones.h"
#ifdef EQEMU_RUST_BOOTSTRAP_BRIDGE
#include "npc.h"
#include "zone.h"
#include "zonedb.h"
#include "map.h"
#include "water_map.h"
#include "mob_movement_manager.h"
#include "common/content/world_content_service.h"
#include "common/player_zone_access.h"
#include "common/rulesys.h"
#include "common/strings.h"
#include "common/zone_store.h"
#include <cmath>
#include <algorithm>
#include <limits>

extern Zone *zone;
extern ZoneDatabase content_db;
namespace ActorZones {
namespace {
bool Access(NPC &actor, uint32_t destination) {
  auto *data = GetZoneVersionWithFallback(destination, 0);
  if (!data || data->shard_at_player_count > 0) { return false; }
  const bool flag = !data->flag_needed.empty() && Strings::IsNumber(data->flag_needed) && Strings::ToBool(data->flag_needed);
  // MVP actors are ordinary status-zero, non-GM characters without zone flags.
  if (0 < RuleI(GM, MinStatusToZoneAnywhere) && CheckPlayerZoneAccess(actor.GetLevel(), 0, false, data->min_level, data->max_level,
      data->min_status, flag, false) != PlayerZoneAccessFailure::None) { return false; }
  const auto expansion = WorldContentService::Instance()->GetCurrentExpansion();
  if (expansion >= 0 && data->expansion > expansion && !data->bypass_expansion_check) { return false; }
  const auto route = WorldContentService::Instance()->FindZone(destination, 0);
  // Do not bypass player routing into a different zone/public instance. This
  // initial actor profile supports ordinary base-version, instance-zero zones.
  return !route.zone_id || (route.zone_id == destination && route.instance.id == 0);
}
bool Project(NPC &actor, const ZonePoint &point, Connection &connection) {
  if (!(point.client_version_mask & EQ::versions::bitRoF2) || point.target_zone_instance ||
      !point.target_zone_id || point.target_zone_id == zone->GetZoneID() || !Access(actor, point.target_zone_id) ||
      !std::isfinite(point.x) || !std::isfinite(point.y) || !std::isfinite(point.z) ||
      std::abs(point.x) >= 999999 || std::abs(point.y) >= 999999 || std::abs(point.z) >= 999999 ||
      !zone->zonemap || !zone->watermap || !zone->pathing) { return false; }
  glm::vec3 probe(point.x, point.y, point.z + 20);
  const auto floor = zone->zonemap->FindBestZ(probe, nullptr);
  if (!std::isfinite(floor) || floor == BEST_Z_INVALID) { return false; }
  const glm::vec3 exit(point.x, point.y, floor + actor.GetZOffset());
  if (zone->watermap->InLiquid(exit) || (std::hypot(actor.GetX()-exit.x, actor.GetY()-exit.y) > 5 && !MobMovementManager::Get().CanNavigateActorTo(&actor, exit.x, exit.y, exit.z, MobMovementManager::ActorRoutePurpose::Transfer))) { return false; }
  const auto x = std::abs(point.target_x) == 999999 ? actor.GetX() : point.target_x;
  const auto y = std::abs(point.target_y) == 999999 ? actor.GetY() : point.target_y;
  const auto z = std::abs(point.target_z) == 999999 ? actor.GetZ() : point.target_z;
  if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z)) { return false; }
  connection = {point.number, point.target_zone_id, {exit.x,exit.y,exit.z}, {x,y,z}, 0};
  return true;
}
}
bool MayEnter(NPC &actor, uint32_t destination) { return Access(actor, destination); }
bool Resolve(NPC &actor, uint32_t id, uint32_t destination, Connection &connection) {
  LinkedListIterator<ZonePoint *> points(zone->zone_point_list);
  for (points.Reset(); points.MoreElements(); points.Advance()) {
    const auto *point = points.GetData();
    if (point->number == id && point->target_zone_id == destination) { return Project(actor, *point, connection); }
  }
  return false;
}
std::vector<Connection> Nearby(NPC &actor, const std::map<int32_t, int32_t> &reputation, HuntBounds bounds) {
  std::vector<Connection> connections;
  if (actor.GetLevel() <= 1 || actor.IsMoving() || actor.IsEngaged()) { return connections; }
  LinkedListIterator<ZonePoint *> points(zone->zone_point_list);
  for (points.Reset(); points.MoreElements(); points.Advance()) {
    Connection connection;
    if (!Project(actor, *points.GetData(), connection)) { continue; }
    const auto *name = ZoneName(connection.zone);
    if (!name) { continue; }
    // These are travel hints, never combat identities or permission. Once in
    // the destination, only live incarnation-checked native NPCs can be hunted.
    const auto rows = content_db.QueryDatabase(fmt::format(
        "SELECT s.id,s.x,s.y,n.level,f.primaryfaction FROM spawn2 s "
        "JOIN spawnentry e ON e.spawngroupID=s.spawngroupID JOIN npc_types n ON n.id=e.npcID "
        "JOIN npc_faction f ON f.id=n.npc_faction_id WHERE s.zone='{}' AND (s.version=0 OR s.version=-1) AND s.enabled=1 "
        "AND n.level BETWEEN {} AND {} AND n.merchant_id=0 ORDER BY s.id,n.id LIMIT 4097",
        Strings::Escape(name), bounds.minimum, bounds.maximum));
    if (!rows.Success() || rows.RowCount() > 4096) { continue; }
    auto distance = std::numeric_limits<double>::infinity();
    for (auto row = rows.begin(); row != rows.end(); ++row) {
      const auto faction = Strings::ToInt(row[4]);
      const auto found = reputation.find(faction);
      const auto reaction = actor.GetPlayerFactionLevel(nullptr, faction, actor.GetFactionRace(), actor.GetClass(),
          actor.GetDeity(), 0, found == reputation.end() ? 0 : found->second);
      if (reaction != FACTION_THREATENINGLY && reaction != FACTION_SCOWLS) { continue; }
      const auto x = Strings::ToFloat(row[1]), y = Strings::ToFloat(row[2]);
      if (!std::isfinite(x) || !std::isfinite(y)) { continue; }
      const auto candidate = std::hypot(actor.GetX()-connection.exit.x, actor.GetY()-connection.exit.y) +
          std::hypot(x-connection.arrival.x, y-connection.arrival.y);
      distance = std::min(distance, candidate);
    }
    if (std::isfinite(distance)) {
      connection.distance = distance;
      connections.push_back(connection);
    }
    if (connections.size() > 64) { return {}; }
  }
  return connections;
}
}
#endif

#include "actor_demo.h"
#include "actor_population.h"
#ifdef EQEMU_RUST_BOOTSTRAP_BRIDGE
#include "actor_demo_state.h"
#include "actor_demo_native.h"
#include "actor_hunt_state.h"
#include "actor_body.h"
#include "entity.h"
#include "npc.h"
#include "zone.h"
#include "map.h"
#include "water_map.h"
#include "mob_movement_manager.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <memory>
#include <vector>
#include <unistd.h>

extern EntityList entity_list;
extern Zone *zone;
extern ZoneDatabase content_db;

namespace ActorDemo {
namespace {
uint64_t Now()
{
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
}
NativeIdentity Identify(Mob *mob)
{
    return mob && mob->IsNPC() ? NativeIdentity{mob->GetID(), mob->CastToNPC()->NativeIncarnation()} : NativeIdentity{};
}
NPC *Find(const NativeIdentity &key)
{
    auto *npc = entity_list.GetNPCByID(key.id);
    return key.Matches(Identify(npc)) && entity_list.GetMob(key.id) == npc ? npc : nullptr;
}

class Demo {
public:
    explicit Demo(std::string directory, bool hunt) : dir(std::move(directory)), public_hunt(hunt) {
        session = std::to_string(getpid()) + "-" + std::to_string(Now()) + "-" +
            std::to_string(ActorBody::Get().Inspect().epoch);
        // No queued command survives entry into a new zone/process session.
        std::remove(Path("command").c_str());
        custody.public_hunt = public_hunt;
    }
    Run run;
    Custody custody;

    bool PairInsideHuntArea(Mob *attacker, Mob *target) const {
        if (!public_hunt || (!custody.actor.Matches(Identify(attacker)) && !custody.actor.Matches(Identify(target)))) { return true; }
        auto *body = Find(custody.actor);
        auto *prey = Find(custody.target);
        return body && prey && InsideHuntArea(Position(body), home, HuntLeash) &&
            InsideHuntArea(Position(prey), home, HuntSearchRadius);
    }
    bool PreserveRetaliationHate(Mob *prey) const {
        if (!public_hunt || !custody.target.Matches(Identify(prey))) { return false; }
        auto *body = Find(custody.actor);
        auto *target = Find(custody.target);
        const auto view = View();
        return body && target && prey == target && target->GetHateListCount() == 1 && target->CheckAggro(body) &&
            custody.CanRetaliate(view, Identify(target), Identify(body),
                ActorBody::Get().HasAdmittedPublicCombat(body, target)) &&
            PairInsideHuntArea(target, body) && target->CombatRange(body) &&
            target->CheckLosFN(body->GetX(), body->GetY(), body->GetZ(), body->GetSize()) && target->IsAttackAllowed(body);
    }

    std::string Path(const char *name) const { return dir + "/" + name; }
    NativeView View() const {
        const auto s = ActorBody::Get().Inspect();
        return {s.epoch, s.generation, s.alive, Identify(Find(custody.actor)), Identify(Find(custody.target)),
            ActorBody::Get().HasPendingTargetKill(custody.target.incarnation), s.goal};
    }
    bool Drained() const { return custody.PlanRetirement(View()).drained; }
    void Retire() {
        if (public_hunt) { DetachPublicTarget(); }
        auto &adapter = ActorBody::Get();
        const auto view = View();
        const auto plan = custody.PlanRetirement(view);
        if (plan.actor) {
            adapter.Send({eqemu_proof::Kind::Retire, view.epoch, view.generation, 0, 0, 0, 0});
        }
        if (auto *target = Find(custody.target); plan.target && target && !target->GetDepop()) {
            target->Depop(false);
        }
    }

    bool Spawn() {
        if (public_hunt) { return SpawnHunter(); }
        auto &adapter = ActorBody::Get();
        if (adapter.Inspect().alive || !Drained()) { return false; }
        const auto *source = content_db.LoadNPCTypesData(2011);
        if (!source || source->npc_id != 2011 || std::strcmp(source->name, "a_large_rat") ||
            source->level != 1 || source->maxlevel || source->race != 36 || source->class_ != 1 ||
            source->bodytype != 21 || source->max_hp != 11 || source->min_dmg != 1 || source->max_dmg != 6 ||
            source->npc_spells_id || source->npc_spells_effects_id || source->merchanttype ||
            source->is_quest_npc || source->untargetable || source->special_abilities[0] ||
            !zone->zonemap || !zone->pathing || !zone->watermap) { return false; }
        // Read immutable public spawn geometry, never touch its NPC or timer.
        bool anchor = false;
        glm::vec3 target;
        LinkedListIterator<Spawn2 *> it(zone->spawn2_list);
        for (it.Reset(); it.MoreElements(); it.Advance()) {
            auto *spawn = it.GetData();
            if (spawn->GetID() == 10863 && spawn->SpawnGroupID() == 7678) {
                target = {spawn->GetX(), spawn->GetY() + 30, spawn->GetZ() + 20};
                anchor = true;
                break;
            }
        }
        if (!anchor) { return false; }
        if (!std::isfinite(target.x) || !std::isfinite(target.y) || !std::isfinite(target.z) ||
            std::abs(target.x) > 1000000 || std::abs(target.y) > 1000000 ||
            std::abs(target.z) > 1000000) { return false; }
        const auto floor = zone->zonemap->FindBestZ(target, nullptr);
        if (floor == BEST_Z_INVALID || !std::isfinite(floor)) { return false; }
        target.z = floor;
        auto type = std::make_unique<NPCType>(*source);
        std::strcpy(type->name, "Staging_Rat");
        type->loottable_id = 0;
        type->skip_global_loot = true;
        type->npc_faction_id = 0;
        type->aggroradius = type->assistradius = 0;
        type->npc_aggro = false;
        type->emoteid = 0;
        type->show_name = true;
        auto npc = std::make_unique<NPC>(type.get(), nullptr, glm::vec4(target, 0), GravityBehavior::Ground);
        // This owned training target stays in the viewing area at low HP.
        // Public rats and the sealed native proof retain ordinary flee behavior.
        npc->SetSpecialAbility(SpecialAbility::FleeingImmunity, 1);
        target.z += npc->GetZOffset();
        npc->SetPosition(target.x, target.y, target.z);
        npc->SaveGuardSpot(glm::vec4(target, 0));
        glm::vec3 start(target.x + 18, target.y, target.z + 20);
        const auto start_floor = zone->zonemap->FindBestZ(start, nullptr);
        if (start_floor == BEST_Z_INVALID || !std::isfinite(start_floor)) { return false; }
        start.z = start_floor + 3.75f;
        if (std::abs(start.z - target.z) > 6 || zone->watermap->InLiquid(target) ||
            zone->watermap->InLiquid(start) || !zone->zonemap->CheckLoS(start, target)) { return false; }
        npc->GiveNPCTypeData(type.release());
        // Registration owns the allocation before any later native observation.
        auto *native = npc.release();
        entity_list.AddNPC(native, true, true);
        custody.target = Identify(native);
        if (adapter.CreateVisibleMeleeBody(std::lround(start.x), std::lround(start.y),
            std::lround(start.z)).event != eqemu_proof::Event::BodyRequested) { return false; }
        const auto s = adapter.Inspect();
        custody.epoch = s.epoch;
        custody.generation = s.generation;
        auto *body = adapter.Body();
        if (!body) { return false; }
        custody.actor = Identify(body);
        actor_x = start.x; actor_y = start.y; actor_z = start.z;
        rat_x = target.x; rat_y = target.y; rat_z = target.z;
        custody.last = {};
        last_position = glm::vec3(body->GetPosition());
        last_approach = s.approach;
        return true;
    }

    void Execute(Action action, uint64_t now) {
        if (action == Action::Retire) { Retire(); }
        if (action == Action::Spawn && !Spawn()) {
            Execute(run.Stop(now, "setup_refused", Phase::Failed), now);
        }
        if (action == Action::Engage && public_hunt) {
            auto *body = Find(custody.actor);
            auto *target = Find(custody.target);
            if (!body || !target || !ActorBody::IsPublicHuntPrey(target) || target->IsEngaged()) {
                Execute(run.Yield(now, "target_unavailable"), now);
                return;
            }
            custody.combat_enabled = true;
            const auto report = ActorBody::Get().StartPublicHunt(custody.target.id);
            if (report.status != eqemu_proof::Status::Ok) {
                Execute(run.Yield(now, "melee_refused"), now);
            } else {
                custody.goal = ActorBody::Get().Inspect().goal;
                custody.last = {};
            }
        } else if (action == Action::Engage &&
            ActorBody::Get().StartMelee({custody.target.id}).status != eqemu_proof::Status::Ok) {
            Execute(run.Stop(now, "melee_refused", Phase::Failed), now);
        }
        if (action == Action::Release) {
            custody.combat_enabled = false; // close swing permission before any native callbacks
            const auto s = ActorBody::Get().Inspect();
            if (s.public_hunt && s.approach != eqemu_proof::Approach::Disabled) {
                ActorBody::Get().ReleasePublicHunt();
            }
            DetachPublicTarget();
        }
        if (action == Action::Rest) {
            custody.target = {};
            custody.contested = custody.interference = false;
            target_type = target_spawn = 0;
            if (auto *body = Find(custody.actor)) { body->SetAppearance(eaSitting); }
        }
        if (action == Action::Stand) {
            next_search = 0;
            if (auto *body = Find(custody.actor)) { body->SetAppearance(eaStanding); }
        }
    }

    void ReadCommand(uint64_t now) {
        std::ifstream input(Path("command"), std::ios::binary);
        if (!input) { return; }
        char buffer[129];
        input.read(buffer, sizeof(buffer));
        const auto size = input.gcount();
        input.close();
        if (std::remove(Path("command").c_str()) != 0) {
            command_result = "consume_failed";
            Execute(run.Stop(now, "control_io_failed", Phase::Failed), now);
            return;
        }
        Command command;
        if (!ParseCommand(std::string(buffer, size), command)) { command_result = "invalid_command"; return; }
        if (const auto *refused = AdmitRequest(command, session, last_request)) {
            command_result = refused; return;
        }
        if (!command.start) {
            Execute(run.Stop(now, "operator_stop"), now);
            command_result = run.Active() ? "stop_queued" : "already_terminal";
        } else if (run.Active() || !Drained() || ActorBody::Get().Inspect().alive) {
            command_result = "busy";
        } else {
            custody = {};
            custody.public_hunt = public_hunt;
            target_type = target_spawn = eligible_count = 0;
            selection = {}; selection_scan_ms = 0;
            selected_distance = 0; selected_from = {}; selected_position = {};
            next_search = 0;
            run_id = session + "-" + std::to_string(command.request);
            run.Start(now, command.seconds, public_hunt);
            command_result = "start_accepted";
        }
    }

    void Tick(uint64_t now) {
        if (now >= next_io) {
            ReadCommand(now);
            next_io = now + 1000;
            if (!WriteStatus(now)) { Execute(run.Stop(now, "status_io_failed", Phase::Failed), now); }
        }
        if (public_hunt) { TickHunt(now); return; }
        Facts facts;
        facts.drained = Drained();
        auto &adapter = ActorBody::Get();
        const auto s = adapter.Inspect();
        auto *body = Find(custody.actor);
        auto *target = Find(custody.target);
        const auto view = View();
        const bool swung = s.swings > custody.last.swings;
        custody.ObserveCounters(view, {s.swings, s.hits, s.damage});
        if (run.phase == Phase::Spawning || run.phase == Phase::Fighting) {
            if (!custody.OwnsRuntime(view) || !s.alive || !body ||
                adapter.Body() != body || body->GetDepop() || body->GetHP() <= 0) {
                facts.failure = "actor_identity_lost";
            } else {
                facts.ready = s.body == eqemu_proof::Body::Present && target;
                facts.victory = s.approach == eqemu_proof::Approach::Victory &&
                    s.last_terminal == eqemu_proof::Event::TargetKilled && s.damage > 0;
                const auto p = glm::vec3(body->GetPosition());
                facts.progress = swung || s.approach != last_approach ||
                    glm::distance(p, last_position) >= 0.25f;
                if (facts.progress) { last_position = p; }
                last_approach = s.approach;
                if (custody.TargetLost(view, target && !target->GetDepop() && target->GetHP() > 0, facts.victory)) {
                    facts.failure = "target_identity_lost";
                }
                if (body->GetOwnerID() || body->GetPetType() != PetType::None || body->GetSwarmInfo() ||
                    (target && (target->GetOwnerID() || target->GetPetType() != PetType::None || target->GetSwarmInfo())) ||
                    (body->IsEngaged() && (!target || body->GetHateListCount() != 1 || !body->CheckAggro(target))) ||
                    (target && target->IsEngaged() && (target->GetHateListCount() != 1 || !target->CheckAggro(body)))) {
                    facts.failure = "foreign_custody";
                }
                if (s.approach == eqemu_proof::Approach::Paused || s.approach == eqemu_proof::Approach::Lost ||
                    s.approach == eqemu_proof::Approach::Defeated) { facts.failure = "native_encounter_refused"; }
            }
        }
        if (custody.interference) { facts.failure = "foreign_interference"; }
        Execute(run.Advance(now, facts), now);
    }

    bool WriteStatus(uint64_t now) const {
        const auto elapsed = run.began ? (run.ended ? run.ended : now) - run.began : 0;
        std::ofstream out(Path("status.json.tmp"), std::ios::trunc);
        if (!out) { return false; }
        // Every string below is generated internally, never reflected input.
        out << "{\"schema\":1,\"scenario\":\"" << (public_hunt ? "qeynos2-public-hunt-v2" : "qeynos2-owned-rat-v1")
            << "\",\"pid\":" << getpid()
            << ",\"session\":\"" << session << "\",\"run_id\":\"" << run_id
            << "\",\"last_request\":" << last_request << ",\"command_result\":\"" << command_result
            << "\",\"phase\":\"" << PhaseName(run.phase) << "\",\"elapsed_ms\":" << elapsed
            << ",\"attempted\":" << run.attempted << ",\"completed\":" << run.completed << ",\"failed\":" << run.failed
            << ",\"actor_epoch\":" << custody.epoch << ",\"actor_generation\":" << custody.generation
            << ",\"actor_id\":" << custody.actor.id << ",\"actor_incarnation\":" << custody.actor.incarnation
            << ",\"target_id\":" << custody.target.id << ",\"target_incarnation\":" << custody.target.incarnation
            << ",\"swings\":" << custody.total.swings << ",\"hits\":" << custody.total.hits << ",\"damage\":" << custody.total.damage
            << ",\"live_owned\":" << (unsigned(bool(Find(custody.actor))) + unsigned(!public_hunt && bool(Find(custody.target))))
            << ",\"custody_drained\":" << (Drained() ? "true" : "false")
            << ",\"terminal_reason\":\"" << run.reason << "\",\"updated_ms\":" << now
            << ",\"position_kind\":\"" << (public_hunt ? "current" : "setup") << "\",\"actor_position\":";
        auto position = [&](NPC *npc) {
            if (npc && std::isfinite(npc->GetX()) && std::isfinite(npc->GetY()) && std::isfinite(npc->GetZ())) {
                out << '[' << npc->GetX() << ',' << npc->GetY() << ',' << npc->GetZ() << ']';
            } else { out << "null"; }
        };
        if (public_hunt) { position(Find(custody.actor)); }
        else { out << '[' << actor_x << ',' << actor_y << ',' << actor_z << ']'; }
        out << ",\"target_position\":";
        if (public_hunt) { position(Find(custody.target)); }
        else { out << '[' << rat_x << ',' << rat_y << ',' << rat_z << ']'; }
        if (public_hunt) {
            const auto *body = Find(custody.actor);
            out << ",\"target_owned\":false,\"target_type_id\":" << target_type << ",\"target_spawn_id\":" << target_spawn
                << ",\"goal\":" << custody.goal << ",\"yielded\":" << run.yielded << ",\"last_outcome\":\"" << run.last_outcome
                << "\",\"actor_hp\":" << (body ? body->GetHP() : 0) << ",\"actor_max_hp\":" << (body ? body->GetMaxHP() : 0)
                << ",\"rest_elapsed_ms\":" << run.RestElapsed(now) << ",\"rest_min_ms\":" << HuntRestMinimumMs
                << ",\"rest_hp_percent\":" << HuntRestHPPercent
                << ",\"incoming_swings\":" << custody.incoming.swings << ",\"incoming_hits\":" << custody.incoming.hits
                << ",\"incoming_damage\":" << custody.incoming.damage << ",\"actor_min_hp\":" << custody.minimum_hp
                << ",\"observed_rest_hp_gain\":" << custody.rest_hp_gain
                << ",\"incoming_non_damaging_swings\":" << custody.incoming.swings - custody.incoming.hits
                << ",\"incoming_last_native_result\":" << custody.incoming_last_native_result
                << ",\"actor_hp_observed\":" << (custody.hp_observed ? "true" : "false")
                << ",\"retaliation_hate_preserved\":" << custody.retaliation_hate_preserved;
            auto *prey = Find(custody.target);
            out << ",\"prey_native_target_id\":" << (prey && prey->GetTarget() ? prey->GetTarget()->GetID() : 0)
                << ",\"prey_hate_entries\":" << (prey ? prey->GetHateListCount() : 0)
                << ",\"home_position\":[" << home.x << ',' << home.y << ',' << home.z
                << "],\"search_radius\":" << HuntSearchRadius << ",\"actor_leash\":" << HuntLeash
                << ",\"z_range\":" << HuntZRange << ",\"route_limit\":" << HuntRouteLimit
                << ",\"floor_upper_slack\":" << HuntZRange << ",\"floor_lower_slack\":" << HuntFloorLowerSlack
                << ",\"eligible_count\":" << eligible_count << ",\"selected_distance_xy\":" << selected_distance
                << ",\"selected_from_position\":[" << selected_from.x << ',' << selected_from.y << ',' << selected_from.z
                << "],\"selected_target_position\":[" << selected_position.x << ',' << selected_position.y << ',' << selected_position.z << ']';
            out << ",\"selection_scan_ms\":" << selection_scan_ms << ",\"selection_rejections\":{";
            for (unsigned i = 1; i < static_cast<unsigned>(HuntRejection::Count); ++i) {
                if (i > 1) { out << ','; }
                out << '"' << HuntRejectionName(static_cast<HuntRejection>(i)) << "\":" << selection.rejected[i];
            }
            out << "},\"nearest_rejected\":";
            if (!selection.nearest.id) { out << "null"; }
            else {
                auto number = [&](double value) { if (std::isfinite(value)) { out << value; } else { out << "null"; } };
                out << "{\"id\":" << selection.nearest.id << ",\"incarnation\":" << selection.nearest.incarnation
                    << ",\"reason\":\"" << HuntRejectionName(selection.reason) << "\",\"position\":["
                    << selection.position.x << ',' << selection.position.y << ',' << selection.position.z
                    << "],\"floor_valid\":" << (selection.floor_valid ? "true" : "false") << ",\"floor\":";
                number(selection.floor);
                out << ",\"z_offset\":"; number(selection.z_offset);
                out << ",\"height_error\":"; number(selection.height_error);
                out << ",\"los\":" << (selection.los ? "true" : "false")
                    << ",\"endpoint\":[" << selection.endpoint.x << ',' << selection.endpoint.y << ',' << selection.endpoint.z
                    << "],\"route_result\":\"" << selection.route.result << "\",\"route_nodes\":";
                if (selection.route.queried) { out << selection.route.nodes; } else { out << "null"; }
                out << ",\"route_direct_distance\":"; number(selection.route.direct_distance);
                out << ",\"route_length\":";
                if (selection.route.length_complete) { number(selection.route.length_prefix); } else { out << "null"; }
                out << ",\"route_length_prefix\":"; number(selection.route.length_prefix);
                out << ",\"route_endpoint_error\":"; number(selection.route.endpoint_error);
                out << '}';
            }
        }
        out << "}\n";
        out.close();
        return bool(out) && std::rename(Path("status.json.tmp").c_str(), Path("status.json").c_str()) == 0;
    }

private:
    static HuntPosition Position(NPC *npc) { return {npc->GetX(), npc->GetY(), npc->GetZ()}; }
    bool SpawnHunter() {
        auto &adapter = ActorBody::Get();
        if (adapter.Inspect().alive || !Drained() || !zone->zonemap || !zone->pathing || !zone->watermap) { return false; }
        glm::vec3 point(HuntHomeX, HuntHomeY, 29);
        const auto floor = zone->zonemap->FindBestZ(point, nullptr);
        if (floor == BEST_Z_INVALID || !std::isfinite(floor)) { return false; }
        point.z = floor + 3.75f;
        if (zone->watermap->InLiquid(point)) { return false; }
        home = {point.x, point.y, point.z};
        if (adapter.CreateVisibleMeleeBody(std::lround(point.x), std::lround(point.y), std::lround(point.z)).event !=
            eqemu_proof::Event::BodyRequested) { return false; }
        const auto s = adapter.Inspect();
        custody.epoch = s.epoch; custody.generation = s.generation;
        auto *body = adapter.Body();
        if (!body) { return false; }
        custody.actor = Identify(body);
        actor_x = point.x; actor_y = point.y; actor_z = point.z;
        custody.last = {};
        last_position = glm::vec3(body->GetPosition());
        last_approach = s.approach;
        return true;
    }
    void DetachPublicTarget() {
        custody.combat_enabled = false;
        auto *body = Find(custody.actor);
        auto *target = Find(custody.target);
        if (!body) { return; }
        // Remove exactly our contribution. Public foreign hate/damage entries
        // and native stats, loot, flee, spawn and respawn remain untouched.
        if (target && target->CheckAggro(body)) { target->RemoveFromHateList(body); }
        if (target && body->CheckAggro(target)) { body->RemoveFromHateList(target); }
        if (body->IsEngaged()) { body->WipeHateList(); } // owned body only
        body->SetTarget(nullptr);
    }
    bool SelectPublicTarget(NPC *body) {
        selection = {};
        selection_scan_ms = Now();
        const auto from = Position(body);
        std::vector<HuntCandidate> candidates;
        for (const auto &entry : entity_list.GetNPCList()) {
            auto *npc = entry.second;
            HuntCandidate c{Identify(npc), Position(npc), npc->GetNPCTypeID(), npc->GetLevel()};
            c.natural = npc->GetSpawn() && npc->GetSpawn()->GetNPC() == npc;
            c.alive = npc->GetHP() > 0 && !npc->GetDepop();
            c.targetable = npc->IsTargetable();
            c.owned = npc->GetOwnerID() || npc->GetPetType() != PetType::None || npc->GetSwarmInfo() || npc->Charmed();
            c.engaged = npc->IsEngaged();
            c.moving = npc->IsMoving() || npc->IsCasting() || npc->IsStunned() || npc->IsMezzed() ||
                npc->IsFeared() || npc->IsBlind() || npc->IsRooted() || npc->IsPseudoRooted();
            c.service = !ActorBody::IsPublicHuntPrey(npc);
            if (HuntEligible(c, from, home)) { candidates.push_back(c); }
        }
        std::sort(candidates.begin(), candidates.end(), [&](const auto &a, const auto &b) { return HuntNearer(a, b, from); });
        eligible_count = candidates.size();
        for (const auto &c : candidates) {
            auto *npc = Find(c.key);
            if (!npc || npc->IsEngaged() || !ActorBody::IsPublicHuntPrey(npc)) {
                selection.Reject(HuntRejection::Identity);
                if (!selection.nearest.id) { selection.nearest = c.key; selection.position = c.position; selection.reason = HuntRejection::Identity; }
                continue;
            }
            glm::vec3 position(npc->GetPosition());
            auto floor_probe = position + glm::vec3(0, 0, 20);
            const auto floor = zone->zonemap->FindBestZ(floor_probe, nullptr);
            HuntSelectionEvidence probe;
            probe.nearest = c.key; probe.position = c.position;
            probe.floor = floor; probe.z_offset = npc->GetZOffset();
            probe.floor_valid = floor != BEST_Z_INVALID && std::isfinite(floor);
            probe.height_error = std::abs(position.z - floor - probe.z_offset);
            // The coordinate overload does not overwrite the actor's LOS
            // cache for candidates rejected before the original LOS gate.
            probe.los = body->CheckLosFN(npc->GetX(), npc->GetY(), npc->GetZ(), npc->GetSize());
            if (!probe.floor_valid) { probe.reason = HuntRejection::FloorInvalid; }
            else if (!HuntFloorAllowed(position.z, floor, probe.z_offset)) { probe.reason = HuntRejection::FloorHeight; }
            else if (zone->watermap->InLiquid(position)) { probe.reason = HuntRejection::Water; }
            else if (!body->CheckLosFN(npc)) { probe.reason = HuntRejection::Los; }
            const auto distance = std::sqrt(DistanceSquared(from, c.position));
            // Same rounded two-unit-short destination as Rust's approach.
            const auto scale = distance > 2 ? 2 / distance : 0;
            probe.endpoint = {std::round(c.position.x - (c.position.x - from.x) * scale),
                std::round(c.position.y - (c.position.y - from.y) * scale), std::round(c.position.z)};
            // Also probe the nearest rejection after an earlier gate fails.
            // This performs no movement or NPC writes, and adds at most one
            // otherwise-unneeded path query per two-second selection scan.
            if (probe.reason == HuntRejection::None || !selection.nearest.id) {
                bool reachable = true;
                if (distance > 5 || !body->CombatRange(npc)) {
                    reachable = MobMovementManager::Get().CanNavigateActorTo(body, probe.endpoint.x, probe.endpoint.y,
                        probe.endpoint.z, true, &probe.route);
                } else { probe.route.result = "native_melee"; }
                if (!reachable && probe.reason == HuntRejection::None) { probe.reason = HuntRejection::Route; }
            }
            if (probe.reason != HuntRejection::None) {
                if (!selection.nearest.id) {
                    const auto counts = selection.rejected;
                    selection = probe;
                    selection.rejected = counts;
                }
                selection.Reject(probe.reason);
                continue;
            }
            custody.target = c.key;
            target_type = c.type; target_spawn = npc->GetSpawnPointID();
            selected_distance = distance; selected_from = from; selected_position = c.position;
            rat_x = position.x; rat_y = position.y; rat_z = position.z;
            return true;
        }
        return false;
    }
    void TickHunt(uint64_t now) {
        auto &adapter = ActorBody::Get();
        auto s = adapter.Inspect();
        auto *body = Find(custody.actor);
        auto *target = Find(custody.target);
        auto view = View();
        if (body) { custody.ObserveHP(body->GetHP(), run.phase == Phase::Resting &&
            body->GetAppearance() == eaSitting && !body->IsEngaged()); }
        const bool swung = s.swings > custody.last.swings;
        custody.ObserveCounters(view, {s.swings, s.hits, s.damage});
        Facts facts;
        facts.drained = Drained();
        if (run.Active() && run.phase != Phase::Waiting && run.phase != Phase::Draining) {
            if (!custody.OwnsRuntime(view) || !s.alive || !body || adapter.Body() != body ||
                body->GetDepop() || body->GetHP() <= 0) {
                facts.failure = s.approach == eqemu_proof::Approach::Defeated ? "actor_defeated" : "actor_identity_lost";
            } else if (body->GetOwnerID() || body->GetPetType() != PetType::None || body->GetSwarmInfo() || body->Charmed()) {
                facts.failure = "actor_foreign_custody";
            } else if (!InsideHuntArea(Position(body), home, HuntLeash)) {
                facts.failure = "actor_leash_exceeded";
            } else {
                facts.ready = s.body == eqemu_proof::Body::Present;
                facts.rested = HuntRested(body->GetHP(), body->GetMaxHP()) && !body->IsEngaged() &&
                    !body->IsMoving() && body->GetAppearance() == eaSitting;
                const auto p = glm::vec3(body->GetPosition());
                facts.progress = swung || s.approach != last_approach || glm::distance(p, last_position) >= 0.25f;
                if (facts.progress) { last_position = p; }
                last_approach = s.approach;
                if (custody.interference) { facts.yield = "foreign_interference"; }
                if (run.phase == Phase::Fighting) {
                    facts.victory = s.goal == custody.goal && s.target == custody.target.incarnation &&
                        s.approach == eqemu_proof::Approach::Victory &&
                        s.last_terminal == eqemu_proof::Event::TargetKilled && s.damage > 0;
                    if (custody.TargetLost(view, target && !target->GetDepop() && target->GetHP() > 0, facts.victory)) {
                        facts.yield = "target_lost";
                    } else if (target && !InsideHuntArea(Position(target), home, HuntSearchRadius)) {
                        facts.yield = "target_left_area";
                    } else if (target && target->IsEngaged() && (target->GetHateListCount() != 1 || !target->CheckAggro(body))) {
                        facts.yield = "target_contested";
                    }
                    if (custody.contested) { facts.yield = "target_contested"; }
                    if (s.approach == eqemu_proof::Approach::Paused || s.approach == eqemu_proof::Approach::Lost) {
                        if (!facts.yield) {
                            facts.yield = target && !ActorMeleeReachAllowed(true,
                                std::sqrt(DistanceSquared(Position(body), Position(target))), body->GetZ() - target->GetZ(),
                                body->CombatRange(target)) ?
                                "target_out_of_reach" : "native_encounter_refused";
                        }
                    }
                }
                if (run.phase == Phase::Releasing) {
                    if (s.public_hunt && s.approach != eqemu_proof::Approach::Disabled) { adapter.ReleasePublicHunt(); }
                    s = adapter.Inspect();
                    facts.goal_idle = s.approach == eqemu_proof::Approach::Disabled && s.phase == eqemu_proof::Phase::Idle &&
                        !s.attacking && !body->IsMoving() && MobMovementManager::Get().InspectActorCombatMotion(body) ==
                            MobMovementManager::ActorCombatMotion::Stopped;
                }
                if (run.phase == Phase::Searching && now >= next_search && !facts.yield) {
                    next_search = now + HuntSearchIntervalMs;
                    facts.target_available = SelectPublicTarget(body);
                }
            }
        }
        Execute(run.Advance(now, facts), now);
    }
    std::string dir, session, run_id, command_result = "ready";
    bool public_hunt = false;
    uint64_t last_request = 0, next_io = 0;
    uint64_t next_search = 0;
    uint32_t target_type = 0, target_spawn = 0, eligible_count = 0;
    double selected_distance = 0;
    HuntPosition home{}, selected_from{}, selected_position{};
    HuntSelectionEvidence selection;
    uint64_t selection_scan_ms = 0;
    glm::vec3 last_position{};
    eqemu_proof::Approach last_approach = eqemu_proof::Approach::Disabled;
    float actor_x = 0, actor_y = 0, actor_z = 0, rat_x = 0, rat_y = 0, rat_z = 0;
};
std::unique_ptr<Demo> demo;
}

void Process()
{
    if (ActorPopulation::Selected()) { ActorPopulation::Process(); return; }
    if (!zone || zone->GetZoneID() != 2 || zone->GetInstanceID() || zone->GetInstanceVersion()) { return; }
    if (!demo) {
        const auto *enabled = std::getenv("EQEMU_STAGING_ACTOR_DEMO");
        const auto *directory = std::getenv("EQEMU_STAGING_ACTOR_DEMO_DIR");
        if (!enabled || std::strcmp(enabled, "1") || !directory || directory[0] != '/' ||
            std::strlen(directory) > 512) { return; }
        const auto *scenario = std::getenv("EQEMU_STAGING_ACTOR_SCENARIO");
        if (scenario && std::strcmp(scenario, "public-hunt-v2") && std::strcmp(scenario, "owned-rat-v1")) { return; }
        demo = std::make_unique<Demo>(directory, scenario && std::strcmp(scenario, "public-hunt-v2") == 0);
    }
    demo->Tick(Now());
}

bool CanCombat(Mob *attacker, Mob *target)
{
    if (ActorPopulation::Selected()) { return ActorPopulation::CanCombat(attacker,target); }
    return !demo || (demo->custody.CanCombat(Identify(attacker), Identify(target)) && demo->PairInsideHuntArea(attacker, target));
}
bool AllowCombat(Mob *attacker, Mob *target)
{
    if (ActorPopulation::Selected()) { return ActorPopulation::AllowCombat(attacker,target); }
    return !demo || (demo->custody.AdmitCombat(Identify(attacker), Identify(target)) && demo->PairInsideHuntArea(attacker, target));
}
bool AllowClientAbility(Mob *attacker, Mob *target, uint16_t requested_target)
{
    if (ActorPopulation::Selected()) {
        return requested_target && target && requested_target==target->GetID() && ActorPopulation::AllowCombat(attacker,target);
    }
    return !demo || demo->custody.AdmitClientAbility(requested_target, target ? target->GetID() : 0,
        Identify(attacker), Identify(target));
}
bool PreserveRetaliationHate(Mob *prey)
{
    if (ActorPopulation::Selected()) { return ActorPopulation::PreserveRetaliationHate(prey); }
    if (!demo || !demo->PreserveRetaliationHate(prey)) { return false; }
    ++demo->custody.retaliation_hate_preserved;
    return true;
}
void ObserveIncomingSwing(Mob *prey, Mob *body, int64_t before, int64_t after, int64_t native_result)
{
    if (ActorPopulation::Selected()) { ActorPopulation::ObserveIncomingSwing(prey,body,before,after);return; }
    if (!demo || !body || !prey || !demo->custody.public_hunt ||
        !demo->custody.actor.Matches(Identify(body)) || !demo->custody.target.Matches(Identify(prey))) { return; }
    const auto s = ActorBody::Get().Inspect();
    // Native Attack owns these arguments through Damage/Death. Registration
    // may already be gone for a fatal hit; incarnation and goal still correlate.
    const auto actor = Identify(body), target = Identify(prey);
    demo->custody.ObserveIncoming({s.epoch, s.generation, s.alive, actor, target, false, s.goal}, target, actor, before, after, native_result);
}

void BeginTeardown()
{
    if (ActorPopulation::Selected()) { ActorPopulation::BeginTeardown();return; }
    if (!demo) { return; }
    const auto now = Now();
    demo->Execute(demo->run.Stop(now, "zone_teardown"), now);
    demo->WriteStatus(now);
}

void FinishTeardown()
{
    if (!demo) { return; }
    const auto now = Now();
    Facts facts;
    facts.drained = true; // called after EntityList::Clear, before adapter reset
    demo->run.Advance(now, facts);
    demo->WriteStatus(now);
    demo.reset();
}
} // namespace ActorDemo
#endif

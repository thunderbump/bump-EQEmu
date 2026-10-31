#include "actor_demo.h"
#ifdef EQEMU_RUST_BOOTSTRAP_BRIDGE
#include "actor_demo_state.h"
#include "actor_body.h"
#include "entity.h"
#include "npc.h"
#include "zone.h"
#include "map.h"
#include "water_map.h"
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <memory>
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
struct Identity {
    uint16_t id = 0;
    uint64_t incarnation = 0;
    bool Matches(Mob *mob) const {
        return mob && mob->IsNPC() && mob->GetID() == id &&
            mob->CastToNPC()->NativeIncarnation() == incarnation;
    }
    NPC *Get() const {
        auto *npc = entity_list.GetNPCByID(id);
        return Matches(npc) && entity_list.GetMob(id) == npc ? npc : nullptr;
    }
    void Set(NPC *npc) { id = npc->GetID(); incarnation = npc->NativeIncarnation(); }
};

class Demo {
public:
    explicit Demo(std::string directory) : dir(std::move(directory)) {
        session = std::to_string(getpid()) + "-" + std::to_string(Now()) + "-" +
            std::to_string(ActorBody::Get().Inspect().epoch);
        // No queued command survives entry into a new zone/process session.
        std::remove(Path("command").c_str());
    }
    Run run;
    Identity actor, rat;
    uint64_t epoch = 0, generation = 0;
    bool interference = false;

    std::string Path(const char *name) const { return dir + "/" + name; }
    bool Drained() const {
        const auto s = ActorBody::Get().Inspect();
        return !actor.Get() && !rat.Get() &&
            (s.epoch != epoch || s.generation != generation || !s.alive);
    }
    void Retire() {
        auto &adapter = ActorBody::Get();
        const auto s = adapter.Inspect();
        if (s.epoch == epoch && s.generation == generation && s.alive) {
            adapter.Send({eqemu_proof::Kind::Retire, s.epoch, s.generation, 0, 0, 0, 0});
        }
        if (auto *target = rat.Get(); target && !target->GetDepop()) { target->Depop(false); }
    }

    bool Spawn() {
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
        rat.Set(native);
        if (adapter.CreateVisibleMeleeBody(std::lround(start.x), std::lround(start.y),
            std::lround(start.z)).event != eqemu_proof::Event::BodyRequested) { return false; }
        const auto s = adapter.Inspect();
        epoch = s.epoch;
        generation = s.generation;
        auto *body = adapter.Body();
        if (!body) { return false; }
        actor.Set(body);
        actor_x = start.x; actor_y = start.y; actor_z = start.z;
        rat_x = target.x; rat_y = target.y; rat_z = target.z;
        last_swings = last_hits = last_damage = 0;
        last_position = glm::vec3(body->GetPosition());
        last_approach = s.approach;
        return true;
    }

    void Execute(Action action, uint64_t now) {
        if (action == Action::Retire) { Retire(); }
        if (action == Action::Spawn && !Spawn()) {
            Execute(run.Stop(now, "setup_refused", true), now);
        }
        if (action == Action::Engage &&
            ActorBody::Get().StartMelee({rat.id}).status != eqemu_proof::Status::Ok) {
            Execute(run.Stop(now, "melee_refused", true), now);
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
            Execute(run.Stop(now, "control_io_failed", true), now);
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
            actor = {}; rat = {}; epoch = generation = 0;
            interference = false;
            swings = hits = damage = 0;
            run_id = session + "-" + std::to_string(command.request);
            run.Start(now, command.seconds);
            command_result = "start_accepted";
        }
    }

    void Tick(uint64_t now) {
        if (now >= next_io) {
            ReadCommand(now);
            next_io = now + 1000;
            if (!WriteStatus(now)) { Execute(run.Stop(now, "status_io_failed", true), now); }
        }
        Facts facts;
        facts.drained = Drained();
        auto &adapter = ActorBody::Get();
        const auto s = adapter.Inspect();
        auto *body = actor.Get();
        auto *target = rat.Get();
        if (s.epoch == epoch && s.generation == generation) {
            swings += s.swings - last_swings;
            hits += s.hits - last_hits;
            damage += s.damage - last_damage;
        }
        if (run.phase == Phase::Spawning || run.phase == Phase::Fighting) {
            if (s.epoch != epoch || s.generation != generation || !s.alive || !body ||
                adapter.Body() != body || body->GetDepop() || body->GetHP() <= 0) {
                facts.failure = "actor_identity_lost";
            } else {
                facts.ready = s.body == eqemu_proof::Body::Present && target;
                facts.victory = s.approach == eqemu_proof::Approach::Victory &&
                    s.last_terminal == eqemu_proof::Event::TargetKilled && s.damage > 0;
                const auto p = glm::vec3(body->GetPosition());
                facts.progress = s.swings > last_swings || s.approach != last_approach ||
                    glm::distance(p, last_position) >= 0.25f;
                if (facts.progress) { last_position = p; }
                last_approach = s.approach;
                if ((!target || target->GetDepop() || target->GetHP() <= 0) && !facts.victory) {
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
        if (s.epoch == epoch && s.generation == generation) {
            last_swings = s.swings; last_hits = s.hits; last_damage = s.damage;
        }
        if (interference) { facts.failure = "foreign_interference"; }
        Execute(run.Advance(now, facts), now);
    }

    bool WriteStatus(uint64_t now) const {
        const auto elapsed = run.began ? (run.ended ? run.ended : now) - run.began : 0;
        std::ofstream out(Path("status.json.tmp"), std::ios::trunc);
        if (!out) { return false; }
        // Every string below is generated internally, never reflected input.
        out << "{\"schema\":1,\"scenario\":\"qeynos2-owned-rat-v1\",\"pid\":" << getpid()
            << ",\"session\":\"" << session << "\",\"run_id\":\"" << run_id
            << "\",\"last_request\":" << last_request << ",\"command_result\":\"" << command_result
            << "\",\"phase\":\"" << PhaseName(run.phase) << "\",\"elapsed_ms\":" << elapsed
            << ",\"attempted\":" << run.attempted << ",\"completed\":" << run.completed << ",\"failed\":" << run.failed
            << ",\"actor_epoch\":" << epoch << ",\"actor_generation\":" << generation
            << ",\"actor_id\":" << actor.id << ",\"actor_incarnation\":" << actor.incarnation
            << ",\"target_id\":" << rat.id << ",\"target_incarnation\":" << rat.incarnation
            << ",\"swings\":" << swings << ",\"hits\":" << hits << ",\"damage\":" << damage
            << ",\"live_owned\":" << (unsigned(bool(actor.Get())) + unsigned(bool(rat.Get())))
            << ",\"custody_drained\":" << (Drained() ? "true" : "false")
            << ",\"terminal_reason\":\"" << run.reason << "\",\"updated_ms\":" << now
            << ",\"actor_position\":[" << actor_x << ',' << actor_y << ',' << actor_z
            << "],\"target_position\":[" << rat_x << ',' << rat_y << ',' << rat_z << "]}\n";
        out.close();
        return bool(out) && std::rename(Path("status.json.tmp").c_str(), Path("status.json").c_str()) == 0;
    }

private:
    std::string dir, session, run_id, command_result = "ready";
    uint64_t last_request = 0, next_io = 0;
    uint64_t swings = 0, hits = 0, damage = 0, last_swings = 0, last_hits = 0, last_damage = 0;
    glm::vec3 last_position{};
    eqemu_proof::Approach last_approach = eqemu_proof::Approach::Disabled;
    float actor_x = 0, actor_y = 0, actor_z = 0, rat_x = 0, rat_y = 0, rat_z = 0;
};
std::unique_ptr<Demo> demo;
}

void Process()
{
    if (!zone || zone->GetZoneID() != 2 || zone->GetInstanceID() || zone->GetInstanceVersion()) { return; }
    if (!demo) {
        const auto *enabled = std::getenv("EQEMU_STAGING_ACTOR_DEMO");
        const auto *directory = std::getenv("EQEMU_STAGING_ACTOR_DEMO_DIR");
        if (!enabled || std::strcmp(enabled, "1") || !directory || directory[0] != '/' ||
            std::strlen(directory) > 512) { return; }
        demo = std::make_unique<Demo>(directory);
    }
    demo->Tick(Now());
}

bool AllowCombat(Mob *attacker, Mob *target)
{
    if (!demo) { return true; }
    const bool a = demo->actor.Matches(attacker), b = demo->rat.Matches(attacker);
    const bool c = demo->actor.Matches(target), d = demo->rat.Matches(target);
    if (!(a || b || c || d) || (a && d) || (b && c)) { return true; }
    demo->interference = true;
    return false;
}

void BeginTeardown()
{
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

// Included only by the in-server registry, inside its anonymous namespace.
// No test advances Rust, steps navigation, swings, or manufactures combat facts.
struct MeleeVisit {
    bool active = false;
    uint32 zone_id = 0, instance_id = 0, server_id = 0;
    bool is_static = false, idle = false;
} melee_visit;

std::chrono::steady_clock::time_point melee_visit_started;
unsigned melee_boot_records = 0;

bool RestoreMeleeZone()
{
    if (!melee_visit.active) { return true; }
    KeepZoneHostAlive host;
    if (!host.Disable()) { return false; }
    ObserveMeleeBootStage("restore-shutdown-start");
    if (zone) { zone->Shutdown(); }
    ObserveMeleeBootStage("restore-shutdown-end");
    const bool booted = !zone && !is_zone_loaded &&
        Zone::Bootup(melee_visit.zone_id, melee_visit.instance_id, melee_visit.is_static);
    ObserveMeleeBootStage("restore-boot-returned");
    if (booted && zone) {
        zone->SetZoneServerId(melee_visit.server_id);
        zone->quest_idle_override = melee_visit.idle;
    }
    const bool restored = host.Restore();
    if (booted && restored && worldserver.Connected()) { melee_visit.active = false; return true; }
    return false;
}

bool VisitMeleeZone()
{
    if (!zone || !worldserver.Connected() || melee_visit.active) { return false; }
    melee_visit = {true, zone->GetZoneID(), zone->GetInstanceID(), zone->GetZoneServerId(),
        zone->IsStaticZone(), zone->quest_idle_override};
    melee_visit_started = std::chrono::steady_clock::now();
    melee_boot_records = 0;
    KeepZoneHostAlive host;
    if (!host.Disable()) { return false; }
    ObserveMeleeBootStage("visit-shutdown-start");
    zone->Shutdown();
    ObserveMeleeBootStage("visit-shutdown-end");
    const bool booted = !zone && !is_zone_loaded && Zone::Bootup(2, 0, melee_visit.is_static);
    ObserveMeleeBootStage("visit-boot-returned");
    if (booted && zone) {
        zone->SetZoneServerId(melee_visit.server_id);
        zone->quest_idle_override = true;
    }
    const bool restored = host.Restore();
    return booted && restored && zone && zone->GetZoneID() == 2 && worldserver.Connected();
}

void CleanupMelee()
{
    RestoreMeleeZone(); // fallback for registry timeout; never changes fixture/media
    actor_shutdown_armed = false;
}

Result FinishMelee(Result result)
{
    if (!RestoreMeleeZone()) { return {Outcome::Failed, "melee prior zone/host restoration failed"}; }
    return result;
}

// Both setup and replacement subjects use the sealed public native templates.
// Never alter the rat's stats, pathing, flee, control, HP or combat permission.
bool MeleeTemplateReady()
{
    const auto *t = content_db.LoadNPCTypesData(2011);
    return t && t->npc_id == 2011 && std::strcmp(t->name, "a_large_rat") == 0 &&
        t->level == 1 && t->maxlevel == 0 && t->class_ == 1 && t->race == 36 &&
        t->bodytype == 21 && t->max_hp == 11 && t->min_dmg == 1 && t->max_dmg == 6 &&
        !t->npc_spells_id && !t->npc_spells_effects_id && !t->merchanttype &&
        !t->is_quest_npc && !t->untargetable && !t->special_abilities[0];
}

// Geometry is observed before choosing a nearby setup position. Setup placement
// is not a movement sample; the victory case still requires actual local travel.
bool MeleeStart(NPC *rat, float distance, glm::vec3 &start)
{
    if (!zone->zonemap || !zone->pathing || !zone->watermap) { return false; }
    for (const auto &offset : {glm::vec2(distance, 0), glm::vec2(-distance, 0),
         glm::vec2(0, distance), glm::vec2(0, -distance)}) {
        glm::vec3 p(rat->GetX() + offset.x, rat->GetY() + offset.y, rat->GetZ() + 20);
        const auto floor = zone->zonemap->FindBestZ(p, nullptr);
        if (floor == BEST_Z_INVALID || !std::isfinite(floor)) { continue; }
        p.z = floor + 3.75f; // ordinary race-1, size-6 body; actual offset checked later
        if (std::abs(p.z - rat->GetZ()) > 6 || zone->watermap->InLiquid(p) ||
            !zone->zonemap->CheckLoS(p, glm::vec3(rat->GetPosition()))) { continue; }
        start = p;
        return true;
    }
    return false;
}

template<bool Cancel> Result ActorMelee()
{
    using namespace eqemu_proof;
    static unsigned step = 0, samples = 0;
    static uint32 began = 0, stage_at = 0, action_at = 0, last_at = 0, last_swing_at = 0;
    static uint32 min_cadence = 30000, stop_ms = 0;
    static uint16 rat_id = 0, city_id = 0;
    static uint64 rat_incarnation = 0, city_incarnation = 0, operation = 0, quiet_swings = 0;
    static int64 city_hp = 0, quiet_hp = 0, min_hp = 11;
    static glm::vec3 previous, origin, quiet;
    static float travelled = 0, drift = 0;
    static uint64 last_swings = 0, total_damage = 0, total_hits = 0, total_swings = 0;
    const auto now = Timer::GetCurrentTime();
    auto fail = [&](const char *why) -> Result {
        char text[256];
        float xy = -1, z = -1, rat_z = 0;
        if (zone && melee_visit.active) {
            auto *b = ActorBody::Get().Body();
            auto *t = entity_list.GetNPCByID(rat_id);
            if (b && t && t->NativeIncarnation() == rat_incarnation) {
                xy = std::hypot(b->GetX() - t->GetX(), b->GetY() - t->GetY());
                z = std::abs(b->GetZ() - t->GetZ());
                rat_z = t->GetZ();
            }
        }
        std::snprintf(text, sizeof(text), "melee %s s=%u ms=%u n=%u d=%.2f hp=%lld dmg=%llu swings=%llu cadence=%u stop=%u xy=%.2f dz=%.2f rat-z=%.2f",
            why, step, now - began, samples, travelled, static_cast<long long>(min_hp),
            static_cast<unsigned long long>(total_damage), static_cast<unsigned long long>(total_swings), min_cadence, stop_ms, xy, z, rat_z);
        return FinishMelee({Outcome::Failed, text});
    };
    if (!step) {
        began = now;
        if (!VisitMeleeZone()) { return fail("zone2 boot/world"); }
        step = 1;
        return {Outcome::Pending, ""};
    }
    if (!zone || zone->GetZoneID() != 2 || !worldserver.Connected() || !zone->CanDoCombat()) {
        return fail("zone2 combat/world");
    }
    if (now - began > 29000) { return fail("deadline"); }
    auto &adapter = ActorBody::Get();
    const auto state = adapter.Inspect();
    auto *body = adapter.Body();
    auto *rat = entity_list.GetNPCByID(rat_id);
    if (rat && rat->NativeIncarnation() != rat_incarnation) { rat = nullptr; }
    auto command = [&](Kind kind) {
        return adapter.Send({kind, state.epoch, state.generation, state.operation, 0, 0, 0});
    };
    if (step >= 3 && body) {
        const glm::vec3 p(body->GetPosition());
        const auto gap = now - last_at;
        const auto delta = glm::distance(p, previous);
        const float bound = gap * 0.001f * body->GetRunspeed() * 0.4f * 1.45f + 2;
        if (gap > 250 || delta > 8 || delta > bound || body->NativeDecisionCount()) {
            return fail("sampling/competing-AI");
        }
        travelled += delta;
        if (delta > 0.05f) { ++samples; }
        previous = p;
        last_at = now;
        if (state.swings > last_swings) {
            if (state.swings != last_swings + 1) { return fail("swing correlation"); }
            if (last_swing_at) {
                min_cadence = std::min(min_cadence, now - last_swing_at);
                if (now - last_swing_at < 1750) { return fail("native timer bypass"); }
            }
            last_swing_at = now;
        }
        last_swings = state.swings;
        total_swings = std::max(total_swings, state.swings);
        total_damage = std::max(total_damage, state.damage);
        total_hits = std::max(total_hits, state.hits);
    }
    if (rat) { min_hp = std::min(min_hp, rat->GetHP()); }
    if (city_id) {
        auto *city = entity_list.GetNPCByID(city_id);
        if (!city || city->NativeIncarnation() != city_incarnation || city->GetHP() != city_hp ||
            (body && (city->CheckAggro(body) || body->CheckAggro(city)))) {
            return fail("city damage/hate/lifetime");
        }
    }
    switch (step) {
    case 1: {
        // This legacy lookup takes a spawn GROUP, despite its name. Check the
        // dedicated spawn2 identity independently from group/type/runtime ID.
        rat = entity_list.GetNPCBySpawnID(7678);
        if (!rat) { if (now - began > 3000) { return fail("native spawn10863 absent"); } break; }
        if (!MeleeTemplateReady() || rat->GetNPCTypeID() != 2011 || rat->GetSpawnGroupId() != 7678 ||
            rat->GetSpawnPointID() != 10863 ||
            rat->GetHP() != 11 || rat->GetOwnerID() || rat->IsEngaged()) { return fail("sealed rat identity/stats"); }
        rat_id = rat->GetID();
        rat_incarnation = rat->NativeIncarnation();
        glm::vec3 start;
        if (!MeleeStart(rat, Cancel ? 3.0f : 18.0f, start)) { return fail("map/nav/water/floor/LOS"); }
        if (adapter.Send({Kind::CreateMeleeBody, state.epoch, state.generation, 0,
            static_cast<int32>(std::lround(start.x)), static_cast<int32>(std::lround(start.y)),
            static_cast<int32>(std::lround(start.z))}).event != Event::BodyRequested) { return fail("body request"); }
        step = 2;
        break;
    }
    case 2: {
        if (state.body != Body::Present) { break; }
        if (!body || !rat || !body->IsTargetable() || body->GetBodyType() != 1 ||
            std::abs(body->GetZOffset() - 3.75f) > 0.05f || !body->CheckLosFN(rat)) { return fail("actual body/reach setup"); }
        origin = previous = glm::vec3(body->GetPosition());
        last_at = now;
        if constexpr (!Cancel) {
            const auto *type = content_db.LoadNPCTypesData(2030);
            if (!type || type->npc_id != 2030 || !type->is_quest_npc || type->class_ != 7) { return fail("public city template"); }
            glm::vec3 p(origin.x + 4, origin.y, origin.z + 20);
            const auto floor = zone->zonemap->FindBestZ(p, nullptr);
            if (floor == BEST_Z_INVALID || !std::isfinite(floor)) { return fail("city floor"); }
            auto *city = new NPC(type, nullptr, glm::vec4(p.x, p.y, floor + 3.75f, 0), GravityBehavior::Ground);
            entity_list.AddNPC(city, false);
            city_id = city->GetID();
            city_incarnation = city->NativeIncarnation();
            city_hp = city->GetHP();
            if (glm::distance(glm::vec3(city->GetPosition()), origin) >=
                glm::distance(glm::vec3(rat->GetPosition()), origin)) { return fail("city not closer"); }
        }
        const auto report = city_id ? adapter.StartMelee({city_id, rat_id}) : adapter.StartMelee({rat_id});
        if (report.status != Status::Ok) { return fail("opt-in"); }
        step = 3;
        break;
    }
    case 3:
        if (state.target && state.target != rat_incarnation) { return fail("Rust prey selection"); }
        if (state.approach == Approach::Paused || state.approach == Approach::Lost || state.approach == Approach::Defeated) {
            return fail("native refusal/range/flee/defeat");
        }
        if constexpr (!Cancel) {
            if (state.approach != Approach::Victory) { break; }
            if (!samples || travelled < 2 || state.last_terminal != Event::TargetKilled || !total_damage || !total_hits ||
                state.swings < 2 || min_hp >= 11 || (rat && rat->GetHP() > 0) || state.phase != Phase::Idle) {
                return fail("travel/damage/natural-death correlation");
            }
            quiet = previous;
            quiet_swings = state.swings;
            stage_at = now;
            step = 4;
        } else {
            if (state.approach == Approach::Victory) { return fail("natural kill before interruption window"); }
            if (!state.attacking || state.phase != Phase::Admitted || !state.swings || !rat) { break; }
            operation = state.operation;
            quiet_swings = state.swings;
            quiet_hp = rat->GetHP();
            quiet = previous;
            action_at = now;
            stage_at = 0;
            if (command(Kind::Pause).event != Event::StopRequested) { return fail("combat pause"); }
            step = 4;
        }
        break;
    case 4:
        drift = std::max(drift, glm::distance(previous, quiet));
        if (!body || body->IsMoving() || drift > 0.05f || state.swings != quiet_swings) { return fail("quiet attacks/drift"); }
        if constexpr (!Cancel) {
            if (state.approach != Approach::Victory || state.operation || state.last_terminal != Event::TargetKilled) { return fail("victory not latched"); }
            if (now - stage_at < 400) { break; }
            if (rat) { return fail("natural death did not drain NPC"); }
            if (command(Kind::Retire).status != Status::Ok) { return fail("victory retire"); }
            step = 12;
        } else {
            if (now - action_at > 1000 && state.phase != Phase::Idle) { return fail("pause stop budget"); }
            if (state.phase != Phase::Idle) { break; }
            if (!rat || rat->GetHP() < quiet_hp || state.approach != Approach::Paused || state.last_operation != operation) { return fail("pause damage/terminal"); }
            if (!stage_at) { stage_at = now; stop_ms = std::max(stop_ms, now - action_at); }
            if (now - stage_at < 400) { break; }
            if (command(Kind::Resume).status != Status::Ok) { return fail("explicit resume"); }
            last_swings = 0;
            last_swing_at = 0;
            step = 5;
        }
        break;
    case 5:
        if (state.approach == Approach::Paused) { return fail("resume admission"); }
        if (!state.attacking || state.phase != Phase::Admitted) { break; }
        body->Stun(1500); // native status interruption, not a synthesized result
        quiet = previous;
        quiet_swings = state.swings;
        quiet_hp = rat ? rat->GetHP() : 0;
        operation = state.operation;
        action_at = now;
        stage_at = 0;
        step = 6;
        break;
    case 6:
        if (now - action_at > 1000 && state.phase != Phase::Idle) { return fail("control stop budget"); }
        drift = std::max(drift, glm::distance(previous, quiet));
        if (!body || !rat || drift > 0.05f || rat->GetHP() < quiet_hp || state.swings != quiet_swings) { return fail("control quiet damage/drift"); }
        if (state.phase != Phase::Idle) { break; }
        if (state.approach != Approach::Paused || state.last_terminal != Event::AttackInterrupted || state.last_operation != operation) { return fail("control terminal"); }
        if (!stage_at) { stage_at = now; stop_ms = std::max(stop_ms, now - action_at); }
        if (now - stage_at < 1900 || body->IsStunned()) { break; }
        if (command(Kind::Resume).status != Status::Ok) { return fail("control explicit resume"); }
        last_swings = 0;
        last_swing_at = 0;
        step = 7;
        break;
    case 7:
        if (!rat || state.approach == Approach::Paused) { return fail("loss setup"); }
        if (!state.attacking || state.phase != Phase::Admitted) { break; }
        operation = state.operation;
        quiet_swings = state.swings;
        quiet = previous;
        rat->Depop(false); // external removal is never a native victory
        action_at = now;
        step = 8;
        break;
    case 8: {
        if (now - action_at > 1000 && state.phase != Phase::Idle) { return fail("loss stop budget"); }
        if (entity_list.GetMob(rat_id) || state.phase != Phase::Idle) { break; }
        stop_ms = std::max(stop_ms, now - action_at);
        if (!body || state.approach != Approach::Lost || state.last_terminal != Event::TargetLost || state.last_operation != operation) { return fail("external removal counted as kill"); }
        const auto *type = content_db.LoadNPCTypesData(2011);
        if (!type) { return fail("reuse public template"); }
        if (!entity_list.ReuseFreeIDForTest(rat_id)) { return fail("reuse allocation"); }
        auto *replacement = new NPC(type, nullptr, glm::vec4(previous.x + 3, previous.y, previous.z, 0), GravityBehavior::Ground);
        entity_list.AddNPC(replacement, false);
        if (replacement->GetID() != rat_id || replacement->NativeIncarnation() == rat_incarnation) { return fail("reuse identity"); }
        rat_incarnation = replacement->NativeIncarnation();
        quiet_hp = replacement->GetHP();
        stage_at = now;
        step = 9;
        break;
    }
    case 9:
        drift = std::max(drift, glm::distance(previous, quiet));
        if (!body || !rat || drift > 0.05f || rat->GetHP() != quiet_hp || rat->CheckAggro(body) || body->CheckAggro(rat) ||
            state.approach != Approach::Lost || state.swings != quiet_swings || state.operation ||
            command(Kind::Resume).status != Status::Busy) { return fail("reused ID inherited authority"); }
        if (now - stage_at < 400) { break; }
        if (command(Kind::Retire).status != Status::Ok) { return fail("loss retirement"); }
        step = 10;
        break;
    case 10: {
        if (state.alive) { break; }
        ActorBody::FinishTeardown();
        auto &fresh = ActorBody::Get();
        const auto s = fresh.Inspect();
        glm::vec3 start;
        if (!rat || !MeleeStart(rat, 3, start) || fresh.Send({Kind::CreateMeleeBody, s.epoch, s.generation, 0,
            static_cast<int32>(std::lround(start.x)), static_cast<int32>(std::lround(start.y)),
            static_cast<int32>(std::lround(start.z))}).status != Status::Ok) { return fail("fresh teardown body"); }
        last_swings = 0;
        last_swing_at = 0;
        // Initial placement of a new body is not displacement by the old body.
        previous = glm::vec3(fresh.Body()->GetPosition());
        last_at = now;
        step = 11;
        break;
    }
    case 11:
        if (state.body != Body::Present) { break; }
        if (state.approach == Approach::Disabled) {
            if (adapter.StartMelee({rat_id}).status != Status::Ok) { return fail("fresh teardown goal"); }
            break;
        }
        if (!state.attacking || state.phase != Phase::Admitted) { break; }
        actor_shutdown_id = adapter.NativeID();
        actor_shutdown_epoch = state.epoch;
        actor_shutdown_generation = state.generation;
        actor_shutdown_stages = 0;
        actor_shutdown_valid = true;
        actor_shutdown_armed = true;
        // Actual Zone::Shutdown while combat is admitted; hooks establish closed
        // admission, native absence, then Rust closure. Restore boots prior zone.
        if (!RestoreMeleeZone()) { return fail("active combat teardown/restore"); }
        actor_shutdown_armed = false;
        if (!actor_shutdown_valid || actor_shutdown_stages != 3) { return fail("active combat cleanup ordering"); }
        {
            char text[220];
            std::snprintf(text, sizeof(text), "melee pause/control quiet>=400/1900ms resume loss/reuse quiet>=400ms stop=%ums drift=%.3f active-zone-teardown closed/absent prior-zone-restored",
                stop_ms, drift);
            return {Outcome::Passed, text};
        }
    case 12:
        if (state.alive) { break; }
        if (body || state.body != Body::Absent || state.last_terminal != Event::TargetKilled) { return fail("retirement absence/terminal"); }
        {
            char text[256];
            std::snprintf(text, sizeof(text), "melee start=%.1f,%.1f,%.1f n=%u d=%.2f hp=%lld damage=%llu hits=%llu swings=%llu cadence=%ums quiet=400ms city-safe natural-kill retired/restored",
                origin.x, origin.y, origin.z, samples, travelled, static_cast<long long>(min_hp), static_cast<unsigned long long>(total_damage),
                static_cast<unsigned long long>(total_hits), static_cast<unsigned long long>(total_swings), min_cadence);
            return FinishMelee({Outcome::Passed, text});
        }
    default: return fail("invalid stage");
    }
    return {Outcome::Pending, ""};
}

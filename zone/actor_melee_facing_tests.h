// Real native Attack/FaceTarget/movement-queue regression. Setup subjects use
// the admitted public template; only setup placement/heading is assigned.
Result PollMeleeRestoration()
{
    if (melee_visit.boot_pending) { return {Outcome::Pending, ""}; }
    if (melee_visit.active || melee_visit.boot_failed) {
        return {Outcome::Failed, "melee prior zone/host restoration failed"};
    }
    auto result = melee_visit.result;
    result.diagnostic += " prior-zone-restored";
    return result;
}

enum class FacingCase { Timed, Lost, Control, Unowned };

template<FacingCase Mode> Result ActorMeleeFacing()
{
    using namespace eqemu_proof;
    using Motion = MobMovementManager::ActorCombatMotion;
    if (melee_visit.finishing) { return PollMeleeRestoration(); }
    static unsigned step = 0;
    static uint16 original_id = 0, target_id = 0;
    static uint64 target_incarnation = 0, quiet_swings = 0, quiet_damage = 0, operation = 0;
    static uint32 began = 0, first_swing_at = 0, action_at = 0, quiet_at = 0, stop_ms = 0;
    static glm::vec3 target_position, origin, settled;
    static float settled_heading = 0;
    static bool turning_seen = false, terminal_seen = false;
    static Event expected = Event::None;
    const auto now = Timer::GetCurrentTime();
    auto fail = [&](const char *why) { return FinishMelee({Outcome::Failed, std::string("facing ") + why}); };
    if (!step) {
        began = now;
        if (!VisitMeleeZone()) { return fail("visit"); }
        step = 1;
        return {Outcome::Pending, ""};
    }
    if (!zone || zone->GetZoneID() != 2 || !worldserver.Connected() || !zone->CanDoCombat()) { return fail("zone/world"); }
    if (now - began > 29000) { return fail("deadline"); }
    auto &adapter = ActorBody::Get();
    const auto state = adapter.Inspect();
    auto *body = adapter.Body();
    auto *target = entity_list.GetNPCByID(target_id);
    if (target && target->NativeIncarnation() != target_incarnation) { target = nullptr; }
    auto &movement = MobMovementManager::Get();
    if (step >= 5 && body && glm::distance(glm::vec3(body->GetPosition()), origin) > 0.05f) {
        return fail("in-range displacement");
    }
    switch (step) {
    case 1: {
        auto *public_rat = entity_list.GetNPCBySpawnID(7678);
        if (!public_rat) { if (now - began > 3000) { return fail("public spawn absent"); } break; }
        if (!MeleeTemplateReady() || public_rat->GetNPCTypeID() != 2011 ||
            public_rat->GetSpawnPointID() != 10863 || public_rat->GetHP() != 11 || public_rat->IsEngaged()) {
            return fail("public template/spawn");
        }
        target_position = glm::vec3(public_rat->GetPosition());
        original_id = public_rat->GetID();
        public_rat->Depop(false); // isolate the owned subject, without a death timer
        step = 2;
        break;
    }
    case 2: {
        if (entity_list.GetMob(original_id)) { break; }
        const auto *type = content_db.LoadNPCTypesData(2011);
        if (!type) { return fail("template"); }
        auto *subject = new NPC(type, nullptr, glm::vec4(target_position, 0), GravityBehavior::Ground);
        entity_list.AddNPC(subject, false);
        target_id = subject->GetID();
        target_incarnation = subject->NativeIncarnation();
        step = 3;
        break;
    }
    case 3: {
        glm::vec3 start;
        if (!target || !MeleeStart(target, 3, start) || adapter.Send({Kind::CreateMeleeBody,
            state.epoch, state.generation, 0, static_cast<int32>(std::lround(start.x)),
            static_cast<int32>(std::lround(start.y)), static_cast<int32>(std::lround(start.z))}).status != Status::Ok) {
            return fail("body setup");
        }
        step = 4;
        break;
    }
    case 4:
        if (state.body != Body::Present) { break; }
        if (!body || !target) { return fail("body/target absent"); }
        origin = glm::vec3(body->GetPosition());
        // Initial setup faces directly away, before any goal/native motion.
        body->SetHeading(movement.FixHeading(body->CalculateHeadingToTarget(target->GetX(), target->GetY()) + 256));
        if constexpr (Mode == FacingCase::Unowned) {
            body->RotateToWalking(movement.FixHeading(body->GetHeading() + 128));
            if (movement.InspectActorCombatMotion(body) != Motion::Unsupported) { return fail("unowned turn admitted"); }
        }
        if (adapter.StartMelee({target_id}).status != Status::Ok) { return fail("goal"); }
        step = 5;
        break;
    case 5:
        if constexpr (Mode == FacingCase::Unowned) {
            if (state.phase != Phase::Idle || state.approach != Approach::Paused) { break; }
            if (state.swings || state.damage || state.last_terminal != Event::AttackRefused) { return fail("unowned turn not refused"); }
            expected = Event::AttackRefused;
        } else {
            if (state.approach == Approach::Paused || state.approach == Approach::Lost || state.approach == Approach::Defeated) {
                return fail("self-interruption before action");
            }
            if (!state.swings) { break; }
            if (!first_swing_at) {
                if (movement.InspectActorCombatMotion(body) != Motion::Facing) { return fail("native turn not queued by swing"); }
                first_swing_at = now;
            }
            if constexpr (Mode == FacingCase::Timed) {
                turning_seen |= body && body->IsMoving() && movement.InspectActorCombatMotion(body) == Motion::Facing;
                if (state.swings < 2) { break; }
                if (!turning_seen || now - first_swing_at < 1750) { return fail("turn/timed swing evidence"); }
                expected = !target || target->GetHP() <= 0 ? Event::TargetKilled : Event::AttackInterrupted;
                if (expected == Event::AttackInterrupted && adapter.Send({Kind::Pause, state.epoch,
                    state.generation, state.operation, 0, 0, 0}).event != Event::StopRequested) { return fail("pause"); }
            } else {
                // Interrupt in the same test advance as the first actual swing,
                // before its queued facing command gets a movement-manager tick.
                if (!target || movement.InspectActorCombatMotion(body) != Motion::Facing) { return fail("interruption turn setup"); }
                if constexpr (Mode == FacingCase::Lost) {
                    target->Depop(false);
                    expected = Event::TargetLost;
                } else {
                    body->Stun(1500);
                    expected = Event::AttackInterrupted;
                }
            }
        }
        quiet_swings = state.swings;
        quiet_damage = state.damage;
        operation = state.operation;
        action_at = now;
        step = 6;
        break;
    case 6:
        if (!body || state.swings != quiet_swings || state.damage != quiet_damage) { return fail("terminal extra attack/absence"); }
        if (state.phase != Phase::Idle) {
            if (now - action_at > 1000) { return fail("terminal fact deadline"); }
            break;
        }
        if (state.last_terminal != expected || !state.last_operation || (operation && state.last_operation != operation)) { return fail("terminal identity/outcome"); }
        if (!terminal_seen) {
            terminal_seen = true;
            settled = glm::vec3(body->GetPosition());
            settled_heading = body->GetHeading();
        }
        if (glm::distance(glm::vec3(body->GetPosition()), settled) > 0.05f || body->GetHeading() != settled_heading) {
            return fail("post-terminal position/heading drift");
        }
        if (body->IsMoving() || movement.InspectActorCombatMotion(body) != Motion::Stopped) {
            if (quiet_at || now - action_at > 1000) { return fail("terminal motion not drained"); }
            break;
        }
        if (!quiet_at) {
            quiet_at = now;
            stop_ms = now - action_at;
            if constexpr (Mode == FacingCase::Lost) {
                if (entity_list.GetMob(target_id)) { quiet_at = 0; break; }
                if (!entity_list.ReuseFreeIDForTest(target_id)) { return fail("reuse allocation"); }
                auto *replacement = new NPC(content_db.LoadNPCTypesData(2011), nullptr,
                    glm::vec4(target_position, 0), GravityBehavior::Ground);
                entity_list.AddNPC(replacement, false);
                if (replacement->GetID() != target_id || replacement->NativeIncarnation() == target_incarnation) { return fail("reuse identity"); }
            }
        }
        if constexpr (Mode == FacingCase::Lost) {
            auto *replacement = entity_list.GetNPCByID(target_id);
            if (!replacement || replacement->GetHP() != 11 || replacement->CheckAggro(body) || body->CheckAggro(replacement) ||
                adapter.Send({Kind::Resume, state.epoch, state.generation, 0, 0, 0, 0}).status != Status::Busy) { return fail("reused target authority"); }
        }
        if (now - quiet_at < (Mode == FacingCase::Control ? 1900u : 400u)) { break; }
        return FinishMelee({Outcome::Passed, "in-range facing-away mode=" + std::to_string(static_cast<unsigned>(Mode)) +
            " native-queue/terminal quiet; stop=" + std::to_string(stop_ms) + "ms swings=" + std::to_string(quiet_swings)});
    default: return fail("stage");
    }
    return {Outcome::Pending, ""};
}

// Included by the in-server registry after its existing zone-visit helpers.
Result ActorEquipment()
{
    if (melee_visit.finishing) { return PollMeleeRestoration(); }
    static unsigned step = 0;
    auto fail = [](const char *message) { return FinishMelee({Outcome::Failed, message}); };
    if (!step) {
        if (!VisitMeleeZone()) { return fail("equipment zone visit refused"); }
        step = 1;
        return {Outcome::Pending, ""};
    }
    if (!zone || zone->GetZoneID() != 2) { return fail("equipment zone unavailable"); }
    auto &adapter = ActorBody::PopulationAdapter(0);
    if (step == 1) {
        if (!adapter.RestorePopulationBody(0, -917, 1224, 3.75, 100, 1)) {
            return fail("equipment population body restore refused");
        }
        step = 2;
        return {Outcome::Pending, ""};
    }
    auto *body = adapter.Body();
    if (!body || adapter.Inspect().body != eqemu_proof::Body::Present) { return {Outcome::Pending, ""}; }
    try {
        auto &inventory = body->GetInv();
        inventory.SetInventoryVersion(EQ::versions::MobVersion::RoF2);
        const auto baseline_ac = body->ACSum();
        std::unique_ptr<EQ::ItemInstance> cap(database.CreateItem(1001));
        std::unique_ptr<EQ::ItemInstance> sword(database.CreateItem(7007));
        std::unique_ptr<EQ::ItemInstance> bag(database.CreateItem(17005));
        if (!cap || !sword || !bag || !sword->IsWeapon() || !bag->IsClassBag() || bag->GetItem()->BagSlots < 3) {
            return fail("equipment cloth cap/rusty sword/backpack fixtures unavailable");
        }
        cap->SetCustomDataString("actor^roundtrip^");
        cap->SetColor(0x123456);
        cap->SetAttuned(true);
        bag->PutItem(2, *cap);
        inventory.PutItem(EQ::invslot::slotHead, *cap);
        inventory.PutItem(EQ::invslot::slotPrimary, *sword);
        inventory.PutItem(EQ::invslot::GENERAL_BEGIN, *bag);
        inventory.PushCursor(*sword);
        // Distinct instances must have distinct native serials in a checkpoint.
        inventory.GetItem(EQ::invslot::slotHead)->SetSerialNumber(1000001);
        inventory.GetItem(EQ::invslot::slotPrimary)->SetSerialNumber(1000002);
        inventory.GetItem(EQ::invslot::GENERAL_BEGIN)->SetSerialNumber(1000003);
        inventory.GetItem(EQ::invslot::GENERAL_BEGIN, 2)->SetSerialNumber(1000004);
        inventory.GetCursorItem()->SetSerialNumber(1000005);
        body->RefreshActorEquipment();
        body->CalcBonuses();
        const auto before = ActorInventory::Snapshot(*body);
        auto *type = new NPCType{};
        std::strcpy(type->name, "EquipmentControl");
        type->race = 1; type->class_ = 1; type->level = 1;
        type->current_hp = type->max_hp = 100;
        auto other = std::make_unique<NPC>(type, nullptr, glm::vec4(0), GravityBehavior::Flying);
        other->GiveNPCTypeData(type);
        ActorInventory::Restore(*other, before);
        if (ActorInventory::Snapshot(*other) != before || body->ACSum() <= baseline_ac ||
            body->GetWeaponDamage(other.get(), inventory.GetItem(EQ::invslot::slotPrimary)) <= 0 ||
            body->GetAttackTimer().GetSetAtTrigger() != static_cast<uint32>(std::max(RuleI(Combat, MinHastedDelay), 100 * sword->GetItem()->Delay))) {
            return fail("equipment snapshot/armor/weapon delay effects changed");
        }
        bool long_route = false;
        for (const auto &offset : {glm::vec2(100,0), glm::vec2(-100,0), glm::vec2(0,100), glm::vec2(0,-100)}) {
            glm::vec3 point(body->GetX()+offset.x, body->GetY()+offset.y, body->GetZ()+20);
            const auto floor = zone->zonemap->FindBestZ(point, nullptr);
            if (!std::isfinite(floor) || floor == BEST_Z_INVALID) { continue; }
            point.z = floor + body->GetZOffset();
            auto &movement = MobMovementManager::Get();
            if (movement.CanNavigateActorTo(body, point.x, point.y, point.z, MobMovementManager::ActorRoutePurpose::Transfer)) {
                if (movement.CanNavigateActorTo(body, point.x, point.y, point.z, false)) { return fail("transfer widened proof route limit"); }
                long_route = true;
                break;
            }
        }
        if (!long_route) { return fail("no actual transfer navmesh route beyond proof80 admitted"); }
        const auto hp = other->GetHP();
        if (body->Attack(other.get()) || body->Mob::Attack(other.get()) || other->GetHP() != hp) {
            return fail("equipment path admitted unauthorized actor swing");
        }
        auto bad = nlohmann::json::parse(before);
        bad[0]["id"] = UINT32_MAX;
        bool refused = false;
        try { ActorInventory::Restore(*other, bad.dump()); }
        catch (const std::exception &) { refused = true; }
        if (!refused || ActorInventory::Snapshot(*other) != before) {
            return fail("missing item restored a partial inventory");
        }
        return FinishMelee({Outcome::Passed, "native carried/bag/cursor data, armor, weapon delay and unauthorized swing controls"});
    } catch (const std::exception &error) {
        return FinishMelee({Outcome::Failed, std::string("equipment exception: ") + error.what()});
    }
}

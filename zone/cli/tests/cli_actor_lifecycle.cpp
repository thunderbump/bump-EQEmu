/*	EQEmu: EQEmulator

	Copyright (C) 2001-2026 EQEmu Development Team

	This program is free software; you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation; either version 3 of the License, or
	(at your option) any later version.

	This program is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
	GNU General Public License for more details.

	You should have received a copy of the GNU General Public License
	along with this program. If not, see <http://www.gnu.org/licenses/>.
*/
#include "actor_lifecycle_scenario.h"
#include "zone/actor_lifecycle.h"
#include "zone/entity.h"
#include "zone/npc.h"
#include "zone/mob_movement_manager.h"
#include "zone/questmgr.h"
#include "zone/zone.h"
#include "common/json/json.hpp"
#include "common/repositories/zone_state_spawns_repository.h"
#include "common/rulesys.h"
#include "common/timer.h"
#include <algorithm>
#include <chrono>
#include <cstring>
#include <iostream>
#include <queue>
#include <set>
#include <stdexcept>
#include <thread>

extern Zone *zone;

// Scoped scenario-only FIFO prioritization, not a production allocator selector.
// Restore all still-free IDs in their original order, preserving IDs freed inside the scope.
class ActorIdReuseScope {
public:
	explicit ActorIdReuseScope(uint16 id) : original(entity_list.free_ids)
	{
		if (entity_list.GetID(id)) throw std::runtime_error("reuse ID still registered");
		auto queue = original;
		std::queue<uint16> prioritized;
		prioritized.push(id);
		bool found = false;
		while (!queue.empty()) { auto next = queue.front(); queue.pop(); if (next == id) found = true; else prioritized.push(next); }
		if (!found) throw std::runtime_error("retired ID was not actually freed");
		entity_list.free_ids.swap(prioritized);
	}
	~ActorIdReuseScope()
	{
		auto current = entity_list.free_ids;
		std::set<uint16> remaining;
		while (!current.empty()) { remaining.insert(current.front()); current.pop(); }
		std::queue<uint16> restored;
		while (!original.empty()) { auto id = original.front(); original.pop(); if (remaining.erase(id)) restored.push(id); }
		current = entity_list.free_ids;
		while (!current.empty()) { auto id = current.front(); current.pop(); if (remaining.erase(id)) restored.push(id); }
		entity_list.free_ids.swap(restored);
	}
private:
	std::queue<uint16> original;
};

// Backing diagnostics belong only to this native scenario, outside the public actor Snapshot.
class ActorLifecycleScenario {
public:
	static uint64_t NativeTicks(const NPC *npc) { return npc->actor_native_ticks; }
};

namespace ActorScenario {
namespace {
using Clock = std::chrono::steady_clock;
struct Failure { bool assertion; const char *message; };
void Require(bool condition, const char *message) { if (!condition) throw Failure{true, message}; }
void Setup(bool condition, const char *message) { if (!condition) throw Failure{false, message}; }
void Tick(volatile std::sig_atomic_t &interrupted)
{
	if (interrupted) throw Failure{false, "cancelled"};
	Setup(zone && zone->IsLoaded(), "loaded fixture disappeared");
	Timer::SetCurrentTime();
	entity_list.Process(); entity_list.MobProcess();
}
NPC *ActorNPC(const Actors::Definition &definition)
{
	for (const auto &[id, npc] : entity_list.GetNPCList())
		if (std::string(npc->GetCleanName()) == "First Actor" && definition.key == "first-actor") return npc;
	return nullptr;
}
NPC *Ordinary(const glm::vec4 &position)
{
	// Fixed genuine row in the sealed PEQ fixture: Animation2, NPC type 501, default HP 46.
	const auto *type = content_db.LoadNPCTypesData(501);
	Setup(type && type->npc_id == 501 && type->max_hp == 46 && std::string(type->name) == "Animation2", "public positive NPC row missing or changed");
	auto npc = std::make_unique<NPC>(type, nullptr, position, GravityBehavior::Water);
	auto *value = npc.release(); entity_list.AddNPC(value, true, true); return value;
}
void Boot()
{
	Setup(!zone, "fixture zone already loaded");
	Setup(Zone::Bootup(202, 0, false) && zone && zone->IsLoaded(), "poknowledge fixture boot failed");
	zone->StopShutdownTimer(); zone->SetIdleWhenEmpty(false); zone->SetSaveZoneState(false);
	Setup(zone->HasMap() && zone->HasWaterMap(), "required public zone maps missing");
}
void Case(Result &r, const char *name)
{
	if (std::find(r.completed_cases.begin(), r.completed_cases.end(), name) == r.completed_cases.end()) r.completed_cases.emplace_back(name);
}
}


void Run(Result &r, volatile std::sig_atomic_t &interrupted)
{
	const auto start = Clock::now();
	std::vector<Actors::Handle> owned;
	double boot_seconds = 0;
	bool state_rule_restored = true;
	auto boot = [&] { const auto began = Clock::now(); Boot(); boot_seconds += std::chrono::duration<double>(Clock::now() - began).count(); };
	try {
		Setup(!interrupted, "cancelled");
		Setup(!RuleB(Zone, UseZoneController) && !RuleB(Bots, Enabled) && !RuleB(Zone, StateSavingOnShutdown), "disposable fixture rules missing");
		auto spawns = content_db.QueryDatabase("SELECT COUNT(*) FROM spawn2 WHERE zone='poknowledge'");
		Setup(spawns.Success() && spawns.RowCount() == 1 && std::string((*spawns.begin())[0]) == "0", "ambient fixture spawns not empty");
		auto state = database.QueryDatabase("SELECT COUNT(*) FROM zone_state_spawns WHERE zone_id=202 AND instance_id=0");
		Setup(state.Success() && state.RowCount() == 1 && std::string((*state.begin())[0]) == "0", "fixture starts with saved zone state");
		boot();
		const auto position = zone->GetSafePoint();
		auto definition = Actors::FirstDefinition(position.x, position.y, position.z, position.w);
		auto &actors = Actors::Lifecycle::Get();
		for (unsigned cycle = 0; cycle < 3; ++cycle) {
			auto created = actors.Create(definition);
			if (created.outcome == Actors::Outcome::Created) owned.push_back(created.handle);
			Require(created.outcome == Actors::Outcome::Created && created.snapshot.state == Actors::State::Live, "actor creation failed");
			Require(created.snapshot.display_name == "First Actor" && created.snapshot.race == definition.race &&
				created.snapshot.gender == definition.gender && created.snapshot.texture == definition.texture &&
				created.snapshot.class_id == definition.class_id && created.snapshot.level == definition.level, "configured actor presentation mismatch");
			auto *native = ActorNPC(definition);
			Require(native && entity_list.GetMob(native->GetID()) == native && native->GetNPCTypeID() == 0 && !native->GetSpawn(), "ordinary native actor indexes missing");
			if (cycle == 0 && r.control == Control::Assertion) throw Failure{true, "forced assertion after actor creation"};
			if (cycle == 0 && r.control == Control::Cancel) {
				std::cout << "EQEMU_ACTOR_PHASE {\"version\":1,\"phase\":\"actor-created\"}" << std::endl;
				const auto deadline = Clock::now() + std::chrono::seconds(60);
				while (!interrupted && Clock::now() < deadline) { Tick(interrupted); std::this_thread::sleep_for(std::chrono::milliseconds(10)); }
				throw Failure{false, interrupted ? "cancelled" : "cancellation control expired without signal"};
			}
			auto duplicate = actors.Create(definition);
			Require(duplicate.outcome == Actors::Outcome::Existing && duplicate.handle == created.handle && entity_list.GetNPCList().size() == 1, "duplicate actor registered twice");
			auto conflict = definition; ++conflict.level;
			Require(actors.Create(conflict).outcome == Actors::Outcome::Conflict && actors.Inspect(created.handle).level == definition.level, "conflicting definition changed original");
			Case(r, "create-duplicate");
			NewSpawn_Struct spawn{}; native->FillSpawnStruct(&spawn, nullptr);
			Require(spawn.spawn.race == definition.race && spawn.spawn.gender == definition.gender && spawn.spawn.level == definition.level, "native spawn presentation mismatch");
			const auto before = ActorLifecycleScenario::NativeTicks(native);
			native->Stun(1); std::this_thread::sleep_for(std::chrono::milliseconds(5)); Tick(interrupted);
			const auto after = ActorLifecycleScenario::NativeTicks(native);
			Require(after > before && !native->IsStunned(), "native NPC processing did not advance"); r.ticks += after - before;
			Case(r, "native-processing");
			const auto retired_id = native->GetID();
			auto *observer = Ordinary(position);
			observer->SetTarget(native); observer->AddToHateList(native, 1, 0, false);
			entity_list.ScanCloseMobs(observer);
			quest_manager.settimerMS("actor-owned-timer", 60000, native);
			Require(observer->GetTarget() == native && observer->CheckAggro(native) &&
				!quest_manager.GetTimers(native).empty() && MobMovementManager::Get().IsRegistered(native) &&
				entity_list.GetCloseMobList(observer).count(retired_id) == 1, "native owned references were not established");
			Require(actors.Retire(created.handle).outcome == Actors::Outcome::RetirementRequested && actors.Inspect(created.handle).state == Actors::State::Retiring, "retirement not requested");
			Require(actors.Create(definition).outcome == Actors::Outcome::Busy, "retiring key accepted recreation");
			actors.Retire(created.handle); Tick(interrupted);
			Require(!entity_list.GetMob(retired_id) && !entity_list.GetNPCByID(retired_id) && actors.Inspect(created.handle).state == Actors::State::Absent, "native retirement incomplete");
			// Pointer value is used only for read-only membership comparisons after deletion, never dereferenced.
			Require(!observer->GetTarget() && !observer->CheckAggro(native) && quest_manager.GetTimers(native).empty() &&
				!MobMovementManager::Get().IsRegistered(native) && !entity_list.GetCloseMobList(observer).count(retired_id), "native removal left owned references");
			observer->Depop(false); Tick(interrupted);
			Require(actors.Retire(created.handle).outcome == Actors::Outcome::Retired, "repeat retirement not harmless");
			auto replacement = actors.Create(definition);
			if (replacement.outcome == Actors::Outcome::Created) owned.push_back(replacement.handle);
			Require(replacement.outcome == Actors::Outcome::Created && replacement.handle != created.handle, "new incarnation missing");
			actors.Retire(created.handle);
			Require(actors.Inspect(replacement.handle).state == Actors::State::Live, "stale handle affected replacement");
			Case(r, "retire-recreate");
			const auto external_id = ActorNPC(definition)->GetID();
			ActorNPC(definition)->Depop(false); Tick(interrupted);
			Require(!entity_list.GetMob(external_id) && actors.Inspect(replacement.handle).state == Actors::State::Absent, "external native removal not reconciled");
			uint16 unrelated_id;
			{
				ActorIdReuseScope reuse(external_id);
				auto *unrelated = Ordinary(position); unrelated_id = unrelated->GetID();
				Require(unrelated_id == external_id, "real native entity ID not reused");
				actors.Retire(replacement.handle);
				Require(entity_list.GetMob(unrelated_id) == unrelated && actors.Inspect(replacement.handle).state == Actors::State::Absent, "stale handle affected unrelated recycled entity");
			}
			r.id_reuse = true;
			Case(r, "external-removal-id-reuse");
			// Native clean display-name collision with the genuine unrelated NPC, before actor construction.
			auto collision = definition; collision.key = "collision"; collision.display_name = "Animation";
			const auto count = entity_list.GetNPCList().size();
			Require(actors.Create(collision).outcome == Actors::Outcome::Refused && entity_list.GetNPCList().size() == count && entity_list.GetMob(unrelated_id), "name collision removed unrelated NPC");
			Case(r, "name-collision");
			entity_list.GetNPCByID(unrelated_id)->Depop(false); Tick(interrupted);
			Require(entity_list.GetNPCList().empty(), "cycle native cleanup incomplete");
			++r.cycles;
		}
		// Scope state saving to this proof only. Rebooting this same zone retains its already-active ruleset;
		// verify after boot as well, so an unexpected native rules reload cannot masquerade as restore.
		const int proof_ruleset = RuleManager::Instance()->GetActiveRulesetID();
		StateSavingScope saving([](bool enabled) {
			return RuleManager::Instance()->SetRule("Zone:StateSavingOnShutdown", enabled ? "true" : "false");
		}, RuleB(Zone, StateSavingOnShutdown), state_rule_restored);
		// Save one real ordinary NPC with deliberately nondefault positive HP and a persisted value.
		auto created = actors.Create(definition);
		if (created.outcome == Actors::Outcome::Created) owned.push_back(created.handle);
		Require(created.outcome == Actors::Outcome::Created, "restart actor creation failed");
		ActorNPC(definition)->SetHP(7);
		auto *ordinary = Ordinary(position);
		ordinary->SetHP(23); ordinary->SetEntityVariable("actor_scenario_positive", "saved");
		zone->SaveZoneState();
		auto saved = ZoneStateSpawnsRepository::GetWhere(database, "zone_id=202 AND instance_id=0");
		Require(saved.size() == 1 && saved[0].npc_id == 501 && saved[0].hp == 23 && saved[0].spawn2_id == 0 && saved[0].entity_variables.find("actor_scenario_positive") != std::string::npos, "ordinary save positive missing or actor persisted");
		const auto reboot_shutdown = Clock::now();
		zone->Shutdown(true);
		r.shutdown += std::chrono::duration<double>(Clock::now() - reboot_shutdown).count();
		Setup(!zone, "native reboot shutdown failed"); boot();
		Setup(RuleManager::Instance()->GetActiveRulesetID() == proof_ruleset && RuleB(Zone, StateSavingOnShutdown), "native reboot changed state-saving proof rules");
		Require(actors.Inspect(created.handle).state == Actors::State::Absent && !ActorNPC(definition), "actor restored across zone lifetime");
		Require(entity_list.GetNPCList().size() == 1, "ordinary saved NPC not restored");
		ordinary = entity_list.GetNPCList().begin()->second;
		Require(ordinary->GetNPCTypeID() == 501 && ordinary->GetHP() == 23 && ordinary->GetEntityVariable("actor_scenario_positive") == "saved", "ordinary state not genuinely restored");
		auto restarted = actors.Create(definition);
		if (restarted.outcome == Actors::Outcome::Created) owned.push_back(restarted.handle);
		Require(restarted.outcome == Actors::Outcome::Created && restarted.handle != created.handle && restarted.snapshot.definition == definition && restarted.snapshot.health == 100 && ActorLifecycleScenario::NativeTicks(ActorNPC(definition)) == 0, "fresh actor restored progress or lost identity");
		actors.Retire(created.handle); Require(actors.Inspect(restarted.handle).state == Actors::State::Live, "old zone handle affected fresh actor");
		r.save_restore = true; Case(r, "save-fresh-zone"); r.status = "passed";
	} catch (const Failure &failure) {
		std::cerr << "Actor scenario: " << failure.message << std::endl;
		r.status = interrupted ? "cancelled" : failure.assertion ? "assertion-failed" : "refused";
	} catch (const std::exception &failure) {
		std::cerr << "Actor scenario infrastructure: " << failure.what() << std::endl; r.status = "refused";
	}
	if (!state_rule_restored || RuleB(Zone, StateSavingOnShutdown)) {
		std::cerr << "Actor state-saving rule restoration refused" << std::endl; r.status = "refused";
	}
	// Also run normal retirement after an assertion or cancellation. Final zone teardown stays in main.
	try {
		auto &actors = Actors::Lifecycle::Get();
		for (auto handle : owned) actors.Retire(handle);
		if (zone && zone->IsLoaded()) { Timer::SetCurrentTime(); entity_list.MobProcess(); }
		for (auto handle : owned) if (actors.Inspect(handle).state != Actors::State::Absent) throw std::runtime_error("actor cleanup still pending");
	} catch (const std::exception &error) {
		std::cerr << "Actor lifecycle cleanup refused: " << error.what() << std::endl;
		r.status = "refused";
	}
	r.boot += boot_seconds;
	r.processing = std::max(0.0, std::chrono::duration<double>(Clock::now() - start).count() - boot_seconds - r.shutdown);
}
}

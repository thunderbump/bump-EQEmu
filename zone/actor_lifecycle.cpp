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
#include "actor_lifecycle.h"

#include "zone/entity.h"
#include "zone/npc.h"
#include "zone/zone.h"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>
#include <unordered_map>

extern Zone *zone;

namespace Actors {
namespace {
std::string CleanName(std::string name)
{
	name.erase(std::remove_if(name.begin(), name.end(), [](unsigned char c) { return std::isdigit(c); }), name.end());
	for (char &c : name) c = c == '_' ? ' ' : static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
	return name;
}
bool Valid(const Definition &d)
{
	return !d.key.empty() && d.key.size() <= 64 && !d.display_name.empty() && d.display_name.size() <= 55 &&
		std::all_of(d.display_name.begin(), d.display_name.end(), [](unsigned char c) { return std::isalpha(c) || c == '_'; }) &&
		d.race > 0 && d.gender <= 2 && d.class_id >= 1 && d.class_id <= 16 && d.level > 0 &&
		std::isfinite(d.size) && d.size > 0 && d.size <= 100 && std::isfinite(d.x) && std::isfinite(d.y) &&
		std::isfinite(d.z) && std::isfinite(d.heading);
}
}

struct Lifecycle::Impl {
	struct Entry {
		Definition definition;
		Handle handle;
		uint16_t native_id = 0;
		bool creating = true;
		bool retiring = false;
	};
	uint64_t epoch = 0, next = 0;
	bool active = false;
	std::unordered_map<std::string, Entry> entries;
	NPC *Resolve(const Entry &e) const
	{
		auto *npc = entity_list.GetNPCByID(e.native_id);
		return npc && npc->actor_incarnation_marker == e.handle.incarnation ? npc : nullptr;
	}
	Snapshot Observe(const Entry &e) const
	{
		Snapshot s;
		s.definition = e.definition;
		if (e.creating) return s;
		auto *npc = Resolve(e);
		if (!npc) return s;
		s.state = e.retiring || npc->GetDepop() ? State::Retiring : State::Live;
		s.display_name = npc->GetCleanName();
		s.race = npc->GetRace(); s.gender = npc->GetGender(); s.texture = npc->GetTexture();
		s.class_id = npc->GetClass(); s.level = npc->GetLevel(); s.health = npc->GetHP();
		return s;
	}
};

Lifecycle::Lifecycle() : impl(std::make_unique<Impl>()) {}
Lifecycle::~Lifecycle() = default;
Lifecycle &Lifecycle::Get() { static Lifecycle lifecycle; return lifecycle; }
void Lifecycle::BeginZone() { impl->entries.clear(); ++impl->epoch; impl->active = true; }
void Lifecycle::EndZone() { impl->active = false; impl->entries.clear(); }

Result Lifecycle::Create(const Definition &d)
{
	if (!zone || !zone->IsLoaded() || !impl->active || !Valid(d)) return {Outcome::Refused, {}, {}, "loaded zone and valid definition required"};
	auto found = impl->entries.find(d.key);
	if (found != impl->entries.end()) {
		if (found->second.creating) return {Outcome::Busy, {}, {}, "creation in progress"};
		auto snapshot = impl->Observe(found->second);
		if (snapshot.state != State::Absent) {
			if (snapshot.state == State::Retiring) return {Outcome::Busy, found->second.handle, snapshot, "retirement pending"};
			if (!(found->second.definition == d)) return {Outcome::Conflict, found->second.handle, snapshot, "conflicting actor definition"};
			return {Outcome::Existing, found->second.handle, snapshot, {}};
		}
		impl->entries.erase(found);
	}
	// Constructor itself checks the raw name; the clean-name gate also protects suffixed NPCs and Clients.
	for (const auto &[id, mob] : entity_list.GetMobList()) {
		if (CleanName(mob->GetName()) == CleanName(d.display_name) || CleanName(mob->GetCleanName()) == CleanName(d.display_name))
			return {Outcome::Refused, {}, {}, "native name collision"};
	}
	Handle handle;
	handle.zone_incarnation = impl->epoch; handle.incarnation = ++impl->next;
	impl->entries.emplace(d.key, Impl::Entry{d, handle}); // reserve before callback-bearing construction
	try {
		auto type = std::make_unique<NPCType>();
		std::memset(type.get(), 0, sizeof(NPCType));
		std::strncpy(type->name, d.display_name.c_str(), sizeof(type->name) - 1);
		type->race = d.race; type->gender = d.gender; type->texture = d.texture;
		type->class_ = d.class_id; type->level = d.level; type->size = d.size;
		type->current_hp = type->max_hp = 100; type->runspeed = 0.7f; type->bodytype = 1;
		type->STR = type->STA = type->DEX = type->AGI = type->INT = type->WIS = type->CHA = 75;
		type->min_dmg = 1; type->max_dmg = 2; type->attack_count = 1;
		auto npc = std::make_unique<NPC>(type.get(), nullptr, glm::vec4(d.x, d.y, d.z, d.heading), GravityBehavior::Water);
		npc->GiveNPCTypeData(type.release()); // NPC owns its private type exactly once
		npc->actor_incarnation_marker = handle.incarnation;
		// Native registration keeps scoped custody until both indexes accept, then owns deletion before callbacks.
		entity_list.AddNPC(std::move(npc), true, true);
		// Never use the transferred pointer or an iterator retained across native callbacks.
		uint16_t native_id = 0;
		for (const auto &[id, candidate] : entity_list.GetNPCList()) {
			if (candidate->actor_incarnation_marker == handle.incarnation) { native_id = id; break; }
		}
		auto entry = impl->entries.find(d.key);
		if (!impl->active || entry == impl->entries.end() || entry->second.handle != handle || !native_id) {
			if (entry != impl->entries.end() && entry->second.handle == handle) impl->entries.erase(entry);
			return {Outcome::Refused, {}, {}, "registration removed during callback"};
		}
		entry->second.native_id = native_id; entry->second.creating = false;
		return {Outcome::Created, handle, impl->Observe(entry->second), {}};
	} catch (...) {
		// If native registration happened, it remains solely native-owned and is depopped normally.
		for (const auto &[id, npc] : entity_list.GetNPCList()) {
			if (npc->actor_incarnation_marker == handle.incarnation) { npc->Depop(false); break; }
		}
		auto entry = impl->entries.find(d.key);
		if (entry != impl->entries.end() && entry->second.handle == handle) impl->entries.erase(entry);
		throw;
	}
}

Snapshot Lifecycle::Inspect(Handle handle)
{
	if (!impl->active || handle.zone_incarnation != impl->epoch) return {};
	for (const auto &[key, entry] : impl->entries) if (entry.handle == handle) return impl->Observe(entry);
	return {};
}

Result Lifecycle::Retire(Handle handle)
{
	if (!impl->active || handle.zone_incarnation != impl->epoch) return {Outcome::Retired, handle, {}, {}};
	for (auto &[key, entry] : impl->entries) {
		if (entry.handle != handle) continue;
		if (entry.creating) return {Outcome::Busy, handle, {}, "creation in progress"};
		auto snapshot = impl->Observe(entry);
		if (snapshot.state == State::Absent) return {Outcome::Retired, handle, snapshot, {}};
		if (!entry.retiring) {
			entry.retiring = true; // Depop itself invokes callbacks before setting p_depop
			auto *npc = impl->Resolve(entry);
			if (npc && !npc->GetDepop()) npc->Depop(false);
		}
		return {Outcome::RetirementRequested, handle, Inspect(handle), {}};
	}
	return {Outcome::Retired, handle, {}, {}};
}

Definition FirstDefinition(float x, float y, float z, float heading)
{
	Definition d; d.key = "first-actor"; d.display_name = "First_Actor";
	d.x = x; d.y = y; d.z = z; d.heading = heading;
	return d;
}
const char *StateName(State state)
{
	switch (state) { case State::Live: return "live"; case State::Retiring: return "retiring"; default: return "absent"; }
}
}

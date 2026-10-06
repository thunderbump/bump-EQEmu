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
#pragma once
// Policy-only native shim. It proves handle/callback/ownership rules, never a native runtime pass.
#include "zone/actor_lifecycle.h"
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <stdexcept>
#include <unordered_map>
namespace glm { struct vec4 { float x,y,z,w; vec4(float x,float y,float z,float w):x(x),y(y),z(z),w(w){} }; }
enum class GravityBehavior { Water };
struct NPCType {
	char name[64]; uint16_t race; uint8_t gender,texture,class_,level,bodytype;
	float size,runspeed; int64_t current_hp,max_hp;
	uint32_t STR,STA,DEX,AGI,INT,WIS,CHA,min_dmg,max_dmg,attack_count;
};
inline std::function<void()> construction_callback, registration_callback, retirement_callback;
class Zone {
public:
	bool loaded = true;
	bool IsLoaded() { return loaded; }
	void Begin() { Actors::Lifecycle::Get().BeginZone(); }
	void End() { Actors::Lifecycle::Get().EndZone(); }
};
class NPC {
public:
	NPC(const NPCType *t, void *, glm::vec4, GravityBehavior):type(t) { if(construction_callback) construction_callback(); ++allocations; }
	~NPC() { delete owned; --allocations; }
	void GiveNPCTypeData(NPCType *t) { owned=t; }
	uint16_t GetID() const { return id; }
	const char *GetName() const { return name.c_str(); }
	std::string type_name() const { return type->name; }
	const char *GetCleanName() const { return clean.c_str(); }
	uint16_t GetRace() const { return type->race; }
	uint8_t GetGender() const { return type->gender; }
	uint8_t GetTexture() const { return type->texture; }
	uint8_t GetClass() const { return type->class_; }
	uint8_t GetLevel() const { return type->level; }
	int64_t GetHP() const { return type->current_hp; }
	bool GetDepop() const { return depop; }
	void Depop(bool) { ++depops; if(retirement_callback) retirement_callback(); depop=true; }
	inline static int allocations=0,depops=0;
	uint16_t id=0; bool depop=false; std::string name,clean;
private:
	friend class Actors::Lifecycle;
	uint64_t actor_incarnation_marker=0,actor_native_ticks=0;
	const NPCType *type; NPCType *owned=nullptr;
};
enum class RegistrationFault { None, BeforeIndexes, BetweenIndexes, Callback };
inline RegistrationFault registration_fault = RegistrationFault::None;
class EntityList {
public:
	std::unordered_map<uint16_t,NPC*> npcs, mobs;
	uint16_t next=1;
	NPC *GetNPCByID(uint16_t id) { auto it=npcs.find(id);return it==npcs.end()?nullptr:it->second; }
	const auto &GetNPCList() { return npcs; }
	const auto &GetMobList() { return mobs; }
	void AddNPC(NPC *npc,bool send,bool queue) { AddNPC(std::unique_ptr<NPC>(npc),send,queue); }
	void AddNPC(std::unique_ptr<NPC> owned,bool,bool) {
		owned->id=next++;
		if (registration_fault==RegistrationFault::BeforeIndexes) throw std::runtime_error("pre-index fault");
		const auto id=owned->id;
		try {
			npcs.emplace(id,owned.get());
			if (registration_fault==RegistrationFault::BetweenIndexes) throw std::runtime_error("partial-index fault");
			mobs.emplace(id,owned.get());
		} catch (...) { npcs.erase(id);throw; }
		auto *npc=owned.release();
		npc->name=npc->type_name();npc->clean=npc->name;for(char &c:npc->clean) if(c=='_') c=' ';
		if (registration_fault==RegistrationFault::Callback) throw std::runtime_error("callback fault");
		if(registration_callback) registration_callback();
	}
};
extern EntityList entity_list;

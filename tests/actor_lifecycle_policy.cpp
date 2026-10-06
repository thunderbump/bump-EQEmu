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
#include "zone/native.h"
#include <iostream>
#include <stdexcept>

EntityList entity_list;
Zone loaded_zone;
Zone *zone = &loaded_zone;
void Check(bool value, const char *message) { if(!value) throw std::runtime_error(message); }
void ProcessRemoval() {
	for(auto i=entity_list.npcs.begin();i!=entity_list.npcs.end();) {
		if(i->second->depop) { delete i->second; i=entity_list.npcs.erase(i); } else ++i;
	}
}
void Clear() { for(auto [id,n]:entity_list.npcs) delete n; entity_list.npcs.clear(); }
int main() {
	try {
		auto &actors=Actors::Lifecycle::Get();
		auto d=Actors::FirstDefinition(1,2,3);
		Check(actors.Create(d).outcome==Actors::Outcome::Refused,"missing zone lifecycle must refuse");
		loaded_zone.Begin(); zone=nullptr;
		Check(actors.Create(d).outcome==Actors::Outcome::Refused,"null zone must refuse"); zone=&loaded_zone;
		auto invalid=d; invalid.display_name="Unsafe123";
		Check(actors.Create(invalid).outcome==Actors::Outcome::Refused && NPC::allocations==0,"invalid name allocated NPC");
		construction_callback=[&] { Check(actors.Create(d).outcome==Actors::Outcome::Busy,"reentrant constructor did not reserve key"); };
		registration_callback=[&] { Check(actors.Create(d).outcome==Actors::Outcome::Busy,"reentrant registration did not reserve key"); };
		construction_callback=[] { throw std::runtime_error("constructor failure"); };
		bool failed=false; try { actors.Create(d); } catch(const std::runtime_error &) { failed=true; }
		Check(failed,"constructor failure swallowed");
		construction_callback=[&] { Check(actors.Create(d).outcome==Actors::Outcome::Busy,"exception left creation reservation"); };
		auto first=actors.Create(d); construction_callback={};registration_callback={};
		Check(first.outcome==Actors::Outcome::Created && NPC::allocations==1,"create ownership failed");
		auto duplicate=actors.Create(d);
		Check(duplicate.outcome==Actors::Outcome::Existing && duplicate.handle==first.handle && NPC::allocations==1,"duplicate not idempotent");
		auto conflict=d; ++conflict.level;
		Check(actors.Create(conflict).outcome==Actors::Outcome::Conflict && actors.Inspect(first.handle).level==d.level,"conflict changed original");
		auto collision=d;collision.key="another";
		Check(actors.Create(collision).outcome==Actors::Outcome::Refused && NPC::allocations==1,"clean-name collision allocated or deleted");
		retirement_callback=[&] { Check(actors.Create(d).outcome==Actors::Outcome::Busy,"depop callback did not mark retiring");actors.Retire(first.handle); };
		actors.Retire(first.handle);retirement_callback={};
		Check(NPC::depops==1 && actors.Inspect(first.handle).state==Actors::State::Retiring,"reentrant retire double depop");
		ProcessRemoval();Check(NPC::allocations==0 && actors.Retire(first.handle).outcome==Actors::Outcome::Retired,"retirement not completed");
		auto replacement=actors.Create(d);
		Check(replacement.handle!=first.handle && replacement.outcome==Actors::Outcome::Created,"recreation reused handle");
		actors.Retire(first.handle);Check(actors.Inspect(replacement.handle).state==Actors::State::Live,"stale handle retired new incarnation");
		auto native=entity_list.npcs.begin()->second;auto native_id=native->id;
		delete native;entity_list.npcs.clear();
		Check(actors.Inspect(replacement.handle).state==Actors::State::Absent,"external removal not reconciled");
		entity_list.next=native_id;
		auto other=d;other.key="other";other.display_name="Other_Actor";auto unrelated=actors.Create(other);
		Check(entity_list.npcs.begin()->first==native_id,"policy reuse fixture failed");
		actors.Retire(replacement.handle);Check(actors.Inspect(unrelated.handle).state==Actors::State::Live,"ID reuse marker failed");
		loaded_zone.End();Clear();loaded_zone.Begin();auto fresh=actors.Create(d);
		Check(fresh.handle!=replacement.handle && actors.Inspect(replacement.handle).state==Actors::State::Absent,"zone lifetime reused handle");
		actors.Retire(replacement.handle);Check(actors.Inspect(fresh.handle).state==Actors::State::Live,"old zone handle affected live actor");
		loaded_zone.End();Clear();loaded_zone.Begin();
		registration_callback=[] { Clear(); };
		Check(actors.Create(d).outcome==Actors::Outcome::Refused && NPC::allocations==0,"removed registration not refused");registration_callback={};
		Check(actors.Create(d).outcome==Actors::Outcome::Created,"removed registration left key permanently busy");
		loaded_zone.End();Clear();Check(NPC::allocations==0,"policy fixture leaked native owner");
		std::cout<<"actor lifecycle policy PASS (synthetic native shim; runtime pending)\n";return 0;
	} catch(const std::exception &e) { std::cerr<<e.what()<<"\n"; Clear();return 1; }
}

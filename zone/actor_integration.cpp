#include "actor_integration.h"
#if defined(EQEMU_RUST_BOOTSTRAP_BRIDGE) && defined(EQEMU_BUILD_TESTS)
#include "actor_population.h"
#include "entity.h"
#include "npc.h"
#include "zone.h"
#include "water_map.h"
#include "mob_movement_manager.h"
#include <array>
#include <chrono>
#include <cstring>
#include <cmath>
#include <utility>
#include <limits>
#include <type_traits>
#include <cstdlib>
#include <stdexcept>

extern EntityList entity_list;
extern Zone *zone;
namespace ActorIntegration {
namespace {
using Event = eqemu_integration::NativeEvent;
using Context = eqemu_integration::Context;
static_assert(std::is_trivially_copyable_v<Event> && std::is_trivially_copyable_v<Context>);
struct Buffer {
	std::array<Event, 4096> slots{};
	size_t used = 0;
	bool overflow = false;
	void Put(const Event &event) {
		if(used==slots.size()){overflow=true;return;}
		slots[used++]=event;
	}
};
Buffer buffer;
bool enabled = false;
uint16_t awards = 0, boots = 0;
struct ReactionState { Context actor{}, other{}; Event event{}; bool used = false; };
std::array<ReactionState,64> reactions{};
struct PathState { uint64_t incarnation = 0; double x=0,y=0,z=0; bool admitted=false; };
std::array<PathState,3> paths{};
std::array<Context,3> last_body{};
std::array<bool,3> drain_pending{};
std::array<bool,3> removed{};
struct Member { Context context{}; int64_t death_hp=0; bool used=false,dead=false; };
std::array<Member,48> members{};
std::array<uint64_t,3> geometry_members{};
uint64_t Now() {
	return std::chrono::duration_cast<std::chrono::microseconds>(
		std::chrono::steady_clock::now().time_since_epoch()).count();
}
Context Describe(Mob *mob) {
	auto context=ActorPopulation::ObservationContext(mob && mob->IsNPC() ? mob->CastToNPC() : nullptr);
	// NPC::Death unregisters the NPC and sets ID0 before the final death hook.
	// Keep the actually registered lifetime ID; never bind a reused live ID.
	if(!context.native_id && context.incarnation){for(const auto&m:members){if(m.used && m.context.incarnation==context.incarnation){context.native_id=m.context.native_id;break;}}}
	return context;
}
Event Make(uint8_t kind, Mob *actor, Mob *other = nullptr) {
	Event e{}; e.at_us=Now(); e.kind=kind; e.actor=Describe(actor); e.other=Describe(other); return e;
}
void Capture(const Event &event) {
	if (!enabled) { return; }
	buffer.Put(event);
}
bool Actor(Mob *mob) { return mob && mob->IsNPC() && mob->CastToNPC()->UsesActorInventory(); }
bool Relevant(NPC *npc) {
	if (!npc) { return false; }
	if (Actor(npc)) { return true; }
	const auto spawn = npc->GetSpawnPointID();
	return spawn==10863 || spawn==10865 || spawn==4347 ||
		(npc->GetNPCTypeID()==2014 || npc->GetNPCTypeID()==2021);
}
uint32_t LoadedFlags() { return zone ? (zone->zonemap?1u:0u)|(zone->watermap?2u:0u)|(zone->pathing?4u:0u):0; }
}
bool CaptureBufferControl() {
	// This is the production callback buffer, exercised without enabling the
	// observer, entering Rust, writing files or changing any actor permission.
	Buffer probe;
	Event e{};e.kind=6;e.actor.owner_epoch=std::numeric_limits<uint64_t>::max();
	e.e=std::numeric_limits<int64_t>::min();e.f=std::numeric_limits<int64_t>::max();
	for(size_t i=0;i<probe.slots.size();++i){e.at_us=i;probe.Put(e);}
	if(probe.used!=4096 || probe.overflow){return false;}
	e.at_us=99999;probe.Put(e);
	if(probe.used!=4096 || !probe.overflow){return false;}
	for(size_t i=0;i<probe.slots.size();++i){const auto &saved=probe.slots[i];
		if(saved.at_us!=i || saved.actor.owner_epoch!=std::numeric_limits<uint64_t>::max() ||
			saved.e!=std::numeric_limits<int64_t>::min() || saved.f!=std::numeric_limits<int64_t>::max()){return false;}}
	return true;
}
bool Enabled() { return enabled; }
void Open(const char *directory) {
	if (enabled) { return; }
	const auto *mode=std::getenv("EQEMU_CDAU_INTEGRATION");
	if (!mode) { return; }
	if (std::strcmp(mode,"central-cdau-integration-v1")) { throw std::runtime_error("observer_optin"); }
	enabled=eqemu_integration::observer_open(directory,zone->GetZoneID());
}
void Drain() {
	if (!enabled) { return; }
	rust::Vec<Event> events;
	for (size_t i=0;i<buffer.used;++i) { events.push_back(buffer.slots[i]); }
	eqemu_integration::observer_native(std::move(events),buffer.overflow);
	buffer.used=0;
}
void Poll() { if (enabled) { eqemu_integration::observer_poll(); } }
void Finish() { if(enabled) {eqemu_integration::observer_finish();} }
void Remember(NPC *npc,bool death) {
	const auto c=Describe(npc);
	for(auto &m:members){if(m.used && m.context.incarnation==c.incarnation){m.context=c;if(death){m.dead=true;m.death_hp=npc->GetHP();}return;}}
	for(auto &m:members){if(!m.used){m={c,npc->GetHP(),true,death};return;}}
	buffer.overflow=true;
}
void Death(NPC *npc){if(enabled && Relevant(npc)){Remember(npc,true);}}
void Registered(NPC *npc) {
	if (!enabled || !Relevant(npc)) { return; }
	Remember(npc,false);
	auto e=Make(11,npc); e.phase=0; e.a=zone->GetZoneID();
	e.x=npc->GetX();e.y=npc->GetY();e.z=npc->GetZ();Capture(e);
	if(e.actor.actor>=1 && e.actor.actor<=3){last_body[e.actor.actor-1]=e.actor;drain_pending[e.actor.actor-1]=true;removed[e.actor.actor-1]=false;}
}
void Removed(NPC *npc) {
	if (!enabled || !Relevant(npc)) { return; }
	Remember(npc,false);
	auto e=Make(11,npc);e.phase=1;e.a=zone->GetZoneID();Capture(e);
	if(e.actor.actor>=1 && e.actor.actor<=3){removed[e.actor.actor-1]=true;}
}
void Reaction(Mob *actor, Mob *observer, int32_t faction, int32_t standing,
	int32_t item_bonus, int32_t spell_bonus, uint8_t reaction) {
	if (!enabled || !Actor(actor) || !observer || !observer->IsNPC()) { return; }
	auto e=Make(10,actor,observer);e.e=faction;e.f=standing;e.g=item_bonus;e.h=spell_bonus;e.a=reaction;
	e.flags=(observer->CheckAggro(actor)?1u:0u)|(actor->GetFeigned()?2u:0u)|
		(actor->IsInvisible(observer)?4u:0u)|(actor->GetInvisibleUndeadLevel()?8u:0u)|
		(observer->GetOwnerID()?16u:0u)|(observer->CastToNPC()->MerchantType?32u:0u)|
		(zone->CanDoCombat()?64u:0u)|(faction<0?128u:0u);
	e.b=observer->CastToNPC()->GetHateListCount();
	// Reaction transitions are recorded once. Authorization attempts are never
	// deduplicated; every actual gate below carries its resolved con.
	for (auto &r:reactions) {
		if (r.used && r.actor.native_id==e.actor.native_id && r.actor.incarnation==e.actor.incarnation &&
			r.other.native_id==e.other.native_id && r.other.incarnation==e.other.incarnation) {
			const auto &old=r.event;
			if (old.e==e.e && old.f==e.f && old.g==e.g && old.h==e.h && old.a==e.a && old.b==e.b && old.flags==e.flags) { return; }
			r.event=e;Capture(e);return;
		}
	}
	for (auto &r:reactions) { if (!r.used) { r={e.actor,e.other,e,true};Capture(e);return; } }
	buffer.overflow=true;
}
void Gate(Mob *attacker,Mob *victim,bool allowed) {
	if (!enabled || (!Actor(attacker) && !Actor(victim))) { return; }
	const bool outgoing=Actor(attacker);
	auto *actor=outgoing?attacker:victim;auto *other=outgoing?victim:attacker;
	auto e=Make(6,actor,other);e.phase=outgoing?0:1;e.a=allowed?1:0;e.c=allowed?0:1;
	auto *recipient=outgoing?other:actor;
	e.e=recipient?recipient->GetHP():0;e.f=e.e;
	for (const auto &r:reactions) {if(r.used && r.actor.native_id==e.actor.native_id && r.actor.incarnation==e.actor.incarnation &&
		r.other.native_id==e.other.native_id && r.other.incarnation==e.other.incarnation) {e.b=r.event.a;}}
	Capture(e);
}
Attack::Attack(Mob *attacker,Mob *victim,bool allowed) {
	if (!enabled || (!Actor(attacker) && !Actor(victim))) { return; }
	const bool outgoing=Actor(attacker);auto *actor=outgoing?attacker:victim;auto *other=outgoing?victim:attacker;
	event_=Make(6,actor,other);event_.phase=outgoing?0:1;event_.a=allowed?1:0;event_.c=allowed?0:1;
	// For incoming retaliation HP belongs to the actor body, matching the actual
	// damaged recipient. For outgoing attacks it belongs to the prey.
	auto *recipient=victim && victim->IsNPC()?victim->CastToNPC():nullptr;
	if (!recipient) { buffer.overflow=true;return; }
	id_=recipient->GetID();incarnation_=recipient->NativeIncarnation();event_.e=recipient->GetHP();
	for(const auto&r:reactions){if(r.used && r.actor.native_id==event_.actor.native_id && r.actor.incarnation==event_.actor.incarnation &&
		r.other.native_id==event_.other.native_id && r.other.incarnation==event_.other.incarnation){event_.b=r.event.a;}}
	active_=true;
}
Attack::~Attack() {
	if (!active_) { return; }
	auto *npc=entity_list.GetNPCByID(id_);
	if(npc && npc->NativeIncarnation()==incarnation_){event_.f=npc->GetHP();}
	else {
		bool observed=false;
		for(const auto&m:members){if(m.used && m.dead && m.context.native_id==id_ && m.context.incarnation==incarnation_){event_.f=m.death_hp;observed=true;break;}}
		if(!observed){buffer.overflow=true;return;}
	}
	event_.at_us=Now();Capture(event_);
}
uint16_t Award(NPC *actor,NPC *victim,uint64_t base,uint64_t scaled,uint64_t final,uint64_t threshold,uint32_t factions) {
	if (!enabled) { return 0; }
	if(awards>=4){buffer.overflow=true;return 0;}
	const auto ref=++awards;
	auto e=Make(8,actor,victim);e.reference=ref;e.phase=0;Capture(e);
	e.phase=1;e.a=base;e.b=scaled;e.c=final;e.d=threshold;e.flags=factions;Capture(e);return ref;
}
void Faction(NPC *actor,NPC *victim,uint16_t award,int32_t id,int32_t before,int32_t after) {
	if (!enabled) { return; }
	auto e=Make(7,actor,victim);e.reference=award;e.e=id;e.f=before;e.g=after;Capture(e);
}
void Level(NPC *actor,uint8_t old_level,uint64_t experience) {
	if(!enabled){return;}auto e=Make(9,actor);e.phase=1;e.a=old_level;e.b=actor->GetLevel();
	e.c=actor->GetBaseExperienceForLevel(actor->GetLevel()+1);e.d=experience;Capture(e);
}
void Body(NPC *actor,uint8_t phase,uint64_t peer_zone) {
	if(!enabled){return;}auto e=Make(11,actor);e.phase=phase;e.a=peer_zone?peer_zone:zone->GetZoneID();
	e.b=e.actor.owner_epoch;e.c=e.actor.generation;e.d=e.actor.incarnation;Capture(e);
	if(e.actor.actor>=1 && e.actor.actor<=3){last_body[e.actor.actor-1]=e.actor;}
}
void Boot(NPC *actor) {
	if(!enabled){return;}if(boots>=12){buffer.overflow=true;return;}
	auto e=Make(12,actor);e.phase=1;e.reference=100;e.a=++boots;Capture(e);
}
void Boot(unsigned actor,uint64_t owner_epoch,uint8_t level) {
	if(!enabled){return;}if(boots>=12 || actor>=3){buffer.overflow=true;return;}
	auto e=Make(12,nullptr);e.actor.actor=actor+1;e.actor.kind=0;e.actor.owner_epoch=owner_epoch;e.actor.level=level;
	e.phase=1;e.reference=100;e.a=++boots;Capture(e);
}
void ObserveDrain(unsigned actor,bool drained) {
	if(!enabled || actor>=3 || !drained || !drain_pending[actor]){return;}
	auto e=Make(11,nullptr);e.actor=last_body[actor];e.phase=2;e.a=zone->GetZoneID();
	e.b=e.actor.owner_epoch;e.c=e.actor.generation;e.d=e.actor.incarnation;
	if(!removed[actor]){e.phase=1;Capture(e);e.phase=2;}
	Capture(e);drain_pending[actor]=false;
}
void PathAdmitted(Mob *actor,double x,double y,double z) {
	if(!enabled || !Actor(actor)){return;}const auto c=Describe(actor);
	if(c.actor>=1 && c.actor<=3){paths[c.actor-1]={c.incarnation,x,y,z,true};
		auto e=Make(12,actor);e.phase=1;e.reference=102;e.x=x;e.y=y;e.z=z;e.flags=LoadedFlags();Capture(e);}
}
void Route(NPC *actor,uint32_t id,uint32_t destination,double x,double y,double z,
	double ax,double ay,double az,bool completed) {
	if(!enabled){return;}const auto c=Describe(actor);uint32_t flags=LoadedFlags()|(id==9?8u:0u);
	if(c.actor>=1 && c.actor<=3){const auto&p=paths[c.actor-1];if(p.admitted && p.incarnation==c.incarnation &&
		p.x==std::lround(x) && p.y==std::lround(y) && p.z==std::lround(z)){flags|=16u|32u;}}
	if(completed){flags|=64u;}
	eqemu_integration::observer_route({id,destination,x,y,z,ax,ay,az,flags});
}
void Geometry(NPC *actor,NPC *hostile,NPC *friendly) {
	if(!enabled || !actor || !hostile || !friendly || actor->IsMoving() || actor->GetAppearance()!=eaStanding ||
		MobMovementManager::Get().InspectActorCombatMotion(actor)!=MobMovementManager::ActorCombatMotion::Stopped){return;}
	const std::array<uint64_t,3> identities{actor->NativeIncarnation(),hostile->NativeIncarnation(),friendly->NativeIncarnation()};
	if(geometry_members==identities){return;}geometry_members=identities;
	uint32_t flags=1u|2u|4u;
	flags|=actor->CheckLosFN(hostile)?8u:0u;flags|=actor->CheckLosFN(friendly)?16u:0u;
	auto &motion=MobMovementManager::Get();
	if(motion.CanNavigateActorTo(actor,hostile->GetX(),hostile->GetY(),hostile->GetZ(),true)){flags|=32u;}
	if(motion.CanNavigateActorTo(actor,friendly->GetX(),friendly->GetY(),friendly->GetZ(),true)){flags|=64u;}
	bool dry=zone->watermap;
	for(auto*npc:{actor,hostile,friendly}){if(!zone->watermap || zone->watermap->SegmentInLiquid(
		glm::vec3(npc->GetX(),npc->GetY(),npc->GetZ()-npc->GetZOffset()),glm::vec3(npc->GetX(),npc->GetY(),npc->GetZ()))){dry=false;}}
	if(dry){flags|=128u;}
	eqemu_integration::observer_geometry({Describe(actor),Describe(hostile),Describe(friendly),
		actor->GetX(),actor->GetY(),actor->GetZ(),hostile->GetX(),hostile->GetY(),hostile->GetZ(),
		friendly->GetX(),friendly->GetY(),friendly->GetZ(),flags});
}
} // namespace ActorIntegration
#else
namespace ActorIntegration {
bool Enabled(){return false;}void Open(const char*){}void Drain(){}void Poll(){}void Finish(){}
void Registered(NPC*){}void Removed(NPC*){}void Death(NPC*){}
void Reaction(Mob*,Mob*,int32_t,int32_t,int32_t,int32_t,uint8_t){}
void Gate(Mob*,Mob*,bool){}
uint16_t Award(NPC*,NPC*,uint64_t,uint64_t,uint64_t,uint64_t,uint32_t){return 0;}
void Faction(NPC*,NPC*,uint16_t,int32_t,int32_t,int32_t){}
void Level(NPC*,uint8_t,uint64_t){}void Body(NPC*,uint8_t,uint64_t){}void Boot(NPC*){}
void Boot(unsigned,uint64_t,uint8_t){}void ObserveDrain(unsigned,bool){}
void PathAdmitted(Mob*,double,double,double){}
void Route(NPC*,uint32_t,uint32_t,double,double,double,double,double,double,bool){}
void Geometry(NPC*,NPC*,NPC*){}
#ifdef EQEMU_RUST_BOOTSTRAP_BRIDGE
Attack::Attack(Mob*,Mob*,bool){}Attack::~Attack(){}
#endif
}
#endif

// SPDX-License-Identifier: MIT
// Run after converting assets from the verified installer:
// g++ -std=gnu++20 -fsanitize=address,undefined -Ibuild/assets -Igame tools/emu/prisoner_test.cpp game/*.cpp build/assets/assets.S -o /tmp/prisoner_test && /tmp/prisoner_test
#define private public
#include "world.hpp"
#undef private
#include <cassert>
using namespace lf2;
int main() {
 World w{}; Stage s; s.width=2000; w.init(s,123); w.add_fighter(0,1,true,0); w.stage_mode=true; w.fighters=2;
 const Group& g=weapon_group(); const ObjDef* d=nullptr; for(int i=0;i<g.count;++i) if(g.objs[i].hdr->id==300) d=&g.objs[i]; assert(d);
 assert(d->bdy[d->frame(0).bdy_off].kind==1003);
 Ent& e=w.ents[1]; e=Ent{}; e.live=true; e.def=d; e.data_group=&g; e.kind=5; e.group=1; e.team=2; e.hp=e.dark_hp=300; e.x=FX(400); e.z=FX(400); e.frame_at_pass=0; e.ai.enabled=true; w.groups[1]=&g;
 w.ents[0].x=FX(300); w.ents[0].z=FX(400); Itr hit{}; w.plain_hit(0,1,hit); assert(e.team==1 && e.frame==3);
 for(int i=0;i<50 && e.kind!=0;++i) w.tick();
 assert(e.live && e.kind==0 && e.team==1 && e.def->hdr->id==35);
}

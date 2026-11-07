// SPDX-License-Identifier: MIT
// Integration check using converted game assets: an AI fighter approaches and picks up a weapon.
#include "world.hpp"
#include "assets.h"
#include <cassert>
#include <initializer_list>
using namespace lf2;
int main() {
    World w{}; Stage stage; stage.width=1600; stage.z1=360; stage.z2=500;
    w.init(stage, 123); w.add_fighter(0,1,false,50); w.add_fighter(1,2,true,0);
    w.ents[0].x=FX(100); w.ents[0].z=FX(400);
    w.ents[1].x=FX(500); w.ents[1].z=FX(440);
    const auto& group=weapon_group(); const ObjDef* weapon=nullptr;
    for (int i=0;i<group.count;++i) if (group.objs[i].hdr->id==100) weapon=&group.objs[i];
    assert(weapon);
    const int slot=w.spawn(weapon,max_fighters,-1,64,0,FX(125),0,FX(400),0,0,0,0);
    assert(slot>=0);
    for (int i=0;i<60 && w.ents[0].held_item!=slot;++i) w.tick();
    assert(w.ents[0].held_item==slot && w.ents[slot].holder==0);
    // Fighters seek distant items only while another target is present.
    w.init(stage, 128); w.add_fighter(0,1,false,50); w.add_fighter(1,2,true,0);
    w.ents[0].x=FX(100); w.ents[0].z=FX(400);
    w.ents[1].x=FX(700); w.ents[1].z=FX(440);
    const int unattended=w.spawn(weapon,max_fighters,-1,64,0,FX(220),0,FX(400),0,0,0,0);
    assert(unattended>=0);
    for (int i=0;i<100 && w.ents[0].held_item!=unattended;++i) w.tick();
    assert(w.ents[0].held_item==unattended);
    w.init(stage, 124); w.add_fighter(0,1,false,50); w.add_fighter(1,2,true,0);
    w.ents[0].x=FX(100); w.ents[0].z=FX(400); w.ents[0].hp=300; w.ents[0].dark_hp=400;
    w.ents[0].blink=0; w.ents[1].x=FX(700); w.ents[1].z=FX(440);
    const ObjDef* milk=nullptr;
    for (int i=0;i<group.count;++i) if (group.objs[i].hdr->id==122) milk=&group.objs[i];
    assert(milk);
    const int bottle=w.spawn(milk,max_fighters,-1,0,1,FX(100),0,FX(400),0,0,0,0);
    assert(bottle>=0); w.ents[bottle].hp=200; w.ents[bottle].holder=0; w.ents[0].held_item=bottle;
    for (int i=0;i<100;++i) w.tick();
    assert(w.ents[bottle].hp<200 && w.ents[0].hp>302);
    w.init(stage,127); w.add_fighter(0,1,false,50); w.add_fighter(1,2,true,0);
    w.ents[0].x=FX(100); w.ents[0].z=FX(400); w.ents[0].hp=200; w.ents[0].dark_hp=500;
    w.ents[0].follow_order=true; w.ents[1].x=FX(700); w.ents[1].z=FX(440);
    const int ignored=w.spawn(weapon,max_fighters,-1,64,0,FX(60),0,FX(400),0,0,0,0);
    int milk_rest=-1;
    for (int i=0;i<milk->hdr->nframes;++i) if (milk->defined(i) && milk->frame(i).state==1004) { milk_rest=i; break; }
    assert(milk_rest>=0);
    const int preferred=w.spawn(milk,max_fighters,-1,milk_rest,0,FX(150),0,FX(400),0,0,0,0);
    assert(ignored>=0 && preferred>=0);
    for (int i=0;i<60 && w.ents[0].held_item<0;++i) w.tick();
    assert(w.ents[0].held_item==preferred);
    w.init(stage,126); w.add_fighter(0,1,false,50);
    const ObjDef* projectile=nullptr; int flying_frame=-1;
    const Group& fighter_group=*w.groups[0];
    for (int i=0;i<fighter_group.count && !projectile;++i) {
        const ObjDef& candidate=fighter_group.objs[i];
        if (candidate.kind()!=3) continue;
        for (int frame=0;frame<candidate.hdr->nframes;++frame) if (candidate.defined(frame) && candidate.frame(frame).state==3000) {
            projectile=&candidate; flying_frame=frame; break;
        }
    }
    assert(projectile);
    w.ents[0].x=FX(100); w.ents[0].z=FX(400); w.ents[0].facing=0;
    const int ball=w.spawn(projectile,0,-1,flying_frame,2,FX(250),0,FX(400),-FX(5),0,0,1);
    assert(ball>=0); w.tick(); assert(w.ents[0].keys.defend);
    for (int id : {2, 34}) {
        int roster=-1;
        for (int i=0;i<ROSTER_COUNT;++i) if (ROSTER_ID[i]==id) roster=i;
        assert(roster>=0);
        w.init(stage,125); w.add_fighter(roster,1,false,50); w.add_fighter(0,1,true,0); w.add_fighter(1,2,true,0);
        auto& healer=w.ents[0]; auto& ally=w.ents[1]; auto& enemy=w.ents[2];
        healer.x=FX(100); healer.z=FX(400); healer.facing=0; healer.mp=500;
        ally.x=FX(160); ally.z=FX(400); ally.hp=100; ally.dark_hp=500;
        enemy.x=FX(700); enemy.z=FX(400);
        const int action=healer.def->frame(0).hit_Uj;
        assert(action>0 && healer.def->defined(action));
        bool cast=false;
        for (int tick=0;tick<30;++tick) { w.tick(); if (healer.frame==action) { cast=true; break; } }
        assert(cast && healer.mp<500);
        bool healing=false;
        for (int tick=0;tick<80;++tick) { w.tick(); if (ally.regen_timer>1000) { healing=true; break; } }
        assert(healing);
        const int hp_before=ally.hp;
        for (int tick=0;tick<16;++tick) w.tick();
        assert(ally.hp>=hp_before+8);
        bool self_healing=false;
        for (int seed=130;seed<140 && !self_healing;++seed) {
            w.init(stage,seed); w.add_fighter(roster,1,false,50); w.add_fighter(1,2,true,0);
            auto& self=w.ents[0]; auto& rival=w.ents[1];
            self.x=FX(100); self.z=FX(400); self.hp=100; self.dark_hp=500; self.mp=500; self.follow_order=true;
            rival.x=FX(700); rival.z=FX(440);
            const int self_action=self.def->frame(0).hit_Dj;
            assert(self_action>0 && self.def->defined(self_action));
            bool self_cast=false;
            for (int tick=0;tick<1000;++tick) { w.tick(); if (self.frame==self_action) { self_cast=true; break; } }
            if (!self_cast) continue;
            assert(self.mp<500);
            for (int tick=0;tick<100;++tick) { w.tick(); if (self.regen_timer>1000) { self_healing=true; break; } }
        }
        assert(self_healing);
    }
    // In Stage mode a fighter that is far ahead of the others turns back when it has nothing to fight.
    w.init(stage,129); w.ai_stage_mode=true; w.auto_finish_round=false;
    w.add_fighter(0,1,true,0); w.add_fighter(1,1,false,50);
    w.ents[0].x=FX(100); w.ents[0].z=FX(400); w.ents[1].x=FX(450); w.ents[1].z=FX(400);
    w.tick(); assert(w.ents[1].keys.left && !w.ents[1].keys.right);
    w.ents[1].x=FX(150); w.tick(); assert(!w.ents[1].keys.left);
    // A landed arrow (frame state 9998) is deleted.
    {
        int henry=-1;
        for (int n=0;n<ROSTER_COUNT;++n) if (ROSTER_ID[n]==4) henry=n;
        assert(henry>=0);
        w.init(stage,131); w.add_fighter(henry,1,true,0);
        const ObjDef* arrow=nullptr;
        for (int n=0;n<w.groups[0]->count;++n) if (w.groups[0]->objs[n].hdr->id==201) arrow=&w.groups[0]->objs[n];
        assert(arrow);
        const int slot=w.spawn(arrow,0,-1,64,1,FX(300),0,FX(400),0,0,0,0);
        assert(slot>=0 && w.ents[slot].live);
        w.tick(); assert(!w.ents[slot].live);
    }
}

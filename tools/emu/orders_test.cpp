// SPDX-License-Identifier: MIT
// Run after converting the original assets:
// g++ -std=gnu++20 -fsanitize=address,undefined -Ibuild/assets -Igame tools/emu/orders_test.cpp game/*.cpp build/assets/assets.S -o /tmp/orders_test && /tmp/orders_test
#define private public
#include "world.hpp"
#undef private
#include <cassert>
using namespace lf2;
int main() {
    World w{}; Stage s; s.width=1600; w.init(s,1);
    w.add_fighter(0,1,true,0); w.add_fighter(1,1,false,50); w.add_fighter(2,2,true,0);
    w.ents[0].x=FX(50); w.ents[0].z=FX(400);
    w.ents[1].x=FX(100); w.ents[1].z=FX(400);
    w.ents[2].x=FX(700); w.ents[2].z=FX(400);
    auto& leader=w.ents[0]; auto& ally=w.ents[1];
    leader.history[1]=leader.history[2]=leader.history[3]=leader.history[4]=9;
    w.team_orders(0);
    assert(ally.follow_order && ally.order_leader==0);
    bool marker=false; for (const Ent& e:w.ents) if (e.live && e.def->hdr->id==998 && e.frame==2) marker=true;
    assert(marker);
    ally.facing=0; w.ai_control(1); assert(!ally.keys.right && !ally.keys.left);
    ally.facing=1; w.ai_control(1); assert(ally.keys.right);
    ally.facing=0; ally.frame=9; w.ai_control(1); assert(ally.keys.left);
    ally.frame=0; leader.live=false; ally.ai.timer=0; w.ai_control(1); assert(!ally.keys.right);
    leader.live=true;
    leader.history[1]=9; leader.history[2]=5; leader.history[3]=9; leader.history[4]=5;
    w.team_orders(0); assert(!ally.follow_order);
    ally.order_x=150; ally.order_z=400; ally.frame=9; ally.facing=0;
    ally.blocked_right=true; ally.prev.attack=true; w.ai_control(1);
    assert(ally.keys.right && ally.keys.left && ally.keys.attack && !ally.prev.attack && ally.order_x==-1000);
    ally.order_x=500; ally.order_z=400; ally.frame=0; ally.blocked_right=false;
    w.ai_control(1); assert(ally.keys.right && ally.order_x==500);
    bool ran=false;
    for (int tick=0;tick<100;++tick) { w.tick(); if (ally.def->frame(ally.frame).state==2) { ran=true; break; } }
    assert(ran);
    w.ai_stage_mode=true; w.stage_exit=true; ally.blocked_right=true; ally.prev.right=ally.prev.attack=true;
    w.ai_control(1); assert(ally.keys.right && ally.keys.attack && !ally.prev.right && !ally.prev.attack && ally.order_x==500);
}

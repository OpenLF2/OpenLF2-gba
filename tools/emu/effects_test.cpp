// SPDX-License-Identifier: MIT
// Run after converting the original assets:
// g++ -std=gnu++20 -fsanitize=address,undefined -Ibuild/assets -Igame tools/emu/effects_test.cpp game/*.cpp build/assets/assets.S -o /tmp/effects_test && /tmp/effects_test
#define private public
#include "world.hpp"
#undef private
#include "assets.h"
#include <cassert>
using namespace lf2;
int main() {
    int roster = -1;
    for (int i=0; i<ROSTER_COUNT; ++i) if (ROSTER_ID[i]==51) roster=i;
    assert(roster>=0);
    World w{}; Stage stage; stage.width=2400; w.init(stage,1); w.add_fighter(roster,1,true,0);
    const Group* group=w.groups[0];
    const ObjDef* seeker=w.find_def(group,221); const ObjDef* column=w.find_def(group,212);
    assert(seeker && column && seeker->frame(80).hit_Fa==11);
    int parent=w.spawn(seeker,0,0,80,1,FX(800),FX(-100),FX(420),0,0,0,0);
    assert(parent>=0); w.ball_update(parent);
    int flame=0, fire=0, ice=0;
    for (const Ent& e:w.ents) if (e.live && e.def) {
        flame += e.def->hdr->id==211; fire += e.def->hdr->id==221; ice += e.def->hdr->id==212;
    }
    assert(!w.ents[parent].live && flame==7 && fire==1 && ice==6);
}

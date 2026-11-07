// SPDX-License-Identifier: MIT
// g++ -std=c++20 -fsanitize=address,undefined -Igame tools/emu/army_test.cpp game/army.cpp -o /tmp/army_test && /tmp/army_test
#include "army.hpp"
#include <cassert>
using namespace lf2;
struct Fixture { int count[2][army_types] = {}; int men[2] = {}; bool slots = true; int first_side = -1; };
int census(void* c, int side, int kind) { return static_cast<Fixture*>(c)->count[side][kind]; }
int living(void* c, int side) { return static_cast<Fixture*>(c)->men[side]; }
bool spawn(void* c, int side, int kind, int, int) {
    auto& f=*static_cast<Fixture*>(c); if (!f.slots) return false;
    if (f.first_side < 0) f.first_side=side;
    ++f.count[side][kind]; if (kind < 9) ++f.men[side]; return true;
}
int main() {
    ArmyRun run; run.preset(0,0,1); run.preset(1,0,1);
    assert(run.stock[0][0]==6 && run.on_field[0][0]==2 && run.stock[0][7]==1 && run.on_field[0][7]==1);
    ArmyRun presets; presets.preset(0,5,1); presets.preset(1,6,1);
    assert(presets.reserves(0)==0 && presets.stock[1][0]==32 && presets.stock[1][7]==16 && presets.stock[1][9]==33);
    int custom_first[army_types] = {}, custom_reserve[army_types] = {};
    custom_first[0]=2; custom_reserve[0]=9; custom_reserve[1]=20;
    presets.configure(0,custom_first,custom_reserve);
    assert(presets.on_field[0][0]==2 && presets.stock[0][0]==11 && presets.stock[0][1]==0);
    Fixture f; ArmyHooks hooks{&f,census,living,spawn};
    run.tick(hooks); assert(f.count[0][0]==1 && run.stock[0][0]==5 && !run.over);
    assert(f.first_side==0); f.first_side=-1;
    run.tick(hooks); assert(f.first_side==0 && f.count[0][0]==2);
    f.slots=false; int before=run.stock[0][0]; run.tick(hooks); assert(run.stock[0][0]==before);
    --f.count[0][0]; f.slots=true; run.tick(hooks); assert(f.count[0][0]==2 && run.stock[0][0]==before-1);
    ArmyRun empty; empty.preset(0,0,1); empty.preset(1,0,1);
    for(int i=0;i<9;++i) empty.stock[1][i]=0;
    Fixture finished; finished.men[0]=1; finished.slots=false;
    ArmyHooks end_hooks{&finished,census,living,spawn}; empty.tick(end_hooks);
    assert(empty.over && empty.winner==1);
    ArmyRun last; int first[army_types]={}, reserve[army_types]={}; first[0]=1;
    last.configure(0,first,reserve); last.configure(1,first,reserve);
    Fixture simultaneous; ArmyHooks last_hooks{&simultaneous,census,living,spawn};
    last.tick(last_hooks);
    assert(simultaneous.men[0]==1 && simultaneous.men[1]==1 && last.over && last.winner==0);
    last.tick(last_hooks); assert(!last.over && simultaneous.men[0]==1 && simultaneous.men[1]==1);
    simultaneous.men[1]=0; last.tick(last_hooks); assert(last.over && last.winner==1);
    assert(ArmyRun::result(3,1)==-1 && ArmyRun::result(0,1)==2 && ArmyRun::result(2,0)==1 && ArmyRun::result(0,0)==0);
}

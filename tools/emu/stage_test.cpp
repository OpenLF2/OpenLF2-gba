// SPDX-License-Identifier: MIT
// g++ -std=c++20 -fsanitize=address,undefined -Igame tools/emu/stage_test.cpp game/stage_run.cpp -o /tmp/stage_test && /tmp/stage_test
#include "stage_run.hpp"
#include <cassert>
using namespace lf2;
struct Fixture { int next = 0, phases = 0, capacity = 64; bool full = false, extra_enemy = false; bool alive[64] = {}; };
int spawn(void* p, const StageEntry&) { auto& f = *static_cast<Fixture*>(p); if (f.full || f.next >= f.capacity) return -1; int n = f.next++; f.alive[n] = true; return n; }
int spawn_item(void*, const StageEntry&) { return -2; }
bool alive(void* p, int n) { return static_cast<Fixture*>(p)->alive[n]; }
bool not_blocking(void*, int) { return false; }
bool fighter_blocks(void*, int handle) { return handle == 0; }
bool extra_enemy(void* p) { return static_cast<Fixture*>(p)->extra_enemy; }
void phase(void* p, const StagePhase&) { ++static_cast<Fixture*>(p)->phases; }
struct ReuseFixture { bool alive[5] = {}, reserved[5] = {}; int last = -1, count = 0; };
int reuse_spawn(void* p, const StageEntry&) {
    auto& f=*static_cast<ReuseFixture*>(p);
    for (int i=0;i<5;++i) if (!f.alive[i] && !f.reserved[i]) {
        f.alive[i]=true; f.last=i; ++f.count; return i;
    }
    return -1;
}
bool reuse_alive(void* p, int n) { return static_cast<ReuseFixture*>(p)->alive[n]; }
void clear_reserved(void* p) { for (bool& v : static_cast<ReuseFixture*>(p)->reserved) v=false; }
void reserve_slot(void* p, int n) { static_cast<ReuseFixture*>(p)->reserved[n]=true; }
int main() {
    const StageEntry entries[] = {{30,0,50,2,0,0,0,9,0,0,2}, {31,0,50,50,0,0,0,9,0,100,1}, {32,0,50,1,0,0,0,9,0,0,0}};
    const StagePhase phases[] = {{900,-1,0,2,1},{1200,-1,2,1,0}};
    const StageScript script{0,2};
    Fixture f; StageHooks hooks{&f,spawn,alive,phase}; StageRun run; run.start(script,phases,entries,1);
    run.tick(hooks); assert(run.phase()==0 && f.next==2 && f.phases==1);
    f.alive[0]=false; f.alive[1]=false; run.tick(hooks); assert(f.next==3 && run.phase()==0);
    f.alive[2]=false; run.tick(hooks); assert(run.phase()==1 && f.phases==2);
    run.tick(hooks); assert(f.next==4); f.alive[3]=false; run.tick(hooks); assert(run.finished());
    Fixture retry; retry.full=true; StageHooks retry_hooks{&retry,spawn,alive,phase}; run.start(script,phases,entries,1);
    run.tick(retry_hooks); assert(!run.finished() && run.phase()==0 && retry.next==0);
    retry.full=false; run.tick(retry_hooks); assert(retry.next==2);
    const StageEntry soldier[] = {{30,0,50,50,0,0,0,9,0,100,1}};
    const StagePhase single[] = {{900,-1,0,1,0}};
    const StageScript one{0,1}; Fixture no_boss; StageHooks single_hooks{&no_boss,spawn,alive,phase};
    run.start(one,single,soldier,1); run.tick(single_hooks); assert(no_boss.next==1);
    no_boss.alive[0]=false; run.tick(single_hooks); assert(run.finished());
    StageHooks item_hooks{&no_boss,spawn_item,alive,phase};
    const StageEntry item[] = {{122,100,500,1,0,0,0,0,0,0,0}};
    run.start(one,single,item,1); run.tick(item_hooks); assert(run.finished());
    Fixture recruit; StageHooks recruit_hooks{&recruit,spawn,alive,phase,not_blocking};
    run.start(one,single,item,1); run.tick(recruit_hooks); assert(run.finished() && recruit.alive[0]);
    const StageEntry scaled[] = {{30,0,50,2,0,0,0,9,0,70,0}};
    Fixture power; StageHooks power_hooks{&power,spawn,alive,phase};
    run.start(one,single,scaled,1); run.set_players(2); run.tick(power_hooks);
    assert(power.next==1 && !run.finished());
    power.alive[0]=false; run.tick(power_hooks); assert(power.next==2);
    const StagePhase loop[] = {{900,-1,0,1,0}, {900,0,0,1,0}};
    const StageScript looping{0,2}; Fixture looping_fixture;
    StageHooks loop_hooks{&looping_fixture,spawn,alive,phase};
    run.start(looping,loop,item,1); run.tick(loop_hooks); looping_fixture.alive[0]=false;
    assert(run.next_bound_same()); run.tick(loop_hooks); assert(run.phase()==1 && run.next_bound_same());
    run.tick(loop_hooks); looping_fixture.alive[1]=false; run.tick(loop_hooks);
    assert(run.phase()==0 && !run.finished());
    Fixture external; external.extra_enemy=true;
    StageHooks external_hooks{&external,spawn_item,alive,phase,nullptr,extra_enemy};
    run.start(one,single,item,1); run.tick(external_hooks); assert(!run.finished());
    external.extra_enemy=false; run.tick(external_hooks); assert(run.finished());
    // A replacement item postpones completion for the tick in which it is spawned.
    const StageEntry replacement_item[]={{122,0,500,2,0,0,0,0,0,0,0}};
    Fixture item_replacement; item_replacement.extra_enemy=true;
    StageHooks replacement_hooks{&item_replacement,spawn,alive,phase,not_blocking,extra_enemy};
    run.start(one,single,replacement_item,1); run.tick(replacement_hooks); assert(item_replacement.next==1 && !run.finished());
    item_replacement.alive[0]=false; item_replacement.extra_enemy=false; run.tick(replacement_hooks);
    assert(item_replacement.next==2 && !run.finished());
    run.tick(replacement_hooks); assert(run.finished());
    const StageEntry zero_total[]={{30,0,50,0,0,0,0,9,0,0,0}};
    Fixture initial; StageHooks initial_hooks{&initial,spawn,alive,phase};
    run.start(one,single,zero_total,1); run.tick(initial_hooks);
    assert(initial.next==1 && !run.finished());
    initial.alive[0]=false; run.tick(initial_hooks); assert(run.finished());
    Fixture occupied; StageHooks occupied_hooks{&occupied,spawn,alive,phase};
    run.start(script,phases,entries,1); run.tick(occupied_hooks); assert(occupied.next==2);
    occupied.alive[0]=false; run.tick(occupied_hooks); assert(occupied.next==3);
    occupied.alive[1]=false; run.tick(occupied_hooks); assert(occupied.next==4);
    // Simultaneous defeats stop soldier replacements before the next boss appears.
    const StageEntry simultaneous[] = {{31,0,50,10,0,0,0,0,0,100,1}, {32,0,50,2,0,0,0,0,0,0,2}};
    Fixture simultaneous_fixture; StageHooks simultaneous_hooks{&simultaneous_fixture,spawn,alive,phase};
    run.start(one,phases,simultaneous,1); run.tick(simultaneous_hooks);
    assert(simultaneous_fixture.next==2);
    simultaneous_fixture.alive[0]=simultaneous_fixture.alive[1]=false; run.tick(simultaneous_hooks);
    assert(simultaneous_fixture.next==3 && simultaneous_fixture.alive[2]);
    const StageEntry drops[] = {{30,0,50,1,0,0,0,0,0,0,0}, {122,0,500,3,0,0,0,0,0,0,0}};
    const StagePhase drop_phase[] = {{900,-1,0,2,0}};
    Fixture drop_fixture; StageHooks drop_hooks{&drop_fixture,spawn,alive,phase,fighter_blocks};
    run.start(one,drop_phase,drops,1); run.tick(drop_hooks);
    assert(drop_fixture.next==2 && !run.finished());
    run.tick(drop_hooks); assert(drop_fixture.next==2);
    drop_fixture.alive[1]=false; run.tick(drop_hooks); assert(drop_fixture.next==3);
    run.tick(drop_hooks); assert(drop_fixture.next==3);
    drop_fixture.alive[0]=false; run.tick(drop_hooks);
    assert(run.finished() && drop_fixture.alive[2]);
    const StageEntry mixed[] = {{31,0,50,10,0,0,0,0,0,100,1}, {32,0,50,2,0,0,0,0,0,200,2}};
    const StagePhase mixed_phase[] = {{900,-1,0,2,0}};
    ReuseFixture reuse; StageHooks reuse_hooks{&reuse,reuse_spawn,reuse_alive,nullptr,nullptr,nullptr,clear_reserved,reserve_slot};
    run.start(one,mixed_phase,mixed,1); run.tick(reuse_hooks); assert(reuse.count==3);
    reuse.alive[0]=reuse.alive[1]=false; run.tick(reuse_hooks);
    assert(reuse.count==5 && reuse.alive[0] && !reuse.alive[1] && reuse.alive[3]);
    const StageEntry many[] = {{122,0,500,1,0,0,0,0,0,1000,0}};
    Fixture many_fixture; StageHooks many_hooks{&many_fixture,spawn,alive,phase,not_blocking};
    run.start(one,single,many,1); run.tick(many_hooks);
    assert(many_fixture.next==10 && run.finished());
    const StageEntry capped[] = {{122,0,500,1,0,0,0,0,0,5000,0}};
    Fixture capped_fixture; StageHooks capped_hooks{&capped_fixture,spawn,alive,phase,not_blocking};
    run.start(one,single,capped,1); run.tick(capped_hooks); assert(capped_fixture.next==40 && run.finished());
    Fixture limited; limited.capacity=7; StageHooks limited_hooks{&limited,spawn,alive,phase,not_blocking};
    run.start(one,single,many,1); run.tick(limited_hooks); assert(limited.next==7 && !run.finished());
    limited.capacity=10; run.tick(limited_hooks); assert(limited.next==10 && !run.finished());
    run.tick(limited_hooks); assert(run.finished());
}

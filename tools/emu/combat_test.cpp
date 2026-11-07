// Synthetic combat interactions; no original game assets are needed.
// g++ -std=gnu++20 -fsanitize=address,undefined -Igame tools/emu/combat_test.cpp game/combat.cpp -o /tmp/combat_test && /tmp/combat_test
#define private public
#include "world.hpp"
#undef private
#include <cassert>
#include <initializer_list>
using namespace lf2;
static bool scripted_random = false;
static int random_calls = 0;
namespace lf2 {
int World::rnd(int) { return scripted_random && random_calls++ == 0 ? 1 : 0; }
void World::play(int) {}
int World::spawn(const ObjDef*,int,int,int,int,fx,fx,fx,fx,fx,fx,int) { return -1; }
void World::change_character(Ent&, const Group*, int, bool) {}
}
int main() {
 World w{}; ObjHeader h{}; h.nframes=256; Frame f[256]{}; Bdy b{0,0,20,20}; Itr i{}; Cpoint cp{};
 ObjDef d{&h,f,&b,&i,&cp}; f[0].nbdy=1; f[0].nitr=1; f[0].wait=1; i.w=i.h=20; i.vrest=5;
 auto setup=[&](int kind) { for(auto &e:w.ents) e=Ent{}; w.ents[0].live=w.ents[1].live=true; w.ents[0].def=w.ents[1].def=&d; w.ents[0].team=1; w.ents[1].team=2; i.kind=kind; w.ents[0].frame_at_pass=w.ents[1].frame_at_pass=0; };
 setup(10); w.detect(); w.apply(0); assert(w.ents[1].frame==182 && w.ents[1].fall_damage==-20 && w.ents[1].vy==FX(-6) && w.ents[1].y==0);
 setup(15); w.detect(); w.apply(0); assert(w.ents[1].vx==FX(1) && w.ents[1].vy==FX(-6));
 setup(16); i.injury=30; w.detect(); w.apply(0); assert(w.ents[1].hp==470 && w.ents[1].frame==200 && w.ents[1].pair_rest[0]==5);
 setup(16); i.injury=30; w.ents[1].dark_hp=2; w.detect(); w.apply(0); assert(w.ents[1].dark_hp==-8);
 setup(8); i.injury=48; i.dvx=10; w.ents[1].team=1; w.ents[1].blink=1; w.detect(); w.apply(0); assert(w.ents[1].regen_timer==1048 && w.ents[0].frame==10);
 setup(9); w.ents[1].kind=3; w.ents[1].team=1; w.detect(); w.apply(0); assert(w.ents[1].frame==30 && w.ents[1].hit_this_frame);
 setup(6); w.detect(); w.apply(0); assert(w.ents[1].super_window==3);
 setup(14); w.ents[0].x=FX(10); w.ents[1].vx=FX(2); w.ents[1].blink=1; w.detect(); w.apply(0); assert(w.ents[1].blocked_right);
 setup(0); h.id=37; i.injury=100; i.bdefend=10; i.effect=0; w.detect(); w.apply(0); assert(w.ents[1].hp==490);
 setup(0); i.effect=2; w.detect(); w.apply(0); assert(w.ents[1].hp==400);
 setup(4); h.id=0; i.effect=0; i.dvx=4; i.injury=30; w.ents[0].fall_damage=10; w.ents[0].vx=-FX(1); w.ents[0].team=2; w.detect(); w.apply(0); assert(w.ents[1].hp==470 && w.ents[1].ix<0);
 setup(0); f[0].cp=0; cp.kind=2; w.ents[1].caught_by=2; w.ents[2].live=true; w.ents[2].def=&d; w.ents[2].catching=1; cp.hurtable=0; w.detect(); w.apply(0); assert(w.ents[1].hp==500);
 f[0].cp=-1; f[0].state=3006; setup(0); w.ents[0].kind=w.ents[1].kind=3; w.detect(); w.apply(0); assert(w.ents[0].frame==20 && w.ents[1].frame==20);
 f[0].state=3000; setup(0); w.ents[0].kind=w.ents[1].kind=3; w.detect(); w.apply(0); assert(w.ents[0].frame==10 && w.ents[1].frame==20);
 f[0].state=0; setup(0); i.effect=0; i.injury=100; i.bdefend=0; w.ents[1].armor=200; w.ents[1].army_side=1;
 w.ents[1].hp=40; w.detect(); w.apply(0); assert(w.ents[1].hp==-10 && w.army_damage[1]==50 && w.army_deaths[1]==1);
 setup(0); i.injury=100; w.ents[1].armor=200; w.ents[1].army_side=0; f[0].state=7; w.ents[0].facing=1; w.ents[0].x=FX(20);
 w.detect(); w.apply(0); assert(w.ents[1].hp==495 && w.army_damage[0]==5);
 f[0].state=0; setup(0); i.injury=30; i.dvx=4; i.dvy=0; i.fall=20; i.effect=0;
 w.ents[1].kind=2; w.ents[1].drop_counter=100; w.plain_hit(0,1,i);
 assert(w.ents[1].frame==20 && w.ents[1].hit_count==0 && w.ents[1].iy==0);
 assert(w.ents[0].pair_rest[1]==3 && w.ents[1].hit_this_frame && w.ents[1].drop_counter==70 && w.ents[1].dark_hp==490);
 setup(0); i.fall=60; w.ents[1].kind=2; w.plain_hit(0,1,i);
 assert(w.ents[1].hit_count==1 && w.ents[1].iy==-FX(7) && w.ents[0].pair_rest[1]==19);
 setup(0); i.fall=60; i.dvy=1; w.ents[1].kind=1; w.ents[1].fall=45;
 w.ents[1].y=FX(0.75); w.ents[1].iy=-FX(0.5); w.plain_hit(0,1,i);
 assert(w.ents[1].iy==FX(0.5) && w.ents[1].fall==0); i.dvy=0;
 setup(0); w.ents[1].kind=6; w.plain_hit(0,1,i);
 assert(w.ents[1].hp==500 && w.ents[0].pair_rest[1]==30);
 setup(0); f[0].state=1002; i.fall=20; i.dvx=4; w.ents[0].kind=1; w.ents[0].vz=FX(3);
 w.plain_hit(0,1,i); assert(w.ents[0].vx==-FX(2) && w.ents[0].vy==-FX(4));
 setup(0); f[0].state=1002; w.ents[0].kind=1; w.ents[0].vz=FX(3);
 w.defended_hit(0,1,i); assert(w.ents[0].vx==-FX(1) && w.ents[0].vy==-FX(4) && w.ents[0].vz==-FX(2));
 setup(0); f[0].state=2000; w.ents[0].kind=2; w.ents[0].vx=FX(10); w.ents[0].vz=FX(5); w.ents[1].x=FX(10);
 w.defended_hit(0,1,i); assert(w.ents[0].vx==FX(4) && w.ents[0].vz==FX(2));
 ObjHeader column_header{}; column_header.id=212; column_header.nframes=256;
 ObjDef column{&column_header,f,&b,&i,&cp};
 setup(0); f[0].state=0; w.ents[1].team=1; w.ents[1].kind=3; w.ents[1].def=&column; i.effect=0;
 assert(w.eligible(w.ents[0],w.ents[1],i,f[0]));
 setup(0); w.ents[1].team=1; f[0].state=18; i.effect=0;
 assert(w.eligible(w.ents[0],w.ents[1],i,f[0]));
 i.effect=21; assert(!w.eligible(w.ents[0],w.ents[1],i,f[0]));
 setup(0); f[0].state=19; f[1].state=18; w.ents[0].state_frame=0; w.ents[1].state_frame=1; i.effect=2;
 assert(!w.eligible(w.ents[0],w.ents[1],i,f[0]));
 setup(0); f[0].state=2000; i.effect=0; i.dvx=8; w.ents[0].x=FX(20); w.ents[1].x=0;
 w.defended_hit(0,1,i); assert(w.ents[1].ix==-FX(8));
 setup(0); f[0].state=0; i.effect=22; w.ents[0].x=FX(20); w.ents[1].x=0;
 w.defended_hit(0,1,i); assert(w.ents[1].ix==FX(8));
 setup(0); i.effect=23; w.ents[0].x=0; w.ents[1].x=FX(20); w.ents[1].y=-FX(10);
 w.defended_hit(0,1,i); assert(w.ents[1].ix==-FX(8));
 setup(0); w.ents[0].vx=FX(5); w.ents[1].vy=-FX(3); w.ents[0].ix=FX(7); w.ents[1].iz=FX(2);
 w.ball_vs_ball(0,1); assert(w.ents[0].frame==20 && w.ents[1].frame==20);
 assert(w.ents[0].vx==FX(5) && w.ents[1].vy==-FX(3) && w.ents[0].ix==0 && w.ents[1].iz==0);
 // Stage objects can be struck by a weapon held by an allied character.
 f[0].state=0; b.kind=1003; w.stage_mode=true; setup(5);
 w.ents[0].kind=1; w.ents[0].holder=2; w.ents[2].live=true; w.ents[2].def=&d;
 w.ents[2].kind=0; w.ents[2].team=1; w.detect(); assert(w.ents[0].list_n==1 && w.ents[0].list_who[0]==1);
 setup(5); w.ents[0].kind=1; w.ents[0].holder=2; w.ents[2].live=true; w.ents[2].def=&d;
 w.ents[2].kind=0; w.ents[2].team=2; w.detect(); assert(w.ents[0].list_n==0);
 w.stage_mode=false; b.kind=0; f[1]=f[0]; f[1].state=1004;
 setup(0); w.ents[1].kind=1; w.ents[1].frame=1; i.effect=0; w.detect(); assert(w.ents[0].list_n==0);
 setup(10); w.ents[1].kind=1; w.ents[1].frame=1; w.detect(); assert(w.ents[0].list_n==1);
 // A fighter's own ball may hit them while they are frozen.
 f[1].state=13; setup(0); w.ents[0].kind=3; w.ents[0].owner=1; w.ents[1].team=1;
 assert(w.eligible(w.ents[0],w.ents[1],i,f[1]));
 // A held weapon chooses the victim nearest its holder, not its own sprite.
 setup(5); i.vrest=0; i.x=-50; i.w=100; w.ents[0].kind=1; w.ents[0].holder=2; w.ents[0].x=FX(50);
 w.ents[1].x=FX(45); w.ents[2].live=true; w.ents[2].def=&d; w.ents[2].team=1; w.ents[2].x=0;
 w.ents[3].live=true; w.ents[3].def=&d; w.ents[3].team=2; w.ents[3].x=FX(5);
 w.detect(); assert(w.ents[0].list_n==1 && w.ents[0].list_who[0]==3);
 // Whirlwinds list all resting items even without per-victim rest.
 setup(10); i.vrest=0; i.x=-50; i.w=100; w.ents[0].x=FX(50); w.ents[1].kind=1; w.ents[1].frame=1;
 f[1].state=1004; w.ents[1].x=FX(20); w.ents[3].live=true; w.ents[3].def=&d; w.ents[3].kind=1;
 w.ents[3].team=2; w.ents[3].frame=1; w.ents[3].x=FX(40);
 w.detect(); assert(w.ents[0].list_n==2);
 // Whirlwinds lift and pull weapons, with a smaller lift for heavy items.
 f[1]=f[0]; f[1].state=1003; i.x=0; i.w=20; i.vrest=5; i.effect=0;
 setup(10); w.ents[0].kind=3; w.ents[1].kind=1; w.ents[1].frame=1;
 w.ents[1].y=-FX(5); w.ents[1].vy=-FX(2); w.ents[1].vx=FX(10);
 w.detect(); w.apply(0); assert(w.ents[1].frame==0 && w.ents[1].vy==-FX(5) && w.ents[1].vx==FX(10)*100/107);
 setup(15); w.ents[0].kind=3; w.ents[1].kind=2; w.ents[1].frame=1;
 w.ents[1].y=-FX(5); w.ents[1].vy=-FX(2); w.ents[1].x=FX(5); w.detect(); w.apply(0);
 assert(w.ents[1].frame==0 && w.ents[1].vx==-FX(1) && w.ents[1].vy==-FX(2)-FX(2.3));
 setup(16); w.ents[0].kind=3; w.ents[1].kind=4; w.ents[1].frame=1; w.ents[1].x=FX(5);
 w.detect(); w.apply(0); assert(w.ents[1].frame==0 && w.ents[1].vx==-FX(1) && w.ents[1].y==0 && w.ents[1].vy==-FX(6));
 // Freezing a fighter releases a carried heavy weapon and sets the collision rests.
 setup(16); i.injury=30; w.ents[1].held_item=2; w.ents[2].live=true; w.ents[2].def=&d;
 w.ents[2].kind=2; w.ents[2].holder=1; w.ents[2].x=FX(1000); w.detect(); w.apply(0);
 assert(w.ents[1].held_item==-1 && w.ents[2].holder==-1 && w.ents[2].vy==-FX(1));
 assert(w.ents[0].pair_rest[2]==45 && w.ents[1].pair_rest[2]==30);
 // Reflect attacks hit thrown objects as ordinary strikes instead of ignoring them.
 setup(9); f[1]=f[0]; f[1].state=1002; i.injury=30; i.fall=20; i.bdefend=0;
 w.ents[0].kind=3; w.ents[1].kind=1; w.ents[1].frame=1; w.detect(); w.apply(0);
 assert(w.ents[1].hp==470 && w.ents[1].hit_this_frame && w.ents[0].hp==500);
 setup(9); w.ents[1].kind=3; w.ents[1].walk_phase=9; w.detect(); w.apply(0);
 assert(w.ents[1].walk_phase==0 && w.ents[1].walk_owner==0);
 // Ordinary hits damage balls and apply their hit rest before redirecting them.
 setup(0); f[1]=f[0]; f[1].state=3000; i.injury=80; i.vrest=5;
 w.ents[1].kind=3; w.ents[1].frame=1; w.detect(); w.apply(0);
 assert(w.ents[1].hp==420 && w.ents[1].dark_hp==474 && w.ents[1].hit_count==1);
 assert(w.ents[1].pair_rest[0]==5 && w.ents[1].hit_lag==-3 && w.ents[1].walk_owner==0);
 // Returning and guided projectiles can be blocked while facing the same way.
 ObjHeader attacker_header{}; attacker_header.nframes=256;
 ObjDef attacker_def{&attacker_header,f,&b,&i,&cp};
 f[0].state=0; f[1]=f[0]; f[1].state=7; h.id=0;
 const int blockable_ids[] = {124,220,221,222};
 for (int id : blockable_ids) {
     setup(0); attacker_header.id=id; w.ents[0].def=&attacker_def; w.ents[1].frame=1;
     i.injury=100; i.dvx=4; i.bdefend=10; w.detect(); w.apply(0); assert(w.ents[1].hp==490);
 }
 setup(0); attacker_header.id=30; w.ents[0].def=&attacker_def; w.ents[1].frame=1;
 w.detect(); w.apply(0); assert(w.ents[1].hp==400);
 // A swung stick bats an incoming bottle away at a minimum speed.
 setup(0); attacker_header.id=100; w.ents[0].def=&attacker_def; w.ents[0].holder=2;
 w.ents[2].live=true; w.ents[2].def=&d; w.ents[1].kind=6; w.ents[1].vx=-FX(2); i.dvx=1;
 w.plain_hit(0,1,i); assert(w.ents[1].ix==FX(10));
 setup(0); attacker_header.id=100; w.ents[0].def=&attacker_def; w.ents[0].holder=2; w.ents[0].facing=1;
 w.ents[2].live=true; w.ents[2].def=&d; w.ents[1].kind=4; w.ents[1].vx=FX(2);
 w.plain_hit(0,1,i); assert(w.ents[1].ix==-FX(10));
 // Heavy thrown objects push away from their position, regardless of facing.
 setup(0); f[0].state=2000; w.ents[0].kind=2; w.ents[0].x=FX(20); w.ents[1].kind=1; i.dvx=4;
 w.plain_hit(0,1,i); assert(w.ents[1].ix==-FX(4));
 // Slow weapons receive a minimum push; downward impulses at ground level bounce.
 setup(0); f[0].state=0; w.ents[0].facing=1; w.ents[1].kind=1; i.dvx=0; i.dvy=3;
 w.plain_hit(0,1,i); assert(w.ents[1].ix==-FX(5) && w.ents[1].iy==FX(12) && w.ents[1].defend_pts==45);
 setup(0); f[0].state=2000; w.ents[0].kind=2; w.ents[0].facing=1; w.ents[1].kind=2; i.fall=60;
 w.plain_hit(0,1,i); assert(w.ents[1].ix==FX(5) && w.ents[1].iy==FX(12));
 // Heavy weapons also push fighters away from their actual position.
 setup(0); f[1]=f[0]; f[1].state=0; w.ents[0].kind=2; w.ents[0].x=FX(20); i.dvx=4; i.dvy=0;
 w.ents[1].frame=w.ents[1].frame_at_pass=1; i.effect=0; i.fall=20;
 w.plain_hit(0,1,i); assert(w.ents[1].ix==-FX(4));
 // Freeze's ball passes through breakable balls while converting them to ice.
 ObjHeader freeze_header{}; freeze_header.id=209; freeze_header.nframes=256;
 ObjHeader ball_header{}; ball_header.id=203; ball_header.nframes=256;
 ObjDef freeze_def{&freeze_header,f,&b,&i,&cp}, ball_def{&ball_header,f,&b,&i,&cp};
 // A flying heavy object pushes a missile according to their positions.
 setup(0); f[0].state=2000; f[1]=f[0]; f[1].state=3006;
 w.ents[0].kind=2; w.ents[0].x=FX(20); w.ents[1].def=&ball_def;
 w.ents[1].kind=3; w.ents[1].frame=w.ents[1].frame_at_pass=w.ents[1].state_frame=1;
 w.plain_hit(0,1,i); assert(w.ents[1].ix==-FX(4));
 setup(0); f[0].state=3000; f[10].state=3001; f[10].dvy=3; f[40].state=3005;
 i.injury=10; i.fall=20; i.dvx=1; i.vrest=5;
 w.ents[0].def=&freeze_def; w.ents[0].kind=3; w.ents[1].def=&ball_def; w.ents[1].kind=3;
 w.plain_hit(0,1,i); assert(w.ents[1].def==&freeze_def && w.ents[1].frame==40 && w.ents[0].frame==0);
 // Freeze's physical strike converts a rebounding ball without an artificial state transition.
 Group freeze_group{}; freeze_group.count=1; freeze_group.objs[0]=freeze_def;
 setup(0); f[0].state=0; f[1]=f[0]; f[1].state=3000; f[30].state=3003;
 attacker_header.id=8; w.ents[0].def=&attacker_def; w.ents[0].data_group=&freeze_group;
 w.ents[1].def=&ball_def; w.ents[1].kind=3; w.ents[1].frame=w.ents[1].frame_at_pass=w.ents[1].state_frame=1;
 w.plain_hit(0,1,i);
 assert(w.ents[1].def==&freeze_group.objs[0] && w.ents[1].frame==30 && w.ents[1].prev_frame==30 && w.ents[1].state_frame==30);
 // A held ice sword freezes breakable balls even when the hit carries a burn effect.
 setup(0); attacker_header.id=213; i.effect=2; w.ents[0].def=&attacker_def;
 w.ents[0].data_group=&freeze_group; w.ents[0].kind=1; w.ents[0].holder=2;
 w.ents[2].live=true; w.ents[2].def=&d; w.ents[2].team=1; w.ents[2].owner=2;
 w.ents[1].def=&ball_def; w.ents[1].kind=3; w.ents[1].frame=w.ents[1].frame_at_pass=w.ents[1].state_frame=1;
 w.plain_hit(0,1,i); assert(w.ents[1].def==&freeze_group.objs[0] && w.ents[1].frame==20);
 i.effect=0;
 f[0].state=3000;
 setup(0); attacker_header.id=200; w.ents[0].def=&attacker_def; w.ents[0].kind=3;
 w.ents[1].def=&ball_def; w.ents[1].kind=3; w.plain_hit(0,1,i);
 assert(w.ents[0].frame==10 && w.ents[0].vx==0 && w.ents[0].vz==FX(3));
 // The impact frame determines whether a flying missile can be redirected.
 setup(0); f[1]=f[0]; f[1].state=3006; f[10].state=3005; f[20].state=0;
 w.ents[0].def=&attacker_def; w.ents[0].kind=3; w.ents[1].def=&ball_def;
 w.ents[1].kind=3; w.ents[1].frame=w.ents[1].frame_at_pass=1;
 w.plain_hit(0,1,i); assert(w.ents[0].frame==10 && w.ents[1].frame==20 && w.ents[1].team==1);
 // Projectile impacts on weapons enter the same hitting animation as other impacts.
 setup(0); f[10].state=3001; w.ents[0].def=&attacker_def; w.ents[0].kind=3; w.ents[0].vx=FX(4);
 w.ents[1].kind=1; w.plain_hit(0,1,i);
 assert(w.ents[0].frame==10 && w.ents[0].vx==0 && w.ents[0].vz==FX(3));
 // A knockdown gives a carried item time to separate from both fighters.
 setup(0); f[0].state=0; i.fall=80; w.ents[1].held_item=2;
 w.ents[2].live=true; w.ents[2].def=&d; w.ents[2].kind=1; w.ents[2].holder=1;
 w.plain_hit(0,1,i); assert(w.ents[0].pair_rest[2]==45 && w.ents[1].pair_rest[2]==30);
 // Striking a weapon creates a visible impact spark.
 for (auto& spark : w.sparks) spark=Spark{};
 setup(0); f[0].state=0; w.ents[1].kind=1; w.ents[1].wait=7; w.plain_hit(0,1,i);
 assert(w.sparks[0].live && w.ents[1].wait==7);
 // Henry's arrow disappears on a fighter, while Rudolf's thrown weapon bounces.
 setup(0); f[0].state=1002; f[1]=f[0]; f[1].state=0; attacker_header.id=201;
 w.ents[0].def=&attacker_def; w.ents[0].kind=1; w.ents[1].frame=w.ents[1].frame_at_pass=1;
 w.plain_hit(0,1,i); assert(!w.ents[0].live);
 setup(0); attacker_header.id=202; w.ents[0].def=&attacker_def; w.ents[0].kind=1;
 w.ents[1].frame=w.ents[1].frame_at_pass=1; w.plain_hit(0,1,i);
 assert(w.ents[0].live && w.ents[0].vy==-FX(4));
 // Empty bottles can be picked up and used as throwable objects.
 for (int kind : {2,7}) {
     setup(kind); f[0].nitr=1; w.ents[0].list_n=1; w.ents[0].list_itr[0]=0; w.ents[0].list_who[0]=1;
     w.ents[1].kind=6; w.ents[1].hp=0; w.ents[1].drop_counter=-1;
     w.apply(0); assert(w.ents[0].held_item==1 && w.ents[1].holder==0 && w.ents[1].drop_counter==0);
 }
 // A heavy pickup can replace an item already held; rolling pickup cannot.
 setup(2); f[0].state=0; f[1]=f[0]; f[1].state=2004;
 w.ents[1].frame=1; w.ents[1].kind=2; w.ents[0].held_item=2; w.ents[0].keys.attack=true;
 w.ents[2].live=true; w.ents[2].def=&d; w.ents[2].holder=0; w.ents[2].x=FX(100);
 w.detect(); assert(w.ents[0].list_n==1); w.apply(0);
 assert(w.ents[0].held_item==1 && w.ents[1].holder==0 && w.ents[0].frame==116);
 setup(7); f[1].state=1004; w.ents[1].frame=1; w.ents[1].kind=1;
 w.ents[0].held_item=2; w.ents[0].keys.attack=true; w.detect(); assert(w.ents[0].list_n==1);
 w.apply(0); assert(w.ents[0].held_item==2 && w.ents[1].holder==-1);
 // Impact sparks use the current animation's center and the attack's leading edge.
 for (auto& spark : w.sparks) spark=Spark{};
 setup(0); f[0].cx=5; f[0].cy=30; i.x=10; i.y=5; i.w=i.h=20;
 w.ents[0].x=FX(100); w.ents[0].y=-FX(10); w.ents[0].z=FX(400); w.ents[1].x=FX(200);
 w.add_spark(w.ents[0],w.ents[1],i,0);
 assert(w.sparks[0].x==121 && w.sparks[0].y==-29 && w.sparks[0].z==400 && w.sparks[0].owner==0);
 w.ents[0].facing=1; w.ents[1].x=0; w.add_spark(w.ents[0],w.ents[1],i,0);
 assert(w.sparks[1].x==71);
 w.ents[1].z=FX(401); w.add_spark(w.ents[0],w.ents[1],i,0); assert(w.sparks[2].owner==1);
 w.ents[1].z=FX(400); w.add_spark(w.ents[1],w.ents[0],i,0); assert(w.sparks[3].owner==1);
 w.ents[0].live=false; w.add_spark(w.ents[0],w.ents[1],i,0); assert(w.sparks[4].owner==1);
 // A held weapon's attack cooldown resets while its holder is not attacking.
 setup(5); f[0].state=1001; f[1]=f[0]; f[1].state=0; f[1].wp_attacking=0;
 w.ents[0].kind=1; w.ents[0].holder=2; w.ents[0].rest=7;
 w.ents[2].live=true; w.ents[2].def=&d; w.ents[2].frame=1;
 w.detect(); assert(w.ents[0].rest==0);
 w.ents[0].rest=7; f[1].wp_attacking=1; w.detect(); assert(w.ents[0].rest==7);
 // Every overlapping body area is listed, with victim rest preventing duplicate damage.
 Bdy bodies[2]={{0,0,20,20,0},{0,0,20,20,0}}; ObjDef multi{&h,f,bodies,&i,&cp};
 setup(0); f[0].state=0; f[0].cx=f[0].cy=0; f[0].nbdy=2; i.x=i.y=0; i.w=i.h=20;
 i.effect=0; i.vrest=5; i.injury=30; w.ents[0].def=w.ents[1].def=&multi;
 w.detect(); assert(w.ents[0].list_n==2); w.apply(0); assert(w.ents[1].hp==470);
 // Collision pairs consume tie-break rolls in pair order, including reverse attacks.
 Frame pair_frames[3]{}; ObjHeader pair_headers[3]{}; Itr pair_itrs[2]{}; Bdy pair_body{0,0,20,20,0};
 for (int n=0;n<3;++n) { pair_headers[n].nframes=1; pair_frames[n].nbdy=1; }
 pair_frames[0].nitr=pair_frames[1].nitr=1;
 pair_itrs[0].x=-20; pair_itrs[0].w=60; pair_itrs[0].h=20;
 pair_itrs[1].x=-110; pair_itrs[1].w=20; pair_itrs[1].h=20;
 ObjDef pair_defs[3]={{&pair_headers[0],&pair_frames[0],&pair_body,&pair_itrs[0],&cp},
                      {&pair_headers[1],&pair_frames[1],&pair_body,&pair_itrs[1],&cp},
                      {&pair_headers[2],&pair_frames[2],&pair_body,&pair_itrs[0],&cp}};
 for (auto& ent : w.ents) ent=Ent{};
 for (int n=0;n<4;++n) { w.ents[n].live=true; w.ents[n].def=&pair_defs[n<2?n:2]; w.ents[n].team=n==0?1:2; }
 w.ents[1].x=FX(100); w.ents[1].nearest=100; w.ents[2].x=-FX(10); w.ents[3].x=FX(10);
 scripted_random=true; random_calls=0; w.detect(); scripted_random=false;
 assert(random_calls==2 && w.ents[1].list_n==0 && w.ents[0].list_n==1 && w.ents[0].list_who[0]==3);
 // An effect-21 hit stops if an earlier interaction already set the victim on fire.
 setup(0); f[0].state=0; f[0].nbdy=1; f[1]=f[0]; f[1].state=18; i.effect=21; i.injury=30;
 w.ents[1].frame=1; w.ents[1].frame_at_pass=0; w.ents[2].live=true; w.ents[2].def=&d; w.ents[2].team=2;
 w.ents[0].list_n=2; w.ents[0].list_who[0]=1; w.ents[0].list_who[1]=2;
 w.ents[0].list_itr[0]=w.ents[0].list_itr[1]=0; w.apply(0);
 assert(w.ents[1].hp==500 && w.ents[2].hp==500);
 // Defended hits convert vrest to a signed byte before applying the twelve-frame cap.
 setup(0); f[0].state=0; i.vrest=130; i.effect=0; w.defended_hit(0,1,i);
 assert(w.ents[1].pair_rest[0]==-126);
 setup(0); i.vrest=20; w.defended_hit(0,1,i); assert(w.ents[1].pair_rest[0]==12);
 // Defended-hit direction uses integer positions, including subpixel ties.
 setup(0); f[0].state=2000; i.dvx=4; i.effect=0; w.ents[0].kind=2;
 w.ents[0].x=FX(20); w.ents[0].vx=FX(10); w.ents[1].x=FX(20.5);
 w.defended_hit(0,1,i); assert(w.ents[1].ix==-FX(4) && w.ents[0].vx==FX(10));
 setup(0); f[0].state=0; i.effect=22; w.ents[0].x=FX(20); w.ents[1].x=FX(20.5);
 w.defended_hit(0,1,i); assert(w.ents[1].ix==FX(4));
 // Freeing a prisoner stops the current attack's remaining target list.
 ObjHeader prisoner_header{}; prisoner_header.id=300; prisoner_header.nframes=256;
 Bdy prisoner_body{0,0,20,20,1003}; ObjDef prisoner_def{&prisoner_header,f,&prisoner_body,&i,&cp};
 setup(0); f[0].state=0; f[0].cp=-1; f[1]=f[0]; f[1].nbdy=1; i.effect=0; i.injury=30;
 w.ents[1].def=&prisoner_def; w.ents[1].kind=5; w.ents[1].frame=1; w.ents[1].wait=7;
 w.ents[2].live=true; w.ents[2].def=&d; w.ents[2].team=2;
 w.ents[0].list_n=2; w.ents[0].list_who[0]=1; w.ents[0].list_who[1]=2;
 w.ents[0].list_itr[0]=w.ents[0].list_itr[1]=0; w.apply(0);
 assert(w.ents[1].frame==3 && w.ents[1].team==1 && w.ents[1].wait==7 && w.ents[1].defend_pts==45);
 assert(w.ents[2].hp==500);
}

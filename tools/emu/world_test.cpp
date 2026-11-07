// Synthetic fusion, teleport and regeneration checks; no installer needed.
// g++ -std=gnu++20 -fsanitize=address,undefined -Itools/emu/fixtures -Igame tools/emu/world_test.cpp game/world.cpp game/combat.cpp game/ai.cpp -o /tmp/world_test && /tmp/world_test
#define private public
#include "world.hpp"
#undef private
#include <cassert>
#include <initializer_list>
using namespace lf2;
static Group firzen{};
static Group louis_ex{};
namespace lf2 { const Group& roster_group(int n) { return n == 0 ? firzen : louis_ex; } const Group& weapon_group() { return firzen; } }
int main() {
 static Frame f[300]{}; static ObjHeader h1{},h2{},h3{}; static Group g1{},g2{};
 for (auto& frame : f) frame.cp = -1;
 h1.id=7; h2.id=8; h3.id=51; h1.nframes=h2.nframes=h3.nframes=300; f[9].state=2;
 g1.objs[0]={&h1,f,nullptr,nullptr,nullptr}; g2.objs[0]={&h2,f,nullptr,nullptr,nullptr}; firzen.objs[0]={&h3,f,nullptr,nullptr,nullptr};
 g1.count=g2.count=firzen.count=louis_ex.count=1; louis_ex.objs[0]=firzen.objs[0];
 World w{}; w.init(Stage{},1); w.fighters=2; w.groups[0]=&g1; w.groups[1]=&g2;
 auto &a=w.ents[0], &b=w.ents[1]; a.live=b.live=true; a.def=&g1.objs[0]; b.def=&g2.objs[0]; a.data_group=&g1; b.data_group=&g2; a.group=0; b.group=1; a.team=b.team=1; a.frame=b.frame=9; a.hp=100; b.hp=120; a.x=FX(120); b.x=FX(100); a.z=b.z=FX(400);
 a.stage_max_hp=750; w.stage_mode=true; w.update_fusion(); assert(a.def==&g1.objs[0] && b.live);
 w.stage_mode=false; w.update_fusion();
 assert(a.def->hdr->id==51 && !b.live && a.hp==220 && a.dark_hp==750 && a.frame==290 && w.groups[0]==&firzen);
 b.team=2; b.keys.attack=true; b.fall=50; b.held_item=max_fighters; b.picture_offset=140; b.ai.enabled=true; b.ai.aggression=73;
 a.merge_timer=1; a.frame=0; w.update_fusion();
 assert(a.def->hdr->id==7 && b.live && b.team==1 && a.hp==110 && b.hp==110 && a.dark_hp==375 && b.dark_hp==375 && w.groups[0]==&g1 && a.merge_timer==900);
 assert(!b.keys.attack && b.fall==0 && b.held_item==-1 && b.picture_offset==0 && b.merge_timer==0 && b.ai.enabled && b.ai.aggression==73);
 a.merge_timer=b.merge_timer=0; a.frame=b.frame=9; a.x=FX(120); b.x=FX(100); a.hp=b.hp=100;
 w.update_fusion(); assert(!b.live && a.merge_original); a.frame=290; w.finish_round();
 assert(b.live && a.def->hdr->id==7 && a.hp==100 && b.hp==100 && !a.merge_original);
 f[0].state=400; b.team=2; a.frame=0; a.facing=0; a.x=FX(500); b.x=FX(700); b.z=FX(410); a.vx=FX(3); w.shake=0; w.frame_rules(0); assert(a.x==FX(580) && a.z==FX(411) && a.vx==0);
 a.x=FX(500); w.teleport_phase=1; w.shake=0; w.frame_rules(0); assert(a.x==FX(500));
 w.teleport_phase=0; w.shake=3; w.frame_rules(0); assert(a.x==FX(580)); w.shake=0;
 a.hp=100; a.dark_hp=200; a.fall_damage=-20; w.counter_12=0; w.regenerate(a); assert(a.hp==92 && a.dark_hp==197);
 a.fall_damage=0; a.follow=0; a.mp=150; w.counter_3=0; w.regenerate(a); assert(a.mp==150);
 a.follow=-1; w.regenerate(a); assert(a.mp>150);
 a.hp=a.dark_hp=100; a.mp=499; w.regenerate(a); assert(a.mp==504);
 static Frame weapon_frames[100]{}; static ObjHeader wh{}; static Bdy body{0,0,20,20}; static Itr pick{};
 for (auto& frame : weapon_frames) frame.cp = -1;
 wh.id=100; wh.kind=1; wh.nframes=100; wh.weapon_hp=40; wh.weapon_drop_hurt=5;
 ObjDef weapon{&wh,weapon_frames,&body,nullptr,nullptr,nullptr};
 f[0].state=0; f[0].nbdy=0; f[0].nitr=1; f[0].itr_off=0; pick.kind=2; pick.w=pick.h=20;
 g1.objs[0].itr=&pick; a.frame=0; a.frame_at_pass=0; a.x=FX(100); a.y=0; a.z=FX(400); a.keys.attack=true; a.prev.attack=false; a.held_item=-1;
 auto &item=w.ents[max_fighters]; item=Ent{}; item.live=true; item.def=&weapon; item.kind=1; item.x=a.x; item.z=a.z; item.frame=60; weapon_frames[60].state=1004; weapon_frames[60].nbdy=1;
 w.detect(); w.apply(0); assert(a.held_item==max_fighters && item.holder==0 && a.frame==115);
 f[115].wp_action=1; f[115].wp_x=8; f[115].wp_y=4; weapon_frames[1].wp_x=2; weapon_frames[1].cx=3;
 w.carry_weapons(); assert(item.frame==1 && item.x==FX(109));
 f[115].wp_dvx=12; f[115].wp_dvy=-3; w.carry_weapons(); assert(a.held_item==-1 && item.holder==-1 && item.thrower==0 && item.frame==40 && item.vx==FX(12));
 item.y=FX(1); item.vy=FX(5); item.drop_counter=40; weapon_frames[40].state=1002; w.physics(max_fighters); assert(item.frame==70 && item.y==0 && item.drop_counter==35);
 auto& transforming_follower=w.ents[7]; transforming_follower=Ent{}; transforming_follower.live=true;
 transforming_follower.def=&g1.objs[0]; transforming_follower.data_group=&g1; transforming_follower.follow=0; transforming_follower.wait=6;
 a.transform_target=&g2; f[0].state=501; a.frame=0; a.wait=9; w.frame_rules(0);
 assert(a.def==&g2.objs[0] && a.transform_original==&g1 && w.groups[0]==&g2 && a.wait==9);
 assert(transforming_follower.def==&g2.objs[0] && transforming_follower.wait==6);
 a.seq[8]=3; a.y=0; a.hp=100; w.recognize(a,8); assert(a.def==&g2.objs[0] && a.seq[8]==3);
 w.after_physics(0); assert(a.def==&g1.objs[0] && a.transform_original==nullptr && a.frame==245 && w.groups[0]==&g1);
 transforming_follower.live=false;
 a.frame=0; w.frame_rules(0); w.finish_round();
 assert(a.def==&g1.objs[0] && !a.transform_original && a.transform_target==&g2);
 static Frame catcher_frames[4]{}, victim_frames[4]{}; static Cpoint catcher_cp{}, victim_cp{};
 for (auto& frame : catcher_frames) frame.cp=-1;
 for (auto& frame : victim_frames) frame.cp=-1;
 catcher_frames[1].cp=0; victim_frames[1].cp=0; catcher_cp.kind=1; catcher_cp.throwvx=6; catcher_cp.throwinjury=-1; catcher_cp.vaction=2; victim_cp.kind=2;
 ObjHeader catcher_hdr{}, victim_hdr{}; catcher_hdr.id=5; catcher_hdr.nframes=4; victim_hdr.id=9; victim_hdr.nframes=4;
 Group catcher_group{}, victim_group{}; catcher_group.count=victim_group.count=1;
 catcher_group.objs[0]={&catcher_hdr,catcher_frames,nullptr,nullptr,&catcher_cp,nullptr}; victim_group.objs[0]={&victim_hdr,victim_frames,nullptr,nullptr,&victim_cp,nullptr};
 a=Ent{}; b=Ent{}; a.live=b.live=true; a.def=&catcher_group.objs[0]; a.data_group=&catcher_group; a.group=0; a.catching=1; a.frame=a.frame_at_pass=1;
 b.def=&victim_group.objs[0]; b.data_group=&victim_group; b.caught_by=0; b.frame=b.frame_at_pass=1; w.groups[0]=&catcher_group;
 auto& dead_follower=w.ents[max_fighters]; dead_follower=Ent{}; dead_follower.live=true; dead_follower.def=&catcher_group.objs[0];
 dead_follower.data_group=&catcher_group; dead_follower.follow=0; dead_follower.hp=0; dead_follower.wait=7;
 w.grab_holds(); assert(a.def==&victim_group.objs[0] && a.transform_original==&catcher_group && b.caught_by==-1 && b.vx==FX(6));
 assert(dead_follower.def==&victim_group.objs[0] && dead_follower.frame==0 && dead_follower.wait==7);
 // Transforming again remembers the record immediately before the new grab.
 a=Ent{}; b=Ent{}; dead_follower=Ent{}; a.live=b.live=true; a.def=&catcher_group.objs[0]; a.data_group=&catcher_group;
 a.transform_original=&g1; a.catching=1; a.frame=a.frame_at_pass=1; b.def=&victim_group.objs[0];
 b.data_group=&victim_group; b.caught_by=0; b.frame=b.frame_at_pass=1; w.grab_holds();
 assert(a.def==&victim_group.objs[0] && a.transform_original==&catcher_group);
 static Frame prisoner_frames[6]{}; for (auto& frame : prisoner_frames) frame.cp=-1;
 ObjHeader prisoner_hdr{}; prisoner_hdr.id=300; prisoner_hdr.kind=5; prisoner_hdr.nframes=6;
 Bdy prisoner_body{0,0,20,20,1003}; prisoner_frames[0].nbdy=1;
 ObjDef prisoner{&prisoner_hdr,prisoner_frames,&prisoner_body,nullptr,nullptr,nullptr};
 a=Ent{}; b=Ent{}; a.live=b.live=true; a.def=&catcher_group.objs[0]; a.team=1; b.def=&prisoner; b.kind=5; b.team=2; b.frame_at_pass=0;
 Itr strike{}; w.plain_hit(0,1,strike); assert(b.team==1 && b.frame==3 && b.hp==500);
 b=Ent{}; b.live=true; b.def=&g1.objs[0]; b.kind=0; b.team=2; b.hp=0; b.frame=230; f[230].state=14;
 h1.id=30; b.stage_join_hp=750; b.stage_join_lives=3; b.blink=2; w.stage_mode=true; w.stage_revival();
 assert(b.live && b.team==1 && b.hp==750 && b.stage_lives==3 && b.frame==219 && b.picture_offset==140 && b.blink==2); h1.id=7;
 b.hp=0; b.frame=230; b.blink=4; b.wait=7; b.vx=FX(3); b.vz=-FX(2); w.stage_revival();
 assert(b.live && b.hp==750 && b.stage_lives==2 && b.y==FX(-300) && b.frame==212 && b.wait==7 && b.vx==FX(3) && b.vz==-FX(2));
 auto& revived_clone=w.ents[max_fighters]; revived_clone=Ent{}; revived_clone.live=true; revived_clone.def=&g1.objs[0];
 revived_clone.kind=0; revived_clone.follow=0; revived_clone.team=1; revived_clone.hp=0;
 revived_clone.frame=230; revived_clone.blink=4; revived_clone.stage_lives=2; revived_clone.stage_max_hp=350;
 w.stage_mode=false; w.stage_revival();
 assert(revived_clone.live && revived_clone.hp==350 && revived_clone.stage_lives==1 && revived_clone.frame==212);
 revived_clone.hp=0; revived_clone.frame=230; revived_clone.blink=4; w.stage_revival(); assert(!revived_clone.live);
 revived_clone=Ent{}; revived_clone.live=true; revived_clone.def=&g1.objs[0]; revived_clone.kind=0;
 revived_clone.hp=0; revived_clone.frame=230; revived_clone.blink=4; w.stage_revival(); assert(!revived_clone.live);
 w.stage_mode=true;
 a.live=false; b.frame=0; f[0].state=0; b.y=0; b.x=FX(100); w.stage.width=900; w.stage_exit=w.ai_stage_mode=true;
 w.ai_control(1); assert(b.keys.right && !b.keys.left);
 w.ai_stage_mode=false;
 for (auto& e : w.ents) e=Ent{};
 static Frame clone_frames[2]{}; for (auto& frame : clone_frames) frame.cp=-1;
 ObjHeader clone_hdr{}; clone_hdr.id=5; clone_hdr.nframes=2;
 Group clones{}; clones.count=1; clones.objs[0]={&clone_hdr,clone_frames,nullptr,nullptr,nullptr,nullptr};
 clone_frames[0].op_kind=1; clone_frames[0].op_oid=0;
 a.live=true; a.kind=0; a.def=&clones.objs[0]; a.data_group=&clones; a.group=0; a.team=1; a.owner=0; w.groups[0]=&clones;
 w.spawn_from_frame(0); assert(w.ents[max_fighters].live && w.ents[max_fighters].follow==0 && w.ents[max_fighters].hp==10 && w.ents[max_fighters].ai.enabled);
 assert(w.ents[max_fighters].stage_max_hp==10);
 for (int i=max_fighters;i<max_ents;++i) w.ents[i]=Ent{};
 clone_frames[0].op_facing=30; w.spawn_from_frame(0);
 assert(w.ents[max_fighters].rest==2 && w.ents[max_fighters+1].rest==0 && w.ents[max_fighters+2].rest==2);
 assert(w.ents[max_fighters].pair_rest[max_fighters+1]==40 && w.ents[max_fighters+1].pair_rest[max_fighters+2]==40 && w.ents[max_fighters+2].pair_rest[max_fighters]==40);
 clone_frames[0].op_facing=0;
 for (auto& e : w.ents) e=Ent{};
 static Frame effect_frames[150]{}; for (auto& frame : effect_frames) frame.cp=-1;
 ObjHeader debris_hdr{}; debris_hdr.id=999; debris_hdr.kind=5; debris_hdr.nframes=150;
 Group debris{}; debris.count=1; debris.objs[0]={&debris_hdr,effect_frames,nullptr,nullptr,nullptr,nullptr}; w.groups[max_fighters]=&debris;
 a.live=true; a.kind=0; a.def=&g1.objs[0]; a.state_frame=200; a.frame=0; a.team=1; a.owner=0; f[200].state=13; f[0].state=0;
 w.status_effects(0); int shards=0; for (int i=max_fighters;i<max_ents;++i) if(w.ents[i].live) {
     assert(w.ents[i].team==0 && w.ents[i].owner==99 && w.ents[i].vz==FX(0.1)); ++shards;
 } assert(shards==15);
 for (int i=max_fighters;i<max_ents;++i) w.ents[i]=Ent{};
 a.state_frame=201; f[201].state=18; a.frame=0; a.vx=FX(2); w.status_effects(0);
 int puffs=0; for (int i=max_fighters;i<max_ents;++i) if(w.ents[i].live) {
     assert(w.ents[i].frame==140 && w.ents[i].vy==FX(-1) && w.ents[i].vx>=-FX(3) && w.ents[i].vx<=FX(7)); ++puffs;
 } assert(puffs==7);
 for (int i=max_fighters;i<max_ents;++i) w.ents[i]=Ent{};
 a.frame=1125; a.state_frame=200; auto& blink_follower=w.ents[max_fighters]; blink_follower=Ent{};
 blink_follower.live=true; blink_follower.def=&g1.objs[0]; blink_follower.follow=0;
 w.after_advance(0); assert(a.live && a.frame==0 && a.blink==-25 && blink_follower.blink==-25 && a.state_frame==200);
 a.frame=400; w.after_advance(0); assert(!a.live && a.frame==0);
 for (auto& e : w.ents) e=Ent{};
 static Frame blast_frames[110]{}; for (auto& frame : blast_frames) frame.cp=-1;
 ObjHeader blast_headers[3]{}; Group blasts{}; blasts.count=3;
 const int blast_ids[]={211,221,212};
 for (int i=0;i<3;++i) { blast_headers[i].id=blast_ids[i]; blast_headers[i].kind=3; blast_headers[i].nframes=110; blasts.objs[i]={&blast_headers[i],blast_frames,nullptr,nullptr,nullptr,nullptr}; }
 blast_frames[0].hit_Fa=11; w.groups[0]=&blasts; auto& blast=w.ents[max_fighters]; blast.live=true; blast.def=&blasts.objs[1]; blast.data_group=&blasts; blast.kind=3; blast.group=0;
 w.ball_update(max_fighters); int pieces=0; for (int i=max_fighters;i<max_ents;++i) if(w.ents[i].live) ++pieces; assert(!blast.live && pieces==14);
 for (auto& e : w.ents) e=Ent{};
 static ObjHeader ice_header{}; ice_header.id=222; ice_header.kind=3; ice_header.nframes=110;
 blasts.count=4; blasts.objs[3]={&ice_header,blast_frames,nullptr,nullptr,nullptr,nullptr};
 a.live=true; a.kind=0; a.def=&g1.objs[0]; a.team=2; a.hp=100;
 blast=Ent{}; blast.live=true; blast.def=&blasts.objs[1]; blast.data_group=&blasts; blast.kind=3; blast.group=0; blast.team=1;
 blast_frames[0].hit_Fa=9; w.groups[0]=&blasts; w.ball_update(max_fighters);
 int seekers=0; for (int i=9;i<max_ents;++i) if(w.ents[i].live) { ++seekers; assert(w.ents[i].chase==0 && (w.ents[i].def->hdr->id==221 || w.ents[i].def->hdr->id==222)); }
 assert(!blast.live && seekers==4);
 for (auto& e : w.ents) e=Ent{};
 static ObjHeader julian_seeker{}; julian_seeker.id=228; julian_seeker.kind=3; julian_seeker.nframes=110;
 blasts.count=5; blasts.objs[4]={&julian_seeker,blast_frames,nullptr,nullptr,nullptr,nullptr};
 blast=Ent{}; blast.live=true; blast.def=&blasts.objs[1]; blast.data_group=&blasts; blast.kind=3; blast.group=0;
 blast.team=1; blast.y=-FX(30)+128; blast.vx=FX(2); blast_frames[0].hit_Fa=13; w.ball_update(max_fighters);
 assert(!blast.live && w.ents[max_fighters+1].live && w.ents[max_fighters+1].y==-FX(30)+128 && w.ents[max_fighters+1].vy==FX(0.1));
 for (auto& e : w.ents) e=Ent{};
 a.live=true; a.kind=0; a.def=&g1.objs[0]; a.team=1; a.hp=100;
 blast=Ent{}; blast.live=true; blast.def=&blasts.objs[1]; blast.data_group=&blasts; blast.kind=3; blast.group=0; blast.team=1; blast.chase=0;
 blast_frames[0].hit_Fa=7; w.ball_update(max_fighters);
 assert(w.ents[max_fighters+1].live && w.ents[max_fighters+1].frame==40 && w.ents[max_fighters+1].def->hdr->id==221);
 for (auto& e : w.ents) e=Ent{};
 a.live=true; a.kind=0; a.def=&g1.objs[0]; a.team=2; a.hp=100; a.x=FX(100); a.y=0;
 blast=Ent{}; blast.live=true; blast.def=&blasts.objs[1]; blast.data_group=&blasts; blast.kind=3; blast.group=0; blast.team=1; blast.y=-FX(30);
 blast_frames[0].hit_Fa=1; w.ball_update(max_fighters); assert(blast.chase==0 && blast.y==-FX(30)+FX(1.2));
 blast.chase=max_ents; blast_frames[0].hit_Fa=4; w.ball_update(max_fighters); assert(blast.live);
 for (auto& e : w.ents) e=Ent{};
 ObjHeader marker_hdr{}; marker_hdr.id=998; marker_hdr.kind=5; marker_hdr.nframes=7;
 Group markers{}; markers.count=1; markers.objs[0]={&marker_hdr,effect_frames,nullptr,nullptr,nullptr,nullptr};
 w.groups[max_fighters]=&markers; a.live=b.live=true; a.kind=b.kind=0; a.def=b.def=&g1.objs[0]; a.team=b.team=1;
 b.team=2; b.hp=0; b.frame=230; b.blink=4; b.stage_join_hp=300; w.stage_mode=true; f[230].state=14;
 w.stage_revival(); assert(b.team==1 && b.hp==300 && w.ents[max_fighters].live && w.ents[max_fighters].frame==6);
 w.ents[max_fighters]=Ent{}; w.stage_mode=false;
 a.x=FX(400); a.z=FX(400); b.x=FX(100); b.z=FX(300);
 a.history[1]=a.history[2]=a.history[3]=a.history[4]=9; w.team_orders(0);
 assert(b.follow_order && b.order_leader==0 && w.ents[max_fighters].live && w.ents[max_fighters].frame==2);
 w.ai_control(1); assert(!b.keys.right && !b.keys.down);
 a.history[1]=9; a.history[2]=5; a.history[3]=9; a.history[4]=5; w.team_orders(0); assert(!b.follow_order);
 a.history[1]=9; a.history[2]=0; a.history[3]=9; a.history[4]=0; w.team_orders(0);
 assert(b.order_x>=360 && b.order_x<=440 && b.order_z>=360 && b.order_z<=440);
 for (auto& e : w.ents) e=Ent{};
 for (int i=0;i<5;++i) {
     auto& e=w.ents[i]; e.live=true; e.kind=0; e.team=1; e.hp=50; e.dark_hp=100;
     e.stage_max_hp=500; e.frame=9; e.order_x=400; e.order_z=300; e.follow_order=true;
 }
 w.camera=100; w.stage_exit=true; w.stage_transition(1);
 for (int i=0;i<5;++i) {
     const auto& e=w.ents[i];
     assert(e.live && e.hp==250 && e.dark_hp==250 && e.mp==500 && e.frame==0);
     assert(e.order_x==-1000 && e.order_z==-1000 && e.follow_order);
 }
 assert(w.camera==0 && !w.stage_exit);
 for(int i=max_fighters;i<max_fighters+3;++i) {
     auto& e=w.ents[i]; e=Ent{}; e.live=true; e.kind=0; e.team=1; e.follow=0;
     e.hp=e.dark_hp=100; e.stage_max_hp=500;
 }
 w.stage_transition(1); assert(w.ents[max_fighters].live && w.ents[max_fighters+1].live && !w.ents[max_fighters+2].live);
 for (int i=max_fighters;i<max_fighters+5;++i) w.ents[i]=Ent{};
 w.ents[max_fighters].follow=0;
 for (int i=max_fighters+1;i<=max_fighters+2;++i) { w.ents[i].live=true; w.ents[i].kind=0; w.ents[i].follow=0; }
 w.ents[max_fighters+3].live=true; w.ents[max_fighters+3].kind=1;
 w.ents[max_fighters+4].live=true; w.ents[max_fighters+4].kind=1; w.ents[max_fighters+4].holder=0;
 w.stage_transition(1);
 assert(w.ents[max_fighters+1].live && !w.ents[max_fighters+2].live && !w.ents[max_fighters+3].live && w.ents[max_fighters+4].live);
 for (int i=0;i<5;++i) assert(to_int(w.ents[i].z)>=w.stage.z1 && to_int(w.ents[i].z)<w.stage.z2);
 static Frame projectile_frames[80]{}; for (auto& frame : projectile_frames) frame.cp=-1;
 ObjHeader projectile_header{}; projectile_header.nframes=80;
 ObjDef projectile{&projectile_header,projectile_frames,nullptr,nullptr,nullptr,nullptr};
 projectile_frames[0].state=1000;
 for (int id : {101,120,121}) {
     for (int direction : {-1,1}) {
         auto& e=w.ents[max_fighters]; e=Ent{}; e.live=true; e.def=&projectile; e.kind=id==121 ? 4 : 1;
         projectile_header.id=id; e.y=FX(-30); e.vx=FX(10)*direction;
         w.physics(max_fighters);
         assert(e.x==(id==101 ? FX(8) : FX(12))*direction);
         if (id==121) assert(e.frame==40);
     }
 }
 auto& bottle=w.ents[max_fighters]; bottle=Ent{}; bottle.live=true; bottle.def=&projectile; bottle.kind=6;
 projectile_header.id=122; bottle.y=FX(-30); bottle.vx=-FX(10); w.physics(max_fighters); assert(bottle.frame==40);
 projectile_frames[40].state=1002; projectile_header.weapon_drop_hurt=5;
 bottle=Ent{}; bottle.live=true; bottle.def=&projectile; bottle.kind=6; bottle.hp=0;
 bottle.y=-FX(1); bottle.vy=FX(2); bottle.vx=FX(12); bottle.drop_counter=20;
 w.physics(max_fighters); assert(bottle.frame==0 && bottle.vy<0 && bottle.drop_counter==-1);
 auto& heavy=w.ents[max_fighters]; heavy=Ent{}; heavy.live=true; heavy.def=&projectile; heavy.kind=2;
 projectile_header.id=150; heavy.y=-FX(1); heavy.vy=FX(10); heavy.vx=FX(8); heavy.drop_counter=20;
 w.physics(max_fighters); assert(heavy.vy==-FX(5) && heavy.drop_counter==19 && heavy.vx==FX(4));
 heavy.y=-FX(1); heavy.vy=FX(2); w.physics(max_fighters);
 assert(heavy.frame==20 && heavy.drop_counter==13 && heavy.vx==FX(2));
 auto& light=w.ents[max_fighters]; light=Ent{}; light.live=true; light.def=&projectile; light.kind=1;
 projectile_header.id=100; light.y=-FX(1); light.vy=FX(2); light.vx=FX(8); light.vz=FX(2); light.drop_counter=20;
 w.physics(max_fighters); assert(light.frame==60 && light.vx==FX(4) && light.vz==FX(2) && light.drop_counter==15);
 for (auto& e : w.ents) e=Ent{};
 auto& drinker=w.ents[0]; auto& milk=w.ents[max_fighters];
 drinker.live=true; drinker.def=&g1.objs[0]; drinker.frame=0; drinker.held_item=max_fighters;
 drinker.hp=700; drinker.dark_hp=749; drinker.stage_max_hp=750; f[0].state=17;
 milk.live=true; milk.def=&projectile; milk.kind=6; milk.holder=0; milk.hp=6;
 projectile_header.id=122; w.carry_weapons();
 assert(drinker.dark_hp==750 && drinker.hp==704 && milk.hp==5);
 f[0].state=0; f[0].wp_dvx=10; f[0].wp_dvy=-3; f[0].wp_dvz=2;
 milk.vz=FX(3); w.carry_weapons(); assert(milk.holder==-1 && milk.vz==FX(3));
 drinker.held_item=max_fighters; milk.holder=0; f[0].wp_dvx=0; f[0].wp_kind=3; w.carry_weapons();
 assert(milk.holder==-1 && milk.frame<6 && milk.vx>=-FX(3) && milk.vx<=FX(3) && milk.vy<=0);
 // Knockdown runs before a simultaneous throw, including its minimum drop height.
 drinker.held_item=max_fighters; milk.holder=0; drinker.y=0; f[0].state=12; f[0].wp_kind=0;
 f[0].wp_dvx=10; f[0].wp_dvy=-3; w.carry_weapons();
 assert(milk.holder==-1 && milk.frame==40 && milk.y<=-FX(2) && milk.vx==FX(10) && milk.vy==-FX(3));
 f[0].state=0; f[0].wp_dvx=0;
 for (auto& e : w.ents) e=Ent{};
 projectile_header.id=122; projectile_header.kind=6;
 Group background{}; background.count=1; background.objs[0]=projectile; w.groups[max_fighters]=&background;
 w.background_drinks=false; for(int i=0;i<5000;++i) w.spawn_weapons();
 for(int i=max_fighters;i<max_ents;++i) assert(!w.ents[i].live);
 w.background_drinks=true; w.stage.width=1600; w.stage_bound=600; w.stage.z1=360; w.stage.z2=500;
 for(int i=0;i<5000;++i) w.spawn_weapons();
 int dropped=0; for(int i=max_fighters;i<max_ents;++i) if(w.ents[i].live) {
     const auto& drop=w.ents[i]; ++dropped;
     assert(drop.x>=FX(30) && drop.x<=FX(581) && drop.z>=FX(390) && drop.z<=FX(477));
     assert(drop.owner==99 && drop.hp==200 && drop.y==FX(-500));
 }
 assert(dropped==4);
 w.stage_bound=0;
 for (auto& e : w.ents) e=Ent{};
 w.stage_mode=false; w.finished=false; w.round_counter=0; w.winner=-1; w.fighters=2;
 w.ents[max_fighters].live=true; w.ents[max_fighters].kind=0; w.ents[max_fighters].team=1; w.ents[max_fighters].hp=10;
 assert(w.stage_allies_alive()); w.ents[max_fighters].hp=0; assert(!w.stage_allies_alive());
 w.ents[max_fighters].team=1; w.ents[max_fighters].stage_lives=3; w.stage_update_reserves(); assert(w.ents[max_fighters].stage_lives==-3);
 w.ents[0].live=true; w.ents[0].kind=0; w.ents[0].team=1; w.ents[0].hp=100;
 w.stage_update_reserves(); assert(w.ents[max_fighters].stage_lives==3);
 auto stats=w.stage_stats(); assert(stats.allies==1 && stats.ally_hp==100 && stats.reserves==2 && stats.enemies==0);
 w.ents[0].live=true; w.ents[0].kind=0; w.ents[0].team=1; w.ents[0].hp=100;
 w.ents[max_fighters].live=true; w.ents[max_fighters].kind=0; w.ents[max_fighters].team=2; w.ents[max_fighters].hp=10;
 w.round_end(); assert(w.round_counter==0 && w.winner==-1);
 w.ents[max_fighters].hp=0; w.round_end(); assert(w.round_counter==1 && w.winner==1);
 w.ents[max_fighters].hp=10; w.round_end(); assert(w.winner==-1 && !w.finished);
 w.ents[max_fighters].hp=0; w.ents[0].human=true; w.round_counter=143; w.round_end();
 assert(w.round_counter==144 && !w.finished); w.round_end(); assert(!w.finished);
 w.ents[0].keys.attack=true; w.round_end(); assert(w.finished);
 w.finished=false; w.ents[max_fighters].hp=10; w.round_end(); assert(w.finished && w.winner==-1);
 w.ents[max_fighters].hp=0;
 w.ents[0].x=FX(100); w.ents[0].vx=FX(10); w.tick(); assert(w.ents[0].x==FX(100));
 w.finished=false; w.auto_finish_round=true; w.round_counter=349; w.round_end(); assert(w.finished);
 w.ents[0].def=&g1.objs[0]; w.ents[0].x=FX(100); w.ents[0].z=FX(400);
 w.ents[max_fighters].def=&g2.objs[0]; w.ents[max_fighters].hp=10; w.ents[max_fighters].x=FX(200); w.ents[max_fighters].z=FX(400);
 f[0].state=0; w.ai_control(0); assert(w.ents[0].ai.target==max_fighters);
 w.ents[max_fighters].x=FX(400); w.ents[max_fighters].z=FX(430); auto& pickup=w.ents[max_fighters+1]; pickup=Ent{}; pickup.live=true;
 pickup.kind=6; pickup.def=&projectile; pickup.x=FX(105); pickup.z=FX(400); projectile_frames[0].state=1004;
 w.ents[0].prev=Keys{}; w.ai_control(0); assert(w.ents[0].keys.attack);
 auto& threat=w.ents[max_fighters+2]; threat=Ent{}; threat.live=true; threat.def=&projectile; threat.kind=3;
 threat.team=2; threat.frame=1; threat.facing=1; threat.x=FX(160); threat.z=FX(400); projectile_frames[1].nitr=1;
 w.ents[0].ai.timer=0; w.ents[0].prev=Keys{}; w.ai_control(0); assert(!w.ents[0].keys.attack);
 threat.live=false;
 w.ents[0].held_item=max_fighters+1; pickup.holder=0; projectile_header.id=122;
 w.ents[0].hp=100; w.ents[0].dark_hp=200; w.ents[0].prev=Keys{};
 w.ai_control(0); assert(w.ents[0].keys.left && !w.ents[0].keys.attack);
 f[0].state=17; w.ents[0].blink=5; w.ents[max_fighters].z=w.ents[0].z;
 w.ai_control(0); assert(w.ents[0].keys.defend && !w.ents[0].keys.attack);
 f[0].state=0; w.ents[0].blink=0;
 w.init(Stage{},123); w.add_fighter(0,1,true,0); w.add_fighter(1,2,false,50); w.place_fighters();
 for(int i=0;i<2;++i) {
     const auto& e=w.ents[i];
     assert(e.blink==75 && e.facing==0 && to_int(e.x)>=198 && to_int(e.x)<595);
     assert(to_int(e.z)>=360 && to_int(e.z)<500);
 }
 w.stage_mode=true; auto& defeated=w.ents[1]; defeated.hp=0; defeated.frame=defeated.prev_frame=230;
 defeated.blink=0; f[230].state=14; w.advance(1); assert(defeated.blink==30);
 w.stage_revival(); assert(defeated.live); defeated.blink=4; w.stage_revival(); assert(!defeated.live);
 // Get-up invulnerability depends on difficulty and whether stage/army troops are minor enemies.
 f[230].wait=0; f[230].next=219; f[219].state=0;
 auto check_getup=[&](int id, bool stage_enemy, bool army_troop, int difficulty, int expected) {
     auto& e=w.ents[1]; e=Ent{}; e.live=true; e.def=&g1.objs[0]; e.team=stage_enemy ? 2 : 1;
     e.army_side=army_troop ? 0 : -1; e.frame=e.prev_frame=230; e.blink=0;
     h1.id=id; w.ai_stage_mode=stage_enemy; w.difficulty=difficulty; w.advance(1); assert(e.blink==expected);
 };
 check_getup(30,true,false,1,0); check_getup(38,true,false,1,15);
 check_getup(38,true,false,2,0); check_getup(7,false,true,1,15);
 check_getup(30,false,true,1,0); check_getup(7,false,false,2,15);
 h1.id=7; w.ai_stage_mode=false; w.difficulty=1; f[230].next=0;
 auto& frozen=w.ents[0]; frozen.frame=frozen.prev_frame=202; frozen.blink=0; f[202].wait=3; w.advance(0);
 assert(frozen.blink==20);
 auto& heavy_animation=w.ents[max_fighters]; heavy_animation=Ent{}; heavy_animation.live=true; heavy_animation.def=&projectile;
 heavy_animation.kind=2; heavy_animation.frame=heavy_animation.prev_frame=0; projectile_frames[0].state=2000;
 heavy_animation.vx=-FX(2); w.advance(max_fighters); assert(heavy_animation.facing==1);
 heavy_animation.vx=0; heavy_animation.frame=0; w.advance(max_fighters); assert(heavy_animation.frame==20);
 for (auto& e : w.ents) e=Ent{};
 w.stage.width=1600; w.stage_mode=true; w.stage_bound=1200; w.clamp_edge_drinks=false;
 auto& edge_player=w.ents[0]; edge_player.live=true; edge_player.def=&g1.objs[0];
 edge_player.kind=0; edge_player.team=1; edge_player.x=-FX(10);
 auto& edge_enemy=w.ents[1]; edge_enemy.live=true; edge_enemy.def=&g2.objs[0];
 edge_enemy.kind=0; edge_enemy.team=2; edge_enemy.x=-FX(200);
 auto& edge_ally=w.ents[max_fighters]; edge_ally.live=true; edge_ally.def=&g1.objs[0];
 edge_ally.kind=0; edge_ally.team=1; edge_ally.x=FX(1650);
 auto& edge_drink=w.ents[max_fighters+1]; edge_drink.live=true; edge_drink.def=&projectile;
 edge_drink.kind=6; edge_drink.army_side=0; edge_drink.x=-FX(5); projectile_header.id=122;
 w.clamp_world();
 assert(edge_player.x==0 && edge_enemy.x==-FX(100) && edge_ally.x==FX(1200));
 assert(edge_drink.x==FX(10));
 edge_enemy.x=FX(1650); w.clamp_world(); assert(edge_enemy.x==FX(1650));
 edge_player.z=FX(500)+1; edge_drink.z=FX(359)-1; w.clamp_world();
 assert(edge_player.z==FX(500) && edge_drink.z==FX(359));
 edge_player.z=FX(360)-1; w.carry_weapons(); assert(edge_player.z==FX(360));
 edge_drink.army_side=-1; edge_drink.x=-1; edge_drink.y=0; w.clamp_world(); assert(!edge_drink.live);
 auto& edge_ball=w.ents[max_fighters+2]; edge_ball=Ent{}; edge_ball.live=true; edge_ball.def=&projectile;
 edge_ball.kind=3; edge_ball.x=FX(-300)-1; w.clamp_world(); assert(!edge_ball.live);
 for (auto& e : w.ents) e=Ent{};
 w.stage_mode=false; w.stage_bound=0; w.camera_limit=106; w.camera=w.camera_step=0;
 auto& camera_fighter=w.ents[max_fighters]; camera_fighter.live=true; camera_fighter.def=&g1.objs[0];
 camera_fighter.kind=0; camera_fighter.hp=100; camera_fighter.x=FX(1300);
 for (int i=0;i<500;++i) w.update_camera(); assert(w.camera==106);
 w.camera_limit=0; for (int i=0;i<500;++i) w.update_camera(); assert(w.camera==806);
 for (auto& e : w.ents) e=Ent{};
 auto& troop=w.ents[1]; troop.live=true; troop.def=&g1.objs[0]; troop.kind=0;
 troop.team=1; troop.hp=120; troop.army_side=0; troop.army_kind=0;
 auto& opponent=w.ents[2]; opponent.live=true; opponent.def=&g2.objs[0]; opponent.kind=0;
 opponent.team=2; opponent.hp=75; opponent.army_side=1;
 auto army=w.army_stats(); assert(army.living[0]==1 && army.hp[0]==120 && army.living[1]==1 && army.hp[1]==75);
 assert(w.army_census(0,7)==1 && w.army_census(0,8)==0);
 troop.def=&g2.objs[0]; assert(w.army_census(0,7)==0 && w.army_census(0,8)==1);
 troop.hp=0; army=w.army_stats(); assert(army.living[0]==0 && army.hp[0]==0 && w.army_census(0,8)==1);
 for (auto& e : w.ents) e=Ent{};
 h1.id=2; f[0].state=0; f[0].hit_Uj=40;
 auto& healer=w.ents[0]; healer.live=true; healer.def=&g1.objs[0]; healer.kind=0;
 healer.team=1; healer.hp=healer.dark_hp=500; healer.mp=500; healer.x=FX(100); healer.z=FX(400); healer.facing=0;
 auto& ally=w.ents[1]; ally.live=true; ally.def=&g2.objs[0]; ally.kind=0; ally.team=1;
 ally.hp=100; ally.dark_hp=500; ally.x=FX(160); ally.z=FX(400);
 auto& enemy=w.ents[2]; enemy.live=true; enemy.def=&g2.objs[0]; enemy.kind=0; enemy.team=2;
 enemy.hp=500; enemy.x=FX(700); enemy.z=FX(400);
 w.ai_control(0); assert(healer.seq[6]==3 && healer.keys.right);
 healer.seq[6]=0; healer.facing=1; w.ai_control(0); assert(healer.seq[6]==0 && healer.keys.right);
 healer.facing=0; healer.mp=350; w.ai_control(0); assert(healer.seq[6]==0);
 healer.mp=500; w.ai_difficulty=2; int easy_heals=0;
 for (int i=0;i<1000;++i) { healer.seq[6]=0; w.ai_control(0); if (healer.seq[6]==3) ++easy_heals; }
 assert(easy_heals>40 && easy_heals<150);
 w.ai_random_game=true; int demo_heals=0;
 for (int i=0;i<100;++i) { healer.seq[6]=0; w.ai_control(0); if (healer.seq[6]==3) ++demo_heals; }
 assert(demo_heals==100);
 enemy.x=healer.x; enemy.z=FX(700); ally.x=FX(210);
 healer.seq[6]=0; w.ai_control(0); assert(healer.seq[6]==0);
 w.ai_random_game=false; w.ai_stage_mode=w.stage_mode=true; enemy.x=FX(700); enemy.z=FX(400);
 healer.x=FX(160); healer.hp=100; healer.dark_hp=500;
 auto& stage_healer=w.ents[max_fighters]; stage_healer=Ent{}; stage_healer.live=true; stage_healer.kind=0;
 stage_healer.def=&g1.objs[0]; stage_healer.team=1; stage_healer.hp=stage_healer.dark_hp=500;
 stage_healer.mp=500; stage_healer.x=FX(100); stage_healer.z=FX(400); stage_healer.facing=0;
 h1.id=34; int stage_sorcerer_heals=0;
 for (int i=0;i<1000;++i) { stage_healer.seq[6]=0; w.ai_control(max_fighters); if (stage_healer.seq[6]==3) ++stage_sorcerer_heals; }
 assert(stage_sorcerer_heals>40 && stage_sorcerer_heals<150);
 h1.id=2; int stage_john_heals=0;
 for (int i=0;i<100;++i) { stage_healer.seq[6]=0; w.ai_control(max_fighters); if (stage_healer.seq[6]==3) ++stage_john_heals; }
 assert(stage_john_heals==100);
 for (auto& e : w.ents) e=Ent{};
 w.stage_mode=w.ai_stage_mode=w.ai_random_game=false; w.ai_difficulty=0; h1.id=7; f[0].state=0;
 auto& defender=w.ents[0]; defender.live=true; defender.def=&g1.objs[0]; defender.kind=0;
 defender.team=1; defender.hp=500; defender.x=FX(100); defender.z=FX(400); defender.facing=1;
 auto& incoming=w.ents[max_fighters]; incoming.live=true; incoming.def=&projectile; incoming.kind=3;
 incoming.team=2; incoming.hp=100; incoming.x=FX(260); incoming.z=FX(400); incoming.vx=-FX(5);
 projectile_frames[0].state=3000;
 w.ai_control(0); assert(defender.keys.defend && defender.keys.right);
 incoming.vx=FX(5); w.ai_control(0); assert(!defender.keys.defend);
 incoming.vx=-FX(5); w.ai_difficulty=2; int defenses=0;
 for (int i=0;i<1000;++i) { w.ai_control(0); if (defender.keys.defend) ++defenses; }
 assert(defenses>120 && defenses<220);
 w.stage_mode=w.ai_stage_mode=true; w.ai_difficulty=0; incoming.team=0;
 w.ai_control(0); assert(!defender.keys.defend);
 incoming.team=2; w.ai_control(0); assert(defender.keys.defend);
 incoming.team=0; defender.team=2; w.ai_control(0); assert(defender.keys.defend);
 incoming.team=2; w.ai_control(0); assert(!defender.keys.defend);
 for (auto& e : w.ents) e=Ent{};
 w.stage_mode=w.ai_stage_mode=false; w.stage.width=1600; w.stage.z1=360; f[230].state=14;
 defender.live=true; defender.def=&g1.objs[0]; defender.kind=0; defender.team=1;
 defender.hp=500; defender.x=FX(400); defender.z=FX(400);
 auto& downed=w.ents[1]; downed.live=true; downed.def=&g2.objs[0]; downed.kind=0; downed.team=2;
 downed.hp=100; downed.frame=230; downed.x=FX(450); downed.z=FX(400);
 w.ai_control(0); assert(defender.ai.target==1 && defender.keys.left && defender.keys.up && !defender.keys.attack);
 downed.frame=0; downed.blink=5; w.ai_control(0); assert(defender.keys.left && !defender.keys.attack);
 downed.x=FX(700); auto& active=w.ents[2]; active=Ent{}; active.live=true; active.def=&g2.objs[0];
 active.kind=0; active.team=2; active.hp=100; active.x=FX(500); active.z=FX(400);
 defender.ai.target=-1; w.ai_control(0); assert(defender.ai.target==2);
 downed.frame=0; downed.blink=0; int retained=0;
 for (int i=0;i<100;++i) { defender.ai.target=1; w.ai_control(0); if (defender.ai.target==1) ++retained; }
 assert(retained>85);
 downed.hp=0; defender.ai.target=1; w.ai_control(0); assert(defender.ai.target==2);
 downed.hp=100; downed.frame=230; downed.x=FX(450); active.live=false;
 defender.ai.target=-1; defender.follow_order=true; defender.held_item=max_fighters; defender.prev.attack=true;
 incoming=Ent{}; incoming.live=true; incoming.def=&projectile; incoming.kind=6; projectile_header.id=122;
 w.ai_control(0); assert(defender.keys.attack && !defender.prev.attack);
 // AI leaves beer when it has enough MP, and healthy stage allies leave milk.
 for (auto& e : w.ents) e=Ent{};
 h1.id=30;
 w.stage_mode=w.ai_stage_mode=false;
 defender.live=true; defender.def=&g1.objs[0]; defender.kind=0; defender.team=1;
 defender.hp=500; defender.stage_max_hp=500; defender.mp=500;
 defender.x=FX(400); defender.z=FX(400);
 active.live=true; active.def=&g2.objs[0]; active.kind=0; active.team=2;
 active.hp=100; active.x=FX(700); active.z=FX(430);
 pickup.live=true; pickup.def=&projectile; pickup.kind=6; pickup.x=FX(350); pickup.z=FX(400);
 projectile_frames[0].state=1004; projectile_header.id=123;
 w.ai_control(0); assert(!defender.keys.left);
 defender.mp=100; w.ai_control(0); assert(defender.keys.left);
 projectile_header.id=122; w.stage_mode=w.ai_stage_mode=true;
 w.ai_control(0); assert(!defender.keys.left);
 defender.hp=100; w.ai_control(0); assert(defender.keys.left);
 defender.follow=1; w.ai_control(0); assert(!defender.keys.left);
 // A teammate with the lowest health prioritizes milk even with an aligned enemy.
 defender.follow=-1; defender.hp=100; active.z=FX(400);
 auto& teammate=w.ents[3]; teammate=Ent{}; teammate.live=true; teammate.def=&g1.objs[0];
 teammate.kind=0; teammate.team=1; teammate.hp=300; teammate.x=FX(600); teammate.z=FX(450);
 w.ai_control(0); assert(defender.keys.left);
 teammate.hp=50; w.ai_control(0); assert(!defender.keys.left);
 active.live=false; w.ai_control(0); assert(!defender.keys.left && !defender.keys.attack);
 // Injured fighters approach allied healing fields while a hostile field cancels that diversion.
 static Frame field_frames[180]{}; ObjHeader field_header{}; field_header.id=200; field_header.nframes=180;
 ObjDef field_def{&field_header,field_frames,nullptr,nullptr,nullptr,nullptr};
 auto& field=w.ents[max_fighters+2]; field=Ent{}; field.live=true; field.def=&field_def; field.kind=3;
 field.frame=50; field.team=1; field.x=FX(350); field.z=FX(410);
 pickup.live=false; active.live=true; active.z=FX(430); teammate.live=false;
 defender.dark_hp=500; defender.hp=100; defender.ai.target=-1;
 w.ai_control(0); assert(defender.keys.left && defender.keys.down);
 auto& hostile_field=w.ents[max_fighters+3]; hostile_field=field; hostile_field.team=2; hostile_field.frame=60;
 w.ai_control(0); assert(!defender.keys.left);
 hostile_field.live=false; defender.hp=500; w.ai_control(0); assert(!defender.keys.left);
 // Aligned hostile fields and active fire/ice hazards constrain movement.
 field.team=2; field.frame=60; field.x=FX(450); field.z=FX(410); active.z=FX(400);
 defender.ai.timer=0; defender.ai.mode=0; w.ai_control(0);
 assert(!defender.keys.right && defender.keys.up);
 field_header.id=211; field.frame=0; field_frames[0].state=18; field.x=FX(550); field.z=FX(400);
 defender.ai.timer=0; w.ai_control(0); assert(!defender.keys.right);
 field_header.id=212; field.frame=150; defender.ai.timer=0; w.ai_control(0); assert(!defender.keys.right);
 field.frame=149; defender.ai.timer=0; w.ai_control(0); assert(defender.keys.right);
 // Ranged fighters maintain their preferred distance and still turn toward enemies.
 field.live=false; w.stage_mode=w.ai_stage_mode=false; h1.id=4; defender.facing=0;
 active.x=FX(530); defender.ai.timer=0; w.ai_control(0); assert(!defender.keys.right);
 active.x=FX(580); defender.ai.timer=0; w.ai_control(0); assert(defender.keys.right);
 active.x=FX(560); defender.facing=1; defender.ai.timer=0; w.ai_control(0); assert(defender.keys.right);
 // A burning runner does not steer toward its target.
 h1.id=7; defender.facing=0; defender.frame=1; f[1].state=19; active.x=FX(700); active.z=FX(450);
 defender.ai.timer=0; w.ai_control(0); assert(!defender.keys.right && !defender.keys.down);
 defender.frame=0; f[1].state=0;
 // Injured stage allies also keep their distance from stronger enemies.
 w.stage_mode=w.ai_stage_mode=true; auto& stage_ally=w.ents[max_fighters]; stage_ally=Ent{};
 stage_ally.live=true; stage_ally.def=&g1.objs[0]; stage_ally.kind=0; stage_ally.team=1;
 stage_ally.hp=100; stage_ally.stage_max_hp=500; stage_ally.x=FX(400); stage_ally.z=FX(400);
 active.hp=300; active.x=FX(530); active.z=FX(400); w.ai_control(max_fighters); assert(!stage_ally.keys.right);
 // Heavy weapons can be thrown at longer range; light weapons attack closer in.
 w.stage_mode=w.ai_stage_mode=false; stage_ally.live=false; defender.frame=0; defender.facing=0;
 defender.x=FX(400); defender.z=FX(400); defender.held_item=max_fighters+1; defender.ai.mode=0;
 pickup=Ent{}; pickup.live=true; pickup.def=&projectile; pickup.kind=2; pickup.holder=0;
 active.x=FX(600); active.z=FX(400); projectile_header.id=150;
 int heavy_attacks=0, distant_light_attacks=0, close_light_attacks=0;
 for (int i=0;i<200;++i) { defender.ai.timer=0; w.ai_control(0); if (defender.keys.attack) ++heavy_attacks; }
 projectile_header.id=100;
 for (int i=0;i<200;++i) { defender.ai.timer=0; w.ai_control(0); if (defender.keys.attack) ++distant_light_attacks; }
 active.x=FX(500);
 for (int i=0;i<200;++i) { defender.ai.timer=0; w.ai_control(0); if (defender.keys.attack) ++close_light_attacks; }
 assert(heavy_attacks>0 && distant_light_attacks==0 && close_light_attacks>0);
 // Ranged fighters attack at distance when facing their target, and can follow defend with a projectile input.
 pickup.live=false; defender.held_item=-1; h1.id=4; active.x=FX(600); defender.facing=0;
 int ranged_attacks=0, facing_away_attacks=0;
 for (int i=0;i<200;++i) { defender.ai.timer=0; w.ai_control(0); if (defender.keys.attack) ++ranged_attacks; }
 defender.facing=1;
 for (int i=0;i<200;++i) { defender.ai.timer=0; w.ai_control(0); if (defender.keys.attack) ++facing_away_attacks; }
 assert(ranged_attacks>0 && facing_away_attacks==0);
 defender.facing=0; defender.frame=110; f[110].state=7; defender.ai.timer=0;
 defender.prev.attack=defender.prev.right=true; w.ai_control(0);
 assert(defender.keys.attack && defender.keys.right && !defender.prev.attack && !defender.prev.right);
 // Ranged fighters retreat from close opponents, choosing a direction that avoids arena edges.
 defender.frame=0; w.stage_bound=0; w.stage.width=1600; active.x=FX(470);
 int retreat_left=0, escape_left_edge=0, escape_right_edge=0;
 for (int i=0;i<200;++i) { defender.ai.timer=0; w.ai_control(0); if (defender.keys.left) ++retreat_left; }
 defender.x=FX(150); active.x=FX(220);
 for (int i=0;i<200;++i) { defender.ai.timer=0; w.ai_control(0); if (defender.keys.right) ++escape_left_edge; }
 defender.x=FX(1450); active.x=FX(1380); defender.facing=1;
 for (int i=0;i<200;++i) { defender.ai.timer=0; w.ai_control(0); if (defender.keys.left) ++escape_right_edge; }
 assert(retreat_left>0 && escape_left_edge>0 && escape_right_edge>0);
 // Sustained attacks react to their target rather than continuing blindly.
 defender.x=FX(400); defender.z=FX(400); defender.facing=0; defender.ai.timer=0;
 active.x=FX(500); active.z=FX(480); h1.id=7; defender.frame=255; f[255].state=0;
 defender.prev.defend=true; w.ai_control(0); assert(defender.keys.defend && !defender.prev.defend);
 active.z=FX(400); defender.frame=270; f[270].state=0; active.frame=2; f[2].state=12;
 defender.prev.defend=true; w.ai_control(0); assert(defender.keys.defend && !defender.prev.defend);
 active.frame=0; h1.id=51; active.z=FX(420); defender.prev.defend=true;
 w.ai_control(0); assert(defender.keys.defend && !defender.prev.defend);
 h1.id=1; active.x=FX(450); active.z=FX(400); defender.frame=260; f[260].state=0;
 int deep_attacks=0; for (int i=0;i<200;++i) { w.ai_control(0); if (defender.keys.attack) ++deep_attacks; }
 assert(deep_attacks>0);
 // Woody and Davis follow airborne targets with the appropriate inputs.
 h1.id=10; defender.frame=271; f[271].state=0; active.frame=2; f[2].state=12; active.y=-FX(20);
 defender.seq[2]=0; w.ai_control(0); assert(defender.seq[2]==3);
 h1.id=11; defender.frame=270; f[270].hit_j=290; defender.prev.jump=true;
 w.ai_control(0); assert(defender.keys.jump && !defender.prev.jump);
 // Jan heals injured teammates, but does not trigger the heal when everyone is healthy.
 h1.id=36; defender.frame=0; defender.hp=defender.dark_hp=500; defender.mp=500;
 active.frame=0; active.y=0; active.x=FX(700); stage_ally.live=true; stage_ally.hp=100; stage_ally.dark_hp=500;
 int jan_heals=0, healthy_heals=0;
 for (int i=0;i<200;++i) { defender.seq[6]=0; defender.ai.timer=0; w.ai_control(0); if (defender.seq[6]==3) ++jan_heals; }
 stage_ally.hp=500;
 for (int i=0;i<200;++i) { defender.seq[6]=0; defender.ai.timer=0; w.ai_control(0); if (defender.seq[6]==3) ++healthy_heals; }
 assert(jan_heals>0 && healthy_heals==0);
 // Rudolf chooses his high-mana special when the target is distant in depth.
 h1.id=5; stage_ally.live=false; defender.mp=500; active.x=FX(700); active.z=FX(460);
 int powered_specials=0, empty_specials=0;
 for (int i=0;i<200;++i) { defender.seq[6]=defender.seq[7]=0; defender.ai.timer=0; w.ai_control(0); if (defender.seq[6]==3 || defender.seq[7]==3) ++powered_specials; }
 defender.mp=0;
 for (int i=0;i<200;++i) { defender.seq[6]=defender.seq[7]=0; defender.ai.timer=0; w.ai_control(0); if (defender.seq[6]==3 || defender.seq[7]==3) ++empty_specials; }
 assert(powered_specials>0 && empty_specials==0);
 // Monk's projectile choice respects direction, depth and mana.
 h1.id=35; defender.mp=500; active.x=FX(700); active.z=defender.z;
 int monk_specials=0, monk_empty=0;
 for (int i=0;i<200;++i) { defender.seq[0]=0; defender.ai.timer=0; w.ai_control(0); if (defender.seq[0]==3) ++monk_specials; }
 defender.mp=0;
 for (int i=0;i<200;++i) { defender.seq[0]=0; defender.ai.timer=0; w.ai_control(0); if (defender.seq[0]==3) ++monk_empty; }
 assert(monk_specials>0 && monk_empty==0);
 // An injured Woody can choose to teleport toward a stronger distant ally.
 h1.id=10; defender.hp=100; defender.mp=500; active.hp=300; active.x=FX(800); active.z=FX(400);
 stage_ally.live=true; stage_ally.team=1; stage_ally.hp=500; stage_ally.x=FX(900); stage_ally.z=FX(400);
 int ally_teleports=0, weak_ally_teleports=0;
 for (int i=0;i<500;++i) { defender.seq[7]=0; defender.ai.timer=0; w.ai_control(0); if (defender.seq[7]==3) ++ally_teleports; }
 stage_ally.hp=200;
 for (int i=0;i<500;++i) { defender.seq[7]=0; defender.ai.timer=0; w.ai_control(0); if (defender.seq[7]==3) ++weak_ally_teleports; }
 assert(ally_teleports>0 && weak_ally_teleports==0);
 // Fighters without a reference special rule do not invent generic combos.
 h1.id=37; stage_ally.live=false; defender.hp=500; defender.mp=500; defender.ai.mode=0;
 active.x=FX(700); active.z=FX(400); f[0].hit_Fa=100;
 for (auto& sequence : defender.seq) sequence=0;
 for (int i=0;i<200;++i) { w.ai_control(0); assert(!defender.keys.defend); }
 for (auto sequence : defender.seq) assert(sequence==0);
 f[0].hit_Fa=0;
 // Defense depends on an aligned attacker facing this fighter.
 h1.id=30; defender.x=FX(400); defender.z=FX(400); active.x=FX(520); active.z=FX(400);
 active.frame=3; f[3].state=3; active.facing=1; int blocks=0, away_blocks=0;
 for (int i=0;i<500;++i) { w.ai_control(0); if (defender.keys.defend) ++blocks; }
 active.facing=0;
 for (int i=0;i<500;++i) { w.ai_control(0); if (defender.keys.defend) ++away_blocks; }
 assert(blocks>0 && away_blocks==0);
 // Carried weapons select the original running, jumping and dashing actions.
 a=Ent{}; a.def=&g1.objs[0]; a.frame=215; a.vx=1; a.keys.jump=true; a.press[p_jump]=1;
 w.crouching(a); assert(a.frame==213);
 a.frame=215; a.vx=-1; w.crouching(a); assert(a.frame==214);
 a=Ent{}; item=Ent{}; a.live=item.live=true; a.def=&g1.objs[0]; item.def=&weapon;
 a.held_item=max_fighters; item.holder=0; item.kind=1; a.keys.attack=true; a.press[p_attack]=1;
 w.jumping(a); assert(a.frame==30);
 a.keys.right=true; w.jumping(a); assert(a.frame==52);
 a.keys.right=false; a.frame=9; w.running(a); assert(a.frame==35);
 a.keys.right=true; w.running(a); assert(a.frame==45);
 a.vx=FX(3); a.vy=0; a.facing=0; w.dashing(a); assert(a.frame==40 && a.vy==-FX(1));
 item.kind=6; item.hp=100; a.keys.right=false; a.frame=9; w.running(a); assert(a.frame==55);
 a.keys.right=true; w.running(a); assert(a.frame==45);
 a.vx=FX(3); a.vy=0; w.dashing(a); assert(a.frame==52 && a.vy==-FX(1));
 item.hp=0; a.keys.right=false; a.frame=9; w.running(a); assert(a.frame==45);
 item.kind=2; a.frame=212; w.jumping(a); assert(a.frame==212);
 // Carrying snaps the weapon to the holder's integer depth and preserves its wait on release.
 a.frame=115; a.z=FX(400)+128; a.held_item=max_fighters; item.holder=0; item.wait=9;
 f[115].state=0; f[115].wp_cover=0; f[115].wp_dvx=12; w.carry_weapons();
 assert(item.z==FX(401) && item.wait==9 && item.holder==-1);
 // Broken boxes and bottles produce their distinct ranges of debris sprites.
 static Frame break_frames[200]{}; static ObjHeader break_hdr{}; static Group break_group{};
 for (auto& frame : break_frames) frame.cp=-1;
 break_hdr.id=999; break_hdr.nframes=200; break_group.count=1;
 break_group.objs[0]={&break_hdr,break_frames,nullptr,nullptr,nullptr,nullptr};
 w.groups[max_fighters]=&break_group;
 item=Ent{}; item.live=true; item.def=&weapon; item.data_group=&break_group; item.group=max_fighters;
 item.kind=1; item.drop_counter=-1; item.frame=0; weapon_frames[0].op_kind=1; weapon_frames[0].op_oid=0;
 w.after_advance(max_fighters); assert(item.live && w.ents[max_fighters+1].live && w.ents[max_fighters+1].def==&break_group.objs[0]);
 weapon_frames[0].op_kind=0; w.after_advance(max_fighters); assert(!item.live);
 const int ids[]={151,122,123,213,120};
 const int counts[]={15,9,9,5,3};
 for (int test=0;test<5;++test) {
     for (int n=8;n<max_ents;++n) w.ents[n]=Ent{};
     wh.id=ids[test]; item.live=true; item.def=&weapon; item.kind=1; item.drop_counter=-1; item.team=2; item.owner=1;
     w.after_advance(max_fighters); assert(!item.live);
     int count=0;
     for (int n=9;n<max_ents;++n) if (w.ents[n].live) {
         assert(w.ents[n].team==0 && w.ents[n].owner==99 && w.ents[n].vz==FX(0.1));
         const int frame=w.ents[n].frame;
         if (test==0) assert((frame>=40 && frame<=47) || (frame>=50 && frame<=57));
         if (test==1) assert((frame>=70 && frame<=77) || (frame>=80 && frame<=83));
         if (test==2) assert((frame>=160 && frame<=167) || (frame>=74 && frame<=77));
         if (test==3) assert(frame>=150 && frame<=157 && w.ents[n].vy<=-FX(8));
         if (test==4) assert((frame>=54 && frame<=57) || (frame>=30 && frame<=33));
         ++count;
     }
     assert(count==counts[test]);
 }
 // Thrown arrows and shuriken follow light-weapon landing physics.
 for (int id : {201,202}) {
     item=Ent{}; wh.id=id; item.live=true; item.def=&weapon; item.kind=1;
     item.frame=40; item.y=FX(1); item.vy=FX(5); item.drop_counter=40;
     weapon_frames[40].state=1002; w.physics(max_fighters);
     assert(item.live && item.frame==70 && item.y==0 && item.drop_counter==35);
 }
 item=Ent{}; wh.id=150; item.live=true; item.def=&weapon; item.kind=2;
 item.y=FX(1); item.vy=0; item.drop_counter=40; w.physics(max_fighters);
 assert(item.live && item.frame==20 && item.y==0 && item.drop_counter==34);
 item=Ent{}; item.live=true; item.def=&break_group.objs[0]; item.kind=5; item.frame=50;
 item.y=0; item.vy=0; item.vx=FX(2); w.physics(max_fighters);
 assert(item.live && item.frame==101 && item.vx==0);
 item=Ent{}; item.live=true; item.def=&weapon; item.kind=1; item.fall_damage=-20; w.physics(max_fighters);
 assert(item.live && item.fall_damage==0);
 // Frozen impacts respect armor, and Louis's dash attack has its own landing frame.
 a=Ent{}; a.live=true; a.def=&g1.objs[0]; a.frame=200; f[200].state=13;
 a.y=FX(1); a.vy=FX(18); a.hp=100; a.armor=200; w.character_ground(a);
 assert(a.hp==95 && a.frame==185 && a.vy==FX(-3.5));
 a.frame=90; f[90].state=100; a.y=FX(1); a.vy=FX(3); a.vx=FX(6);
 w.character_ground(a); assert(a.frame==94 && a.vy==0 && a.vx==FX(2));
 // Heavy weapons use heavy walking/running frames and do not spend attack MP.
 a=Ent{}; item=Ent{}; a.live=item.live=true; a.def=&g1.objs[0]; item.def=&weapon;
 a.held_item=max_fighters; item.holder=0; item.kind=2; a.frame=0;
 w.walking(a); assert(a.frame==12);
 a.keys.right=true; a.prev.right=false; a.dash=5; w.walking(a); assert(a.frame==16);
 a.keys=Keys{}; a.keys.attack=true; a.press[p_attack]=1; a.mp=100; f[50].mp=30;
 w.walking(a); assert(a.frame==50 && a.mp==100);
 item.kind=1; f[20].mp=f[25].mp=30; a.mp=100; w.walking(a);
 assert((a.frame==20 || a.frame==25) && a.mp==100);
 // Heavy weapons prevent special-move combos, but light weapons do not.
 a=Ent{}; a.live=true; a.def=&g1.objs[0]; a.held_item=max_fighters; a.frame=0;
 item.live=true; item.kind=2; f[0].hit_Fa=260; a.seq[0]=3;
 w.recognize(a,0); assert(a.frame==0 && a.seq[0]==3);
 item.kind=1; w.recognize(a,0); assert(a.frame==260 && a.seq[0]==0);
 // A merged fighter can use a defined D-J-A move before the split fallback.
 a.frame=0; a.held_item=-1; a.seq[8]=3; a.merge_original=&g1; a.merge_timer=500; f[0].hit_ja=260;
 w.recognize(a,8); assert(a.frame==260 && a.merge_timer==500);
 a.frame=0; a.seq[8]=3; f[0].hit_ja=0; w.recognize(a,8); assert(a.merge_timer==0 && a.seq[8]==3);
 // Defend lock is refreshed even when the defend frame has no next frame.
 a=Ent{}; a.live=true; a.def=&g1.objs[0]; a.frame=a.prev_frame=110;
 f[110].wait=0; f[110].next=0; a.defend_lock=0; w.advance(0);
 assert(a.frame==110 && a.defend_lock==3);
 // A held-object spawn replaces an existing held item without leaving it attached.
 for (auto& ent : w.ents) ent=Ent{};
 static Frame spawn_frames[2]{}; static ObjHeader spawn_parent{}, spawn_weapon{}; static Group spawn_group{};
 spawn_parent.nframes=spawn_weapon.nframes=2; spawn_weapon.kind=1;
 spawn_frames[0].op_kind=2; spawn_frames[0].op_oid=1;
 spawn_group.count=2; spawn_group.objs[0]={&spawn_parent,spawn_frames,nullptr,nullptr,nullptr,nullptr};
 spawn_group.objs[1]={&spawn_weapon,spawn_frames,nullptr,nullptr,nullptr,nullptr};
 w.groups[0]=&spawn_group; a.live=true; a.def=&spawn_group.objs[0]; a.data_group=&spawn_group; a.held_item=max_fighters;
 item.live=true; item.def=&weapon; item.kind=1; item.holder=0;
 w.spawn_from_frame(0);
 assert(a.held_item==max_fighters+1 && w.ents[max_fighters+1].holder==0 && item.holder==-1 && item.live);
 // A guided thrown weapon avoids its thrower's team even if its own team changes.
 for (auto& ent : w.ents) ent=Ent{};
 a.live=b.live=item.live=true; a.def=b.def=&g1.objs[0]; a.team=1; b.team=3;
 a.x=FX(10); b.x=FX(100); item.def=&weapon; item.kind=1; item.team=2;
 item.thrower=0; item.frame=40; weapon_frames[40].hit_Fa=3; w.ball_update(max_fighters);
 assert(item.chase==1);
 a.live=false; item.chase=-1; b.team=1; w.ball_update(max_fighters); assert(item.chase==1);
 // A seeker retains its old target if no replacement exists; a never-targeted seeker expires.
 b.hp=0; item.hp=500; item.vx=0; w.ball_update(max_fighters); assert(item.hp==500 && item.chase==1 && item.vx>0);
 b.live=false; b.hp=100; item.vx=0; w.ball_update(max_fighters);
 assert(item.hp==500 && item.chase==1 && item.vx>0);
 b.live=true; b.hp=0;
 item.chase=-1; w.ball_update(max_fighters); assert(item.hp==0);
 // Returning copies test their pre-movement height; spent seekers still animate by speed.
 b.hp=100; item=Ent{}; item.live=true; item.def=&weapon; item.kind=3; item.team=2;
 item.frame=40; item.chase=1; item.y=-FX(25)-64; item.vy=FX(2); weapon_frames[40].hit_Fa=7;
 w.ball_update(max_fighters); assert(item.frame==40 && item.y>-FX(25));
 w.ball_update(max_fighters); assert(item.frame==60 && item.vx==0 && item.facing==1);
 item.frame=40; item.hp=0; item.vx=FX(13); item.chase=1; weapon_frames[40].hit_Fa=2;
 w.ball_update(max_fighters); assert(item.frame==5 && item.vx==FX(15));
 // Reusing an object slot must not transfer the old object's sparks to the new object.
 w.sparks[0]=Spark{}; w.sparks[0].live=true; w.sparks[0].owner=max_fighters+1; w.ents[max_fighters+1].live=false;
 const int new_slot=w.spawn(&weapon,0,0,40,1,0,0,0,0,0,0,0);
 assert(new_slot==max_fighters+1 && !w.sparks[0].live);
 // Nonzero spawn frames still enter from frame zero for animation and state effects.
 assert(w.ents[new_slot].frame==40 && w.ents[new_slot].prev_frame==0 && w.ents[new_slot].state_frame==0);
 // Breaking out of a catch gives both fighters a hit count for impulse resolution.
 a=Ent{}; b=Ent{}; a.live=b.live=true; a.def=&catcher_group.objs[0]; b.def=&victim_group.objs[0];
 a.frame=a.frame_at_pass=b.frame=b.frame_at_pass=1; a.catching=1; b.caught_by=0;
 a.catch_timer=2; catcher_cp.kind=1; catcher_cp.decrease=-5; catcher_cp.throwvx=0; victim_cp.kind=2;
 w.grab_holds(); assert(a.frame==0 && b.frame==181 && a.hit_count==1 && b.hit_count==1 && b.iy==-FX(3));
 // Initial catch positioning preserves the half-pixel midpoint and truncates the holder's height.
 a=Ent{}; b=Ent{}; a.live=b.live=true; a.def=&catcher_group.objs[0]; b.def=&victim_group.objs[0];
 a.x=FX(0.5); a.y=-FX(2.5); b.x=FX(11); catcher_cp.x=victim_cp.x=0;
 Itr catch_itr{}; catch_itr.catching=catch_itr.caught=1; w.catch_victim(0,1,catch_itr);
 assert(a.x==FX(5.5) && b.x==FX(5.5) && a.y==-FX(2) && b.y==-FX(2));
 // Catch interactions continue through the recorded target list.
 a=Ent{}; b=Ent{}; auto& second_caught=w.ents[2]; second_caught=Ent{};
 a.live=b.live=second_caught.live=true; a.def=&catcher_group.objs[0]; b.def=second_caught.def=&victim_group.objs[0];
 catcher_frames[1].nitr=1; catcher_group.objs[0].itr=&catch_itr; catch_itr.kind=3;
 a.frame=a.frame_at_pass=1; a.list_n=2; a.list_who[0]=1; a.list_who[1]=2; a.list_itr[0]=a.list_itr[1]=0;
 w.apply(0); assert(b.caught_by==0 && second_caught.caught_by==0 && a.catching==2);
 // MP regeneration uses health before healing, and healing also applies to non-fighters.
 a=Ent{}; a.live=true; a.def=&g1.objs[0]; a.hp=99; a.dark_hp=200; a.mp=100; a.heal_timer=9;
 f[0].state=0; w.counter_12=1; w.counter_3=0; w.regenerate(a); w.heal(a);
 assert(a.mp==105 && a.hp==107);
 a.kind=3; a.hp=50; a.dark_hp=100; a.heal_timer=9; w.heal(a); assert(a.hp==58);
 f[0].state=1700; w.heal(a); assert(a.regen_timer==1100);
 // Every fighter attacks before objects, including fighters in spawned-object slots.
 static Frame order_fighter_frames[256]{}, order_ball_frames[256]{};
 for (auto& frame : order_fighter_frames) frame.cp=-1;
 for (auto& frame : order_ball_frames) frame.cp=-1;
 static ObjHeader order_fighter_header{}, order_ball_header{};
 order_fighter_header.id=1; order_fighter_header.nframes=256;
 order_ball_header.id=200; order_ball_header.kind=3; order_ball_header.nframes=256;
 static Bdy order_body{0,0,20,20,0}; static Itr fighter_hit{}, ball_hit{};
 fighter_hit.w=fighter_hit.h=ball_hit.w=ball_hit.h=20; fighter_hit.vrest=ball_hit.vrest=5;
 fighter_hit.injury=30; ball_hit.injury=50;
 order_fighter_frames[0].nbdy=1; order_fighter_frames[1].nbdy=1; order_fighter_frames[1].nitr=1;
 order_ball_frames[0].state=3000; order_ball_frames[0].nbdy=order_ball_frames[0].nitr=1;
 order_ball_frames[10].state=3001; order_ball_frames[20].state=3002; order_ball_frames[30].state=3003;
 ObjDef order_fighter{&order_fighter_header,order_fighter_frames,&order_body,&fighter_hit,nullptr,nullptr};
 ObjDef order_ball{&order_ball_header,order_ball_frames,&order_body,&ball_hit,nullptr,nullptr};
 World order_world{}; order_world.init(Stage{},42);
 auto& victim=order_world.ents[0]; auto& projectile_order=order_world.ents[1]; auto& clone=order_world.ents[max_fighters];
 victim.live=projectile_order.live=clone.live=true; victim.def=clone.def=&order_fighter; projectile_order.def=&order_ball;
 victim.team=clone.team=1; projectile_order.team=2; projectile_order.kind=3; clone.frame=1; clone.follow=0;
 victim.z=projectile_order.z=clone.z=FX(400); order_world.tick();
 assert(victim.hp==500 && projectile_order.team==1);
 // Guided objects choose their movement before fighter control and teleportation.
 order_world.init(Stage{},42); order_fighter_frames[0].state=400; order_fighter_frames[1].state=0;
 order_fighter_frames[1].nitr=0; order_ball_frames[0].nitr=0; order_ball_frames[0].hit_Fa=1;
 auto& teleporting=order_world.ents[0]; auto& teleport_target=order_world.ents[1]; auto& seeker=order_world.ents[max_fighters];
 teleporting.live=teleport_target.live=seeker.live=true; teleporting.def=teleport_target.def=&order_fighter; seeker.def=&order_ball;
 teleporting.team=1; teleport_target.team=seeker.team=2; teleport_target.frame=1; teleport_target.x=FX(200);
 seeker.kind=3; seeker.x=FX(50); seeker.y=-FX(30); seeker.chase=0;
 teleporting.z=teleport_target.z=seeker.z=FX(400); order_world.teleport_phase=1; order_world.tick();
 assert(teleporting.x==FX(80) && seeker.vx<0);
 // A revived fighter uses teammates' positions at its own physics step.
 order_world.init(Stage{},42); order_world.stage_mode=true; order_fighter_frames[0].state=0;
 order_fighter_frames[230].state=14;
 auto& player_order=order_world.ents[0]; auto& returning=order_world.ents[1]; auto& revival_mate=order_world.ents[2];
 player_order.live=returning.live=revival_mate.live=true; player_order.def=returning.def=revival_mate.def=&order_fighter;
 player_order.team=1; returning.team=revival_mate.team=2; returning.frame=230; returning.hp=0;
 returning.blink=4; returning.stage_lives=2; returning.stage_max_hp=100;
 revival_mate.x=FX(100); revival_mate.vx=FX(10); revival_mate.z=returning.z=player_order.z=FX(400);
 World revival_rng{}; revival_rng.init(Stage{},42); const int expected_x=100+revival_rng.rnd(51)-26;
 order_world.tick(); assert(returning.hp==100 && returning.x==expected_x*256 && revival_mate.x==FX(110));
 // Louis's armour fragments are independent and spawn at integer positions.
 static Frame armor_source_frame[1]{}, armor_fragment_frames[4]{}; static ObjHeader armor_source_header{}, armor_headers[2]{};
 armor_source_header.nframes=1; armor_source_frame[0].state=9996; armor_source_frame[0].wait=10; armor_source_frame[0].cp=-1;
 for (auto& frame : armor_fragment_frames) { frame.state=1000; frame.wait=10; frame.cp=-1; }
 armor_headers[0].id=217; armor_headers[1].id=218;
 for (auto& header : armor_headers) { header.kind=1; header.nframes=4; }
 ObjDef armor_source{&armor_source_header,armor_source_frame,nullptr,nullptr,nullptr,nullptr};
 Group armor_group{}; armor_group.count=2;
 for (int n=0;n<2;++n) armor_group.objs[n]={&armor_headers[n],armor_fragment_frames,nullptr,nullptr,nullptr,nullptr};
 order_world.init(Stage{},42); order_world.groups[max_fighters]=&armor_group;
 auto& transforming=order_world.ents[0]; transforming.live=true; transforming.def=&armor_source; transforming.team=1;
 transforming.wait=1; transforming.x=FX(100.5); transforming.y=-FX(10.5); transforming.z=FX(400.5);
 order_world.tick(); int armor_count=0;
 for (int n=8;n<max_ents;++n) if (order_world.ents[n].live) {
     const Ent& piece=order_world.ents[n]; ++armor_count;
     assert(piece.team==0 && piece.owner==-1 && piece.x%256==0 && piece.y%256==0 && piece.z==FX(401));
 }
 assert(armor_count==5);
 // Round-end record restoration keeps the current frame and does not transform followers.
 for (auto& ent : w.ents) ent=Ent{};
 a.live=b.live=true; a.def=&g2.objs[0]; a.data_group=&g2; a.transform_original=&g1;
 a.group=0; a.frame=42; a.wait=7; b.def=&g2.objs[0]; b.data_group=&g2; b.follow=0; w.groups[0]=&g2;
 w.finish_round();
 assert(a.def==&g1.objs[0] && a.frame==42 && a.wait==7 && b.def==&g2.objs[0]);
}

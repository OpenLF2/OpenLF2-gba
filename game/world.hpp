// SPDX-License-Identifier: MIT
// Copyright (c) 2026 OpenLF2 contributors

// The fighting simulation: fighters, projectiles, hits, grabs and the computer opponents. It follows the
// rules of Little Fighter 2 as implemented by OpenLF2 (frame driven characters, hit rests, fall points) in a
// form that fits the GBA: fixed point arithmetic and no allocation.
#pragma once
#include "data.hpp"
#include <cstdint>

// Hot code runs from the GBA's IWRAM, as ARM code: its zero wait state 32-bit bus is much faster than the ROM's.
#if defined(__arm__)
#define LF2_HOT __attribute__((section(".iwram.text"), target("arm")))
#else
#define LF2_HOT
#endif

namespace lf2 {

// x / d truncated toward zero, exactly like the operator, but fast on the ARM7 for |x| < 2^24 and 1 <= d < 256.
int sdiv(int x, int d);

using fx = std::int32_t;                       // 24.8 fixed point
constexpr fx FX(double v) { return fx(v * 256.0); }
inline int to_int(fx v) { return v / 256; }    // truncates toward zero like the original

constexpr int max_ents = 48;
constexpr int max_fighters = 10;
constexpr int max_sounds = 16;
constexpr int max_sparks = 8;

struct Keys {
    bool up = false, down = false, left = false, right = false, attack = false, jump = false, defend = false;
};

enum Press { p_attack, p_jump, p_defend, p_right, p_left, p_up, p_down, press_count };

struct Ai {
    bool enabled = false;
    int target = -1;
    int think = 0;
    int mode = 0;
    int timer = 0;
    int aggression = 50;
    std::uint8_t keys = 0;
};

struct Ent {
    bool live = false;
    const ObjDef* def = nullptr;
    const Group* data_group = nullptr;          // object tables stay stable across transformations
    int group = 0;                              // roster slot (0..7) whose palette and tile pool this uses
    int kind = 0;                               // 0 character, 3 ball, ...
    fx x = 0, y = 0, z = 0;
    fx vx = 0, vy = 0, vz = 0;
    fx ix = 0, iy = 0, iz = 0;                  // impulse waiting for the hit stop to end
    int frame = 0, prev_frame = 0, state_frame = 0, frame_at_pass = 0, wait = 0;
    int facing = 0;                             // 0 right, 1 left
    int team = 0;
    int hp = 500, dark_hp = 500, mp = 500;
    int armor = 100;
    Keys keys, prev;
    std::uint8_t press[press_count] = {};
    std::uint8_t seq[9] = {};
    std::uint8_t history[5] = {};
    int fall = 0;                               // "timer b0": accumulated knock-down points
    int defend_pts = 0;                         // "timer b8"
    int super_window = 0;
    bool blocked_left = false, blocked_right = false, blocked_up = false, blocked_down = false;
    int rest = 0;                               // "timer ec": attack rest
    int hit_lag = 0;
    int hit_count = 0;
    int blink = 0;
    int dash = 0, walk_phase = 0, defend_lock = 0;
    int fall_damage = 0;
    int owner = -1;
    int follow = -1;
    int order_x = -1000, order_z = -1000, order_leader = -1;
    bool follow_order = false;
    int held_item = -1, holder = -1, thrower = -1, drop_counter = 0;
    int catching = -1, caught_by = -1, catch_timer = 0;
    int nearest = 1000, nearest_catcher = 1000;
    int list_n = 0;
    std::uint8_t list_who[20], list_itr[20];
    std::int8_t pair_rest[max_ents] = {};
    bool hit_this_frame = false;
    bool human = false;
    int picture_offset = 0;
    int walk_owner = -1;
    int stage_lives = 0, stage_max_hp = 500, stage_join_hp = 0, stage_join_lives = 0;
    int army_side = -1, army_kind = -1;
    int chase = -1;
    int heal_timer = 0, regen_timer = 0;
    int merge_timer = 0, merge_partner = -1;
    const Group* merge_original = nullptr;
    const Group* transform_original = nullptr;
    const Group* transform_target = nullptr;
    Ai ai;
    // drawing state kept by the video layer
    int sprite_slot = -1;
    std::uint32_t shown_tile = 0xFFFFFFFFu;
    int order = 0;
};

struct Spark { bool live; int x, y, z, timer, kind, owner; bool small; };

struct Stage {
    int width = 794, z1 = 360, z2 = 500;
};

struct StageStats {
    int allies = 0, ally_hp = 0, enemies = 0, enemy_hp = 0, reserves = 0;
};
struct ArmyStats { int living[2] = {}, hp[2] = {}; };

class World {
public:
    Ent ents[max_ents];
    Stage stage;
    int camera = 0, camera_step = 0;
    int stage_bound = 0, camera_limit = 0, stage_exit_x = 0;
    int counter_12 = 0, counter_3 = 0;
    int round_counter = 0;
    int winner = -1;                            // winning team once the fight is decided
    bool finished = false;
    bool stage_mode = false;
    bool stage_exit = false;
    bool background_drinks = true;
    bool clamp_edge_drinks = false;
    bool auto_finish_round = false;
    int difficulty = 1;
    int ai_difficulty = 0;
    bool ai_stage_mode = false, ai_random_game = false;
    int fighters = 0;
    int sounds[max_sounds];
    int sound_count = 0;
    int army_damage[2] = {}, army_deaths[2] = {};
    Spark sparks[max_sparks];
    int shake = 0;                              // set when a heavy hit lands near the camera (screen shake)
    int teleport_phase = 0;                     // alternates each simulation frame

    void init(const Stage& stage, std::uint32_t seed);
    // Adds a fighter; returns its slot. `roster` indexes the converted fighters.
    int add_fighter(int roster, int team, bool human, int aggression);
    void place_fighters();
    void stage_transition(int difficulty);
    bool stage_allies_alive() const;
    StageStats stage_stats() const;
    ArmyStats army_stats() const;
    int army_census(int side, int id) const;
    void stage_update_reserves();
    void finish_round();
    void set_keys(int slot, const Keys& keys);
    void tick();                                // one logic frame (30 per second)
    int rnd(int n);                             // 0 .. n-1
    void play(int wav);

    Ent* find_free();
    int spawn(const ObjDef* def, int group, int owner, int frame, int team, fx x, fx y, fx z, fx vx, fx vy, fx vz, int facing);

    const Group* groups[max_fighters + 1];

private:
    std::uint32_t seed_ = 1;
    int frame_number_ = 0;

    // input and control
    void control(int index);
    void sequences(Ent& e);
    void recognize(Ent& e, int n);
    void enter_frame(Ent& e, int target);
    void walking(Ent& e);
    void running(Ent& e);
    void jumping(Ent& e);
    void crouching(Ent& e);
    void recovering(Ent& e);
    void dashing(Ent& e);
    void frame_velocity(Ent& e);
    void frame_rules(int index);
    const Group* character_group(int id) const;
    void change_character(Ent& e, const Group* group, int frame, bool change_followers = true);
    void update_fusion();
    void split_fusion(Ent& e);
    void spawn_weapons();
    void carry_weapons();
    void check_holders();
    void stage_revival();
    void stage_revival(int index);
    void after_physics(int index);
    void status_effects(int index);
    void team_orders(int index);
    void ball_update(int index);
    const ObjDef* find_def(const Group* group, int id) const;
    // movement
    void physics(int index);
    void character_ground(Ent& e);
    void advance(int index);
    void after_advance(int index);
    void spawn_from_frame(int index);
    void remove(int index);
    // combat
    void detect();
    void apply(int index);
    void plain_hit(int a, int v, const Itr& itr);
    void defended_hit(int a, int v, const Itr& itr);
    void catch_victim(int a, int v, const Itr& itr);
    void ball_vs_ball(int a, int v);
    void add_spark(const Ent& attacker, const Ent& victim, const Itr& itr, int kind);
    void apply_impulses();
    void weapon_hit_recoil(Ent& attacker, const Ent& victim, bool defended);
    void grab_holds();
    void grab_place();
    void update_camera();
    void clamp_world();
    void regenerate(Ent& e);
    void heal(Ent& e);
    void round_end();
    void ai_control(int index);
    void ai_control_body(int index);
    // Live entities in slot order (and where the object slots start): the computer fighters scan these
    // instead of every slot. Valid during a tick until something spawns.
    std::uint8_t scan_list[max_ents] = {};
    int scan_count = 0, scan_objects = 0;
    bool scan_valid = false, in_tick = false;
    void rebuild_scan();
    void push(Ent& v, int amount) { v.ix += amount * 256; }
    int scaled_damage(const Ent& victim, int damage) const { return victim.armor > 0 ? damage * 100 / victim.armor : damage; }
    void count_army_hit(const Ent& victim, int damage, bool can_kill = true) {
        if (victim.army_side < 0 || victim.army_side > 1 || damage <= 0) return;
        army_damage[victim.army_side] += damage;
        if (can_kill && victim.follow < 0 && victim.hp > 0 && damage >= victim.hp) ++army_deaths[victim.army_side];
    }
    bool eligible(const Ent& a, const Ent& v, const Itr& itr, const Frame& vf) const;
    bool overlap(int ax, int ay, int aw, int ah, int bx, int by, int bw, int bh) const;
    void body_box(const Ent& e, const Bdy& b, int& x, int& y) const;
    void itr_box(const Ent& e, const Itr& i, int& x, int& y) const;
    friend struct AiHelper;
};

}  // namespace lf2

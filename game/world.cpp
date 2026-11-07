// SPDX-License-Identifier: MIT
// Copyright (c) 2026 OpenLF2 contributors

// Control, animation and movement. The rules follow OpenLF2's control.lua, animation.lua and physics.lua.

#include "world.hpp"
#include "assets.h"

namespace lf2 {

namespace {
// Frames every character shares.
constexpr int F_STAND = 0, F_WALK = 5, F_RUN = 9, F_RUN_ATTACK = 85, F_JUMP_ATTACK = 80, F_DASH_ATTACK = 90,
              F_ROWING = 100, F_DEFEND = 110, F_BROKEN = 112, F_FALLING = 180, F_JUMP = 210, F_DASH = 213,
              F_CROUCH = 215, F_STOP_RUN = 218, F_CROUCH_ALT = 219, F_LYING = 230;
// Frame states.
enum State { S_STAND = 0, S_WALK = 1, S_RUN = 2, S_ATTACK = 3, S_JUMP = 4, S_DASH = 5, S_ROW = 6, S_DEFEND = 7,
             S_BROKEN = 8, S_CATCH = 9, S_CAUGHT = 10, S_HURT = 11, S_FALL = 12, S_ICE = 13, S_LYING = 14,
             S_DIZZY = 16, S_BURN = 18, S_BURN_RUN = 19 };
constexpr int input_press_frames = 5;
constexpr int run_double_tap = 10;
constexpr fx gravity = FX(1.7);

fx fx_div14(fx v) { return sdiv(v * 5, 7); }              // v / 1.4
fx fx_div12(fx v) { return sdiv(v * 5, 6); }              // v / 1.2

bool ball_like(const Ent& e) {
    const int s = e.def->frame(e.frame).state;
    return e.kind == 3 || (s >= 3000 && s <= 3006);
}
}  // namespace

namespace {
// ceil(2^32 / n): x % n for x < 2^24 and n < 256 without the slow software division of the ARM7.
struct Reciprocals {
    std::uint32_t value[256];
    constexpr Reciprocals() : value() {
        for (unsigned n = 2; n < 256; ++n) value[n] = std::uint32_t(((std::uint64_t(1) << 32) + n - 1) / n);
    }
};
constexpr Reciprocals reciprocals;
}  // namespace

// x / d for x < 2^24 and 1 <= d < 256, exactly, without the ARM7's software division.
LF2_HOT static unsigned small_div(unsigned x, unsigned d) {
    if (d < 2 || d >= 256 || x >= (1u << 24)) return d == 1 ? x : x / d;
    return unsigned((std::uint64_t(x) * reciprocals.value[d]) >> 32);
}

LF2_HOT int sdiv(int x, int d) {
    const unsigned magnitude = x < 0 ? 0u - unsigned(x) : unsigned(x);
    const int q = int(small_div(magnitude, unsigned(d)));
    return x < 0 ? -q : q;
}

LF2_HOT int World::rnd(int n) {
    seed_ = seed_ * 1664525u + 1013904223u;
    const unsigned x = seed_ >> 8, m = unsigned(n);
    if (m < 2) return m == 1 ? 0 : int(x % m);
    if (m >= 256) return int(x % m);
    const unsigned q = unsigned((std::uint64_t(x) * reciprocals.value[m]) >> 32);
    return int(x - q * m);
}

void World::play(int wav) {
    if (wav <= 0 || sound_count >= max_sounds) return;
    for (int i = 0; i < sound_count; ++i) if (sounds[i] == wav) return;
    sounds[sound_count++] = wav;
}

bool World::stage_allies_alive() const {
    for (const Ent& e : ents)
        if (e.live && e.kind == 0 && e.team == 1 && e.hp > 0) return true;
    return false;
}

ArmyStats World::army_stats() const {
    ArmyStats result;
    for (const Ent& e : ents) if (e.live && e.kind == 0 && e.hp > 0) {
        const int side = e.team == 1 ? 0 : 1;
        ++result.living[side]; result.hp[side] += e.hp;
    }
    return result;
}

int World::army_census(int side, int id) const {
    int count = 0;
    for (const Ent& e : ents)
        if (e.live && e.army_side == side && e.def && e.def->hdr->id == id) ++count;
    return count;
}

StageStats World::stage_stats() const {
    StageStats result;
    const Ent& player = ents[0];
    const bool player_alive = player.live && player.kind == 0 && player.team == 1 && player.hp > 0;
    for (const Ent& e : ents) {
        if (!e.live || e.kind != 0) continue;
        if (e.team == 2) {
            if (e.hp > 0) { ++result.enemies; result.enemy_hp += e.hp; }
        } else {
            if (e.hp > 0) { ++result.allies; result.ally_hp += e.hp; }
            if (player_alive && e.stage_lives >= 2) result.reserves += e.stage_lives - 1;
        }
    }
    return result;
}

void World::stage_update_reserves() {
    const Ent& player = ents[0];
    const bool player_alive = player.live && player.kind == 0 && player.team == 1 && player.hp > 0;
    for (Ent& e : ents) {
        if (!e.live || e.kind != 0 || e.team != 1) continue;
        if ((player_alive && e.stage_lives < 0) || (!player_alive && e.stage_lives >= 2))
            e.stage_lives = -e.stage_lives;
    }
}

void World::stage_transition(int difficulty) {
    for (Ent& e : ents) {
        if (!e.live) continue;
        e.order_x = e.order_z = -1000;
        if (e.kind == 0) {
            e.x = (50 + rnd(30)) * 256;
            const int depth = stage.z2 - stage.z1;
            e.z = (stage.z1 + (depth > 0 ? rnd(depth) : 0)) * 256;
            e.dark_hp += (difficulty + 2) * 50;
            if (e.dark_hp > e.stage_max_hp) e.dark_hp = e.stage_max_hp;
            e.hp = e.dark_hp; e.mp = 500;
            if (e.frame >= 9 && e.frame <= 11) e.frame = 0;
            if (e.frame >= 16 && e.frame <= 18) e.frame = 12;
        }
    }
    int followers[max_fighters] = {};
    for (int i = 0; i < max_ents; ++i) {
        Ent& e = ents[i];
        const bool spawned = i >= max_fighters || (stage_mode && i > 0);
        if (!spawned) continue;
        if (e.live && e.kind != 0 && e.holder < 0) { e.live = false; continue; }
        if (e.follow < 0 || e.follow >= max_fighters) continue;
        if (followers[e.follow] < 2) ++followers[e.follow];
        else if (e.live) e.live = false;
    }
    camera = camera_step = 0;
    stage_bound = camera_limit = stage_exit_x = 0;
    stage_exit = false;
}

void World::init(const Stage& s, std::uint32_t seed) {
    for (auto& e : ents) e = Ent();
    for (auto& sp : sparks) sp.live = false;
    for (auto& group : groups) group = nullptr;
    groups[max_fighters] = &weapon_group();
    stage = s;
    camera = camera_step = 0;
    stage_bound = camera_limit = stage_exit_x = 0;
    counter_12 = counter_3 = 0;
    round_counter = 0;
    winner = -1;
    finished = false;
    stage_mode = false;
    stage_exit = false;
    background_drinks = true;
    clamp_edge_drinks = false;
    auto_finish_round = false;
    ai_difficulty = 0;
    difficulty = 1;
    ai_stage_mode = ai_random_game = false;
    fighters = 0;
    sound_count = 0;
    army_damage[0] = army_damage[1] = army_deaths[0] = army_deaths[1] = 0;
    seed_ = seed ? seed : 1;
    frame_number_ = 0;
    shake = 0;
    teleport_phase = 0;
}

int World::add_fighter(int roster, int team, bool human, int aggression) {
    const int slot = fighters++;
    const Group& g = roster_group(roster);
    groups[slot] = &g;
    Ent& e = ents[slot];
    e = Ent();
    e.live = true;
    e.def = &g.objs[0];
    e.data_group = &g;
    e.group = slot;
    e.kind = 0;
    e.team = team;
    e.human = human;
    e.owner = slot;
    e.ai.enabled = !human;
    e.ai.aggression = aggression;
    return slot;
}

void World::place_fighters() {
    for (int i = 0; i < fighters; ++i) {
        Ent& e = ents[i];
        const int span = stage.z2 - stage.z1;
        e.z = (stage.z1 + (span > 0 ? rnd(span) : 0)) * 256;
        const int half = stage.width / 2;
        e.x = (stage.width / 4 + (half > 0 ? rnd(half) : 0)) * 256;
        e.facing = 0;
        e.y = 0;
        e.frame = F_STAND;
        e.prev_frame = e.state_frame = e.frame;
        e.blink = 75;
    }
    camera = stage.width / 2 - 397;
    if (camera < 0) camera = 0;
    if (camera > stage.width - 794) camera = stage.width - 794;
}

void World::set_keys(int slot, const Keys& k) {
    Ent& e = ents[slot];
    e.prev = e.keys;
    e.keys = k;
}

Ent* World::find_free() {
    for (int i = max_fighters; i < max_ents; ++i) if (!ents[i].live) return &ents[i];
    return nullptr;
}

int World::spawn(const ObjDef* def, int group, int owner, int frame, int team, fx x, fx y, fx z, fx vx, fx vy, fx vz, int facing) {
    scan_valid = false;    // the scan list does not know the new entity
    for (int i = max_fighters; i < max_ents; ++i) {
        if (ents[i].live) continue;
        for (auto& spark : sparks) if (spark.live && spark.owner == i) spark.live = false;
        Ent& e = ents[i];
        e = Ent();
        e.live = true;
        e.def = def;
        e.data_group = groups[group];
        e.group = group;
        e.kind = def->kind();
        e.drop_counter = def->hdr->weapon_hp;
        e.owner = owner;
        e.team = team;
        e.x = x; e.y = y; e.z = z;
        e.vx = vx; e.vy = vy; e.vz = vz;
        e.frame = frame;
        e.facing = facing;
        // A new object does not hit its own team's older attacks for a moment; clear stale rests against it.
        for (auto& o : ents) o.pair_rest[i] = 0;
        return i;
    }
    return -1;
}

void World::remove(int index) {
    Ent& e = ents[index];
    if (index < max_fighters) { e.frame = F_STAND; return; }
    e.live = false;
}

// ---------------------------------------------------------------------------------------------------------
// Control

void World::enter_frame(Ent& e, int target) {
    bool turn = false;
    if (target < 0) { target = -target; turn = true; }
    if (target == 999) target = 0;
    if (!e.def->defined(target)) return;
    const int cost = e.def->frame(target).mp;
    const int mp_cost = cost % 1000;
    const int hp_cost = (cost / 1000) * 10;
    if (e.mp >= mp_cost && e.hp > hp_cost) {
        e.hp -= hp_cost;
        e.mp -= mp_cost;
        e.frame = target;
        if (turn) e.facing = 1 - e.facing;
        for (auto& p : e.press) p = 0;
    }
}

namespace {
struct SeqSpec { int second; char expect_second; int third; char expect_third; int hit; int face; };
// press indices: defend, then `second` (a direction or jump), then `third` (attack or jump)
constexpr SeqSpec seq_specs[9] = {
    {p_right, 'R', p_attack, 'a', 0, 0}, {p_left, 'L', p_attack, 'a', 0, 1}, {p_up, 'U', p_attack, 'a', 1, -1},
    {p_down, 'D', p_attack, 'a', 2, -1}, {p_right, 'R', p_jump, 'j', 3, 0}, {p_left, 'L', p_jump, 'j', 3, 1},
    {p_up, 'U', p_jump, 'j', 4, -1}, {p_down, 'D', p_jump, 'j', 5, -1}, {p_jump, 'j', p_attack, 'a', 6, -1},
};
int press_of(char c) {
    switch (c) {
    case 'U': return p_up; case 'D': return p_down; case 'L': return p_left; case 'R': return p_right;
    case 'd': return p_defend; case 'j': return p_jump; default: return p_attack;
    }
}
int hit_field(const Frame& f, int which) {
    switch (which) {
    case 0: return f.hit_Fa; case 1: return f.hit_Ua; case 2: return f.hit_Da;
    case 3: return f.hit_Fj; case 4: return f.hit_Uj; case 5: return f.hit_Dj; default: return f.hit_ja;
    }
}
bool interrupted(const Ent& e, char expected, bool advanced) {
    for (int i = 0; i < press_count; ++i)
        if (e.press[i] == input_press_frames && (!advanced || i != press_of(expected))) return true;
    return false;
}
}  // namespace

void World::recognize(Ent& e, int n) {
    const SeqSpec& spec = seq_specs[n];
    int state = e.seq[n];
    bool advanced = false;
    if (state == 0 && e.press[p_defend] == input_press_frames) { state = 1; advanced = true; }
    if (state == 1) {
        if (e.press[spec.second] == input_press_frames) { state = 2; advanced = true; }
        else if (interrupted(e, 'd', advanced)) state = 0;
    }
    if (state == 2) {
        if (e.press[spec.third] == input_press_frames) { state = 3; advanced = true; }
        else if (interrupted(e, spec.expect_second, advanced)) state = 0;
    }
    if (state == 3) {
        const int target = hit_field(e.def->frame(e.frame), spec.hit);
        if (n == 8 && e.def->hdr->id == 6 && target == 300 && e.hp > 177) { e.seq[n] = 3; return; }
        const bool heavy = e.held_item >= 0 && e.held_item < max_ents &&
                           ents[e.held_item].live && ents[e.held_item].kind == 2;
        if (target != 0 && !heavy && (n != 8 || !e.transform_original)) {
            enter_frame(e, target);
            if (spec.face >= 0) e.facing = spec.face;
            e.seq[n] = 0;
            return;
        }
        if (n == 8 && e.merge_original) { e.merge_timer = 0; e.seq[n] = 3; return; }
        if (interrupted(e, spec.expect_third, advanced)) state = 0;
    }
    e.seq[n] = std::uint8_t(state);
}

void World::walking(Ent& e) {
    const ObjDef& d = *e.def;
    const Keys& k = e.keys;
    const int rate = d.mv(walk_rate) ? d.mv(walk_rate) : 1;
    const bool heavy = e.held_item >= 0 && ents[e.held_item].live && ents[e.held_item].kind == 2;
    fx speed = d.mv(heavy ? heavy_walk_speed : walk_speed), speedz = d.mv(heavy ? heavy_walk_speedz : walk_speedz);
    if (e.dash > 0) --e.dash;
    if (e.dash < 0) ++e.dash;
    if (heavy && e.frame < 12) e.frame = 12;
    auto cycle = [&]() {
        const int limit = rate * 6;
        e.walk_phase = e.walk_phase + 1 < limit ? e.walk_phase + 1 : (e.walk_phase + 1) % limit;
        const int first = heavy ? 12 : F_WALK;
        const int step = int(small_div(unsigned(e.walk_phase), unsigned(rate)));
        if (e.walk_phase < rate * 4) e.frame = step + first;
        else e.frame = first + 6 - step;
    };
    if (k.right && !k.left && to_int(e.y) == 0) {
        if (e.facing == 1) e.dash = 0;
        e.facing = 0;
        cycle();
        e.vx = speed;
        if (!e.prev.right) e.dash += run_double_tap;
        if (e.dash >= run_double_tap + 1) { e.frame = heavy ? 16 : F_RUN; e.walk_phase = e.dash = 0; }
    }
    if (!k.right && k.left && to_int(e.y) == 0) {
        if (e.facing == 0) e.dash = 0;
        e.facing = 1;
        cycle();
        e.vx = -speed;
        if (!e.prev.left) e.dash -= run_double_tap;
        if (e.dash <= -(run_double_tap + 1)) { e.frame = heavy ? 16 : F_RUN; e.walk_phase = e.dash = 0; }
    }
    const bool sideways = k.left == k.right;
    if (k.up && !k.down && to_int(e.y) == 0) {
        if (sideways) cycle();
        e.vz = -speedz;
        e.vx = fx_div14(e.vx);
    }
    if (k.down && !k.up && to_int(e.y) == 0) {
        if (sideways) cycle();
        e.vz = speedz;
        e.vx = fx_div14(e.vx);
    }
    if (k.attack && e.press[p_attack] > 0) {
        e.dash = 0;
        e.wait = 0;
        if (heavy) e.frame = 50;
        else if (e.held_item >= 0) {
            const Ent& item = ents[e.held_item];
            if ((item.def->hdr->id == 120 || item.def->hdr->id == 124) && (k.left || k.right || k.up || k.down)) e.frame = 45;
            else if (item.kind == 4 || (item.kind == 6 && item.hp <= 0)) e.frame = 45;
            else if (item.kind == 6) e.frame = 55;
            else e.frame = (rnd(2) + 4) * 5;
        } else e.frame = e.super_window > 0 ? 70 : (rnd(2) + 12) * 5;
        if (e.held_item < 0 && e.super_window <= 0) {
            e.mp -= e.def->frame(e.frame).mp;
            if (e.mp < 0) e.mp = 0;
        }
    }
    if (heavy) return;
    if (k.jump && e.press[p_jump] > 0) { e.wait = 0; e.dash = 0; e.frame = F_JUMP; }
    if (k.defend && e.defend_lock == 0 && e.press[p_defend] > 0) { e.dash = 0; e.wait = 0; e.frame = F_DEFEND; }
}

void World::running(Ent& e) {
    const ObjDef& d = *e.def;
    const Keys& k = e.keys;
    const int rate = d.mv(run_rate) ? d.mv(run_rate) : 1;
    e.wait = 0;
    const int limit = rate * 4;
    e.walk_phase = e.walk_phase + 1 < limit ? e.walk_phase + 1 : (e.walk_phase + 1) % limit;
    const bool heavy = e.held_item >= 0 && ents[e.held_item].live && ents[e.held_item].kind == 2;
    const int run_frame = heavy ? 16 : F_RUN, stop_frame = heavy ? 19 : F_STOP_RUN;
    if (e.walk_phase < rate * 3) e.frame = int(small_div(unsigned(e.walk_phase), unsigned(rate))) + run_frame;
    else e.frame = run_frame + 1;
    const fx speed = d.mv(heavy ? heavy_run_speed : run_speed), speedz = d.mv(heavy ? heavy_run_speedz : run_speedz);
    if (e.facing == 0) { e.vx = speed; if (k.left) e.frame = stop_frame; }
    if (e.facing == 1) { e.vx = -speed; if (k.right) e.frame = stop_frame; }
    if (k.up && !k.down && to_int(e.y) == 0) { e.vz = -speedz; e.vx = fx_div12(e.vx); }
    if (!k.up && k.down && to_int(e.y) == 0) { e.vz = speedz; e.vx = fx_div12(e.vx); }
    if (k.attack && e.press[p_attack] > 0) {
        const bool directions = k.left || k.right || k.up || k.down;
        int target = heavy ? 50 : F_RUN_ATTACK;
        if (!heavy && e.held_item >= 0 && e.held_item < max_ents && ents[e.held_item].live) {
            const Ent& item = ents[e.held_item];
            if (item.kind == 4 || (item.kind == 6 && (item.hp <= 0 || directions))) target = 45;
            else if (item.kind == 6) target = 55;
            else target = directions ? 45 : 35;
        }
        const int cost = d.frame(target).mp;
        if (heavy || (e.held_item >= 0 && e.held_item < max_ents && ents[e.held_item].live)) e.frame = target;
        else if (e.mp >= cost) { e.mp -= cost; e.frame = target; }
    }
    if (heavy) return;
    if (k.defend && e.press[p_defend] > 0) e.frame = F_ROWING + 2;
    if (k.jump && e.press[p_jump] > 0) {
        play(7);
        e.dash = 0;
        e.frame = F_DASH;
        e.vx = (e.facing == 0 ? 1 : -1) * d.mv(dash_distance);
        e.vy = d.mv(dash_height);
        if (k.up && !k.down) e.vz = -d.mv(dash_distancez);
        else if (!k.up && k.down) e.vz = d.mv(dash_distancez);
    }
}

void World::jumping(Ent& e) {
    const Keys& k = e.keys;
    if (k.right && !k.left) e.facing = 0;
    if (k.left && !k.right) e.facing = 1;
    if (!k.attack) return;
    if (e.held_item >= 0 && e.held_item < max_ents && ents[e.held_item].live) {
        const Ent& item = ents[e.held_item];
        if (item.kind == 1) {
            e.wait = 0;
            e.frame = (k.left || k.right || k.up || k.down) ? 52 : 30;
        } else if (item.kind == 4 || item.kind == 6) e.frame = 52;
        return;
    }
    e.wait = 0;
    e.frame = F_JUMP_ATTACK;
    e.mp -= e.def->frame(80).mp;
    if (e.mp < 0) e.mp = 0;
}

void World::crouching(Ent& e) {
    const ObjDef& d = *e.def;
    const Keys& k = e.keys;
    if (k.defend && e.press[p_defend] > 0) e.frame = F_ROWING + 2;
    if (k.jump && (k.right || e.vx > 0) && e.press[p_jump] > 0) {
        play(7);
        e.frame = F_DASH + e.facing;
        e.dash = 0;
        e.vx = d.mv(dash_distance);
        e.vy = d.mv(dash_height);
    }
    if (k.jump && (k.left || e.vx < 0) && e.press[p_jump] > 0) {
        play(7);
        e.frame = F_DASH + 1 - e.facing;
        e.dash = 0;
        e.vx = -d.mv(dash_distance);
        e.vy = d.mv(dash_height);
    }
    if (k.up && !k.down) e.vz = -d.mv(dash_distancez);
    else if (!k.up && k.down) e.vz = d.mv(dash_distancez);
}

void World::recovering(Ent& e) {
    const ObjDef& d = *e.def;
    if (!(e.fall_damage >= 0 && e.keys.jump && e.press[p_jump] > 0 && e.hp > 0)) return;
    if (e.facing == 0) e.frame = e.vx <= 0 ? 100 : 108;
    else e.frame = e.vx >= 0 ? 100 : 108;
    e.wait = 0;
    const fx height = d.mv(row_height), dist = d.mv(row_distance);
    if (height < e.vy) e.vy = height;
    if (e.vx < 256 && e.vx > -256) e.vx = e.facing == 1 ? dist : -dist;
    else e.vx = e.vx > 0 ? dist : -dist;
}

void World::dashing(Ent& e) {
    const Keys& k = e.keys;
    if (k.right && !k.left) e.facing = 0;
    if (e.facing == 0 && e.frame != 217 && e.vx < 0) e.frame = F_DASH + 1;
    if (e.facing == 0 && e.frame != 216 && e.vx > 0) e.frame = F_DASH;
    if (k.left && !k.right) e.facing = 1;
    if (e.facing == 1 && e.frame != 217 && e.vx > 0) e.frame = F_DASH + 1;
    if (e.facing == 1 && e.frame != 216 && e.vx < 0) e.frame = F_DASH;
    const bool forward = (e.vx > 0 && e.facing == 0) || (e.vx < 0 && e.facing == 1);
    if (!forward || !k.attack) return;
    if (e.held_item >= 0 && e.held_item < max_ents && ents[e.held_item].live) {
        const Ent& item = ents[e.held_item];
        const bool directions = k.left || k.right || k.up || k.down;
        if (item.kind == 1 || ((item.kind == 4 || item.kind == 6) && directions)) {
            e.frame = item.kind == 1 ? 40 : 52;
            e.vy -= FX(1);
            e.wait = 0;
        }
        return;
    }
    const int cost = e.def->frame(F_DASH_ATTACK).mp;
    if (e.mp >= cost) { e.mp -= cost; e.frame = F_DASH_ATTACK; }
}

LF2_HOT void World::frame_velocity(Ent& e) {
    const Frame& f = e.def->frame(e.frame);
    const int dvx = f.dvx;
    if (dvx > 500) e.vx = (dvx - 550) * 256;
    else {
        const fx v = dvx * 256;
        if (dvx > 0 && v > e.vx && e.facing == 0) e.vx = v;
        if (dvx > 0 && -e.vx < v && e.facing == 1) e.vx = -v;
        if (dvx < 0 && v < e.vx && e.facing == 0) e.vx = v;
        if (dvx < 0 && -e.vx > v && e.facing == 1) e.vx = -v;
    }
    if (f.dvy != 0) {
        if (f.dvy > 500) e.vy = (f.dvy - 550) * 256;
        else e.vy += f.dvy * 256;
    }
    if (f.dvz != 0) {
        if (f.dvz > 500) e.vz = (f.dvz - 550) * 256;
        else {
            if (e.keys.up && e.press[p_up] >= e.press[p_down]) e.vz = -f.dvz * 256;
            if (e.keys.down && e.press[p_up] <= e.press[p_down]) e.vz = f.dvz * 256;
        }
    }
}

LF2_HOT void World::control(int index) {
    Ent& e = ents[index];
    for (auto& p : e.press) if (p > 0) --p;
    if (e.defend_lock > 0) --e.defend_lock;
    if (e.kind == 0) {
        auto fresh = [&](bool now, bool before, int key, int code) {
            if (!before && now) {
                e.press[key] = input_press_frames;
                for (int i = 0; i < 4; ++i) e.history[i] = e.history[i + 1];
                e.history[4] = std::uint8_t(code);
            }
        };
        fresh(e.keys.right, e.prev.right, p_right, 6);
        fresh(e.keys.left, e.prev.left, p_left, 4);
        fresh(e.keys.up, e.prev.up, p_up, 8);
        fresh(e.keys.down, e.prev.down, p_down, 2);
        fresh(e.keys.defend, e.prev.defend, p_defend, 9);
        fresh(e.keys.jump, e.prev.jump, p_jump, 0);
        fresh(e.keys.attack, e.prev.attack, p_attack, 5);
        for (int i = 0; i < 9; ++i) recognize(e, i);
        const Frame& f = e.def->frame(e.frame);
        if (f.hit_a != 0 && e.press[p_attack] > e.press[p_defend] && e.press[p_attack] > e.press[p_jump]) {
            enter_frame(e, f.hit_a);
            e.press[p_attack] = 0;
        } else if (f.hit_d != 0 && e.press[p_defend] > e.press[p_attack] && e.press[p_defend] > e.press[p_jump]) {
            enter_frame(e, f.hit_d);
            e.press[p_defend] = 0;
        } else if (f.hit_j != 0 && e.press[p_jump] > e.press[p_attack] && e.press[p_jump] > e.press[p_defend]) {
            enter_frame(e, f.hit_j);
            e.press[p_jump] = 0;
        }
        if (e.frame == F_DEFEND) {
            if (e.keys.right) e.facing = 0;
            if (e.keys.left) e.facing = 1;
        }
        int state = e.def->frame(e.frame).state;
        if (state == 301 || state == S_BURN_RUN) {
            if (e.keys.up && !e.keys.down && to_int(e.y) == 0) e.vz = -e.def->mv(run_speedz);
            if (e.keys.down && !e.keys.up && to_int(e.y) == 0) e.vz = e.def->mv(run_speedz);
        }
        if (state == S_STAND || state == S_WALK) walking(e);
        state = e.def->frame(e.frame).state;
        if (state == S_JUMP && to_int(e.y) < 0) jumping(e);
        state = e.def->frame(e.frame).state;
        if (state == S_RUN) running(e);
        if (e.frame == F_CROUCH) crouching(e);
        if (e.frame == 182 || e.frame == 188) recovering(e);
        if (e.def->frame(e.frame).state == S_DASH) dashing(e);
    }
    frame_velocity(e);
}

const Group* World::character_group(int id) const {
    for (int i = 0; i < ROSTER_COUNT; ++i) if (ROSTER_ID[i] == id) return &roster_group(i);
    return nullptr;
}

void World::change_character(Ent& e, const Group* group, int frame, bool change_followers) {
    if (!group || group->count == 0) return;
    e.def = &group->objs[0];
    e.data_group = group;
    e.kind = e.def->kind();
    if (e.group >= 0 && e.group < fighters && &ents[e.group] == &e) groups[e.group] = group;
    e.frame = e.def->defined(frame) ? frame : 0;
    const int index = int(&e - ents);
    if (change_followers && index >= 0 && index < max_ents) {
        for (Ent& follower : ents) {
            if (!follower.live || follower.kind != 0 || follower.follow != index || follower.hp <= 0) continue;
            follower.def = &group->objs[0]; follower.data_group = group;
            follower.frame = to_int(follower.y) < 0 && follower.def->defined(212) ? 212 : 0;
        }
    }
}

LF2_HOT void World::frame_rules(int index) {
    Ent& e = ents[index];
    const int state = e.def->frame(e.frame).state;
    if (state == 500 && (!e.transform_target || e.transform_original)) { e.frame = 0; return; }
    if (state == 501 && e.transform_target) {
        e.transform_original = e.data_group;
        change_character(e, e.transform_target, 0);
        return;
    }
    if ((state != 400 && state != 401) || teleport_phase != 0) return;
    const bool enemy = state == 400;
    int best = -1, distance = enemy ? 10000 : -1;
    for (int i = 0; i < max_ents; ++i) {
        const Ent& target = ents[i];
        if (i == index || !target.live || target.kind != 0 || target.hp <= 0 ||
            ((target.team != e.team) != enemy)) continue;
        int dx = to_int(target.x) - to_int(e.x), dz = to_int(target.z) - to_int(e.z);
        if (dx < 0) dx = -dx;
        if (dz < 0) dz = -dz;
        const int d = dx + dz;
        if ((enemy && d < distance) || (!enemy && d > distance)) { best = i; distance = d; }
    }
    e.y = 0;
    if (best >= 0) {
        const Ent& target = ents[best];
        const int offset = enemy ? 120 : 60;
        e.x = (to_int(target.x) + (e.facing == 0 ? -offset : offset)) * 256;
        e.z = (to_int(target.z) + 1) * 256;
    }
    e.vx = e.vy = e.vz = 0;
}

void World::split_fusion(Ent& e) {
    if (!e.merge_original || e.merge_partner < 0 || e.merge_partner >= max_ents) return;
    const int partner_index = e.merge_partner;
    Ent& partner = ents[partner_index];
    const ObjDef* partner_def = partner.def;
    const Group* partner_group = partner.data_group;
    const int partner_group_index = partner.group;
    const bool partner_human = partner.human, partner_ai = partner.ai.enabled;
    const int partner_aggression = partner.ai.aggression, partner_sprite = partner.sprite_slot;
    const int half_hp = e.hp / 2, half_dark = e.dark_hp / 2;
    e.def = &e.merge_original->objs[0];
    e.data_group = groups[e.group] = e.merge_original;
    e.merge_original = nullptr;
    e.merge_partner = -1;
    e.merge_timer = 900;
    e.hp = half_hp; e.dark_hp = half_dark; e.mp = 0;
    e.vx = 0; e.frame = 112; e.wait = 0;
    partner = Ent{};
    partner.live = true; partner.def = partner_def; partner.data_group = partner_group;
    partner.group = partner_group_index; partner.owner = partner_index;
    partner.human = partner_human; partner.ai.enabled = partner_ai; partner.ai.aggression = partner_aggression;
    partner.sprite_slot = partner_sprite;
    partner.team = e.team;
    partner.hp = half_hp; partner.dark_hp = half_dark; partner.mp = 0;
    partner.x = e.x; partner.z = e.z; partner.y = 0;
    partner.vx = partner.vy = partner.vz = 0;
    partner.facing = 1 - e.facing;
    partner.frame = partner.prev_frame = partner.state_frame = 112;
    partner.wait = 0;
}

void World::finish_round() {
    for (Ent& e : ents) {
        if (!e.live) continue;
        if (e.transform_original) {
            const Group* original = e.transform_original;
            e.transform_original = nullptr;
            const int frame = e.frame, wait = e.wait;
            change_character(e, original, 0, false);
            e.frame = frame; e.wait = wait;
        } else if (e.merge_original) split_fusion(e);
    }
}

void World::update_fusion() {
    for (int i = 0; i < fighters; ++i) {
        Ent& e = ents[i];
        if (e.merge_timer > 0) --e.merge_timer;
        if (!e.live) continue;
        if (e.merge_original && e.merge_partner >= 0 && e.merge_timer == 0 && (e.frame < 9 || e.frame > 260)) {
            split_fusion(e);
            continue;
        }
        // Stage and Army reinforcements live in fighter slots only as a GBA
        // storage shortcut; the reference keeps them outside fusion's slot range.
        if (stage_mode && i > 0) continue;
        const int id = e.def->hdr->id;
        if ((id != 7 && id != 8) || e.hp <= 0 || e.hp >= 177 || e.merge_timer != 0 ||
            e.def->frame(e.frame).state != S_RUN) continue;
        for (int j = 0; j < fighters; ++j) {
            if (stage_mode && j > 0) continue;
            Ent& other = ents[j];
            if (i == j || !other.live || other.def->hdr->id != (id == 7 ? 8 : 7) || other.team != e.team ||
                other.hp <= 0 || other.hp >= 177 || other.merge_timer != 0 || other.def->frame(other.frame).state != S_RUN ||
                to_int(e.x) <= to_int(other.x)) continue;
            int dx = to_int(e.x) - to_int(other.x), dz = to_int(e.z) - to_int(other.z);
            if (dz < 0) dz = -dz;
            if (dx >= 50 || dz >= 8) continue;
            const Group* merged = character_group(51);
            if (!merged || !merged->objs[0].defined(290)) break;
            e.merge_original = e.data_group;
            e.merge_partner = j;
            e.merge_timer = 4500;
            e.hp += other.hp;
            e.dark_hp += other.dark_hp;
            if (e.dark_hp > e.stage_max_hp) e.dark_hp = e.stage_max_hp;
            if (e.hp > e.dark_hp) e.hp = e.dark_hp;
            e.x = ((to_int(e.x) + to_int(other.x)) / 2) * 256;
            e.z = ((to_int(e.z) + to_int(other.z)) / 2) * 256;
            e.vx = 0; e.mp = 500; e.frame = 290; e.wait = 0;
            e.def = &merged->objs[0];
            e.data_group = groups[e.group] = merged;
            e.list_n = 0;
            other.live = false;
            break;
        }
    }
}

void World::spawn_weapons() {
    const Group* g = groups[max_fighters];
    if (!g) return;
    int count = 0;
    for (const Ent& e : ents) if (e.live && (e.kind == 1 || e.kind == 2 || e.kind == 4 || e.kind == 6)) ++count;
    if (count >= 4 || rnd(200) != 0) return;
    int choices[32], n = 0;
    for (int i = 0; i < g->count; ++i) {
        const ObjDef& d = g->objs[i];
        if (d.hdr->id >= 100 && d.hdr->id < 200 && (d.kind() == 1 || d.kind() == 2 || d.kind() == 4 || d.kind() == 6)) {
            if ((d.hdr->id == 122 || d.hdr->id == 123) && (rnd(2) == 0 || !background_drinks)) continue;
            if (n < 32) choices[n++] = i;
        }
    }
    const int span = stage_bound != 0 ? stage_bound : stage.width;
    const int xa = rnd(30), xb = rnd(30);
    const int x = xb + 30 + xa * ((span - 60) / 30);
    const int za = rnd(30), zb = rnd(30);
    const int z = zb + stage.z1 + 30 + za * ((stage.z2 - stage.z1 - 60) / 30);
    if (!n) return;
    const ObjDef* d = &g->objs[choices[rnd(n)]];
    const int slot = spawn(d, max_fighters, 99, 0, 0, x * 256, FX(-500.0), z * 256, 0, 0, 0, 0);
    if (slot >= 0 && d->hdr->id == 122) ents[slot].hp = 200;
}

void World::carry_weapons() {
    // Held-item positions and hitboxes use the holder's already-clamped depth.
    for (Ent& e : ents) if (e.live && e.kind == 0) {
        if (e.z < stage.z1 * 256) e.z = stage.z1 * 256;
        if (e.z > stage.z2 * 256) e.z = stage.z2 * 256;
    }
    for (int i = 0; i < max_ents; ++i) {
        Ent& item = ents[i];
        if (!item.live || item.holder < 0 || item.holder >= max_ents) continue;
        Ent& holder = ents[item.holder];
        if (!holder.live || holder.held_item != i) { item.holder = -1; continue; }
        const Frame& hf = holder.def->frame(holder.frame);
        if (hf.state == 17 && item.kind == 6 && item.hp > 0) {
            if (item.def->hdr->id == 122) {
                --item.hp;
                if (item.hp % 5 == 0) { holder.dark_hp += 2; if (holder.dark_hp > holder.stage_max_hp) holder.dark_hp = holder.stage_max_hp; holder.hp += 4; if (holder.hp > holder.dark_hp) holder.hp = holder.dark_hp; }
                if (item.hp % 6 == 0) { holder.mp += 5; if (holder.mp > 500) holder.mp = 500; }
            } else if (item.def->hdr->id == 123) {
                item.hp -= 2; holder.mp += 3; if (holder.mp > 500) holder.mp = 500;
                if (item.follow >= 0 && item.mp > 150) holder.mp = 150;
            }
            if (item.hp <= 0) {
                item.holder = -1; holder.held_item = -1; holder.frame = 0;
                item.frame = 0; item.vy = FX(-8.0); item.vx = (rnd(7) - 3) * 256; item.drop_counter = 0;
                continue;
            }
        }
        const int action = hf.wp_action;
        if (item.def->defined(action)) item.frame = action;
        const Frame& wf = item.def->frame(item.frame);
        item.facing = holder.facing;
        item.hit_lag = holder.hit_lag;
        const int x = to_int(holder.x) + (holder.facing == 0 ? hf.wp_x - hf.cx : hf.cx - hf.wp_x);
        const int y = to_int(holder.y) - hf.cy + hf.wp_y;
        item.x = (x + (item.facing == 0 ? wf.cx - wf.wp_x : wf.wp_x - wf.cx)) * 256;
        item.y = (y + wf.cy - wf.wp_y + (hf.wp_cover == 0 ? -1 : 1)) * 256;
        item.z = (to_int(holder.z) + (hf.wp_cover == 0 ? 1 : -1)) * 256;
        const bool knocked_loose = hf.state == S_FALL || hf.state == S_CAUGHT;
        const bool thrown = hf.wp_dvx != 0 && (item.kind == 1 || item.kind == 2 || item.kind == 4 || item.kind == 6);
        if (knocked_loose || thrown || hf.wp_kind == 3) {
            item.holder = -1;
            holder.held_item = -1;
            if (knocked_loose) {
                item.frame = rnd(16);
                item.vx = (holder.hit_count == 1 ? holder.ix : holder.vx) / 3;
                item.vy = holder.hit_count == 1 ? holder.iy : holder.vy;
                if (item.y > FX(-2.0)) item.y = FX(-2.0);
            }
            if (thrown) {
                if (item.kind != 2) item.thrower = int(&holder - ents);
                item.frame = item.kind == 2 ? rnd(6) : 40;
                item.vx = (holder.facing == 0 ? hf.wp_dvx : -hf.wp_dvx) * 256;
                item.vy = hf.wp_dvy * 256;
                if (holder.keys.up && !holder.keys.down) item.vz = -hf.wp_dvz * 256;
                else if (holder.keys.down && !holder.keys.up) item.vz = hf.wp_dvz * 256;
            }
            if (hf.wp_kind == 3) {
                item.frame = rnd(6); item.vx = (rnd(7) - 3) * 256;
                item.vy = -rnd(4) * 256; item.vz = (rnd(5) - 2) * 256 / 5;
            }
        }
    }
}

// A holder whose item is gone or belongs to someone else lets go of it.
void World::check_holders() {
    for (int i = 0; i < max_ents; ++i) {
        Ent& holder = ents[i];
        if (!holder.live || holder.held_item < 0) continue;
        if (holder.held_item >= max_ents || !ents[holder.held_item].live || ents[holder.held_item].holder != i) holder.held_item = -1;
    }
}

void World::stage_revival() {
    for (int i = 0; i < max_ents; ++i) stage_revival(i);
}

void World::stage_revival(int i) {
    Ent& e = ents[i];
    // Scripted stage fighters occupy the normal fighter slots on GBA;
    // followers and other spawned characters use the object slots.
    if (i < max_fighters && e.follow < 0 && !(stage_mode && i > 0)) return;
    if (!e.live || e.kind != 0 || e.hp > 0 || e.def->frame(e.frame).state != S_LYING) return;
    if (e.blink <= 0 || e.blink >= 5) return;
    if (e.stage_join_hp > 0) {
        e.team = 1;
        if (e.def->hdr->id >= 30 && e.def->hdr->id <= 36) e.picture_offset = 140;
        e.stage_max_hp = e.stage_join_hp;
        e.stage_lives = e.stage_join_lives;
        e.stage_join_hp = e.stage_join_lives = 0;
        e.hp = e.dark_hp = e.stage_max_hp;
        e.mp = 0; e.frame = 219; e.wait = 0; e.hit_lag = 10;
        const ObjDef* marker = find_def(groups[max_fighters], 998);
        if (marker) spawn(marker, max_fighters, i, 6, e.team, e.x, e.y, e.z, 0, 0, 0, 0);
    } else if (e.stage_lives >= 2) {
        --e.stage_lives;
        int x = 0, z = 0, count = 0;
        for (int j = 0; j < max_ents; ++j) {
            const Ent& mate = ents[j];
            if (j != i && mate.live && mate.kind == 0 && mate.team == e.team) {
                x += to_int(mate.x); z += to_int(mate.z); ++count;
            }
        }
        if (count) { e.x = (x / count + rnd(51) - 26) * 256; e.z = (z / count + rnd(31) - 16) * 256; }
        e.hp = e.dark_hp = e.stage_max_hp; e.mp = 500;
        e.blink = 20; e.frame = 212;
        e.y = FX(-300); e.vy = 0;
    } else e.live = false;
}

void World::after_physics(int index) {
    stage_revival(index);
    Ent& e = ents[index];
    if (e.live && e.transform_original && e.seq[8] == 3 && to_int(e.y) == 0 && e.hp > 0) {
        const Group* original = e.transform_original;
        e.transform_original = nullptr;
        e.seq[8] = 0;
        change_character(e, original, 245);
    }
}

void World::status_effects(int index) {
    Ent& e = ents[index];
    const int previous = e.def->frame(e.state_frame).state;
    const int current = e.def->frame(e.frame).state;
    const bool thaw = (previous == S_ICE || e.state_frame == 200) && current != S_ICE && e.frame != 200;
    const bool burning = previous == S_BURN || previous == S_BURN_RUN;
    if (!thaw && !burning) return;
    if (thaw) play(66);
    const ObjDef* debris = find_def(groups[max_fighters], 999);
    if (!debris) return;
    const int amount = thaw ? 15 : ((current == S_BURN || current == S_BURN_RUN) ? (rnd(4) == 0 ? 1 : 0) : 7);
    for (int piece = 0; piece < amount; ++piece) {
        if (!find_free()) break;
        const fx y = e.y - rnd(29) * 256;
        const fx x = e.x + (thaw ? rnd(39) - 19 : rnd(59) - 29) * 256;
        const fx vy = thaw ? (-8 - rnd(20) / 2) * 256 : FX(-1.0);
        const fx vx = (rnd(11) - 5) * 256 + (thaw ? e.ix / 2 : e.vx);
        const int frame = thaw ? (piece < 2 ? 120 : piece < 5 ? 130 : piece < 9 ? 125 : 135) : 140 + rnd(1);
        if (spawn(debris, max_fighters, 99, frame, 0, x, y, e.z, vx, vy, FX(0.1), 0) < 0) break;
    }
}

void World::team_orders(int index) {
    if (index >= max_fighters) return;
    Ent& leader = ents[index];
    if (!leader.live || leader.kind != 0 || leader.hp <= 0 || leader.history[1] != 9) return;
    const auto* h = leader.history;
    int command = -1;
    if (h[2] == 0 && h[3] == 9 && h[4] == 0) command = 0;
    else if (h[2] == 9 && h[3] == 9 && h[4] == 9) command = 2;
    else if (h[2] == 5 && h[3] == 9 && h[4] == 5) command = 4;
    if (command < 0) return;
    for (auto& key : leader.history) key = 0;
    const ObjDef* marker = find_def(groups[max_fighters], 998);
    if (!marker || spawn(marker, max_fighters, index, command, leader.team, leader.x, 0, leader.z, 0, 0, 0, 0) < 0) return;
    for (Ent& member : ents) {
        if (!member.live || member.kind != 0 || member.hp <= 0 || member.team != leader.team) continue;
        if (command == 0) {
            member.order_x = to_int(leader.x) + rnd(81) - 40;
            member.order_z = to_int(leader.z) + rnd(81) - 40;
        } else {
            member.follow_order = command == 2;
            member.order_leader = command == 2 ? index : -1;
        }
    }
}

// ---------------------------------------------------------------------------------------------------------
// Movement

LF2_HOT void World::character_ground(Ent& e) {
    const ObjDef& d = *e.def;
    if (e.y > 0 && e.vy > 0 && d.frame(e.frame).state == S_ICE) {
        if (e.vy > FX(17.0) || e.vx > FX(9.0) || e.vx < -FX(9.0)) {
            e.hp -= scaled_damage(e, 10);
            e.y = 0;
            e.vy = FX(-3.5);
            if (e.vx > FX(7.0)) e.vx = FX(7.0);
            if (e.vx < -FX(7.0)) e.vx = -FX(7.0);
            e.frame = 185;
        } else {
            e.y = 0; e.vy = 0; e.vx /= 3;
        }
    }
    if (e.y > 0 && e.vy == 0 && e.frame == F_JUMP + 2) {
        e.y = 0; e.vy = 0; e.vx /= 3; e.frame = F_CROUCH; e.wait = 0;
        return;
    }
    if (e.y > 0 && e.vy > 0) {
        const int state = d.frame(e.frame).state;
        if (state != S_FALL && state != S_BURN) {
            const int idx = e.frame;
            e.y = 0; e.vy = 0; e.vx /= 3;
            e.frame = state == 100 ? 94 : (idx == F_JUMP + 2 || state == S_ROW) ? F_CROUCH : F_CROUCH_ALT;
            e.wait = 0;
            return;
        }
        play(6);
        if (e.fall_damage != 0) {
            if (e.fall_damage < 0) e.fall_damage = -e.fall_damage;
            const int damage = scaled_damage(e, e.fall_damage);
            e.hp -= damage;
            e.dark_hp -= damage;
            e.fall_damage = 0;
        }
        if (e.vy > FX(11.0) || e.vx > FX(9.0) || e.vx < -FX(9.0) || state == S_BURN) {
            e.y = 0;
            e.vy = FX(-3.5);
            if (e.vx > FX(7.0)) e.vx = FX(7.0);
            if (e.vx < -FX(7.0)) e.vx = -FX(7.0);
            e.frame = (e.frame < 186 || state == S_BURN) ? 185 : 191;
        } else {
            e.y = 0; e.vy = 0; e.wait = 0; e.vx /= 3;
            e.frame = e.frame >= 186 ? F_LYING + 1 : F_LYING;
        }
    }
}

LF2_HOT void World::physics(int index) {
    Ent& e = ents[index];
    const ObjDef& d = *e.def;
    if (e.hit_lag != 0) {
        if (e.hit_lag > 0) --e.hit_lag;
        if (e.hit_lag >= 0) return;
        ++e.hit_lag;
        return;
    }
    if (e.holder >= 0) return;
    if (d.frame(e.frame).cp >= 0 && d.cp[d.frame(e.frame).cp].kind == 2) return;   // being held
    if (!((e.vx > 0 && e.blocked_right) || (e.vx < 0 && e.blocked_left))) e.x += e.vx;
    if (e.kind == 4 || d.hdr->id == 120) e.x += e.vx / 5;
    if (d.hdr->id == 101) e.x -= e.vx / 5;
    if (!((e.vz > 0 && e.blocked_down) || (e.vz < 0 && e.blocked_up))) e.z += e.vz;
    e.blocked_left = e.blocked_right = e.blocked_up = e.blocked_down = false;
    const bool ball = ball_like(e);
    if (ball && d.frame(e.frame).hit_j > 0) e.z += (d.frame(e.frame).hit_j - 50) * 256;
    if (to_int(e.y) >= 0) {
        // Ground friction: one unit per frame toward zero.
        if (e.vx > 0) { e.vx -= 256; if (e.vx < 0) e.vx = 0; }
        if (e.vx < 0) { e.vx += 256; if (e.vx > 0) e.vx = 0; }
        if (e.vz > 0) { e.vz -= 256; if (e.vz < 0) e.vz = 0; }
        if (e.vz < 0) { e.vz += 256; if (e.vz > 0) e.vz = 0; }
    }
    if ((e.kind == 4 || e.kind == 6) && d.frame(e.frame).state == 1000 &&
        (e.vx > FX(9) || e.vx < -FX(9)) && d.defined(40)) e.frame = 40;
    e.y += e.vy;
    if (e.y < 0) {
        if (!ball) {
            fx acceleration = gravity;
            if (e.kind == 6) acceleration = FX(1.133333);
            else if (e.kind == 4) acceleration = FX(0.85);
            else if (e.kind != 0 && d.frame(e.frame).state == 1002) {
                const int id = d.hdr->id;
                acceleration = id == 124 ? FX(0.17) : id == 120 ? FX(0.425) : id == 101 ? FX(1.133333) : FX(0.5667);
            }
            e.vy += acceleration;
        }
        if (e.kind == 0) {
            const int state = d.frame(e.frame).state;
            if (state == S_FALL) {
                if (e.frame < 185) {
                    if (e.vy >= FX(-8.0)) {
                        if (e.vy >= FX(1.0)) e.frame = e.vy >= FX(8.0) ? 183 : 182;
                        else e.frame = 181;
                    } else e.frame = 180;
                    if (e.fall_damage < 0) e.frame = (e.vy < FX(12.0) && counter_12 > 5) ? 182 : 181;
                } else if (e.frame < 191 && e.frame > 185) {
                    if (e.vy >= FX(-8.0)) {
                        if (e.vy >= FX(1.0)) e.frame = e.vy >= FX(8.0) ? 189 : 188;
                        else e.frame = 187;
                    } else e.frame = 186;
                }
            }
            if (d.frame(e.frame).state == S_BURN && e.frame < 205 && e.vy > FX(1.0)) e.frame = 205;
        }
    } else if (!(d.frame(e.frame).cp >= 0 && d.cp[d.frame(e.frame).cp].kind == 2)) {
        if (e.kind == 0) character_ground(e);
        else if (!ball) {
            if (e.def->hdr->id == 999 && e.def->defined(101)) {
                e.y = 0; e.vx = e.vy = 0; e.frame = 101; e.wait = 0;
            } else if ((e.kind == 2 && e.y > 0) ||
                       ((e.kind == 1 || e.kind == 4 || e.kind == 6) && e.y > 0 && e.vy > 0)) {
                e.y = 0;
                const int state = d.frame(e.frame).state;
                if (e.kind == 2) {
                    --e.drop_counter;
                    if (e.vy > FX(9.0)) { play(d.hdr->weapon_drop_sound); e.vy = FX(-5.0); e.vx /= 2; e.facing = 1 - e.facing; }
                    else { e.drop_counter -= d.hdr->weapon_drop_hurt; if (e.drop_counter < 0) e.drop_counter = 0; e.vy = 0; e.vx /= 2; e.wait = 0; e.frame = 20; }
                } else if (e.kind == 1) {
                    e.drop_counter -= d.hdr->weapon_drop_hurt;
                    if (e.vy > FX(9.9) && state == 1002) { play(d.hdr->weapon_drop_sound); e.frame = 7; e.vy = FX(-8.0); e.vx /= 2; e.facing = 1 - e.facing; }
                    else { e.frame = state == 1002 ? 70 : 60; e.vy = 0; e.vx /= 2; e.wait = 0; }
                } else {
                    e.drop_counter -= d.hdr->weapon_drop_hurt;
                    if (e.kind == 6 && e.hp < 1) e.drop_counter = -1;
                    if ((e.vy > FX(8.5) || e.vx > FX(10) || e.vx < -FX(10)) && (state == 1002 || state == 1000)) {
                        play(d.hdr->weapon_drop_sound); e.frame = 0; e.vy = -e.vy * 7 / 10; e.vx = e.vx * 7 / 10;
                        if (e.vy < FX(-10.0)) e.vy = FX(-10.0);
                    } else { e.frame = state == 1002 ? 70 : 60; e.vy = 0; e.vx = e.vx * 7 / 10; e.wait = 0; }
                }
            }
        }
    }
    if (d.frame(e.frame).state != S_FALL) e.fall_damage = 0;
}

// ---------------------------------------------------------------------------------------------------------
// Frame animation

LF2_HOT void World::advance(int index) {
    Ent& e = ents[index];
    const ObjDef& d = *e.def;
    if (e.hit_lag != 0 && e.kind != 3 && !ball_like(e)) return;
    if (e.rest > 0) --e.rest;
    if (e.holder >= 0) return;
    const Frame* f = &d.frame(e.frame);
    if (f->cp >= 0 && d.cp[f->cp].kind == 2) return;
    if (ball_like(e) && f->hit_a > 0) {
        e.hp -= f->hit_a;
        if (e.hp <= 0) { e.hp = 0; e.frame = f->hit_d; f = &d.frame(e.frame); }
    }
    if (e.blink > 0) --e.blink;
    if (e.blink < 0) ++e.blink;
    if (e.fall > 0) --e.fall;
    if (e.defend_pts > 0) --e.defend_pts;
    if (e.super_window > 0) --e.super_window;
    if (e.frame != e.prev_frame) {
        play(f->sound);
        e.wait = 0;
    }
    ++e.wait;
    if (f->state == S_STAND && to_int(e.y) < 0) { e.frame = F_JUMP + 2; f = &d.frame(e.frame); }
    if (e.kind == 2 && f->state == 2000 && to_int(e.y) == 0 && e.vx > -FX(0.1) && e.vx < FX(0.1)) {
        e.frame = 20; f = &d.frame(e.frame);
    }
    if (f->state == 2000) e.facing = e.vx > 0 ? 0 : 1;
    if (f->state == S_LYING && e.hp <= 0) {
        if ((e.follow >= 0 || (stage_mode && index > 0)) && e.blink < 1) e.blink = 30;
        e.wait = 0;
    }
    if (e.wait <= f->wait) {
        if (e.frame == F_DEFEND || e.frame == F_BROKEN + 2) e.defend_lock = 3;
        if (e.frame == 202) e.blink = 20;
        e.prev_frame = e.frame;
        return;
    }
    e.wait = 0;
    if (f->next == 0) {
        if (e.frame == F_DEFEND || e.frame == F_BROKEN + 2) e.defend_lock = 3;
        if (e.frame == 202) e.blink = 20;
        e.prev_frame = e.frame;
        return;
    }
    e.frame = f->next;
    if (e.frame < 0) { e.facing = 1 - e.facing; e.frame = -e.frame; }
    bool airborne_landing = false;
    if (e.frame == 999) {
        if (to_int(e.y) == 0 || e.kind != 0) e.frame = F_STAND;
        else { airborne_landing = true; e.frame = F_JUMP + 2; }
    }
    if (e.frame < 0 || e.frame >= 400) return;
    const bool stage_enemy = ai_stage_mode && e.team == 2;
    const bool army_troop = e.army_side >= 0;
    const bool ordinary_fighter = !stage_enemy && !army_troop;
    const bool minor_enemy = d.hdr->id / 10 == 3 && d.hdr->id != 38;
    if (d.frame(e.prev_frame).state == S_LYING && d.frame(e.frame).state != S_ICE &&
        (ordinary_fighter || difficulty != 2) &&
        (ordinary_fighter || !(ai_stage_mode || stage_mode) || !minor_enemy)) e.blink = 15;
    if (e.frame == F_JUMP + 2 && !airborne_landing) {
        e.vy = d.mv(jump_height);
        if (e.keys.right && !e.keys.left) e.vx = d.mv(jump_distance);
        else if (e.keys.left && !e.keys.right) e.vx = -d.mv(jump_distance);
        if (!e.keys.up && e.keys.down) e.vz = d.mv(jump_distancez);
        else if (e.keys.up && !e.keys.down) e.vz = -d.mv(jump_distancez);
    }
    play(d.frame(e.frame).sound);
    const Frame& cur = d.frame(e.frame);
    if (cur.mp < 0) {
        if (e.mp < cur.mp) e.frame = cur.hit_d;
        else e.mp += cur.mp;
        const Frame& c2 = d.frame(e.frame);
        if (c2.hit_d > 0) {
            if (e.keys.left && !e.keys.right && to_int(e.y) == 0 && e.facing == 0) e.frame = c2.hit_d;
            if (!e.keys.left && e.keys.right && to_int(e.y) == 0 && e.facing == 1) e.frame = c2.hit_d;
        }
    }
    if (e.frame == F_DEFEND || e.frame == F_BROKEN + 2) e.defend_lock = 3;
    if (e.frame == 202) e.blink = 20;
    e.prev_frame = e.frame;
}

void World::spawn_from_frame(int index) {
    Ent& p = ents[index];
    const ObjDef& d = *p.def;
    const Frame& f = d.frame(p.frame);
    if (f.op_kind <= 0 || f.op_oid < 0 || p.wait != 0 || (p.hit_lag != 0 && p.kind == 0)) return;
    const Group* g = p.data_group;
    if (!g) return;
    if (f.op_oid >= g->count) return;
    const ObjDef* def = &g->objs[f.op_oid];
    int total = f.op_facing;
    int variant = total, count = 1;
    if (total > 10) { variant = total % 10; count = total / 10; }
    int created[max_ents], created_count = 0;
    for (int k = 0; k < count && k < max_ents; ++k) {
        const int dir = (variant == 0) ? p.facing : (variant == 1) ? 1 - p.facing : 0;
        const int base_y = to_int(p.y) - f.cy;
        int px;
        if (p.facing == 0) px = to_int(p.x) - f.cx + f.op_x;
        else px = f.cx - f.op_x + to_int(p.x);
        fx vz = 0;
        const int st = def->frame(f.op_action).state;
        if ((st == 3000 || st == 1002 || st == 3006) && def->hdr->id != 223 && def->hdr->id != 224) {
            if (!p.keys.up) { if (p.keys.down) vz = FX(2.5); }
            else if (!p.keys.down) vz = FX(-2.5);
            if (def->hdr->id == 211) vz /= 4;
        }
        fx vx = dir == 0 ? f.op_dvx * 256 : -f.op_dvx * 256;
        if (count > 1) {
            const int spread = (k * 10 * 256) / (count - 1) - 5 * 256;
            vz += spread;
            vx += (vx > 0 && spread > 0) || (vx < 0 && spread < 0) ? -spread : spread;
        }
        const int slot = spawn(def, p.group, p.owner, f.op_action, p.team, px * 256, (base_y + f.op_y) * 256,
                               p.z + 256, vx, f.op_dvy * 256, vz, dir);
        if (slot < 0) break;
        created[created_count++] = slot;
        ents[slot].data_group = g;
        if (def->kind() == 0) {
            Ent& child = ents[slot];
            child.follow = p.follow >= 0 ? p.follow : index;
            child.blink = p.blink;
            child.ai.enabled = true;
            child.ai.aggression = p.ai.aggression;
        }
        if (def->hdr->id == 5 || def->hdr->id == 52) {
            Ent& child = ents[slot];
            child.hp = child.dark_hp = child.stage_max_hp = 10; child.mp = 5;
        }
        if (p.kind == 3 && f.state == 3003 && p.walk_owner >= 0 && p.walk_owner < max_ents) {
            ents[slot].pair_rest[p.walk_owner] = 10;
            if (ents[p.walk_owner].live) ents[p.walk_owner].pair_rest[slot] = 10;
        }
        if (f.op_kind == 2) {
            if (p.held_item >= 0 && p.held_item < max_ents && ents[p.held_item].holder == index)
                ents[p.held_item].holder = -1;
            p.held_item = slot;
            ents[slot].holder = index;
        }
    }
    if (created_count > 1) {
        const int half = created_count / 2;
        for (int k = 0; k < created_count; ++k) {
            Ent& child = ents[created[k]];
            if (created_count % 2 == 0) {
                if (k < half - 1) child.rest = (half - k) * 2 - 2;
                else if (k > half) child.rest = (k - half) * 2;
            } else {
                if (k < half) child.rest = (half - k) * 2;
                else if (k > half) child.rest = (k - half) * 2;
            }
            for (int j = 0; j < k; ++j) {
                child.pair_rest[created[j]] = 40;
                ents[created[j]].pair_rest[created[k]] = 40;
            }
        }
    }
}

void World::after_advance(int index) {
    Ent& e = ents[index];
    if (e.frame >= 1100 && e.frame < 1300) {
        const int blink = 1100 - e.frame;
        for (Ent& follower : ents)
            if (follower.live && follower.follow == index) follower.blink = blink;
        e.blink = blink;
        e.frame = 0;
        return;
    }
    if (e.frame < 0 || e.frame >= 400) {
        e.frame = 0;
        e.live = false;
        return;
    }
    const Frame& initial_frame = e.def->frame(e.frame);
    const bool frame_spawns = e.frame >= 0 && e.frame < 400 && e.def->defined(e.frame) &&
        initial_frame.op_kind > 0 && initial_frame.op_oid >= 0 && e.wait == 0 && !(e.hit_lag != 0 && e.kind == 0);
    if (!frame_spawns && (e.kind == 1 || e.kind == 2 || e.kind == 4 || e.kind == 6) && e.drop_counter < 0) {
        if (e.holder >= 0 && e.holder < max_ents && ents[e.holder].held_item == index) ents[e.holder].held_item = -1;
        play(e.def->hdr->weapon_broken_sound);
        const Group* debris_group = groups[max_fighters];
        const ObjDef* debris = find_def(debris_group, 999);
        if (debris) {
            const int id = e.def->hdr->id;
            const int count = id == 150 ? 13 : id == 151 ? 15 : id == 101 || id == 218 ? 7 :
                              id == 122 || id == 123 ? 9 : id == 121 ? 4 :
                              id == 120 || id == 124 || id == 201 ? 3 : id == 100 || id == 213 || id == 217 ? 5 : 0;
            for (int piece = 0; piece < count; ++piece) {
                if (!find_free()) break;
                const int x = to_int(e.x) + rnd(7) - 3;
                const int y = to_int(e.y) + rnd(7) - 3;
                const bool heavy = id == 150 || id == 151 || id == 213;
                int vy = heavy ? -8 - rnd(20) / 2 : -6 - rnd(8) / 2;
                const int vx = rnd(11) - 5;
                int base = id == 100 ? (piece < 2 ? 10 : 14) : id == 101 ? (piece < 5 ? 20 + rnd(2) * 4 : 30) :
                           id == 150 ? (piece < 5 ? 0 : 4) : id == 151 ? (piece < 2 ? 40 : piece < 5 ? 44 : piece < 8 ? 50 : 54) :
                           id == 120 ? (piece < 2 ? 54 : 30) : id == 124 ? 170 : id == 121 ? 60 :
                           id == 122 ? (piece < 1 ? 70 : piece < 3 ? 80 : 74) :
                           id == 123 ? (piece < 1 ? 160 : piece < 3 ? 164 : 74) :
                           id == 213 ? (piece < 2 ? 150 : 154) : 174;
                const int frame = id == 201 ? 0 : base + rnd(4);
                if ((id == 122 || id == 123) && piece >= 3) vy = -4 - rnd(18) / 2;
                const int slot = spawn(debris, max_fighters, 99, frame, 0,
                                       x * 256, y * 256, to_int(e.z) * 256,
                                       vx * 256, vy * 256, FX(0.1), 0);
                if (slot < 0) break;
            }
        }
        e.live = false;
        return;
    }
    const ObjDef& d = *e.def;
    if (e.frame < 0 || e.frame >= 400 || !d.defined(e.frame)) {
        if (e.kind != 0 || index >= max_fighters) { e.live = false; return; }   // an effect or a clone ends
        e.frame = F_STAND;
    }
    if (e.kind == 0) {
        auto knock_down = [&]() { e.frame = F_FALLING + 6; e.vy = FX(-3.0); e.iy = FX(-3.0); e.y = FX(-1.0); };
        if (e.hp <= 0 && (e.frame < 12 || e.frame == F_DEFEND || e.frame == F_DEFEND + 1)) knock_down();
        if (to_int(e.y) == 0 && e.y == 0 && e.vy == 0 && e.iy == 0 &&
            ((e.frame >= 180 && e.frame <= 189 && e.frame != 184) || (e.frame >= 212 && e.frame <= 214))) knock_down();
    }
    const Frame& frame = e.def->frame(e.frame);
    const bool spawning = frame.op_kind > 0 && frame.op_oid >= 0 && e.wait == 0 && !(e.hit_lag != 0 && e.kind == 0);
    spawn_from_frame(index);
    if (!spawning) team_orders(index);
    status_effects(index);
    e.state_frame = e.frame;
}

void World::regenerate(Ent& e) {
    if (e.kind != 0) return;
    if (e.hp > 0 && e.hp < e.dark_hp && counter_12 == 0) ++e.hp;
    if (e.fall_damage < 0 && counter_12 == 0) {
        const int loss = e.armor > 0 ? 900 / e.armor : 9;
        e.hp -= loss;
        e.dark_hp -= loss / 3;
        if (e.hp < 0) e.hp = 0;
        if (e.dark_hp < 0) e.dark_hp = 0;
    }
    if ((e.follow < 0 || e.mp < 150) && e.mp < 500 && counter_3 == 0 && e.blink > -1) {
        int hp = e.hp < 500 ? e.hp : 500;
        if (e.def->hdr->id == 51 || e.def->hdr->id == 52) hp /= 2;
        e.mp += (500 - hp) / 100 + 1;
    }
}

void World::heal(Ent& e) {
    if (e.regen_timer >= 1000 && e.regen_timer < 2000 && e.hp > 0) {
        --e.regen_timer;
        if (e.regen_timer % 8 == 0) {
            if (e.hp < e.dark_hp) { e.hp += 8; if (e.hp > e.dark_hp) e.hp = e.dark_hp; }
            else e.regen_timer = 0;
        }
        if (e.regen_timer % 1000 == 0) e.regen_timer = 0;
    }
    if (e.heal_timer > 0 && e.hp > 0) {
        --e.heal_timer;
        if (e.heal_timer % 8 == 0 && e.hp < e.dark_hp) { e.hp += 8; if (e.hp > e.dark_hp) { e.hp = e.dark_hp; e.heal_timer = 0; } }
    }
    if (e.def->frame(e.frame).state == 1700) e.regen_timer = 1100;
}

// ---------------------------------------------------------------------------------------------------------
// Camera and bounds

void World::clamp_world() {
    for (int i = 0; i < max_ents; ++i) {
        Ent& e = ents[i];
        if (!e.live) continue;
        if (e.kind == 0) {
            if (e.z < stage.z1 * 256) e.z = stage.z1 * 256;
            if (e.z > stage.z2 * 256) e.z = stage.z2 * 256;
            const bool spawned = i >= max_fighters || (stage_mode && i > 0);
            const int margin = spawned ? 100 : (stage_mode && e.team == 2 ? 300 : 0);
            if (e.x < -margin * 256) e.x = -margin * 256;
            if (e.x > (stage.width + (spawned ? 100 : 0)) * 256) e.x = (stage.width + (spawned ? 100 : 0)) * 256;
            if (stage_bound > 0 && e.team != 2 && e.blink == 0 && e.x > stage_bound * 256) e.x = stage_bound * 256;
        } else {
            if (e.z < (stage.z1 - 1) * 256) e.z = (stage.z1 - 1) * 256;
            if (e.z > (stage.z2 + 1) * 256) e.z = (stage.z2 + 1) * 256;
            if (stage_mode && e.def->hdr->id == 300) continue;
            if (ball_like(e)) { if (e.x < FX(-300) || e.x > (stage.width + 300) * 256) e.live = false; }
            else if ((e.def->hdr->id == 122 || e.def->hdr->id == 123) && (e.army_side >= 0 || clamp_edge_drinks)) {
                if (e.x < FX(10)) e.x = FX(10);
                if (e.x > (stage.width - 10) * 256) e.x = (stage.width - 10) * 256;
            }
            else if ((e.x < 0 || e.x > stage.width * 256) && to_int(e.y) == 0) e.live = false;
        }
    }
}

void World::update_camera() {
    clamp_world();
    int sum = 0, count = 0;
    for (int i = 0; i < max_fighters; ++i) {
        const Ent& e = ents[i];
        if (e.live && e.human && e.hp > 0) {
            if (e.def->frame(e.frame).state == S_LYING) sum += to_int(e.x);
            else sum += 130 + to_int(e.x) - e.facing * 260;
            ++count;
        }
    }
    if (count == 0) {
        for (int i = 0; i < max_ents; ++i) {
            const Ent& e = ents[i];
            if (e.live && e.kind == 0 && e.hp > 0) { sum += to_int(e.x); ++count; }
        }
        if (count == 0) { sum = 800; count = 1; }
    }
    int target = sum / count - 397;
    if (target < 0) target = 0;
    if (target > stage.width - 794) target = stage.width - 794;
    if (camera_limit != 0 && target > camera_limit) target = camera_limit;
    const int step = ((target - camera) / 14 + camera_step * 6) / 7;
    camera_step = step;
    if (camera_step == 0) { if (camera < target) camera_step = 1; else if (target < camera) camera_step = -1; }
    camera += camera_step;
    if (camera < 0) camera = 0;
    if (camera > stage.width - 794) camera = stage.width - 794;
}

void World::round_end() {
    if (stage_mode) return;
    int alive[max_fighters + 1] = {};
    int teams = 0, last = -1;
    for (int i = 0; i < max_ents; ++i) {
        const Ent& e = ents[i];
        if (e.live && e.kind == 0 && e.hp > 0 && e.team > 0 && e.team <= max_fighters) alive[e.team]++;
    }
    for (int t = 1; t <= max_fighters; ++t) if (alive[t] > 0) { ++teams; last = t; }
    if (teams < 2 && !finished) {
        winner = teams == 1 ? last : -1;
        if (round_counter < (auto_finish_round ? 350 : 144)) ++round_counter;
    } else if (teams >= 2) winner = -1;
    if (auto_finish_round && round_counter >= 350) finished = true;
    if (!auto_finish_round && round_counter >= 144) {
        for (int i = 0; i < max_fighters; ++i)
            if (ents[i].live && ents[i].human && (ents[i].keys.attack || ents[i].keys.jump)) finished = true;
    }
}

void World::tick() {
    if (finished) return;
    ++frame_number_;
    counter_12 = (counter_12 + 1) % 12;
    counter_3 = (counter_3 + 1) % 3;
    shake = shake > 0 ? shake - 1 : 0;
    teleport_phase = 1 - teleport_phase;
    sound_count = 0;
    round_end();
    if (finished) return;
    in_tick = true;
    rebuild_scan();
    for (int i = 0; i < max_ents; ++i) {
        Ent& e = ents[i];
        if (!e.live) continue;
        if (e.kind == 0) {
            if (e.ai.enabled) { e.prev = e.keys; ai_control(i); }
        } else ball_update(i);
    }
    for (int i = 0; i < max_ents; ++i) {
        if (!ents[i].live) continue;
        control(i);
        frame_rules(i);
    }
    for (int i = 0; i < max_ents; ++i) if (ents[i].live) {
        physics(i);
        // State 9998 marks an object that is finished (a landed arrow); it is deleted right after movement.
        if (ents[i].def->frame(ents[i].frame).state == 9998) {
            Ent& gone = ents[i];
            if (gone.holder >= 0 && gone.holder < max_ents && ents[gone.holder].held_item == i) ents[gone.holder].held_item = -1;
            gone.live = false;
            continue;
        }
        if (ents[i].live) after_physics(i);
    }
    carry_weapons();
    detect();
    update_fusion();
    for (int i = 0; i < max_ents; ++i) if (ents[i].live && ents[i].kind == 0) apply(i);
    spawn_weapons();
    for (int i = 0; i < max_ents; ++i) if (ents[i].live && ents[i].kind > 0) apply(i);
    grab_holds();
    grab_place();
    check_holders();
    carry_weapons();
    update_camera();
    apply_impulses();
    for (int i = 0; i < max_ents; ++i) {
        Ent& e = ents[i];
        if (!e.live) continue;
        const int state = e.def->frame(e.frame).state;
        if (e.kind == 0 && state == 9996 && e.wait == 1) {
            for (int piece = 0; piece < 5; ++piece) {
                const ObjDef* armor = find_def(groups[max_fighters], piece == 4 ? 218 : 217);
                if (!armor) break;
                const int x = to_int(e.x) + rnd(7) - 3;
                const int y = to_int(e.y) + rnd(7) - 9;
                const int vy = -5 - rnd(15) / 2;
                const int vz = piece == 4 ? 1 : (piece % 2 == 0 ? 3 + rnd(2) : -3 - rnd(2));
                const int vx = piece < 2 ? -10 - rnd(3) : piece < 4 ? 10 + rnd(3) : rnd(7) - 3;
                const int frame = rnd(4), facing = rnd(2);
                const int slot = spawn(armor, max_fighters, -1, frame, 0, x * 256, y * 256,
                                       (to_int(e.z) + 1) * 256, vx * 256, vy * 256, vz * 256, facing);
                if (slot < 0) break;
                ents[slot].rest = 6;
            }
        }
        if (e.kind == 0 && state == 9995) {
            const Group* target = character_group(50);
            if (target) change_character(e, target, 0, false); else e.frame = 0;
        } else if (state >= 8000 && state < 9000) {
            const Group* target = character_group(state - 8000);
            if (target) change_character(e, target, 0, false); else e.frame = 0;
            e.picture_offset = 140;
        }
        regenerate(e);
        advance(i);
        if (e.live) after_advance(i);
    }
    for (auto& e : ents) if (e.live) heal(e);
    for (auto& e : ents) { e.list_n = 0; e.nearest = 1000; e.nearest_catcher = 1000; e.hit_this_frame = false; }
    for (auto& s : sparks) if (s.live &&
        (s.owner < 0 || s.owner >= max_ents || !ents[s.owner].live || --s.timer <= 0)) s.live = false;
    in_tick = false;
    scan_valid = false;
}

}  // namespace lf2

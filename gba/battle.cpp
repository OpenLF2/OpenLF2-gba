// SPDX-License-Identifier: MIT
// Copyright (c) 2026 OpenLF2 contributors

#include "battle.hpp"
#include "assets.h"
#include "hw.hpp"
#include "sound.hpp"
#include "video.hpp"
#include "../game/stage_run.hpp"
#include "../game/army.hpp"
#include "stage_rules.hpp"
#include <new>

using namespace lf2;

#ifdef LF2_TIMING
int battle_debug_value = 0, battle_debug_value2 = 0;
#endif

namespace {
// The simulation state is large and is only touched through its own methods: it lives in EWRAM.
__attribute__((section(".ewram"), aligned(4))) unsigned char world_storage[sizeof(World)];
World& world = *reinterpret_cast<World*>(world_storage);

BattleSetup current;
int roster_of[max_fighters];
constexpr int hud_slots = 8;                                    // fighters with a head icon in the status panel
const Group* shown_group[hud_slots];
// The sprite hardware has 16 palettes: 0-7 and 10 hold fighters, 8 the weapons. Fighters that share a
// picture set (and, in team battles, a team colour) share a palette, whatever slot they occupy.
constexpr int weapon_bank = 8;
constexpr int palette_banks[] = {0, 1, 2, 3, 4, 5, 6, 7, 10};
constexpr int palette_bank_count = 9;
const Group* bank_group[palette_bank_count];
int bank_variant[palette_bank_count];
bool bank_used[palette_bank_count];
int total_fighters = 0;
int ticks = 0;
bool paused = false;
int frame_flip = 0;
int camera_world = 0;
int shake_x = 0, shake_y = 0;

constexpr int scale_num = 1229, scale_shift = 12;               // 0.3
inline int scaled(int v) { return (v * scale_num) >> scale_shift; }
constexpr int upload_budget = 12 * 1024;                          // bytes of picture data per frame

int team_variant(const Ent& e) { return current.teams ? (e.team == 1 ? 1 : 2) : 0; }

// The palette bank showing `group` in `variant`; loads it into a bank not used by this frame's sprites.
int bank_for(const Group* group, int variant) {
    if (!group) return palette_banks[0];
    int free_bank = -1;
    for (int i = 0; i < palette_bank_count; ++i) {
        if (bank_group[i] == group && bank_variant[i] == variant) { bank_used[i] = true; return palette_banks[i]; }
        if (free_bank < 0 && !bank_used[i]) free_bank = i;
    }
    if (free_bank < 0) return palette_banks[0];     // more picture sets than palettes: colours are wrong, not missing
    std::uint16_t palette[16];
    for (int c = 0; c < 16; ++c) palette[c] = group->palette[c];
    if (variant) palette[15] = variant == 1 ? 0x7E08 : 0x211F;
    video::obj_palette(palette_banks[free_bank], palette);
    bank_group[free_bank] = group; bank_variant[free_bank] = variant; bank_used[free_bank] = true;
    return palette_banks[free_bank];
}

int order[max_ents];
int spark_slots[max_sparks];
int spark_frames[max_sparks];
StageRun stage_run;
static_assert(max_ents <= 128, "StageRun stores entity indices in signed bytes");
bool stage_reserved[max_fighters] = {};
ArmyRun army_run;
int stage_level = 0;
bool stage_waiting = false;
bool stage_group_clear = false;
int stage_clear_timer = 0;
int stage_clear_delay = 0;
int stage_intro = 0;
int stage_phase_go = 0;
bool stage_background_pending = false;
int stage_restart_level = -1;
bool stage_wait_release = false;
int stage_shutter = 0;
bool stage_shutter_group = false;
int survival_waves = 0;
int stage_heroes[10] = {1, 2, 4, 5, 6, 11, 9, 10, 8, 7};
int stage_hero_index = 10;
constexpr int stage_backgrounds[] = {0, 2, 3, 7, 6, 4};

bool stage_allies_at_exit() {
    // Stage recruits share fighter slots on GBA, but only the original player
    // slots must reach the exit in OpenLF2.
    const Ent& player = world.ents[0];
    return !player.live || player.hp <= 0 || to_int(player.x) >= world.stage_exit_x;
}

void stage_next_level() {
    const int previous_group = stage_level / 10;
    stage_level = stage_level % 10 == 4 ? stage_level + 6 : stage_level + 1;
    if (stage_level / 10 != previous_group) {
        current.stage = stage_backgrounds[stage_level / 10];
        world.stage.width = video::stage_width(current.stage);
        video::stage_z(current.stage, world.stage.z1, world.stage.z2);
        stage_background_pending = true;
    }
    world.stage_transition(current.difficulty);
    stage_run.start(STAGE_SCRIPTS[stage_level], STAGE_PHASES, STAGE_ENTRIES, 1);
    stage_waiting = false;
    stage_group_clear = false;
    stage_clear_timer = 0;
    stage_clear_delay = 0;
    stage_intro = 70;
    stage_phase_go = 0;
}

int stage_spawn(void*, const StageEntry& entry) {
    int id = entry.id;
    int health = entry.hp;
    if (!current.army_mode && id != 300) {
        if (current.difficulty == -1) health = health * 3 / 2;
        else if (current.difficulty == 2) health = health * 3 / 4;
    }
    if (id == 1000 || id == 3000 || id == 3001) {
        bool available = false;
        for (int n = 1; n < max_fighters; ++n) if (!world.ents[n].live && (current.army_mode || !stage_reserved[n])) { available = true; break; }
        if (!available) return -1;
    }
    if (id == 1000) {
        if (stage_hero_index >= 10) {
            for (int i = 0; i < 50; ++i) {
                int a = world.rnd(10), b = world.rnd(10), temp = stage_heroes[a];
                stage_heroes[a] = stage_heroes[b]; stage_heroes[b] = temp;
            }
            stage_hero_index = 0;
        }
        id = stage_heroes[stage_hero_index++];
    } else if (id == 3000 || id == 3001) {
        if (id == 3001 && world.rnd(7) == 0) { id = 32; health *= 4; }
        else id = 30 + world.rnd(2);
    }
    if (id >= 100 && id < 200) {
        const Group& items = weapon_group();
        for (int n = 0; n < items.count; ++n) if (items.objs[n].hdr->id == id) {
            if (!world.find_free()) return -1;
            const int z = world.stage.z1 + world.rnd(world.stage.z2 - world.stage.z1);
            const int x = entry.x == -1000 ? (world.rnd(2) == 0 ? world.stage_bound + 150 + world.rnd(300) : -150 - world.rnd(300)) : entry.x + world.rnd(300);
            const int slot = world.spawn(&items.objs[n], max_fighters, -1, 0, 0, x * 256, FX(-300), z * 256, 0, 0, 0, 0);
            if (slot < 0) return -1;
            Ent& item = world.ents[slot];
            item.owner = slot;
            item.hp = item.dark_hp = item.stage_max_hp = health;
            if (id == 122) item.hp = 200;
            return slot;
        }
        return -2;
    }
    int roster = -1;
    for (int n = 0; n < ROSTER_COUNT; ++n) if (ROSTER_ID[n] == id) { roster = n; break; }
    const Group* selected = roster >= 0 ? &roster_group(roster) : nullptr;
    const ObjDef* definition = selected ? &selected->objs[0] : nullptr;
    if (id == 300) {
        selected = &weapon_group();
        for (int n = 0; n < selected->count; ++n) if (selected->objs[n].hdr->id == 300) { definition = &selected->objs[n]; break; }
    }
    if (!definition) return -2;
    int slot = -1;
    for (int n = 1; n < max_fighters; ++n) if (!world.ents[n].live && (current.army_mode || !stage_reserved[n])) { slot = n; break; }
    if (slot < 0) return -1;
    Ent& e = world.ents[slot];
    if (e.sprite_slot >= 0) video::free_tiles(e.sprite_slot, 0);
    for (auto& spark : world.sparks) if (spark.live && spark.owner == slot) spark.live = false;
    e = Ent();
    world.groups[slot] = selected;
    e.live = true; e.def = definition; e.data_group = selected;
    e.group = slot; e.kind = definition->kind(); e.team = 2; e.owner = slot;
    e.ai.enabled = true; e.ai.aggression = 55;
    e.hp = e.dark_hp = health; e.mp = 500;
    e.stage_max_hp = health; e.stage_lives = entry.reserve;
    e.stage_join_hp = entry.join; e.stage_join_lives = entry.join_reserve;
    const int spawn_bound = current.army_mode ? world.stage.width : world.stage_bound;
    const int z = world.stage.z1 + world.rnd(world.stage.z2 - world.stage.z1);
    const int x = current.army_mode ? entry.x : entry.x == -1000 ? (world.rnd(2) == 0 ? spawn_bound + 150 + world.rnd(300) : -150 - world.rnd(300)) : entry.x + world.rnd(300);
    e.x = x * 256; e.y = entry.y * 256;
    e.z = z * 256;
    e.facing = x > spawn_bound - 794 ? 1 : 0;
    e.frame = e.def->defined(entry.act) ? entry.act : 0;
    e.blink = 20;
    for (auto& other : world.ents) other.pair_rest[slot] = 0;
    return slot;
}

bool stage_alive(void*, int slot) {
    return slot >= 0 && slot < max_ents && world.ents[slot].live;
}

bool stage_blocks(void*, int slot) {
    const Ent& e = world.ents[slot];
    return e.live && e.kind == 0 && e.team == 2;
}

bool stage_enemies_left(void*) {
    for (const Ent& e : world.ents)
        if (e.live && e.kind == 0 && e.team == 2) return true;
    return false;
}

void stage_phase_started(void*, const StagePhase& phase) {
    if (world.stage_bound > 0 && world.stage_bound != phase.bound && stage_intro == 0 && stage_phase_go == 0)
        stage_phase_go = 68;
    if (current.survival) {
        ++survival_waves;
        Ent& e = world.ents[0];
        if (e.live && e.kind == 0 && e.team == 1 && e.hp <= 0) {
            if (e.dark_hp < 5) e.dark_hp = 5;
            e.hp = 5; e.mp = 500; e.x = FX(100);
        }
    }
    world.stage_bound = phase.bound;
    world.camera_limit = phase.bound - 794;
    if (phase.music > 0) music_play(phase.music);
}

void stage_clear_reservations(void*) { for (bool& reserved : stage_reserved) reserved = false; }
void stage_reserve(void*, int slot) { if (slot >= 0 && slot < max_fighters) stage_reserved[slot] = true; }

const StageHooks stage_hooks{nullptr, stage_spawn, stage_alive, stage_phase_started, stage_blocks, stage_enemies_left, stage_clear_reservations, stage_reserve};

int army_census(void*, int side, int kind) {
    return world.army_census(side, army_ids[kind]);
}

int army_living(void*, int side) {
    int count = 0;
    for (const Ent& e : world.ents) if (e.live && e.kind == 0 && e.hp > 0 && e.team == side + 1) ++count;
    return count;
}

bool army_spawn(void*, int side, int kind, int id, int hp) {
    if (kind >= 9) {
        const Group& items = weapon_group();
        for (int i = 0; i < items.count; ++i) if (items.objs[i].hdr->id == id) {
            const int x = side == 0 ? 50 : world.stage.width - 50;
            const int z = world.stage.z1 + world.rnd(world.stage.z2 - world.stage.z1);
            const int slot = world.spawn(&items.objs[i], max_fighters, -1, 0, 0, x * 256, FX(-300), z * 256, 0, 0, 0, 0);
            if (slot < 0) return false;
            Ent& e = world.ents[slot]; e.army_side = side; e.army_kind = kind; e.owner = slot;
            e.hp = e.dark_hp = e.stage_max_hp = hp;
            return true;
        }
        return false;
    }
    // Each army gets half of the fighter slots (slot 0 is the player).
    int side_count = 0;
    for (int i = 1; i < max_fighters; ++i) if (world.ents[i].live && world.ents[i].team == side + 1) ++side_count;
    if (side_count >= (max_fighters - 2) / 2) return false;
    StageEntry entry{}; entry.id = id; entry.hp = hp; entry.act = 212; entry.y = -300;
    entry.x = side == 0 ? -100 : world.stage.width + 100;
    const int slot = stage_spawn(nullptr, entry);
    if (slot < 0) return false;
    Ent& e = world.ents[slot];
    e.team = side + 1; e.army_side = side; e.army_kind = kind;
    e.picture_offset = side == 0 ? (id == 37 ? 114 : 140) : 0;
    e.armor = side == 0 ? current.army_defense : current.enemy_defense;
    e.x = side == 0 ? FX(-100) : (world.stage.width + 100) * 256;
    e.facing = side == 0 ? 0 : 1;
    return true;
}

const ArmyHooks army_hooks{nullptr, army_census, army_living, army_spawn};
}  // namespace

void battle_init() {
    new (world_storage) World();
}

void battle_start(const BattleSetup& setup) {
    current = setup;
    stage_background_pending = false;
    stage_restart_level = -1;
    stage_wait_release = false;
    stage_shutter = 0;
    stage_shutter_group = false;
    if (current.stage_mode) current.stage = stage_backgrounds[current.survival ? 5 : current.stage_start_level / 10];
    video::stage_load(current.stage, !setup.title);
    Stage st;
    st.width = video::stage_width(current.stage);
    video::stage_z(current.stage, st.z1, st.z2);
    world.init(st, setup.seed);
    world.background_drinks = !setup.stage_mode && !setup.army_mode && setup.lineup_count == 0;
    world.clamp_edge_drinks = setup.survival;
    world.auto_finish_round = setup.player < 0;
    world.difficulty = setup.difficulty;
    world.ai_difficulty = setup.difficulty > 0 ? setup.difficulty : 0;
    world.ai_stage_mode = setup.stage_mode;
    world.ai_random_game = setup.player < 0;
    video::reset_sprites();
    for (int i = 0; i < max_sparks; ++i) { spark_slots[i] = -1; spark_frames[i] = -1; }
    total_fighters = 0;
    unsigned r = setup.seed * 2654435761u + 12345u;
    auto next = [&]() { r = r * 1664525u + 1013904223u; return int((r >> 12) & 0xFFFF); };
    int used[ROSTER_COUNT] = {};
    int count = setup.stage_mode || setup.army_mode ? 1 : setup.opponents + (setup.player >= 0 ? 1 : 0);
    if (count > max_fighters) count = max_fighters;
    if (setup.lineup_count > 0) {
        count = setup.lineup_count < 4 ? setup.lineup_count : 4;
        for (int i = 0; i < count; ++i) {
            roster_of[total_fighters] = setup.lineup_roster[i];
            world.add_fighter(setup.lineup_roster[i], setup.lineup_team[i], i == 0 && setup.player >= 0, 60);
            Ent& fighter = world.ents[total_fighters];
            fighter.hp = fighter.dark_hp = fighter.stage_max_hp = setup.lineup_hp[i];
            for (int j = 0; j < ROSTER_COUNT; ++j)
                if (ROSTER_ID[j] == setup.lineup_transform[i]) fighter.transform_target = &roster_group(j);
            ++total_fighters;
        }
    } else if (setup.player >= 0) {
        roster_of[total_fighters] = setup.player;
        used[setup.player] = 1;
        world.add_fighter(setup.player, setup.teams ? 1 : total_fighters + 1, true, 0);
        ++total_fighters;
    }
    while (total_fighters < count) {
        int pick = next() % ROSTER_COUNT;
        for (int tries = 0; tries < ROSTER_COUNT && used[pick]; ++tries) pick = (pick + 1) % ROSTER_COUNT;
        used[pick] = 1;
        roster_of[total_fighters] = pick;
        world.add_fighter(pick, setup.teams ? (total_fighters % 2) + 1 : total_fighters + 1, false, 40 + next() % 50);
        ++total_fighters;
    }
    for (int i = 0; i < total_fighters; ++i)
        world.ents[i].mp = setup.stage_mode || setup.lineup_count > 0 ? 500 : 200;
    world.place_fighters();
    for (int i = 0; i < hud_slots; ++i) shown_group[i] = nullptr;
    if (setup.stage_mode) {
        world.stage_mode = true;
        world.fighters = max_fighters;
        world.ents[0].team = 1;
        world.ents[0].x = (50 + world.rnd(30)) * 256;
        world.camera = 0;
        stage_level = setup.survival ? 50 : setup.stage_start_level;
        survival_waves = -1;
        stage_hero_index = 10;
        stage_waiting = false;
        stage_group_clear = false;
        stage_clear_timer = 0;
        stage_clear_delay = 0;
        stage_intro = 70;
        stage_phase_go = 0;
        stage_background_pending = false;
        stage_run.start(STAGE_SCRIPTS[stage_level], STAGE_PHASES, STAGE_ENTRIES, 1);
    }
    if (setup.army_mode) {
        world.stage_mode = true; // troop slots use the same defeat and removal lifecycle as stage enemies
        world.fighters = max_fighters;
        world.ents[0].team = 1;
        world.ents[0].x = FX(100);
        world.ents[0].armor = setup.army_defense;
        army_run = ArmyRun{};
        if (setup.army_custom) {
            army_run.configure(0, setup.army_first[0], setup.army_reserve[0]);
            army_run.configure(1, setup.army_first[1], setup.army_reserve[1]);
        } else {
            army_run.preset(0, setup.army_strategy, setup.army_size);
            army_run.preset(1, setup.enemy_strategy, setup.enemy_army_size);
        }
    }
    for (int i = 0; i < total_fighters && i < hud_slots; ++i) {
        const Group& g = roster_group(roster_of[i]);
        video::upload_raw(video::tile_head + i * 4, g.head, 128);
        shown_group[i] = &g;
    }
    for (int i = 0; i < palette_bank_count; ++i) { bank_group[i] = nullptr; bank_variant[i] = 0; bank_used[i] = false; }
    video::obj_palette(weapon_bank, weapon_group().palette);
    video::obj_ui_palette();
    ticks = 0;
    paused = false;
    frame_flip = 0;
    camera_world = world.camera;
    shake_x = shake_y = 0;
    video::stage_scroll(world.camera, current.title ? 10 : 0);
}

void battle_set_paused(bool p) { paused = p; }

void battle_tick(unsigned keys) {
    if (paused || world.finished) return;
    ++ticks;
    Keys k;
    k.left = keys & Key::left;
    k.right = keys & Key::right;
    k.up = keys & Key::up;
    k.down = keys & Key::down;
    k.attack = keys & Key::a;
    k.jump = keys & Key::b;
    k.defend = keys & (Key::l | Key::r);
    if (stage_wait_release) {
        if (!k.attack && !k.jump) stage_wait_release = false;
        k.attack = k.jump = false;
    }
    if (current.player >= 0) world.set_keys(0, k);
    if (current.stage_mode && !world.finished) {
        if (stage_intro > 0) --stage_intro;
        if (stage_phase_go > 0) {
            const int go = stage_phase_go + 101;
            if ((go / 10) % 2 == 1 && go % 10 == 0) sound_play(SOUND_MENU_OK);
            --stage_phase_go;
        }
        world.stage_update_reserves();
        if (!world.stage_allies_alive()) {
            world.winner = 2;
            if (world.round_counter < 144) ++world.round_counter;
            if (world.round_counter == 80) sound_play(SOUND_MENU_END);
        }
        if (world.round_counter >= 144 && !stage_group_clear && (k.attack || k.jump)) world.finished = true;
        if (stage_shutter > 0) {
            ++stage_shutter;
            if (stage_shutter == 11) {
                if (stage_shutter_group) {
                    if (stage_level == 44) { world.winner = 1; world.finished = true; stage_shutter = 0; }
                    else stage_restart_level = stage_level + 6;
                } else stage_next_level();
            }
            if (stage_shutter >= 21) stage_shutter = 0;
        } else if (!world.finished && !stage_waiting) {
            // Only player slots count here; recruited stage enemies occupy spawn slots
            // in OpenLF2 even though this port places them among the fighter slots.
            const int id = world.ents[0].def->hdr->id;
            int strength = id == 51 ? 2 : id == 52 ? 3 : 1;
            if (current.difficulty == -1) strength = strength * 3 / 2 + 1;
            stage_run.set_players(strength);
            stage_run.tick(stage_hooks);
            if (stage_phase_go > 0 && stage_run.next_bound_same()) stage_phase_go = 0;
            if (stage_run.finished()) {
                world.stage_exit_x = world.stage_bound;
                stage_waiting = true;
                stage_group_clear = !current.survival && stage_level % 10 == 4;
                stage_clear_timer = 0;
                stage_clear_delay = 68;
            }
        } else if (!world.finished && stage_waiting && (stage_intro > 0 || stage_phase_go > 0)) {
            // Earlier cues complete before the final clear cue begins.
        } else if (!world.finished && stage_waiting && stage_clear_delay > 0) {
            world.stage_bound = 0;
            world.stage_exit = !stage_group_clear;
            const int go = stage_clear_delay + 101;
            if ((go / 10) % 2 == 1 && go % 10 == 0) sound_play(SOUND_MENU_OK);
            --stage_clear_delay;
            if (stage_clear_delay == 0 && stage_group_clear) sound_play(SOUND_MENU_PASS);
        } else if (!world.finished && stage_waiting && stage_group_clear) {
            if (stage_clear_timer < 144) ++stage_clear_timer;
            if (stage_clear_timer >= 144 && (k.attack || k.jump)) {
                stage_shutter = 1;
                stage_shutter_group = true;
            }
        } else if (!world.finished && stage_waiting && stage_allies_at_exit()) {
            stage_shutter = 1;
            stage_shutter_group = false;
        }
    }
    if (current.army_mode && !world.finished) {
        army_run.tick(army_hooks);
        if (army_run.over) {
            const int winner = ArmyRun::result(army_living(nullptr, 0), army_living(nullptr, 1));
            if (winner >= 0) {
                world.winner = winner;
                if (world.round_counter < 144) ++world.round_counter;
                if (world.round_counter == 80) sound_play(SOUND_MENU_END);
            } else world.winner = -1;
        } else world.winner = -1;
        if (world.round_counter >= 144 && (k.attack || k.jump)) world.finished = true;
    }
    const int previous_round_counter = world.round_counter;
    world.tick();
    if (!current.stage_mode && !current.army_mode && previous_round_counter < 80 && world.round_counter == 80)
        sound_play(SOUND_MENU_END);
    if (world.finished) world.finish_round();
    for (int i = 0; i < world.sound_count; ++i) sound_play(world.sounds[i]);
    world.sound_count = 0;
}

bool battle_over() { return world.finished; }

int battle_winner_roster() {
    for (int i = 0; i < total_fighters; ++i)
        if (world.ents[i].live && world.ents[i].team == world.winner && world.ents[i].hp > 0) return roster_of[i];
    for (int i = 0; i < total_fighters; ++i)
        if (world.ents[i].team == world.winner) return roster_of[i];
    return -1;
}

bool battle_player_won() { return current.player >= 0 && world.winner == world.ents[0].team; }
int battle_winner_team() { return world.winner; }
int battle_stage_checkpoint() { return stage_level / 10 * 10; }
int battle_stage_background(int level) {
    int group = level / 10;
    if (group < 0) group = 0;
    if (group > 5) group = 5;
    return stage_backgrounds[group];
}
bool battle_player_alive() { return current.player >= 0 && world.ents[0].hp > 0; }
int battle_fighter_dark_hp(int slot) { return slot >= 0 && slot < total_fighters ? world.ents[slot].dark_hp : 0; }
bool battle_fighter_alive(int slot) { return slot >= 0 && slot < total_fighters && world.ents[slot].hp > 0; }
int battle_fighter_transform(int slot) {
    if (slot < 0 || slot >= total_fighters || !world.ents[slot].transform_target) return -1;
    return world.ents[slot].transform_target->objs[0].hdr->id;
}

// ---------------------------------------------------------------------------------------------------- draw

namespace {
void draw_hud() {
    video::text_clear();
    // Up to four fighters get tall cells (bars, name and role) inside a soft frame; more share the height in two rows.
    const bool tall = total_fighters <= 4;
    video::hud_shift(-4, tall ? -3 : -2);
    if (!current.hud) return;
    video::fill(0, 0, 32, 5, UI_PLAIN, 0);
    video::fill(0, 31, 32, 1, UI_PLAIN, 0);               // shown above the panel by the scroll
    for (int i = 0; i < total_fighters; ++i) {
        const Ent& e = world.ents[i];
        const int col = (i % 4) * 7 + 1;
        const int row = tall ? 0 : (i / 4) * 2;
        video::fill(col, row, 2, 2, i == 0 && current.player >= 0 ? UI_LIGHT : UI_PANEL, 0);
        if (e.live && e.hp <= 0) video::text(col, row, "KO", 1, 0);
        video::hp_bar(col + 2, row, 4, e.live ? e.hp : 0, e.live ? e.dark_hp : 0, 500);
        video::bar(col + 2, row + 1, 4, e.live ? e.mp : 0, 500, 3);
        if (tall) {
            char name[8];
            const char* full = ROSTER_NAME[roster_of[i]];
            int n = 0;
            while (full[n] && n < 7) { name[n] = full[n]; ++n; }
            name[n] = 0;
            video::text(col, 2, name, 1, 1);
            const bool mine = i == 0 && current.player >= 0;
            video::text(col, 3, !e.live ? "MERGED" : e.hp <= 0 ? "DOWN" : mine ? "PLAYER" : current.teams ? (e.team == 1 ? "BLUE" : "RED") : "CPU", mine ? 1 : 0, 1);
        }
    }
    if (ticks < 50 && !current.stage_mode) video::text(12, 8, "FIGHT!", 1);
    else if (world.round_counter > 0 && world.round_counter < 120) video::text(13, 8, "K.O.", 1);
    else if (world.round_counter >= 144 && current.player >= 0)
        video::text(9, 8, "A/B: CONTINUE", 1);
    if (current.stage_mode && current.survival) {
        video::text(18, 1, "SURVIVAL", 1, 1);
        video::text_number(27, 1, survival_waves, 3, 1, 1);
    } else if (current.stage_mode) {
        video::text(21, 1, "STAGE", 1, 1);
        video::text_number(27, 1, stage_level / 10 + 1, 1, 1, 1);
        video::text(28, 1, "-", 1, 1);
        video::text_number(29, 1, stage_level % 10 + 1, 1, 1, 1);
    } else if (current.army_mode) {
        const ArmyStats stats = world.army_stats();
        video::text(16, 0, "MEN", 1, 1); video::text_number(20, 0, stats.living[0], 2, 1, 1);
        video::text(22, 0, "-", 1, 1); video::text_number(23, 0, stats.living[1], 2, 1, 1);
        video::text(16, 1, "HP", 1, 1); video::text_number(19, 1, stats.hp[0], 4, 1, 1);
        video::text(23, 1, "-", 1, 1); video::text_number(24, 1, stats.hp[1], 4, 1, 1);
        video::text(16, 2, "RES", 1, 1); video::text_number(20, 2, army_run.reserves(0), 3, 1, 1);
        video::text(23, 2, "-", 1, 1); video::text_number(24, 2, army_run.reserves(1), 3, 1, 1);
        video::text(16, 3, "KOS", 1, 1); video::text_number(20, 3, world.army_deaths[0], 3, 1, 1);
        video::text(23, 3, "-", 1, 1); video::text_number(24, 3, world.army_deaths[1], 3, 1, 1);
        video::text(16, 4, "HIT", 1, 1); video::text_number(20, 4, world.army_damage[0], 3, 1, 1);
        video::text(23, 4, "-", 1, 1); video::text_number(24, 4, world.army_damage[1], 3, 1, 1);
    }
    if (current.stage_mode) {
        const StageStats stats = world.stage_stats();
        video::text(16, 2, "A:", 0, 1); video::text_number(18, 2, stats.allies, 2, 0, 1);
        video::text(21, 2, "HP:", 0, 1); video::text_number(24, 2, stats.ally_hp, 4, 0, 1);
        video::text(16, 3, "E:", 0, 1); video::text_number(18, 3, stats.enemies, 2, 0, 1);
        video::text(21, 3, "HP:", 0, 1); video::text_number(24, 3, stats.enemy_hp, 4, 0, 1);
        if (stats.reserves > 0) { video::text(16, 4, "RESERVE:", 0, 1); video::text_number(24, 4, stats.reserves, 3, 0, 1); }
    }
    if (current.stage_mode && stage_intro > 0 && !world.finished) {
        if (current.survival) video::text(10, 8, "SURVIVAL", 1);
        else {
            video::text(10, 8, "STAGE", 1);
            video::text_number(16, 8, stage_level / 10 + 1, 1, 1);
            video::text(17, 8, "-", 1);
            video::text_number(18, 8, stage_level % 10 + 1, 1, 1);
        }
    }
    if (current.stage_mode && stage_intro == 0 && stage_phase_go > 0 && !world.finished) {
        if (((stage_phase_go + 101) / 10) % 2 == 1) video::text(23, 8, "GO >", 1);
    } else if (current.stage_mode && stage_waiting && stage_intro == 0 && !world.finished) {
        if (stage_clear_delay > 0) {
            if (((stage_clear_delay + 101) / 10) % 2 == 1) video::text(23, 8, "GO >", 1);
        } else if (stage_group_clear) video::text(8, 8, stage_clear_timer >= 144 ? "A/B: CONTINUE" : "STAGE CLEAR", 1);
        else video::text(23, 8, "GO >", 1);
    }
    if (paused) video::text(12, 8, "PAUSED", 1);
#ifdef LF2_TIMING
    video::text_number(20, 5, battle_debug_value, 3, 0);
    video::text_number(25, 5, battle_debug_value2, 3, 0);
#endif
}
}  // namespace

void battle_draw() {
    if (stage_restart_level >= 0) {
        BattleSetup next = current;
        next.stage_start_level = stage_restart_level;
        next.seed += 2654435761u;
        battle_start(next);
        stage_wait_release = true;
        stage_shutter = 11;
    }
    if (stage_background_pending) {
        video::stage_load(current.stage);
        stage_background_pending = false;
    }
    for (int i = 0; i < hud_slots; ++i) {
        const Group* group = world.groups[i];
        if (!group || shown_group[i] == group) continue;
        video::upload_raw(video::tile_head + i * 4, group->head, 128);
        shown_group[i] = group;
    }
    for (bool& used : bank_used) used = false;
    // Camera, with a little shake after a heavy hit.
    camera_world = world.camera;
    const int title_offset = current.title ? 10 : 0;
    video::stage_scroll(camera_world, title_offset);
    draw_hud();
    if (stage_shutter > 0)
        video::stage_shutter(stage_shutter <= 10 ? stage_shutter * 13 / 10 : (21 - stage_shutter) * 13 / 10);
    video::bg0_flush();

    video::sprites_begin();
    // Status cells: soft frame and head icon.
    if (current.hud) {
        const bool tall = total_fighters <= 4;
        for (int i = 0; i < total_fighters; ++i) {
            const int col = (i % 4) * 7 + 1;
            const int hx = col * 8 + 4, hy = tall ? 3 : 2 + (i / 4) * 17;
            if (world.ents[i].live && world.ents[i].hp > 0) video::sprite(hx, hy, 16, video::tile_head + i * 4, bank_for(world.groups[i], team_variant(world.ents[i])), 0, false);
            const bool mine = i == 0 && current.player >= 0;
            const int bank = current.teams ? (world.ents[i].team == 1 ? 13 : 12) : mine ? 14 : 11;
            if (tall) video::sprite(hx - 3, 1, 64, video::tile_ring, bank, 0, false);
            else video::sprite(hx - 3, 1 + (i / 4) * 17, 64, video::tile_ring2, bank, 0, false, true);
        }
    }
    // Release tiles for expired sparks before drawing the depth-sorted objects.
    for (int i = 0; i < max_sparks; ++i) {
        const Spark& s = world.sparks[i];
        if (!s.live) {
            if (spark_slots[i] >= 0) video::free_tiles(spark_slots[i], 32);
            spark_slots[i] = spark_frames[i] = -1;
            continue;
        }
    }
    auto draw_spark = [&](int i) {
        const Spark& s = world.sparks[i];
        if (spark_slots[i] < 0) spark_slots[i] = video::alloc_tiles(32);
        if (spark_slots[i] < 0) return;
        const int frame = (s.kind == 1 ? 10 : 0) + (s.small ? 5 : 0) + (s.kind == 1 ? (9 - s.timer) / 2 : 5 - s.timer);
        // The first frame in each spark sequence is intentionally blank in LF2.
        if (frame % 5 == 0) return;
        if (spark_frames[i] != frame) {
            video::upload(spark_slots[i], 32, ui_bin + UI_SPARK_OFF + frame * 512, 512);
            spark_frames[i] = frame;
        }
        const int sx = scaled(s.x - camera_world) - 16;
        const int sy = scaled(s.z + s.y - 128) + video::field_top + title_offset - 16;
        video::sprite(sx, sy, 32, video::tile_index(spark_slots[i], 32), 9, 1, false);
    };
    // Entities, nearest to the camera first (lowest OAM index is in front).
    int n = 0;
    for (int i = 0; i < max_ents; ++i) {
        const Ent& e = world.ents[i];
        if (!e.live) {
            if (e.sprite_slot >= 0) {
                video::free_tiles(e.sprite_slot, 0);
                Ent& m = world.ents[i];
                m.sprite_slot = -1;
                m.shown_tile = 0xFFFFFFFFu;
            }
            continue;
        }
        int j = n++;
        while (j > 0 && world.ents[order[j - 1]].z <= e.z) { order[j] = order[j - 1]; --j; }
        order[j] = i;
    }
    int budget = upload_budget;
    for (int k = 0; k < n; ++k) {
        for (int i = 0; i < max_sparks; ++i)
            if (world.sparks[i].live && world.sparks[i].owner == order[k]) draw_spark(i);
        Ent& e = world.ents[order[k]];
        const Frame& f = e.def->frame(e.frame);
        const Picture* pictures = e.picture_offset == 114 ? e.def->knight_alt : e.def->alt;
        if (e.picture_offset && (!pictures || e.frame < 0 || e.frame >= e.def->hdr->nframes)) continue;
        const Picture* alternate = e.picture_offset ? &pictures[e.frame] : nullptr;
        if (alternate ? !alternate->has_picture() : !f.has_picture()) continue;
        const int size = alternate ? alternate->size : f.size;
        const int ox = alternate ? alternate->ox : f.ox, oy = alternate ? alternate->oy : f.oy;
        const std::uint32_t tile = alternate ? alternate->tile() : f.tile();
        if (e.blink > 0 && (e.blink & 2)) continue;
        // Tile slots follow the picture being shown (only a few pictures need the big 64x64 kind).
        if (e.sprite_slot >= 0 && (e.sprite_slot >= 100) != (size > 32)) {
            video::free_tiles(e.sprite_slot, 0);
            e.sprite_slot = -1;
        }
        if (e.sprite_slot < 0) {
            e.sprite_slot = video::alloc_tiles(size);
            e.shown_tile = 0xFFFFFFFFu;
            if (e.sprite_slot < 0) continue;
        }
        const unsigned bytes = unsigned(size) * size / 2;
        if (e.shown_tile != tile) {
            if (budget < int(bytes)) continue;
            video::upload(e.sprite_slot, size, sprites_bin + tile, bytes);
            e.shown_tile = tile;
            budget -= int(bytes);
        }
        const int ax = scaled(to_int(e.x) - camera_world);
        const int ay = scaled(to_int(e.z) + to_int(e.y) - 128) + video::field_top + title_offset;
        const bool flip = e.facing == 1;
        const int left = flip ? ax - (ox + size) : ax + ox;
        const int bank = e.group >= max_fighters ? weapon_bank : bank_for(world.groups[e.group], team_variant(world.ents[e.group]));
        video::sprite(left, ay + oy, size, video::tile_index(e.sprite_slot, size), bank, 1, flip);
    }
    // Shadows behind everything else.
    for (int k = 0; k < n; ++k) {
        const Ent& e = world.ents[order[k]];
        if (e.kind != 0) continue;
        if (e.blink > 0 && (e.blink & 2)) continue;
        const int sx = scaled(to_int(e.x) - camera_world) - 8;
        const int sy = scaled(to_int(e.z) - 128) + video::field_top + title_offset - 4;
        video::sprite(sx, sy, 16, video::tile_shadow, 15, 2, false, true);
    }
    video::sprites_end();
    video::oam_flush();
}

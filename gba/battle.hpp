// SPDX-License-Identifier: MIT
// Copyright (c) 2026 OpenLF2 contributors

// A fight: the simulation plus drawing it with sprites and scrolling stage layers.
#pragma once
#include "../game/world.hpp"
#include "../game/army.hpp"

struct BattleSetup {
    int player = 0;          // roster index of the player's fighter, or -1 for a computer-only demo
    int opponents = 3;
    int stage = 0;
    unsigned seed = 1;
    bool hud = true;
    bool title = false;      // title demo: no foreground strip (the logo uses its layer)
    bool teams = false;      // false: every fighter for themselves; true: alternating two teams
    bool stage_mode = false;
    bool survival = false;
    int stage_start_level = 0;
    bool army_mode = false;
    int difficulty = 1;       // -1 crazy, 0 difficult, 1 normal, 2 easy
    int army_size = 1, enemy_army_size = 1, army_strategy = 0, enemy_strategy = 0;
    int army_defense = 100, enemy_defense = 100;
    bool army_custom = false;
    int army_first[2][lf2::army_types] = {}, army_reserve[2][lf2::army_types] = {};
    int lineup_count = 0;
    int lineup_roster[4] = {}, lineup_hp[4] = {}, lineup_team[4] = {};
    int lineup_transform[4] = {-1, -1, -1, -1};
};

void battle_init();
void battle_start(const BattleSetup& setup);
// Runs one logic frame with the given pressed buttons (hardware key bits, 1 = held).
void battle_tick(unsigned keys);
// Redraws sprites, status panel and background. Call during vertical blank.
void battle_draw();
bool battle_over();                         // the fight has been decided and the result shown long enough
int battle_winner_roster();                 // roster index of the winner, or -1
bool battle_player_won();
int battle_winner_team();
int battle_stage_checkpoint();
int battle_stage_background(int level);
void battle_set_paused(bool paused);
bool battle_player_alive();
int battle_fighter_dark_hp(int slot);
bool battle_fighter_alive(int slot);
int battle_fighter_transform(int slot);

#ifdef LF2_TIMING
extern int battle_debug_value, battle_debug_value2;    // worst logic frame time in percent of a video frame (development builds)
#endif

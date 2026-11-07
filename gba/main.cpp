// SPDX-License-Identifier: MIT
// Copyright (c) 2026 OpenLF2 contributors

// Front end: title, fighter selection, stage and opponent setup, the fight and its result.
#include "assets.h"
#include "battle.hpp"
#include "hw.hpp"
#include "sound.hpp"
#include "video.hpp"
#include "../game/data.hpp"
#include "../game/tournament.hpp"
#include <cstdint>

namespace {

enum class Screen { title, select, setup, army_setup, tournament_setup, bracket, battle, result };

constexpr int frames_per_tick = 2;              // the simulation runs at 30 Hz
constexpr int min_opponents = 1, max_opponents = 7;

Screen screen = Screen::title;
std::uint16_t keys_before = 0;
unsigned frame = 0;
int cursor = 0;
int stage_cursor = 0;
int opponents = 3;
int chosen = 0;
int result_timer = 0;
bool paused = false;
bool won = false;
int winner_roster = -1;
int preview_handle = -1;
#ifdef LF2_TIMING
int worst_tick = 0, worst_present = 0;
#endif
BattleSetup setup;

void new_demo() {
    BattleSetup demo;
    demo.player = -1;
    demo.opponents = 2;
    demo.stage = int((frame * 7) % STAGE_COUNT);
    while (video::stage_has_front(demo.stage)) demo.stage = (demo.stage + 1) % STAGE_COUNT;
    demo.seed = frame * 2654435761u + 77;
    demo.hud = false;
    demo.title = true;
    battle_start(demo);
    video::logo_show();
}

// ------------------------------------------------------------------------------------------------ title

void title_text() {
    video::text(5, 9, "FOR GAME BOY ADVANCE", 0);
    if ((frame / 30) % 2 == 0) video::text(9, 17, "PRESS START", 1, 0);
}

void draw_title() {
    video::text_clear();
    title_text();
    video::hud_shift(0, -4);
    video::bg0_flush();
}

// ------------------------------------------------------------------------------------------------ select

void select_enter() {
    screen = Screen::select;
    video::stage_clear();
    video::reset_sprites();
    preview_handle = video::alloc_tiles(32);
    video::sprites_begin();
    video::sprites_end();
    video::oam_flush();
}

void draw_select() {
    video::text_clear();
    video::panel(0, 0, 30, 20);
    video::text(4, 0, "CHOOSE YOUR FIGHTER", 1, 1);
    const int per_column = (ROSTER_COUNT + 1) / 2;
    for (int i = 0; i < ROSTER_COUNT; ++i) {
        const int col = i < per_column ? 2 : 11;
        const int row = 2 + (i % per_column);
        if (i == cursor) video::fill(col - 1, row, 9, 1, UI_LIGHT, 0);
        video::text(col, row, ROSTER_NAME[i], 0, i == cursor ? 2 : 1);
    }
    video::text(1, 18, "A:NEXT", 0, 1);
    video::bg0_flush();

    // Preview: the standing frame, enlarged.
    const lf2::Group& g = lf2::roster_group(cursor);
    video::obj_palette(0, g.palette);
    const lf2::Frame& f = g.objs[0].frame(0);
    video::sprites_begin();
    if (f.has_picture() && preview_handle >= 0) {
        video::upload(preview_handle, f.size, sprites_bin + f.tile(), unsigned(f.size) * f.size / 2);
        const int scale = 2;
        const int x = 188 + (f.ox + 16) * scale - 16, y = 80 + (f.oy + 24) * scale - 16;
        video::sprite_scaled(x - 16, y - 16, f.size, video::tile_index(preview_handle, f.size), 0, 0, 256 * scale);
    }
    video::sprites_end();
    video::oam_flush();
}

// ------------------------------------------------------------------------------------------------- setup

int setup_row = 0;
int battle_mode = 0;  // free for all, teams, stage, survival, army, individual and team championship
int difficulty = 1;
int stage_checkpoint = 0;
int army_size = 1, enemy_army_size = 1, army_strategy = 0, enemy_strategy = 0;
int army_defense = 100, enemy_defense = 100;
bool army_configured[2] = {}, army_edited[2] = {};
const char* army_names[] = {"BALANCED", "INFERIOR", "RANGED", "MELEE", "GIANT", "ZERO", "FULL"};
const char* soldier_names[] = {"BANDIT", "HUNTER", "JACK", "SORCERER", "JUSTIN", "MARK", "MONK", "JAN", "KNIGHT", "MILK", "BEER"};
int army_side = 0, army_row = 0, army_column = 0;
lf2::Tournament tournament;
int tournament_row = 0, tournament_mapping[4] = {};
bool tournament_in_match = false;

void setup_show_stage() {
    const int background = battle_mode == 2 ? battle_stage_background(stage_checkpoint) : battle_mode == 3 ? battle_stage_background(50) : stage_cursor;
    video::stage_load(background);
    int width = video::stage_width(background);
    video::stage_scroll((width - 794) / 2);
}

void setup_enter() {
    screen = Screen::setup;
    if (preview_handle >= 0) { video::free_tiles(preview_handle, 32); preview_handle = -1; }
    setup_row = 0;
    setup_show_stage();
}

void draw_setup() {
    video::text_clear();
    video::panel(0, 0, 30, 5);
    video::text(4, 2, "CHOOSE THE BATTLEFIELD", 1, 1);
    video::panel(0, 11, 30, 9);
    const char* labels[3] = {"STAGE", battle_mode == 4 ? "SIZE" : battle_mode == 2 ? "START" : "ENEMIES", "MODE"};
    for (int i = 0; i < 3; ++i) {
        const int row = 12 + i * 2;
        const int variant = i == setup_row ? 2 : 1;
        if (i == setup_row) video::fill(1, row, 28, 1, UI_LIGHT, 0);
        video::text(2, row, labels[i], 0, variant);
        video::text(10, row, "<", 1, variant);
        video::text(28, row, ">", 1, variant);
    }
    const int v0 = setup_row == 0 ? 2 : 1, v1 = setup_row == 1 ? 2 : 1, v2 = setup_row == 2 ? 2 : 1;
    const int background = battle_mode == 2 ? battle_stage_background(stage_checkpoint) : battle_mode == 3 ? battle_stage_background(50) : stage_cursor;
    video::text(12, 12, STAGE_NAME[background], 1, v0);
    video::text_number(18, 14, battle_mode == 4 ? army_size : battle_mode == 2 ? stage_checkpoint / 10 + 1 : opponents, 1, 1, v1);
    if (battle_mode == 4) { video::text(20, 14, "/", 1, v1); video::text_number(22, 14, enemy_army_size, 1, 1, v1); }
    const char* modes[] = {"FREE FOR ALL", "TEAM BATTLE", "STAGE MODE", "SURVIVAL", "ARMY BATTLE", "CHAMPIONSHIP", "TEAM CHAMP"};
    video::text(12, 16, modes[battle_mode], 1, v2);
    if (battle_mode == 4) {
        video::text(2, 6, "START+L/R: DEFENSE", 1, 0);
        video::text(1, 10, "SELECT+<>: R SIZE", 1, 0);
        video::text_number(7, 8, army_defense, 3, 1, 0);
        video::text(12, 8, "VS", 1, 0);
        video::text_number(17, 8, enemy_defense, 3, 1, 0);
        video::text(1, 18, "L:", 0, 1); video::text(3, 18, army_edited[0] ? "CUSTOM" : army_names[army_strategy], 0, 1);
        video::text(15, 18, "R:", 0, 1); video::text(17, 18, army_edited[1] ? "CUSTOM" : army_names[enemy_strategy], 0, 1);
        video::text(2, 19, "A:ARMY SETUP  B:BACK", 0, 1);
    } else {
        const char* levels[] = {"CRAZY", "DIFFICULT", "NORMAL", "EASY"};
        video::text(2, 7, "L/R: DIFFICULTY", 1, 0);
        video::text(10, 9, levels[difficulty + 1], 1, 0);
        video::text(2, 18, "A:FIGHT  B:BACK", 0, 1);
    }
    video::bg0_flush();
    video::sprites_begin();
    video::sprites_end();
    video::oam_flush();
}

void army_setup_enter() {
    screen = Screen::army_setup;
    army_side = army_row = army_column = 0;
    lf2::ArmyRun presets;
    presets.preset(0, army_strategy, army_size);
    presets.preset(1, enemy_strategy, enemy_army_size);
    for (int side = 0; side < 2; ++side) if (!army_configured[side]) {
        for (int i = 0; i < lf2::army_types; ++i) {
            setup.army_first[side][i] = presets.on_field[side][i];
            setup.army_reserve[side][i] = presets.stock[side][i] - presets.on_field[side][i];
        }
        army_configured[side] = true;
    }
    setup.army_custom = true;
}

void draw_army_setup() {
    video::text_clear();
    video::panel(0, 0, 30, 20);
    video::text(3, 0, army_side == 0 ? "BLUE ARMY" : "RED ARMY", 1, 1);
    video::text(1, 1, "TROOP         FIELD  RESERVE", 0, 1);
    for (int i = 0; i < lf2::army_types; ++i) {
        const int row = i + 3;
        if (i == army_row) video::fill(1, row, 28, 1, UI_LIGHT, 0);
        const int variant = i == army_row ? 2 : 1;
        video::text(2, row, soldier_names[i], 0, variant);
        video::text_number(16, row, setup.army_first[army_side][i], 2, 1, variant);
        video::text_number(23, row, setup.army_reserve[army_side][i], 2, 1, variant);
        if (i == army_row) video::text(army_column == 0 ? 15 : 22, row, ">", 1, variant);
    }
    video::text(1, 15, "L/R: TEAM  A: COLUMN", 0, 1);
    video::text(1, 16, "LEFT/RIGHT: CHANGE", 0, 1);
    video::text(1, 18, "START: FIGHT  B: BACK", 0, 1);
    video::bg0_flush();
    video::sprites_begin(); video::sprites_end(); video::oam_flush();
}

void tournament_setup_enter() {
    screen = Screen::tournament_setup;
    tournament_row = 0;
    unsigned random = frame * 1664525u + 1013904223u;
    bool used[ROSTER_COUNT] = {};
    used[chosen] = true;
    for (int i = 0; i < 8; ++i) {
        int fighter = chosen;
        if (i != 0) {
            random = random * 1664525u + 1013904223u;
            fighter = int((random >> 8) % ROSTER_COUNT);
            for (int j = 0; j < ROSTER_COUNT && used[fighter]; ++j) fighter = (fighter + 1) % ROSTER_COUNT;
            used[fighter] = true;
        }
        tournament.entrants[i] = {fighter, ROSTER_ID[fighter], 500, i == 0};
    }
    tournament_in_match = false;
}

void draw_tournament_setup() {
    video::text_clear(); video::panel(0, 0, 30, 20);
    video::text(2, 0, battle_mode == 6 ? "TEAM CHAMPIONSHIP" : "CHAMPIONSHIP", 1, 1);
    video::text(2, 1, "SELECT EIGHT FIGHTERS", 0, 1);
    for (int i = 0; i < 8; ++i) {
        const int row = i + 3, variant = i == tournament_row ? 2 : 1;
        if (i == tournament_row) video::fill(1, row, 28, 1, UI_LIGHT, 0);
        video::text_number(2, row, i + 1, 1, 1, variant);
        video::text(5, row, ROSTER_NAME[tournament.entrants[i].fighter], 0, variant);
        video::text(19, row, i == 0 ? "PLAYER" : "CPU", 0, variant);
    }
    video::text(1, 13, "UP/DOWN: SLOT", 0, 1);
    video::text(1, 14, "LEFT/RIGHT: FIGHTER", 0, 1);
    video::text(1, 16, "A: BRACKET  B: BACK", 0, 1);
    video::bg0_flush(); video::sprites_begin(); video::sprites_end(); video::oam_flush();
}

void draw_bracket() {
    video::text_clear(); video::panel(0, 0, 30, 20);
    video::text(2, 0, tournament.finished() ? "CHAMPION" : "CHAMPIONSHIP", 1, 1);
    if (tournament.finished()) {
        const int champion = tournament.champion() * (tournament.team() ? 2 : 1);
        video::text(3, 4, ROSTER_NAME[tournament.entrants[champion].fighter], 1, 1);
        if (tournament.team()) video::text(3, 6, ROSTER_NAME[tournament.entrants[champion + 1].fighter], 1, 1);
        video::text(2, 16, "A: EXIT", 0, 1);
    } else {
        const lf2::TournamentMatch m = tournament.current();
        video::text(2, 2, "ROUND", 1, 1); video::text_number(9, 2, tournament.round() + 1, 1, 1, 1);
        for (int i = 0; i < m.count; ++i) {
            const auto& e = tournament.entrants[m.entrants[i]];
            const int row = 5 + i * 2 + (i >= m.count / 2 ? 1 : 0);
            video::text(3, row, ROSTER_NAME[e.fighter], 1, 1);
            video::text_number(17, row, e.hp, 3, 0, 1);
            if (e.human) video::text(23, row, "YOU", 0, 1);
        }
        video::text(2, 16, "A: NEXT MATCH", 0, 1);
        video::text(2, 18, "B: EXIT", 0, 1);
    }
    video::bg0_flush(); video::sprites_begin(); video::sprites_end(); video::oam_flush();
}

// ------------------------------------------------------------------------------------------------- result

void result_enter() {
    screen = Screen::result;
    result_timer = 0;
    won = battle_player_won();
    winner_roster = battle_winner_roster();
    sound_play(SOUND_MENU_END);
}

void draw_result() {
    // Added to the status panel that battle_draw() has just built.
    video::panel(3, 6, 24, 7);
    if (setup.stage_mode) {
        video::text(8, 7, won ? "STAGE CLEAR" : "STAGE FAILED", 1, 1);
        if (!won && !setup.survival) {
            video::text(5, 9, "RETRY STAGE", 0, 1);
            video::text_number(18, 9, stage_checkpoint / 10 + 1, 1, 1, 1);
        }
    } else if (setup.army_mode) {
        const int team = battle_winner_team();
        video::text(11, 7, team == 0 ? "DRAW" : won ? "YOU WIN!" : "YOU LOSE", 1, 1);
        if (team != 0) video::text(7, 9, won ? "BLUE ARMY WINS" : "RED ARMY WINS", 1, 1);
    } else if (winner_roster >= 0) {
        video::text(11, 7, won ? "YOU WIN!" : "YOU LOSE", 1, 1);
        video::text(5, 9, "WINNER:", 0, 1);
        video::text(13, 9, ROSTER_NAME[winner_roster], 1, 1);
    } else {
        video::text(13, 8, "DRAW", 1, 1);
    }
    video::text(11, 11, "PRESS A", 0, 1);
    video::bg0_flush();
}

// -------------------------------------------------------------------------------------------- transitions

void fade_to(int target) {
    static int level = 16;
    const int step = target > level ? 2 : -2;
    while (level != target) { level += step; video::fade(level); wait_frame(); sound_update(); }
}

void present() {
    switch (screen) {
    case Screen::title:
        battle_draw();
        draw_title();
        break;
    case Screen::select: draw_select(); break;
    case Screen::setup: draw_setup(); break;
    case Screen::army_setup: draw_army_setup(); break;
    case Screen::tournament_setup: draw_tournament_setup(); break;
    case Screen::bracket: draw_bracket(); break;
    case Screen::battle: battle_draw(); break;
    case Screen::result: battle_draw(); draw_result(); break;
    }
}

void go(Screen next) {
    fade_to(16);                                // fully black: the new screen can be set up unseen
    if (next == Screen::battle) music_play(MUSIC_STAGE1 + stage_cursor % 5);
    else if (next == Screen::result) music_stop();
    else if (next == Screen::title || next == Screen::select) music_play(MUSIC_MAIN);
    switch (next) {
    case Screen::title: screen = Screen::title; new_demo(); break;
    case Screen::select: select_enter(); break;
    case Screen::setup: setup_enter(); break;
    case Screen::army_setup: army_setup_enter(); break;
    case Screen::tournament_setup: tournament_setup_enter(); break;
    case Screen::bracket: screen = Screen::bracket; break;
    case Screen::battle:
        screen = Screen::battle;
        setup.player = chosen;
        setup.opponents = opponents;
        setup.stage = stage_cursor;
        setup.seed = frame * 2654435761u + 1;
        setup.hud = true;
        setup.teams = battle_mode == 1 || battle_mode == 4 || (tournament_in_match && tournament.team());
        if (!tournament_in_match) setup.lineup_count = 0;
        setup.stage_mode = battle_mode == 2 || battle_mode == 3;
        setup.survival = battle_mode == 3;
        setup.stage_start_level = stage_checkpoint;
        setup.army_mode = battle_mode == 4;
        setup.difficulty = difficulty;
        if (!setup.army_mode) setup.army_custom = false;
        setup.army_size = army_size;
        setup.enemy_army_size = enemy_army_size;
        setup.army_strategy = army_strategy;
        setup.enemy_strategy = enemy_strategy;
        setup.army_defense = army_defense;
        setup.enemy_defense = enemy_defense;
        paused = false;
        battle_start(setup);
        break;
    case Screen::result: result_enter(); break;
    }
    present();
    fade_to(0);
}

}  // namespace

extern "C" int main() {
    REG_DISPCNT = 0x0080;
    REG_WAITCNT = 0x4317;
    video::init();
    video::load_ui();
    battle_init();
    sound_init();
    frame_sync_start();
    video::fade(16);
    screen = Screen::title;
    new_demo();
    present();
    music_play(MUSIC_MAIN);
    REG_DISPCNT = 0x1F40;                        // mode 0, BG0-3, sprites, 1D tile mapping, screen on
    fade_to(0);

    for (;;) {
        wait_frame();
        if (REG_VCOUNT >= 160) {                // vertical blank: update the picture of the last logic frame
#ifdef LF2_TIMING
            const std::uint16_t t0 = REG_TM2CNT_L;
            present();
            const std::uint16_t t1 = REG_TM2CNT_L;
            const int d = t1 >= t0 ? t1 - t0 : t1 + 4389 - t0;
            if (d > worst_present) worst_present = d;
            battle_debug_value2 = worst_present * 100 / 4389;
#else
            present();
#endif
        }
        const std::uint16_t keys = ~REG_KEYINPUT & 0x3FF;
        const std::uint16_t pressed = keys & ~keys_before;
        keys_before = keys;
        ++frame;
        sound_update();

        switch (screen) {
        case Screen::title:
            if ((frame % frames_per_tick) == 0) battle_tick(0);
            if (battle_over()) new_demo();
            if (pressed & Key::start) { sound_play(SOUND_MENU_OK); go(Screen::select); }
            break;
        case Screen::select: {
            if (pressed & (Key::up | Key::left)) { cursor = (cursor + ROSTER_COUNT - 1) % ROSTER_COUNT; sound_play(SOUND_MENU_JOIN); }
            if (pressed & (Key::down | Key::right)) { cursor = (cursor + 1) % ROSTER_COUNT; sound_play(SOUND_MENU_JOIN); }
            if ((pressed & Key::r) && opponents < max_opponents) { ++opponents; sound_play(SOUND_MENU_JOIN); }
            if ((pressed & Key::l) && opponents > min_opponents) { --opponents; sound_play(SOUND_MENU_JOIN); }
            if (pressed & Key::a) { chosen = cursor; sound_play(SOUND_MENU_OK); go(Screen::setup); }
            if (pressed & Key::b) { sound_play(SOUND_MENU_CANCEL); go(Screen::title); }
            break;
        }
        case Screen::setup: {
            if (battle_mode != 4 && (pressed & Key::l)) { difficulty = difficulty == -1 ? 2 : difficulty - 1; sound_play(SOUND_MENU_JOIN); }
            if (battle_mode != 4 && (pressed & Key::r)) { difficulty = difficulty == 2 ? -1 : difficulty + 1; sound_play(SOUND_MENU_JOIN); }
            if (battle_mode == 4 && (pressed & Key::l)) {
                if (keys & Key::start) { army_defense += 50; if (army_defense > 300) army_defense = 100; }
                else { army_strategy = (army_strategy + 1) % 7; army_configured[0] = army_edited[0] = false; }
                sound_play(SOUND_MENU_JOIN);
            }
            if (battle_mode == 4 && (pressed & Key::r)) {
                if (keys & Key::start) { enemy_defense += 50; if (enemy_defense > 300) enemy_defense = 100; }
                else { enemy_strategy = (enemy_strategy + 1) % 7; army_configured[1] = army_edited[1] = false; }
                sound_play(SOUND_MENU_JOIN);
            }
            if (pressed & Key::up) { setup_row = (setup_row + 2) % 3; sound_play(SOUND_MENU_JOIN); }
            if (pressed & Key::down) { setup_row = (setup_row + 1) % 3; sound_play(SOUND_MENU_JOIN); }
            const int change = ((pressed & Key::right) ? 1 : 0) - ((pressed & Key::left) ? 1 : 0);
            if (change) {
                sound_play(SOUND_MENU_JOIN);
                if (setup_row == 0 && battle_mode != 2 && battle_mode != 3) { stage_cursor = (stage_cursor + STAGE_COUNT + change) % STAGE_COUNT; setup_show_stage(); }
                else if (setup_row == 1 && battle_mode == 4) {
                    int& size = keys & Key::select ? enemy_army_size : army_size;
                    const int old_size = size;
                    size += change; if (size < 1) size = 1; if (size > 3) size = 3;
                    if (size != old_size) { const int side = keys & Key::select ? 1 : 0; army_configured[side] = army_edited[side] = false; }
                }
                else if (setup_row == 1 && battle_mode == 2) { stage_checkpoint += change * 10; if (stage_checkpoint < 0) stage_checkpoint = 0; if (stage_checkpoint > 40) stage_checkpoint = 40; setup_show_stage(); }
                else if (setup_row == 1) { opponents += change; if (opponents < min_opponents) opponents = min_opponents; if (opponents > max_opponents) opponents = max_opponents; }
                else if (setup_row == 2) { battle_mode = (battle_mode + change + 7) % 7; setup_show_stage(); }
            }
            if (pressed & Key::a) { sound_play(SOUND_MENU_OK); go(battle_mode == 4 ? Screen::army_setup : battle_mode >= 5 ? Screen::tournament_setup : Screen::battle); }
            if (pressed & Key::b) { sound_play(SOUND_MENU_CANCEL); go(Screen::select); }
            break;
        }
        case Screen::army_setup: {
            if (pressed & Key::up) army_row = (army_row + lf2::army_types - 1) % lf2::army_types;
            if (pressed & Key::down) army_row = (army_row + 1) % lf2::army_types;
            if (pressed & (Key::l | Key::r)) army_side = 1 - army_side;
            if (pressed & Key::a) army_column = 1 - army_column;
            const int change = ((pressed & Key::right) ? 1 : 0) - ((pressed & Key::left) ? 1 : 0);
            if (change) {
                int& value = army_column == 0 ? setup.army_first[army_side][army_row] : setup.army_reserve[army_side][army_row];
                const int limit = army_column == 0 ? 10 : 30;
                value += change; if (value < 0) value = limit; if (value > limit) value = 0;
                army_edited[army_side] = true;
            }
            if (pressed & Key::start) { sound_play(SOUND_MENU_OK); go(Screen::battle); }
            if (pressed & Key::b) { sound_play(SOUND_MENU_CANCEL); go(Screen::setup); }
            break;
        }
        case Screen::tournament_setup: {
            if (pressed & Key::up) tournament_row = (tournament_row + 7) % 8;
            if (pressed & Key::down) tournament_row = (tournament_row + 1) % 8;
            const int change = ((pressed & Key::right) ? 1 : 0) - ((pressed & Key::left) ? 1 : 0);
            if (change) {
                auto& entrant = tournament.entrants[tournament_row];
                entrant.fighter = (entrant.fighter + change + ROSTER_COUNT) % ROSTER_COUNT;
                entrant.original_id = ROSTER_ID[entrant.fighter];
                if (tournament_row == 0) chosen = entrant.fighter;
            }
            if (pressed & Key::a) { tournament.start(battle_mode == 6, frame * 2654435761u + 1, difficulty); go(Screen::bracket); }
            if (pressed & Key::b) go(Screen::setup);
            break;
        }
        case Screen::bracket: {
            if (pressed & Key::b) { go(Screen::select); break; }
            if (pressed & Key::a) {
                if (tournament.finished()) { go(Screen::select); break; }
                if (tournament.simulate()) break;
                const auto match = tournament.current();
                const int half = match.count / 2;
                int human = 0;
                while (human < match.count && !tournament.entrants[match.entrants[human]].human) ++human;
                const int side = human / half;
                int slot = 0;
                tournament_mapping[slot++] = human;
                for (int i = side * half; i < (side + 1) * half; ++i) if (i != human) tournament_mapping[slot++] = i;
                for (int i = (1 - side) * half; i < (2 - side) * half; ++i) tournament_mapping[slot++] = i;
                setup.lineup_count = match.count;
                for (int i = 0; i < match.count; ++i) {
                    const auto& entrant = tournament.entrants[match.entrants[tournament_mapping[i]]];
                    setup.lineup_roster[i] = entrant.fighter;
                    setup.lineup_hp[i] = entrant.hp;
                    setup.lineup_team[i] = i < half ? 1 : 2;
                    setup.lineup_transform[i] = entrant.transform_id;
                }
                tournament_in_match = true;
                go(Screen::battle);
            }
            break;
        }
        case Screen::battle:
            if (pressed & Key::start) { paused = !paused; battle_set_paused(paused); sound_play(SOUND_MENU_OK); }
            if ((frame % frames_per_tick) == 0) {
#ifdef LF2_TIMING
                const std::uint16_t t0 = REG_TM2CNT_L;
                battle_tick(keys);
                const std::uint16_t t1 = REG_TM2CNT_L;
                const int d = t1 >= t0 ? t1 - t0 : t1 + 4389 - t0;
                if (d > worst_tick) worst_tick = d;
                battle_debug_value = worst_tick * 100 / 4389;
#else
                battle_tick(keys);
#endif
            }
            if (pressed & Key::select) { tournament_in_match = false; go(Screen::select); break; }
            if (battle_over()) {
                if (setup.stage_mode && !setup.survival) stage_checkpoint = battle_player_won() ? 0 : battle_stage_checkpoint();
                go(Screen::result);
            }
            break;
        case Screen::result:
            if ((frame % frames_per_tick) == 0) battle_tick(0);
            ++result_timer;
            if ((pressed & Key::a) || result_timer > 60 * 20) {
                sound_play(SOUND_MENU_OK);
                if (tournament_in_match) {
                    const auto match = tournament.current();
                    int hp[4] = {}; bool alive[4] = {};
                    for (int i = 0; i < match.count; ++i) {
                        hp[tournament_mapping[i]] = battle_fighter_dark_hp(i);
                        alive[tournament_mapping[i]] = battle_fighter_alive(i);
                        tournament.entrants[match.entrants[tournament_mapping[i]]].transform_id = battle_fighter_transform(i);
                    }
                    const int side = tournament_mapping[0] / (match.count / 2) + 1;
                    const int winner = battle_winner_team() == 1 ? side : battle_winner_team() == 2 ? 3 - side : 0;
                    tournament.result(winner, hp, alive);
                    tournament_in_match = false;
                    go(Screen::bracket);
                } else go(Screen::select);
            }
            break;
        }
    }
}

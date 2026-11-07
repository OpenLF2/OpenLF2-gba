// SPDX-License-Identifier: MIT
// Copyright (c) 2026 OpenLF2 contributors

// Screen layout: BG0 is the text and status panel; BG1..BG3 are the scrolling stage strips (front, middle, far);
// sprites hold the fighters, shadows, sparks and head icons.
#pragma once
#include "hw.hpp"
#include <cstdint>

namespace video {

constexpr int oam_count = 128;
constexpr int field_top = 40;        // the status panel covers the first 40 lines
constexpr int tile_pool32 = 42;      // 32x32 sprite slots
constexpr int tile_pool64 = 3;       // 64x64 sprite slots

void init();
void load_ui();                                       // font, panel and bar tiles, shared sprite tiles
void set_blank(bool blank);

// Stages. `index` is a stage of assets.h.
void stage_load(int index, bool front = true);
bool stage_has_front(int index);
void logo_show();                                     // title logo (load the stage with front = false)
int stage_width(int index);
void stage_z(int index, int& z1, int& z2);
const char* stage_name(int index);
// Scrolls the strips to the original-pixel camera position and streams in the columns that come into view.
void stage_scroll(int camera, int vertical_offset = 0);
void stage_clear();                                   // blank backdrop (for menus)

// Text and panel on BG0 (32x32 tiles). `bank` is 0 white, 1 gold.
void text_clear();
// variant 0: transparent background, 1: panel background, 2: highlight background
void text(int col, int row, const char* s, int bank = 0, int variant = 0);
void text_number(int col, int row, int value, int digits, int bank = 0, int variant = 0);
void panel(int col, int row, int w, int h);           // filled navy rectangle
void fill(int col, int row, int w, int h, int tile, int bank = 0);
void bar(int col, int row, int tiles, int value, int max, int bank);   // horizontal bar made of `tiles` tiles
void hp_bar(int col, int row, int tiles, int hp, int dark, int max);   // health with the recoverable part
void hud_shift(int x, int y);                         // scroll of the text layer in pixels
void stage_shutter(int height);                       // black stripes over the play field, 0..13 pixels
void bg0_flush();                                     // copies the text map to VRAM (vblank)

// Sprites.
void obj_palette(int bank, const std::uint16_t* colors);   // 16 colours
void obj_ui_palette();
int alloc_tiles(int size);                            // returns a handle (>= 0) or -1
void free_tiles(int handle, int size);
std::uint16_t tile_index(int handle, int size);       // first tile number of a handle
void upload(int handle, int size, const std::uint8_t* tiles, unsigned bytes);   // copy picture data to a slot
void upload_raw(unsigned tile_number, const std::uint8_t* data, unsigned bytes);
void sprites_begin();
void reset_sprites();                                 // free every tile slot
// Adds a sprite to the shadow OAM; returns false when full. size: 8, 16, 32 or 64 (square); wide16x8 for shadows.
bool sprite(int x, int y, int size, unsigned tile, int bank, int priority, bool hflip, bool wide = false);
// Affine (scaled) square sprite, `scale` in 8.8 (256 = natural size).
bool sprite_scaled(int x, int y, int size, unsigned tile, int bank, int priority, int scale);
void sprites_end();
void oam_flush();                                     // copies the shadow OAM to the hardware (vblank)
void fade(int level);                                 // 0 normal .. 16 black

// Fixed sprite tile numbers.
constexpr unsigned tile_shadow = 4, tile_spark = 8, tile_head = 24;   // 2 tiles, 4 x 4 tiles, 8 x 4 tiles
constexpr unsigned tile_pool32_base = 64, tile_pool64_base = 832, tile_ring = 736, tile_ring2 = 800;   // frames: 64 and 32 tiles

}  // namespace video

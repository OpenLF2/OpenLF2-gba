// SPDX-License-Identifier: MIT
// Copyright (c) 2026 OpenLF2 contributors

#include "video.hpp"
#include "assets.h"
#include <cstring>

namespace video {

namespace {
constexpr unsigned sb_far = 28, sb_mid = 29, sb_front = 30, sb_text = 31;
constexpr unsigned screenblock(unsigned n) { return 0x06000000 + n * 0x800; }
constexpr int ring = 32;
constexpr int rows = 15;
constexpr int column_stride = 512;
constexpr int blank_tile = 480;
constexpr int scale_num = 1229, scale_shift = 12;                 // 0.3

// Stage layout in stages.bin (see tools/stages.py).
#pragma pack(push, 2)
struct StageHeader {
    std::uint16_t width, z1, z2, backdrop;
    std::uint8_t nstrips, pad;
    char name[16];
    std::uint32_t strip_off[3];
};
struct StripHeader {
    std::uint16_t cols, speed;
    std::uint8_t bank0, banks;
    std::uint16_t slot;               // layer, frame count, ticks/frame, and indexed-column flag
    std::uint16_t pal[6][16];
};
#pragma pack(pop)

struct Strip {
    const StripHeader* head = nullptr;
    const std::uint8_t* columns = nullptr;
    std::int16_t loaded[ring];
    int bg = 0;                      // hardware layer 1..3
    int charbase = 0;
    int animation_frame = -1;
    unsigned map = 0;
};
Strip strips[3];
int strip_count = 0;
int stage_index = -1;
unsigned stage_tick = 0;

std::uint16_t text_map[32 * 32];
std::uint16_t oam_shadow[oam_count * 4];
int oam_used = 0;
bool pool32[tile_pool32];
bool pool64[tile_pool64];
int scroll_x[3];

inline volatile std::uint16_t* vram16(unsigned address) { return reinterpret_cast<volatile std::uint16_t*>(address); }

const StageHeader& header(int index) {
    return *reinterpret_cast<const StageHeader*>(stages_bin + STAGE_OFFSET[index]);
}

void load_column(Strip& s, int column) {
    const int slot = column & (ring - 1);
    const std::uint8_t* src;
    if (s.head->slot & 0x8000) {
        const int frames = 1 + ((s.head->slot >> 2) & 63);
        const int table_bytes = (frames * s.head->cols * 2 + 3) & ~3;
        const auto* table = reinterpret_cast<const std::uint16_t*>(s.columns);
        src = s.columns + table_bytes + table[s.animation_frame * s.head->cols + column] * column_stride;
    } else src = s.columns + column * column_stride;
    std::uint8_t* tiles = VRAM + s.charbase * 0x4000 + slot * rows * 32;
    dma_copy(src, tiles, rows * 32);
    volatile std::uint16_t* map = vram16(s.map);
    const std::uint16_t* entries = reinterpret_cast<const std::uint16_t*>(src + rows * 32);
    for (int r = 0; r < rows; ++r) map[r * 32 + slot] = std::uint16_t(entries[r] + slot * rows);
    s.loaded[slot] = std::int16_t(column);
}
}  // namespace

void init() {
    REG_DISPCNT = 0x0080;
    REG_BGCNT(0) = std::uint16_t(0 | (3 << 2) | (sb_text << 8));                       // text: priority 0, charblock 3
    REG_BGCNT(1) = std::uint16_t(2 | (2 << 2) | (sb_front << 8));
    REG_BGCNT(2) = std::uint16_t(2 | (1 << 2) | (sb_mid << 8));
    REG_BGCNT(3) = std::uint16_t(3 | (0 << 2) | (sb_far << 8));
    for (int i = 0; i < 4; ++i) { REG_BGHOFS(i) = 0; REG_BGVOFS(i) = 0; }
    strips[0].bg = 3; strips[0].charbase = 0; strips[0].map = screenblock(sb_far);
    strips[1].bg = 2; strips[1].charbase = 1; strips[1].map = screenblock(sb_mid);
    strips[2].bg = 1; strips[2].charbase = 2; strips[2].map = screenblock(sb_front);
    for (auto& s : strips) for (auto& l : s.loaded) l = -1;
    sprites_begin();
    sprites_end();
    oam_flush();
}

void set_blank(bool blank) {
    if (blank) REG_DISPCNT = REG_DISPCNT | 0x0080;
    else REG_DISPCNT = REG_DISPCNT & ~0x0080;
}

void load_ui() {
    // Tiles of the status panel and text: charblock 3.
    dma_copy(ui_bin + UI_TILES_OFF, VRAM + 3 * 0x4000, UI_TILE_BYTES);
    dma_copy(ui_bin + UI_PAL_OFF, BG_PALETTE + 12 * 16, 4 * 32);
    obj_ui_palette();
    dma_copy(ui_bin + UI_RINGPAL_OFF, OBJ_PALETTE + 11 * 16, 4 * 32);   // banks 11..14: neutral, red, blue, gold
    dma_copy(ui_bin + UI_RING_OFF, OBJ_VRAM + tile_ring * 32, 2048);
    dma_copy(ui_bin + UI_RING2_OFF, OBJ_VRAM + tile_ring2 * 32, 1024);
    // Shared sprite tiles.
    dma_copy(ui_bin + UI_SHADOW_OFF, OBJ_VRAM + tile_shadow * 32, 64);
    dma_copy(ui_bin + UI_SPARKPAL_OFF, OBJ_PALETTE + 9 * 16, 32);
    text_clear();
}

void obj_ui_palette() {
    dma_copy(ui_bin + UI_OBJPAL_OFF, OBJ_PALETTE + 15 * 16, 32);
}

void obj_palette(int bank, const std::uint16_t* colors) {
    dma_copy(colors, OBJ_PALETTE + bank * 16, 32);
}

// --------------------------------------------------------------------------------------------- stages

int stage_width(int index) { return header(index).width; }
const char* stage_name(int index) { return STAGE_NAME[index]; }
void stage_z(int index, int& z1, int& z2) { z1 = header(index).z1; z2 = header(index).z2; }

bool stage_has_front(int index) {
    const StageHeader& h = header(index);
    for (int i = 0; i < h.nstrips; ++i)
        if ((reinterpret_cast<const StripHeader*>(stages_bin + STAGE_OFFSET[index] + h.strip_off[i])->slot & 3) == 2) return true;
    return false;
}

void stage_clear() {
    strip_count = 0;
    stage_index = -1;
    stage_tick = 0;
    for (auto& s : strips) s.head = nullptr;
    for (int i = 1; i <= 3; ++i) { REG_BGHOFS(i) = 0; }
    BG_PALETTE[0] = 0x2C00;
    // Hide the strips by blanking their maps.
    for (auto& s : strips) {
        volatile std::uint16_t* map = vram16(s.map);
        for (int i = 0; i < 32 * 32; ++i) map[i] = blank_tile;
        for (auto& l : s.loaded) l = -1;
    }
    unsigned zero = 0;
    for (auto& s : strips) {
        // tile 480 of each charblock: transparent
        volatile std::uint32_t* t = reinterpret_cast<volatile std::uint32_t*>(VRAM + s.charbase * 0x4000 + blank_tile * 32);
        for (int i = 0; i < 8; ++i) t[i] = zero;
    }
}

void stage_load(int index, bool front) {
    stage_clear();
    stage_index = index;
    const StageHeader& h = header(index);
    BG_PALETTE[0] = h.backdrop;
    strip_count = h.nstrips;
    // Strips are stored far to near; the nearest strip always uses the front layer.
    for (int i = 0; i < strip_count; ++i) {
        const std::uint8_t* base = stages_bin + STAGE_OFFSET[index] + h.strip_off[i];
        const int slot = reinterpret_cast<const StripHeader*>(base)->slot & 3;
        if (slot == 2 && !front) continue;
        Strip& s = strips[slot];
        s.head = reinterpret_cast<const StripHeader*>(base);
        s.columns = base + sizeof(StripHeader);
        s.animation_frame = 0;
        for (int b = 0; b < s.head->banks; ++b) dma_copy(s.head->pal[b], BG_PALETTE + (s.head->bank0 + b) * 16, 32);
        for (auto& l : s.loaded) l = -1;
    }
}

// Word-wise clear from IWRAM (the library's memset works byte by byte from ROM).
IWRAM_CODE __attribute__((optimize("no-tree-loop-distribute-patterns"))) static void clear_words(void* p, unsigned bytes) {
    auto* w = static_cast<std::uint32_t*>(p);
    for (unsigned i = 0; i < bytes / 4; ++i) w[i] = 0;
}

// Equal columns, compared a word at a time when both are aligned.
IWRAM_CODE __attribute__((optimize("no-tree-loop-distribute-patterns"))) static bool same_bytes(const std::uint8_t* a, const std::uint8_t* b, unsigned bytes) {
    if (((reinterpret_cast<std::uintptr_t>(a) | reinterpret_cast<std::uintptr_t>(b)) & 3) == 0) {
        const auto* x = reinterpret_cast<const std::uint32_t*>(a);
        const auto* y = reinterpret_cast<const std::uint32_t*>(b);
        for (unsigned i = 0; i < bytes / 4; ++i) if (x[i] != y[i]) return false;
        return true;
    }
    if (((reinterpret_cast<std::uintptr_t>(a) | reinterpret_cast<std::uintptr_t>(b)) & 1) == 0) {
        const auto* x = reinterpret_cast<const std::uint16_t*>(a);
        const auto* y = reinterpret_cast<const std::uint16_t*>(b);
        for (unsigned i = 0; i < bytes / 2; ++i) if (x[i] != y[i]) return false;
        return true;
    }
    return std::memcmp(a, b, bytes) == 0;
}

void stage_scroll(int camera, int vertical_offset) {
    const unsigned tick = stage_tick++ / 2; // backgrounds advance at the 30 Hz simulation rate
    for (int i = 0; i < 3; ++i) {
        Strip& s = strips[i];
        if (!s.head || stage_index < 0) { REG_BGHOFS(s.bg) = 0; continue; }
        const int frames = 1 + ((s.head->slot >> 2) & 63);
        const unsigned duration = (s.head->slot >> 8) & 127;
        const int frame = frames > 1 ? int((tick / (duration ? duration : 1)) % frames) : 0;
        if (s.animation_frame != frame) {
            if (s.head->slot & 0x8000) {
                const auto* table = reinterpret_cast<const std::uint16_t*>(s.columns);
                for (auto& loaded : s.loaded)
                    if (loaded >= 0 && table[s.animation_frame * s.head->cols + loaded] !=
                                       table[frame * s.head->cols + loaded]) loaded = -1;
            } else {
                const auto* base = reinterpret_cast<const std::uint8_t*>(s.head) + sizeof(StripHeader);
                const auto* next_columns = base + frame * s.head->cols * column_stride;
                for (auto& loaded : s.loaded)
                    if (loaded >= 0 && !same_bytes(s.columns + loaded * column_stride,
                                                   next_columns + loaded * column_stride, column_stride)) loaded = -1;
                s.columns = next_columns;
            }
            s.animation_frame = frame;
        }
        // Pixels of strip scrolled: camera * speed * 0.3
        const int px = ((camera * s.head->speed) >> 8) * scale_num >> scale_shift;
        const int c0 = px >> 3;
        int c1 = (px + 239) >> 3;
        if (c1 >= s.head->cols) c1 = s.head->cols - 1;
        for (int c = c0; c <= c1; ++c)
            if (c >= 0 && s.loaded[c & (ring - 1)] != c) load_column(s, c);
        REG_BGHOFS(s.bg) = std::uint16_t(px & 255);
        REG_BGVOFS(s.bg) = std::uint16_t((256 - field_top - vertical_offset) & 255);
        scroll_x[i] = px;
    }
}

// ------------------------------------------------------------------------------------------------ text

void text_clear() {
    clear_words(text_map, sizeof text_map);
    REG_BGHOFS(0) = 0;
    REG_BGVOFS(0) = 0;
}

static inline std::uint16_t tile_entry(int tile, int bank) { return std::uint16_t(tile | ((12 + bank) << 12)); }

static int glyph_index(int c) {
    static std::int8_t table[128];
    static bool ready = false;
    if (!ready) {
        for (auto& t : table) t = 0;
        for (int i = 0; UI_CHARSET[i]; ++i) table[static_cast<unsigned char>(UI_CHARSET[i])] = std::int8_t(i);
        ready = true;
    }
    if (c >= 'a' && c <= 'z') c -= 32;
    return (c >= 0 && c < 128) ? table[c] : 0;
}

void text(int col, int row, const char* s, int bank, int variant) {
    for (int i = 0; s[i] && col + i < 32; ++i)
        text_map[row * 32 + col + i] = tile_entry(UI_FONT_BASE + variant * UI_GLYPHS + glyph_index(static_cast<unsigned char>(s[i])), bank);
}

void text_number(int col, int row, int value, int digits, int bank, int variant) {
    char buf[12];
    if (value < 0) value = 0;
    for (int i = digits - 1; i >= 0; --i) { buf[i] = char('0' + value % 10); value /= 10; }
    buf[digits] = 0;
    text(col, row, buf, bank, variant);
}

void fill(int col, int row, int w, int h, int tile, int bank) {
    for (int r = 0; r < h; ++r)
        for (int c = 0; c < w; ++c)
            if (col + c < 32 && row + r < 32) text_map[(row + r) * 32 + col + c] = tile_entry(tile, bank);
}

void panel(int col, int row, int w, int h) { fill(col, row, w, h, UI_PANEL, 0); }

void bar(int col, int row, int tiles, int value, int max, int bank) {
    int px = max > 0 ? value * tiles * 8 / max : 0;
    if (px < 0) px = 0;
    if (px > tiles * 8) px = tiles * 8;
    for (int i = 0; i < tiles; ++i) {
        int fillpx = px - i * 8;
        if (fillpx < 0) fillpx = 0;
        if (fillpx > 8) fillpx = 8;
        text_map[row * 32 + col + i] = tile_entry(UI_BAR_BASE + fillpx, bank);
    }
}

void hp_bar(int col, int row, int tiles, int hp, int dark, int max) {
    auto px = [&](int v) { int p = max > 0 ? v * tiles * 8 / max : 0; return p < 0 ? 0 : p > tiles * 8 ? tiles * 8 : p; };
    const int a = px(hp), b = px(dark) < a ? a : px(dark);
    for (int i = 0; i < tiles; ++i) {
        int fa = a - i * 8, fb = b - i * 8;
        fa = fa < 0 ? 0 : fa > 8 ? 8 : fa;
        fb = fb < 0 ? 0 : fb > 8 ? 8 : fb;
        text_map[row * 32 + col + i] = tile_entry(UI_HP_BASE + fb * (fb + 1) / 2 + fa, 2);
    }
}

void hud_shift(int x, int y) {
    REG_BGHOFS(0) = std::uint16_t(x & 511);
    REG_BGVOFS(0) = std::uint16_t(y & 511);
}

void stage_shutter(int height) {
    if (height <= 0) return;
    if (height > 13) height = 13;
    // BG0 character tiles 224..243 are free and precede the screen blocks.
    volatile std::uint16_t* tiles = vram16(0x0600C000 + 224 * 32);
    for (int row = 5; row < 20; ++row) {
        for (int y = 0; y < 8; ++y) {
            const int screen_y = row * 8 + y - 3;
            const std::uint16_t pixels = ((screen_y - 40 + 130) % 13 < height) ? 0x7777 : 0;
            const int offset = (row - 5) * 16 + y * 2;
            tiles[offset] = pixels;
            tiles[offset + 1] = pixels;
        }
        fill(0, row, 32, 1, 224 + row - 5, 0);
    }
}

void bg0_flush() {
    dma_copy(text_map, VRAM + sb_text * 0x800, sizeof text_map);
}

// ---------------------------------------------------------------------------------------------- sprites

int alloc_tiles(int size) {
    if (size <= 32) {
        for (int i = 0; i < tile_pool32; ++i) if (!pool32[i]) { pool32[i] = true; return i; }
        return -1;
    }
    for (int i = 0; i < tile_pool64; ++i) if (!pool64[i]) { pool64[i] = true; return 100 + i; }
    return -1;
}

void free_tiles(int handle, int size) {
    (void)size;
    if (handle < 0) return;
    if (handle >= 100) pool64[handle - 100] = false;
    else pool32[handle] = false;
}

std::uint16_t tile_index(int handle, int) {
    if (handle >= 100) return std::uint16_t(tile_pool64_base + (handle - 100) * 64);
    return std::uint16_t(tile_pool32_base + handle * 16);
}

void upload(int handle, int, const std::uint8_t* tiles, unsigned bytes) {
    upload_raw(tile_index(handle, 0), tiles, bytes);
}

void upload_raw(unsigned tile_number, const std::uint8_t* data, unsigned bytes) {
    dma_copy(data, OBJ_VRAM + tile_number * 32, bytes);
}

void sprites_begin() {
    oam_used = 0;
}

void reset_sprites() {
    for (auto& p : pool32) p = false;
    for (auto& p : pool64) p = false;
    oam_used = 0;
}

static int size_code(int size) { return size == 8 ? 0 : size == 16 ? 1 : size == 32 ? 2 : 3; }

bool sprite(int x, int y, int size, unsigned tile, int bank, int priority, bool hflip, bool wide) {
    if (oam_used >= oam_count) return false;
    if (x <= -size || x >= 240 || y <= -size || y >= 160) return true;     // off screen
    std::uint16_t* o = oam_shadow + oam_used * 4;
    o[0] = std::uint16_t((y & 0xFF) | ((wide ? 1 : 0) << 14));
    o[1] = std::uint16_t((x & 0x1FF) | ((wide ? (size == 64 ? 3 : 0) : size_code(size)) << 14) | (hflip ? 0x1000 : 0));
    o[2] = std::uint16_t(tile | (priority << 10) | (bank << 12));
    ++oam_used;
    return true;
}

bool sprite_scaled(int x, int y, int size, unsigned tile, int bank, int priority, int scale) {
    if (oam_used >= oam_count) return false;
    std::uint16_t* o = oam_shadow + oam_used * 4;
    o[0] = std::uint16_t((y & 0xFF) | 0x0300);                // affine, double size, rotation group 0
    o[1] = std::uint16_t((x & 0x1FF) | (size_code(size) << 14));
    o[2] = std::uint16_t(tile | (priority << 10) | (bank << 12));
    ++oam_used;
    // Group 0 takes its matrix from the fourth halfword of the first four sprites: PA = PD = 1 / scale.
    const int inverse = (256 * 256) / scale;
    oam_shadow[3] = std::uint16_t(inverse);
    oam_shadow[7] = 0;
    oam_shadow[11] = 0;
    oam_shadow[15] = std::uint16_t(inverse);
    return true;
}

void sprites_end() {
    for (int i = oam_used; i < oam_count; ++i) {
        std::uint16_t* o = oam_shadow + i * 4;
        o[0] = 0x0200;
        o[1] = o[2] = 0;
    }
}

void oam_flush() {
    // The affine parameters live in the fourth halfword of the first sprites; copy them with everything else.
    dma_copy(oam_shadow, const_cast<std::uint16_t*>(reinterpret_cast<volatile std::uint16_t*>(0x07000000)), oam_count * 8);
}

void fade(int level) {
    REG_BLDCNT = 0x00FF;
    REG_BLDY = std::uint16_t(level < 0 ? 0 : level > 16 ? 16 : level);
}

}  // namespace video

namespace video {
// The title logo on layer BG1 (the foreground strip's layer, so the stage must be loaded without it).
void logo_show() {
    const std::uint8_t* base = logo_bin;
    dma_copy(base, BG_PALETTE + 6 * 16, 32);
    dma_copy(base + 32, VRAM + 2 * 0x4000, LOGO_TILES * 32);
    const std::uint16_t* map = reinterpret_cast<const std::uint16_t*>(base + 32 + LOGO_TILES * 32);
    volatile std::uint16_t* dst = vram16(screenblock(sb_front));
    for (int i = 0; i < 32 * 32; ++i) dst[i] = 6 << 12;
    for (int r = 0; r < LOGO_ROWS; ++r)
        for (int c = 0; c < LOGO_COLS; ++c) dst[r * 32 + c] = map[r * LOGO_COLS + c];
    REG_BGHOFS(1) = 0;
    REG_BGVOFS(1) = 4;
}
}  // namespace video

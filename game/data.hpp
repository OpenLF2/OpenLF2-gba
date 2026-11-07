// SPDX-License-Identifier: MIT
// Copyright (c) 2026 OpenLF2 contributors

// Layout of the tables written by tools/convert_assets.py. All of it is read in place from the ROM.
#pragma once
#include <cstdint>

namespace lf2 {

#pragma pack(push, 2)
struct Picture {
    std::uint16_t tile_lo, tile_hi;
    std::int8_t ox, oy;
    std::uint8_t size, pad;
    bool has_picture() const { return !(tile_lo == 0xFFFF && tile_hi == 0xFFFF); }
    std::uint32_t tile() const { return std::uint32_t(tile_lo) | (std::uint32_t(tile_hi) << 16); }
};
struct Frame {
    std::uint16_t tile_lo, tile_hi;           // byte offset of the picture in sprites_bin; all ones: none
    std::int8_t ox, oy;                       // top left of the picture relative to the anchor, in screen pixels
    std::uint8_t size, sound;                 // picture size (8, 16, 32 or 64), sound effect number
    std::int16_t state, wait, next, dvx, dvy, dvz, cx, cy;
    std::int16_t hit_a, hit_d, hit_j, hit_Fa, hit_Ua, hit_Da, hit_Fj, hit_Uj, hit_Dj, hit_ja;
    std::int16_t mp;
    std::uint8_t nbdy, nitr;
    std::uint16_t bdy_off, itr_off;
    std::int16_t cp;                          // index into the object's catch points, or -1
    std::int16_t op_kind, op_x, op_y, op_dvx, op_dvy, op_oid, op_action, op_facing;
    std::int16_t wp_kind, wp_x, wp_y, wp_action, wp_attacking, wp_cover, wp_dvx, wp_dvy, wp_dvz;
    bool has_picture() const { return !(tile_lo == 0xFFFF && tile_hi == 0xFFFF); }
    std::uint32_t tile() const { return std::uint32_t(tile_lo) | (std::uint32_t(tile_hi) << 16); }
};
struct Itr { std::int16_t kind, x, y, w, h, dvx, dvy, fall, arest, vrest, bdefend, injury, effect, zwidth, catching, caught; };
struct Bdy { std::int16_t x, y, w, h, kind; };
struct Cpoint {
    std::int16_t kind, x, y, vaction, aaction, taction, injury, fronthurt, backhurt, hurtable, decrease, dircontrol,
        throwvx, throwvy, throwvz, throwinjury, cover, jaction, daction;
};
struct ObjHeader {
    std::int16_t id;
    std::uint8_t kind, slot_size;
    std::uint16_t nframes, pad;
    // walking_frame_rate, walking_speed, walking_speedz, running_frame_rate, running_speed, running_speedz,
    // heavy_walking_speed, heavy_walking_speedz, heavy_running_speed, heavy_running_speedz, jump_height,
    // jump_distance, jump_distancez, dash_height, dash_distance, dash_distancez, rowing_height, rowing_distance
    std::int16_t mv[18];
    std::uint32_t frames_off, bdy_off, itr_off, cp_off, strength_off, alt_off, knight_off;
    std::int16_t weapon_hp, weapon_drop_hurt, weapon_hit_sound, weapon_drop_sound, weapon_broken_sound;
};
#pragma pack(pop)

enum Move { walk_rate, walk_speed, walk_speedz, run_rate, run_speed, run_speedz, heavy_walk_speed,
            heavy_walk_speedz, heavy_run_speed, heavy_run_speedz, jump_height, jump_distance, jump_distancez,
            dash_height, dash_distance, dash_distancez, row_height, row_distance };

// An object (the fighter itself or something it spawns) with its tables resolved.
struct ObjDef {
    const ObjHeader* hdr;
    const Frame* frames;
    const Bdy* bdy;
    const Itr* itr;
    const Cpoint* cp;
    const Itr* strength;
    const Picture* alt = nullptr;
    const Picture* knight_alt = nullptr;
    const Frame& frame(int n) const { return (n >= 0 && n < hdr->nframes) ? frames[n] : frames[0]; }
    bool defined(int n) const { return n >= 0 && n < hdr->nframes && frames[n].wait >= 0; }
    int kind() const { return hdr->kind; }
    // movement value in 8.8 fixed point (rates are plain integers)
    int mv(int i) const { return hdr->mv[i]; }
};

// One entry of the character roster: palette, head icon and the objects of the fighter.
struct Group {
    const std::uint16_t* palette;             // 16 colours
    const std::uint8_t* head;                 // 16x16 4bpp, 128 bytes
    int count;
    ObjDef objs[32];
};

// Resolves roster entry `index` (0 .. ROSTER_COUNT-1).
const Group& roster_group(int index);
const Group& weapon_group();

}  // namespace lf2

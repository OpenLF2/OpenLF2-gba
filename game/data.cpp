// SPDX-License-Identifier: MIT
// Copyright (c) 2026 OpenLF2 contributors

#include "data.hpp"
#include "assets.h"

namespace lf2 {

namespace {
// Loaded lazily; every used field is assigned by load_group before it is read.
#if defined(__arm__)
__attribute__((section(".ewram")))
#endif
Group groups[ROSTER_COUNT];
bool loaded[ROSTER_COUNT];
Group weapons;
bool weapons_loaded;

std::uint32_t u32(const unsigned char* p) {
    return std::uint32_t(p[0]) | (std::uint32_t(p[1]) << 8) | (std::uint32_t(p[2]) << 16) | (std::uint32_t(p[3]) << 24);
}
}  // namespace

static void load_group(Group& g, const unsigned char* base) {
    g.palette = reinterpret_cast<const std::uint16_t*>(base);
    g.head = base + 32;
    g.count = int(u32(base + 160));
    if (g.count > 32) g.count = 32;
    for (int i = 0; i < g.count; ++i) {
        const unsigned char* obj = base + u32(base + 164 + 4 * i);
        ObjDef& d = g.objs[i];
        d.hdr = reinterpret_cast<const ObjHeader*>(obj);
        d.frames = reinterpret_cast<const Frame*>(obj + d.hdr->frames_off);
        d.bdy = reinterpret_cast<const Bdy*>(obj + d.hdr->bdy_off);
        d.itr = reinterpret_cast<const Itr*>(obj + d.hdr->itr_off);
        d.cp = reinterpret_cast<const Cpoint*>(obj + d.hdr->cp_off);
        d.strength = reinterpret_cast<const Itr*>(obj + d.hdr->strength_off);
        d.alt = d.hdr->alt_off ? reinterpret_cast<const Picture*>(obj + d.hdr->alt_off) : nullptr;
        d.knight_alt = d.hdr->knight_off ? reinterpret_cast<const Picture*>(obj + d.hdr->knight_off) : nullptr;
    }
}

const Group& roster_group(int index) {
    Group& g = groups[index];
    if (!loaded[index]) {
        load_group(g, data_bin + ROSTER_OFFSET[index]);
        loaded[index] = true;
    }
    return g;
}

const Group& weapon_group() {
    if (!weapons_loaded) {
        load_group(weapons, data_bin + WEAPON_OFFSET);
        weapons_loaded = true;
    }
    return weapons;
}

}  // namespace lf2

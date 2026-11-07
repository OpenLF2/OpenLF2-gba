#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Converts the Little Fighter 2 v2.0a installer into GBA data.

    convert_assets.py LF2_v2.0a.exe OUTDIR

Reads the installer directly (nothing is extracted to disk) and writes into OUTDIR:
    data.bin     fighter, object and frame tables (layouts in game/data.hpp)
    sprites.bin  4bpp sprite tiles, downscaled to 30% of the original size
    stages.bin   background strips
    sounds.bin   8-bit sound effects
    ui.bin       user interface tiles
    assets.h / assets.S   symbols and an assembler file that includes the binaries
"""
import os
import struct
import sys

import numpy as np
from PIL import Image

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from lf2_data import parse_bg, parse_object, pairs, num  # noqa: E402
from lf2_installer import Installer  # noqa: E402
from sprites import (SCALE, content_bbox, downscale, load_rgba, median_cut, nearest,  # noqa: E402
                     rgb555, to_tiles_4bpp)
import stages  # noqa: E402
import logo  # noqa: E402
import music  # noqa: E402
import sounds  # noqa: E402
import ui  # noqa: E402
import stage_rules  # noqa: E402

KEY = b"SiuHungIsAGoodBearBecauseHeIsVeryGood"

# (object id, display name) in character select order.
ROSTER = [
    (11, "DAVIS"), (9, "DENNIS"), (10, "WOODY"), (1, "DEEP"), (2, "JOHN"),
    (4, "HENRY"), (5, "RUDOLF"), (6, "LOUIS"), (7, "FIREN"), (8, "FREEZE"),
    (30, "BANDIT"), (31, "HUNTER"), (32, "MARK"), (33, "JACK"), (34, "SORCERER"),
    (35, "MONK"), (36, "JAN"), (37, "KNIGHT"), (39, "JUSTIN"), (38, "BAT"),
    (52, "JULIAN"), (51, "FIRZEN"), (50, "LOUIS EX"),
]

OUTLINE_COLOR = (28, 20, 20)
OUTLINE = 1              # pixels of dark outline drawn around every picture (palette entry 15)
FRAME_FMT = "<IbbBB" + "h" * 19 + "BBHHh" + "h" * 17   # see game/data.hpp
ITR_FMT = "<" + "h" * 16
BDY_FMT = "<hhhhh"
CP_FMT = "<" + "h" * 19
HDR_FMT = "<hBBHH" + "h" * 18 + "IIIIIIIhhhhh"
FRAME_SIZE = struct.calcsize(FRAME_FMT)

MOVE_KEYS = ["walking_frame_rate", "walking_speed", "walking_speedz", "running_frame_rate", "running_speed",
             "running_speedz", "heavy_walking_speed", "heavy_walking_speedz", "heavy_running_speed",
             "heavy_running_speedz", "jump_height", "jump_distance", "jump_distancez", "dash_height",
             "dash_distance", "dash_distancez", "rowing_height", "rowing_distance"]


def clamp16(v):
    return max(-32768, min(32767, int(v)))


def sound_number(path):
    path = str(path).replace("\\", "/").lower()
    digits = "".join(ch for ch in os.path.basename(path) if ch.isdigit())
    return int(digits) if digits and path.endswith(".wav") and int(digits) < 256 else 0


class Source:
    def __init__(self, path):
        self.inst = Installer(path)
        self.cache = {}

    def raw(self, name):
        return self.inst.read(name)

    def text(self, name):
        d = self.inst.read(name)
        if name.endswith(".dat"):
            d = bytes((d[k] + 256 - KEY[k % len(KEY)]) & 255 for k in range(123, len(d)))
        return d.decode("latin-1")

    def sheet(self, path):
        path = path.lower().replace("\\", "/")
        if path not in self.cache:
            self.cache[path] = load_rgba(self.inst.read(path))
        return self.cache[path]


def catalog(src):
    out = {}
    text = src.text("data/data.txt")
    inside = False
    for line in text.splitlines():
        line = line.strip()
        if line.startswith("<object>"):
            inside = True
        elif line.startswith("<object_end>"):
            inside = False
        elif inside and line.startswith("id:"):
            kv = dict(pairs(line.split("#")[0]))
            out[num(kv["id"])] = (num(kv["type"]), kv["file"].replace("\\", "/").lower())
    return out


class Obj:
    """A parsed object with its cell images downscaled and waiting for palette quantisation."""

    def __init__(self, src, oid, kind, path):
        self.id, self.kind, self.path = oid, kind, path
        self.data = parse_object(src.text(path))
        self.frames = self.data["frames"]
        self.deps = []
        self.cells = {}      # frame number -> dict(rgb, alpha, size, ox, oy)
        self.alt_cells = {}
        self.knight_cells = {}
        for n, fr in self.frames.items():
            for op in fr["opoint"]:
                if op.get("oid", 0) > 0:
                    self.deps.append(op["oid"])
        self.src = src

    def cell_for(self, pic):
        for sh in self.data["sheets"]:
            if sh["first"] <= pic <= sh["last"]:
                idx = pic - sh["first"]
                col, row = idx % sh["cols"], idx // sh["cols"]
                img = self.src.sheet(sh["path"])
                x0, y0 = col * (sh["w"] + 1), row * (sh["h"] + 1)
                if y0 + sh["h"] > img.shape[0] or x0 + sh["w"] > img.shape[1]:
                    return None
                return img[y0:y0 + sh["h"], x0:x0 + sh["w"]]
        return None

    def render(self):
        for n, fr in self.frames.items():
            self.render_cell(n, fr, 0, self.cells)
            if self.kind == 0:
                self.render_cell(n, fr, 140, self.alt_cells)
                if self.id == 37:
                    self.render_cell(n, fr, 114, self.knight_cells)

    def render_cell(self, n, fr, offset, destination):
        cell = self.cell_for(fr.get("pic", 0) + offset)
        if cell is None:
            return
        bb = content_bbox(cell)
        if bb is None:
            return
        cx, cy = fr.get("centerx", 0), fr.get("centery", 0)
        x0 = int(np.floor((bb[0] - cx) * SCALE)) - OUTLINE
        y0 = int(np.floor((bb[1] - cy) * SCALE)) - OUTLINE
        x1 = int(np.ceil((bb[2] - cx) * SCALE)) + OUTLINE
        y1 = int(np.ceil((bb[3] - cy) * SCALE)) + OUTLINE
        need = max(x1 - x0, y1 - y0, 1)
        size = next((s for s in (8, 16, 32, 64) if s >= need), 64)
        ax, ay = -x0, -y0
        # Keep the anchor inside the canvas for oversized content (it is clipped on the right/bottom).
        ax = min(ax, size - 1) if x1 - x0 > size else ax
        ay = min(ay, size - 1) if y1 - y0 > size else ay
        rgb, alpha = downscale(cell, (cx, cy), size, size, (ax, ay))
        destination[n] = dict(rgb=rgb, alpha=alpha, size=size, ox=-ax, oy=-ay)

def closure(cat, src, root_id, objs):
    """Loads root_id and everything its frames spawn."""
    if root_id in objs:
        return
    kind, path = cat[root_id]
    o = Obj(src, root_id, kind, path)
    objs[root_id] = o
    for dep in o.deps:
        if dep in cat:
            closure(cat, src, dep, objs)


def build_group(src, cat, root_id, tiles_out, tile_cache):
    """Returns the packed group: palette plus every object (root first) in the layout of game/data.hpp."""
    objs = {}
    closure(cat, src, root_id, objs)
    # Firzen's explosion creates Freeze's columns by engine rule, not by an opoint.
    if root_id == 51 and 212 in cat:
        closure(cat, src, 212, objs)
    order = [root_id] + sorted(k for k in objs if k != root_id)
    for oid in order:
        objs[oid].render()
    pts, wts = [], []
    for oid in order:
        weight = 3.0 if oid == root_id else 1.0
        for c in list(objs[oid].cells.values()) + list(objs[oid].alt_cells.values()) + list(objs[oid].knight_cells.values()):
            m = c["alpha"] > 0.5
            if m.any():
                pts.append(c["rgb"][m])
                wts.append(c["alpha"][m] * weight)
    if not pts or sum(len(p) for p in pts) < 200:
        return None   # the installer carries only placeholder pictures for this object
    palette = median_cut(np.concatenate(pts), np.concatenate(wts), 14)
    pal555 = [0] + [rgb555(p) for p in palette]
    pal555 += [0] * (15 - len(pal555))
    pal555.append(rgb555(OUTLINE_COLOR))
    index_of = {oid: i for i, oid in enumerate(order)}
    blobs = []
    for oid in order:
        o = objs[oid]
        blobs.append(pack_object(o, palette, index_of, tiles_out, tile_cache))
    return pal555, blobs, objs[root_id]


def build_weapon_group(src, cat, tiles_out, tile_cache):
    ids = [oid for oid, (kind, _) in sorted(cat.items()) if 99 < oid < 200 and kind in (1, 2, 4, 6)]
    objs = {}
    for oid in ids:
        closure(cat, src, oid, objs)
    for oid in (217, 218, 300, 998, 999):
        if oid in cat:
            closure(cat, src, oid, objs)
    order = sorted(objs)
    for oid in order:
        objs[oid].render()
    pts, wts = [], []
    for oid in order:
        for c in objs[oid].cells.values():
            mask = c["alpha"] > 0.5
            if mask.any():
                pts.append(c["rgb"][mask])
                wts.append(c["alpha"][mask])
    if not pts:
        raise ValueError("installer has no weapon pictures")
    palette = median_cut(np.concatenate(pts), np.concatenate(wts), 14)
    pal555 = [0] + [rgb555(p) for p in palette]
    pal555 += [0] * (15 - len(pal555))
    pal555.append(rgb555(OUTLINE_COLOR))
    index_of = {oid: i for i, oid in enumerate(order)}
    return pal555, [pack_object(objs[oid], palette, index_of, tiles_out, tile_cache) for oid in order]


def pack_object(o, palette, index_of, tiles_out, tile_cache):
    frames = o.frames
    nframes = (max(frames) + 1) if frames else 0
    bdys, itrs, cps = bytearray(), bytearray(), bytearray()
    nb = ni = ncp = 0
    fbytes = bytearray()
    altbytes = bytearray()
    knightbytes = bytearray()
    slot = 32
    for n in range(nframes):
        fr = frames.get(n)
        for cells, dest in ((o.alt_cells, altbytes), (o.knight_cells, knightbytes)):
            alt = cells.get(n)
            if alt is None:
                dest += struct.pack("<IbbBB", 0xFFFFFFFF, 0, 0, 0, 0)
                continue
            idx = nearest(palette, alt["rgb"]) + 1
            idx[alt["alpha"] <= 0.5] = 0
            if OUTLINE:
                solid = idx > 0
                grown = solid.copy()
                grown[1:, :] |= solid[:-1, :]
                grown[:-1, :] |= solid[1:, :]
                grown[:, 1:] |= solid[:, :-1]
                grown[:, :-1] |= solid[:, 1:]
                idx[grown & ~solid] = 15
            data = to_tiles_4bpp(idx)
            key = (data, alt["size"])
            if key not in tile_cache:
                tile_cache[key] = len(tiles_out)
                tiles_out += data
            dest += struct.pack("<IbbBB", tile_cache[key], alt["ox"], alt["oy"], alt["size"], 0)
        if fr is None:
            fbytes += struct.pack(FRAME_FMT, 0xFFFFFFFF, 0, 0, 0, 0, 0, -1, *([0] * 17), 0, 0, 0, 0, -1, *([0] * 17))
            continue
        cell = o.cells.get(n)
        tile, ox, oy, size = 0xFFFFFFFF, 0, 0, 0
        if cell is not None:
            idx = nearest(palette, cell["rgb"]) + 1
            idx[cell["alpha"] <= 0.5] = 0
            if OUTLINE:
                solid = idx > 0
                grown = solid.copy()
                grown[1:, :] |= solid[:-1, :]
                grown[:-1, :] |= solid[1:, :]
                grown[:, 1:] |= solid[:, :-1]
                grown[:, :-1] |= solid[:, 1:]
                idx[grown & ~solid] = 15
            data = to_tiles_4bpp(idx)
            key = (data, cell["size"])
            if key not in tile_cache:
                tile_cache[key] = len(tiles_out)
                tiles_out += data
            tile, ox, oy, size = tile_cache[key], cell["ox"], cell["oy"], cell["size"]
            slot = max(slot, size)
        snd = 0
        if "sound" in fr:
            snd = sound_number(fr["sound"])
        bdy_off = nb
        for b in fr["bdy"]:
            bdys += struct.pack(BDY_FMT, b.get("x", 0), b.get("y", 0), b.get("w", 0), b.get("h", 0), b.get("kind", 0))
            nb += 1
        itr_off = ni
        for it in fr["itr"]:
            cat_ = it.get("catchingact", 0)
            cgt = it.get("caughtact", 0)
            itrs += struct.pack(ITR_FMT, it.get("kind", 0), it.get("x", 0), it.get("y", 0), it.get("w", 0),
                                it.get("h", 0), it.get("dvx", 0), it.get("dvy", 0), it.get("fall", 0),
                                it.get("arest", 0), it.get("vrest", 0), it.get("bdefend", 0),
                                it.get("injury", 0), it.get("effect", 0), it.get("zwidth", 0), cat_, cgt)
            ni += 1
        cp = fr["cpoint"]
        cp_idx = -1
        if cp is not None:
            cp_idx = ncp
            cps += struct.pack(CP_FMT, *[clamp16(cp.get(k, 0)) for k in (
                "kind", "x", "y", "vaction", "aaction", "taction", "injury", "fronthurtact", "backhurtact",
                "hurtable", "decrease", "dircontrol", "throwvx", "throwvy", "throwvz", "throwinjury", "cover", "jaction", "daction")])
            ncp += 1
        op = fr["opoint"][0] if fr["opoint"] else None
        opv = [0] * 8
        if op:
            opv = [op.get("kind", 0), op.get("x", 0), op.get("y", 0), op.get("dvx", 0), op.get("dvy", 0),
                   index_of.get(op.get("oid", 0), -1), op.get("action", 0), op.get("facing", 0)]
        wp = fr["wpoint"] or {}
        wpv = [wp.get(k, 0) for k in ("kind", "x", "y", "weaponact", "attacking", "cover", "dvx", "dvy", "dvz")]
        g = lambda k: clamp16(fr.get(k, 0))  # noqa: E731
        fbytes += struct.pack(
            FRAME_FMT, tile, ox, oy, size, snd,
            g("state"), g("wait"), g("next"), g("dvx"), g("dvy"), g("dvz"), g("centerx"), g("centery"),
            g("hit_a"), g("hit_d"), g("hit_j"), g("hit_Fa"), g("hit_Ua"), g("hit_Da"), g("hit_Fj"),
            g("hit_Uj"), g("hit_Dj"), g("hit_ja"), g("mp"),
            min(len(fr["bdy"]), 255), min(len(fr["itr"]), 255), bdy_off, itr_off, cp_idx, *[clamp16(v) for v in opv + wpv])
    strength_off = len(itrs)
    for n in range(10):
        strength = o.data["strengths"].get(n, {})
        itrs += struct.pack(ITR_FMT, *[clamp16(strength.get(k, 0)) for k in (
            "kind", "x", "y", "w", "h", "dvx", "dvy", "fall", "arest", "vrest", "bdefend", "injury", "effect", "zwidth", "catchingact", "caughtact")])
    mv = o.data["move"]
    mvals = []
    for k in MOVE_KEYS:
        v = mv.get(k, 0.0)
        mvals.append(clamp16(v if "rate" in k else round(v * 256)))
    return dict(o=o, frames=bytes(fbytes), alt=bytes(altbytes) if o.alt_cells else b"", knight=bytes(knightbytes) if o.knight_cells else b"", bdy=bytes(bdys), itr=bytes(itrs), cp=bytes(cps), n=nframes,
                slot=slot, mv=mvals, strength_off=strength_off)


def head_tiles(src, o, palette):
    """16x16 head icon (from the character's small picture) in the group palette."""
    path = o.data["small"].lower() or ""
    if not path:
        return bytes(128)
    img = src.sheet(path)
    h, w, _ = img.shape
    side = min(w, h)
    # Face area: the picture is 40x45; use its upper square.
    cell = img[0:side, (w - side) // 2:(w - side) // 2 + side]
    rgb, alpha = downscale(cell, (0, 0), 16, 16, (0, 0), scale=16.0 / side)
    idx = nearest(palette, rgb) + 1
    idx[alpha <= 0.5] = 0
    return to_tiles_4bpp(idx)


def pack_group(pal, head, blobs):
    if len(blobs) > 32:
        raise ValueError("object group exceeds the runtime limit of 32")
    hsize = struct.calcsize(HDR_FMT)
    objbytes = []
    for b in blobs:
        o = b["o"]
        f_off = hsize
        bd_off = f_off + len(b["frames"])
        it_off = bd_off + len(b["bdy"])
        cp_off = it_off + len(b["itr"])
        alt_off = cp_off + len(b["cp"]) if b["alt"] else 0
        knight_off = cp_off + len(b["cp"]) + len(b["alt"]) if b["knight"] else 0
        blob = bytearray(struct.pack(HDR_FMT, o.id, o.kind, b["slot"], b["n"], 0, *b["mv"],
                                     f_off, bd_off, it_off, cp_off, it_off + b["strength_off"], alt_off, knight_off,
                                     clamp16(o.data["weapon"].get("weapon_hp", 0)),
                                     clamp16(o.data["weapon"].get("weapon_drop_hurt", 0)),
                                     *[sound_number(o.data["weapon"].get(k, "")) for k in
                                       ("weapon_hit_sound", "weapon_drop_sound", "weapon_broken_sound")]))
        blob += b["frames"] + b["bdy"] + b["itr"] + b["cp"] + b["alt"] + b["knight"]
        while len(blob) % 4:
            blob += b"\0"
        objbytes.append(bytes(blob))
    header_size = 32 + 128 + 4 + 4 * len(blobs)
    pos = header_size
    obj_off = []
    for blob in objbytes:
        obj_off.append(pos)
        pos += len(blob)
    grp = bytearray()
    grp += struct.pack("<16H", *pal)
    grp += head
    grp += struct.pack("<I", len(blobs))
    grp += b"".join(struct.pack("<I", x) for x in obj_off)
    assert len(grp) == header_size
    for blob in objbytes:
        grp += blob
    return grp


def main():
    if len(sys.argv) != 3:
        raise SystemExit(__doc__)
    convert(Source(sys.argv[1]), sys.argv[2])


def convert_fighters(src):
    """The fighter and weapon tables and the sprite tiles: data.bin, sprites.bin and what assets.h says about them."""
    cat = catalog(src)

    tiles = bytearray()
    tile_cache = {}
    data = bytearray()
    group_table = []
    names = []
    for oid, name in ROSTER:
        built = build_group(src, cat, oid, tiles, tile_cache)
        if built is None:
            print(f"{name}: no pictures in the installer, skipped")
            continue
        pal, blobs, root = built
        head = head_tiles(src, root, np.array([[0, 0, 0]] + [[((c & 31) << 3), (((c >> 5) & 31) << 3), (((c >> 10) & 31) << 3)] for c in pal[1:15]], dtype=np.float32)[1:])
        names.append(name)
        grp = pack_group(pal, head, blobs)
        while len(data) % 4:
            data += b"\0"
        group_table.append((len(data), len(grp)))
        data += grp
        print(f"{name:9s} {len(blobs)} objects, group {len(grp) / 1024:.0f} KiB, tiles so far {len(tiles) / 1024:.0f} KiB")

    weapon_pal, weapon_blobs = build_weapon_group(src, cat, tiles, tile_cache)
    while len(data) % 4:
        data += b"\0"
    weapon_offset = len(data)
    data += pack_group(weapon_pal, bytes(128), weapon_blobs)
    return {"data": bytes(data), "tiles": bytes(tiles), "group_table": group_table, "names": names,
            "weapon_offset": weapon_offset}


def convert_misc(src, music_wavs=None):
    """Sounds, interface, logo, music and the stage rules."""
    return {"snd": sounds.build(src), "ui": ui.build(src), "logo": logo.build(src),
            "mus": music.build(src, music_wavs),
            "rules": stage_rules.generate(stage_rules.parse(src.text("data/stage.dat")))}


def convert(src, out, music_wavs=None):
    """Writes everything main() describes into out. music_wavs ({track: WAV bytes}) replaces the ffmpeg run."""
    fighters = convert_fighters(src)
    st = stages.build(src)
    write_output(out, fighters, st, convert_misc(src, music_wavs))


def write_output(out, fighters, st, misc):
    """Writes the binaries and headers from the results of convert_fighters, stages.build and convert_misc."""
    os.makedirs(out, exist_ok=True)
    data, tiles = fighters["data"], fighters["tiles"]
    group_table, names, weapon_offset = fighters["group_table"], fighters["names"], fighters["weapon_offset"]
    snd, u, lg, mus = misc["snd"], misc["ui"], misc["logo"], misc["mus"]
    open(os.path.join(out, "data.bin"), "wb").write(data)
    open(os.path.join(out, "sprites.bin"), "wb").write(tiles)
    open(os.path.join(out, "stages.bin"), "wb").write(st["blob"])
    open(os.path.join(out, "sounds.bin"), "wb").write(snd["blob"])
    open(os.path.join(out, "ui.bin"), "wb").write(u["blob"])
    open(os.path.join(out, "logo.bin"), "wb").write(lg["blob"])
    open(os.path.join(out, "music.bin"), "wb").write(mus["blob"])
    with open(os.path.join(out, "stage_rules.hpp"), "w") as stage_file:
        stage_file.write(misc["rules"])

    roster = [(i, n) for i, n in ROSTER if n in names]
    with open(os.path.join(out, "assets.h"), "w") as h:
        h.write("// Generated by tools/convert_assets.py. Do not edit.\n#pragma once\n#include <cstdint>\n")
        h.write(f"constexpr int FRAME_BYTES = {FRAME_SIZE};\nconstexpr int HEADER_BYTES = {struct.calcsize(HDR_FMT)};\n")
        h.write(f"constexpr int ROSTER_COUNT = {len(roster)};\n")
        h.write(f"constexpr std::uint32_t WEAPON_OFFSET = {weapon_offset};\n")
        h.write("extern const unsigned char data_bin[], data_bin_end[], sprites_bin[], stages_bin[], sounds_bin[], ui_bin[], music_bin[], logo_bin[];\n")
        h.write("constexpr std::uint32_t ROSTER_OFFSET[] = {" + ",".join(str(o) for o, _ in group_table) + "};\n")
        h.write("constexpr const char* ROSTER_NAME[] = {" + ",".join(f'"{n}"' for n in names) + "};\n")
        h.write("constexpr int ROSTER_ID[] = {" + ",".join(str(i) for i, _ in roster) + "};\n")
        h.write(st["header"])
        h.write(snd["header"])
        h.write(u["header"])
        h.write(mus["header"])
        h.write(lg["header"])
    with open(os.path.join(out, "assets.S"), "w") as s:
        s.write('    .section .rodata\n    .balign 4\n')
        for sym, fn in (("data_bin", "data.bin"), ("sprites_bin", "sprites.bin"), ("stages_bin", "stages.bin"),
                        ("sounds_bin", "sounds.bin"), ("ui_bin", "ui.bin"), ("music_bin", "music.bin"), ("logo_bin", "logo.bin")):
            s.write(f'    .global {sym}\n{sym}:\n    .incbin "{os.path.join(out, fn)}"\n    .balign 4\n')
            if sym == "data_bin":
                s.write("    .global data_bin_end\ndata_bin_end:\n")
    total = len(data) + len(tiles) + len(st["blob"]) + len(snd["blob"]) + len(u["blob"]) + len(mus["blob"])
    print(f"data {len(data) / 1024:.0f} KiB, sprites {len(tiles) / 1024:.0f} KiB, stages {len(st['blob']) / 1024:.0f} KiB, "
          f"sounds {len(snd['blob']) / 1024:.0f} KiB, total {total / 1048576:.2f} MiB")


if __name__ == "__main__":
    main()

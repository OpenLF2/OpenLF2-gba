# SPDX-License-Identifier: MIT
"""Stage backgrounds: up to three horizontally scrolling strips per stage, in 4bpp tiles.

A strip is 15 tiles (120 pixels = 400 original pixels, from original y 128 down) tall and is stored one
tile column at a time, so the console can stream the column that scrolls into view into a 32-column
ring buffer. Every tile picks one of the strip's palette banks."""
import io
import math
import os
import struct

import numpy as np
from PIL import Image

from lf2_data import parse_bg
from pe_bitmap import bitmap
from sprites import SCALE, downscale, median_cut, nearest, open_bmp, rgb555, sqdist, weights

STAGES = [("sys/lf", "LION FOREST"), ("sys/hkc", "HK COLISEUM"), ("sys/sp", "STANLEY PRISON"),
          ("sys/gw", "THE GREAT WALL"), ("sys/bc", "BROKEBACK CLIF"), ("sys/cuhk", "CUHK"),
          ("sys/ft", "FORBIDDEN TOWER"), ("sys/qi", "QUEEN'S ISLAND"), ("sys/thv", "TAI HOM VILLAGE"),
          ("lee_on_road", "LEE ON ROAD")]
VIEW_TOP = 128          # original y of the first row shown on the console
ROWS = 15               # tiles per strip column
STRIP_BANKS = [3, 3, 6]  # palette banks per strip (far, middle, front)
BANK_BASE = [0, 3, 6]
COLUMN_STRIDE = 512
SCREEN = 794


def layer_image(src, path, transparent):
    im = open_bmp(src.raw(path))
    a = np.asarray(im, dtype=np.float32)
    alpha = np.ones(a.shape[:2], dtype=np.float32)
    if transparent:
        alpha[a.sum(axis=2) == 0] = 0.0
    return np.dstack([a, alpha])


def compose(src, bg, members, f_strip, stage_w):
    """Draws the layers of one strip at original resolution; returns an (400, W, 4) array."""
    nominal = int(SCREEN + (stage_w - SCREEN) * f_strip)
    width = nominal + 40          # the console shows 800 original pixels
    canvas = np.zeros((400, width, 4), dtype=np.float32)
    c0 = (stage_w - SCREEN) / 2.0
    for layer in members:
        f = layer["f"]
        if "color" in layer:
            c = layer["color"]
            r5, g6, b5 = (c >> 11) & 31, (c >> 5) & 63, c & 31
            rgb = [(r5 << 3) | (r5 >> 2), (g6 << 2) | (g6 >> 4), (b5 << 3) | (b5 >> 2)]
            x0 = int(round(layer.get("x", 0) + c0 * (f_strip - f)))
            y0 = layer.get("y", 0) - VIEW_TOP
            w, h = layer.get("width", SCREEN), layer.get("height", 0)
            xa, xb = max(0, x0), min(width, x0 + w)
            ya, yb = max(0, y0), min(400, y0 + h)
            if xb > xa and yb > ya:
                canvas[ya:yb, xa:xb, :3] = rgb
                canvas[ya:yb, xa:xb, 3] = 1.0
            continue
        img = layer_image(src, layer["file"], layer.get("transparency", 1) != 0)
        h, w, _ = img.shape
        x0 = int(round(layer.get("x", 0) + c0 * (f_strip - f)))
        y0 = layer.get("y", 0) - VIEW_TOP
        loop = layer.get("loop", 0)
        lwidth = layer.get("width", w)
        xs = [x0]
        if loop > 0 and w > 0:
            start = x0 - ((x0 + loop - 1) // loop) * loop if x0 > 0 else x0
            xs = list(range(start, x0 + max(lwidth, w), loop))
        for x in xs:
            sx0, sx1 = max(0, -x), min(w, width - x)
            sy0, sy1 = max(0, -y0), min(h, 400 - y0)
            if sx1 <= sx0 or sy1 <= sy0:
                continue
            src_px = img[sy0:sy1, sx0:sx1]
            dst = canvas[y0 + sy0:y0 + sy1, x + sx0:x + sx1]
            a = src_px[:, :, 3:4]
            dst[:, :, :3] = np.where(a > 0, src_px[:, :, :3], dst[:, :, :3])
            dst[:, :, 3:4] = np.maximum(dst[:, :, 3:4], a)
    canvas[:, nominal:, :] = canvas[:, nominal - 1:nominal, :]      # continue the last column to the right
    return canvas


def kmeans(points, k, iters=12):
    rng = np.random.default_rng(1)
    centers = points[rng.choice(len(points), size=min(k, len(points)), replace=False)].copy()
    for _ in range(iters):
        d = sqdist(points[:, None, :], centers[None, :, :])
        lab = d.argmin(axis=1)
        for i in range(len(centers)):
            m = lab == i
            if m.any():
                centers[i] = points[m].mean(axis=0)
    return lab


def quantize_strip(rgb, alpha, banks):
    """rgb (H, W, 3), alpha (H, W): returns palettes (banks, 16, 3), tile bank per tile, indices (H, W)."""
    H, W = alpha.shape
    th, tw = H // 8, W // 8
    tiles_rgb = rgb[:th * 8, :tw * 8].reshape(th, 8, tw, 8, 3).transpose(0, 2, 1, 3, 4).reshape(th * tw, 64, 3)
    tiles_a = (alpha[:th * 8, :tw * 8] > 0.5).reshape(th, 8, tw, 8).transpose(0, 2, 1, 3).reshape(th * tw, 64)
    cnt = np.maximum(tiles_a.sum(axis=1), 1)
    means = (tiles_rgb * tiles_a[:, :, None]).sum(axis=1) / cnt[:, None]
    has = tiles_a.any(axis=1)
    if not has.any():
        return np.zeros((banks, 16, 3), np.float32), np.zeros(th * tw, np.int32), np.zeros((th * 8, tw * 8), np.int32)
    k = min(banks, int(has.sum()))
    lab = np.zeros(th * tw, np.int32)
    lab[has] = kmeans(means[has], k)
    pals = np.zeros((banks, 15, 3), np.float32)
    for it in range(2):
        for b in range(banks):
            sel = (lab == b) & has
            if not sel.any():
                continue
            px = tiles_rgb[sel][tiles_a[sel]]
            if len(px) > 20000:
                px = px[::len(px) // 20000 + 1]
            pals[b, :] = 0
            p = median_cut(px, np.ones(len(px), np.float32), 15)
            pals[b, :len(p)] = p
            if len(p) < 15:
                pals[b, len(p):] = p[-1]
        # reassign every tile to the bank that reproduces it best
        best = np.zeros((th * tw, banks), np.float64)
        for b in range(banks):
            d = sqdist(tiles_rgb[:, :, None, :], pals[b][None, None, :, :]).min(axis=2)
            best[:, b] = (d * tiles_a).sum(axis=1)
        lab = best.argmin(axis=1).astype(np.int32)
    idx = np.zeros((th * tw, 64), np.int32)
    for b in range(banks):
        sel = lab == b
        if sel.any():
            d = sqdist(tiles_rgb[sel][:, :, None, :], pals[b][None, None, :, :])
            idx[sel] = d.argmin(axis=2) + 1
    idx[~tiles_a] = 0
    full = idx.reshape(th, tw, 8, 8).transpose(0, 2, 1, 3).reshape(th * 8, tw * 8)
    out = np.zeros((banks, 16, 3), np.float32)
    out[:, 1:] = pals
    return out, lab, full


def index_with_palettes(rgb, alpha, palettes):
    """Assign tiles to an existing set of banks, preserving one palette across animation frames."""
    height, width = alpha.shape
    th, tw = height // 8, width // 8
    pixels = rgb.reshape(th, 8, tw, 8, 3).transpose(0, 2, 1, 3, 4).reshape(th * tw, 64, 3)
    visible = (alpha > 0.5).reshape(th, 8, tw, 8).transpose(0, 2, 1, 3).reshape(th * tw, 64)
    errors = np.empty((th * tw, len(palettes)), np.float64)
    for bank in range(len(palettes)):
        distances = sqdist(pixels[:, :, None, :], palettes[bank, None, None, 1:, :])
        errors[:, bank] = (distances.min(axis=2) * visible).sum(axis=1)
    labels = errors.argmin(axis=1).astype(np.int32)
    indices = np.zeros((th * tw, 64), np.int32)
    for bank in range(len(palettes)):
        selected = labels == bank
        if selected.any():
            distances = sqdist(pixels[selected, :, None, :], palettes[bank, None, None, 1:, :])
            indices[selected] = distances.argmin(axis=2) + 1
    indices[~visible] = 0
    image = indices.reshape(th, tw, 8, 8).transpose(0, 2, 1, 3).reshape(height, width)
    return labels, image


def pack_tiles(full, th, tw):
    """Returns bytes of column-major tile data: for column c, rows 0..th-1, 32 bytes each."""
    out = bytearray()
    for c in range(tw):
        for r in range(th):
            t = full[r * 8:(r + 1) * 8, c * 8:(c + 1) * 8]
            for row in t:
                for x in range(0, 8, 2):
                    out.append(int(row[x]) | (int(row[x + 1]) << 4))
    return bytes(out)


def deduplicate_columns(data):
    """Return an index table and pool for frame-major 512-byte columns."""
    if len(data) % COLUMN_STRIDE:
        raise ValueError("incomplete stage column")
    unique = {}
    table, pool = bytearray(), bytearray()
    for start in range(0, len(data), COLUMN_STRIDE):
        column = bytes(data[start:start + COLUMN_STRIDE])
        if column not in unique:
            unique[column] = len(unique)
            pool += column
        table += struct.pack("<H", unique[column])
    table += bytes((-len(table)) % 4)
    return table + pool


def lee_on_road(src):
    """The special background is drawn by code in the original, not a bg.dat file."""
    exe = src.raw("lf2.exe")
    images = {f"pe/{name.lower()}": bitmap(exe, name) for name in ("BACK99_1", "BACK99_2", "BACK99_3")}
    fence = Image.new("RGB", (70, 40), (0, 0, 0))
    from PIL import ImageDraw
    draw = ImageDraw.Draw(fence)
    # The original uses one-pixel rails. Widen them before downscaling so they
    # remain visible at the GBA's 30% scale.
    draw.rectangle((0, 2, 2, 38), fill=(87, 127, 167))
    draw.rectangle((3, 2, 4, 38), fill=(47, 67, 87))
    draw.rectangle((0, 0, 69, 2), fill=(87, 127, 167))
    draw.rectangle((0, 3, 69, 4), fill=(47, 67, 87))
    draw.rectangle((0, 20, 69, 22), fill=(87, 127, 167))
    draw.rectangle((0, 23, 69, 24), fill=(47, 67, 87))
    output = io.BytesIO()
    fence.save(output, format="BMP")
    images["pe/road_fence"] = output.getvalue()

    class RoadSource:
        def raw(self, path):
            return images[path] if path in images else src.raw(path)

    def rect(y, h, rgb, width=SCREEN):
        r, g, b = rgb
        return {"file": "rect", "rect": ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3),
                "width": width, "height": h, "x": 0, "y": y}

    layers = [rect(128, 400, (0, 0, 0)),
              {"file": "pe/back99_2", "transparency": 0, "width": 810, "x": 250, "y": 128},
              {"file": "pe/back99_3", "transparency": 1, "width": 1918, "x": 30, "y": 175, "loop": 500},
              rect(326, 20, (63, 63, 63), 2400), rect(345, 156, (87, 87, 87), 2400),
              rect(471, 30, (63, 63, 63), 2400), rect(328, 2, (55, 55, 55), 2400),
              {"file": "pe/road_fence", "transparency": 1, "width": 2400, "x": 60, "y": 290, "loop": 70},
              {"file": "pe/back99_1", "transparency": 0, "width": 2400, "x": 10, "y": 390, "loop": 320}]
    return RoadSource(), {"width": 2400, "z": (350, 470), "layers": layers}


def build_stage(src, key, title):
    if key == "lee_on_road":
        src, bg = lee_on_road(src)
    else:
        bg = parse_bg(src.text(f"bg/{key}/bg.dat"))
    stage_w = max(bg["width"], SCREEN + 16)
    layers = []
    animated_cycles = {"sys/hkc": (16,), "sys/ft": (13,), "sys/sp": (6, 14, 16), "sys/qi": (12, 20)}.get(key, ())
    for lay in bg["layers"]:
        if "file" not in lay:
            continue
        lay = dict(lay)
        if lay.get("cc", 0) > 0 and lay.get("cc") not in animated_cycles and lay.get("c1", 0) != 0:
            continue          # blinking scenery (lights, fire): only the first frame of its cycle is drawn
        lay["file"] = lay["file"].lower()
        if "rect" in lay:
            # A filled rectangle (the ground under the scenery); the colour is RGB565.
            lay["color"] = lay["rect"]
        w = lay.get("width", 0) or SCREEN
        lay["f"] = min(1.0, max(0.0, (w - SCREEN) / float(stage_w - SCREEN)))
        layers.append(lay)
    # Split by scroll speed: static layers (sky, ground fill), the slow scenery and the fast foreground.
    groups = [[], [], []]
    for lay in layers:
        if key == "sys/sp":
            # The wall effects and fire have different periods. They share the
            # same scroll speed, so place them on separate hardware layers.
            slot = 0 if "color" in lay else 2 if "/fire" in lay["file"] else 1
        else:
            slot = 0 if lay["f"] < 0.02 else 1 if lay["f"] < 0.6 else 2
        groups[slot].append(lay)
    strips = []
    backdrop = (60, 90, 140)
    for slot, members in enumerate(groups):
        if not members:
            continue
        speed_layers = []
        animation_groups = set()
        for lay in members:
            group = (lay.get("cc", 0), lay.get("x", 0), lay.get("y", 0), lay.get("width", 0), lay.get("loop", 0))
            if lay.get("cc", 0) > 0:
                if group in animation_groups:
                    continue
                animation_groups.add(group)
            speed_layers.append(lay)
        f_strip = 0.0 if slot == 0 else sum(l["f"] for l in speed_layers) / len(speed_layers)
        cycles = {lay.get("cc", 0) for lay in members}
        active_cycles = cycles.intersection(animated_cycles)
        if active_cycles:
            period = math.lcm(*active_cycles)
            duration = 8 if active_cycles == {16} else 2 if active_cycles == {14} else 1
            phases = tuple(range(0, period, duration))
        else:
            duration, phases = 1, (0,)
        nominal_width = int(SCREEN + (stage_w - SCREEN) * f_strip) + 40
        out_w = int(np.ceil(nominal_width * SCALE / 8.0)) * 8
        out_h = ROWS * 8
        frames = []
        for phase in phases:
            visible = [lay for lay in members if lay.get("cc", 0) not in active_cycles or
                       lay.get("c1", 0) <= phase % lay["cc"] <= lay.get("c2", 0)]
            canvas = compose(src, bg, visible, f_strip, stage_w)
            frames.append(downscale(canvas, (0, 0), out_w, out_h, (0, 0)))
        samples = frames[::max(1, len(frames) // 12)]
        rgb = np.concatenate([frame[0] for frame in samples], axis=0)
        cov = np.concatenate([frame[1] for frame in samples], axis=0)
        banks = STRIP_BANKS[slot]
        pals, _, _ = quantize_strip(rgb, cov, banks)
        indexed = [index_with_palettes(rgb, alpha, pals) for rgb, alpha in frames]
        lab = np.concatenate([frame[0] for frame in indexed])
        idx = np.concatenate([frame[1] for frame in indexed], axis=0)
        if not strips:
            opaque = cov[0] > 0.5
            if opaque.any():
                backdrop = tuple(rgb[0][opaque].mean(axis=0))
        strips.append(dict(f=f_strip, cols=out_w // 8, pals=pals, lab=lab, idx=idx,
                           slot=slot, banks=banks, frames=len(phases), duration=duration))
    # Strip header + column data.
    blob = bytearray()
    sheads = []
    for st in strips:
        cols = st["cols"]
        colbytes = bytearray()
        for frame in range(st["frames"]):
            data = pack_tiles(st["idx"][frame * out_h:(frame + 1) * out_h], ROWS, cols)
            for c in range(cols):
                tiles = data[c * ROWS * 32:(c + 1) * ROWS * 32]
                m = bytearray()
                for r in range(ROWS):
                    tilelab = int(st["lab"][(frame * ROWS + r) * cols + c])
                    m += struct.pack("<H", r | ((BANK_BASE[st["slot"]] + tilelab) << 12))
                col = bytearray(tiles) + m
                col += bytes(COLUMN_STRIDE - len(col))
                colbytes += col
        pal = bytearray()
        for b in range(6):
            if b < st["banks"]:
                pal += struct.pack("<16H", *[0] + [rgb555(p) for p in st["pals"][b][1:]])
            else:
                pal += bytes(32)
        flags = st["slot"] | ((st["frames"] - 1) << 2) | (st["duration"] << 8)
        if st["frames"] > 1:
            packed_columns = deduplicate_columns(colbytes)
            if len(packed_columns) < len(colbytes):
                colbytes = packed_columns
                flags |= 0x8000
        head = struct.pack("<HHBBH", cols, int(round(st["f"] * 256)), BANK_BASE[st["slot"]], st["banks"], flags) + pal
        sheads.append((head, bytes(colbytes)))
    return dict(title=title, width=stage_w, z=bg["z"], backdrop=backdrop, strips=sheads)


def build_one(src, index):
    """One entry of STAGES, ready for assemble()."""
    key, title = STAGES[index]
    s = build_stage(src, key, title)
    n = sum(len(c) for _, c in s["strips"])
    print(f"stage {title:16s} width {s['width']} strips {len(s['strips'])} {n / 1024:.0f} KiB")
    return s


def build(src):
    return assemble([build_one(src, i) for i in range(len(STAGES))])


def assemble(stages):
    blob = bytearray()
    offsets = []
    hdr_fmt = "<HHHHBB16s3I"
    for s in stages:
        offsets.append(len(blob))
        hsize = struct.calcsize(hdr_fmt)
        strip_offs = []
        pos = hsize
        for head, cols in s["strips"]:
            strip_offs.append(pos)
            pos += len(head) + len(cols)
        strip_offs += [0] * (3 - len(strip_offs))
        z1, z2 = s["z"]
        name = s["title"].encode()[:15]
        blob += struct.pack(hdr_fmt, s["width"], z1, z2, rgb555(s["backdrop"]), len(s["strips"]), 0,
                            name.ljust(16, b"\0"), *strip_offs)
        for head, cols in s["strips"]:
            blob += head + cols
        while len(blob) % 4:
            blob += b"\0"
    h = f"constexpr int STAGE_COUNT = {len(stages)};\n"
    h += "constexpr std::uint32_t STAGE_OFFSET[] = {" + ",".join(map(str, offsets)) + "};\n"
    h += "constexpr const char* STAGE_NAME[] = {" + ",".join(f'"{s["title"]}"' for s in stages) + "};\n"
    return {"blob": bytes(blob), "header": h}

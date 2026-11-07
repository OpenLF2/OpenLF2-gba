# SPDX-License-Identifier: MIT
"""User interface tiles: a 5x7 pixel font, health/mana bar pieces, panel pieces, shadow and spark sprites."""
import struct

import numpy as np

from sprites import rgb555, to_tiles_4bpp, load_rgba, downscale, median_cut, nearest
from pe_bitmap import bitmap

GLYPHS = {
    "A": "01110 10001 10001 11111 10001 10001 10001", "B": "11110 10001 10001 11110 10001 10001 11110",
    "C": "01110 10001 10000 10000 10000 10001 01110", "D": "11110 10001 10001 10001 10001 10001 11110",
    "E": "11111 10000 10000 11110 10000 10000 11111", "F": "11111 10000 10000 11110 10000 10000 10000",
    "G": "01110 10001 10000 10111 10001 10001 01111", "H": "10001 10001 10001 11111 10001 10001 10001",
    "I": "01110 00100 00100 00100 00100 00100 01110", "J": "00111 00010 00010 00010 00010 10010 01100",
    "K": "10001 10010 10100 11000 10100 10010 10001", "L": "10000 10000 10000 10000 10000 10000 11111",
    "M": "10001 11011 10101 10101 10001 10001 10001", "N": "10001 11001 10101 10011 10001 10001 10001",
    "O": "01110 10001 10001 10001 10001 10001 01110", "P": "11110 10001 10001 11110 10000 10000 10000",
    "Q": "01110 10001 10001 10001 10101 10010 01101", "R": "11110 10001 10001 11110 10100 10010 10001",
    "S": "01111 10000 10000 01110 00001 00001 11110", "T": "11111 00100 00100 00100 00100 00100 00100",
    "U": "10001 10001 10001 10001 10001 10001 01110", "V": "10001 10001 10001 10001 10001 01010 00100",
    "W": "10001 10001 10001 10101 10101 11011 10001", "X": "10001 10001 01010 00100 01010 10001 10001",
    "Y": "10001 10001 01010 00100 00100 00100 00100", "Z": "11111 00001 00010 00100 01000 10000 11111",
    "0": "01110 10001 10011 10101 11001 10001 01110", "1": "00100 01100 00100 00100 00100 00100 01110",
    "2": "01110 10001 00001 00010 00100 01000 11111", "3": "11110 00001 00001 01110 00001 00001 11110",
    "4": "00010 00110 01010 10010 11111 00010 00010", "5": "11111 10000 11110 00001 00001 10001 01110",
    "6": "00110 01000 10000 11110 10001 10001 01110", "7": "11111 00001 00010 00100 01000 01000 01000",
    "8": "01110 10001 10001 01110 10001 10001 01110", "9": "01110 10001 10001 01111 00001 00010 01100",
    ":": "00000 00100 00000 00000 00000 00100 00000", "-": "00000 00000 00000 11111 00000 00000 00000",
    ".": "00000 00000 00000 00000 00000 00110 00110", "/": "00001 00010 00010 00100 01000 01000 10000",
    "!": "00100 00100 00100 00100 00100 00000 00100", "?": "01110 10001 00001 00010 00100 00000 00100",
    "+": "00000 00100 00100 11111 00100 00100 00000", "<": "00010 00100 01000 10000 01000 00100 00010",
    ">": "01000 00100 00010 00001 00010 00100 01000", "(": "00010 00100 01000 01000 01000 00100 00010",
    ")": "01000 00100 00010 00010 00010 00100 01000", "%": "11001 11010 00010 00100 01000 01011 10011",
    "*": "00000 10101 01110 11111 01110 10101 00000", ",": "00000 00000 00000 00000 00110 00100 01000",
    "'": "00100 00100 01000 00000 00000 00000 00000", "=": "00000 00000 11111 00000 11111 00000 00000",
    "_": "00000 00000 00000 00000 00000 00000 11111", " ": "00000 00000 00000 00000 00000 00000 00000",
}
CHARSET = " ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789:-./!?<>+()%*,'=_"
VARIANT_BACKGROUND = (0, 3, 4)    # transparent, panel navy, highlight blue


def glyph_tile(rows, background=0):
    t = np.full((8, 8), background, np.int32)
    bits = [[c == "1" for c in r] for r in rows.split()]
    for y, r in enumerate(bits):
        for x, v in enumerate(r):
            if v:
                t[y, x + 1] = 1
    # drop shadow
    for y in range(7):
        for x in range(8):
            if t[y, x] == 1 and x + 1 < 8 and y + 1 < 8 and t[y + 1, x + 1] == background:
                t[y + 1, x + 1] = 2
    return t


def tile_bytes(t):
    return to_tiles_4bpp(t)


def bar_tile(fill):
    """An 8 pixel wide bar piece: 1 = filled, 2 = empty, 3 = outline; rows 2..5 are the bar."""
    t = np.full((8, 8), 4, np.int32)           # 4 = panel colour above and below the bar
    t[1, :] = 3
    t[6, :] = 3
    t[7, :] = 6                                # faint line under the card
    for x in range(8):
        t[2:6, x] = 1 if x < fill else 2
    return t


def build(src):
    tiles = bytearray(32)                      # tile 0: empty
    for background in VARIANT_BACKGROUND:
        for ch in CHARSET:
            tiles += tile_bytes(glyph_tile(GLYPHS.get(ch, GLYPHS[" "]), background))
    bar_base = len(tiles) // 32
    for fill in range(9):
        tiles += tile_bytes(bar_tile(fill))
    hp_base = len(tiles) // 32
    for b in range(9):            # health bar pieces: a pixels of health, up to b pixels of recoverable health
        for a in range(b + 1):
            t = np.full((8, 8), 4, np.int32)
            t[0, :] = 6                # faint cell border above the bar
            t[7, :] = 3
            for x in range(8):
                t[1:7, x] = 1 if x < a else 5 if x < b else 2
            tiles += tile_bytes(t)
    panel = len(tiles) // 32
    solid = lambda idx: np.full((8, 8), idx, np.int32)  # noqa: E731
    grid = solid(3)                            # panel fill (navy) with a barely visible grid
    grid[0, :] = 6
    grid[:, 0] = 6
    tiles += tile_bytes(grid)
    edge = solid(3)
    edge[7, :] = 4
    tiles += tile_bytes(edge)                  # panel bottom edge
    plain = solid(3)
    tiles += tile_bytes(plain)                 # plain panel (status panel in a fight)
    vline = solid(3)
    vline[:, 4] = 6
    tiles += tile_bytes(vline)                 # faint vertical divider
    tiles += tile_bytes(solid(4))              # light fill
    tiles += tile_bytes(solid(5))              # highlight fill (select cursor)
    tiles += tile_bytes(solid(1))              # white
    def pal(*cols):
        p = [0] + [rgb555(c) for c in cols]
        return p + [0] * (16 - len(p))
    # Banks 12..15 of the background palette: text, gold text, health bar, mana bar.
    banks = [
        pal((255, 255, 255), (30, 30, 60), (24, 28, 72), (96, 112, 176), (176, 136, 32), (33, 40, 90)),
        pal((255, 214, 80), (60, 30, 10), (24, 28, 72), (96, 112, 176), (176, 136, 32), (33, 40, 90)),
        pal((232, 48, 40), (96, 24, 24), (16, 8, 8), (24, 28, 72), (240, 160, 64), (33, 40, 90)),
        pal((64, 120, 248), (24, 40, 96), (8, 8, 24), (24, 28, 72), (96, 112, 176)),
    ]
    palbytes = b"".join(struct.pack("<16H", *b) for b in banks)
    # Sprite tiles shared by every battle: shadow (16x8, 2 tiles) and four 16x16 hit sparks.
    shadow = np.zeros((8, 16), np.int32)
    for y in range(8):
        for x in range(16):
            if ((x - 7.5) / 8.0) ** 2 + ((y - 3.5) / 4.0) ** 2 <= 1.0 and (x + y) % 2 == 0:
                shadow[y, x] = 1
    sh = bytearray()
    for tx in range(2):
        sh += to_tiles_4bpp(np.pad(shadow[:, tx * 8:(tx + 1) * 8], ((0, 0), (0, 0))))
    sheet = load_rgba(bitmap(src.raw('lf2.exe'), 'SPARK'))
    pictures = []
    for row, width, height in ((0, 102, 80), (80, 61, 48), (128, 102, 80), (208, 61, 48)):
        for f in range(5):
            cell = sheet[row:row + height, f * width:(f + 1) * width]
            pictures.append(downscale(cell, (width / 2, height / 2), 32, 32, (16, 16)))
    pts, weights = [], []
    for rgb, alpha in pictures:
        mask = alpha > 0.5
        if mask.any():
            pts.append(rgb[mask]); weights.append(alpha[mask])
    spark_palette = median_cut(np.concatenate(pts), np.concatenate(weights), 15)
    spark_pal = [0] + [rgb555(color) for color in spark_palette]
    spark_pal += [0] * (16 - len(spark_pal))
    sp = bytearray()
    for rgb, alpha in pictures:
        pixels = nearest(spark_palette, rgb) + 1
        pixels[alpha <= 0.5] = 0
        sp += to_tiles_4bpp(pixels)
    def inside(x, y, w, h, r):
        if x < 0 or y < 0 or x >= w or y >= h:
            return False
        cx = r - 0.5 if x < r else (w - r - 0.5 if x >= w - r else None)
        cy = r - 0.5 if y < r else (h - r - 0.5 if y >= h - r else None)
        if cx is None or cy is None:
            return True
        return (x - cx) ** 2 + (y - cy) ** 2 <= r * r
    ring = np.zeros((64, 64), np.int32)       # soft rounded frame of a status cell: 54 x 38 pixels
    for y in range(64):
        for x in range(64):
            if inside(x, y, 54, 38, 4):
                ring[y, x] = 1 if not inside(x - 1, y - 1, 52, 36, 3) else 2 if not inside(x - 2, y - 2, 50, 34, 2) else 0
    ringbytes = to_tiles_4bpp(ring)
    small = np.zeros((32, 64), np.int32)      # frame of the cells in a two row panel: one soft line, 54 x 18 pixels
    for y in range(32):
        for x in range(64):
            if inside(x, y, 54, 18, 3) and not inside(x - 1, y - 1, 52, 16, 2):
                small[y, x] = 2
    sb = bytearray()
    for ty in range(4):                       # a 64 x 32 sprite: 8 x 4 tiles in row order
        for tx in range(8):
            blk = np.zeros((8, 8), np.int32)
            blk[:, :] = small[ty * 8:(ty + 1) * 8, tx * 8:(tx + 1) * 8]
            sb += to_tiles_4bpp(blk)
    smallbytes = bytes(sb)
    ring_pal = b"".join(struct.pack("<16H", 0, rgb555(o), rgb555(i), *([0] * 13)) for o, i in (
        ((14, 16, 44), (64, 78, 140)), ((110, 20, 20), (255, 96, 96)), ((20, 44, 120), (96, 150, 255)),
        ((96, 66, 16), (232, 184, 80))))
    obj_pal = [0, rgb555((0, 0, 0)), rgb555((255, 240, 160)), rgb555((255, 160, 40)), rgb555((255, 255, 255))] + [0] * 11
    blob = bytearray()
    parts = [("tiles", bytes(tiles)), ("pal", palbytes), ("shadow", bytes(sh)), ("spark", bytes(sp)), ("sparkpal", struct.pack('<16H', *spark_pal)),
             ("objpal", struct.pack("<16H", *obj_pal)), ("ring", ringbytes), ("ringpal", ring_pal), ("ring2", smallbytes)]
    offs = {}
    for name, data in parts:
        offs[name] = len(blob)
        blob += data
        while len(blob) % 4:
            blob += b"\0"
    h = (f"constexpr int UI_TILE_BYTES = {len(tiles)};\nconstexpr int UI_FONT_BASE = 1;\n"
         f"constexpr int UI_GLYPHS = {len(CHARSET)};\nconstexpr char UI_CHARSET[] = \"{CHARSET}\";\n"
         f"constexpr int UI_BAR_BASE = {bar_base};\nconstexpr int UI_HP_BASE = {hp_base};\nconstexpr int UI_PANEL = {panel};\n"
         f"constexpr int UI_EDGE = {panel + 1};\nconstexpr int UI_PLAIN = {panel + 2}, UI_VLINE = {panel + 3}, UI_LIGHT = {panel + 4};\n"
         f"constexpr int UI_HILITE = {panel + 5};\nconstexpr int UI_WHITE = {panel + 6};\n"
         f"constexpr std::uint32_t UI_TILES_OFF = {offs['tiles']}, UI_PAL_OFF = {offs['pal']}, "
         f"UI_SHADOW_OFF = {offs['shadow']}, UI_SPARK_OFF = {offs['spark']}, UI_SPARKPAL_OFF = {offs['sparkpal']}, UI_OBJPAL_OFF = {offs['objpal']}, UI_RING_OFF = {offs['ring']}, UI_RINGPAL_OFF = {offs['ringpal']}, UI_RING2_OFF = {offs['ring2']};\n")
    return {"blob": bytes(blob), "header": h}

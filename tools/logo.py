# SPDX-License-Identifier: MIT
"""Convert the original LF2 menu logo into transparent GBA background tiles."""
import struct

import numpy as np

from pe_bitmap import bitmap
from sprites import content_bbox, downscale, load_rgba, median_cut, nearest, rgb555, to_tiles_4bpp

COLS, ROWS = 30, 7
SCALE = 0.48


def build(src):
    sheet = load_rgba(bitmap(src.raw("lf2.exe"), "MENU_CLIP"))
    # Keep the English logo and remove the small subtitle and version below it.
    logo = sheet[41:121, :496].copy()
    logo[64:, 320:, 3] = 0
    bbox = content_bbox(logo)
    if bbox is None:
        raise ValueError("original menu logo contains no visible pixels")
    x0, y0, x1, y1 = bbox
    logo = logo[y0:y1, x0:x1]
    width, height = round((x1 - x0) * SCALE), round((y1 - y0) * SCALE)
    rgb, alpha = downscale(logo, (0, 0), width, height, (0, 0), SCALE)
    visible = alpha >= 0.3
    if not np.any(visible):
        raise ValueError("original menu logo contains no visible pixels")
    palette = median_cut(rgb[visible], alpha[visible], 14)
    canvas = np.zeros((ROWS * 8, COLS * 8), np.uint8)
    canvas[:52, :] = 1
    x = (COLS * 8 - width) // 2
    y = (ROWS * 8 - round(80 * SCALE)) // 2 + round(y0 * SCALE)
    area = canvas[y:y + height, x:x + width]
    area[visible] = nearest(palette, rgb)[visible] + 2

    colors = [0, rgb555((18, 37, 101))] + [rgb555(color) for color in palette]
    colors += [0] * (16 - len(colors))
    tiles = [bytes(32)]
    tile_indices = {tiles[0]: 0}
    entries = []
    for row in range(ROWS):
        for col in range(COLS):
            tile = to_tiles_4bpp(canvas[row * 8:(row + 1) * 8, col * 8:(col + 1) * 8])
            if tile not in tile_indices:
                tile_indices[tile] = len(tiles)
                tiles.append(tile)
            entries.append(tile_indices[tile] | (6 << 12))
    blob = struct.pack("<16H", *colors) + b"".join(tiles) + struct.pack("<%dH" % len(entries), *entries)
    header = f"constexpr int LOGO_COLS = {COLS}, LOGO_ROWS = {ROWS}, LOGO_TILES = {len(tiles)};\n"
    return {"blob": blob, "header": header}

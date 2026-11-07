# SPDX-License-Identifier: MIT
"""Sprite helpers: sheet loading, area-weighted downscaling and palette quantisation."""
import io
import numpy as np
from PIL import Image

SCALE = 0.3   # GBA pixels per original pixel: the 794-pixel wide battlefield becomes 238 pixels


def decode_rle8(data):
    """Tolerant RLE8 BMP decoder: pixels the stream never writes stay palette index 0, as in the original."""
    import struct
    offset, = struct.unpack_from("<I", data, 10)
    width, height, _, bits, comp, _, _, _, colors = struct.unpack_from("<iiHHIIiiI", data, 18)
    colors = colors or 256
    pal = [tuple(data[54 + 4 * i + 2 - c] for c in range(3)) for i in range(colors)]
    top_down = height < 0
    height = abs(height)
    px = np.zeros((height, width), dtype=np.uint8)
    x = y = 0
    p = offset
    while p + 1 < len(data) and y < height:
        n, v = data[p], data[p + 1]
        p += 2
        if n:
            px[y, x:x + n] = v if x < width else 0
            x += n
        elif v == 0:
            x, y = 0, y + 1
        elif v == 1:
            break
        elif v == 2:
            x += data[p]
            y += data[p + 1]
            p += 2
        else:
            px[y, x:x + v] = np.frombuffer(data[p:p + v], dtype=np.uint8)[:max(0, min(v, width - x))] if x < width else 0
            p += v + (v & 1)
            x += v
    if not top_down:
        px = px[::-1]
    lut = np.array(pal + [(0, 0, 0)] * (256 - len(pal)), dtype=np.uint8)
    return Image.fromarray(lut[px], "RGB")


def open_bmp(data):
    """Opens a BMP as RGB. Many of the original files are RLE8 bitmaps that Pillow decodes incompletely (the
    pictures come out mostly empty), so those use the tolerant decoder; files cut short read as black."""
    import struct
    comp, = struct.unpack_from("<I", data, 30)
    if comp == 1:
        return decode_rle8(data)
    try:
        return Image.open(io.BytesIO(data)).convert("RGB")
    except Exception:  # noqa: BLE001
        offset, = struct.unpack_from("<I", data, 10)
        width, height, _, bits = struct.unpack_from("<iiHH", data, 18)
        stride = ((width * bits + 31) // 32) * 4
        need = offset + stride * abs(height)
        return Image.open(io.BytesIO(data + bytes(max(0, need - len(data))))).convert("RGB")


def load_rgba(data, key_black=True):
    """Decodes a BMP into an (h, w, 4) float array; pure black (the colour key) is transparent."""
    im = open_bmp(data)
    a = np.asarray(im, dtype=np.float32)
    alpha = np.ones(a.shape[:2], dtype=np.float32)
    if key_black:
        alpha[(a.sum(axis=2) == 0)] = 0.0
    return np.dstack([a, alpha])


def weights(count, origin, scale, size):
    """Row i covers source interval [origin + i/scale, origin + (i+1)/scale); returns (count, size) weights."""
    w = np.zeros((count, size), dtype=np.float32)
    step = 1.0 / scale
    for i in range(count):
        a = origin + i * step
        b = a + step
        lo = max(0, int(np.floor(a)))
        hi = min(size, int(np.ceil(b)))
        for s in range(lo, hi):
            cover = min(b, s + 1) - max(a, s)
            if cover > 0:
                w[i, s] = cover
        t = w[i].sum()
        if t > 0:
            w[i] /= step if t > step else t
    return w


def matmul(a, b):
    """a @ b. The browser build swaps in a faster function with the same results (web_fast.py)."""
    return a @ b


def downscale(cell, anchor, out_w, out_h, anchor_out, scale=SCALE):
    """Resamples `cell` (h, w, 4) so that source point `anchor` lands on output position `anchor_out`.
    Returns premultiplied-average RGB (out_h, out_w, 3) and coverage alpha (out_h, out_w)."""
    h, w, _ = cell.shape
    ox = anchor[0] - anchor_out[0] / scale
    oy = anchor[1] - anchor_out[1] / scale
    wx = weights(out_w, ox, scale, w)
    wy = weights(out_h, oy, scale, h)
    a = cell[:, :, 3]
    pm = cell[:, :, :3] * a[:, :, None]
    cov = matmul(matmul(wy, a), wx.T)
    rgb = np.stack([matmul(matmul(wy, pm[:, :, c]), wx.T) for c in range(3)], axis=2)
    full = np.maximum(wy.sum(axis=1)[:, None] * wx.sum(axis=1)[None, :], 1e-6)
    rgb = rgb / np.maximum(cov, 1e-6)[:, :, None]
    return rgb, cov / full


def content_bbox(cell):
    ys, xs = np.nonzero(cell[:, :, 3] > 0)
    if len(xs) == 0:
        return None
    return xs.min(), ys.min(), xs.max() + 1, ys.max() + 1


def rgb555(c):
    r, g, b = (int(round(float(v))) for v in c)
    return ((min(255, r) >> 3) | ((min(255, g) >> 3) << 5) | ((min(255, b) >> 3) << 10))


def median_cut(points, weights_, k):
    """Weighted median cut. points (n, 3) float, weights (n,). Returns (k', 3) palette."""
    boxes = [(np.arange(len(points)))]
    while len(boxes) < k:
        best, best_score = None, 0
        for bi, idx in enumerate(boxes):
            if len(idx) < 2:
                continue
            p = points[idx]
            rng = p.max(axis=0) - p.min(axis=0)
            score = float(rng.max()) * float(weights_[idx].sum()) ** 0.5
            if score > best_score:
                best, best_score = bi, score
        if best is None:
            break
        idx = boxes.pop(best)
        p = points[idx]
        axis = int(np.argmax(p.max(axis=0) - p.min(axis=0)))
        order = idx[np.argsort(p[:, axis], kind="stable")]
        cum = np.cumsum(weights_[order])
        cut = int(np.searchsorted(cum, cum[-1] / 2.0)) + 1
        cut = min(max(cut, 1), len(order) - 1)
        boxes.append(order[:cut])
        boxes.append(order[cut:])
    pal = []
    for idx in boxes:
        wsum = weights_[idx].sum()
        pal.append((points[idx] * weights_[idx, None]).sum(axis=0) / max(wsum, 1e-6))
    return np.array(pal)


def sqdist(a, b):
    """Squared distance over the last axis (3 colour channels); the same sums as ((a - b) ** 2).sum(axis=-1), faster."""
    d = a - b
    return d[..., 0] * d[..., 0] + d[..., 1] * d[..., 1] + d[..., 2] * d[..., 2]


def nearest(palette, rgb):
    d = sqdist(rgb[:, :, None, :], palette[None, None, :, :])
    return d.argmin(axis=2)


def to_tiles_4bpp(idx):
    """idx: (n, n) palette indices 0..15 (0 transparent). Returns bytes in OBJ 1D tile order."""
    n = idx.shape[0]
    out = bytearray()
    for ty in range(n // 8):
        for tx in range(n // 8):
            t = idx[ty * 8:(ty + 1) * 8, tx * 8:(tx + 1) * 8]
            for row in t:
                for x in range(0, 8, 2):
                    out.append(int(row[x]) | (int(row[x + 1]) << 4))
    return bytes(out)

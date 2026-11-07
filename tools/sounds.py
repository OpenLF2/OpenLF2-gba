# SPDX-License-Identifier: MIT
"""Sound effects: 16-bit WAV files to 8-bit samples at the mixer's rate."""
import io
import wave

import numpy as np

MIX_RATE = 16777216 // 1254     # 13379 Hz: one 224-sample buffer per video frame
MENU = {"m_ok": 103, "m_cancel": 104, "m_join": 105, "m_end": 106, "m_pass": 107}
COUNT = 128


def decode(raw):
    w = wave.open(io.BytesIO(raw))
    n, ch, width, rate = w.getnframes(), w.getnchannels(), w.getsampwidth(), w.getframerate()
    d = w.readframes(n)
    if width == 2:
        a = np.frombuffer(d, dtype="<i2").astype(np.float32) / 32768.0
    else:
        a = (np.frombuffer(d, dtype=np.uint8).astype(np.float32) - 128.0) / 128.0
    if ch > 1:
        a = a.reshape(-1, ch).mean(axis=1)
    return a, rate


def resample(a, rate):
    n = int(len(a) * MIX_RATE / rate)
    if n < 2:
        return np.zeros(2, dtype=np.float32)
    # Averaging over the source span of each output sample avoids aliasing when the rate drops.
    edges = np.linspace(0, len(a), n + 1)
    cum = np.concatenate([[0.0], np.cumsum(a, dtype=np.float64)])
    lo = np.interp(edges[:-1], np.arange(len(cum)), cum)
    hi = np.interp(edges[1:], np.arange(len(cum)), cum)
    return ((hi - lo) / np.maximum(edges[1:] - edges[:-1], 1e-9)).astype(np.float32)


def build(src):
    blob = bytearray()
    offsets = [0] * COUNT
    lengths = [0] * COUNT
    files = {i: f"data/{i:03d}.wav" for i in range(1, 103)}
    files.update({v: f"data/{k}.wav" for k, v in MENU.items()})
    for i, name in sorted(files.items()):
        try:
            raw = src.raw(name)
        except KeyError:
            continue
        a, rate = decode(raw)
        a = resample(a, rate)
        peak = float(np.abs(a).max()) or 1.0
        a = a * min(1.0, 0.95 / peak) if peak > 0.95 else a
        q = np.clip(np.round(a * 127), -128, 127).astype(np.int8)
        offsets[i] = len(blob)
        lengths[i] = len(q)
        blob += q.tobytes()
        while len(blob) % 4:
            blob += b"\0"
    h = f"constexpr int SOUND_COUNT = {COUNT};\n"
    h += "constexpr std::uint32_t SOUND_OFFSET[] = {" + ",".join(map(str, offsets)) + "};\n"
    h += "constexpr std::uint32_t SOUND_LENGTH[] = {" + ",".join(map(str, lengths)) + "};\n"
    h += "constexpr std::uint32_t SOUND_RATE[SOUND_COUNT] = {" + ",".join([str(MIX_RATE)] * COUNT) + "};\n"
    h += "enum : int { SOUND_MENU_OK = 103, SOUND_MENU_CANCEL = 104, SOUND_MENU_JOIN = 105, SOUND_MENU_END = 106, SOUND_MENU_PASS = 107 };\n"
    return {"blob": bytes(blob), "header": h}

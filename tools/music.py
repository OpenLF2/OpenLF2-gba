# SPDX-License-Identifier: MIT
"""Background music: the original WMA tracks converted to 4-bit IMA ADPCM at the mixer's sample rate.

Needs ffmpeg. Without it the ROM is built without music."""
import os
import shutil
import struct
import subprocess
import tempfile

MIX_RATE = 16777216 // 1254
BLOCK = 256
TRACKS = ["main", "stage1", "stage2", "stage3", "stage4", "stage5", "boss1", "boss2"]


# The ffmpeg arguments after "-i <input>", shared with the browser build (web/worker.js runs them in ffmpeg.wasm).
ARGS = ["-ac", "1", "-ar", str(MIX_RATE), "-c:a", "adpcm_ima_wav", "-block_size", str(BLOCK), "-f", "wav"]


def convert(wma, out_wav):
    subprocess.run(["ffmpeg", "-v", "error", "-y", "-i", wma] + ARGS + [out_wav], check=True)
    return data_chunk(open(out_wav, "rb").read())


def data_chunk(d):
    i = 12
    while i + 8 <= len(d):
        cid, size = d[i:i + 4], struct.unpack_from("<I", d, i + 4)[0]
        if cid == b"data":
            data = d[i + 8:i + 8 + size]
            return data[:len(data) // BLOCK * BLOCK]
        i += 8 + size + (size & 1)
    raise ValueError("no data chunk")


def build(src, wavs=None):
    """wavs: optional {track name: WAV bytes} already produced by ffmpeg; otherwise ffmpeg is run here."""
    if wavs is None and not shutil.which("ffmpeg"):
        print("ffmpeg not found: building without music")
        return {"blob": bytes(4), "header": "constexpr int MUSIC_COUNT = 0;\nconstexpr std::uint32_t MUSIC_OFFSET[] = {0};\nconstexpr std::uint32_t MUSIC_BLOCKS[] = {0};\n"}
    blob = bytearray()
    offsets, blocks = [], []
    with tempfile.TemporaryDirectory() as tmp:
        for name in TRACKS:
            if wavs is not None:
                data = data_chunk(wavs[name])
            else:
                wma = os.path.join(tmp, name + ".wma")
                open(wma, "wb").write(src.raw(f"bgm/{name}.wma"))
                data = convert(wma, os.path.join(tmp, name + ".wav"))
            offsets.append(len(blob))
            blocks.append(len(data) // BLOCK)
            blob += data
            print(f"music {name}: {len(data) / 1024:.0f} KiB, {len(data) // BLOCK * 505 / MIX_RATE:.0f} s")
    h = f"constexpr int MUSIC_COUNT = {len(TRACKS)};\n"
    h += "constexpr std::uint32_t MUSIC_OFFSET[] = {" + ",".join(map(str, offsets)) + "};\n"
    h += "constexpr std::uint32_t MUSIC_BLOCKS[] = {" + ",".join(map(str, blocks)) + "};\n"
    h += "enum : int { MUSIC_MAIN = 0, MUSIC_STAGE1, MUSIC_STAGE2, MUSIC_STAGE3, MUSIC_STAGE4, MUSIC_STAGE5, MUSIC_BOSS1, MUSIC_BOSS2 };\n"
    return {"blob": bytes(blob), "header": h}

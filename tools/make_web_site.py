#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Assembles the static site that builds the ROM in the browser.

    make_web_site.py ROM ASSETDIR OUTDIR [--installer LF2_v2.0a.exe --modules DIR]

ROM is the finished build/openlf2.gba, ASSETDIR the build/assets it was made from. OUTDIR gets the page, the
converter's Python files, config.json, manifest.json and template.bin.gz: the ROM with the converted data
(sprites, stages, sounds, music, ...) and the boot logo zeroed out. The browser runs the same converter, checks every output file
against the SHA-256 in manifest.json and writes it back at its ROM offset, so the result is the same file as the
one the Makefile produces. The template holds only compiled code; no pictures, sounds or music.

The browser's numpy, Pillow and ffmpeg round some floating-point results differently from a native build (BLAS
and SIMD code paths). With --installer the browser's conversion is run here, under node (tools/web_convert.mjs; it
needs pyodide@0.27.7 and @ffmpeg/core@0.12.10 in the node_modules next to --modules), and every byte that differs
from the native result is listed in manifest.json as a fix-up. The final file is therefore identical, and the page
refuses to produce a ROM whose pieces do not match their SHA-256.
"""
import gzip
import hashlib
import json
import os
import shutil
import subprocess
import sys
import tempfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import web_tasks  # noqa: E402

BLOBS = ["data.bin", "sprites.bin", "stages.bin", "sounds.bin", "ui.bin", "music.bin", "logo.bin"]
HEADERS = ["assets.h", "stage_rules.hpp"]
HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)


def sha(b):
    return hashlib.sha256(b).hexdigest()


def browser_output(installer, modules):
    """The converter's files as the browser produces them."""
    with tempfile.TemporaryDirectory() as tmp:
        subprocess.run(["node", os.path.join(HERE, "web_convert.mjs"), installer, tmp, modules], check=True)
        return {n: open(os.path.join(tmp, n), "rb").read() for n in BLOBS + HEADERS}


def main():
    args = sys.argv[1:]
    opts = {}
    for flag in ("--installer", "--modules"):
        if flag in args:
            i = args.index(flag)
            opts[flag] = args[i + 1]
            del args[i:i + 2]
    if len(args) != 3:
        raise SystemExit(__doc__)
    rom_path, assets, out = args
    rom = bytearray(open(rom_path, "rb").read())
    os.makedirs(out, exist_ok=True)
    mine = browser_output(opts["--installer"], opts["--modules"]) if "--installer" in opts else None

    logo = bytes(rom[0x04:0xA0])
    rom[0x04:0xA0] = bytes(len(logo))
    manifest = {"logo": {"offset": 4, "sha256": sha(logo)}, "romSize": len(rom), "blobs": [], "headers": [], "python": [], "tasks": web_tasks.task_names(),
                "fixups": {}}
    for name in BLOBS:
        blob = open(os.path.join(assets, name), "rb").read()
        offset = rom.find(blob)
        if offset < 0 or rom.find(blob, offset + 1) >= 0:
            raise SystemExit(f"{name} is not in the ROM exactly once")
        rom[offset:offset + len(blob)] = bytes(len(blob))
        manifest["blobs"].append({"name": name, "offset": offset, "size": len(blob), "sha256": sha(blob)})
        if mine is not None and mine[name] != blob:
            if len(mine[name]) != len(blob):
                raise SystemExit(f"{name} has a different length in the browser")
            fix = []
            for i, (a, b) in enumerate(zip(mine[name], blob)):
                if a != b:
                    fix += [i, b]
            manifest["fixups"][name] = fix
            print(f"{name}: {len(fix) // 2} bytes differ between the browser and the native build")
    for name in HEADERS:
        if mine is not None and mine[name] != open(os.path.join(assets, name), "rb").read():
            raise SystemExit(f"{name} differs in the browser")
        manifest["headers"].append({"name": name, "sha256": sha(open(os.path.join(assets, name), "rb").read())})

    os.makedirs(os.path.join(out, "py"), exist_ok=True)
    for name in sorted(os.listdir(HERE)):
        if name.endswith(".py") and name != os.path.basename(__file__):
            shutil.copy(os.path.join(HERE, name), os.path.join(out, "py", name))
            manifest["python"].append(name)
    for name in ("index.html", "worker.js", "task-worker.js", "config.json"):
        shutil.copy(os.path.join(ROOT, "web", name), os.path.join(out, name))
    with gzip.GzipFile(os.path.join(out, "template.bin.gz"), "wb", compresslevel=9, mtime=0) as f:
        f.write(rom)
    with open(os.path.join(out, "manifest.json"), "w") as f:
        json.dump(manifest, f, separators=(",", ":"))
    print(f"template.bin.gz: {os.path.getsize(os.path.join(out, 'template.bin.gz')) / 1024:.0f} KiB")


if __name__ == "__main__":
    main()

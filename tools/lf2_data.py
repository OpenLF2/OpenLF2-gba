# SPDX-License-Identifier: MIT
"""Parser for the Little Fighter 2 object, background and stage text formats (already decoded)."""
import re

KEY = re.compile(r"([A-Za-z_]\w*)(\([^)]*\))?\s*:\s*")


def pairs(line):
    """Splits `a: 1 b: 2.5 file: x\\y.bmp` into [(key, value-string)] preserving order."""
    out = []
    matches = list(KEY.finditer(line))
    for i, m in enumerate(matches):
        end = matches[i + 1].start() if i + 1 < len(matches) else len(line)
        out.append((m.group(1) + (m.group(2) or ""), line[m.end():end].strip()))
    return out


def num(text, default=0):
    try:
        return int(text.split()[0])
    except (ValueError, IndexError):
        try:
            return int(float(text.split()[0]))
        except (ValueError, IndexError):
            return default


def flt(text, default=0.0):
    try:
        return float(text.split()[0])
    except (ValueError, IndexError):
        return default


def clean(line):
    return line.split("#", 1)[0].strip() if "#" in line and "<" not in line.split("#", 1)[0] else line.strip()


def parse_object(text):
    """Returns dict(name, sheets=[(path, w, h, cols, rows, first)], movement={...}, frames={n: frame})."""
    obj = {"name": "", "head": "", "small": "", "sheets": [], "move": {}, "frames": {}, "weapon": {}, "strengths": {}}
    lines = text.replace("\r", "").split("\n")
    section = None
    frame = None
    sub = None
    strength = None
    for raw in lines:
        line = raw.strip()
        if not line:
            continue
        if line.startswith("<bmp_begin>"):
            section = "bmp"
            continue
        if line.startswith("<bmp_end>"):
            section = None
            continue
        if line.startswith("<frame>"):
            parts = line.split(None, 2)
            frame = {"n": int(parts[1]), "name": parts[2] if len(parts) > 2 else "", "bdy": [], "itr": [],
                     "opoint": [], "cpoint": None, "wpoint": None, "bpoint": None}
            obj["frames"][frame["n"]] = frame
            section = "frame"
            sub = None
            continue
        if line.startswith("<frame_end>"):
            section = None
            frame = None
            continue
        if line.startswith("<weapon_strength_list>"):
            section = "wsl"
            continue
        if line.startswith("<weapon_strength_list_end>"):
            section = None
            continue
        if section == "bmp":
            mv = re.match(r"^([A-Za-z_]+)\s+(-?[\d.]+)\s*$", line)
            if mv:
                obj["move"][mv.group(1)] = float(mv.group(2))
                continue
            kv = pairs(line)
            if not kv:
                continue
            k0 = kv[0][0]
            if k0.startswith("file"):
                rng = re.search(r"\((\d+)-(\d+)\)", k0)
                d = dict(kv)
                path = kv[0][1].split()[0].replace("\\", "/")
                obj["sheets"].append({"path": path, "w": num(d.get("w", "0")), "h": num(d.get("h", "0")),
                                      "cols": num(d.get("row", "1")), "rows": num(d.get("col", "1")),
                                      "first": int(rng.group(1)) if rng else 0,
                                      "last": int(rng.group(2)) if rng else 0})
            elif k0 in ("name", "head", "small"):
                obj[k0] = kv[0][1].replace("\\", "/")
            else:
                for k, v in kv:
                    if k.startswith("weapon_"):
                        obj["weapon"][k] = v if k.endswith("_sound") else num(v)
            continue
        if section == "frame":
            if line.endswith("_end:") or line in ("bpoint_end:", "wpoint_end:", "opoint_end:", "bdy_end:", "itr_end:", "cpoint_end:"):
                sub = None
                continue
            m = re.match(r"(bpoint|wpoint|opoint|bdy|itr|cpoint):$", line)
            if m:
                sub = m.group(1)
                cur = {}
                if sub == "bdy":
                    frame["bdy"].append(cur)
                elif sub == "itr":
                    frame["itr"].append(cur)
                elif sub == "opoint":
                    frame["opoint"].append(cur)
                else:
                    frame[sub] = cur
                continue
            kv = pairs(line)
            if sub is None:
                for k, v in kv:
                    if k == "sound":
                        frame[k] = v
                    else:
                        frame[k] = num(v)
            else:
                for k, v in kv:
                    cur[k] = num(v)
            continue
        if section == "wsl":
            for k, v in pairs(line):
                if k == "entry":
                    strength = obj["strengths"].setdefault(num(v), {})
                elif strength is not None:
                    strength[k] = num(v)
            continue
        # weapon / top-level properties
        for k, v in pairs(line):
            obj["weapon"][k] = num(v)
    return obj


def parse_bg(text):
    bg = {"name": "", "width": 0, "z": (0, 0), "layers": [], "shadow": ""}
    cur = None
    for raw in text.replace("\r", "").split("\n"):
        line = raw.strip()
        if not line:
            continue
        if line.startswith("layer:"):
            cur = {}
            bg["layers"].append(cur)
            continue
        if line.startswith("layer_end"):
            cur = None
            continue
        if cur is not None and "file" not in cur and not line.startswith(("transparency", "rect", "loop", "x:", "width")) and ":" not in line.split()[0]:
            cur["file"] = line.replace("\\", "/")
            continue
        kv = pairs(line)
        if cur is not None:
            for k, v in kv:
                cur[k] = num(v)
        else:
            for k, v in kv:
                if k == "name":
                    bg["name"] = v
                elif k == "width":
                    bg["width"] = num(v)
                elif k == "zboundary":
                    p = v.split()
                    bg["z"] = (int(p[0]), int(p[1]))
                elif k == "shadow":
                    bg["shadow"] = v.replace("\\", "/")
    return bg

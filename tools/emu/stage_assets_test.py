# SPDX-License-Identifier: MIT
"""Check adjacent opaque stage layers retain their own pixels."""
import io
import os
import sys

from PIL import Image

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
import stages  # noqa: E402


class Source:
    def raw(self, name):
        color = (255, 0, 0) if name.endswith("left") else (0, 255, 0)
        image = Image.new("RGB", (4, 4), color)
        data = io.BytesIO()
        image.save(data, format="BMP")
        return data.getvalue()

    def text(self, path):
        if "/qi/" in path:
            return """name: test
width: 810
zboundary: 350 470
layer:
left
transparency: 0 width: 794 x: 0 y: 128 cc: 20 c1: 0 c2: 4
layer_end:
layer:
right
transparency: 0 width: 794 x: 8 y: 128 cc: 12 c1: 1 c2: 6
layer_end:
"""
        if "/sp/" in path:
            layers = ["layer:\nbg/sys/sp/fire_%s\ntransparency: 0 width: 794 x: 0 y: 128 cc: 14 c1: %d c2: %d\nlayer_end:" %
                      ("left" if frame % 2 == 0 else "right", frame * 2, frame * 2 + 1) for frame in range(7)]
            layers.append("layer:\nbg/sys/sp/wall_right\ntransparency: 0 width: 794 x: 12 y: 128 cc: 6 c1: 0 c2: 2\nlayer_end:")
            layers.append("layer:\nbg/sys/sp/wall_left\ntransparency: 0 width: 794 x: 12 y: 128 cc: 16 c1: 8 c2: 11\nlayer_end:")
            return "name: test\nwidth: 810\nzboundary: 350 470\n" + "\n".join(layers)
        if "/ft/" in path:
            layers = ["layer:\n%s\ntransparency: 0 width: 794 x: 0 y: 128 cc: 13 c1: %d c2: %d\nlayer_end:" %
                      ("left" if frame % 2 == 0 else "right", frame, frame) for frame in range(13)]
            return "name: test\nwidth: 810\nzboundary: 350 470\n" + "\n".join(layers)
        return """name: test
width: 810
zboundary: 350 470
layer:
left
transparency: 0 width: 794 x: 0 y: 128 cc: 16 c1: 0 c2: 7
layer_end:
layer:
right
transparency: 0 width: 794 x: 0 y: 128 cc: 16 c1: 8 c2: 15
layer_end:
"""


layers = [{"file": "left", "transparency": 0, "x": 0, "y": 128, "f": 0},
          {"file": "right", "transparency": 0, "x": 4, "y": 128, "f": 0}]
canvas = stages.compose(Source(), {}, layers, 0, 810)
assert tuple(canvas[0, 1, :3]) == (255, 0, 0)
assert tuple(canvas[0, 5, :3]) == (0, 255, 0)
assert canvas[0, 8, 3] == 0


def frame_bytes(header, data, frame):
    cols = int.from_bytes(header[:2], "little")
    flags = int.from_bytes(header[6:8], "little")
    frames = 1 + ((flags >> 2) & 63)
    if not flags & 0x8000:
        start = frame * cols * stages.COLUMN_STRIDE
        return data[start:start + cols * stages.COLUMN_STRIDE]
    table_bytes = (frames * cols * 2 + 3) & ~3
    out = bytearray()
    for col in range(cols):
        pos = (frame * cols + col) * 2
        index = int.from_bytes(data[pos:pos + 2], "little")
        start = table_bytes + index * stages.COLUMN_STRIDE
        out += data[start:start + stages.COLUMN_STRIDE]
    return bytes(out)


original = b"".join(bytes([value]) * stages.COLUMN_STRIDE for value in (1, 2, 1, 3, 2, 3))
packed = stages.deduplicate_columns(original)
synthetic_header = bytearray(8)
synthetic_header[:2] = (3).to_bytes(2, "little")
synthetic_header[6:8] = (0x8000 | (1 << 2) | (1 << 8)).to_bytes(2, "little")
assert frame_bytes(synthetic_header, packed, 0) + frame_bytes(synthetic_header, packed, 1) == original
assert len(packed) < len(original)

stage = stages.build_stage(Source(), "sys/hkc", "TEST")
assert len(stage["strips"]) == 1
header, columns = stage["strips"][0]
assert int.from_bytes(header[6:8], "little") & 4
assert frame_bytes(header, columns, 0) != frame_bytes(header, columns, 1)

stage = stages.build_stage(Source(), "sys/ft", "TEST")
header, columns = stage["strips"][0]
flags = int.from_bytes(header[6:8], "little")
assert 1 + ((flags >> 2) & 63) == 13 and (flags >> 8) & 127 == 1
assert flags & 0x8000
assert frame_bytes(header, columns, 0) != frame_bytes(header, columns, 1)

stage = stages.build_stage(Source(), "sys/sp", "TEST")
wall_header, wall_columns = stage["strips"][0]
wall_flags = int.from_bytes(wall_header[6:8], "little")
assert 1 + ((wall_flags >> 2) & 63) == 48 and (wall_flags >> 8) & 127 == 1
assert frame_bytes(wall_header, wall_columns, 0) != frame_bytes(wall_header, wall_columns, 3)
header, columns = stage["strips"][1]
flags = int.from_bytes(header[6:8], "little")
assert 1 + ((flags >> 2) & 63) == 7 and (flags >> 8) & 127 == 2
assert frame_bytes(header, columns, 0) != frame_bytes(header, columns, 1)

stage = stages.build_stage(Source(), "sys/qi", "TEST")
header, columns = stage["strips"][0]
flags = int.from_bytes(header[6:8], "little")
assert 1 + ((flags >> 2) & 63) == 60 and (flags >> 8) & 127 == 1
assert flags & 0x8000
assert frame_bytes(header, columns, 0) != frame_bytes(header, columns, 1)

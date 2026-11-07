#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Reads files out of the Little Fighter 2 v2.0a installer (LF2_v2.0a.exe) without extracting it.

Only the 29,586,263-byte installer is supported. The layout follows OpenLF2's installer.cpp.
"""
import bz2
import hashlib
import struct
import zlib

SHA256 = "540c485547e234b0d09595f54d067aafd7176092c308aca61868f3cbc03030e1"

STORED, ZLIB, BZIP2 = 0, 1, 2


def _decode(method, data):
    if method == STORED:
        return bytes(data)
    if method == ZLIB:
        return zlib.decompress(data)
    if method == BZIP2:
        return bz2.decompress(data)
    raise ValueError("unsupported compression %d" % method)


def _normalize(name):
    return name.replace("\\", "/").lstrip("/").lower()


class Installer:
    def __init__(self, path):
        with open(path, "rb") as f:
            self.b = f.read()
        b = self.b
        if hashlib.sha256(b).hexdigest() != SHA256:
            raise ValueError("%s is not the Little Fighter 2 v2.0a installer (LF2_v2.0a.exe, 29,586,263 bytes) "
                             "this converter was written for" % path)
        if b[:2] != b"MZ":
            raise ValueError("not a PE file")
        pe = struct.unpack_from("<I", b, 0x3C)[0]
        nsec = struct.unpack_from("<H", b, pe + 6)[0]
        optsize = struct.unpack_from("<H", b, pe + 20)[0]
        sec = pe + 24 + optsize
        overlay = 0
        for i in range(nsec):
            s = sec + i * 40
            size, off = struct.unpack_from("<II", b, s + 16)
            overlay = max(overlay, off + size)
        if b[overlay:overlay + 6] != b"wwgT)H":
            raise ValueError("unsupported installer (expected the 29,586,263-byte LF2_v2.0a.exe)")
        cur = overlay + 6
        table = None
        payload = None
        while cur < len(b):
            rid, flags, size = struct.unpack_from("<HHI", b, cur)
            body = cur + 8
            if rid == 0x7F7F:
                payload = body + 4
                break
            if rid == 0x143A:
                dsize = struct.unpack_from("<I", b, body)[0]
                method = b[body + 4]
                table = _decode(method, b[body + 5:body + size])
                assert len(table) == dsize
            cur = body + size
        if payload is None or table is None:
            raise ValueError("missing payload or file table")
        self.entries = {}
        count = struct.unpack_from("<I", table, 0)[0]
        cur = 4
        for index in range(count):
            size, kind = struct.unpack_from("<HH", table, cur)
            if kind == 0:
                rec = table[cur:cur + size]
                name = rec[62:].split(b"\0")[0].decode("latin-1")
                off, comp = struct.unpack_from("<II", rec, 6)
                dec = struct.unpack_from("<I", rec, 18)[0]
                self.entries[_normalize(name)] = (payload + off, comp, dec)
            cur += size

    def names(self):
        return sorted(self.entries)

    def read(self, name):
        off, comp, dec = self.entries[_normalize(name)]
        method = self.b[off]
        data = _decode(method, self.b[off + 1:off + comp])
        assert len(data) == dec, (name, len(data), dec)
        return data


if __name__ == "__main__":
    import sys
    inst = Installer(sys.argv[1])
    for n in inst.names():
        print(n, inst.entries[n][2])

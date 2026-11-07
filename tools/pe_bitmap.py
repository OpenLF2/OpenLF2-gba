# SPDX-License-Identifier: MIT
"""Read a named bitmap from the PE32 executable inside the LF2 installer."""
import struct


def bitmap(executable, name):
    data = executable
    def u16(offset):
        return struct.unpack_from('<H', data, offset)[0]
    def u32(offset):
        return struct.unpack_from('<I', data, offset)[0]
    pe = u32(0x3c)
    if data[:2] != b'MZ' or data[pe:pe + 4] != b'PE\0\0' or u16(pe + 24) != 0x10b:
        raise ValueError('expected a PE32 executable')
    sections = pe + 24 + u16(pe + 20)
    def file_offset(rva):
        for index in range(u16(pe + 6)):
            section = sections + 40 * index
            start, size, raw = u32(section + 12), u32(section + 16), u32(section + 20)
            if start <= rva < start + size:
                return raw + rva - start
        raise ValueError('resource RVA outside PE sections')
    root = file_offset(u32(pe + 24 + 112))
    def child(directory, key):
        base = root + directory
        count = u16(base + 12) + u16(base + 14)
        for index in range(count):
            entry = base + 16 + index * 8
            found, target = u32(entry), u32(entry + 4)
            if found & 0x80000000:
                text = root + (found & 0x7fffffff)
                found = data[text + 2:text + 2 + 2 * u16(text)].decode('utf-16le')
            if found == key:
                return target & 0x7fffffff
        raise ValueError('PE bitmap resource not found: ' + str(key))
    kind = child(0, 2)
    resource = child(kind, name.upper())
    language = root + resource
    if u16(language + 12) + u16(language + 14) != 1:
        raise ValueError('ambiguous PE bitmap language')
    entry = root + u32(language + 20)
    start, size = file_offset(u32(entry)), u32(entry + 4)
    dib = data[start:start + size]
    if len(dib) != size:
        raise ValueError('truncated PE bitmap')
    header_size, bits = struct.unpack_from('<I10xH', dib)
    colors = struct.unpack_from('<I', dib, 32)[0] or (1 << bits if bits <= 8 else 0)
    pixel_offset = 14 + header_size + 4 * colors
    return struct.pack('<2sIHHI', b'BM', len(dib) + 14, 0, 0, pixel_offset) + dib

#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright 2026 WineExplorer contributors
"""Independent oracle for the wexprefetchd flat image (layout 1, see protocol.h).

usage: verify_flat.py SOURCE.dll FLAT.img

Rebuilds the expected flat image from the PE headers exactly as protocol.h specifies and compares it with the
file the daemon produced, byte for byte (holes must read as zero).  Exit 0 only on an exact match.
"""
import struct
import sys

PAGE = 0x1000
SECTOR = 0x1ff
SCN_WRITE = 0x80000000
SCN_SHARED = 0x10000000


def round_up(value, align):
    return (value + align - 1) // align * align


def expected_sections(data):
    """Yield (index, name, virtual_address, file_start, file_size) for every section the daemon must store."""
    e_lfanew = struct.unpack_from('<I', data, 0x3c)[0]
    assert data[e_lfanew:e_lfanew + 4] == b'PE\0\0', 'not a PE'
    num_sections, = struct.unpack_from('<H', data, e_lfanew + 6)
    opt_size, = struct.unpack_from('<H', data, e_lfanew + 20)
    opt = e_lfanew + 24
    magic, = struct.unpack_from('<H', data, opt)
    assert magic in (0x10b, 0x20b)
    section_alignment, = struct.unpack_from('<I', data, opt + 32)
    size_of_image, = struct.unpack_from('<I', data, opt + 56)
    align = max(section_alignment, PAGE)
    table = opt + opt_size
    for i in range(num_sections):
        off = table + i * 40
        name = data[off:off + 8].rstrip(b'\0').decode('ascii', 'replace')
        vsize, va, raw_size, raw_ptr = struct.unpack_from('<IIII', data, off + 8)
        chars, = struct.unpack_from('<I', data, off + 36)
        if (chars & SCN_SHARED) and (chars & SCN_WRITE):
            continue
        if raw_ptr == 0 or raw_size == 0:
            continue
        map_size = round_up(vsize if vsize else raw_size, align)
        file_start = raw_ptr & ~SECTOR
        file_size = min(round_up(raw_ptr + raw_size, 0x200) - file_start, map_size)
        file_size = min(file_size, max(0, len(data) - file_start))
        yield i, name, va, file_start, file_size
    return size_of_image


def main():
    return main_args(sys.argv[1], sys.argv[2])


def main_args(source_path, flat_path):
    src = open(source_path, 'rb').read()
    flat = open(flat_path, 'rb').read()
    e_lfanew = struct.unpack_from('<I', src, 0x3c)[0]
    size_of_image, = struct.unpack_from('<I', src, e_lfanew + 24 + 56)
    image_size = round_up(size_of_image, PAGE)
    ok = True
    if len(flat) != image_size:
        print('FAIL image size %d != expected %d' % (len(flat), image_size))
        ok = False
    expected = bytearray(max(len(flat), image_size))
    for i, name, va, file_start, file_size in expected_sections(src):
        expected[va:va + file_size] = src[file_start:file_start + file_size]
        print('section %2d %-8s va=%08x file_start=%08x size=%08x' % (i, name, va, file_start, file_size))
    if bytes(expected[:len(flat)]) != flat:
        for off in range(0, len(flat), PAGE):
            if expected[off:off + PAGE] != flat[off:off + PAGE]:
                print('FAIL first differing page at offset %#x' % off)
                break
        ok = False
    print('MATCH' if ok else 'MISMATCH')
    return 0 if ok else 1


if __name__ == '__main__':
    sys.exit(main())

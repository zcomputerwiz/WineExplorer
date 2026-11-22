#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright 2026 WineExplorer contributors
"""Variants of a signed package that each break it in one way. The zip is rewritten record by record, byte for byte: only
the members named below change, so the layout stays the one a signing tool made (a general zip writer would reorder and
re-compress, and the package would no longer be recognised as one).

usage: tamper.py SIGNED.appx OUTDIR"""
import os
import struct
import sys
import zlib


def parse(data):
    eocd = data.rindex(b'PK\x05\x06')
    count, cd_size, cd_offset = struct.unpack_from('<HII', data, eocd + 10)
    zip64 = data.rfind(b'PK\x06\x07', 0, eocd)
    if zip64 >= 0:   # a signing tool's archive: the sizes are in the zip64 end record
        record = struct.unpack_from('<Q', data, zip64 + 8)[0]
        count, cd_size, cd_offset = struct.unpack_from('<QQQ', data, record + 32)
    entries, pos = [], cd_offset
    for _ in range(count):
        assert data[pos:pos + 4] == b'PK\x01\x02'
        (flags, method, _t, _d, crc, csize, size, nlen, elen, clen) = struct.unpack_from('<HHHHIIIHHH', data, pos + 8)
        offset = struct.unpack_from('<I', data, pos + 42)[0]
        name = data[pos + 46:pos + 46 + nlen].decode('utf-8')
        entries.append({'name': name, 'flags': flags, 'method': method, 'crc': crc, 'csize': csize, 'size': size,
                        'offset': offset, 'cd': bytearray(data[pos:pos + 46 + nlen + elen + clen])})
        pos += 46 + nlen + elen + clen
    order = sorted(range(count), key=lambda i: entries[i]['offset'])
    ends = {order[k]: (entries[order[k + 1]]['offset'] if k + 1 < count else cd_offset) for k in range(count)}
    for i, e in enumerate(entries):
        e['raw'] = bytearray(data[e['offset']:ends[i]])
        nlen, elen = struct.unpack_from('<HH', e['raw'], 26)
        e['data_at'] = 30 + nlen + elen
    return entries, zip64 >= 0, data[eocd + 22:eocd + 22 + struct.unpack_from('<H', data, eocd + 20)[0]]


def content(e):
    raw = bytes(e['raw'][e['data_at']:e['data_at'] + e['csize']])
    return raw if e['method'] == 0 else zlib.decompress(raw, -15)


def stored(e, new):
    """The member with new content, stored, with a plain local header (no data descriptor)."""
    name = e['name'].encode('utf-8')
    crc = zlib.crc32(new) & 0xffffffff
    e = dict(e)
    local = struct.pack('<IHHHHHIIIHH', 0x04034b50, 20, 0, 0, 0x6000, 0x5a21, crc, len(new), len(new), len(name), 0) + name
    e['raw'] = bytearray(local + new)
    e['method'], e['flags'], e['crc'], e['csize'], e['size'] = 0, 0, crc, len(new), len(new)
    # the central directory record again from its fixed part: this name, no extra field, no comment
    cd = bytearray(e['cd'][:46]) + name
    struct.pack_into('<HHHHHH', cd, 6, 20, 20, 0, 0, 0x6000, 0x5a21)
    struct.pack_into('<III', cd, 16, crc, len(new), len(new))
    struct.pack_into('<HHH', cd, 28, len(name), 0, 0)
    e['cd'] = cd
    return e


def pe_pkcs7(path):
    """The PKCS #7 message in the certificate table of a signed program (the part a signature check hands out)."""
    d = open(path, 'rb').read()
    pe = struct.unpack_from('<I', d, 0x3c)[0]
    magic = struct.unpack_from('<H', d, pe + 24)[0]
    table = pe + 24 + (112 if magic == 0x20b else 96) + 4 * 8      # data directory 4, the certificate table
    offset, size = struct.unpack_from('<II', d, table)
    assert offset and size, 'the program is not signed'
    length, _revision, _type = struct.unpack_from('<IHH', d, offset)
    blob = d[offset + 8:offset + length]
    der = 2 + (blob[1] & 0x7f) + int.from_bytes(blob[2:2 + (blob[1] & 0x7f)], 'big') if blob[1] & 0x80 else 2 + blob[1]
    return blob[:der]


def write(path, entries, zip64, comment=b'', prefix=b''):
    out, cd = bytearray(prefix), bytearray()
    for e in entries:
        struct.pack_into('<I', e['cd'], 42, len(out))
        out += e['raw']
        cd += e['cd']
    cd_offset = len(out)
    out += cd
    if zip64:
        record = len(out)
        out += struct.pack('<IQHHIIQQQQ', 0x06064b50, 44, 45, 45, 0, 0, len(entries), len(entries), len(cd), cd_offset)
        out += struct.pack('<IIQI', 0x07064b50, 0, record, 1)
        out += struct.pack('<IHHHHIIH', 0x06054b50, 0, 0, 0xffff, 0xffff, 0xffffffff, 0xffffffff, len(comment)) + comment
    else:
        out += struct.pack('<IHHHHIIH', 0x06054b50, 0, 0, len(entries), len(entries), len(cd), cd_offset, len(comment)) + comment
    with open(path, 'wb') as f:
        f.write(out)


def main():
    signed, outdir = sys.argv[1:3]
    program = sys.argv[3] if len(sys.argv) > 3 else None   # a program signed with the same certificate
    entries, zip64, comment = parse(open(signed, 'rb').read())
    by_name = {e['name']: i for i, e in enumerate(entries)}

    def variant(name, edit, prefix=b""):
        copy = [dict(e, raw=bytearray(e['raw']), cd=bytearray(e['cd'])) for e in entries]
        result = edit(copy)
        write(os.path.join(outdir, name), result if result is not None else copy, zip64, comment, prefix)

    def replace(copy, member, fn):
        i = by_name[member]
        copy[i] = stored(copy[i], fn(content(copy[i])))

    def bad_block(copy):
        replace(copy, 'logo.png', lambda b: b[:6] + bytes([b[6] ^ 0x1b]) + b[7:])

    def bad_blockmap(copy):
        replace(copy, 'AppxBlockMap.xml', lambda b: b.replace(b'Size="', b'Size ="', 1))

    def other_publisher(copy):
        replace(copy, 'AppxManifest.xml', lambda b: b.replace(b'CN=WineExplorer Signature Test', b'CN=Someone Else Entirely'))

    def extra_member(copy):
        base = copy[by_name['logo.png']]
        extra = stored(dict(base, name='extra.txt'), b'added after signing')
        copy.append(extra)

    def no_signature(copy):
        return [e for e in copy if e['name'] != 'AppxSignature.p7x']

    def bad_p7x(copy):
        replace(copy, 'AppxSignature.p7x', lambda b: b[:-20] + bytes([b[-20] ^ 0xff]) + b[-19:])

    def short_p7x(copy):
        replace(copy, 'AppxSignature.p7x', lambda b: b[:3])

    def wrong_magic(copy):
        replace(copy, 'AppxSignature.p7x', lambda b: b'PKCY' + b[4:])

    variant('bad-block.appx', bad_block)
    variant('bad-blockmap.appx', bad_blockmap)
    variant('other-publisher.appx', other_publisher)
    variant('extra-member.appx', extra_member)
    variant('no-signature.appx', no_signature)
    variant('bad-p7x.appx', bad_p7x)
    variant('short-p7x.appx', short_p7x)
    variant('wrong-magic.appx', wrong_magic)
    # the unmodified package rewritten by this tool: it must still verify as the original does (a check of the writer)
    variant('rewritten.appx', lambda copy: None)
    if program:
        # the signature of another kind of subject as the package's own: the PKCS #7 of a signed program, in a PKCX envelope
        message = pe_pkcs7(program)
        variant('foreign-p7x.appx', lambda copy: replace(copy, 'AppxSignature.p7x', lambda b: b'PKCX' + message))
        # a signed program with a package behind it: its signature is the program's, the archive is read from the end
        variant('pe-prefix.appx', lambda copy: None, open(program, 'rb').read())
        # the same, with the archive inside the certificate table of the program: the table is enlarged to hold it, which
        # the program's own signature does not cover, so the program still verifies as signed
        pe = open(program, 'rb').read()
        scratch = os.path.join(outdir, 'hidden.tmp')
        copy = [dict(e, raw=bytearray(e['raw']), cd=bytearray(e['cd'])) for e in entries]
        write(scratch, copy, zip64, b'', pe)
        pad = (-(os.path.getsize(scratch) - len(pe))) % 8        # a certificate table is a multiple of eight bytes
        write(scratch, copy, zip64, b'\0' * pad, pe)
        data = bytearray(open(scratch, 'rb').read())
        os.remove(scratch)
        added = len(data) - len(pe)
        header = struct.unpack_from('<I', data, 0x3c)[0]
        table = header + 24 + (112 if struct.unpack_from('<H', data, header + 24)[0] == 0x20b else 96) + 4 * 8
        offset, size = struct.unpack_from('<II', data, table)
        assert offset + size == len(pe), 'the certificate table is not at the end of the program'
        struct.pack_into('<I', data, table + 4, size + added)
        struct.pack_into('<I', data, offset, struct.unpack_from('<I', data, offset)[0] + added)
        with open(os.path.join(outdir, 'pe-hidden-zip.appx'), 'wb') as f:
            f.write(data)


main()

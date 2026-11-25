#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright 2026 WineExplorer contributors
#
# Damage an event log file of the EventLog service (WineExplorer L1, docs/EVENTLOG_DESIGN.md, E5) while the service
# is stopped, for the test of its load rule:
#   torn FILE   flip a byte in the middle of the third record (its CRC no longer matches): the load keeps two records
#   stale FILE  append a copy of the first record and its trailer after the last: the load cuts it
# The file: a 48-byte header, then records, each an EVENTLOGRECORD (its first DWORD is its length) and an 8-byte
# trailer (CRC32, record number).

import struct
import sys

HEADER = 48
TRAILER = 8


def records(data):
    pos = HEADER
    out = []
    while pos + 4 <= len(data):
        (length,) = struct.unpack_from("<I", data, pos)
        if length < 56 or pos + length + TRAILER > len(data):
            break
        out.append((pos, length))
        pos += length + TRAILER
    return out


def main():
    mode, path = sys.argv[1], sys.argv[2]
    data = bytearray(open(path, "rb").read())
    recs = records(data)
    if mode == "torn":
        if len(recs) < 3:
            print("corrupt=too-few records=%d" % len(recs))
            return 1
        pos, length = recs[2]
        data[pos + length // 2] ^= 0xFF
    elif mode == "stale":
        if not recs:
            print("corrupt=too-few records=0")
            return 1
        pos, length = recs[0]
        data += data[pos:pos + length + TRAILER]
    else:
        return 2
    open(path, "wb").write(bytes(data))
    print("corrupt=%s records=%d" % (mode, len(recs)))
    return 0


if __name__ == "__main__":
    sys.exit(main())

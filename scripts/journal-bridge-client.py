#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright 2026 WineExplorer contributors
"""Test client of wex-journal-bridge's socket (docs/EVENTLOG_JOURNAL.md, wine/wex_journal.h), for the suite.

  journal-bridge-client.py SOCKET hello CURSOR SECONDS   send HELLO with CURSOR, print each frame type received
                                                         ("frame=N"), then "end=eof" or "end=timeout"
  journal-bridge-client.py SOCKET garbage                send HELLO, then a frame with a bad magic; prints "end=eof"
                                                         when the bridge closes the connection
  journal-bridge-client.py SOCKET connect                connect and wait 2 s; "end=eof" when the bridge refused
"""

import select
import socket
import struct
import sys
import time

MAGIC = 0x4A584557
VERSION = 1
HELLO = 1
HDR = struct.Struct("<IHHII")


def frame(ftype, body):
    return HDR.pack(MAGIC, VERSION, ftype, HDR.size + len(body), 0) + body


def read_frames(sock, seconds):
    buf = b""
    deadline = time.time() + seconds
    while time.time() < deadline:
        ready, _, _ = select.select([sock], [], [], max(0.0, deadline - time.time()))
        if not ready:
            break
        data = sock.recv(65536)
        if not data:
            print("end=eof")
            return
        buf += data
        while len(buf) >= HDR.size:
            magic, version, ftype, size, _ = HDR.unpack_from(buf)
            if magic != MAGIC or size < HDR.size or len(buf) < size:
                break
            print("frame=%d" % ftype)
            buf = buf[size:]
    print("end=timeout")


def main():
    path, mode = sys.argv[1], sys.argv[2]
    sock = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
    sock.connect(path)
    if mode == "hello":
        sock.sendall(frame(HELLO, sys.argv[3].encode() + b"\0"))
        read_frames(sock, float(sys.argv[4]))
    elif mode == "garbage":
        sock.sendall(frame(HELLO, b"\0"))
        sock.sendall(struct.pack("<IHHII", 0x12345678, VERSION, 4, HDR.size, 0))
        read_frames(sock, 5)
    elif mode == "connect":
        read_frames(sock, 2)
    else:
        print("usage: see the module's docstring", file=sys.stderr)
        return 2
    return 0


if __name__ == "__main__":
    sys.exit(main())

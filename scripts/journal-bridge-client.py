#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright 2026 WineExplorer contributors
"""Test client of wex-journal-bridge's socket (docs/EVENTLOG_JOURNAL.md, wine/wex_journal.h), for the suite.

  journal-bridge-client.py SOCKET hello CURSOR SECONDS   send HELLO with CURSOR, print each frame type received
                                                         ("frame=N"), then "end=eof" or "end=timeout"
  journal-bridge-client.py SOCKET garbage                send HELLO, then a frame with a bad magic; prints "end=eof"
                                                         when the bridge closes the connection
  journal-bridge-client.py SOCKET connect                connect and wait 2 s; "end=eof" when the bridge refused
  journal-bridge-client.py SOCKET fakebridge             listen there as a bridge gone wrong would: to the service
                                                         that connects, events it may not write (EventLog 6005 in
                                                         System, another program's source, Service Control Manager
                                                         7036) and one it may (a Linux log source); prints
                                                         "served=1" after a few seconds
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


EVENT = 2
LOG_SYSTEM, LOG_APPLICATION, LOG_LINUX = 1, 2, 3


def event(log, source, event_id, strings):
    body = struct.pack("<IHHII", event_id, 1, len(strings), log, 0)
    body += source.encode() + b"\0" + b"\0"  # the source, an empty cursor
    for s in strings:
        body += s.encode() + b"\0"
    return frame(EVENT, body)


def fakebridge(path):
    import os
    try:
        os.unlink(path)
    except FileNotFoundError:
        pass
    srv = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
    srv.bind(path)
    os.chmod(path, 0o666)
    srv.listen(1)
    srv.settimeout(40)
    conn, _ = srv.accept()
    conn.recv(65536)  # its HELLO
    conn.sendall(event(LOG_SYSTEM, "EventLog", 6005, ["WEXJ FAKE 6005"]))
    conn.sendall(event(LOG_APPLICATION, "WexJ Evil", 1, ["WEXJ FAKE EVIL"]))
    conn.sendall(event(LOG_SYSTEM, "Service Control Manager", 7036, ["WEXJ FAKE 7036", "running"]))
    conn.sendall(event(LOG_LINUX, "WexJ Fake", 1, ["WEXJ FAKE ALLOWED"]))
    time.sleep(4)
    conn.close()
    srv.close()
    os.unlink(path)
    print("served=1")


def main():
    path, mode = sys.argv[1], sys.argv[2]
    if mode == "fakebridge":
        fakebridge(path)
        return 0
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

#!/usr/bin/env python3
# A stand-in for the accounts root helper (WineExplorer H1), for the tests of accounts_forward
#
# Copyright 2026 WineExplorer contributors
#
# This program is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; either version 2 of the License, or
# (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program; if not, see <https://www.gnu.org/licenses/>.

"""accounts-fake-helper.py MODE SOCKET LOG

Serves SOCKET (SOCK_SEQPACKET) as the helper of src/include/wexaccounts_proto.h would, one connection at a time.
Each request is logged as one line: "req op=N verdict=V uid=U id=I flags=F body=TEXT". MODE echo replies with status 0
and the body "verdict=V uid=U op=N body=TEXT"; MODE silent reads the request and never replies."""

import os
import socket
import struct
import sys

MAGIC = 0x41584557
VERSION = 1
REQ = struct.Struct('<IHHIIIII')   # magic version op request_id flags verdict caller_uid body_len
REP = struct.Struct('<IHHII')      # magic version status request_id body_len
MAX_FRAME = 65536


def main():
    mode, path, log = sys.argv[1], sys.argv[2], sys.argv[3]
    try:
        os.unlink(path)
    except FileNotFoundError:
        pass
    srv = socket.socket(socket.AF_UNIX, socket.SOCK_SEQPACKET)
    srv.bind(path)
    os.chmod(path, 0o666)
    srv.listen(4)
    with open(log, 'a', buffering=1) as out:
        out.write('listening\n')
        held = []
        while True:
            conn, _ = srv.accept()
            frame = conn.recv(MAX_FRAME + 1)
            if len(frame) < REQ.size:
                out.write('short %d\n' % len(frame))
                conn.close()
                continue
            magic, version, op, rid, flags, verdict, uid, blen = REQ.unpack_from(frame)
            body = frame[REQ.size:]
            text = body.decode('utf-8', 'replace')
            ok = magic == MAGIC and version == VERSION and blen == len(body)
            out.write('req op=%d verdict=%d uid=%d id=%d flags=%d ok=%d body=%s\n' % (op, verdict, uid, rid, flags, ok, text))
            if mode == 'silent':
                held.append(conn)   # never answered; closed when the helper ends
                continue
            reply = ('verdict=%d uid=%d op=%d body=%s' % (verdict, uid, op, text)).encode()
            conn.send(REP.pack(MAGIC, VERSION, 0, rid, len(reply)) + reply)
            conn.close()


if __name__ == '__main__':
    main()

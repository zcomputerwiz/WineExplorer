#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright 2026 WineExplorer contributors
#
# Client of the X0 lab's control socket (server/wex_x0lab.c, lab builds only; docs/SESSION0_CROSS_SERVER.md,
# "Appendix: X0 wire protocol and how its tests reach it"). Every lab wineserver listens on wex-x0-lab in its server
# directory, which is its working directory, and serves root only.
#
# Usage (as root): x0lab.py PID COMMAND [WORDS...]   prints the server's one answer

import os
import socket
import sys


def main():
    if len(sys.argv) < 3:
        print("usage: x0lab.py PID COMMAND [WORDS...]", file=sys.stderr)
        return 2
    path = "/proc/%s/cwd/wex-x0-lab" % sys.argv[1]
    # the server directory is the user's: trust the socket only when the server's own account owns it
    try:
        if os.stat(path).st_uid != os.stat("/proc/%s" % sys.argv[1]).st_uid:
            print("x0lab: %s is not owned by the server's account" % path, file=sys.stderr)
            return 1
    except OSError as e:
        print("x0lab: %s: %s" % (path, e), file=sys.stderr)
        return 1
    s = socket.socket(socket.AF_UNIX, socket.SOCK_SEQPACKET)
    s.settimeout(float(os.environ.get("X0LAB_TIMEOUT", "120")))
    try:
        s.connect(path)
        s.send(" ".join(sys.argv[2:]).encode())
        answer = s.recv(65536).decode(errors="replace")
    except OSError as e:
        print("x0lab: %s: %s" % (path, e), file=sys.stderr)
        return 1
    sys.stdout.write(answer if answer.endswith("\n") else answer + "\n")
    return 0


if __name__ == "__main__":
    sys.exit(main())

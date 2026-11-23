#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright 2026 WineExplorer contributors
# lab: use as WINESERVER=.../wsgdb.sh : runs the wineserver of the candidate (WSGDB_REAL) under gdb, follows its fork into the
# background, and when it dies on a signal writes the backtrace to $WSGDB_LOG (default /tmp/wsgdb.log).
log=${WSGDB_LOG:-/tmp/wsgdb.log}
exec gdb -batch -nx -ex "set follow-fork-mode child" -ex "set detach-on-fork on" -ex "handle SIGPIPE nostop noprint pass" \
    -ex "handle SIGUSR1 nostop noprint pass" -ex "handle SIGCHLD nostop noprint pass" -ex "handle SIGALRM nostop noprint pass" \
    -ex run -ex "bt 25" -ex "info registers rip rsp" -ex "info locals" --args "$WSGDB_REAL" "$@" >> "$log" 2>&1

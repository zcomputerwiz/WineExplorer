#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright 2026 WineExplorer contributors
#
# wex-make.sh [MAKE ARGUMENTS...] - make, as every Wine build on the test machine runs it (docs/TOOLING.md, "Builds").
#
#   - One build at a time on the whole machine: the lock is in /mnt/wsl, which all WSL distributions share (wexbuild,
#     wexgpo, Debian), so a session's build waits for another's instead of both running make -j16 at once. A waiting
#     build says whose build it waits for.
#   - At low priority (nice 10, idle I/O class), so that a suite or a VM running meanwhile keeps its share.
#   - Through ccache when it is installed (Debian's /usr/lib/ccache wrappers, gcc and the MinGW cross compilers), with
#     one cache for the machine's build trees: a tree that changes a few files, or a second tree of the same commit,
#     recompiles only what differs.
#
# The arguments are make's own (-C DIR, -jN, targets, VAR=VALUE). The exit status is make's. Run as root.
set -u

lock=/mnt/wsl/wex-build.lock
[ -d /mnt/wsl ] && [ -w /mnt/wsl ] || lock=/var/lock/wex-build.lock
owner="$lock.owner"

exec 9>>"$lock" || { echo "wex-make: cannot open $lock" >&2; exit 1; }
if ! flock -n 9; then
    echo "wex-make: waiting for another build: $(cat "$owner" 2>/dev/null || echo unknown)" >&2
    flock 9 || { echo "wex-make: cannot take $lock" >&2; exit 1; }
fi
printf '%s pid %s, %s, since %s: make %s\n' "$(cat /etc/hostname 2>/dev/null || hostname)" "$$" "$PWD" \
    "$(date +%T)" "$*" > "$owner"

if [ -d /usr/lib/ccache ] && command -v ccache >/dev/null 2>&1; then
    export PATH="/usr/lib/ccache:$PATH"
    export CCACHE_DIR="${CCACHE_DIR:-/var/cache/wex-ccache}"
    mkdir -p "$CCACHE_DIR"
fi

# the lock stays with this shell (fd 9 is not given to make: no server or daemon of a build may hold it)
nice -n 10 ionice -c 3 make "$@" 9>&-
status=$?
: > "$owner"
exit $status

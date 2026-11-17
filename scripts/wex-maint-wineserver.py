#!/usr/bin/python3 -I
# SPDX-License-Identifier: GPL-2.0-or-later
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
# You should have received a copy of the GNU General Public License along
# with this program; if not, write to the Free Software Foundation, Inc.,
# 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
"""wineserver for a component install that runs through session 0 (wex-components, MachineWine).

The install's programs are clients of session 0's server, which serves the machine root for every session: it never
goes idle and is not the install's to stop. winetricks still calls "$WINESERVER -w" (wait until Wine is done) and
"$WINESERVER -k" (end Wine). Here they mean this install's own Wine programs: those in the caller's cgroup, the
transient unit systemd-run made for the install, apart from the caller and its ancestors.
  -w   wait until none of them is left
  -k   end them (SIGKILL), then wait
Any other option (-p, -d) is accepted and does nothing. Nothing outside the caller's cgroup is ever signalled."""
import os
import select
import signal
import sys

WINE_DIRECTORIES = ('/usr/local/bin/', '/usr/local/lib/wine/')
CGROUP_ROOT = '/sys/fs/cgroup'


def own_cgroup():
    with open('/proc/self/cgroup', encoding='ascii') as cgroup:
        for line in cgroup:
            if line.startswith('0::'):
                return CGROUP_ROOT + line[3:].strip()
    raise SystemExit('wex-maint-wineserver: no unified cgroup')


def ancestors():
    """The caller's chain: this process and its parents (winetricks, xvfb-run, timeout, ...)."""
    chain, pid = set(), os.getpid()
    while pid > 1 and pid not in chain:
        chain.add(pid)
        try:
            with open(f'/proc/{pid}/stat', encoding='ascii', errors='replace') as info:
                pid = int(info.read().rsplit(')', 1)[1].split()[1])
        except (OSError, ValueError, IndexError):
            break
    return chain


def wine_programs(cgroup, skip):
    """The Wine programs (their executable is one of the installed Wine's loaders) in CGROUP, but SKIP."""
    try:
        with open(cgroup + '/cgroup.procs', encoding='ascii') as procs:
            pids = [int(pid) for pid in procs.read().split()]
    except OSError:
        return []
    found = []
    for pid in pids:
        if pid in skip:
            continue
        try:
            exe = os.readlink(f'/proc/{pid}/exe')
        except OSError:
            continue        # gone, or a zombie
        if exe.startswith(WINE_DIRECTORIES) and os.path.basename(exe).startswith('wine') and \
                os.path.basename(exe) != 'wineserver':
            found.append(pid)
    return found


def wait_for(cgroup, skip, kill=False):
    """Until no Wine program of the cgroup is left: wait on process descriptors (no polling), and look again, since a
    program may have started another one before it ended."""
    while True:
        pids = wine_programs(cgroup, skip)
        if not pids:
            return 0
        descriptors = []
        for pid in pids:
            try:
                descriptors.append(os.pidfd_open(pid))
            except ProcessLookupError:
                continue
        try:
            if kill:
                for descriptor in descriptors:
                    try:
                        signal.pidfd_send_signal(descriptor, signal.SIGKILL)
                    except ProcessLookupError:
                        pass
            if descriptors:
                select.select(descriptors, [], [])
        finally:
            for descriptor in descriptors:
                os.close(descriptor)


def main(argv):
    if '-w' in argv or '--wait' in argv:
        return wait_for(own_cgroup(), ancestors())
    if any(arg.startswith('-k') or arg.startswith('--kill') for arg in argv):
        return wait_for(own_cgroup(), ancestors(), kill=True)
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))

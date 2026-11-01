#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright 2026 WineExplorer contributors
"""Check that a Wine build puts the first thread's data block on a signal stack boundary.

The signal handlers find thread_data by masking the stack pointer with the 64 KB signal stack mask. A first block
that is only page aligned makes the main thread read garbage: its suspend requests are never answered and thread
samplers (Chromium's stack sampling profiler) hang forever in GetThreadContext. Whether the block lands aligned
depends on what the C library mapped before it, for instance the nine locale files of LANG=C.UTF-8, so the check
starts many processes and reads the block's address from WINEDEBUG=+virtual. It can also run suspend-context-probe
(see scripts/suspend-context-probe.c; build the 32-bit and the 64-bit flavor) against the same build.

Usage: check-first-thread-alignment.py --wine BUILD/wine [--wineserver BUILD/server/wineserver] [--prefix DIR]
           [--runs 200] [--lang C.UTF-8] [--probe suspend-context-probe64.exe --probe suspend-context-probe32.exe]
           [--probe-runs 20] [--probe-count 1000] [--timeout 60]
Exit status: 0 when every run is aligned and every probe run finishes, 1 otherwise.
"""
import argparse
import os
import re
import signal
import subprocess
import sys
import threading

VIEW = re.compile(r'View: (0x[0-9a-f]+) - (0x[0-9a-f]+) ')
FIRST_THREAD_VIEW = 0x110000   # signal stack mask + 1 plus the kernel stack, see virtual_alloc_first_thread_data()
SIGNAL_STACK_MASK = 0xffff


def kill_group(proc):
    try:
        os.killpg(proc.pid, signal.SIGKILL)
    except OSError:
        pass


def stop_server(wineserver, env):
    try:
        subprocess.run([wineserver, '-k'], env=env, check=False, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    except OSError:
        pass


def first_thread_block(wine, env):
    proc = subprocess.Popen([wine, 'cmd', '/c', 'exit'], env=env, stdout=subprocess.DEVNULL, stderr=subprocess.PIPE,
                            text=True, errors='replace', start_new_session=True)
    timer = threading.Timer(30, kill_group, [proc])
    timer.start()
    found = None
    try:
        for line in proc.stderr:
            match = VIEW.search(line)
            if match and int(match.group(2), 16) - int(match.group(1), 16) + 1 == FIRST_THREAD_VIEW:
                found = int(match.group(1), 16)
                break
    finally:
        timer.cancel()
        kill_group(proc)
        proc.wait()
    return found


def check_alignment(wine, env, runs):
    env = dict(env, WINEDEBUG='+virtual')
    aligned = misaligned = missing = 0
    residues = set()
    for _ in range(runs):
        base = first_thread_block(wine, env)
        if base is None:
            missing += 1
        elif base & SIGNAL_STACK_MASK:
            misaligned += 1
            residues.add(base & SIGNAL_STACK_MASK)
        else:
            aligned += 1
    print('alignment: %d runs, %d aligned, %d misaligned, %d without a first-thread view; residues %s' % (
        runs, aligned, misaligned, missing, sorted(hex(r) for r in residues)))
    return misaligned == 0 and aligned > 0


def check_probe(wine, wineserver, env, exe, runs, count, timeout):
    env = dict(env, WINEDEBUG='-all')
    ok = 0
    for number in range(1, runs + 1):
        proc = subprocess.Popen([wine, exe, str(count)], env=env, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                                text=True, errors='replace', start_new_session=True)
        try:
            out, _ = proc.communicate(timeout=timeout)
            result = out.strip().splitlines()[-1] if out.strip() else 'no output'
            good = proc.returncode == 0 and result.startswith('ok ')
        except subprocess.TimeoutExpired:
            # wineserver runs in its own session and keeps the output pipe open, so stop it before reading the rest
            kill_group(proc)
            stop_server(wineserver, env)
            try:
                proc.communicate(timeout=10)
            except subprocess.TimeoutExpired:
                pass
            result, good = 'timeout after %g s' % timeout, False
        ok += good
        if not good:
            print('probe %s run %d: %s' % (os.path.basename(exe), number, result))
    print('probe %s: %d of %d runs finished' % (os.path.basename(exe), ok, runs))
    return ok == runs


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--wine', required=True, help='wine launcher of a build tree or an install')
    parser.add_argument('--wineserver', help='default: server/wineserver next to the launcher, else wineserver')
    parser.add_argument('--prefix', default=os.environ.get('WINEPREFIX', os.path.expanduser('~/.wine')),
                        help='existing prefix; the probe needs a working one')
    parser.add_argument('--runs', type=int, default=200, help='processes started for the alignment check')
    parser.add_argument('--lang', default='C.UTF-8', help='LANG for the runs (empty keeps the environment)')
    parser.add_argument('--probe', action='append', default=[], help='suspend-context-probe executable (repeatable)')
    parser.add_argument('--probe-runs', type=int, default=20)
    parser.add_argument('--probe-count', type=int, default=1000, help='suspend/resume rounds per probe run')
    parser.add_argument('--timeout', type=float, default=60, help='seconds before a probe run counts as hung')
    args = parser.parse_args()

    wine = os.path.abspath(args.wine)
    here = os.path.dirname(wine)
    wineserver = args.wineserver or next((p for p in (os.path.join(here, 'server', 'wineserver'),
                                                      os.path.join(here, 'wineserver')) if os.path.exists(p)),
                                         'wineserver')
    env = dict(os.environ, WINEPREFIX=os.path.abspath(args.prefix), WINEDLLOVERRIDES='winemenubuilder.exe=d')
    if args.lang:
        env['LANG'] = args.lang
        env.pop('LC_ALL', None)
    good = True
    try:
        good &= check_alignment(wine, env, args.runs)
        for exe in args.probe:
            good &= check_probe(wine, wineserver, env, os.path.abspath(exe), args.probe_runs, args.probe_count,
                                args.timeout)
    finally:
        stop_server(wineserver, env)
    print('PASS' if good else 'FAIL')
    return 0 if good else 1


if __name__ == '__main__':
    sys.exit(main())

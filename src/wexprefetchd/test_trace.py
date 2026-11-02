#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright 2026 WineExplorer contributors
"""Tests for the prefetch trace (.pf recording, readahead replay) and the KSM tuner mode of wexprefetchd.

usage: test_trace.py DAEMON_BINARY PE_FILE
"""
import os
import shutil
import struct
import subprocess
import sys
import tempfile
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import test_pf_v17
import test_wexprefetchd as T

CHILD = '''
import os, socket, struct, sys
sock = sys.argv[1]
for path in sys.argv[2:]:
    fd = os.open(path, os.O_RDONLY)
    s = socket.socket(socket.AF_UNIX, socket.SOCK_SEQPACKET)
    s.settimeout(20)
    s.connect(sock)
    socket.send_fds(s, [struct.pack("<IIII", 0x31465058, 1, 1, 0)], [fd])
    data, got, _f, _a = socket.recv_fds(s, 4096, 4)
    for g in got:
        os.close(g)
    os.close(fd)
    s.close()
'''


def run_app(d, files):
    """One short-lived process that asks for the given images, like a Wine process starting."""
    subprocess.run([sys.executable, '-c', CHILD, d.sock] + files, check=True)


def wait_for(pred, secs=6):
    end = time.time() + secs
    while time.time() < end:
        v = pred()
        if v:
            return v
        time.sleep(0.1)
    return None


def make_images(tmp, pe):
    data = bytearray(open(pe, 'rb').read())
    lfanew = struct.unpack_from('<I', data, 0x3c)[0]
    exe = os.path.join(tmp, 'App.exe')
    open(exe, 'wb').write(data)
    chars = struct.unpack_from('<H', data, lfanew + 22)[0]
    dlls = []
    for n in ('one.dll', 'two.dll'):
        dll = bytearray(data)
        struct.pack_into('<H', dll, lfanew + 22, chars | 0x2000)
        dll += n.encode()          # distinct content, still a valid PE
        p = os.path.join(tmp, n)
        open(p, 'wb').write(dll)
        dlls.append(p)
    return exe, dlls


def pf_files(tdir):
    return sorted(f for f in os.listdir(tdir) if f.endswith('.pf'))


def main():
    binary, pe = sys.argv[1], sys.argv[2]
    tmp = tempfile.mkdtemp(prefix='wextrace')
    try:
        exe, dlls = make_images(tmp, pe)
        tdir = os.path.join(tmp, 'traces')
        os.environ['WEXPF_TRACE_DIR'] = tdir
        os.environ['WEXPF_VERBOSE'] = '1'

        d = T.Daemon(binary, tmp)
        run_app(d, [exe] + dlls)
        pfs = wait_for(lambda: pf_files(tdir))
        T.check('trace: .pf written after the process exits', pfs and len(pfs) == 1, pfs)
        if pfs:
            data = open(os.path.join(tdir, pfs[0]), 'rb').read()
            info = test_pf_v17.parse_pf_v17(data)
            names = [m['filename'] for m in info['metrics']] if 'metrics' in info else []
            T.check('trace: three images listed', sorted(names) == sorted([exe] + dlls) or
                    sorted(os.path.realpath(n) for n in names) == sorted(os.path.realpath(p) for p in [exe] + dlls),
                    names)
            upper = os.path.realpath(exe).upper()
            T.check('trace: file name carries the XP path hash',
                    pfs[0] == test_pf_v17.make_pf_name_py('APP.EXE', test_pf_v17.scca_xp_hash(upper)), pfs[0])
            T.check('trace: run count 1', info.get('run_count') == 1, info.get('run_count'))

        run_app(d, [exe] + dlls)
        again = wait_for(lambda: info and test_pf_v17.parse_pf_v17(open(os.path.join(tdir, pfs[0]), 'rb').read())
                         .get('run_count') == 2)
        T.check('trace: run count 2 after the second run', again)
        replayed = wait_for(lambda: 'readahead: 3 files' in open(os.path.join(tmp, 'daemon.log')).read())
        T.check('readahead: the second run replays three files', replayed)
        T.check('daemon alive', d.alive())
        d.stop()

        # a .pf that lies must not crash the replay or leave the daemon unusable
        pf = os.path.join(tdir, pfs[0])
        raw = bytearray(open(pf, 'rb').read())
        for off in (12, 84, 88, 100, 104, 152, 160):
            raw[off:off + 4] = b'\xff\xff\xff\xff'
        open(pf, 'wb').write(raw)
        d = T.Daemon(binary, tmp)
        run_app(d, [exe] + dlls)
        time.sleep(1)
        T.check('corrupt .pf: daemon survives', d.alive())
        d.stop()

        # switches
        shutil.rmtree(tdir)
        d = T.Daemon(binary, tmp, 'Prefetch=0\nReadahead=0\n')
        run_app(d, [exe] + dlls)
        time.sleep(2)
        T.check('Prefetch=0 writes nothing', not os.path.isdir(tdir) or not pf_files(tdir))
        d.stop()

        # KSM tuner mode against a fake sysfs and meminfo
        ksm = os.path.join(tmp, 'ksm')
        os.makedirs(ksm)
        for n in ('run', 'pages_to_scan', 'sleep_millisecs', 'pages_sharing'):
            open(os.path.join(ksm, n), 'w').write('0\n')
        mi = os.path.join(tmp, 'meminfo')
        open(mi, 'w').write('MemTotal:       16000000 kB\nMemAvailable:    1000000 kB\n')
        env = dict(os.environ, WEXPF_KSM_DIR=ksm, WEXPF_MEMINFO=mi, WEXPF_KSM_INTERVAL_MS='50', WEXPF_KSM_STEPS='6')
        r = subprocess.run([binary, '--ksm-tuner', '--foreground'], env=env, timeout=30)
        read = lambda n: open(os.path.join(ksm, n)).read().strip()
        T.check('ksm tuner: exits after its steps', r.returncode == 0, r.returncode)
        T.check('ksm tuner: scanning on under memory pressure', read('run') == '1', read('run'))
        T.check('ksm tuner: pages_to_scan raised', int(read('pages_to_scan')) > 64, read('pages_to_scan'))
        T.check('ksm tuner: sleep_millisecs set', int(read('sleep_millisecs')) > 0, read('sleep_millisecs'))
    finally:
        shutil.rmtree(tmp, ignore_errors=True)
    ok = all(T.results)
    print('%d/%d passed' % (sum(T.results), len(T.results)))
    return 0 if ok else 1


if __name__ == '__main__':
    sys.exit(main())

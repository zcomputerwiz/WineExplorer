#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright 2026 WineExplorer contributors
"""End-to-end and abuse tests for wexprefetchd.

usage: test_wexprefetchd.py DAEMON_BINARY PE_FILE [BIG_PE_FILE]

PE_FILE is any small PE with FileAlignment 0x200 (a MinGW probe exe is fine).  Starts the daemon in a private
directory, then checks: correct flat images (against verify_flat.py), content addressing (copy dedups, modified
copy gets its own entry), protocol abuse, hostile PE headers (random mutations must never crash the daemon),
concurrency (one build for many simultaneous first requests), the size limit, Enabled=0 with SIGHUP, the cache cap
and the status file.  Exit 0 only if everything passes.
"""
import array
import os
import random
import shutil
import signal
import socket
import struct
import subprocess
import sys
import tempfile
import threading
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import verify_flat

MAGIC, VERSION, OP_GET = 0x31465058, 1, 1
OK, BAD_REQUEST, NOT_PE, TOO_LARGE, BUSY, IO_ERROR, DISABLED, UNSUPPORTED = range(8)
REPLY = struct.Struct('<IIiIQ65s7s')

results = []


def check(name, cond, detail=''):
    results.append(bool(cond))
    print('%s %s%s' % ('PASS' if cond else 'FAIL', name, (' -- ' + str(detail)) if detail and not cond else ''))
    return cond


class Daemon:
    def __init__(self, binary, tmp, config=''):
        self.tmp = tmp
        self.sock = os.path.join(tmp, 'run', 'prefetch.sock')
        self.cache = os.path.join(tmp, 'cache')
        self.config = os.path.join(tmp, 'prefetch.conf')
        os.makedirs(self.cache, exist_ok=True)
        open(self.config, 'w').write(config)
        env = dict(os.environ, WEXPF_SOCKET=self.sock, WEXPF_CACHE_DIR=self.cache, WEXPF_CONFIG=self.config,
                   WEXPF_FOREGROUND='1')
        self.log = open(os.path.join(tmp, 'daemon.log'), 'w')
        self.proc = subprocess.Popen([binary, '--foreground'], env=env, stdout=self.log, stderr=self.log)
        for _ in range(100):
            if os.path.exists(self.sock):
                break
            time.sleep(0.05)

    def alive(self):
        return self.proc.poll() is None

    def stop(self):
        if self.alive():
            self.proc.send_signal(signal.SIGTERM)
            try:
                self.proc.wait(10)
            except subprocess.TimeoutExpired:
                self.proc.kill()
        return self.proc.returncode

    def builds(self):
        self.log.flush()
        return sum(1 for line in open(os.path.join(self.tmp, 'daemon.log')) if 'built flat image' in line)

    def images(self):
        return sorted(f for f in os.listdir(self.cache) if f.endswith('.img'))


def request(daemon, fds, header=None, extra=b'', timeout=20):
    """Send one datagram with the given descriptors; return (status, reply_fields, received_fds)."""
    s = socket.socket(socket.AF_UNIX, socket.SOCK_SEQPACKET)
    s.settimeout(timeout)
    s.connect(daemon.sock)
    payload = struct.pack('<IIII', *(header or (MAGIC, VERSION, OP_GET, 0))) + extra
    socket.send_fds(s, [payload], fds)
    try:
        data, got, _flags, _addr = socket.recv_fds(s, 4096, 4)
    except (socket.timeout, ConnectionResetError):
        s.close()
        return None, None, []
    s.close()
    if len(data) != REPLY.size:
        for g in got:
            os.close(g)
        return -1, None, []
    magic, version, status, layout, image_size, sha, _pad = REPLY.unpack(data)
    return status, dict(layout=layout, image_size=image_size, sha=sha.split(b'\0')[0].decode()), got


def get_image(daemon, path):
    fd = os.open(path, os.O_RDONLY)
    try:
        status, info, got = request(daemon, [fd])
    finally:
        os.close(fd)
    return status, info, got


def main():
    binary, pe = sys.argv[1], sys.argv[2]
    big = sys.argv[3] if len(sys.argv) > 3 else None
    tmp = tempfile.mkdtemp(prefix='wexpf-test-')
    d = Daemon(binary, tmp)
    check('daemon starts and listens', d.alive() and os.path.exists(d.sock))

    # 1. correct flat image, checked against the independent oracle
    status, info, got = get_image(d, pe)
    check('valid PE: status OK with one descriptor', status == OK and len(got) == 1, (status, got))
    if got:
        flat_path = os.path.join(tmp, 'flat.bin')
        with os.fdopen(got[0], 'rb') as f:
            open(flat_path, 'wb').write(f.read())
        check('flat image matches the independent oracle', verify_flat.main_args(pe, flat_path) == 0)
        import hashlib
        check('reported sha256 is the digest of the source', info['sha'] == hashlib.sha256(open(pe, 'rb').read()).hexdigest())
        check('layout and size fields', info['layout'] == 1 and info['image_size'] == os.path.getsize(flat_path))
    # descriptor must be read-only
    fd = os.open(pe, os.O_RDONLY)
    status, info, got = request(d, [fd])
    os.close(fd)
    if got:
        import fcntl
        check('returned descriptor is O_RDONLY', fcntl.fcntl(got[0], fcntl.F_GETFL) & os.O_ACCMODE == os.O_RDONLY)
        os.close(got[0])

    # 2. content addressing: a byte-identical copy dedups, a modified copy gets its own entry
    copy = os.path.join(tmp, 'copy.dll')
    shutil.copy(pe, copy)
    s1, i1, g1 = get_image(d, copy)
    [os.close(g) for g in g1]
    check('identical copy dedups to one cache entry', s1 == OK and len(d.images()) == 1, d.images())
    mod = os.path.join(tmp, 'mod.dll')
    data = bytearray(open(pe, 'rb').read())
    sec_off = struct.unpack_from('<I', data, 0x3c)[0]
    first_raw = None
    nsec = struct.unpack_from('<H', data, sec_off + 6)[0]
    opt_size = struct.unpack_from('<H', data, sec_off + 20)[0]
    table = sec_off + 24 + opt_size
    first_raw = struct.unpack_from('<I', data, table + 20)[0]
    data[first_raw + 8] ^= 0xff
    open(mod, 'wb').write(data)
    s2, i2, g2 = get_image(d, mod)
    if g2:
        flat2 = os.path.join(tmp, 'flat2.bin')
        with os.fdopen(g2[0], 'rb') as f:
            open(flat2, 'wb').write(f.read())
    check('modified copy gets a different digest and its own entry',
          s2 == OK and i2['sha'] != info['sha'] and len(d.images()) == 2, (s2, d.images()))
    check('modified copy image matches the oracle for the modified bytes', g2 and verify_flat.main_args(mod, flat2) == 0)
    check('original entry unchanged after the modified one', len(d.images()) == 2 and
          os.path.getsize(os.path.join(d.cache, info['sha'] + '.img')) == info['image_size'])

    # 3. protocol abuse
    def expect(name, fds, header=None, extra=b'', accepted=(BAD_REQUEST,), allow_close=False):
        status, _info, got = request(d, fds, header, extra)
        [os.close(g) for g in got]
        ok = status in accepted or (allow_close and status in (None, -1))
        check('abuse: ' + name, ok and not got and d.alive(), (status, len(got), d.alive()))

    fd = os.open(pe, os.O_RDONLY)
    expect('no descriptor', [])
    expect('two descriptors', [fd, fd])
    expect('wrong magic', [fd], (0xdeadbeef, VERSION, OP_GET, 0))
    expect('wrong version', [fd], (MAGIC, 99, OP_GET, 0))
    expect('wrong op', [fd], (MAGIC, VERSION, 77, 0))
    expect('nonzero flags', [fd], (MAGIC, VERSION, OP_GET, 1))
    # short datagram
    s = socket.socket(socket.AF_UNIX, socket.SOCK_SEQPACKET)
    s.settimeout(10)
    s.connect(d.sock)
    socket.send_fds(s, [b'\x01\x02'], [fd])
    try:
        data = s.recv(4096)
        st = struct.unpack_from('<i', data, 8)[0] if len(data) == REPLY.size else -1
    except Exception:
        st = None
    s.close()
    check('abuse: short datagram', st in (BAD_REQUEST, None, -1) and d.alive(), st)
    os.close(fd)
    wfd = os.open(os.path.join(tmp, 'writeonly'), os.O_WRONLY | os.O_CREAT, 0o600)
    expect('write-only descriptor', [wfd], accepted=(BAD_REQUEST, NOT_PE, TOO_LARGE))
    os.close(wfd)
    dfd = os.open(tmp, os.O_RDONLY)
    expect('directory descriptor', [dfd])
    os.close(dfd)
    sfd = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
    expect('socket descriptor', [sfd.fileno()])
    sfd.close()

    # 4. non-PE and hostile headers
    def file_with(name, content):
        p = os.path.join(tmp, name)
        open(p, 'wb').write(content)
        return p

    for name, content in (('empty', b''), ('text', b'hello world\n' * 100), ('mz-only', b'MZ' + b'\0' * 100),
                          ('truncated', open(pe, 'rb').read()[:0x180]),
                          ('bad-lfanew', b'MZ' + b'\0' * 0x3a + struct.pack('<I', 0xfffffff0) + b'\0' * 200)):
        status, _i, got = get_image(d, file_with(name, content))
        [os.close(g) for g in got]
        check('hostile: ' + name + ' rejected without crash',
              status in (NOT_PE, BAD_REQUEST, TOO_LARGE, UNSUPPORTED) and not got and d.alive(), (status, d.alive()))

    rnd = random.Random(1234)
    src = open(pe, 'rb').read()
    bad = 0
    statuses = {}
    for n in range(300):
        m = bytearray(src)
        for _ in range(rnd.randint(1, 8)):
            off = rnd.randrange(0, min(len(m), 0x400))
            m[off] = rnd.randrange(256)
        # sometimes poke fields of the section table with extreme values
        if rnd.random() < 0.5:
            e = struct.unpack_from('<I', m, 0x3c)[0]
            try:
                opt_size = struct.unpack_from('<H', m, e + 20)[0]
                table = e + 24 + opt_size + 40 * rnd.randrange(0, 3)
                struct.pack_into('<I', m, table + 8 + 4 * rnd.randrange(4), rnd.choice([0, 1, 0xfff, 0x1000, 0x7fffffff, 0xffffffff]))
            except struct.error:
                pass
        p = file_with('fuzz.bin', bytes(m))
        status, _i, got = get_image(d, p)
        [os.close(g) for g in got]
        statuses[status] = statuses.get(status, 0) + 1
        if status not in (OK, NOT_PE, UNSUPPORTED, TOO_LARGE, IO_ERROR) or not d.alive():
            bad += 1
            break
    check('fuzz: 300 mutated headers never crash or confuse the daemon', bad == 0 and d.alive(), statuses)
    print('     fuzz status counts:', statuses)

    # 5. concurrency: many simultaneous first requests for one new file produce one build
    fresh = os.path.join(tmp, 'fresh.dll')
    data = bytearray(open(pe, 'rb').read())
    data[first_raw + 16] ^= 0x55
    open(fresh, 'wb').write(data)
    before = d.builds()
    outcomes = []
    lock = threading.Lock()

    def worker():
        st, inf, g = get_image(d, fresh)
        [os.close(x) for x in g]
        with lock:
            outcomes.append((st, inf['sha'] if inf else None))

    threads = [threading.Thread(target=worker) for _ in range(12)]
    [t.start() for t in threads]
    [t.join() for t in threads]
    shas = {sha for st, sha in outcomes if st == OK}
    check('concurrent: 12 simultaneous requests all succeed with one digest',
          len(outcomes) == 12 and all(st in (OK, BUSY) for st, _ in outcomes) and len(shas) == 1, outcomes)
    check('concurrent: exactly one build', d.builds() - before == 1, d.builds() - before)

    # 6. big real-world image, if given
    if big:
        t0 = time.time()
        status, info, got = get_image(d, big)
        elapsed = time.time() - t0
        [os.close(g) for g in got]
        check('big image (%s) builds' % os.path.basename(big), status == OK, status)
        print('     first request %.2fs' % elapsed)

    rc = d.stop()
    check('SIGTERM exits 0', rc == 0, rc)
    check('status file written at exit', os.path.exists(os.path.join(tmp, 'run', 'prefetch.status')))
    check('no temporary files left behind', not [f for f in os.listdir(d.cache) if f.startswith('.tmp')], os.listdir(d.cache))

    # 6b. restart with a warm cache and an empty memo: concurrent requests for one big file must not each
    # re-read and re-hash it (they used to queue behind the build lock, ~2 s apiece for a 340 MB DLL)
    if big:
        d = Daemon(binary, tmp)
        t0 = time.time()
        outcomes = []

        def big_worker():
            st, inf, g = get_image(d, big)
            [os.close(x) for x in g]
            with lock:
                outcomes.append((st, inf['sha'] if inf else None))

        threads = [threading.Thread(target=big_worker) for _ in range(12)]
        [t.start() for t in threads]
        [t.join() for t in threads]
        elapsed = time.time() - t0
        check('warm cache, empty memo: 12 concurrent requests for a big image share one hash',
              len(outcomes) == 12 and all(st == OK for st, _ in outcomes) and len({s for _, s in outcomes}) == 1 and
              elapsed < 8, (outcomes[:2], elapsed))
        print('     12 concurrent requests took %.2fs' % elapsed)
        check('warm cache: no rebuild', d.builds() == 0, d.builds())
        d.stop()

    # 6c. a browser start-up burst: dozens of processes ask at once while one big image is being hashed.  With the
    # default limit none may be told BUSY (they would silently fall back); with MaxConcurrent=16 the limit must bite.
    if big:
        burst_pe = os.path.join(tmp, 'burst.dll')
        shutil.copyfile(big, burst_pe)
        with open(burst_pe, 'r+b') as f:
            f.seek(0x608)
            f.write(b'\x90')

        def burst(config, n=48):
            bd = Daemon(binary, tempfile.mkdtemp(prefix='wexpf-burst-'), config)
            res = []

            def w():
                st, inf, g = get_image(bd, burst_pe)
                [os.close(x) for x in g]
                with lock:
                    res.append(st)

            ts = [threading.Thread(target=w) for _ in range(n)]
            t0 = time.time()
            [t.start() for t in ts]
            [t.join() for t in ts]
            alive = bd.alive()
            bd.stop()
            shutil.rmtree(bd.tmp, ignore_errors=True)
            return res, time.time() - t0, alive

        res, elapsed, alive = burst('')
        check('burst: 48 simultaneous cold requests all served by default',
              len(res) == 48 and all(st == OK for st in res) and alive, (sorted(set(res)), elapsed))
        print('     48 simultaneous cold requests took %.2fs' % elapsed)
        res, elapsed, alive = burst('MaxConcurrent=16\n')
        check('burst: MaxConcurrent=16 answers the excess with BUSY and survives',
              BUSY in res and OK in res and alive and all(st in (OK, BUSY) for st in res), sorted(set(res)))
        os.unlink(burst_pe)

    # 7. configuration: size limit, Enabled=0 via SIGHUP, cache cap
    d = Daemon(binary, tmp + '-b' if False else tempfile.mkdtemp(prefix='wexpf-test2-'), 'MaxImageMB=1\n')
    big_blob = file_with('blob', b'MZ' + b'\0' * (2 * 1024 * 1024))
    status, _i, got = get_image(d, big_blob)
    [os.close(g) for g in got]
    check('MaxImageMB=1 refuses a 2 MB file', status == TOO_LARGE, status)
    open(d.config, 'w').write('Enabled=0\n')
    d.proc.send_signal(signal.SIGHUP)
    time.sleep(0.5)
    status, _i, got = get_image(d, pe)
    [os.close(g) for g in got]
    check('Enabled=0 after SIGHUP answers DISABLED', status == DISABLED, status)
    d.stop()

    d = Daemon(binary, tempfile.mkdtemp(prefix='wexpf-test3-'), 'MaxCacheMB=1\n')
    shas = []
    for n in range(6):
        v = bytearray(open(pe, 'rb').read())
        v[first_raw + 24 + n] ^= 0x11
        p = file_with('cap%d.dll' % n, bytes(v))
        status, inf, got = get_image(d, p)
        [os.close(g) for g in got]
        shas.append(inf['sha'] if inf else None)
        time.sleep(0.05)
    total = sum(os.stat(os.path.join(d.cache, f)).st_blocks * 512 for f in d.images())
    newest = shas[-1]
    check('cache cap: total stays near MaxCacheMB and the newest entry survives',
          (newest + '.img') in d.images() and len(d.images()) < 6 and total <= 2 * 1024 * 1024, (len(d.images()), total))
    d.stop()

    print('\n%d/%d passed' % (sum(results), len(results)))
    shutil.rmtree(tmp, ignore_errors=True)
    return 0 if all(results) else 1


if __name__ == '__main__':
    sys.exit(main())

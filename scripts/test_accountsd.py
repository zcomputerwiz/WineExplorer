#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright 2026 WineExplorer contributors
"""Tests of wex-accountsd (docs/LOCAL_USERS_DESIGN.md): the peer check, the frames, the verdict classes, every operation and
protective rule, and that a password hash appears only in the SetPassword call and never in the helper's log.

Run as root on a machine with libsystemd, dbus-daemon, python3-dbus and python3-gi (wexgpo has them):
    python3 scripts/test_accountsd.py [PATH-TO-wex-accountsd]
Builds the helper if no path is given. The helper runs with WEX_ACCOUNTSD_TEST=1 against a private bus whose
AccountsService, logind and systemd are scripts/accountsd-fake-bus.py."""
import ctypes
import json
import os
import select
import signal
import socket
import struct
import subprocess
import sys
import tempfile
import time

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
failures = 0

WEXA_MAGIC = 0x41584557
(LIST, GET, ADMINS, CREATE, DELETE, RENAME, FULLNAME, LOCKED, TYPE, PASSWORD, SYNC) = range(1, 12)
(OK, DENIED, NOT_FOUND, EXISTS, BAD_ARG, LOGGED_IN, BUSY, UNAVAILABLE, LAST_ADMIN, BAD_REQUEST) = range(10)
NONE, USER, ELEVATED = 0, 1, 2
HASH = '$6$saltsalt$' + 'A' * 86
HASH2 = '$6$rounds=5000$pepper$' + 'B' * 86


def check(cond, name):
    global failures
    print(('ok ' if cond else 'FAIL ') + name)
    if not cond:
        failures += 1


def s(text):
    b = text.encode()
    return struct.pack('<H', len(b)) + b


def u(n):
    return struct.pack('<I', n)


def frame(op, body=b'', verdict=ELEVATED, caller=0, rid=1, flags=0, magic=WEXA_MAGIC, version=1, body_len=None):
    return struct.pack('<IHHIIIII', magic, version, op, rid, flags, verdict, caller,
                       len(body) if body_len is None else body_len) + body


class Conn:
    def __init__(self, path):
        self.sock = socket.socket(socket.AF_UNIX, socket.SOCK_SEQPACKET)
        self.sock.settimeout(20)
        self.sock.connect(path)

    def req(self, data):
        """(status, body), or None when the helper closed the connection"""
        try:
            self.sock.send(data)
            reply = self.sock.recv(70000)
        except (ConnectionResetError, BrokenPipeError, socket.timeout):
            return None
        if not reply:
            return None
        magic, version, status, rid, blen = struct.unpack('<IHHII', reply[:16])
        assert magic == WEXA_MAGIC and blen == len(reply) - 16
        return status, reply[16:]

    def close(self):
        self.sock.close()


def parse_page(body):
    """(total, first, records) of a page of the user list"""
    total, first, count = struct.unpack_from('<III', body)
    off, out = 12, []
    for _ in range(count):
        rec, off = parse_record(body, off)
        out.append(rec)
    return total, first, out


def parse_name_page(body):
    total, first, count = struct.unpack_from('<III', body)
    off, out = 12, []
    for _ in range(count):
        (n,) = struct.unpack_from('<H', body, off)
        out.append(body[off + 2:off + 2 + n].decode())
        off += 2 + n
    return total, first, out


def parse_records(body):
    return parse_page(body)[2]


def parse_record(body, off):
    vals = []
    for _ in range(2):
        (n,) = struct.unpack_from('<H', body, off)
        vals.append(body[off + 2:off + 2 + n].decode())
        off += 2 + n
    uid, typ, locked = struct.unpack_from('<III', body, off)
    return dict(name=vals[0], full=vals[1], uid=uid, type=typ, locked=locked), off + 12


class Rig:
    def __init__(self, binary, workdir, many=0):
        self.dir = workdir
        self.binary = binary
        self.log = os.path.join(workdir, 'fake.log')
        self.helper_log = os.path.join(workdir, 'helper.log')
        self.pidfile = os.path.join(workdir, 'mainpid')
        self.sock = os.path.join(workdir, 'accounts.sock')
        self.bus = subprocess.Popen(['dbus-daemon', '--session', '--nofork', '--print-address=1'], stdout=subprocess.PIPE,
                                    text=True)
        self.address = self.bus.stdout.readline().strip()
        open(self.log, 'w').close()
        self.fake = subprocess.Popen([sys.executable, os.path.join(HERE, 'accountsd-fake-bus.py'), self.address, self.log],
                                     stdout=subprocess.PIPE, text=True, env=dict(os.environ, FAKE_MANY=str(many)))
        assert self.fake.stdout.readline().strip() == 'READY'
        self.helper = None

    def start_helper(self, s0user='root', admins='alice,bob', mainpid=None, extra=()):
        self.stop_helper()
        conf = os.path.join(self.dir, 'accountsd.conf')
        with open(conf, 'w') as f:
            f.write('session0_user=%s\nsession0_unit=wex-session0.service\nadmin_group=sudo\nbuiltin_administrator=builtin\n'
                    'sync_unit=wex-accounts-sync.service\nhelper_user=wexaccounts\n' % s0user)
        with open(self.pidfile, 'w') as f:
            f.write(str(mainpid if mainpid else os.getpid()))
        env = dict(os.environ, WEX_ACCOUNTSD_TEST='1')
        args = [self.binary, '--config', conf, '--bus', self.address, '--listen', self.sock, '--mainpid-file', self.pidfile]
        if admins is not None:
            args += ['--admin-members', admins]
        self.helper = subprocess.Popen(args + list(extra), env=env, stderr=open(self.helper_log, 'a'))
        for _ in range(100):
            if os.path.exists(self.sock):
                break
            time.sleep(0.05)
        os.chmod(self.sock, 0o666)

    def stop_helper(self):
        if self.helper:
            self.helper.terminate()
            self.helper.wait()
            self.helper = None
        if os.path.exists(self.sock):
            os.unlink(self.sock)

    def connect(self):
        return Conn(self.sock)

    def calls(self):
        with open(self.log) as f:
            return [json.loads(l) for l in f if l.strip()]

    def close(self):
        self.stop_helper()
        for p in (self.fake, self.bus):
            p.terminate()
            p.wait()


def tests_operations(rig):
    c = rig.connect()
    # verdict classes
    check(c.req(frame(LIST, verdict=NONE)) == (DENIED, b''), 'no caller: a read is refused')
    check(c.req(frame(CREATE, s('x1') + s('X') + u(0) + s(HASH), verdict=USER))[0] == DENIED, 'a user verdict cannot change')
    check(c.req(frame(SYNC, verdict=USER))[0] == DENIED, 'a user verdict cannot start the sync')
    check(c.req(frame(LIST, verdict=7))[0] == DENIED, 'an unknown verdict is refused')

    # reads
    st, body = c.req(frame(LIST, verdict=USER))
    names = sorted(r['name'] for r in parse_records(body))
    check(st == OK and names == ['alice', 'bob', 'builtin', 'carol', 'dave'], 'list: the users in range, not the system account (%s)' % names)
    st, body = c.req(frame(GET, s('alice'), verdict=USER))
    rec, _ = parse_record(body, 0)
    check(st == OK and rec['uid'] == 1001 and rec['type'] == 1 and rec['locked'] == 0 and rec['full'] == 'Alice A', 'get: alice')
    check(c.req(frame(GET, s('bob'), verdict=USER))[0] == OK, 'get: bob (locked)')
    check(c.req(frame(GET, s('svc'), verdict=USER))[0] == NOT_FOUND, 'get: a system account is not found')
    check(c.req(frame(GET, s('root'), verdict=USER))[0] == NOT_FOUND, 'get: root is not found')
    check(c.req(frame(GET, s('nobody-here'), verdict=USER))[0] == NOT_FOUND, 'get: an unknown name')
    st, body = c.req(frame(ADMINS, verdict=USER))
    check(st == OK and body.count(b'alice') == 1 and body.startswith(u(2)), 'admin members')

    # create
    st, body = c.req(frame(CREATE, s('eve') + s('Eve E') + u(0) + s(HASH)))
    rec = parse_record(body, 0)[0] if st == OK else None
    check(st == OK and rec['name'] == 'eve' and rec['type'] == 0, 'create: a standard user')
    calls = rig.calls()
    check(any(x['op'] == 'CreateUser' and x['user'] == 'eve' and x['type'] == 0 for x in calls), 'create: CreateUser as a standard user')
    check(any(x['op'] == 'SetPassword' and x['user'] == 'eve' and x['value'] == HASH for x in calls), 'create: SetPassword with the hash')
    st, body = c.req(frame(CREATE, s('frank') + s('Frank') + u(1) + s(HASH2)))
    check(st == OK and any(x['op'] == 'SetAccountType' and x['user'] == 'frank' and x['value'] == 1 for x in rig.calls()),
          'create: an administrator gets the type set')
    check(c.req(frame(CREATE, s('eve') + s('Eve') + u(0) + s(HASH)))[0] == EXISTS, 'create: an existing name')
    for bad, why in (('Root', 'upper case'), ('1abc', 'a digit first'), ('sudo', 'reserved'), ('root', 'reserved'),
                     ('daemon', 'reserved'), ('wexaccounts', 'reserved'), ('a' * 33, 'too long'), ('a b', 'a space'),
                     ('', 'empty')):
        check(c.req(frame(CREATE, s(bad) + s('X') + u(0) + s(HASH)))[0] == BAD_ARG, 'create: refuses the name (%s)' % why)
    check(c.req(frame(CREATE, s('adm') + s('X') + u(0) + s(HASH)))[0] == EXISTS, 'create: a name a group has')
    for bad, why in (('$5$saltsalt$' + 'A' * 86, 'not SHA-512'), ('$6$saltsalt$' + 'A' * 85, 'a short digest'),
                     ('$6$' + 'a' * 17 + '$' + 'A' * 86, 'a long salt'), ('$6$rounds=10$salt$' + 'A' * 86, 'too few rounds'),
                     ('$6$salt$' + 'A' * 85 + '!', 'a bad character'), ('plain', 'a plain password'), ('', 'empty'),
                     ('$6$salt$' + 'A' * 86 + 'x', 'a long digest')):
        check(c.req(frame(CREATE, s('gina') + s('G') + u(0) + s(bad)))[0] == BAD_ARG, 'create: refuses the hash (%s)' % why)
    check(c.req(frame(CREATE, s('gina') + s('G:G') + u(0) + s(HASH)))[0] == BAD_ARG, 'create: a full name with a colon')
    check(c.req(frame(CREATE, s('gina') + s('G,G') + u(0) + s(HASH)))[0] == BAD_ARG, 'create: a full name with a comma')
    check(c.req(frame(CREATE, s('gina') + s('G') + u(2) + s(HASH)))[0] == BAD_ARG, 'create: an unknown type')

    # delete
    check(c.req(frame(DELETE, s('dave')))[0] == LOGGED_IN, 'delete: a logged-in user')
    check(c.req(frame(DELETE, s('alice'), caller=1001))[0] == DENIED, "delete: the caller's own account")
    check(c.req(frame(DELETE, s('builtin')))[0] == DENIED, 'delete: the built-in administrator')
    check(c.req(frame(DELETE, s('svc')))[0] == DENIED, 'delete: a system account')
    check(c.req(frame(DELETE, s('alice')))[0] == LAST_ADMIN, 'delete: the last administrator (bob is locked)')
    check(c.req(frame(DELETE, s('nobody-here')))[0] == NOT_FOUND, 'delete: an unknown user')
    check(c.req(frame(DELETE, s('eve')))[0] == OK, 'delete: a user')
    check(any(x['op'] == 'DeleteUser' and x['remove'] is False for x in rig.calls()), 'delete: files are never removed')

    # rename, full name, lock, type, password
    check(c.req(frame(RENAME, s('carol') + s('root')))[0] == BAD_ARG, 'rename: to a reserved name')
    check(c.req(frame(RENAME, s('carol') + s('adm')))[0] == EXISTS, 'rename: to a name a group has')
    check(c.req(frame(RENAME, s('dave') + s('dave2')))[0] == LOGGED_IN, 'rename: a logged-in user')
    check(c.req(frame(RENAME, s('builtin') + s('b2')))[0] == DENIED, 'rename: the built-in administrator')
    check(c.req(frame(RENAME, s('carol') + s('carol2')))[0] == OK, 'rename: a user')
    check(c.req(frame(FULLNAME, s('carol2') + s('Carol Two')))[0] == OK, 'full name: set')
    check(c.req(frame(FULLNAME, s('carol2') + s('a:b')))[0] == BAD_ARG, 'full name: a colon')
    check(c.req(frame(LOCKED, s('carol2') + u(1)))[0] == OK, 'lock: a user')
    check(c.req(frame(LOCKED, s('carol2') + u(0)))[0] == OK, 'unlock: a user')
    check(c.req(frame(LOCKED, s('alice') + u(1), caller=1001))[0] == DENIED, 'lock: the caller itself')
    check(c.req(frame(LOCKED, s('alice') + u(1)))[0] == LAST_ADMIN, 'lock: the last administrator')
    check(c.req(frame(LOCKED, s('bob') + u(0)))[0] == OK, 'unlock: bob')
    check(c.req(frame(LOCKED, s('alice') + u(1)))[0] == OK, 'lock: an administrator when another is active')
    check(c.req(frame(LOCKED, s('alice') + u(0)))[0] == OK, 'unlock: alice')
    check(c.req(frame(LOCKED, s('carol2') + u(2)))[0] == BAD_REQUEST, 'lock: a bad flag')
    check(c.req(frame(TYPE, s('carol2') + u(1)))[0] == OK, 'type: promote')
    check(c.req(frame(TYPE, s('carol2') + u(0)))[0] == OK, 'type: demote')
    check(c.req(frame(TYPE, s('alice') + u(0), caller=1001))[0] == DENIED, 'type: the caller cannot demote itself')
    check(c.req(frame(TYPE, s('builtin') + u(0)))[0] == DENIED, 'type: the built-in administrator')
    check(c.req(frame(PASSWORD, s('carol2') + s(HASH2)))[0] == OK, 'password: a hash')
    check(c.req(frame(PASSWORD, s('carol2') + s('hunter2')))[0] == BAD_ARG, 'password: a plain password is refused')
    check(c.req(frame(PASSWORD, s('svc') + s(HASH)))[0] == DENIED, 'password: a system account')

    # the sync
    check(c.req(frame(SYNC))[0] == OK, 'sync: started')
    check(any(x['op'] == 'StartUnit' and x['unit'] == 'wex-accounts-sync.service' and x['mode'] == 'replace'
              for x in rig.calls()), 'sync: exactly the sync unit, mode replace')

    c.close()

    # frames: each one closes the connection (the helper serves one connection at a time: a fresh one for each)
    def closed(data, why):
        cc = rig.connect()
        r = cc.req(data)
        cc.close()
        check(r is None, 'frame: ' + why + ' closes the connection')

    closed(frame(LIST, magic=0x12345678), 'a bad magic')
    closed(frame(LIST, version=2), 'an unknown version')
    closed(b'WEXA', 'a short frame')
    closed(frame(CREATE, s('x') + s('y'), body_len=999), 'a body length that does not match')
    closed(frame(LIST) + b'extra', 'trailing bytes')
    closed(frame(GET, b'\x00' * 70000), 'an oversized frame')
    c = rig.connect()
    r = c.req(frame(99))
    check(r is not None and r[0] == BAD_REQUEST, 'frame: an unknown operation is refused, the connection stays')
    r = c.req(frame(LIST, flags=1, verdict=USER))
    check(r is not None and r[0] == BAD_REQUEST, 'frame: an unknown flag is refused')
    r = c.req(frame(GET, s('x') + b'junk', verdict=USER))
    check(r is not None and r[0] == BAD_REQUEST, 'frame: trailing bytes in a body are refused')
    r = c.req(frame(GET, struct.pack('<H', 300) + b'a' * 300, verdict=USER))
    check(r is not None and r[0] == BAD_REQUEST, 'frame: a string over 255 bytes')
    r = c.req(frame(GET, struct.pack('<H', 3) + b'a\x00b', verdict=USER))
    check(r is not None and r[0] == BAD_REQUEST, 'frame: a NUL inside a string')
    c.close()


def tests_peer(rig, binary):
    # a program that is not the main process of the unit: refused at accept, nothing read
    rig.start_helper(mainpid=1)
    c = rig.connect()
    check(c.req(frame(LIST, verdict=USER)) is None, 'peer: not the main process of the unit is dropped')
    c.close()
    rig.start_helper(s0user='nobody')
    c = rig.connect()
    check(c.req(frame(LIST, verdict=USER)) is None, "peer: another uid than session 0's account is dropped")
    c.close()
    # a child of uid nobody: dumpable (its /proc directory is its own) is refused, non-dumpable is accepted
    for dumpable, want, why in ((True, False, 'a dumpable process of the account'), (False, True, 'a non-dumpable process of the account')):
        rig.start_helper(s0user='nobody')
        rd, wr = os.pipe()
        pid = os.fork()
        if pid == 0:
            os.close(wr)
            os.setgroups([])
            os.setgid(65534)
            os.setuid(65534)
            # a change of uid clears the dumpable flag: set it as the program wants it
            ctypes.CDLL(None).prctl(4, 1 if dumpable else 0, 0, 0, 0)
            os.read(rd, 1)
            try:
                cc = Conn(rig.sock)
                r = cc.req(frame(LIST, verdict=USER))
                os._exit(0 if r is not None else 3)
            except Exception:
                os._exit(4)
        os.close(rd)
        with open(rig.pidfile, 'w') as f:
            f.write(str(pid))
        os.write(wr, b'x')
        _, status = os.waitpid(pid, 0)
        check(os.WEXITSTATUS(status) == (0 if want else 3), 'peer: ' + why + (' is served' if want else ' is dropped'))
    rig.start_helper()


def tests_findings(binary, tmp):
    """The reviewer's findings F1 (paging), F2 (rollback), F5 (log injection), F6 (the timeout) and the group-file nit"""
    # F1: more users than one frame holds, with long full names: every page is whole, the pages cover the list
    d = os.path.join(tmp, 'many')
    os.makedirs(d)
    os.chmod(d, 0o755)
    big = Rig(binary, d, many=400)
    try:
        big.start_helper()
        c = big.connect()
        seen, start, pages, ok = [], 0, 0, True
        while pages < 50:
            r = c.req(frame(LIST, u(start), verdict=USER))
            if r is None or r[0] != OK or len(r[1]) > 65520:
                ok = False
                break
            total, first, recs = parse_page(r[1])
            if first != start or (not recs and start < total):
                ok = False
                break
            seen += [x['name'] for x in recs]
            start += len(recs)
            pages += 1
            if start >= total:
                break
        check(ok and pages > 1 and len(seen) == len(set(seen)) == total == 405,
              'F1: the list of 405 users with long names comes in %d whole pages (%d seen)' % (pages, len(seen)))
        r = c.req(frame(LIST, u(100000), verdict=USER))
        check(r is not None and r[0] == OK and parse_page(r[1])[0] == 405 and parse_page(r[1])[2] == [], 'F1: a start past the end is an empty page')
        check(c.req(frame(LIST, b'abc', verdict=USER))[0] == BAD_REQUEST, 'F1: a start index that is not a u32 is refused')
        c.close()
        # the administrators: 1024 names of 63 characters do not fit in one frame
        names = ['a%03d' % i + 'x' * 58 for i in range(1024)]
        big.start_helper(admins=','.join(names))
        c = big.connect()
        seen, start, pages = [], 0, 0
        while pages < 10:
            r = c.req(frame(ADMINS, u(start), verdict=USER))
            if r is None or r[0] != OK:
                break
            total, first, got = parse_name_page(r[1])
            seen += got
            start += len(got)
            pages += 1
            if start >= total or not got:
                break
        check(pages > 1 and seen == names, 'F1: 1024 administrators come in %d whole pages' % pages)
        c.close()
    finally:
        big.close()

    d = os.path.join(tmp, 'findings')
    os.makedirs(d)
    os.chmod(d, 0o755)
    rig = Rig(binary, d)
    try:
        rig.start_helper()
        c = rig.connect()
        # F2: a create that cannot be finished takes the user away again, and reports the failure
        for method, typ, name in (('SetPassword', 0, 'zed'), ('SetAccountType', 1, 'zed2')):
            with open(rig.log + '.fail', 'w') as f:
                f.write(method)
            r = c.req(frame(CREATE, s(name) + s('Z') + u(typ) + s(HASH)))
            os.unlink(rig.log + '.fail')
            check(r is not None and r[0] not in (OK, DENIED), 'F2: create fails when %s fails (status %s)' % (method, r and r[0]))
            check(c.req(frame(GET, s(name), verdict=USER))[0] == NOT_FOUND, 'F2: the half-made user is gone after %s failed' % method)
            calls = rig.calls()
            check(any(x['op'] == 'CreateUser' and x['user'] == name for x in calls) and
                  any(x['op'] == 'DeleteUser' and x['remove'] is False for x in calls), 'F2: it was deleted again, files kept (%s)' % method)
        check(c.req(frame(CREATE, s('zed') + s('Z') + u(0) + s(HASH)))[0] == OK, 'F2: the name is usable again after the rollback')
        # F5: a name with a newline cannot forge a journal line
        c.req(frame(GET, s('x\nwex-accountsd: FORGED op 4 verdict 2'), verdict=USER))
        c.req(frame(DELETE, s('y\x1b[31m\nwex-accountsd: FORGED2')))
        c.close()
        rig.stop_helper()
        with open(rig.helper_log, errors='replace') as f:
            text = f.read()
        check('FORGED' not in text and '\x1b' not in text, 'F5: control characters and forged lines never reach the journal')
        check("user '<invalid>'" in text, 'F5: an invalid name is logged as <invalid>')
        # F6: a call that takes longer than the helper's wait is answered in about 10 s, not 25
        rig.start_helper()
        c = rig.connect()
        open(rig.log + '.slow', 'w').close()
        t0 = time.time()
        r = c.req(frame(FULLNAME, s('carol') + s('Slow One')))
        took = time.time() - t0
        os.unlink(rig.log + '.slow')
        check(r is not None and r[0] in (BUSY, UNAVAILABLE) and 8 <= took <= 14, 'F6: a slow call fails after about 10 s (%.1f s, status %s)' % (took, r and r[0]))
        c.close()
        time.sleep(4)   # the fake finishes its slow call
        # the nit: the administrators come from the group and passwd files themselves, primary-group users included
        gf, pf = os.path.join(d, 'group'), os.path.join(d, 'passwd')
        with open(gf, 'w') as f:
            f.write('adm:x:4:\nsudo:x:27:alice,bob\n')
        with open(pf, 'w') as f:
            f.write('primary1:x:2001:27::/:/bin/false\nalice:x:1001:1001::/:/bin/false\nother:x:2002:100::/:/bin/false\n')
        rig.start_helper(admins=None, extra=['--group-file', gf, '--passwd-file', pf])
        c = rig.connect()
        r = c.req(frame(ADMINS, verdict=USER))
        got = sorted(parse_name_page(r[1])[2]) if r and r[0] == OK else None
        check(got == ['alice', 'bob', 'primary1'], 'nit: administrators from the group and passwd files, primary group included (%s)' % got)
        c.close()
    finally:
        rig.close()


def main():
    binary = sys.argv[1] if len(sys.argv) > 1 else None
    tmp = tempfile.mkdtemp(prefix='accountsd-')
    os.chmod(tmp, 0o755)   # the peer tests connect as another user
    if not binary:
        binary = os.path.join(tmp, 'wex-accountsd')
        subprocess.check_call(['gcc', '-O2', '-Wall', '-Wextra', '-Werror', '-DWEX_ACCOUNTSD_LAB_TEST', '-o', binary,
                               os.path.join(ROOT, 'tools/wex-accountsd/wex-accountsd.c'), '-lsystemd'])
    rig = Rig(binary, tmp)
    try:
        rig.start_helper()
        tests_operations(rig)
        # the hash never reaches the helper's log, and the sensitive calls are logged without it
        rig.stop_helper()
        with open(rig.helper_log) as f:
            text = f.read()
        check(HASH not in text and HASH2 not in text and 'AAAAAAAAAA' not in text, 'the hash is never in the helper log')
        check("op 4 verdict 2" in text and "user 'eve'" in text, 'the helper logs a line per request')
        tests_peer(rig, binary)
        tests_findings(binary, tmp)
    finally:
        rig.close()
    print('%d failure(s)' % failures if failures else 'accountsd=PASS')
    return 1 if failures else 0


if __name__ == '__main__':
    sys.exit(main())

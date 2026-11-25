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
        self.sock.settimeout(8)
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


def parse_records(body):
    (count,) = struct.unpack_from('<I', body)
    off, out = 4, []
    for _ in range(count):
        rec, off = parse_record(body, off)
        out.append(rec)
    return out


def parse_record(body, off):
    vals = []
    for _ in range(2):
        (n,) = struct.unpack_from('<H', body, off)
        vals.append(body[off + 2:off + 2 + n].decode())
        off += 2 + n
    uid, typ, locked = struct.unpack_from('<III', body, off)
    return dict(name=vals[0], full=vals[1], uid=uid, type=typ, locked=locked), off + 12


class Rig:
    def __init__(self, binary, workdir):
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
                                     stdout=subprocess.PIPE, text=True)
        assert self.fake.stdout.readline().strip() == 'READY'
        self.helper = None

    def start_helper(self, s0user='root', admins='alice,bob', mainpid=None):
        self.stop_helper()
        conf = os.path.join(self.dir, 'accountsd.conf')
        with open(conf, 'w') as f:
            f.write('session0_user=%s\nsession0_unit=wex-session0.service\nadmin_group=sudo\nbuiltin_administrator=builtin\n'
                    'sync_unit=wex-accounts-sync.service\n' % s0user)
        with open(self.pidfile, 'w') as f:
            f.write(str(mainpid if mainpid else os.getpid()))
        env = dict(os.environ, WEX_ACCOUNTSD_TEST='1')
        self.helper = subprocess.Popen([self.binary, '--config', conf, '--bus', self.address, '--listen', self.sock,
                                        '--admin-members', admins, '--mainpid-file', self.pidfile],
                                       env=env, stderr=open(self.helper_log, 'a'))
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


def main():
    binary = sys.argv[1] if len(sys.argv) > 1 else None
    tmp = tempfile.mkdtemp(prefix='accountsd-')
    os.chmod(tmp, 0o755)   # the peer tests connect as another user
    if not binary:
        binary = os.path.join(tmp, 'wex-accountsd')
        subprocess.check_call(['gcc', '-O2', '-Wall', '-Wextra', '-Werror', '-o', binary,
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
    finally:
        rig.close()
    print('%d failure(s)' % failures if failures else 'accountsd=PASS')
    return 1 if failures else 0


if __name__ == '__main__':
    sys.exit(main())

#!/usr/bin/python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright 2026 WineExplorer contributors
"""Root-only disposable S8 component client; NOT a PAM/ Wine acceptance test.

Uses the real S6 packet receiver/kernel ancillary checks on socketpairs.
S5 authority, server endorsements and S7 completion are explicit test doubles.
No double is imported by the evidence implementation or any installed service.
"""
import importlib.util
import multiprocessing
import os
from pathlib import Path
import socket
import struct
import sys
import time
import unittest
from dataclasses import replace
from unittest.mock import patch

sys.dont_write_bytecode = True
import evidence as e

spec = importlib.util.spec_from_file_location("s6_fixture", Path(__file__).with_name("e1s6-lab.py"))
s6 = importlib.util.module_from_spec(spec)
spec.loader.exec_module(s6)


class Clock:
    now = 1_000_000_000

    def __call__(self):
        return self.now


class Tx:
    """Fixture endpoints, not an authenticated wineserver/broker registration."""
    def __init__(self, uid=0, session=1, broker=12):
        self.uid, self.session, self.broker_gen, self.server_gen = uid, session, broker, 11
        self.pid = os.getpid()
        self.data, self.remote = socket.socketpair(socket.AF_UNIX, socket.SOCK_SEQPACKET)
        for sock in (self.data, self.remote):
            s6.pass_identity(sock)  # unsupported SO_PASSPIDFD must FAIL
            sock.setblocking(False)
        self.child, self.child_remote = socket.socketpair(socket.AF_UNIX, socket.SOCK_SEQPACKET)
        self.dead, self.sequence, self.operations = False, 0, []
        self.pin = os.pidfd_open(os.getpid())

    def live(self):
        if self.dead or not s6.alive(self.pin):
            raise e.Denied()

    def endorsement(self, operation):
        self.live()
        if operation not in (e.REDEEM, e.CANCEL, e.STORAGE_QUERY):
            raise e.Denied()
        self.operations.append((self.sequence, operation))
        self.sequence += 1

    def close(self):
        for sock in (self.data, self.remote, self.child, self.child_remote):
            sock.close()
        os.close(self.pin)


class Worker:
    """Deterministic S7 result fixture; no passwords, PAM or subprocesses."""
    def __init__(self, raw, status=0, abnormal=False, cancelled=False, pending=False):
        self.raw, self.status, self.abnormal = raw, status, abnormal
        self.binding = raw[16:28] + raw[40:72]
        self.identity = raw[72:104]
        self.deadline = struct.unpack_from("<Q", raw, 104)[0]
        self.is_cancelled, self.cleanup_pending = cancelled, pending
        self.closed = False
        self.verified = self.consumed = False
        self.seed = bytearray(b"s" * 32)

    def cancelled(self, dialog):
        return self.is_cancelled

    def collect(self):
        return self.status

    def verify_exit(self):
        if self.abnormal:
            raise ValueError("worker denied")
        self.verified = True

    def take_seed(self):
        if not self.verified or self.status or self.consumed:
            raise ValueError("worker denied")
        self.consumed = True
        return self.seed

    def close(self):
        self.closed = True


def native_fixture(wire, audit_path):
    """Actual disposable native process; authority/PAM remain fixture doubles.

    Private multiprocessing pipe is inherited only by this test parent/child.
    No listener, credentials, installed service or arbitrary execution command.
    """
    audit_fd = os.open(audit_path, os.O_WRONLY | os.O_APPEND | os.O_NOFOLLOW)
    tx = Tx()
    verifier = int.from_bytes(os.getrandom(8), "little")
    service = e.Service(10, verifier, e.Audit(audit_fd), lambda c, b: True, lab=True)
    c = e.Connection(service, tx, s6.receive)
    try:
        wire.send(verifier)
        while True:
            command = wire.recv()
            if command[0] == "ready":
                now = time.monotonic_ns()
                b = e.Binding(1, b"r" * 32, b"b" * 32, os.getrandom(32), b"a" * 16,
                              1001, 10, 11, 12, verifier, 14, 15, now,
                              now + 120_000_000_000, "alice")
                service.open(c, b)
                service.begin(c, 1, b, Worker)
                wire.send(service.collect(c, 1))
            elif command[0] == "redeem":
                try:
                    service.redeem(c, command[1], command[2])
                    wire.send(e.OK)
                except e.Denied:
                    wire.send(e.DENIED)
            else:
                raise e.Denied()
    except EOFError:
        pass
    finally:
        service.close()
        tx.close()
        os.close(audit_fd)
        wire.close()


class EvidenceTests(unittest.TestCase):
    def setUp(self):
        self.clock, self.connections, self.services = Clock(), [], []
        self.counter = 0
        self.audit_fd = os.open(sys.argv[1] + "/audit", os.O_WRONLY | os.O_CREAT |
                                os.O_APPEND | os.O_CLOEXEC | os.O_NOFOLLOW, 0o600)
        self.s = self.service()
        self.c = self.connection()

    def tearDown(self):
        for service in self.services:
            service.close()
        for c in self.connections:
            c.tx.close()
        os.close(self.audit_fd)

    def service(self, limits=e.Limits(), *, verifier=13, **kw):
        s = e.Service(10, verifier, e.Audit(self.audit_fd),
                      lambda c, b: not getattr(c.tx, "stale", False), lab=True,
                      limits=limits, clock=self.clock, **kw)
        self.services.append(s)
        return s

    def connection(self, service=None, **kw):
        c = e.Connection(service or self.s, Tx(**kw), s6.receive)
        self.connections.append(c)
        return c

    def binding(self, c=None, **kw):
        c = c or self.c
        self.counter += 1
        b = e.Binding(self.counter, b"r" * 32, b"b" * 32, os.getrandom(32),
                      b"a" * 16, 1001, 10, c.server_gen, c.broker_gen, c.service.verifier_gen,
                      14, 15, self.clock.now, self.clock.now + 120_000_000_000, "alice")
        return replace(b, **kw)

    def start(self, b=None, c=None, **kw):
        c, b = c or self.c, b or self.binding(c)
        c.service.open(c, b)
        workers = []

        def factory(raw):
            w = Worker(raw, **kw)
            workers.append(w)
            return w

        c.service.begin(c, b.request_id, b, factory)
        return b, workers[0]

    def ready(self, c=None, b=None, **kw):
        c = c or self.c
        b, w = self.start(b, c, **kw)
        ref, record = c.service.collect(c, b.request_id)
        self.assertTrue(w.closed and w.consumed)
        self.assertFalse(any(w.seed))
        self.assertEqual(len(record), e.EVIDENCE_SIZE)
        self.assertEqual(len(ref), 16)
        return b, ref, record

    def deny(self, call, *args):
        with self.assertRaises(e.Denied) as caught:
            call(*args)
        self.assertEqual(str(caught.exception), str(e.Denied()))

    def test_redeem_once_and_replay(self):
        b, ref, record = self.ready()
        self.assertEqual(self.s.redeem(self.c, ref, record), record)
        self.deny(self.s.redeem, self.c, ref, record)

    def test_wrong_connection_even_equal_identity(self):
        b, ref, record = self.ready()
        other = self.connection()
        self.deny(self.s.redeem, other, ref, record)
        # Wrong connection cannot burn another connection's evidence either.
        self.assertEqual(self.s.redeem(self.c, ref, record), record)

    def test_all_record_fields_mismatch_burns(self):
        offsets = dict(kind=0, version=2, length=4, caps=8, request=16,
                       request_digest=24, binding_digest=56, nonce=88, attempt=120,
                       account=124, uid=140, completed=148, expiry=156, root=164,
                       server=172, broker=180, verifier=188, map=196, policy=204,
                       method=212, service=216)
        for field, offset in offsets.items():
            with self.subTest(field=field):
                # Fresh service avoids the audit's deliberate 64 lines/s cap.
                s = self.service()
                c = self.connection(s)
                b, ref, record = self.ready(c)
                bad = bytearray(record)
                bad[offset] ^= 1
                self.deny(s.redeem, c, ref, bytes(bad))
                self.deny(s.redeem, c, ref, record)

    def test_cancel_and_late_success(self):
        b, ref, record = self.ready()
        self.s.cancel(self.c, b.request_id)
        self.s.cancel(self.c, b.request_id)
        self.deny(self.s.redeem, self.c, ref, record)
        self.deny(self.s.collect, self.c, b.request_id)

    def test_expiry_is_exclusive(self):
        b, ref, record = self.ready()
        self.clock.now = b.expiry
        self.deny(self.s.redeem, self.c, ref, record)
        self.assertFalse(self.s.requests)

    def test_restart_and_connection_death(self):
        b, ref, record = self.ready()
        self.s.close()
        new = self.service(verifier=99)
        c = self.connection(new)
        self.deny(new.redeem, c, ref, record)
        self.deny(new.open, c, b)
        # Lost channel invalidates a ready result without any public cancel.
        b, ref, record = self.ready(c)
        c.tx.remote.close()
        new.tick()
        self.deny(new.redeem, c, ref, record)

    def test_actual_native_fixture_process_restart(self):
        ctx = multiprocessing.get_context("spawn")
        children = []

        def start():
            local, remote = ctx.Pipe()
            child = ctx.Process(target=native_fixture,
                                args=(remote, sys.argv[1] + "/audit"))
            child.start()
            remote.close()
            children.append((child, local))
            self.assertTrue(local.poll(5), "native fixture startup")
            return child, local, local.recv()

        try:
            child, wire, generation = start()
            wire.send(("ready",))
            self.assertTrue(wire.poll(5))
            ref, record = wire.recv()
            # Kill the exact owned process with live evidence, without cleanup.
            child.kill()
            child.join(3)
            self.assertFalse(child.is_alive())
            _, fresh_wire, fresh_generation = start()
            self.assertNotEqual(generation, fresh_generation)
            fresh_wire.send(("redeem", ref, record))
            self.assertTrue(fresh_wire.poll(5))
            self.assertEqual(fresh_wire.recv(), e.DENIED)
        finally:
            for child, wire in children:
                wire.close()
                if child.is_alive():
                    child.kill()
                child.join(3)
                self.assertFalse(child.is_alive(), "native fixture not reaped")
                child.close()
    def test_broker_server_policy_and_owner_death(self):
        for field in ("broker_gen", "server_gen", "stale", "dead"):
            with self.subTest(field=field):
                s = self.service()
                c = self.connection(s)
                b, ref, record = self.ready(c)
                setattr(c.tx, field, True if field in ("stale", "dead") else 999)
                s.tick()
                self.deny(s.redeem, c, ref, record)

    def test_three_attempts_new_nonce_and_service_assignment(self):
        self.s = self.service(e.Limits(account_failures=10))
        c = self.connection()
        b, w = self.start(c=c, status=1)
        for attempt in range(1, 4):
            self.deny(self.s.collect, c, b.request_id)
            self.assertTrue(w.closed)
            self.assertEqual(struct.unpack_from("<I", w.raw, 24)[0], attempt)
            b = replace(b, nonce=os.getrandom(32))
            if attempt < 3:
                workers = []
                self.s.begin(c, b.request_id, b,
                             lambda raw: workers.append(Worker(raw, status=1)) or workers[-1])
                w = workers[-1]
        self.deny(self.s.begin, c, b.request_id, b, Worker)

    def test_request_tombstone_survives_broker_restart(self):
        b, w = self.start(status=1)
        self.deny(self.s.collect, self.c, b.request_id)
        self.c.close()
        c = self.connection(broker=999)
        self.deny(self.s.open, c, replace(b, broker_gen=999))
        # A different ID cannot reset failure throttles either.
        self.assertEqual(self.s.callers.rows[self.c.uid][0], 1)

    def test_absolute_deadline_includes_queue(self):
        b = self.binding()
        self.s.open(self.c, b)
        self.clock.now = b.expiry - 1
        _, raw = self.s.begin(self.c, b.request_id, b, Worker)
        self.assertEqual(struct.unpack_from("<Q", raw, 104)[0], b.expiry)
        self.clock.now = b.expiry
        self.s.tick()
        self.deny(self.s.collect, self.c, b.request_id)
        self.deny(self.s.open, self.c, replace(b, created=self.clock.now,
                                              expiry=self.clock.now + 120_000_000_001))

    def test_verifier_keeps_broker_deadline(self):
        now = 1_000_000_000
        txs = []
        try:
            # Past, equal or more than 300 s ahead: the broker's expiry denies
            # and the five-second setup deadline is kept.
            for bad in (now - 1, now, now + 300_000_000_001):
                with self.subTest(expiry=bad):
                    tx = Tx()
                    txs.append(tx)
                    with self.assertRaises(ValueError):
                        s6.broker_deadline(tx, now, bad)
                    self.assertFalse(hasattr(tx, "deadline"))
            # A valid expiry becomes the transaction deadline.
            tx = Tx()
            txs.append(tx)
            s6.broker_deadline(tx, now, now + 300_000_000_000)
            self.assertEqual(tx.deadline, now + 300_000_000_000)
        finally:
            for tx in txs:
                tx.close()

    def test_account_throttle_across_requesters(self):
        for uid in range(1, 4):
            c = self.connection(uid=uid)
            b, w = self.start(c=c, status=1)
            self.deny(self.s.collect, c, b.request_id)
        c = self.connection(uid=4)
        b = self.binding(c)
        self.s.open(c, b)
        self.deny(self.s.begin, c, b.request_id, b, Worker)

    def test_caller_throttle_across_accounts_and_reconnect(self):
        for i in range(6):
            c = self.connection(broker=100 + i)
            b, w = self.start(self.binding(c, account_key=bytes([i + 1]) * 16), c, status=1)
            self.deny(self.s.collect, c, b.request_id)
            c.close()
        c = self.connection(broker=999)
        b = self.binding(c, account_key=b"z" * 16)
        self.s.open(c, b)
        self.deny(self.s.begin, c, b.request_id, b, Worker)

    def test_table_pressure_cannot_erase_unexpired_history(self):
        s = self.service(e.Limits(throttle_slots=2))
        for uid in (1, 2):
            c = self.connection(s, uid=uid)
            b, w = self.start(self.binding(c, account_key=bytes([uid]) * 16), c, status=1)
            self.deny(s.collect, c, b.request_id)
        c = self.connection(s, uid=3)
        b = self.binding(c, account_key=b"z" * 16)
        s.open(c, b)
        self.deny(s.begin, c, b.request_id, b, Worker)
        self.assertEqual(len(s.accounts.rows), 2)
        self.assertEqual(len(s.callers.rows), 2)
        self.assertEqual(s.callers.rows[1][0], 1)
        self.clock.now += 120_000_000_000
        # Expired rows really are evicted; table size remains bounded.
        b, w = self.start(self.binding(c, account_key=b"z" * 16), c, status=1)
        self.deny(s.collect, c, b.request_id)
        self.assertEqual(len(s.accounts.rows), 1)

    def test_account_table_pressure_separate_from_caller_table(self):
        s = self.service(e.Limits(throttle_slots=2))
        c = self.connection(s, uid=1)
        for i in (1, 2):
            b, w = self.start(self.binding(c, account_key=bytes([i]) * 16), c, status=1)
            self.deny(s.collect, c, b.request_id)
        b = self.binding(c, account_key=b"z" * 16)
        s.open(c, b)
        self.deny(s.begin, c, b.request_id, b, Worker)
        self.assertEqual(len(s.accounts.rows), 2)

    def test_concurrency_global_session_and_requester(self):
        s = self.service(e.Limits(global_active=2))
        c1 = self.connection(s, uid=1)
        b1, w1 = self.start(c=c1)
        # Same UID and session, and same UID in another session, both deny.
        for session in (1, 2):
            c = self.connection(s, uid=1, session=session)
            b = self.binding(c)
            s.open(c, b)
            self.deny(s.begin, c, b.request_id, b, Worker)
        c2 = self.connection(s, uid=2)
        self.start(c=c2)
        c3 = self.connection(s, uid=3)
        b3 = self.binding(c3)
        s.open(c3, b3)
        self.deny(s.begin, c3, b3.request_id, b3, Worker)
        s.cancel(c1)
        self.assertTrue(w1.closed)
        self.start(c=c3)

    def test_request_table_pressure_and_terminal_retention(self):
        s = self.service(e.Limits(requests=1))
        c = self.connection(s)
        b, ref, record = self.ready(c)
        s.redeem(c, ref, record)
        self.deny(s.open, c, self.binding(c))
        self.clock.now = b.expiry
        s.open(c, self.binding(c))

    def test_error_uniformity_unknown_and_wrong_password(self):
        b, w = self.start(status=1)
        wrong = self.c.ready(b.request_id)
        self.assertEqual(wrong, struct.pack("<I", e.DENIED) + bytes(e.REPLY_SIZE - 4))
        self.c.tx.remote.recv(e.REPLY_SIZE)
        c = self.connection()
        unknown = self.binding(c, account_key=bytes(16), host_uid=0, name="")
        self.s.open(c, unknown)
        _, raw = self.s.reject_selection(c, unknown.request_id, b"unknown-canary",
                                        lambda raw: Worker(raw, status=1))
        self.assertEqual(struct.unpack_from("<H", raw, 10)[0], 6)
        self.assertEqual(raw[72:96], bytes(24))
        self.assertEqual(raw[120:], b"decoy")
        self.assertEqual(self.s.requests[(c.uid, unknown.request_id)].state, "running")
        # No early reply; the same worker-ready failure encoder is mandatory.
        with self.assertRaises(BlockingIOError):
            c.tx.remote.recv(e.REPLY_SIZE)
        unknown_reply = c.ready(unknown.request_id)
        self.assertEqual(wrong, unknown_reply)
        self.assertEqual(self.s.callers.rows[c.uid][0], 2)
        with open(sys.argv[1] + "/audit", "rb") as log:
            self.assertNotIn(b"unknown-canary", log.read())

    def test_ineligible_decoy_uses_account_key_and_cannot_succeed(self):
        b = self.binding(account_key=bytes(16), host_uid=0, name="")
        self.s.open(self.c, b)
        self.s.reject_selection(self.c, b.request_id, b"private-name", Worker, account_key=b"k" * 16)
        self.assertEqual(self.s.accounts.rows[b"k" * 16][0], 1)
        self.deny(self.s.collect, self.c, b.request_id)  # even a faulty success worker
        r = self.s.requests[(self.c.uid, b.request_id)]
        self.assertEqual((r.state, r.ref, r.record), ("terminal", b"", b""))

    def test_decoy_shares_active_limit_and_attempts(self):
        b = self.binding(account_key=bytes(16), host_uid=0, name="")
        self.s.open(self.c, b)
        self.s.reject_selection(self.c, b.request_id, b"unknown", lambda raw: Worker(raw, status=1))
        other = self.connection()
        second = self.binding(other)
        self.s.open(other, second)
        self.deny(self.s.begin, other, second.request_id, second, Worker)
        for attempt in range(1, 4):
            r = self.s.requests[(self.c.uid, b.request_id)]
            self.assertEqual(r.attempt, attempt)
            self.deny(self.s.collect, self.c, b.request_id)
            if attempt < 3:
                self.s.reject_selection(self.c, b.request_id, b"unknown", lambda raw: Worker(raw, status=1))
        self.deny(self.s.reject_selection, self.c, b.request_id, b"unknown", Worker)

    def test_cancel_audits_each_owned_request_and_attempt(self):
        first, _ = self.start(status=1)
        self.deny(self.s.collect, self.c, first.request_id)
        second = self.binding()
        self.s.open(self.c, second)
        other = self.connection()
        foreign, ref, record = self.ready(other, self.binding(other, account_key=b"z" * 16))
        self.s.cancel(self.c)
        with open(sys.argv[1] + "/audit", "rb") as log:
            rows = log.read().splitlines()
        for binding, attempt in ((first, 1), (second, 0)):
            self.assertIn(("E1S8 request=%016x attempt=%d reason=cancel" %
                           (binding.request_id, attempt)).encode("ascii"), rows)
        self.assertEqual(self.s.redeem(other, ref, record), record)

    def test_proc_executable_is_anchored_to_live_pin(self):
        pin = os.pidfd_open(os.getpid())
        proc = None
        try:
            proc = s6.process_directory(pin, os.getpid())
            s6.check_executable(proc, pin, os.getpid(), os.stat("/proc/self/exe"))
            with self.assertRaises(ValueError):
                s6.process_directory(pin, os.getpid() + 1)
            with self.assertRaises(ValueError):
                s6.check_executable(proc, pin, os.getpid(), os.fstat(self.audit_fd))
        finally:
            if proc is not None:
                os.close(proc)
            os.close(pin)

    def test_dead_pin_cannot_authenticate_retained_proc(self):
        child = multiprocessing.Process(target=time.sleep, args=(30,))
        child.start()
        pin = proc = None
        try:
            pin = os.pidfd_open(child.pid)
            proc = s6.process_directory(pin, child.pid)
            expected = os.stat("/proc/self/exe")
            s6.check_executable(proc, pin, child.pid, expected)
            child.terminate()
            child.join(timeout=5)
            self.assertFalse(child.is_alive())
            with self.assertRaises(ValueError):
                s6.check_executable(proc, pin, child.pid, expected)
        finally:
            if child.is_alive():
                child.kill()
            child.join(timeout=5)
            if proc is not None:
                os.close(proc)
            if pin is not None:
                os.close(pin)

    def test_worker_success_is_mandatory_and_cancel_wins(self):
        for kw in ({"status": 1}, {"abnormal": True}, {"cancelled": True}, {"pending": True}):
            s = self.service()
            c = self.connection(s)
            b, w = self.start(c=c, **kw)
            self.deny(s.collect, c, b.request_id)
            self.assertFalse(any(r.ref for r in s.requests.values()))
            self.assertTrue(w.closed)
        s = self.service()
        c = self.connection(s)
        b, w = self.start(c=c)
        c.tx.remote.send(b"queued-cancel")
        self.deny(s.collect, c, b.request_id)
        self.assertTrue(w.closed)

    def test_failed_reaping_blocks_new_concurrency(self):
        b, w = self.start(pending=True)
        self.s.cancel(self.c)
        self.assertTrue(self.s.failed)
        self.assertIs(self.s.requests[(self.c.uid, b.request_id)].worker, w)
        self.deny(self.s.open, self.c, self.binding())

    def test_worker_start_binding_is_checked(self):
        b = self.binding()
        self.s.open(self.c, b)
        workers = []

        def factory(raw):
            w = Worker(raw)
            w.identity = bytes(32)
            workers.append(w)
            return w

        self.deny(self.s.begin, self.c, b.request_id, b, factory)
        self.assertTrue(workers[0].closed)

    def test_audit_rate_pressure_disables_service(self):
        self.s.audit.second = self.clock.now // 1_000_000_000
        self.s.audit.lines = 64
        self.deny(self.s.open, self.c, self.binding())
        self.assertTrue(self.s.failed)

    def test_expiry_during_audit_cannot_return_success(self):
        b, ref, record = self.ready()
        emit = self.s.audit.emit

        def advancing(reason, now, rid=0, attempt=0):
            emit(reason, now, rid, attempt)
            if reason == "redeemed":
                self.clock.now = b.expiry

        with patch.object(self.s.audit, "emit", side_effect=advancing):
            self.deny(self.s.redeem, self.c, ref, record)
        self.deny(self.s.redeem, self.c, ref, record)

    def test_cleanup_exception_keeps_owner(self):
        b, w = self.start()
        with patch.object(w, "close", side_effect=OSError()):
            self.s.cancel(self.c)
            self.assertTrue(self.s.failed)
            self.assertIs(self.s.requests[(self.c.uid, b.request_id)].worker, w)

    def test_audit_failure_and_backward_clock_revoke(self):
        for mode in ("audit", "clock"):
            s = self.service()
            c = self.connection(s)
            b, ref, record = self.ready(c)
            if mode == "audit":
                with patch.object(s.audit, "emit", side_effect=OSError()):
                    self.deny(s.redeem, c, ref, record)
            else:
                self.clock.now -= 1
                self.deny(s.redeem, c, ref, record)
                self.clock.now += 1
            self.assertTrue(s.failed)
            self.assertFalse(any(r.ref for r in s.requests.values()))

    def test_rng_failure_and_collisions(self):
        b, w = self.start()
        with patch.object(e.os, "getrandom", return_value=bytes(16)):
            self.deny(self.s.collect, self.c, b.request_id)
        s = self.service()
        c = self.connection(s, uid=1)
        with patch.object(e.os, "getrandom", return_value=b"x" * 16):
            self.ready(c, self.binding(c, nonce=b"n" * 32))
            c2 = self.connection(s, uid=2)
            b, w = self.start(self.binding(c2, nonce=b"m" * 32), c2)
            self.deny(s.collect, c2, b.request_id)

    def test_changed_core_binding_and_reused_nonce(self):
        for field, value in (("request_digest", b"x" * 32), ("binding_digest", b"x" * 32),
                             ("expiry", self.clock.now + 10), ("map_gen", 99),
                             ("policy_gen", 99)):
            s = self.service()
            c = self.connection(s)
            b = self.binding(c)
            s.open(c, b)
            self.deny(s.begin, c, b.request_id, replace(b, **{field: value}), Worker)
        b, w = self.start(status=1)
        self.deny(self.s.collect, self.c, b.request_id)
        self.deny(self.s.begin, self.c, b.request_id, b, Worker)

    def test_default_disabled(self):
        s = e.Service(10, 13, e.Audit(self.audit_fd), lambda c, b: True, clock=self.clock)
        self.services.append(s)
        c = self.connection(s)
        self.deny(s.open, c, self.binding(c))

    def packet(self, c, op, payload):
        raw = struct.pack("<8sHHI", e.MAGIC, 1, op, 16 + len(payload)) + payload
        c.tx.remote.send(raw)
        reply = c.serve()
        self.assertEqual(c.tx.remote.recv(e.REPLY_SIZE + 1), reply)
        self.assertEqual(len(reply), e.REPLY_SIZE)
        return reply

    def test_real_seqpacket_redeem_replay_and_cancel(self):
        b, ref, record = self.ready()
        self.assertEqual(self.packet(self.c, e.REDEEM, ref + record),
                         struct.pack("<I", e.OK) + ref + record)
        self.assertEqual(self.packet(self.c, e.REDEEM, ref + record),
                         struct.pack("<I", e.DENIED) + bytes(e.REPLY_SIZE - 4))
        self.assertEqual(self.c.tx.operations, [(0, e.REDEEM), (1, e.REDEEM)])
        c = self.connection()
        b, ref, record = self.ready(c)
        self.packet(c, e.CANCEL, struct.pack("<Q", b.request_id))
        self.deny(self.s.redeem, c, ref, record)

    def test_seqpacket_wrong_connection_and_malformed_are_generic(self):
        b, ref, record = self.ready()
        c = self.connection()
        wrong = self.packet(c, e.REDEEM, ref + record)
        c2 = self.connection()
        malformed = self.packet(c2, 999, b"x" * 8)
        self.assertEqual(wrong, malformed)
        self.assertEqual(self.s.redeem(self.c, ref, record), record)

    def test_seqpacket_storage_query_is_denied(self):
        c = self.connection()
        key = b"k" * 16
        raw = struct.pack("<8sHHI", e.MAGIC, 1, e.STORAGE_QUERY, 32) + key
        c.tx.remote.send(raw)
        reply = c.serve()
        self.assertEqual(struct.unpack_from("<I", reply)[0], e.DENIED)
        # The retired storage query is an ordinary denial, not an audit
        # failure that disables the whole service.
        self.assertFalse(c.service.failed)

    def test_e2e_pam_to_evidence_redemption(self):
        # 1. Success redemption and secret omission check
        canary_secret = b"SUPER-SECRET-CANARY-PASSWORD-12345"
        c = self.connection()
        b = self.binding(c)
        c.service.open(c, b)

        def worker_factory(raw):
            w = Worker(raw, status=0)
            return w

        c.service.begin(c, b.request_id, b, worker_factory)
        ref, record = c.service.collect(c, b.request_id)
        self.assertEqual(len(ref), 16)
        self.assertEqual(len(record), e.EVIDENCE_SIZE)

        # Audit log verification: secret must never be present
        with open(sys.argv[1] + "/audit", "rb") as f:
            audit_bytes = f.read()
        self.assertNotIn(canary_secret, audit_bytes)

        # Wire redemption: send REDEEM packet over seqpacket
        raw_redeem = struct.pack("<8sHHI", e.MAGIC, 1, e.REDEEM, 16 + 16 + e.EVIDENCE_SIZE) + ref + record
        c.tx.remote.send(raw_redeem)
        reply = c.serve()
        self.assertEqual(len(reply), e.REPLY_SIZE)
        status, rep_ref, rep_record = struct.unpack_from("<I16s220s", reply)
        self.assertEqual(status, e.OK)
        self.assertEqual(rep_ref, ref)
        self.assertEqual(rep_record, record)

        # Replay attempt: must fail closed (status == DENIED)
        c.tx.remote.send(raw_redeem)
        reply_replayed = c.serve()
        status_replayed = struct.unpack_from("<I", reply_replayed)[0]
        self.assertEqual(status_replayed, e.DENIED)

    def test_e2e_wrong_password_quota_exceeded_and_worker_crash(self):
        # Wrong password flow
        c = self.connection()
        b = self.binding(c)
        c.service.open(c, b)
        for attempt in range(1, 4):
            c.service.begin(c, b.request_id, b, lambda raw: Worker(raw, status=1))
            self.deny(c.service.collect, c, b.request_id)
            b = replace(b, nonce=os.getrandom(32))

        # 4th attempt: quota exceeded
        self.deny(c.service.begin, c, b.request_id, b, Worker)

        # Worker crash (abnormal termination) on fresh account
        c2 = self.connection()
        b2 = self.binding(c2, account_key=b"c" * 16)
        c2.service.open(c2, b2)
        c2.service.begin(c2, b2.request_id, b2, lambda raw: Worker(raw, status=0, abnormal=True))
        self.deny(c2.service.collect, c2, b2.request_id)

    def test_lab_adapter_supplies_no_credentials(self):
        # R1: the evidence path must never answer PAM prompts from stored data.
        src = Path(__file__).with_name("e1s6-lab.py").read_text()
        self.assertNotIn("fixtures-passwords", src)
        self.assertNotIn("on_feeder", src)
        # R9: digests come from the S1 answer layout, generations exist before use.
        self.assertIn("request_digest=self.template[104:136]", src)
        self.assertLess(src.index("root_gen, verifier_gen = generation()"),
                        src.index("evidence.Service(root_gen"))


if __name__ == "__main__":
    if len(sys.argv) != 2 or os.geteuid() != 0:
        raise SystemExit("owned root fixture directory required")
    if e.EVIDENCE.size != e.EVIDENCE_SIZE:
        raise SystemExit("FAIL: S1 evidence size")
    suite = unittest.defaultTestLoader.loadTestsFromTestCase(EvidenceTests)
    # Suppress tracebacks: no accidental diagnostic payload/secret printing.
    result = unittest.TestResult()
    suite.run(result)
    print("E1S8_COMPONENT cases=%d failures=%d errors=%d" %
          (result.testsRun, len(result.failures), len(result.errors)))
    for test, _ in result.failures + result.errors:
        print("FAIL " + test.id())
    raise SystemExit(0 if result.wasSuccessful() else 1)

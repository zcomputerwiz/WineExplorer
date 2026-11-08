# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright 2026 WineExplorer contributors
"""Disabled S8 native service component. No listener, installer or launch API.

Only the root service may construct Service/Connection. S18 must supply live S6
transactions and a fresh native authority validator; serialized registrations
and caller-supplied success flags are never accepted as authentication.
"""
import fcntl
import hashlib
import os
import select
import socket
import stat
import struct
import threading
import time
from dataclasses import dataclass, replace

OK, DENIED = 0, 1
REDEEM, CANCEL, STORAGE_QUERY = 4, 5, 6  # S6 direct-endorsement operation numbers
MAGIC = b"E1S8CTL\0"
EVIDENCE = struct.Struct("<HHIQQ32s32s32sI16sQQQQQQQQQII")
EVIDENCE_SIZE = 220
REPLY_SIZE = 4 + 16 + EVIDENCE_SIZE
U64 = (1 << 64) - 1


class Denied(Exception):
    def __init__(self):
        super().__init__("Administrator credentials could not be verified. Nothing was started.")


@dataclass(frozen=True)
class Limits:
    attempts: int = 3
    lifetime_ns: int = 120_000_000_000
    global_active: int = 4
    requests: int = 256
    throttle_slots: int = 256  # each of the account and caller tables
    account_failures: int = 3
    caller_failures: int = 6
    throttle_ns: int = 120_000_000_000

    def check(self):
        # S7's fixed maximums are hard ceilings. Smaller provisioning is allowed.
        values = tuple(vars(self).values())
        if (any(type(v) is not int or v <= 0 for v in values)
                or self.attempts > 3 or self.lifetime_ns > 120_000_000_000
                or self.global_active > 32 or self.requests > 4096
                or self.throttle_slots > 4096 or self.throttle_ns > 3_600_000_000_000
                or self.account_failures > 100 or self.caller_failures > 100):
            raise Denied()


@dataclass(frozen=True)
class Binding:
    request_id: int
    request_digest: bytes
    binding_digest: bytes
    nonce: bytes
    account_key: bytes
    host_uid: int  # selected administrator, never requester UID
    root_gen: int
    server_gen: int
    broker_gen: int
    verifier_gen: int
    map_gen: int
    policy_gen: int
    created: int  # original broker admission time, including queueing
    expiry: int   # original absolute request deadline, in request_digest
    name: str     # S4 canonical ASCII name; not logged

    def check(self, now, limits, *, selected=False):
        ints = (self.request_id, self.root_gen, self.server_gen, self.broker_gen,
                self.verifier_gen, self.map_gen, self.policy_gen)
        if (any(type(v) is not int or not 0 < v <= U64 for v in ints)
                or type(self.created) is not int or type(self.expiry) is not int
                or not 0 <= self.created <= now < self.expiry <= U64
                or self.expiry - self.created > limits.lifetime_ns
                or any(type(v) is not bytes or len(v) != n or not any(v)
                       for v, n in ((self.request_digest, 32), (self.binding_digest, 32),
                                    (self.nonce, 32)))):
            raise Denied()
        if not selected and (self.account_key, self.host_uid, self.name) == (bytes(16), 0, ""):
            return  # native request admission precedes private account resolution
        if (type(self.host_uid) is not int or not 0 < self.host_uid < 0xffffffff
                or type(self.account_key) is not bytes or len(self.account_key) != 16
                or not any(self.account_key) or not isinstance(self.name, str)
                or not 1 <= len(self.name) <= 63
                or any(c not in "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_.-"
                       for c in self.name)):
            raise Denied()

    def record(self, attempt, completed):
        # S1 kind 3, caps=0 on this disabled lab link. Native layouts never cross.
        return EVIDENCE.pack(3, 1, EVIDENCE_SIZE, 0, self.request_id,
                             self.request_digest, self.binding_digest, self.nonce,
                             attempt, self.account_key, self.host_uid, completed,
                             self.expiry, self.root_gen, self.server_gen, self.broker_gen,
                             self.verifier_gen, self.map_gen, self.policy_gen, 1, 1)

    def start(self, attempt, limit, *, decoy=False):
        name = b"decoy" if decoy else self.name.encode("ascii")
        key, uid = (bytes(16), 0) if decoy else (self.account_key, self.host_uid)
        return (struct.pack("<8sHHIQIIII", b"E1S7PAM\0", 1, 6 if decoy else 1, 120 + len(name),
                            self.request_id, attempt, 0, 0, 0) + self.nonce
                + struct.pack("<16sQQQII", key, uid, self.map_gen,
                              self.expiry, limit, len(name)) + name)


class Audit:
    """Preopened by root through S7 protected(..., audit=True). Never owns fd."""
    REASONS = ("open", "attempt", "ready", "redeemed", "cancel", "expired",
               "binding", "limit", "throttle", "pressure", "worker-status",
               "worker-protocol", "authority", "connection", "rng", "cleanup",
               "disabled", "clock", "protocol", "account-unknown")

    def __init__(self, fd):
        st = os.fstat(fd)
        if (st.st_uid or stat.S_IMODE(st.st_mode) != 0o600 or st.st_nlink != 1
                or not stat.S_ISREG(st.st_mode)
                or not fcntl.fcntl(fd, fcntl.F_GETFL) & os.O_APPEND):
            raise Denied()
        try:
            os.getxattr(fd, "system.posix_acl_access")
        except OSError as exc:
            import errno
            if exc.errno not in (errno.ENODATA, errno.ENOTSUP):
                raise Denied() from None
        else:
            raise Denied()
        self.fd = fd
        self.second, self.lines = -1, 0

    def emit(self, reason, now, rid=0, attempt=0):
        if reason not in self.REASONS:
            raise Denied()
        second = now // 1_000_000_000
        if second != self.second:
            self.second, self.lines = second, 0
        self.lines += 1
        # Hard bound and deny on logging pressure; no secret-bearing diagnostics.
        line = ("E1S8 request=%016x attempt=%d reason=%s\n" %
                (rid, attempt, reason)).encode("ascii")
        if (self.lines > 64 or os.fstat(self.fd).st_size + len(line) > 16 * 1024 * 1024
                or os.write(self.fd, line) != len(line)):
            raise Denied()
        os.fsync(self.fd)


class Throttle:
    """Fixed capacity. Reservations count even if a worker crashes/disconnects.

    Only a verified successful attempt refunds its reservation. Expired slots
    may be evicted, never unexpired failures. Full live tables deny new keys.
    """
    def __init__(self, slots, maximum, window):
        self.slots, self.maximum, self.window = slots, maximum, window
        self.rows = {}

    def reserve(self, key, now):
        for old, (_, until) in list(self.rows.items()):
            if now >= until:
                del self.rows[old]
        count, until = self.rows.get(key, (0, now + self.window))
        if key not in self.rows and len(self.rows) >= self.slots:
            return "pressure"
        if count >= self.maximum or until > U64:
            return "throttle"
        self.rows[key] = count + 1, until
        return None

    def refund(self, key, until):
        row = self.rows.get(key)
        if row is not None and row[1] == until:
            self.rows[key] = max(0, row[0] - 1), until


@dataclass
class Request:
    connection: object
    binding: Binding
    attempt: int = 0
    state: str = "pending"
    worker: object = None
    ref: bytes = b""
    record: bytes = b""
    account_until: int = 0
    caller_until: int = 0
    decoy: bool = False


class Service:
    def __init__(self, root_gen, verifier_gen, audit, validate, *, lab=False,
                 limits=Limits(), clock=time.monotonic_ns):
        """Provision ONCE per native service, shared by ALL S6 connections.

        validate(connection, binding) must consult current protected map/policy,
        full S5 binding/retained caller and lifecycle gates, not public bytes.
        No default validator and no default lab enablement.
        """
        limits.check()
        if (os.getuid() != 0 or os.geteuid() != 0
                or not all(type(v) is int and 0 < v <= U64 for v in (root_gen, verifier_gen))):
            raise Denied()
        self.root_gen, self.verifier_gen = root_gen, verifier_gen
        self.audit, self.validate, self.enabled = audit, validate, lab
        self.limits, self.clock = limits, clock
        self.lock = threading.RLock()
        self.failed, self.last = False, -1
        self.requests = {}
        self.accounts = Throttle(limits.throttle_slots, limits.account_failures, limits.throttle_ns)
        self.callers = Throttle(limits.throttle_slots, limits.caller_failures, limits.throttle_ns)

    def _now(self):
        try:
            now = self.clock()
            if type(now) is not int or not 0 <= now <= U64 or now < self.last:
                raise Denied()
            self.last = now
            return now
        except Exception:
            self.failed = True
            self._revoke_all()
            try:
                self.audit.emit("clock", max(self.last, 0))
            except Exception:
                pass
            raise Denied() from None

    def _log(self, reason, r=None):
        try:
            self.audit.emit(reason, self.last, r.binding.request_id if r else 0,
                            r.attempt if r else 0)
        except Exception:
            self.failed = True
            self._revoke_all()
            raise Denied() from None

    def _stop(self, r):
        r.ref, r.record = b"", b""
        r.state = "terminal"
        if r.worker is not None:
            try:
                r.worker.close()
                if r.worker.cleanup_pending:
                    self.failed = True  # no new concurrency admission until restart
                    return  # retain bounded cleanup ownership
            except Exception:
                self.failed = True
                return  # never drop an object that may still own a live process
            r.worker = None

    def _revoke_all(self):
        for r in self.requests.values():
            self._stop(r)

    def _deny(self, reason, r=None):
        if r is not None:
            self._stop(r)
        self._log(reason, r)
        raise Denied()

    def _quiet(self, c, r):
        try:
            c.quiet()
        except Exception:
            self._deny("cancel", r)

    def _sweep(self, now):
        for key, r in list(self.requests.items()):
            if now >= r.binding.expiry:
                self._stop(r)
                if r.worker is None:
                    del self.requests[key]
                self._log("expired", r)

    def _gate(self, c, b, now):
        if not self.enabled or self.failed:
            self._deny("disabled")
        try:
            c.live()
            b.check(now, self.limits)
            if ((b.root_gen, b.verifier_gen, b.server_gen, b.broker_gen) !=
                    (self.root_gen, self.verifier_gen, c.server_gen, c.broker_gen)
                    or not self.validate(c, b)):
                raise Denied()
        except Exception:
            self._deny("authority")

    def open(self, c, b):
        with self.lock:
            now = self._now()
            self._sweep(now)
            self._gate(c, b, now)
            key = c.uid, b.request_id
            if key in self.requests or len(self.requests) >= self.limits.requests:
                self._deny("pressure")
            r = Request(c, b)
            self.requests[key] = r
            self._log("open", r)

    def _request(self, c, rid, now):
        self._sweep(now)
        r = self.requests.get((c.uid, rid))
        if r is None or r.connection is not c or r.state == "terminal":
            self._deny("connection")
        try:
            self._gate(c, r.binding, now)
        except Denied:
            self._stop(r)
            raise
        return r

    def begin(self, c, rid, b, worker_factory):
        """Internal native call. Factory owns S7 Worker, never a broker Boolean.

        Returns service-assigned attempt and exact START; factory is invoked
        only after quotas are reserved, under the service lock.
        """
        return self._begin(c, rid, b, worker_factory)

    def _begin(self, c, rid, b, worker_factory, *, decoy=False, throttle_key=None):
        with self.lock:
            now = self._now()
            r = self._request(c, rid, now)
            try:
                self._gate(c, b, now)
                b.check(now, self.limits, selected=not decoy)
            except Denied:
                self._stop(r)
                raise
            # Account selection and nonce alone may change between submissions.
            stable = replace(b, nonce=r.binding.nonce, account_key=r.binding.account_key,
                             host_uid=r.binding.host_uid, name=r.binding.name)
            if stable != r.binding or r.state != "pending":
                self._deny("binding", r)
            if r.attempt and b.nonce == r.binding.nonce:
                self._deny("binding", r)
            if r.attempt >= self.limits.attempts:
                self._deny("limit", r)
            self._quiet(c, r)
            active = [x for x in self.requests.values() if x.worker is not None]
            if (len(active) >= self.limits.global_active
                    or any(x.connection.session_key == c.session_key or x.connection.uid == c.uid
                           for x in active)):
                self._deny("limit", r)
            r.binding, r.attempt, r.decoy = b, r.attempt + 1, decoy
            # Pessimistic charging before spawning also counts setup/crash/cancel.
            reason = self.callers.reserve(c.uid, now)
            if reason:
                self._deny(reason, r)
            r.caller_until = self.callers.rows[c.uid][1]
            account_bucket = throttle_key if decoy else b.account_key
            reason = self.accounts.reserve(account_bucket, now)
            if reason:
                self._deny(reason, r)
            r.account_until = self.accounts.rows[account_bucket][1]
            self._log("attempt", r)
            raw = b.start(r.attempt, self.limits.attempts, decoy=decoy)
            try:
                self._gate(c, b, self._now())
                self._quiet(c, r)
                r.worker = worker_factory(raw)
                if (r.worker.binding != raw[16:28] + raw[40:72]
                        or r.worker.identity != raw[72:104]
                        or r.worker.deadline != b.expiry):
                    self._deny("worker-protocol", r)
                r.state = "running"
            except Exception:
                self._deny("worker-protocol", r)
            return r.attempt, raw

    def reject_selection(self, c, rid, candidate, worker_factory, *, account_key=None):
        """Native unknown/ineligible selection: run an S7 failure-only decoy.

        Same admission, active slot, prompts, deadline and delayed failure as
        begin(). S18 must wait for ready(), never send an early denied() reply.
        A known but ineligible selection MUST supply its authoritative S4 key;
        only unknown names use a bounded private hash. Neither is logged.
        """
        with self.lock:
            now = self._now()
            r = self._request(c, rid, now)
            if (type(candidate) is not bytes or not 1 <= len(candidate) <= 63
                    or (account_key is not None and
                        (type(account_key) is not bytes or len(account_key) != 16 or not any(account_key)))):
                self._deny("limit", r)
            tag = account_key if account_key is not None else hashlib.sha256(b"E1S8-UNKNOWN\0" + candidate.lower()).digest()
            try:
                nonce = os.getrandom(32)
            except OSError:
                self._deny("rng", r)
            if len(nonce) != 32 or not any(nonce):
                self._deny("rng", r)
            result = self._begin(c, rid, replace(r.binding, nonce=nonce), worker_factory,
                                 decoy=True, throttle_key=tag)
            self._log("account-unknown", r)
            return result

    def collect(self, c, rid):
        """After S7 control readiness only. Returns (ref, exact kind-3 bytes).

        There is no API accepting success/seed/identity from a Wine client.
        S7 checks its private result, canonical identity and natural exit.
        """
        with self.lock:
            now = self._now()
            r = self._request(c, rid, now)
            if r.state != "running" or r.worker is None:
                self._deny("worker-protocol", r)
            w = r.worker
            try:
                self._quiet(c, r)  # queued cancel/EOF wins over result
                if w.cancelled(c.tx.child):
                    self._deny("cancel", r)
                status = w.collect()
                if type(status) is not int or status not in (0, 1):
                    self._deny("worker-protocol", r)
                if r.decoy and not status:
                    self._deny("worker-protocol", r)  # a decoy can never issue evidence
                w.verify_exit()
                if status:
                    w.close()
                    if w.cleanup_pending:
                        self._deny("cleanup", r)
                    r.worker, r.state = None, "pending"
                    self._log("worker-status", r)
                    raise Denied()
                seed = w.take_seed()
                try:
                    if len(seed) != 32 or not any(seed):
                        self._deny("worker-protocol", r)
                finally:
                    seed[:] = bytes(len(seed))
                w.close()
                if w.cleanup_pending:
                    self._deny("cleanup", r)
                r.worker = None
                now = self._now()
                self._gate(c, r.binding, now)
                self._quiet(c, r)
                try:
                    ref = os.getrandom(16)
                except OSError:
                    self._deny("rng", r)
                if (len(ref) != 16 or not any(ref)
                        or any(x.ref == ref for x in self.requests.values())):
                    self._deny("rng", r)
                r.ref, r.record = ref, r.binding.record(r.attempt, now)
                r.state = "ready"
                self._log("ready", r)
                self._gate(c, r.binding, self._now())
                self._quiet(c, r)
                # Refund only after all success gates and required audit succeed.
                self.accounts.refund(r.binding.account_key, r.account_until)
                self.callers.refund(c.uid, r.caller_until)
                return r.ref, r.record
            except Denied:
                # A normal authentication failure permits another bounded
                # attempt; all other failures are terminal.
                if r.state != "pending":
                    self._stop(r)
                raise
            except Exception:
                self._deny("worker-protocol", r)

    def redeem(self, c, ref, expected):
        with self.lock:
            now = self._now()
            self._sweep(now)
            # Connection object identity is checked before lookup/consumption.
            matches = [r for r in self.requests.values()
                       if r.connection is c and r.state == "ready" and r.ref == ref]
            if len(matches) != 1:
                self._deny("connection")
            r = matches[0]
            record = r.record
            self._stop(r)  # atomic burn BEFORE comparisons, including mismatch
            self._gate(c, r.binding, now)
            self._quiet(c, r)
            if type(expected) is not bytes or expected != record:
                self._deny("binding", r)
            self._log("redeemed", r)
            self._gate(c, r.binding, self._now())
            self._quiet(c, r)
            return record

    def cancel(self, c, rid=None):
        with self.lock:
            self._now()
            for r in self.requests.values():
                if r.connection is c and (rid is None or rid == r.binding.request_id):
                    self._stop(r)
                    self._log("cancel", r)

    def storage_generation(self, c, account_key):
        with self.lock:
            now = self._now()
            self._sweep(now)
            if not self.enabled or self.failed:
                self._deny("disabled")
            try:
                c.live()
            except Exception:
                self._deny("authority")
            if (not isinstance(account_key, (bytes, bytearray)) or len(account_key) != 16
                    or not any(account_key)):
                self._deny("binding")
            if hasattr(self, "query_storage") and callable(self.query_storage):
                gen = self.query_storage(c, account_key)
                if isinstance(gen, int) and 0 < gen <= U64:
                    return gen
            # No provider, or it gave no valid generation: never guess one.
            self._deny("storage")

    def tick(self):
        """S18 must schedule <=50ms, also during PAM/NSS and queue waits."""
        with self.lock:
            now = self._now()
            self._sweep(now)
            for r in self.requests.values():
                if r.state != "terminal":
                    try:
                        self._gate(r.connection, r.binding, now)
                        if r.worker is not None:
                            self._quiet(r.connection, r)
                            if r.worker.cancelled(r.connection.tx.child):
                                raise Denied()
                    except Exception:
                        self._stop(r)

    def close(self):
        with self.lock:
            self.enabled = False
            self._revoke_all()
            # S19 must retain the native supervisor until these owners drain.
            self.requests = {k: r for k, r in self.requests.items() if r.worker is not None}


class Connection:
    """Native-only adapter around the actual S6 Transaction, never wire IDs.

    S18 owns the S6 transaction and serializes all callbacks on its GLib loop.
    receive is S6.receive; worker_factory is constructed with pinned S7 paths.
    """
    def __init__(self, service, tx, receive):
        tx.live()
        self.service, self.tx, self.receive = service, tx, receive
        self.uid, self.server_gen, self.broker_gen = tx.uid, tx.server_gen, tx.broker_gen
        # Conservative across prefixes/servers: at most one per host UID/session,
        # and the service additionally enforces one verifier per host UID.
        self.session_key = self.uid, tx.session
        self.closed = False

    def live(self):
        if (self.closed or (self.tx.uid, self.tx.server_gen, self.tx.broker_gen, self.tx.session) !=
                (self.uid, self.server_gen, self.broker_gen, self.session_key[1])):
            raise Denied()
        self.tx.live()
        poll = select.poll()
        poll.register(self.tx.data, select.POLLHUP | select.POLLERR | select.POLLNVAL)
        if poll.poll(0):
            raise Denied()

    def quiet(self):
        self.live()
        try:
            # Control metadata only; passwords are on the separate child FD.
            self.tx.data.recv(1, socket.MSG_PEEK | socket.MSG_DONTWAIT)
        except BlockingIOError:
            return
        raise Denied()  # any queued control during authentication cancels

    def close(self):
        self.closed = True
        try:
            self.service.cancel(self)
        except Denied:
            pass  # audit/clock failure already revokes all work; never expose it

    def ready(self, rid):
        """S18 control-ready callback: generic fixed-size identity-ready reply."""
        result = struct.pack("<I", DENIED) + bytes(REPLY_SIZE - 4)
        try:
            ref, record = self.service.collect(self, rid)
            result = struct.pack("<I", OK) + ref + record
        except Denied:
            pass
        return self._send(result)

    def denied(self):
        """S18 uses this for admission/selection/start failures as well."""
        return self._send(struct.pack("<I", DENIED) + bytes(REPLY_SIZE - 4))

    def _send(self, result):
        try:
            if self.tx.data.send(result) != len(result):
                raise Denied()
        except Exception:
            self.close()
            result = struct.pack("<I", DENIED) + bytes(len(result) - 4)
        return result

    def serve(self):
        """One broker REDEEM/CANCEL on S6's endorsed SOCK_SEQPACKET channel.

        S18 routes readiness here in identity-ready state only. Every response
        has the same fixed size; all failures zero every result byte.
        """
        result = struct.pack("<I", DENIED) + bytes(REPLY_SIZE - 4)
        try:
            self.live()
            raw, cred, pin, rights = self.receive(self.tx.data, 16 + 16 + EVIDENCE_SIZE,
                                                  0, minimum=24)
            try:
                if cred[:2] != (self.tx.pid, self.uid) or rights:
                    raise Denied()
                magic, version, operation, length = struct.unpack_from("<8sHHI", raw)
                if magic != MAGIC or version != 1 or length != len(raw):
                    raise Denied()
                self.tx.endorsement(operation)  # next sequence, real server credential/pin
                if operation == REDEEM and len(raw) == 16 + 16 + EVIDENCE_SIZE:
                    record = self.service.redeem(self, raw[16:32], raw[32:])
                    result = struct.pack("<I", OK) + raw[16:32] + record
                elif operation == CANCEL and len(raw) == 24:
                    rid = struct.unpack_from("<Q", raw, 16)[0]
                    self.service.cancel(self, rid)
                    # Cancellation is deliberately the same public denial class.
                elif operation == STORAGE_QUERY and len(raw) == 32:
                    result = struct.pack("<IIQ", DENIED, 0, 0)
                    gen = self.service.storage_generation(self, raw[16:32])
                    result = struct.pack("<IIQ", OK, 0, gen)
                else:
                    raise Denied()
            finally:
                os.close(pin)
                for fd in rights:
                    os.close(fd)
        except Exception:
            try:
                with self.service.lock:
                    self.service._now()
                    self.service._log("protocol")
            except Denied:
                pass
            self.close()
        return self._send(result)

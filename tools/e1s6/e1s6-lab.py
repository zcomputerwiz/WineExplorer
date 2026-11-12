#!/usr/bin/python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright 2026 WineExplorer contributors
"""S6 metadata transport fixture. Never installed or activated by production.

Requires Linux SO_PEERPIDFD and SO_PASSPIDFD; unsupported kernels deny.
S7 may provision an explicit native worker; answers never pass through Python.
No evidence redemption, token creation or target execution lives here.
"""
import array
import hashlib
import json
import os
import re
import select
import socket
import stat
import struct
import subprocess
import sys
import time

# The R8 codec is installed next to this file (scripts/lib/e1s6-fixture.sh).
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import e1r8


BUS = "org.wineexplorer.E1S6Lab"
PATH = "/org/wineexplorer/E1S6Lab"
PEERPIDFD, PASSPIDFD, SCM_PIDFD = 77, 76, 4
REG_SIZE, ANSWER_SIZE, ENDORSE_SIZE = 112, 156, 96


def audit(site, frame):
    # This module is the uninstalled lab fixture, never a production endpoint.
    # Fixed site names and source locations; packet contents are never logged.
    print("E1S6_DENY native site=%s function=%s line=%d" %
          (site, frame.f_code.co_name, frame.f_lineno), file=sys.stderr, flush=True)


def require(ok, site="check"):
    if not ok:
        audit(site, sys._getframe(1))
        raise ValueError("transport denied")


def audit_exception(exc):
    trace = exc.__traceback__
    origin = trace
    call = None
    while trace is not None:
        if trace.tb_frame.f_code.co_filename == __file__:
            origin = trace
        if trace.tb_frame.f_code.co_name == "call_blocking":
            # dbus-python's synchronous proxy call: method metadata only, not
            # args (which could include transport bytes or FDs).
            call = {key: str(trace.tb_frame.f_locals.get(key, ""))[:256]
                    for key in ("bus_name", "object_path", "dbus_interface", "method")}
        if trace.tb_next is None:
            break
        trace = trace.tb_next
    name = getattr(exc, "get_dbus_name", None)
    # Bound/escape exception diagnostics to one line. Never include packet
    # buffers, call args, authentication answers or other traceback locals.
    detail = {"type": type(exc).__name__, "text": str(exc)[:1024],
              "dbus_name": str(name())[:256] if callable(name) else "", "call": call}
    print("E1S6_DENY native site=exception function=%s line=%d origin=%s:%d errno=%d detail=%s" %
          (trace.tb_frame.f_code.co_name if trace else "none", trace.tb_lineno if trace else 0,
           origin.tb_frame.f_code.co_name if origin else "none", origin.tb_lineno if origin else 0,
           exc.errno if isinstance(exc, OSError) and exc.errno is not None else 0,
           json.dumps(detail, ensure_ascii=True, separators=(",", ":"))),
          file=sys.stderr, flush=True)


def random_bytes(size):
    value = os.getrandom(size)
    require(len(value) == size and any(value), "rng-result")
    return value


def generation():
    return int.from_bytes(random_bytes(8), "little")


LOCKOUT_KEY = rb"[software\\wineexplorer\\elevation]"
LOCKOUT_VALUE = re.compile(rb'"(LockoutBadCount|LockoutDuration)"=dword:([0-9a-fA-F]{8})', re.IGNORECASE)


def lockout_policy(cache):
    """Account lockout policy from the machine registry: (threshold, window in ns); (0, 0) is no lockout.

    Read from the machine root's saved hive (machine_root in the root-owned sysrun.conf), which only the
    system group can change: HKLM\\Software\\WineExplorer\\Elevation (the key of the other elevation
    policy), DWORD LockoutBadCount (0..999, 0 or absent: never locked out) and LockoutDuration (minutes,
    default 30, 0: the longest, 99999). Windows keeps this policy in the account database, not in a
    registry policy value; the names are those of its security templates. A hive that cannot be read keeps
    the last policy read.
    """
    try:
        root = None
        with open("/etc/wineexplorer/sysrun.conf", "rb") as conf:
            for line in conf.read(65536).splitlines():
                if line.startswith(b"machine_root="):
                    root = os.fsdecode(line[len(b"machine_root="):])
        if not root:
            return cache["policy"]
        fd = os.open(os.path.join(root, "system.reg"), os.O_RDONLY | os.O_NOFOLLOW | os.O_CLOEXEC)
        with os.fdopen(fd, "rb") as hive:
            st = os.fstat(hive.fileno())
            stamp = (st.st_dev, st.st_ino, st.st_mtime_ns, st.st_size)
            if stamp == cache["stamp"]:
                return cache["policy"]
            if not stat.S_ISREG(st.st_mode) or st.st_size > 256 * 1024 * 1024:
                return cache["policy"]
            values, inside = {}, False
            for raw in hive:
                line = raw.rstrip(b"\r\n")
                if line.startswith(b"["):
                    inside = line.lower().startswith(LOCKOUT_KEY)
                elif inside:
                    match = LOCKOUT_VALUE.fullmatch(line)
                    if match:
                        values[match.group(1).lower()] = int(match.group(2), 16)
        threshold = values.get(b"lockoutbadcount", 0)
        minutes = values.get(b"lockoutduration", 30)
        if threshold > 999:
            threshold = 999
        if minutes == 0 or minutes > 99999:
            minutes = 99999
        cache["stamp"] = stamp
        return (threshold, minutes * 60_000_000_000) if threshold else (0, 0)
    except OSError:
        return cache["policy"]


def alive(fd):
    poll = select.poll()
    poll.register(fd, select.POLLIN)
    return not poll.poll(0)


def process_directory(pin, pid):
    """Anchor procfs once while the kernel-issued pidfd is demonstrably live.

    A pidfd's /proc/self/fd link is anon_inode, not a process directory. Tie
    fdinfo's PID to SCM_CREDENTIALS, bracket open with pin liveness, then use
    only this held proc directory. Death during open cannot authenticate a
    reused numeric PID. This does not freeze exec or executable contents.
    """
    require(child_pid_from_pin(pin) == pid, "proc-pin-before-open")
    fd = os.open("/proc/%d" % pid, os.O_PATH | os.O_DIRECTORY | os.O_NOFOLLOW | os.O_CLOEXEC)
    try:
        require(child_pid_from_pin(pin) == pid, "proc-pin-after-open")
        return fd
    except BaseException:
        os.close(fd)
        raise


def check_executable(proc, pin, pid, expected):
    require(child_pid_from_pin(pin) == pid, "executable-pin-before-open")
    exe = os.open("exe", os.O_RDONLY | os.O_CLOEXEC, dir_fd=proc)
    try:
        st = os.fstat(exe)
        require((st.st_dev, st.st_ino) == (expected.st_dev, expected.st_ino), "executable-inode")
        require(child_pid_from_pin(pin) == pid, "executable-pin-after-stat")
    finally:
        os.close(exe)


def protected(path):
    """Open every component without following links. Return the pinned fd."""
    require(path.startswith("/") and ".." not in path.split("/"))
    fd = os.open("/", os.O_RDONLY | os.O_DIRECTORY | os.O_CLOEXEC)
    try:
        parts = path.split("/")[1:]
        for index, part in enumerate(parts):
            require(bool(part))
            nxt = os.open(part, os.O_RDONLY | os.O_NOFOLLOW | os.O_CLOEXEC,
                          dir_fd=fd)
            os.close(fd)
            fd = nxt
            st = os.fstat(fd)
            require(st.st_uid == 0 and not st.st_mode & 0o022)
            require(stat.S_ISREG(st.st_mode) if index == len(parts) - 1
                    else stat.S_ISDIR(st.st_mode))
        result, fd = fd, -1
        return result
    finally:
        if fd >= 0:
            os.close(fd)


def peer(sock):
    """Kernel socket creator identity, tied to a pidfd, not a /proc PID guess."""
    pid, uid, gid = struct.unpack("3i", sock.getsockopt(socket.SOL_SOCKET, socket.SO_PEERCRED, 12))
    fd = sock.getsockopt(socket.SOL_SOCKET, PEERPIDFD)
    try:
        require(alive(fd), "peer-pin-live")
        return pid, uid, gid, fd
    except Exception:
        os.close(fd)
        raise


def pass_identity(sock):
    sock.setsockopt(socket.SOL_SOCKET, socket.SO_PASSCRED, 1)
    sock.setsockopt(socket.SOL_SOCKET, PASSPIDFD, 1)


def process_real_uid(pid, pin):
    require(alive(pin), "process-real-uid-live")
    with open("/proc/%d/status" % pid, encoding="ascii") as status:
        uids = [int(line.split()[1]) for line in status if line.startswith("Uid:")]
    require(len(uids) == 1 and alive(pin), "process-real-uid-parsed")
    return uids[0]


def receive_control_msg(sock):
    fds, pins, identity, valid = [], [], None, True
    try:
        data, ancillary, flags, _ = sock.recvmsg(245, 512, socket.MSG_CMSG_CLOEXEC)
        for level, kind, value in ancillary:
            if level == socket.SOL_SOCKET and kind in (socket.SCM_RIGHTS, SCM_PIDFD):
                values = array.array("i")
                values.frombytes(value[:len(value) - len(value) % values.itemsize])
                valid = valid and len(value) % values.itemsize == 0
                if kind == socket.SCM_RIGHTS:
                    fds.extend(values)
                else:
                    pins.extend(values)
            elif level == socket.SOL_SOCKET and kind == socket.SCM_CREDENTIALS:
                if identity is None and len(value) == 12:
                    identity = struct.unpack("3i", value)
                else:
                    valid = False
            else:
                valid = False
        require(bool(data), "receive-eof")
        require(valid and not flags & (socket.MSG_TRUNC | socket.MSG_CTRUNC), "receive-ancillary")
        return data, identity, pins, fds
    except Exception:
        for fd in fds + pins:
            if fd >= 0:
                os.close(fd)
        raise


def receive(sock, size, rights_count=0, minimum=None):
    """Exactly one packet; kernel credentials and kernel-supplied sender pidfd.

    A supplied SCM_RIGHTS fd can never stand in for SCM_PIDFD. Close every fd
    on malformed/truncated messages, including unexpected ancillary data.
    """
    fds, pins, identity, valid = [], [], None, True
    try:
        data, ancillary, flags, _ = sock.recvmsg(size + 1, 256, socket.MSG_CMSG_CLOEXEC)
        for level, kind, value in ancillary:
            if level == socket.SOL_SOCKET and kind in (socket.SCM_RIGHTS, SCM_PIDFD):
                values = array.array("i")
                values.frombytes(value[:len(value) - len(value) % values.itemsize])
                valid = valid and len(value) % values.itemsize == 0
                if kind == socket.SCM_RIGHTS:
                    fds.extend(values)
                else:
                    pins.extend(values)
            elif level == socket.SOL_SOCKET and kind == socket.SCM_CREDENTIALS:
                if identity is None and len(value) == 12:
                    identity = struct.unpack("3i", value)
                else:
                    valid = False
            else:
                valid = False
        require(bool(data), "receive-eof")
        require(valid and not flags & (socket.MSG_TRUNC | socket.MSG_CTRUNC), "receive-ancillary")
        require((len(data) == size if minimum is None else minimum <= len(data) <= size)
                and identity is not None and len(pins) == 1, "receive-framing-identity")
        require(len(fds) == rights_count and all(fd >= 0 for fd in fds + pins) and alive(pins[0]),
                "receive-fds-live")
        result = data, identity, pins[0], fds
        pins, fds = [], []
        return result
    finally:
        for fd in fds + pins:
            if fd >= 0:
                os.close(fd)


def registration(role, endpoint, gens, host, channel, session):
    # S1 kind 7, exact LE encoding; no readiness capabilities advertised.
    return (struct.pack("<HHIQII", 7, 1, REG_SIZE, 0, role, 0) + endpoint
            + struct.pack("<QQQQ", *gens) + host + channel
            + struct.pack("<II", session, 0))


def answer(data):
    require(len(data) == ANSWER_SIZE, "answer-size")
    require(struct.unpack_from("<HHIQ", data) == (2, 1, ANSWER_SIZE, 0), "answer-header")
    request, attempt, round_, index, kind = struct.unpack_from("<QIIII", data, 16)
    require(request and attempt and round_ == 1 and index == 0 and kind in (2, 3), "answer-fields")
    require(all(any(data[a:b]) for a, b in ((40, 72), (72, 104), (104, 136), (136, 152))), "answer-bindings")
    require(data[152:] == b"\0" * 4, "answer-payload")  # no payload; credential text is unsupported


def broker_deadline(tx, now, expiry):
    # Groundwork step 2: after the PAM start packet the verifier keeps the
    # broker's absolute deadline, checked against its own clock and bound.
    require(now < expiry <= now + 300_000_000_000, "pam-start-expiry")
    tx.deadline = expiry


#
# R9 Verifier definitions and pure decision functions (WO R9-2)
#

CHANNEL_KIND_A = "A"
CHANNEL_KIND_B0 = "B0"


def r9_resolve_s0_user(user_name=None):
    """Resolve session 0 user name to unix UID. Startup error if missing or unresolvable."""
    if user_name is None:
        user_name = os.environ.get("WEX_E1S6_LAB_S0_USER")
    if not user_name:
        audit("r9-s0-user-missing", sys._getframe(1))
        raise ValueError("WEX_E1S6_LAB_S0_USER required when R9 enabled")
    import pwd
    try:
        return pwd.getpwnam(user_name).pw_uid
    except KeyError:
        if user_name.isdigit():
            return int(user_name)
        audit("r9-s0-user-unknown", sys._getframe(1))
        raise ValueError("unknown session 0 user: %s" % user_name)


def r9_classify_channel(r9_enabled, s0_uid, client_uid, cred_uid, cred_gid, system_gid):
    """Classify channel kind based on endorsement credentials."""
    if cred_gid != system_gid:
        audit("endorsement-gid", sys._getframe(1))
        raise ValueError("transport denied")
    if r9_enabled:
        if cred_uid == s0_uid:
            return CHANNEL_KIND_B0
        elif cred_uid == client_uid:
            return CHANNEL_KIND_A
        else:
            audit("endorsement-uid", sys._getframe(1))
            raise ValueError("transport denied")
    else:
        if cred_uid != client_uid:
            audit("endorsement-uid", sys._getframe(1))
            raise ValueError("transport denied")
        return CHANNEL_KIND_A


def r9_gate_message(r9_enabled, channel_kind, mtype):
    """Check message gating per channel kind. Returns True if accepted, False if refused."""
    if not r9_enabled:
        return mtype in (17, 25, 28)
    if channel_kind == CHANNEL_KIND_B0:
        return mtype in (17, 21, 25, 32)
    elif channel_kind == CHANNEL_KIND_A:
        return mtype in (28,)
    return False


def r9_handle_start(rec, channel_kind, channel_id, a_channel_exists, a_channel_kind,
                    a_channel_endorsed, a_channel_id, r9_enabled=True):
    """Validate START attempt binding. Returns (ok, new_state, error_reason)."""
    if r9_enabled:
        if channel_kind != CHANNEL_KIND_B0:
            audit("start-wrong-channel-kind", sys._getframe(1))
            return False, rec.get("state") if rec else None, "start-wrong-channel-kind"
        if not a_channel_exists:
            audit("start-a-channel-missing", sys._getframe(1))
            return False, rec.get("state") if rec else None, "start-a-channel-missing"
        if a_channel_kind != CHANNEL_KIND_A:
            audit("start-not-a-channel", sys._getframe(1))
            return False, rec.get("state") if rec else None, "start-not-a-channel"
        if not a_channel_endorsed:
            audit("start-a-channel-unendorsed", sys._getframe(1))
            return False, rec.get("state") if rec else None, "start-a-channel-unendorsed"
        if channel_id != a_channel_id:
            audit("start-channel-id-mismatch", sys._getframe(1))
            return False, rec.get("state") if rec else None, "start-channel-id-mismatch"
    if rec is None:
        audit("start-without-endorsed", sys._getframe(1))
        return False, None, "start-without-endorsed"
    if rec.get("state") != "ENDORSED":
        audit("start-state-not-endorsed", sys._getframe(1))
        return False, rec.get("state"), "start-state-not-endorsed"
    return True, "STARTED", None


def r9_handle_grant(rec, channel_kind, grant_msg, r9_enabled=True):
    """Process a GRANT message (type 32). Returns (ok, new_state, reply_bytes, error_reason)."""
    if not r9_enabled:
        audit("grant-r9-disabled", sys._getframe(1))
        return False, rec.get("state") if rec else None, None, "grant-r9-disabled-unknown-type"
    if channel_kind != CHANNEL_KIND_B0:
        audit("grant-wrong-channel-kind", sys._getframe(1))
        return False, rec.get("state") if rec else None, None, "grant-wrong-channel-kind"
    if rec is None:
        audit("grant-without-attempt", sys._getframe(1))
        return False, None, None, "grant-without-attempt"
    curr_state = rec.get("state")
    if curr_state != "RESULT_RETAINED":
        audit("grant-state-not-result-retained", sys._getframe(1))
        return False, curr_state, None, "grant-state-not-result-retained"
    if grant_msg.account_key != rec.get("account_key"):
        audit("grant-account-key-mismatch", sys._getframe(1))
        return False, curr_state, None, "grant-account-key-mismatch"
    if grant_msg.evidence_ref != rec.get("evidence_ref"):
        audit("grant-evidence-ref-mismatch", sys._getframe(1))
        return False, curr_state, None, "grant-evidence-ref-mismatch"

    rec["state"] = "GRANTED"
    t = rec["tuple"]
    reply = e1r8.encode_granted(t)
    return True, "GRANTED", reply, None


def r9_handle_redeem(rec, channel_kind, redeem_ref, now, control_sock=None, r9_enabled=True):
    """Process a REDEEM message (type 28). Returns (ok, new_state, reply_bytes, error_reason)."""
    if rec is None:
        audit("redeem-without-attempt", sys._getframe(1))
        return False, None, None, "redeem-without-attempt"

    expected_state = "GRANTED" if r9_enabled else "RESULT_RETAINED"
    if r9_enabled and channel_kind != CHANNEL_KIND_A:
        audit("redeem-wrong-channel-kind", sys._getframe(1))
        return False, rec.get("state"), None, "redeem-wrong-channel-kind"

    # Burn before comparison: one redemption, right or wrong
    record = rec.pop("evidence", None)
    ref = rec.pop("evidence_ref", None)
    state = rec.get("state")
    rec["state"] = "REDEEMED"

    t = rec["tuple"]
    if state != expected_state:
        audit("redeem-evidence-state", sys._getframe(1))
        return False, "REDEEMED", None, "redeem-state-invalid"
    if record is None or ref is None:
        audit("redeem-evidence-missing", sys._getframe(1))
        return False, "REDEEMED", None, "redeem-no-evidence"
    if ref != redeem_ref:
        audit("redeem-evidence-ref", sys._getframe(1))
        return False, "REDEEMED", None, "redeem-ref-mismatch"
    if now >= t.deadline:
        audit("redeem-evidence-deadline", sys._getframe(1))
        return False, "REDEEMED", None, "redeem-deadline-expired"

    name = rec["account"]
    name = name if isinstance(name, bytes) else str(name).encode("ascii")
    receipt = (e1r8._put_header(30, 316 + len(name), t) + ref + record +
               struct.pack("<I", len(name)) + name)
    reply_29 = e1r8._put_header(29, 296, t) + record

    if control_sock is not None and os.environ.get("WEX_E1S6_LAB_NO_RECEIPT") != "1":
        control_sock.sendmsg([receipt], [(
            socket.SOL_SOCKET, socket.SCM_CREDENTIALS,
            struct.pack("3i", os.getpid(), os.getuid(), os.getgid()))])

    return True, "REDEEMED", reply_29, None


def r9_handle_cancel(rec, channel_kind, cancel_msg, r9_enabled=True):
    """Process a CANCEL message (type 21). Returns (ok, new_state, error_reason)."""
    if r9_enabled and channel_kind != CHANNEL_KIND_B0:
        audit("cancel-wrong-channel-kind", sys._getframe(1))
        return False, rec.get("state") if rec else None, "cancel-wrong-channel-kind"
    if rec is None:
        audit("cancel-without-attempt", sys._getframe(1))
        return False, None, "cancel-without-attempt"

    curr_state = rec.get("state")
    reason = cancel_msg.reason if hasattr(cancel_msg, "reason") else int(cancel_msg)

    if curr_state == "REDEEMED":
        rec["cancelled"] = True
        rec["cancel_reason"] = reason
        return True, "REDEEMED", None
    else:
        rec["state"] = "CANCELLED"
        rec["cancel_reason"] = reason
        rec.pop("evidence", None)
        rec.pop("evidence_ref", None)
        return True, "CANCELLED", None


def service(server_path, system_group, pam_paths=None):
    require(os.getuid() == 0 and os.geteuid() == 0)
    import grp
    import dbus
    import dbus.service
    import dbus.mainloop.glib
    from gi.repository import GLib

    r9_enabled = os.environ.get("WEX_E1S6_LAB_R9") == "1"
    s0_uid = None
    if r9_enabled:
        s0_uid = r9_resolve_s0_user(os.environ.get("WEX_E1S6_LAB_S0_USER"))

    pam_exe = pam_audit = evidence_service = supervisor = None
    # R8 account lockout, shared by every broker session of this service. Off by default, like the
    # "Account lockout threshold" policy of Windows XP / Server 2003 (0: never locked out); the fixed 2 s
    # refusal is then the only brake. The policy is read from the machine registry (see lockout_policy).
    r8_lock = {"stamp": None, "policy": (0, 0), "accounts": None}

    def r8_lockout_accounts():
        policy = lockout_policy(r8_lock)
        if policy != r8_lock["policy"] or (policy[0] and r8_lock["accounts"] is None):
            r8_lock["policy"] = policy
            r8_lock["accounts"] = evidence.Throttle(256, policy[0], policy[1]) if policy[0] else None
            print("E1R8_LOCKOUT threshold=%d minutes=%d" % (policy[0], policy[1] // 60_000_000_000), flush=True)
        return r8_lock["accounts"]
    # Assigned before the evidence service captures them (R9).
    root_gen, verifier_gen = generation(), generation()
    if pam_paths is None:
        def_worker = "/usr/local/libexec/wineexplorer/pam-worker"
        def_audit = "/usr/local/libexec/wineexplorer/pam-audit.log"
        if os.path.exists(def_worker) and os.path.exists(def_audit):
            pam_paths = (def_worker, def_audit)
    if pam_paths is not None:
        # Provisioning arguments only. No bus/request/environment service name,
        # worker path or audit path. Keep the executable inode pinned.
        # supervisor.py: next to this file when installed, else the source tree's ../e1s7.
        module_dir = os.path.dirname(os.path.abspath(__file__))
        if not os.path.exists(os.path.join(module_dir, "supervisor.py")):
            module_dir = os.path.abspath(os.path.join(module_dir, "..", "e1s7"))
        module_pin = protected(os.path.join(module_dir, "supervisor.py"))
        os.close(module_pin)
        sys.path.insert(0, module_dir)
        import supervisor
        pam_exe = supervisor.protected(pam_paths[0])
        pam_audit = supervisor.protected(pam_paths[1], audit=True)
        import evidence
        evidence_audit = evidence.Audit(os.dup(pam_audit))
        evidence_service = evidence.Service(root_gen, verifier_gen, evidence_audit,
                                            lambda c, b: not getattr(c.tx, "stale", False), lab=True)

        # The storage generation now comes from the wineserver; the retired
        # STORAGE_QUERY operation is denied (see docs/E1_S18D_UNIX_SIDE.md).

    # The profile provisioning helper, pinned like the worker; without it no attempt can succeed.
    provision_exe = None
    if os.path.exists("/usr/local/libexec/wineexplorer/wex-profile-provision"):
        provision_exe = protected("/usr/local/libexec/wineexplorer/wex-profile-provision")

    server_exe = protected(server_path)
    server_stat = os.fstat(server_exe)
    system_gid = grp.getgrnam(system_group).gr_gid
    dbus.mainloop.glib.DBusGMainLoop(set_as_default=True)
    bus = dbus.SystemBus(private=True)
    daemon = dbus.Interface(bus.get_object("org.freedesktop.DBus", "/org/freedesktop/DBus"),
                            "org.freedesktop.DBus")
    name = dbus.service.BusName(BUS, bus, do_not_queue=True, allow_replacement=False)
    own = str(bus.get_unique_name())
    pending = {}
    seen_generations = set()
    server_generations = {}

    def prune_server_pins():
        for server_gen, (pid, pin) in list(server_generations.items()):
            if pin is not None and not alive(pin):
                os.close(pin)
                # Retain a bounded tombstone: closing a dead pin must not let
                # another server reuse that generation during this service.
                server_generations[server_gen] = pid, None

    class Transaction:
        def __init__(self, owner, pin):
            self.owner, self.pin = owner, pin
            self.kind = CHANNEL_KIND_A
            self.broker_pin = self.server_pin = self.child_pin = None
            self.control = self.data = self.child = None
            self.remote, self.watch, self.cancelled = (), None, False
            self.reply_watch = self.server_proc = None
            self.control_watch = None
            self.endorsed_records = {}
            self.pending_s6 = None
            self.pam = None
            self.evidence_conn = None

        def initialize(self):
            self.pid, self.uid, _, self.broker_pin = peer(self.pin)
            require(self.uid != 0, "open-broker-root")
            require(int(daemon.GetConnectionUnixProcessID(self.owner)) == self.pid, "open-broker-pid")
            require(int(daemon.GetConnectionUnixUser(self.owner)) == self.uid, "open-broker-uid")
            self.server_pin = self.child_pin = None
            self.control, remote_control = socket.socketpair(socket.AF_UNIX, socket.SOCK_SEQPACKET)
            self.remote = (remote_control,)
            self.data, remote_data = socket.socketpair(socket.AF_UNIX, socket.SOCK_SEQPACKET)
            self.remote = (remote_control, remote_data)
            for sock in (self.control, self.data):
                pass_identity(sock)
                sock.setblocking(False)
            self.challenge = (b"\0" * 24 + struct.pack("<QQ", root_gen, verifier_gen)
                              + random_bytes(32) + b"\0" * 24)
            self.deadline = time.monotonic_ns() + 5_000_000_000
            self.sequence, self.phase = 0, "endorse"
            self.reg = self.dialog_reg = self.template = None
            self.child = None
            # The evidence connection is created only after endorsement has set
            # server_gen/broker_gen/session (R9); see the pam-start phase.
            self.watch = GLib.io_add_watch(self.data.fileno(), GLib.IO_IN | GLib.IO_HUP | GLib.IO_ERR,
                                            self.message)
            self.control_watch = GLib.io_add_watch(self.control.fileno(), GLib.IO_IN | GLib.IO_HUP | GLib.IO_ERR,
                                                   self.control_message)

        def release_reply(self):
            self.reply_watch = None
            for remote in self.remote:
                remote.close()
            self.remote = ()
            return False

        def cancel(self):
            if self.cancelled:
                return
            self.cancelled = True
            if self.reply_watch is not None:
                GLib.source_remove(self.reply_watch)
            self.release_reply()
            if self.control_watch is not None:
                GLib.source_remove(self.control_watch)
                self.control_watch = None
            if hasattr(self, "endorsed_records"):
                for rec in list(self.endorsed_records.values()):
                    if r9_enabled:
                        if rec.get("state") not in ("REDEEMED", "RETIRED"):
                            rec["state"] = "CANCELLED"
                            rec.pop("evidence", None)
                            rec.pop("evidence_ref", None)
                        if self.kind == CHANNEL_KIND_A:
                            b0 = rec.get("b0_channel")
                            if b0 is not None:
                                b0.drop_record(rec)
                            else:
                                self.drop_record(rec)
                        else:
                            self.drop_record(rec)
                    else:
                        self.drop_record(rec)
                    for name in ("pidfd", "prefix"):
                        pfd = rec.pop(name, None)
                        if pfd is not None and pfd >= 0:
                            try:
                                os.close(pfd)
                            except OSError:
                                pass
                self.endorsed_records.clear()
            if hasattr(self, "pending_s6") and self.pending_s6 is not None:
                raw, cred, pin, fds = self.pending_s6
                if pin is not None and pin >= 0:
                    try:
                        os.close(pin)
                    except OSError:
                        pass
                for fd in fds:
                    if fd >= 0:
                        try:
                            os.close(fd)
                        except OSError:
                            pass
                self.pending_s6 = None
            if hasattr(self, "evidence_conn") and self.evidence_conn is not None:
                self.evidence_conn.close()
                self.evidence_conn = None
            if self.pam is not None:
                self.pam.close()
                self.pam = None
            if self.watch:
                GLib.source_remove(self.watch)
                self.watch = None
            for sock in (self.pin, self.control, self.data, self.child, *self.remote):
                if sock is not None:
                    sock.close()
            for fd in (self.broker_pin, self.server_pin, self.child_pin, self.server_proc):
                if fd is not None:
                    os.close(fd)
            if pending.get(self.owner) is self:
                del pending[self.owner]

        def drop_record(self, rec):
            """End one attempt's worker side: watch, secret end, control socket, worker and its pin.

            The dialog's pidfd stays with the record until the transaction ends. The evidence record is
            wiped: nothing can be redeemed afterwards.
            """
            watch = rec.pop("worker_watch", None)
            if watch is not None:
                GLib.source_remove(watch)
            for name in ("secret_v", "worker_ctrl"):
                sock = rec.pop(name, None)
                if sock is not None:
                    try:
                        sock.close()
                    except OSError:
                        pass
            proc = rec.pop("worker_proc", None)
            if proc is not None:
                import signal
                try:
                    os.killpg(proc.pid, signal.SIGKILL)
                except (ProcessLookupError, OSError):
                    pass
                try:
                    proc.wait(timeout=0.5)
                except Exception:
                    pass
            wpfd = rec.pop("worker_pidfd", None)
            if wpfd is not None and wpfd >= 0:
                try:
                    os.close(wpfd)
                except OSError:
                    pass
            rec.pop("evidence", None)
            rec.pop("evidence_ref", None)

        def provision_profile(self, rec, account_key, map_gen):
            """First use: create the authenticated administrator's profile in the caller's prefix.

            The helper looks the account up again in the protected map, takes the prefix only as the
            descriptor the endorsed server sent, and gives up root before it touches the prefix. An
            existing profile is validated and left alone. Without a profile nothing can be launched,
            so a failure ends the attempt.
            """
            require(provision_exe is not None, "profile-helper-not-provisioned")
            name = rec["account"]
            name = name.decode("ascii") if isinstance(name, bytes) else str(name)
            prefix = rec["prefix"]
            caller_uid = rec["a_channel"].uid if (r9_enabled and "a_channel" in rec) else self.uid
            done = subprocess.run(
                ["/proc/self/fd/%d" % provision_exe, name, account_key.hex(), str(map_gen),
                 str(caller_uid), str(prefix)],
                stdin=subprocess.DEVNULL, stdout=subprocess.PIPE, stderr=subprocess.DEVNULL,
                close_fds=True, pass_fds=(provision_exe, prefix), env={}, timeout=10)
            line = done.stdout.decode("ascii", "replace").strip().splitlines()[-1:] or [""]
            print("E1R8_PROFILE request=%x rc=%d %s" % (rec["tuple"].request_id, done.returncode, line[0][:80]),
                  flush=True)
            # 9 (busy): the profile exists and is locked, normally because this server has it loaded for
            # an earlier elevation. Nothing was created or changed; the server's own load decides.
            require(done.returncode in (0, 9), "profile-provision")

        def worker_message(self, source, condition, key):
            """The R8 worker's control socket: W_ACK after the dialog attached, then its private result."""
            rec = self.endorsed_records.get(key)
            try:
                require(rec is not None, "worker-record")
                self.live()
                t = rec["tuple"]
                data = rec["worker_ctrl"].recv(supervisor.RESULT_SIZE + 1)
                if rec["state"] == "ATTACH_RESERVED":
                    e1r8.decode_w_ack(data, t)
                    # The dialog holds the secret end now; this service never reads or watches it.
                    rec.pop("secret_v").close()
                    rec["state"] = "DELEGATED"
                    self.data.send(e1r8.encode_delegated(t))
                    rec["worker_ctrl"].send(e1r8.encode_prompt_release(t))
                    rec["state"] = "PAM"
                    print("E1R8_DELEGATED request=%x" % key[0], flush=True)
                    return True
                require(rec["state"] == "PAM", "worker-result-state")
                rec["worker_watch"] = None
                authenticated, account_key, uid, map_gen = supervisor.r8_result(data, key[0], key[1], t.nonce)
                # A result alone is not enough: the worker must have ended by itself with the matching status.
                require(rec["worker_proc"].wait(timeout=1) == (0 if authenticated else 1), "worker-exit")
                if authenticated:
                    # A decoy can never authenticate; the key is the one the broker selected.
                    require(any(rec["account_key"]) and account_key == rec["account_key"], "worker-account")
                    now = time.monotonic_ns()
                    require(now < t.deadline, "worker-result-deadline")
                    self.provision_profile(rec, account_key, map_gen)
                    # A success clears the account's count and gives the caller this attempt back.
                    if "throttle" in rec:
                        accounts, bucket = rec.pop("throttle")
                        accounts.rows.pop(bucket, None)
                    ref = random_bytes(16)
                    rec["evidence"] = evidence.EVIDENCE.pack(
                        3, 1, evidence.EVIDENCE_SIZE, 0x3ff, t.request_id, t.request_digest, t.binding_digest,
                        t.nonce, t.attempt, account_key, uid, now, t.deadline, root_gen, self.server_gen,
                        self.broker_gen, verifier_gen, map_gen, t.policy_gen, 1, 1)
                    rec["evidence_ref"] = ref
                    rec["state"] = "RESULT_RETAINED"
                    self.data.send(e1r8.encode_result_v(t, 1, account_key, rec["account"], ref))
                    print("E1R8_RESULT request=%x authenticated=1" % key[0], flush=True)
                else:
                    rec["state"] = "REFUSED"
                    self.data.send(e1r8.encode_result_v(t, 2, bytes(16), b"", bytes(16)))
                    print("E1R8_RESULT request=%x authenticated=0" % key[0], flush=True)
                return False
            except Exception as exc:
                if rec is not None:
                    rec["worker_watch"] = None
                audit_exception(exc)
                self.cancel()
                return False

        def live(self):
            require(not self.cancelled and time.monotonic_ns() < self.deadline, "live-deadline")
            require(str(daemon.GetNameOwner(BUS)) == own, "live-owner")
            require(int(daemon.GetConnectionUnixProcessID(self.owner)) == self.pid, "live-broker-connection")
            require(alive(self.broker_pin), "live-broker-pin")
            if self.server_pin is not None:
                require(alive(self.server_pin), "live-server-pin")
            if self.child_pin is not None:
                require(alive(self.child_pin), "live-child-pin")

        def receive_control_packet(self):
            data, identity, pins, fds = receive_control_msg(self.control)
            try:
                if len(data) >= 8 and data[:4] == b"E1R8":
                    version, msg_type = struct.unpack_from("<HH", data, 4)
                    if msg_type == 16:
                        if r9_enabled:
                            require(self.kind == CHANNEL_KIND_A, "endorsed-wrong-channel-kind")
                        require(len(data) == 244, "endorsed-size")
                        require(len(fds) == 2, "endorsed-rights-count")
                        d_pidfd = fds[0]
                        # The server's own prefix directory: the only place a profile may be provisioned.
                        prefix_stat = os.fstat(fds[1])
                        require(stat.S_ISDIR(prefix_stat.st_mode) and prefix_stat.st_uid == self.uid,
                                "endorsed-prefix")
                        require(identity is not None and len(identity) == 3, "endorsed-cred")
                        require(len(pins) == 1 and alive(pins[0]), "endorsed-sender-pin-count")
                        sender_pin = pins[0]

                        require(identity[1] == self.uid, "endorsed-uid")
                        require(identity[2] == system_gid, "endorsed-gid")
                        if self.server_proc is None:
                            self.server_proc = process_directory(sender_pin, identity[0])
                        check_executable(self.server_proc, sender_pin, identity[0], server_stat)
                        require(self.server_pin is not None and alive(self.server_pin), "endorsed-server-pin-live")
                        require(identity[0] == self.server_pid, "endorsed-server-pid")
                        require(child_pid_from_pin(sender_pin) == self.server_pid, "endorsed-sender-pid")

                        magic, ver, mtype, length, request_id, attempt, reserved = struct.unpack_from("<4sHHIQII", data, 0)
                        require(ver == 1, "endorsed-version")
                        require(length == 244, "endorsed-length")
                        require(reserved == 0, "endorsed-reserved")
                        require(attempt in (1, 2, 3), "endorsed-attempt")

                        body_pid = struct.unpack_from("<I", data, 236)[0]
                        body_uid = struct.unpack_from("<I", data, 240)[0]
                        require(alive(d_pidfd), "endorsed-dialog-alive")
                        require(child_pid_from_pin(d_pidfd) == body_pid, "endorsed-dialog-pid")
                        require(process_real_uid(body_pid, d_pidfd) == body_uid, "endorsed-dialog-uid")

                        require((request_id, attempt) not in self.endorsed_records, "endorsed-duplicate")
                        require(len(self.endorsed_records) < 8, "endorsed-capacity")
                        # A further attempt: the next number, and every earlier one refused or retired.
                        earlier = [(a, r["state"]) for (rid, a), r in self.endorsed_records.items()
                                   if rid == request_id]
                        require(attempt == len(earlier) + 1 and
                                all(state in ("REFUSED", "RETIRED") for _, state in earlier),
                                "endorsed-attempt-order")

                        fds.pop(0)
                        prefix_fd = fds.pop(0)
                        tail = e1r8.decode_tuple_tail(data[76:236])
                        t = e1r8.E1R8Tuple(request_id=request_id, attempt=attempt,
                                           nonce=data[28:60], channel_id=data[60:76])
                        t.binding_digest = tail.binding_digest
                        t.request_digest = tail.request_digest
                        t.deadline = tail.deadline
                        t.root_gen = tail.root_gen
                        t.server_gen = tail.server_gen
                        t.broker_gen = tail.broker_gen
                        t.verifier_gen = tail.verifier_gen
                        t.map_gen = tail.map_gen
                        t.policy_gen = tail.policy_gen
                        t.caller_session = tail.caller_session
                        t.desktop_route = tail.desktop_route
                        t.desktop_mode = tail.desktop_mode
                        t.dialog_id = tail.dialog_id

                        self.endorsed_records[(request_id, attempt)] = {
                            "tail": data[76:236],
                            "pid": body_pid,
                            "uid": body_uid,
                            "pidfd": d_pidfd,
                            "prefix": prefix_fd,
                            "tuple": t,
                            "state": "ENDORSED",
                            "a_channel": self,
                        }
                        # From here the person at the dialog sets the pace: the transaction lives until the
                        # broker's own attempt deadline (checked and bounded), not the 5 s setup bound.
                        # A deadline outside that bound extends nothing (the setup bound stays).
                        now = time.monotonic_ns()
                        if now < t.deadline <= now + 300_000_000_000:
                            self.deadline = t.deadline
                        print("E1R8_ENDORSED ok request=%x pid=%d" % (request_id, body_pid), flush=True)
                        return None
                    else:
                        require(False, "endorsed-unknown-r8-type")
                else:
                    require(len(data) == ENDORSE_SIZE, "endorsement-size")
                    require(len(fds) == 0, "endorsement-rights")
                    require(len(pins) == 1 and alive(pins[0]), "endorsement-pin")
                    pin = pins.pop(0)
                    return data, identity, pin, fds
            finally:
                for fd in fds + pins:
                    if fd >= 0:
                        os.close(fd)

        def control_message(self, source, condition):
            try:
                self.live()
                if condition & (GLib.IO_HUP | GLib.IO_ERR):
                    self.cancel()
                    return False
                msg = self.receive_control_packet()
                if msg is not None:
                    require(self.pending_s6 is None, "duplicate-pending-s6")
                    self.pending_s6 = msg
                return True
            except Exception as exc:
                audit_exception(exc)
                self.cancel()
                return False

        def endorsement(self, operation, child_pid=0):
            self.live()
            if self.pending_s6 is not None:
                msg = self.pending_s6
                self.pending_s6 = None
            else:
                while True:
                    poll = select.poll()
                    poll.register(self.control.fileno(), select.POLLIN)
                    timeout = min(1000, max(0, (self.deadline - time.monotonic_ns()) // 1_000_000))
                    require(poll.poll(timeout) == [(self.control.fileno(), select.POLLIN)],
                            "endorsement-control-ready")
                    # Readiness never extends the one absolute transaction deadline.
                    self.live()
                    msg = self.receive_control_packet()
                    if msg is None:
                        continue
                    break
            raw, cred, pin, rights = msg
            try:
                server_gen, broker_gen, broker_pid, session = struct.unpack_from("<QQII", raw)
                require(server_gen and broker_gen and broker_pid == self.pid, "endorsement-broker")
                require(raw[24:72] == self.challenge[24:72], "endorsement-challenge")
                require(struct.unpack_from("<QIIQ", raw, 72) == (self.sequence, operation, child_pid, 0),
                        "endorsement-operation-sequence")
                self.kind = r9_classify_channel(r9_enabled, s0_uid, self.uid, cred[1], cred[2], system_gid)
                # A wexsys process alone is insufficient: only the protected
                # installed wineserver inode, held from root-controlled startup.
                if self.server_proc is None:
                    self.server_proc = process_directory(pin, cred[0])
                check_executable(self.server_proc, pin, cred[0], server_stat)
                if self.server_pin is None:
                    # Refuse reused generations, even from another server PID.
                    require(broker_gen not in seen_generations, "endorsement-reused-broker-generation")
                    require(len(seen_generations) < 4096, "endorsement-broker-capacity")
                    prune_server_pins()
                    if server_gen in server_generations:
                        previous_pid, previous_pin = server_generations[server_gen]
                        require(cred[0] == previous_pid and previous_pin is not None and alive(previous_pin),
                                "endorsement-reused-server-generation")
                    else:
                        require(len(server_generations) < 4096, "endorsement-server-capacity")
                        server_generations[server_gen] = cred[0], os.dup(pin)
                    seen_generations.add(broker_gen)
                    self.server_pid = cred[0]
                    self.server_gen, self.broker_gen, self.session = server_gen, broker_gen, session
                    self.server_pin, pin = pin, None
                    self.host, self.channel = random_bytes(16), random_bytes(16)
                    gens = root_gen, server_gen, broker_gen, verifier_gen
                    self.reg = registration(1, random_bytes(16), gens, self.host, self.channel, session)
                else:
                    require((cred[0], server_gen, broker_gen, session) ==
                            (self.server_pid, self.server_gen, self.broker_gen, self.session), "endorsement-registration")
                self.sequence += 1
            finally:
                if pin is not None:
                    os.close(pin)

        def message(self, source, condition):
            try:
                self.live()
                if self.phase == "evidence-ready":
                    require(self.evidence_conn is not None)
                    self.evidence_conn.serve()
                    return True
                try:
                    peek = self.data.recv(12, socket.MSG_PEEK | socket.MSG_DONTWAIT)
                except (BlockingIOError, OSError):
                    peek = b""
                if len(peek) >= 12 and peek[:4] == b"E1R8":
                    ver, mtype, length = struct.unpack_from("<HHI", peek, 4)
                    if mtype == 17:  # START
                        raw, cred, pin, fds = receive(self.data, length, 0, length)
                        try:
                            require((cred[0], cred[1]) == (self.pid, self.uid), "message-broker-identity")
                            if r9_enabled:
                                require(self.kind == CHANNEL_KIND_B0, "start-wrong-channel-kind")
                                # Spec section 6 (and 12): the attempt's record was made by the server's ENDORSED
                                # on the agent's channel. Exactly one live A channel holds it; START's header must
                                # equal its sealed tuple (decode_start below), and that channel is of the session
                                # the tuple names. The header's channel id is the attempt's random token.
                                rid, attempt = struct.unpack_from("<QI", raw, 12)
                                holders = [tx for tx in pending.values()
                                           if not tx.cancelled and getattr(tx, "kind", None) == CHANNEL_KIND_A
                                           and (rid, attempt) in tx.endorsed_records]
                                require(len(holders) == 1, "start-a-channel-missing")
                                a_tx = holders[0]
                                require(a_tx.server_pin is not None and alive(a_tx.server_pin),
                                        "start-a-channel-unendorsed")
                                rec = a_tx.endorsed_records[(rid, attempt)]
                                require(rec.get("state") == "ENDORSED", "start-state-not-endorsed")
                                require(rec["tuple"].caller_session == a_tx.session, "start-session")
                                rec["a_channel"] = a_tx
                                rec["b0_channel"] = self
                                self.endorsed_records[(rid, attempt)] = rec
                            else:
                                rid, attempt = struct.unpack_from("<QI", raw, 12)
                                require((rid, attempt) in self.endorsed_records, "start-without-endorsed")
                                rec = self.endorsed_records[(rid, attempt)]
                                require(rec.get("state") == "ENDORSED", "start-state-not-endorsed")
                            start_msg = e1r8.decode_start(raw, rec["tuple"])

                            # With a lockout threshold: charge the attempt before any worker exists, so a
                            # crash, a cancel or a killed dialog counts too. An unknown name is counted under
                            # its own bucket, so it behaves like a real account. A successful
                            # authentication clears the account's count.
                            accounts = r8_lockout_accounts()
                            if accounts is not None:
                                bucket = (bytes(start_msg.account_key) if any(start_msg.account_key) else
                                          b"name:" + hashlib.sha256(bytes(start_msg.account)).digest()[:16])
                                if accounts.reserve(bucket, time.monotonic_ns()) is not None:
                                    # Locked out: no worker, no PAM call. The broker is told (lab frame 31,
                                    # header only) so that the dialog can say so; the attempt is over.
                                    rec["state"] = "REFUSED"
                                    print("E1R8_LOCKED request=%x" % rid, flush=True)
                                    self.data.send(e1r8._put_header(31, 76, rec["tuple"]))
                                    return True
                                rec["throttle"] = (accounts, bucket)

                            sec_v, sec_w = socket.socketpair(socket.AF_UNIX, socket.SOCK_SEQPACKET)
                            for s in (sec_v, sec_w):
                                s.setsockopt(socket.SOL_SOCKET, socket.SO_PASSCRED, 1)
                                s.setsockopt(socket.SOL_SOCKET, PASSPIDFD, 1)
                                require(s.getsockopt(socket.SOL_SOCKET, socket.SO_PASSCRED) != 0, "secret-passcred")
                                require(s.getsockopt(socket.SOL_SOCKET, PASSPIDFD) != 0, "secret-passpidfd")

                            w_setup = e1r8.encode_w_setup(rec["tuple"], rec["pid"], rec["uid"])
                            require(pam_exe is not None and pam_audit is not None, "pam-worker-not-provisioned")

                            w_proc, w_ctrl, w_pidfd, w_pid = supervisor.spawn_worker_bootstrap(
                                pam_exe, pam_audit, sec_w, w_setup, rec["pidfd"], raw
                            )
                            sec_w.close()
                            rec["worker_watch"] = GLib.io_add_watch(
                                w_ctrl.fileno(), GLib.IO_IN | GLib.IO_HUP | GLib.IO_ERR,
                                self.worker_message, (rid, attempt))

                            rec["state"] = "WORKER_READY"
                            rec["secret_v"] = sec_v
                            rec["worker_proc"] = w_proc
                            rec["worker_ctrl"] = w_ctrl
                            rec["worker_pidfd"] = w_pidfd
                            rec["worker_pid"] = w_pid
                            rec["account"] = start_msg.account
                            rec["account_key"] = start_msg.account_key

                            print("E1R8_WORKER_READY request=%x pid=%d" % (rid, w_pid), flush=True)

                            ready_pkt = e1r8.encode_worker_ready(rec["tuple"])
                            self.data.send(ready_pkt)
                            return True
                        finally:
                            if pin is not None:
                                os.close(pin)
                            for fd in fds:
                                os.close(fd)
                    if r9_enabled and mtype == 32:  # GRANT
                        raw, cred, pin, fds = receive(self.data, length, 0, length)
                        try:
                            require((cred[0], cred[1]) == (self.pid, self.uid), "message-broker-identity")
                            require(self.kind == CHANNEL_KIND_B0, "grant-wrong-channel-kind")
                            rid, attempt = struct.unpack_from("<QI", raw, 12)
                            rec = self.endorsed_records.get((rid, attempt))
                            require(rec is not None, "grant-without-attempt")
                            t = rec["tuple"]
                            grant_msg = e1r8.decode_grant(raw, t)
                            ok, new_state, reply, err = r9_handle_grant(rec, self.kind, grant_msg, r9_enabled=True)
                            require(ok, err or "grant-denied")
                            self.data.send(reply)
                            print("E1R8_GRANTED request=%x" % rid, flush=True)
                            return True
                        finally:
                            if pin is not None:
                                os.close(pin)
                            for fd in fds:
                                os.close(fd)
                    if r9_enabled and mtype == 21:  # CANCEL
                        raw, cred, pin, fds = receive(self.data, length, 0, length)
                        try:
                            require((cred[0], cred[1]) == (self.pid, self.uid), "message-broker-identity")
                            require(self.kind == CHANNEL_KIND_B0, "cancel-wrong-channel-kind")
                            rid, attempt = struct.unpack_from("<QI", raw, 12)
                            rec = self.endorsed_records.get((rid, attempt))
                            require(rec is not None, "cancel-unknown-attempt")
                            t = rec["tuple"]
                            cancel_msg = e1r8.decode_cancel(raw, t)
                            ok, new_state, err = r9_handle_cancel(rec, self.kind, cancel_msg, r9_enabled=True)
                            require(ok, err or "cancel-denied")
                            if new_state == "CANCELLED":
                                self.drop_record(rec)
                            return True
                        finally:
                            if pin is not None:
                                os.close(pin)
                            for fd in fds:
                                os.close(fd)
                    if mtype in (25, 28):  # RELEASE; REDEEM (lab frame: evidence_ref:16, answered by type 29)
                        raw, cred, pin, fds = receive(self.data, length, 0, length)
                        try:
                            require((cred[0], cred[1]) == (self.pid, self.uid), "message-broker-identity")
                            rid, attempt = struct.unpack_from("<QI", raw, 12)
                            if r9_enabled:
                                if mtype == 25:
                                    require(self.kind == CHANNEL_KIND_B0, "release-wrong-channel-kind")
                                    rec = self.endorsed_records.get((rid, attempt))
                                    require(rec is not None, "r8-message-without-endorsed")
                                    t = rec["tuple"]
                                    e1r8.decode_release(raw, t)
                                    self.drop_record(rec)
                                    rec["state"] = "RETIRED"
                                    self.data.send(e1r8.encode_released(t))
                                    print("E1R8_RELEASED request=%x" % rid, flush=True)
                                    return True
                                elif mtype == 28:
                                    require(self.kind == CHANNEL_KIND_A, "redeem-wrong-channel-kind")
                                    rec = self.endorsed_records.get((rid, attempt))
                                    require(rec is not None, "r8-message-without-endorsed")
                                    t = rec["tuple"]
                                    require(len(raw) == 92 and raw[:76] == e1r8._put_header(28, 92, t), "redeem-frame")
                                    now = time.monotonic_ns()
                                    ok, new_state, reply_29, err = r9_handle_redeem(
                                        rec, self.kind, raw[76:92], now, control_sock=self.control, r9_enabled=True)
                                    require(ok, err or "redeem-denied")
                                    self.data.send(reply_29)
                                    print("E1R8_REDEEMED request=%x" % rid, flush=True)
                                    return True
                            else:
                                rec = self.endorsed_records.get((rid, attempt))
                                require(rec is not None, "r8-message-without-endorsed")
                                t = rec["tuple"]
                                if mtype == 28:
                                    require(len(raw) == 92 and raw[:76] == e1r8._put_header(28, 92, t), "redeem-frame")
                                    # Burn before the comparison: one redemption, right or wrong.
                                    record, ref = rec.pop("evidence", None), rec.pop("evidence_ref", None)
                                    state, rec["state"] = rec["state"], "REDEEMED"
                                    require(state == "RESULT_RETAINED" and record is not None and ref == raw[76:92]
                                            and time.monotonic_ns() < t.deadline, "redeem-evidence")
                                    # R10: the receipt goes to the server first, on the control socket it retained
                                    # at endorsement, with this process's kernel credentials. The server builds
                                    # the consumed record from it; the broker's copy below installs nothing.
                                    name = rec["account"]
                                    name = name if isinstance(name, bytes) else str(name).encode("ascii")
                                    receipt = (e1r8._put_header(30, 316 + len(name), t) + ref + record +
                                               struct.pack("<I", len(name)) + name)
                                    # Lab negative only (this script is the lab verifier): withhold the receipt
                                    # to show that the broker's copy alone gets no token.
                                    require(os.environ.get("WEX_E1S6_LAB_NO_RECEIPT") == "1" or
                                            self.control.sendmsg([receipt], [(
                                        socket.SOL_SOCKET, socket.SCM_CREDENTIALS,
                                        struct.pack("3i", os.getpid(), os.getuid(), os.getgid()))]) == len(receipt),
                                        "receipt-send")
                                    self.data.send(e1r8._put_header(29, 296, t) + record)
                                    print("E1R8_REDEEMED request=%x" % rid, flush=True)
                                else:
                                    e1r8.decode_release(raw, t)
                                    self.drop_record(rec)
                                    rec["state"] = "RETIRED"
                                    self.data.send(e1r8.encode_released(t))
                                    print("E1R8_RELEASED request=%x" % rid, flush=True)
                                return True
                        finally:
                            if pin is not None:
                                os.close(pin)
                            for fd in fds:
                                os.close(fd)
                # Broker data are authenticated per message, not just by the
                # socket creator (a stolen/passed FD cannot impersonate it).
                size, count = ((8, 0) if self.phase == "endorse" else
                               (REG_SIZE + ANSWER_SIZE + 4, 1) if self.phase == "child" else
                               (REG_SIZE + 183, 0) if self.phase == "pam-start" else
                               (REG_SIZE + ANSWER_SIZE, 0))
                raw, cred, pin, fds = receive(self.data, size, count,
                    REG_SIZE + 121 if self.phase == "pam-start" else None)
                try:
                    require((cred[0], cred[1]) == (self.pid, self.uid), "message-broker-identity")
                    if self.phase == "endorse":
                        require(raw == b"E1S6OPEN", "open-packet")
                        self.endorsement(1)
                        verifier_reg = registration(2, random_bytes(16),
                            (root_gen, self.server_gen, self.broker_gen, verifier_gen),
                            random_bytes(16), self.channel, self.session)
                        print("E1S6_STAGE native phase=open", flush=True)
                        self.data.send(self.reg + verifier_reg)
                        self.phase = "child"
                    elif self.phase == "child":
                        child_pid = struct.unpack_from("<I", raw, REG_SIZE + ANSWER_SIZE)[0]
                        self.endorsement(2, child_pid)
                        require(raw[:REG_SIZE] == self.reg, "child-broker-registration")
                        template = raw[REG_SIZE:REG_SIZE + ANSWER_SIZE]
                        answer(template)
                        require(struct.unpack_from("<I", template, 36)[0] == 2, "child-answer-kind")
                        require(template[136:152] == self.channel, "child-answer-channel")
                        self.child = socket.socket(fileno=fds.pop())
                        self.child.settimeout(1)
                        # SO_PASS* was set by wexhost BEFORE it started the child.
                        hello, child_cred, child_pin, extra = receive(self.child, 8)
                        try:
                            require(hello == b"E1S6CHLD" and child_cred[:2] == (child_pid, self.uid), "child-identity")
                            self.child_pin, child_pin = child_pin, None
                        finally:
                            if child_pin is not None:
                                os.close(child_pin)
                        self.template = template
                        self.dialog_reg = registration(3, random_bytes(16),
                            (root_gen, self.server_gen, self.broker_gen, verifier_gen),
                            random_bytes(16), self.channel, self.session)
                        print("E1S6_STAGE native phase=child", flush=True)
                        self.data.send(self.dialog_reg)
                        self.phase = "pam-start" if pam_exe is not None else "exchange"
                    elif self.phase == "pam-start":
                        self.endorsement(3)
                        require(raw[:REG_SIZE] == self.dialog_reg)
                        start = raw[REG_SIZE:]
                        require(start[16:28] == self.template[16:28])
                        require(start[40:72] == self.template[40:72])
                        if evidence_service is not None:
                            self.evidence_conn = evidence.Connection(evidence_service, self, receive)
                            magic, ver, kind, size, rid, attempt, round_, index, style = struct.unpack_from("<8sHHIQIIII", start)
                            nonce = start[40:72]
                            key, uid, map_gen, expiry, limit, len_name = struct.unpack_from("<16sQQQII", start, 72)
                            broker_deadline(self, time.monotonic_ns(), expiry)
                            name = start[120:120 + len_name].decode("ascii", errors="replace")
                            decoy = (kind == 6)

                            b = evidence.Binding(
                                request_id=rid,
                                # S1 answer layout: nonce 40..72, binding digest
                                # 72..104, request digest 104..136 (R9).
                                request_digest=self.template[104:136],
                                binding_digest=self.template[72:104],
                                nonce=nonce,
                                account_key=key,
                                host_uid=uid if not decoy else 0,
                                root_gen=root_gen,
                                server_gen=self.server_gen,
                                broker_gen=self.broker_gen,
                                verifier_gen=verifier_gen,
                                map_gen=map_gen,
                                policy_gen=1,
                                created=time.monotonic_ns(),
                                expiry=expiry,
                                name="" if decoy else name
                            )

                            # R1: this service never reads, stores or manufactures a
                            # credential. Typed answers may only reach the worker over a
                            # private channel delegated to the registered dialog process
                            # (not built yet, R8). Until then the dialog end is closed at
                            # once, so every PAM conversation fails and no evidence issues.
                            worker_dialog, unwired_dialog = socket.socketpair(socket.AF_UNIX, socket.SOCK_SEQPACKET)
                            unwired_dialog.close()

                            def worker_factory(raw_start):
                                w = supervisor.Worker(pam_exe, pam_audit, worker_dialog, self.child_pin,
                                                      child_pid_from_pin(self.child_pin), self.uid, raw_start)
                                return w

                            if decoy:
                                evidence_service.reject_selection(self.evidence_conn, rid, name.encode("ascii", errors="replace"), worker_factory)
                            else:
                                evidence_service.open(self.evidence_conn, b)
                                evidence_service.begin(self.evidence_conn, rid, b, worker_factory)

                            r = evidence_service.requests.get((self.evidence_conn.uid, rid))
                            require(r is not None and r.worker is not None, "worker-spawned")
                            worker = r.worker

                            def on_worker_ready(src, cond):
                                try:
                                    self.evidence_conn.ready(rid)
                                    self.phase = "evidence-ready"
                                except Exception as exc:
                                    audit_exception(exc)
                                    self.cancel()
                                return False

                            GLib.io_add_watch(worker.control.fileno(), GLib.IO_IN | GLib.IO_HUP | GLib.IO_ERR, on_worker_ready)
                            self.phase = "pam-running"
                        else:
                            self.pam = supervisor.LabTransaction(self, pam_exe, pam_audit, start,
                                GLib, child_pid_from_pin(self.child_pin))
                            self.phase = "pam-running"
                    elif self.phase == "exchange":
                        self.endorsement(3)
                        require(raw[:REG_SIZE] == self.dialog_reg, "exchange-registration")
                        candidate = raw[REG_SIZE:]
                        answer(candidate)
                        if struct.unpack_from("<I", candidate, 36)[0] == 3:
                            require(candidate[:36] == self.template[:36] and candidate[40:] == self.template[40:],
                                    "cancel-template")
                            print("E1S6_STAGE native phase=cancel", flush=True)
                            self.cancel()
                            return False
                        require(candidate == self.template, "exchange-template")
                        self.child.send(self.template)
                        reply, child_cred, child_pin, extra = receive(self.child, ANSWER_SIZE)
                        try:
                            require(child_cred[:2] == (child_pid_from_pin(self.child_pin), self.uid), "exchange-child-identity")
                            require(reply == self.template, "exchange-child-echo")
                        finally:
                            os.close(child_pin)
                        # Completion is single-use; keep the broker socket until
                        # a replay/disconnect, but no later success can be sent.
                        self.live()
                        print("E1S6_STAGE native phase=exchange", flush=True)
                        self.data.send(reply)
                        self.phase = "complete"
                    else:
                        audit("late-replay", sys._getframe())
                        raise ValueError("late or replayed message")
                finally:
                    os.close(pin)
                    for fd in fds:
                        os.close(fd)
                return True
            except Exception as exc:
                # Never log packet content, prompts or arbitrary diagnostics.
                audit_exception(exc)
                self.cancel()
                return False

    class Endpoint(dbus.service.Object):
        @dbus.service.method(BUS, in_signature="huut", out_signature="ayhh", sender_keyword="sender")
        def Open(self, pin, minimum, maximum, caps, sender=None):
            require(int(minimum) == 1 and int(maximum) == 1 and int(caps) == 0, "open-negotiation")
            if sender in pending:
                pending[sender].cancel()
            require(len(pending) < 8 and len(seen_generations) < 4096
                    and len(server_generations) < 4096, "open-capacity")
            sock = tx = None
            try:
                sock = socket.socket(fileno=pin.take())
                tx = Transaction(str(sender), sock)
                tx.initialize()
                pending[str(sender)] = tx
                result = (dbus.ByteArray(tx.challenge), dbus.types.UnixFd(tx.remote[0].fileno()),
                          dbus.types.UnixFd(tx.remote[1].fileno()))
                # Retain sockets through synchronous D-Bus reply serialization.
                # Close on the next loop turn (or cancellation), not for the
                # whole transaction: a retained remote would mask broker EOF.
                tx.reply_watch = GLib.idle_add(tx.release_reply, priority=GLib.PRIORITY_HIGH)
                return result
            except Exception as exc:
                audit_exception(exc)
                if tx is not None:
                    tx.cancel()
                elif sock is not None:
                    sock.close()
                raise dbus.exceptions.DBusException("transport denied", name=BUS + ".Denied")

        @dbus.service.method(BUS, in_signature="tuayayay", out_signature="hhuuay", sender_keyword="sender")
        def DialogAttach(self, request_id, attempt, nonce, channel_id, dialog_id, sender=None):
            """R8 section 7 step 2: the endorsed dialog collects the secret end and the worker's pin.

            A refused call changes nothing in the attempt. The caller must be the very process the server
            endorsed: same pid and uid on the bus as the pinned dialog, pin still alive.
            """
            try:
                key = int(request_id), int(attempt)
                matches = [tx for tx in pending.values() if key in tx.endorsed_records
                           and (getattr(tx, "kind", CHANNEL_KIND_A) == CHANNEL_KIND_B0 if r9_enabled else True)]
                require(len(matches) == 1, "attach-unknown")
                tx = matches[0]
                rec = tx.endorsed_records[key]
                t = rec["tuple"]
                # Who is asking comes first: nobody but the endorsed dialog learns anything about the attempt.
                require(int(daemon.GetConnectionUnixProcessID(sender)) == rec["pid"], "attach-sender-pid")
                require(int(daemon.GetConnectionUnixUser(sender)) == rec["uid"], "attach-sender-uid")
                require(child_pid_from_pin(rec["pidfd"]) == rec["pid"], "attach-pin")
                require(rec["state"] == "WORKER_READY", "attach-state")
                require((bytes(bytearray(nonce)), bytes(bytearray(channel_id)), bytes(bytearray(dialog_id)))
                        == (bytes(t.nonce), bytes(t.channel_id), bytes(t.dialog_id)), "attach-tuple")
                tx.live()
                rec["state"] = "ATTACH_RESERVED"
                print("E1R8_ATTACH ok request=%x" % key[0], flush=True)
                return (dbus.types.UnixFd(rec["secret_v"].fileno()), dbus.types.UnixFd(rec["worker_pidfd"]),
                        dbus.UInt32(rec["worker_pid"]), dbus.UInt32(0), dbus.ByteArray(rec["tail"]))
            except Exception as exc:
                audit_exception(exc)
                raise dbus.exceptions.DBusException("attach denied", name=BUS + ".Denied")

    def tick():
        prune_server_pins()
        if evidence_service is not None:
            try:
                evidence_service.tick()
            except Exception:
                pass
        for tx in list(pending.values()):
            try:
                tx.live()
                for rec in list(tx.endorsed_records.values()):
                    if r9_enabled and rec.get("state") == "GRANTED":
                        now = time.monotonic_ns()
                        t = rec.get("tuple")
                        if t and now >= t.deadline:
                            rec["state"] = "CANCELLED"
                            rec["cancel_reason"] = e1r8.CANCEL_R9_DEADLINE
                            rec.pop("evidence", None)
                            rec.pop("evidence_ref", None)
                            tx.drop_record(rec)
                        a_tx = rec.get("a_channel")
                        if a_tx and (a_tx.cancelled or not alive(a_tx.pin)):
                            rec["state"] = "CANCELLED"
                            rec.pop("evidence", None)
                            rec.pop("evidence_ref", None)
                            tx.drop_record(rec)
                        b0_tx = rec.get("b0_channel")
                        if b0_tx and (b0_tx.cancelled or not alive(b0_tx.pin)):
                            rec["state"] = "CANCELLED"
                            rec.pop("evidence", None)
                            rec.pop("evidence_ref", None)
                            tx.drop_record(rec)
                    # The endorsed dialog's death ends its attempt (and with it the session).
                    # After redemption or a refusal the dialog is expected to leave; the broker's RELEASE
                    # retires the attempt.
                    # R9: after the grant the dialog has done its part (B0 sends it DONE before LAUNCH), so it
                    # may leave in GRANTED too; without R9 that state does not occur.
                    require(rec["state"] in ("GRANTED", "REDEEMED", "REFUSED", "RETIRED", "CANCELLED") or
                            alive(rec["pidfd"]), "live-dialog-pin")
                if tx.pam is not None:
                    require(not tx.pam.worker.cancelled(tx.child))
            except Exception as exc:
                audit_exception(exc)
                tx.cancel()
        return True

    def owner_changed(well_known, old, new):
        if str(well_known) == BUS and str(new) != own:
            for tx in list(pending.values()):
                tx.cancel()
        if str(well_known) in pending and not str(new):
            pending[str(well_known)].cancel()

    endpoint = Endpoint(bus, PATH)
    bus.add_signal_receiver(owner_changed, "NameOwnerChanged", "org.freedesktop.DBus",
                            bus_name="org.freedesktop.DBus")
    GLib.timeout_add(50, tick)
    print("E1S6 READY", flush=True)
    try:
        GLib.MainLoop().run()
    finally:
        for tx in list(pending.values()):
            tx.cancel()
        for _, fd in server_generations.values():
            if fd is not None:
                os.close(fd)
        os.close(server_exe)
        for fd in (pam_exe, pam_audit, provision_exe):
            if fd is not None:
                os.close(fd)


def child_pid_from_pin(fd):
    # This is the fdinfo of an already kernel-issued pidfd, never a PID lookup
    # used as an authentication authority. Dead/reaped pins fail closed.
    with open("/proc/self/fdinfo/%d" % fd, encoding="ascii") as info:
        values = [int(line.split()[1]) for line in info if line.startswith("Pid:")]
    require(len(values) == 1 and values[0] > 0 and alive(fd), "pidfd-pid-live")
    return values[0]


def impostor():
    """Same-UID native bus impostor, with forged endorsement on its own FD."""
    import dbus
    require(os.getuid() != 0)
    bus = dbus.SystemBus(private=True)
    daemon = dbus.Interface(bus.get_object("org.freedesktop.DBus", "/org/freedesktop/DBus"),
                            "org.freedesktop.DBus")
    owner = str(daemon.GetNameOwner(BUS))
    require(int(daemon.GetConnectionUnixUser(owner)) == 0)
    local, remote = socket.socketpair(socket.AF_UNIX, socket.SOCK_SEQPACKET)
    endpoint = dbus.Interface(bus.get_object(owner, PATH), BUS)
    try:
        raw, control_fd, data_fd = endpoint.Open(dbus.types.UnixFd(remote.fileno()),
                                                dbus.UInt32(1), dbus.UInt32(1), dbus.UInt64(0))
        control = socket.socket(fileno=control_fd.take())
        data = socket.socket(fileno=data_fd.take())
        try:
            forged = bytearray(raw)
            struct.pack_into("<QQII", forged, 0, generation(), generation(), os.getpid(), 1)
            struct.pack_into("<QIIQ", forged, 72, 0, 1, 0, 0)
            control.send(forged)  # real sender's executable/credentials cannot be forged
            data.send(b"E1S6OPEN")
            data.settimeout(2)
            require(data.recv(REG_SIZE * 2 + 1) == b"")
        finally:
            control.close()
            data.close()
        print("E1S6_IMPOSTOR PASS")
    finally:
        local.close()
        remote.close()
        bus.close()


if __name__ == "__main__":
    if len(sys.argv) == 2 and sys.argv[1] == "--impostor":
        impostor()
    elif len(sys.argv) == 5 and sys.argv[1] == "--lab-service" and sys.argv[3] == "--group":
        service(sys.argv[2], sys.argv[4])
    elif (len(sys.argv) == 9 and sys.argv[1] == "--lab-service" and sys.argv[3] == "--group"
          and sys.argv[5] == "--pam-worker" and sys.argv[7] == "--pam-audit"):
        service(sys.argv[2], sys.argv[4], (sys.argv[6], sys.argv[8]))
    else:
        raise SystemExit("lab only: --lab-service PROTECTED_WINESERVER --group SYSTEM_GROUP")

# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright 2026 WineExplorer contributors
"""Disabled S6 lab adapter. Only control metadata crosses Python; never answers.

The caller MUST supply S6's authenticated process pin and private dialog socket.
No public PID/path/UID assertion authenticates a caller. No evidence redemption.
"""
import os
import signal
import socket
import stat
import struct
import subprocess
import threading
import time

HEADER, PACKET, RESULT_SIZE = 72, 4168, 140
MAGIC = b"E1S7PAM\0"


def request(raw):
    if not 121 <= len(raw) <= 183:
        raise ValueError("worker denied")
    magic, version, kind, size, rid, attempt, round_, index, style = struct.unpack_from("<8sHHIQIIII", raw)
    key, uid, generation, deadline, limit, length = struct.unpack_from("<16sQQQII", raw, HEADER)
    decoy = kind == 6
    if (magic != MAGIC or version != 1 or kind not in (1, 6) or size != len(raw)
            or not rid or not 1 <= attempt <= limit <= 3 or round_ or index or style
            or not any(raw[40:72]) or uid >= 0xffffffff
            or (decoy and (any(key) or uid or raw[120:] != b"decoy"))
            or (not decoy and (not any(key) or not uid))
            or not generation or not 1 <= length <= 63 or len(raw) != 120 + length
            or not time.monotonic_ns() < deadline <= time.monotonic_ns() + 120_000_000_000):
        raise ValueError("worker denied")
    return deadline


def protected(path, *, audit=False):
    """Pin protected ancestors and final inode; refuse symlinks and ACLs."""
    if not path.startswith("/") or any(p in ("", ".", "..") for p in path.split("/")[1:]):
        raise ValueError("worker denied")
    fd = os.open("/", os.O_RDONLY | os.O_DIRECTORY | os.O_CLOEXEC)
    try:
        parts = path.split("/")[1:]
        for index, part in enumerate(parts):
            last = index == len(parts) - 1
            flags = (os.O_WRONLY | os.O_APPEND) if audit and last else os.O_RDONLY
            nxt = os.open(part, flags | os.O_NOFOLLOW | os.O_NONBLOCK | os.O_CLOEXEC, dir_fd=fd)
            os.close(fd)
            fd = nxt
            st = os.fstat(fd)
            if (st.st_uid or st.st_mode & 0o022 or
                    not (stat.S_ISREG(st.st_mode) if last else stat.S_ISDIR(st.st_mode))):
                raise ValueError("worker denied")
            try:
                os.getxattr(fd, "system.posix_acl_access")
            except OSError as exc:
                import errno
                if exc.errno not in (errno.ENODATA, errno.ENOTSUP):
                    raise
            else:
                raise ValueError("worker denied")
            if last and (st.st_nlink != 1 or (audit and stat.S_IMODE(st.st_mode) != 0o600)
                         or (not audit and (not st.st_mode & 0o100 or st.st_mode & 0o6000))):
                raise ValueError("worker denied")
        result, fd = fd, -1
        return result
    finally:
        if fd >= 0:
            os.close(fd)


class Worker:
    def __init__(self, executable_fd, audit_fd, dialog, peer_pin, peer_pid, peer_uid, raw, *, test_args=()):
        self.process = None
        self.control = None
        self.result = None
        self.consumed = False
        self.verified = False
        self.audit_fd = audit_fd
        self.cleanup_pending = False
        self.deadline = request(raw)
        self.decoy = struct.unpack_from("<H", raw, 10)[0] == 6
        self.binding = raw[16:28] + raw[40:72]
        self.identity = raw[72:104]
        local, remote = socket.socketpair(socket.AF_UNIX, socket.SOCK_SEQPACKET)
        self.control = local
        try:
            # Execute the pinned inode, never reopen a replaceable pathname.
            # The lab binary is root-started, never setuid/setgid or installed.
            args = ["/proc/self/fd/%d" % executable_fd,
                    "--executable-fd", str(executable_fd),
                    "--control-fd", str(remote.fileno()), "--dialog-fd", str(dialog.fileno()),
                    "--peer-pin-fd", str(peer_pin), "--peer-pid", str(peer_pid),
                    "--peer-uid", str(peer_uid), "--audit-fd", str(audit_fd), *test_args]
            self.process = subprocess.Popen(args, stdin=subprocess.DEVNULL, stdout=subprocess.DEVNULL,
                                            stderr=subprocess.DEVNULL, close_fds=True,
                                            pass_fds=(executable_fd, remote.fileno(), dialog.fileno(), peer_pin, audit_fd),
                                            process_group=0, env={})
            local.send(raw)
            local.setblocking(False)
        except BaseException:
            self.close()
            raise
        finally:
            remote.close()

    def collect(self):
        """Called only after control readiness. Store seed privately, once."""
        raw, ancillary, flags, _ = self.control.recvmsg(RESULT_SIZE + 1, 256, socket.MSG_CMSG_CLOEXEC)
        # The control pair is private to the service and its own child. It
        # cannot introduce FDs. Close any anyway before denying.
        for level, kind, value in ancillary:
            if level == socket.SOL_SOCKET and kind == socket.SCM_RIGHTS:
                import array
                fds = array.array("i")
                fds.frombytes(value[:len(value) - len(value) % fds.itemsize])
                for fd in fds:
                    os.close(fd)
        allowed_flags = socket.MSG_CMSG_CLOEXEC | socket.MSG_EOR
        if (flags & ~allowed_flags or ancillary or len(raw) != RESULT_SIZE or raw[:16] != struct.pack("<8sHHI", MAGIC, 1, 5, RESULT_SIZE)
                or raw[16:28] + raw[40:72] != self.binding or any(raw[28:40])):
            raise ValueError("worker denied")
        status = struct.unpack_from("<I", raw, 72)[0]
        if status not in (0, 1) or (status and any(raw[76:])):
            raise ValueError("worker denied")
        if not status and (self.decoy or raw[76:108] != self.identity or not any(raw[108:140])):
            raise ValueError("worker denied")
        if not time.monotonic_ns() < self.deadline:
            raise ValueError("worker denied")
        self.result = bytearray(raw)
        return status

    def take_seed(self):
        """Parent-local seed, never a portable approval. S8 owns real evidence."""
        if (not self.verified or self.consumed or self.result is None or any(self.result[72:76])
                or time.monotonic_ns() >= self.deadline):
            raise ValueError("worker denied")
        self.consumed = True
        seed = bytearray(self.result[108:140])
        self.result[:] = bytes(len(self.result))
        return seed

    def verify_exit(self):
        """A result alone is insufficient: confirm natural exit, without
        reaping/releasing the leader PID until close() signals its group.
        """
        until = min(time.monotonic_ns() + 200_000_000, self.deadline)
        while time.monotonic_ns() < until:
            info = os.waitid(os.P_PID, self.process.pid, os.WEXITED | os.WNOHANG | os.WNOWAIT)
            if info is not None:
                expected = 0 if self.result is not None and not any(self.result[72:76]) else 1
                if info.si_code != os.CLD_EXITED or info.si_status != expected:
                    raise ValueError("worker denied")
                self.verified = True
                return
            time.sleep(0.001)
        raise ValueError("worker denied")

    def cancelled(self, dialog):
        """Peek only public header bytes, so cancellation also stops a module
        blocked outside its PAM callback. Never copy an answer body to Python.
        The native worker remains the sole answer consumer.
        """
        if time.monotonic_ns() >= self.deadline:
            return True
        try:
            head = dialog.recv(HEADER, socket.MSG_PEEK | socket.MSG_DONTWAIT)
        except BlockingIOError:
            return False
        if not head:
            return True
        return (len(head) != HEADER or head[:12] != MAGIC + struct.pack("<HH", 1, 3)
                or head[16:28] + head[40:72] != self.binding)

    def close(self):
        """Bounded TERM/KILL of only our new process group, then wait/reap."""
        if self.process is not None:
            pid = self.process.pid
            # Kill the owned group even if its leader already exited: a PAM
            # module must not leave an auxiliary helper running in this group.
            try:
                os.killpg(pid, signal.SIGTERM)
            except ProcessLookupError:
                pass
            # Keep the leader unreaped until after the final group signal, so
            # its PID/process-group number cannot be reused in this interval.
            try:
                os.killpg(pid, signal.SIGKILL)
            except ProcessLookupError:
                pass
            try:
                self.process.wait(timeout=1)
            except subprocess.TimeoutExpired:
                # A kernel-uninterruptible child can outlast SIGKILL. Retain an
                # exact parent waiter instead of dropping ownership or keeping
                # the bus loop blocked. Never claim confirmed cleanup here.
                self.cleanup_pending = True
                threading.Thread(target=self.process.wait, name="e1s7-reap", daemon=False).start()
            self.process = None
            if not self.verified:
                # Best effort cleanup audit AFTER termination/reaping. Storage
                # failure can never turn this cancelled transaction into success.
                try:
                    rid, attempt = struct.unpack("<QI", self.binding[:12])
                    event = "reap-pending" if self.cleanup_pending else "supervisor-cleanup"
                    line = ("E1S7 request=%016x attempt=%d event=%s success=0\n" %
                            (rid, attempt, event)).encode("ascii")
                    if os.fstat(self.audit_fd).st_size < 16 * 1024 * 1024:
                        os.write(self.audit_fd, line)
                except OSError:
                    pass
        if self.control is not None:
            self.control.close()
            self.control = None
        if self.result is not None:
            self.result[:] = bytes(len(self.result))
            self.result = None


class LabTransaction:
    """GLib glue invoked only by the S6 root service's authenticated branch."""
    def __init__(self, tx, executable_fd, audit_fd, raw, glib, peer_pid):
        self.tx, self.glib = tx, glib
        self.watch = None
        self.worker = Worker(executable_fd, audit_fd, tx.child, tx.child_pin, peer_pid, tx.uid, raw)
        # S6's stub has a five-second setup deadline. The authentication request
        # can only shorten it; changing it to 120s is future integration work.
        self.worker.deadline = min(self.worker.deadline, tx.deadline)
        try:
            self.watch = glib.io_add_watch(self.worker.control.fileno(), glib.IO_IN | glib.IO_HUP | glib.IO_ERR, self.done)
        except BaseException:
            self.worker.close()
            raise

    def done(self, source, condition):
        self.watch = None
        try:
            self.tx.live()
            self.worker.collect()
            self.worker.verify_exit()
            # Do NOT send seed/account/PAM status to the broker. No S8 record
            # store or redemption exists. Finish the lab transaction by closing.
        except Exception:
            pass
        finally:
            self.tx.cancel()
        return False

    def close(self):
        if self.watch is not None:
            self.glib.source_remove(self.watch)
            self.watch = None
        self.worker.close()


def r8_result(raw, request_id, attempt, nonce):
    """The R8 worker's private S7 RESULT: (authenticated, key, uid, map generation).

    The seed bytes are checked for presence and dropped; they never leave here.
    """
    if (len(raw) != RESULT_SIZE or raw[:16] != struct.pack("<8sHHI", MAGIC, 1, 5, RESULT_SIZE)
            or struct.unpack_from("<QI", raw, 16) != (request_id, attempt) or any(raw[28:40])
            or raw[40:72] != nonce):
        raise ValueError("worker denied")
    status = struct.unpack_from("<I", raw, 72)[0]
    if status not in (0, 1) or (status and any(raw[76:])):
        raise ValueError("worker denied")
    if status:
        return False, bytes(16), 0, 0
    key, uid, generation = raw[76:92], *struct.unpack_from("<QQ", raw, 92)
    if not any(key) or not 0 < uid < 0xffffffff or not generation or not any(raw[108:140]):
        raise ValueError("worker denied")
    return True, bytes(key), uid, generation


def spawn_worker_bootstrap(executable_fd, audit_fd, secret_w, w_setup_bytes, d_pidfd, start_bytes):
    local, remote = socket.socketpair(socket.AF_UNIX, socket.SOCK_SEQPACKET)
    proc = None
    w_pidfd = None
    try:
        args = ["/proc/self/fd/%d" % executable_fd,
                "--executable-fd", str(executable_fd),
                "--control-fd", str(remote.fileno()),
                "--dialog-fd", str(secret_w.fileno()),
                "--audit-fd", str(audit_fd),
                "--bootstrap"]
        proc = subprocess.Popen(args, stdin=subprocess.DEVNULL, stdout=subprocess.DEVNULL,
                                stderr=subprocess.DEVNULL, close_fds=True,
                                pass_fds=(executable_fd, remote.fileno(), secret_w.fileno(), audit_fd),
                                process_group=0, env={})
        w_pidfd = os.pidfd_open(proc.pid, 0)
        ancillary = [(socket.SOL_SOCKET, socket.SCM_RIGHTS, struct.pack("i", d_pidfd))]
        sent = local.sendmsg([w_setup_bytes], ancillary)
        if sent != len(w_setup_bytes):
            raise ValueError("failed to send w_setup")
        # The broker's START, unchanged: the selected account and its key.
        if local.send(start_bytes) != len(start_bytes):
            raise ValueError("failed to send start")
        return proc, local, w_pidfd, proc.pid
    except BaseException:
        if w_pidfd is not None:
            os.close(w_pidfd)
        local.close()
        if proc is not None:
            try:
                os.killpg(proc.pid, signal.SIGKILL)
            except ProcessLookupError:
                pass
            proc.wait()
        raise
    finally:
        remote.close()


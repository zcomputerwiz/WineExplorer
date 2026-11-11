#!/usr/bin/python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright 2026 WineExplorer contributors
"""Root-only disposable guest client. Never print answers or secret-bearing exceptions."""
import datetime
import os
import pathlib
import pwd
import select
import socket
import stat
import struct
import subprocess
import sys
import time

sys.path.insert(0, sys.argv[3])
from supervisor import Worker, protected, MAGIC, HEADER


def require(ok):
    if not ok:
        raise RuntimeError("fixture assertion failed")


def frame(kind, rid, nonce, attempt=1, round_=0, index=0, style=0, payload=b""):
    return (struct.pack("<8sHHIQIIII", MAGIC, 1, kind, HEADER + len(payload), rid,
                        attempt, round_, index, style) + nonce + payload)


def main():
    root = pathlib.Path(sys.argv[1])
    service = sys.argv[2]
    require(os.getuid() == 0 and root.parent == pathlib.Path("/run") and root.name.startswith("wex-pam-test-"))
    require(root.stat().st_uid == 0 and stat.S_IMODE(root.stat().st_mode) == 0o700)
    started = datetime.datetime.now(datetime.timezone.utc).isoformat()
    # /root is protected; refuse a symlink/hardlink or permissive credential file.
    credential_path = pathlib.Path("/root/e1s-fixtures-passwords")
    fd = os.open(credential_path, os.O_RDONLY | os.O_NOFOLLOW | os.O_CLOEXEC)
    try:
        st = os.fstat(fd)
        require(stat.S_ISREG(st.st_mode) and st.st_uid == 0 and st.st_nlink == 1
                and stat.S_IMODE(st.st_mode) == 0o600 and st.st_size <= 65536)
        with os.fdopen(fd, "r", encoding="utf-8", closefd=False) as f:
            secrets = dict(line.rstrip("\n").split("=", 1) for line in f)
    finally:
        os.close(fd)
    for name in ("bob", "alice", "carol", "eve"):
        require(bool(secrets[name]))
    for name in ("wexs7_locked", "wexs7_expired"):
        secrets[name] = os.getrandom(24).hex()
        subprocess.run(["chpasswd"], input=(name + ":" + secrets[name] + "\n").encode(),
                       stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, check=True)
    subprocess.run(["passwd", "-l", "wexs7_locked"], stdout=subprocess.DEVNULL, check=True)
    subprocess.run(["chage", "-E", "1", "wexs7_expired"], check=True)
    config = root / "config"
    config.mkdir(mode=0o755)
    (config / "sysrun.conf").write_text("group=wexsys\nadmin_group=sudo\n", encoding="ascii")
    (config / "sysrun.conf").chmod(0o644)
    subprocess.run([str(root / "accounts"), "--directory", str(config), "sync", "--initialize"],
                   stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, check=True)
    lines = (config / "accounts.map").read_text(encoding="ascii").splitlines()
    generation = int(lines[0].split()[2])
    accounts = {line.split()[0]: line.split() for line in lines[1:] if line.split()[3] == "active"}
    for name in secrets:
        if name in ("bob", "alice", "carol", "eve", "wexs7_locked", "wexs7_expired"):
            require(name in accounts)
    require("wexs7_nopass" in accounts)
    pamdir = root / "pam.d"
    pamdir.mkdir(mode=0o700)
    allow = root / "allow"
    allow.write_text("bob\nalice\ncarol\neve\nwexs7_locked\nwexs7_expired\nwexs7_nopass\n", encoding="ascii")
    allow.chmod(0o600)
    auth = ("auth requisite pam_listfile.so item=user sense=allow onerr=fail file=" + str(allow) + "\n"
            "auth [success=1 default=ignore] pam_unix.so\n"
            "auth requisite pam_deny.so\nauth required pam_permit.so\n")
    account = "account requisite pam_unix.so\naccount required pam_permit.so\n"
    stacks = {"": auth + account}
    for suffix, args in (("multi", ""), ("rounds", "rounds=9"), ("prompt-size", "oversize"),
                         ("style", "unsupported"), ("block", "block")):
        stacks[suffix] = "auth requisite " + str(root / "pam-lab.so") + " " + args + "\n" + auth + account
    stacks["redirect"] = "auth required " + str(root / "pam-lab.so") + " redirect\naccount required pam_permit.so\n"
    # A nullok stack: pam_unix would accept an empty password, so only the
    # worker's PAM_DISALLOW_NULL_AUTHTOK can refuse a passwordless account.
    auth_nullok = ("auth requisite pam_listfile.so item=user sense=allow onerr=fail file=" + str(allow) + "\n"
                   "auth [success=1 default=ignore] pam_unix.so nullok\n"
                   "auth requisite pam_deny.so\nauth required pam_permit.so\n")
    stacks["nullok"] = auth_nullok + account
    for suffix, stack in stacks.items():
        (pamdir / (service + ("-" + suffix if suffix else ""))).write_text(stack, encoding="ascii")
    (pamdir / "other").write_text("auth required pam_permit.so\naccount required pam_permit.so\n", encoding="ascii")
    # These are all provisioning inputs, never request-selected PAM services.
    audit = root / "audit.log"
    audit.touch(mode=0o600, exist_ok=False)
    audit_fd = protected(str(audit), audit=True)
    exe_fd = protected(str(root / "worker"))
    peer_pin = os.pidfd_open(os.getpid())
    canaries = [value.encode() for value in secrets.values()] + [b"E1S7-SECRET-CANARY", b"E1S7-WRONG-CANARY"]
    seeds = []

    def run(case, user="alice", suffix="", expected=1, action="answer", seconds=12, decoy=False, raw_mode=None):
        print("CASE " + case, flush=True)
        row = accounts.get(user)
        key, uid = (bytes.fromhex(row[2]), int(row[1])) if row else (os.getrandom(16), 1234567)
        nonce, rid = os.getrandom(32), int.from_bytes(os.getrandom(8), "little") or 1
        deadline = time.monotonic_ns() + seconds * 1_000_000_000
        selected = b"decoy" if decoy else user.encode()
        if decoy:
            key, uid = bytes(16), 0
        start = frame(6 if decoy else 1, rid, nonce,
                      payload=struct.pack("<16sQQQII", key, uid, generation, deadline, 3, len(selected)) + selected)
        dialog, client = socket.socketpair(socket.AF_UNIX, socket.SOCK_SEQPACKET)
        worker = None
        natural = False
        cancelled = False
        pid = None
        rounds = set()
        prompts = 0
        transcript, answered_at, result_at = [], None, None
        submitted_at, first_prompt_at = time.monotonic(), None
        if raw_mode is None:
            raw_mode = bool(suffix)
        test_args = ("--test-service", service + ("-" + suffix if suffix else ""),
                     "--test-confdir", str(pamdir), "--test-account-dir", str(config))
        if raw_mode:
            test_args += ("--test-raw-conversation", "1")
        try:
            worker = Worker(exe_fd, audit_fd, dialog, peer_pin, os.getpid(), 0, start,
                            test_args=test_args)
            pid = worker.process.pid
            until = time.monotonic() + seconds + 2
            while time.monotonic() < until:
                # The same parent watchdog used by S6: a header peek, no secret
                # body copies, and cancellation independent of PAM callbacks.
                if worker.cancelled(dialog):
                    if action in ("close", "block-cancel"):
                        cancelled = True
                        break
                ready, _, _ = select.select([worker.control, client] if client.fileno() >= 0 else [worker.control], [], [], 0.03)
                if worker.control in ready:
                    result_at = time.monotonic()
                    try:
                        status = worker.collect()
                    except ValueError:
                        require(action in ("deadline", "block-deadline", "close") or time.monotonic_ns() >= deadline)
                    else:
                        require(status == expected)
                        worker.verify_exit()
                        if status == 0:
                            seed = worker.take_seed()
                            require(any(seed) and bytes(seed) not in seeds)
                            seeds.append(bytes(seed))
                            seed[:] = bytes(len(seed))
                            try:
                                worker.take_seed()
                            except ValueError:
                                pass
                            else:
                                require(False)
                    break
                if client in ready:
                    if first_prompt_at is None:
                        first_prompt_at = time.monotonic()
                    raw = client.recv(HEADER + 4097)
                    require(len(raw) >= HEADER and raw[:12] == MAGIC + struct.pack("<HH", 1, 2))
                    require(raw[16:28] + raw[40:72] == start[16:28] + start[40:72])
                    round_, index, style = struct.unpack_from("<III", raw, 28)
                    rounds.add(round_)
                    prompts += 1
                    if action == "deadline":
                        continue
                    if action == "close":
                        client.close()
                        continue
                    if action == "cancel":
                        client.send(frame(4, rid, nonce))
                        continue
                    text = raw[HEADER:]
                    transcript.append((round_, index, style, text))
                    if style in (3, 4):
                        reply = b""
                    elif style == 2:
                        reply = b"label"
                    elif text == b"fixture challenge":
                        reply = b"E1S7-SECRET-CANARY"
                    else:
                        require(style == 1)
                        reply = secrets.get(user, "E1S7-WRONG-CANARY").encode()
                    if action == "wrong" and style == 1:
                        reply = b"E1S7-WRONG-CANARY"
                    elif action == "empty" and style == 1:
                        reply = b""
                    elif action == "space" and style == 1:
                        reply = b" "
                    elif action == "oversize" and style == 1:
                        reply = b"x" * 4097
                    elif action == "utf8" and style == 1:
                        reply = b"\xc0\xaf"
                    elif action == "sequence":
                        index += 1
                    client.send(frame(3, rid, nonce, round_=round_, index=index, style=style, payload=reply))
                    answered_at = time.monotonic()
                if action == "block-cancel" and time.monotonic_ns() + (seconds - 1) * 1_000_000_000 >= deadline:
                    client.send(frame(4, rid, nonce))
            else:
                require(False)
            # Observe natural worker termination BEFORE cleanup's kill fallback.
            # A real PAM failure delay can continue after a failed callback.
            # Keep the original request deadline; never disable PAM's delay.
            end = min(time.monotonic() + (3.5 if action in ("cancel", "close") else 1.5),
                      deadline / 1_000_000_000 + 1)
            while time.monotonic() < end:
                info = os.waitid(os.P_PID, pid, os.WEXITED | os.WNOHANG | os.WNOWAIT)
                if info is not None:
                    natural = True
                    if expected == 0:
                        require(info.si_status == 0)
                    else:
                        require(info.si_status != 0)
                    break
                time.sleep(0.01)
            require(natural or cancelled)
            if suffix == "multi" and raw_mode:
                require(len(rounds) >= 3 and prompts >= 7)  # two module rounds plus pam_unix
        finally:
            if worker is not None:
                worker.close()
            dialog.close()
            client.close()
        require(pid is not None)
        records = [line for line in audit.read_bytes().splitlines()
                   if ("request=%016x" % rid).encode("ascii") in line]
        require(bool(records))
        if case == "expired-account":
            require(any(b"stage=4 reason=6" in line and b"success=0" in line for line in records))
        if case in ("wrong-password", "locked-account", "passwordless-empty",
                    "passwordless-space", "passwordless-wrong"):
            require(any(b"stage=3" in line and b"success=0" in line for line in records))
        try:
            os.waitpid(pid, os.WNOHANG)
        except ChildProcessError:
            pass
        else:
            require(False)  # supervisor must reap its exact child
        try:
            os.killpg(pid, 0)
        except ProcessLookupError:
            pass
        else:
            require(False)
        print("PASS " + case, flush=True)
        return (transcript,
                result_at - answered_at if result_at is not None and answered_at is not None else None,
                first_prompt_at - submitted_at if first_prompt_at is not None else None)

    try:
        # A success is released at once; only refusals are held until the fixed release time.
        _, elapsed, _ = run("success", expected=0)
        require(elapsed is not None and elapsed < 1.0)
        print("PASS success-released-at-once seconds=%.3f" % elapsed, flush=True)
        run("fresh-single-use-seed", expected=0)
        run("identity-only-nonadmin", user="eve", expected=0)
        run("wrong-password", action="wrong")
        run("unknown-account", user="wexs7_unknown")
        run("missing-fixed-service-no-other-fallback", suffix="missing")
        run("locked-account", user="wexs7_locked")
        run("expired-account", user="wexs7_expired")
        # A passwordless administrator must never authenticate, even with a
        # nullok stack: the worker's PAM_DISALLOW_NULL_AUTHTOK refuses an empty
        # answer, a single space and a wrong password, while a passworded
        # account still works under the same stack.
        run("passwordless-empty", user="wexs7_nopass", suffix="nullok", action="empty")
        run("passwordless-space", user="wexs7_nopass", suffix="nullok", action="space")
        run("passwordless-wrong", user="wexs7_nopass", suffix="nullok", action="wrong")
        run("passworded-nullok", user="alice", suffix="nullok", expected=0)
        # Exercise the real worker and pam_unix delay, not a Python sleep/mock.
        # Compare full public prompt bytes/order/style and post-answer latency.
        classes = []
        for sample in range(3):
            classes.append(run("timing-wrong-%d" % sample, action="wrong"))
            classes.append(run("timing-unknown-%d" % sample, user="wexs7_unknown"))
            classes.append(run("timing-ineligible-%d" % sample, user="eve", decoy=True))
        require(all(transcript == [(1, 0, 1, b"Password: ")] for transcript, _, _ in classes))
        times = [elapsed for _, elapsed, _ in classes]
        require(all(elapsed is not None and 1.9 <= elapsed <= 2.25 for elapsed in times))
        require(max(times) - min(times) <= 0.25)
        prompt_times = [elapsed for _, _, elapsed in classes]
        require(all(elapsed is not None for elapsed in prompt_times))
        require(max(prompt_times) - min(prompt_times) <= 0.25)
        print("PASS account-class-prompts-and-timing tolerance-ms=250", flush=True)
        for suffix in ("multi", "block"):
            transcript, elapsed, _ = run("fixed-policy-" + suffix, suffix=suffix, raw_mode=False)
            require(transcript == [(1, 0, 1, b"Password: ")] and elapsed is not None and 1.9 <= elapsed <= 2.25)
        run("multi-round-and-message", suffix="multi", expected=0)
        run("too-many-rounds", suffix="rounds")
        run("oversize-prompt", suffix="prompt-size")
        run("oversize-answer", action="oversize")
        run("invalid-utf8", action="utf8")
        run("wrong-sequence", action="sequence")
        run("unsupported-style", suffix="style")
        run("unexpected-final-user", suffix="redirect")
        run("deadline-exceeded", action="deadline", seconds=2)
        run("module-hang-deadline", suffix="block", action="block-deadline", seconds=2)
        run("cancel-mid-conversation", action="cancel")
        run("channel-close", action="close")
        run("cancel-outside-callback", suffix="block", action="block-cancel", seconds=4)
        # Worker/application audit and actual PAM journal. No trace of answers,
        # dummy OTP canary, actual fixture passwords or wrong-password canary.
        journal = subprocess.run(["journalctl", "--since", started, "--no-pager", "-o", "cat"],
                                 stdout=subprocess.PIPE, stderr=subprocess.DEVNULL, timeout=30, check=True).stdout
        logs = audit.read_bytes() + journal
        require(all(canary not in logs for canary in canaries))
        require(b"success=1" in audit.read_bytes() and b"success=0" in audit.read_bytes())
        print("PASS secret-scan-audit-and-journal", flush=True)
    finally:
        for fd in (audit_fd, exe_fd, peer_pin):
            os.close(fd)


if __name__ == "__main__":
    try:
        main()
    except BaseException:
        # Tracebacks can include source/arguments or exception diagnostic text.
        print("FAIL: PAM worker fixture (no secret diagnostics)", file=sys.stderr)
        raise SystemExit(1)

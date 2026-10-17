#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright 2026 WineExplorer contributors
"""Run installed Wine checks in the named user's actual X11 or Wayland session."""
import argparse
import os
from pathlib import Path
import pwd
import subprocess


def desktop_session(account):
    candidates = []
    for entry in Path("/proc").iterdir():
        if not entry.name.isdecimal():
            continue
        try:
            if entry.stat().st_uid != account.pw_uid:
                continue
            command = (entry / "cmdline").read_bytes().replace(b"\0", b" ").lower()
            if b"explorer.exe" not in command or b"/shell" not in command:
                continue
            environment = dict(item.split(b"=", 1) for item in
                               (entry / "environ").read_bytes().split(b"\0")
                               if b"=" in item)
            candidates.append((int(entry.name), environment))
        except (FileNotFoundError, PermissionError, ProcessLookupError):
            continue
    if not candidates:
        raise SystemExit(f"No Explorer shell session for {account.pw_name}")
    return max(candidates, key=lambda item: item[0])


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--user", default=os.environ.get("WEX_TEST_USER", "bobsmith"))
    parser.add_argument("--report", action="store_true")
    parser.add_argument("--user-file", type=Path)
    parser.add_argument("--log-file", type=Path)
    parser.add_argument("--wait", action="store_true")
    parser.add_argument("command", nargs=argparse.REMAINDER)
    args = parser.parse_args()
    if os.geteuid():
        raise SystemExit("Run through the test VM's root serial console.")
    account = pwd.getpwnam(args.user)
    shell_pid, raw_environment = desktop_session(account)
    environment = {os.fsdecode(key): os.fsdecode(value)
                   for key, value in raw_environment.items()}
    # /proc retains the loader's original environment, including internal
    # startup values that Wine has since unset. Their FD/address reservation
    # belongs to that process; a new Unix-launched Wine client cannot reuse it.
    for key in ("WINESERVERSOCKET", "WINELOADERNOEXEC", "WINEPRELOADRESERVE"):
        environment.pop(key, None)
    environment["HOME"] = account.pw_dir
    environment.setdefault("WINEPREFIX", account.pw_dir + "/.wine")
    if args.user_file:
        args.user_file.write_text(account.pw_name + "\n")
    if args.report:
        print(f"Desktop user: {account.pw_name}; Explorer PID: {shell_pid}")
        for key in ("DISPLAY", "WAYLAND_DISPLAY", "WINEPREFIX"):
            print(f"{key}={environment.get(key, '')}")
    if args.command:
        wine = environment.get("WINELOADER", "/usr/local/bin/wine")
        log_path = args.log_file or Path(f"/tmp/wex-program-check-{account.pw_name}.log")
        with log_path.open("ab") as log:
            process = subprocess.Popen(
                [wine, *args.command], env=environment, cwd=account.pw_dir,
                user=account.pw_uid, group=account.pw_gid,
                extra_groups=os.getgrouplist(account.pw_name, account.pw_gid),
                stdin=subprocess.DEVNULL, stdout=log, stderr=log,
                start_new_session=True)
        print(f"Started Wine PID {process.pid}; log: {log_path}")
        if args.wait:
            raise SystemExit(process.wait())
    elif not args.report:
        parser.error("supply --report or a Windows program")


if __name__ == "__main__":
    main()

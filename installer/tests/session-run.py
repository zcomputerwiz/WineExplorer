#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright 2026 WineExplorer contributors
"""Run installed checks in the named user's actual X11 or Wayland session."""
import argparse
import os
from pathlib import Path
import pwd
import re
import shutil
import subprocess
import time

# Window probes (installer/tests/safe-mode.sh, review 2026-11-23 "Needs a decision" #4): what is open in the session,
# read from its X server (the X11 session's, or XWayland's in the Wayland session, where Wine draws through X11)
# instead of OCR of a screenshot. One line per window of `xwininfo -root -tree`:
#      0x1c00004 "runtest.txt - Notepad": ("notepad.exe" "Notepad")  640x480+0+0  +10+30
TREE_LINE = re.compile(r'^\s*(0x[0-9a-fA-F]+) (?:"(.*)"|\(has no name\)): \((.*)\)\s+(\d+)x(\d+)[+-]-?\d+[+-]-?\d+'
                       r'\s+([+-]-?\d+)([+-]-?\d+)\s*$')
WINE_DESKTOP = re.compile(r" - Wine desktop$")   # the X11 session's virtual desktop window (/desktop=Name,WxH)


def parse_tree(text):
    """The windows of `xwininfo -root -tree` output: dicts with id, name, cls (the WM_CLASS words, lower case), w, h, x,
    y (absolute)."""
    windows = []
    for line in text.splitlines():
        m = TREE_LINE.match(line)
        if not m:
            continue
        ident, name, cls, w, h, x, y = m.groups()
        windows.append(dict(id=ident, name=name or "", cls=re.findall(r'"([^"]*)"', cls.lower()),
                            w=int(w), h=int(h), x=int(x.lstrip("+")), y=int(y.lstrip("+"))))   # "+-1": -1
    return windows


def root_size(text):
    """The screen's size from `xwininfo -root`."""
    w = re.search(r"^\s*Width:\s*(\d+)", text, re.M)
    h = re.search(r"^\s*Height:\s*(\d+)", text, re.M)
    return (int(w.group(1)), int(h.group(1))) if w and h else (0, 0)


def desktop_windows(windows, screen, viewable):
    """The Explorer desktop is up when the shell's desktop view (a screen-sized explorer.exe window that is not Wine's
    virtual desktop window itself) and the taskbar (an explorer.exe window along the bottom edge, the screen's width)
    are both viewable. Returns (view, taskbar) or None."""
    sw, sh = screen
    shell = [w for w in windows if "explorer.exe" in w["cls"] and not WINE_DESKTOP.search(w["name"])]
    view = [w for w in shell if (w["w"], w["h"], w["x"], w["y"]) == (sw, sh, 0, 0)]
    bar = [w for w in shell if w["w"] == sw and 0 < w["h"] < sh // 4 and w["y"] + w["h"] == sh]
    view = [w for w in view if viewable(w["id"])]
    bar = [w for w in bar if viewable(w["id"])]
    return (view[0], bar[0]) if view and bar else None


def x_query(environment, *args):
    env = {key: environment[key] for key in ("DISPLAY", "XAUTHORITY", "HOME") if key in environment}
    env["PATH"] = "/usr/bin:/bin"
    result = subprocess.run(["xwininfo", *args], env=env, capture_output=True, text=True, timeout=30)
    return result.stdout if result.returncode == 0 else ""


def probe(account, args):
    """--windows, --wait-window REGEX, --gone-window REGEX, --wait-desktop: 0 when met, 1 on timeout (with the window
    list), 3 when this machine has no xwininfo (the caller falls back to OCR)."""
    if not shutil.which("xwininfo", path="/usr/bin:/bin"):
        print("probe=unavailable")
        return 3
    end = time.monotonic() + args.timeout
    windows = []
    pattern = re.compile(args.wait_window or args.gone_window or ".", re.I)
    while True:
        try:
            _, raw = desktop_session(account)
            environment = {os.fsdecode(k): os.fsdecode(v) for k, v in raw.items()}
            environment.setdefault("HOME", account.pw_dir)
            windows = parse_tree(x_query(environment, "-root", "-tree"))
        except SystemExit:      # no Explorer session yet
            environment, windows = None, []
        viewable = lambda ident: "IsViewable" in x_query(environment, "-id", ident)
        if environment is not None and windows:
            if args.windows:
                for w in windows:
                    if w["name"] or w["cls"]:
                        print(f'window id={w["id"]} viewable={int(viewable(w["id"]))} size={w["w"]}x{w["h"]} '
                              f'at={w["x"]},{w["y"]} class={"/".join(w["cls"])} name={w["name"]}')
                return 0
            if args.wait_desktop:
                screen = root_size(x_query(environment, "-root"))
                found = desktop_windows(windows, screen, viewable)
                if found:
                    print(f"desktop=ready screen={screen[0]}x{screen[1]} view={found[0]['id']} taskbar={found[1]['id']}")
                    return 0
            else:
                named = [w for w in windows if w["name"] and pattern.search(w["name"]) and viewable(w["id"])]
                if args.wait_window and named:
                    print(f"window={named[0]['name']}")
                    return 0
                if args.gone_window and not named:
                    print("window=gone")
                    return 0
        if time.monotonic() >= end:
            break
        time.sleep(1)
    if args.windows:
        print("windows=none")
    else:
        print("desktop=none" if args.wait_desktop else ("window=none" if args.wait_window else "window=still-open"))
    if environment is None:
        print(f"no Explorer session for {account.pw_name}")
    for w in windows:
        if w["name"] or "explorer.exe" in w["cls"]:
            print(f'  window {w["id"]} {w["w"]}x{w["h"]}+{w["x"]}+{w["y"]} {"/".join(w["cls"])} "{w["name"]}"')
    return 1


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
            parent = Path("/proc") / (entry / "stat").read_text().rsplit(")", 1)[1].split()[1]
            try:
                raw = (entry / "environ").read_bytes()
            except PermissionError:
                raw = b""
            environment = dict(item.split(b"=", 1) for item in raw.split(b"\0") if b"=" in item)
            if not environment:
                # an administrator's elevated desktop in Safe Mode runs with the system group, so an account without
                # root cannot read its environment: the session script's (its parent, as it was started) gives the
                # display and the prefix; it has no WEXMODE (the script exports that later), so a report runs as root
                session = dict(item.split(b"=", 1) for item in (parent / "environ").read_bytes().split(b"\0")
                               if b"=" in item)
                environment = {**session, **environment}
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
    parser.add_argument("--native", action="store_true",
                        help="launch a Linux program without the shell's Wine environment")
    parser.add_argument("--native-wayland", default="wayland-0",
                        help="public socket in the controlled Wayland test session")
    parser.add_argument("--native-display", default=":0",
                        help="public XWayland display in the controlled Wayland test session")
    probes = parser.add_mutually_exclusive_group()
    probes.add_argument("--windows", action="store_true", help="list the session's X windows")
    probes.add_argument("--wait-window", metavar="REGEX", help="until a viewable window's title matches")
    probes.add_argument("--gone-window", metavar="REGEX", help="until no viewable window's title matches")
    probes.add_argument("--wait-desktop", action="store_true", help="until the desktop view and the taskbar show")
    parser.add_argument("--timeout", type=float, default=0, help="seconds the probes wait (default: look once)")
    parser.add_argument("command", nargs=argparse.REMAINDER)
    args = parser.parse_args()
    account = pwd.getpwnam(args.user)
    # Root may start programs in any account's session; an account without root (installed test disks have no root
    # serial login) only in its own.
    if os.geteuid() not in (0, account.pw_uid):
        raise SystemExit("Run through the test VM's root serial console, or as the desktop user.")
    if args.windows or args.wait_window or args.gone_window or args.wait_desktop:
        raise SystemExit(probe(account, args))
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
        for key in ("DISPLAY", "WAYLAND_DISPLAY", "WINEPREFIX", "WEXMODE"):
            print(f"{key}={environment.get(key, '')}")
    if args.command:
        wine = environment.get("WINELOADER", "/usr/local/bin/wine")
        if args.native:
            on_wayland = bool(environment.get("WAYLAND_DISPLAY"))
            environment = {key: value for key, value in environment.items()
                           if not key.startswith(("WINE", "WEX_"))}
            if on_wayland:
                environment["WAYLAND_DISPLAY"] = args.native_wayland
                environment["DISPLAY"] = args.native_display
            command = args.command
        else:
            command = [wine, *args.command]
        log_path = args.log_file or Path(f"/tmp/wex-program-check-{account.pw_name}.log")
        identity = {}
        if os.geteuid() == 0:
            identity = dict(user=account.pw_uid, group=account.pw_gid,
                            extra_groups=os.getgrouplist(account.pw_name, account.pw_gid))
        with log_path.open("ab") as log:
            process = subprocess.Popen(
                command, env=environment, cwd=account.pw_dir, **identity,
                stdin=subprocess.DEVNULL, stdout=log, stderr=log,
                start_new_session=True)
        print(f"Started {'native' if args.native else 'Wine'} PID {process.pid}; log: {log_path}")
        if args.wait:
            raise SystemExit(process.wait())
    elif not args.report:
        parser.error("supply --report or a Windows program")


if __name__ == "__main__":
    main()

#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Check the installed display manager on a stopped, read-only mounted test disk.

Usage: check-lightdm.py ROOT [AUTOLOGIN_USER]
An empty/omitted user expects the Welcome screen and its isolated greeter prefix.
"""
import configparser
import os
from pathlib import Path
import sys

root = Path(sys.argv[1])
autologin = sys.argv[2] if len(sys.argv) > 2 else ""
seat = {}
files = []
for directory in ("usr/share/lightdm/lightdm.conf.d", "usr/local/share/lightdm/lightdm.conf.d",
                  "etc/xdg/lightdm/lightdm.conf.d", "etc/lightdm/lightdm.conf.d"):
    files.extend(sorted((root / directory).glob("*.conf")))
files.append(root / "etc/lightdm/lightdm.conf")
for filename in files:
    config = configparser.ConfigParser(interpolation=None, strict=False)
    config.read(filename)
    for section in config.sections():
        # LightDM treats this deprecated name as Seat:*. AccountsService uses it.
        if section in ("Seat:*", "SeatDefaults"):
            seat.update(config[section])
assert seat.get("greeter-session") == "wineexplorer-greeter", "custom greeter not selected"
assert seat.get("user-session") == "wine-explorer", "wrong desktop session"
assert seat.get("autologin-user", "") == autologin, "wrong automatic logon setting"
assert seat.get("allow-guest") == "false", "guest logon enabled"
manager = root / "etc/systemd/system/display-manager.service"
assert manager.is_symlink() and Path(manager.readlink()).name == "lightdm.service", "wrong display manager"
assert (root / "etc/X11/default-display-manager").read_text().strip() == "/usr/sbin/lightdm"
assert (root / "usr/local/bin/wex-greeter.sh").is_file(), "greeter launcher missing"
assert (root / "usr/share/xgreeters/wineexplorer-greeter.desktop").is_file(), "greeter definition missing"
assert (root / "usr/share/xgreeters/lightdm-gtk-greeter.desktop").is_file(), "recovery greeter missing"
passwd = [line.split(":") for line in (root / "etc/passwd").read_text().splitlines()]
account = next(row for row in passwd if row[0] == "lightdm")
uid, gid = int(account[2]), int(account[3])
home = root / "var/lib/lightdm"
assert home.is_dir() and (home.stat().st_uid, home.stat().st_gid) == (uid, gid), "LightDM home ownership lost"
# the greeter's prefix is the account's ~/.wine, the only prefix wex-sysrun serves (docs/SAFE_MODE_WORKORDER.md C7)
assert account[5] == "/var/lib/lightdm", f"lightdm's home is {account[5]}, not /var/lib/lightdm"
if not autologin:
    prefix = home / ".wine"
    if not prefix.exists():
        prefix = home / ".wine-logon"   # made before the move; the greeter moves it at its next start
    # a made prefix has user.reg; under the system role its HKLM comes from the machine root (no system.reg)
    assert os.path.lexists(prefix / "user.reg"), "greeter prefix has no user.reg"
    assert prefix.stat().st_uid == uid, "greeter prefix not owned by lightdm"
print("PASS: installed LightDM, custom/recovery greeters, session, automatic logon and prefix isolation")

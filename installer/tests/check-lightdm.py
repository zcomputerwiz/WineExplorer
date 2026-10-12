#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Check the installed display manager on a stopped, read-only mounted test disk.

Usage: check-lightdm.py ROOT [AUTOLOGIN_USER]
An empty/omitted user expects the Welcome screen and its isolated greeter prefix.
"""
import configparser
from pathlib import Path
import sys

root = Path(sys.argv[1])
autologin = sys.argv[2] if len(sys.argv) > 2 else ""
config = configparser.ConfigParser(interpolation=None, strict=False)
for directory in ("usr/share/lightdm/lightdm.conf.d", "etc/lightdm/lightdm.conf.d"):
    config.read(sorted((root / directory).glob("*.conf")))
config.read(root / "etc/lightdm/lightdm.conf")
seat = config["Seat:*"]
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
if not autologin:
    passwd = [line.split(":") for line in (root / "etc/passwd").read_text().splitlines()]
    uid = int(next(row[2] for row in passwd if row[0] == "lightdm"))
    registry = root / "var/lib/lightdm/.wine-logon/system.reg"
    assert registry.is_file() and registry.stat().st_uid == uid, "greeter prefix not owned by lightdm"
print("PASS: installed LightDM, custom/recovery greeters, session, automatic logon and prefix isolation")

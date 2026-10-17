#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Read-only account assertions against a mounted interactive-install root."""
import ctypes
import ctypes.util
from pathlib import Path
import sys
import subprocess

root = Path(sys.argv[1])
password = Path(sys.argv[2]).read_bytes().replace(b"\r", b"").replace(b"\n", b"")
passwd = [line.split(":") for line in (root / "etc/passwd").read_text().splitlines()]
names = {row[4].split(",")[0]: row[0] for row in passwd}
assert "Alice" in names and "Bob Smith" in names, "full account names missing"
shadow = dict((row[0], row[1]) for row in
              (line.split(":") for line in (root / "etc/shadow").read_text().splitlines()))
library = ctypes.CDLL(ctypes.util.find_library("crypt"))
library.crypt.argtypes = (ctypes.c_char_p, ctypes.c_char_p)
library.crypt.restype = ctypes.c_char_p
for username in ("root", names["Alice"]):
    digest = shadow[username].encode()
    assert digest and not digest.startswith((b"!", b"*")), f"{username} has no usable password"
    assert library.crypt(password, digest) == digest, f"{username} password does not match fixture"
assert not shadow[names["Bob Smith"]], "passwordless account has a password/lock marker"
sudo = next(row[3].split(",") for row in
            (line.split(":") for line in (root / "etc/group").read_text().splitlines())
            if row[0] == "sudo")
assert names["Alice"] in sudo, "first account is not in sudo"
assert not any(row[0] == "wexsetup" for row in passwd), "setup account remains"
assert not (root / "etc/wineexplorer/setup").exists(), "setup state remains"
for fullname in ("Alice", "Bob Smith"):
    username = names[fullname]
    log = (root / "home" / username / ".wine-session.log").read_text()
    assert f"USER={username} " in log, f"{fullname} did not start its own session"
subprocess.run([sys.executable, str(Path(__file__).with_name("check-lightdm.py")), str(root)], check=True)

# Prefix-split layout, symlink integrity, and per-user prefix size assertions
machine_root = root / "var/lib/wineexplorer/machine"
assert machine_root.is_dir(), "machine root /var/lib/wineexplorer/machine missing"
assert (machine_root / "drive_c/windows/system32").is_dir(), "machine root windows/system32 missing"
assert (machine_root / "system.reg").is_file(), "machine root system.reg missing"

fluent_theme = machine_root / "drive_c/windows/Resources/Themes/Fluent/Fluent.theme"
assert fluent_theme.is_file(), "Fluent.theme missing from installed machine root"
fluent_shell32 = machine_root / "drive_c/windows/Resources/Themes/Fluent/Icons/xpres_shell32.dll"
assert fluent_shell32.is_file(), "xpres_shell32.dll missing from Fluent theme in machine root"
fluent_cursor = machine_root / "drive_c/windows/Resources/Themes/Fluent/Cursors/arrow.cur"
assert fluent_cursor.is_file(), "arrow.cur missing from Fluent theme cursors in machine root"


skel_wine = root / "etc/skel/.wine"
assert skel_wine.is_dir(), "/etc/skel/.wine missing"
assert (skel_wine / "drive_c/windows").is_symlink(), "/etc/skel/.wine/drive_c/windows is not a symlink"
assert (skel_wine / "drive_c/Program Files").is_symlink(), "/etc/skel/.wine/drive_c/Program Files is not a symlink"
assert (skel_wine / "system.reg").is_symlink(), "/etc/skel/.wine/system.reg is not a symlink"
assert (skel_wine / "userdef.reg").is_symlink(), "/etc/skel/.wine/userdef.reg is not a symlink"

for fullname in ("Alice", "Bob Smith"):
    username = names[fullname]
    pfx = root / "home" / username / ".wine"
    assert pfx.is_dir(), f"{fullname} prefix missing"
    assert (pfx / "drive_c/windows").is_symlink(), f"{fullname} drive_c/windows is not a symlink"
    assert (pfx / "drive_c/Program Files").is_symlink(), f"{fullname} drive_c/Program Files is not a symlink"
    assert (pfx / "system.reg").is_symlink(), f"{fullname} system.reg is not a symlink"
    assert (pfx / "userdef.reg").is_symlink(), f"{fullname} userdef.reg is not a symlink"
    assert (pfx / "user.reg").is_file() and not (pfx / "user.reg").is_symlink(), f"{fullname} user.reg should be a real file"

    res = subprocess.run(["du", "-sm", str(pfx)], capture_output=True, text=True, check=True)
    size_mb = int(res.stdout.split()[0])
    print(f"{fullname} ({username}) prefix size: {size_mb} MB")
    assert size_mb < 20, f"{fullname} prefix size {size_mb} MB exceeds 20 MB limit (legacy was 1.7 GB)"

print("PASS: full names, root/Alice passwords, Bob passwordless state, administrator membership, prefix-split layout and per-user prefix size < 20 MB")

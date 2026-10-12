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
print("PASS: full names, root/Alice passwords, Bob passwordless state, administrator membership and setup cleanup")

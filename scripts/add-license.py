#!/usr/bin/env python3
"""Give every project-authored C/H/RC file the Wine-style LGPL 2.1+ header.

Targets: files added since the upstream Wine import, excluding code that came from ReactOS (programs/calc,
programs/wmplayer) or upstream Wine (dlls/ntoskrnl.exe).  Files that already mention the GNU license are left alone.
Usage: scripts/add-license.py [--dry-run]
"""
import re, subprocess, sys, os

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
BASE = "03476d7"
SKIP = re.compile(r"programs/(calc|wmplayer)/|ntoskrnl|/lang/")
COPY = " * Copyright 2026 WineExplorer Desktop Environment Project"
LICENSE = """ * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301, USA"""


def new_files():
    out = subprocess.check_output(["git", "diff", "--name-only", "--diff-filter=A", BASE, "HEAD", "--", "wine"],
                                  cwd=ROOT, stderr=subprocess.DEVNULL).decode().split("\n")
    out += subprocess.check_output(["git", "ls-files", "--others", "--exclude-standard", "--", "wine"],
                                   cwd=ROOT, stderr=subprocess.DEVNULL).decode().split("\n")
    return sorted({f for f in out if f.endswith((".c", ".h", ".rc")) and not SKIP.search(f)})


def process(path, dry):
    full = os.path.join(ROOT, path)
    raw = open(full, "rb").read().decode("utf-8", "replace")
    eol = "\r\n" if "\r\n" in raw else "\n"
    text = raw.replace("\r\n", "\n")
    if re.search(r"GNU|Lesser General", text[:3000]):
        return False
    m = re.match(r"\s*/\*(.*?)\*/\n?", text, re.S)
    if m:
        inner = [re.sub(r"^\s*\*? ?", "", l).rstrip() for l in m.group(1).strip("\n").split("\n")]
        while inner and not inner[0]: inner.pop(0)
        while inner and not inner[-1]: inner.pop()
        body = text[m.end():]
    else:
        inner = ["WineExplorer Desktop Environment - " + os.path.basename(path)]
        body = text
    has_copy = any("Copyright" in l for l in inner)
    lines = ["/*"] + [(" * " + l).rstrip() for l in inner] + [" *"]
    if not has_copy: lines += [COPY, " *"]
    lines += [LICENSE, " */"]
    result = "\n".join(lines) + "\n" + ("\n" if not body.startswith("\n") else "") + body
    if not dry:
        open(full, "wb").write(result.replace("\n", eol).encode("utf-8"))
    return True


if __name__ == "__main__":
    dry = "--dry-run" in sys.argv
    n = 0
    for f in new_files():
        if process(f, dry): n += 1; print(("would add: " if dry else "added: ") + f)
    print(n, "files")

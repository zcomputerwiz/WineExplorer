#!/usr/bin/env python3
"""Give every project-authored C/H/RC file the project's GPL-2.0-or-later header (see LICENSE.md).

Targets: project src files and Wine files added since the release tag, excluding code from ReactOS (programs/calc,
programs/wmplayer) or upstream Wine (dlls/ntoskrnl.exe).  Files that already mention the GNU license are left alone,
so older project files keep their LGPL-2.1-or-later header (compatible).
Usage: scripts/add-license.py [--dry-run]
"""
import re, subprocess, sys, os

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
BASE = "wine-11.19"   # The real release tag in the Wine submodule.
SKIP = re.compile(r"programs/(calc|wmplayer)/|ntoskrnl|/lang/")
COPY = " * Copyright 2026 WineExplorer Desktop Environment Project"
LICENSE = """ * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA."""


def new_files():
    out = subprocess.check_output(["git", "ls-files", "--", "src"],
                                  cwd=ROOT, stderr=subprocess.DEVNULL).decode().split("\n")
    out += subprocess.check_output(["git", "ls-files", "--others", "--exclude-standard", "--", "src"],
                                   cwd=ROOT, stderr=subprocess.DEVNULL).decode().split("\n")
    wine = os.path.join(ROOT, "wine")
    top = subprocess.check_output(["git", "-C", wine, "rev-parse", "--show-toplevel"]).decode().strip()
    if os.path.normcase(os.path.realpath(top)) == os.path.normcase(os.path.realpath(wine)):
        added = subprocess.check_output(["git", "-C", wine, "diff", "--name-only", "--diff-filter=A",
                                         BASE, "HEAD"], stderr=subprocess.DEVNULL).decode().splitlines()
        out += ["wine/" + path for path in added]
    return sorted({f for f in out if f.endswith((".c", ".h", ".rc")) and not SKIP.search(f)})


def process(path, dry):
    full = os.path.join(ROOT, path)
    with open(full, "rb") as source:
        raw = source.read().decode("utf-8", "replace")
    eol = "\r\n" if "\r\n" in raw else "\n"
    text = raw.replace("\r\n", "\n")
    notice = re.sub(r"(?m)^\s*\* ?", "", text[:3000])
    if re.search(r"GNU\s+(?:Lesser\s+)?General\s+Public\s+License|SPDX-License-Identifier:", notice):
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
        with open(full, "wb") as target:
            target.write(result.replace("\n", eol).encode("utf-8"))
    return True


if __name__ == "__main__":
    dry = "--dry-run" in sys.argv
    n = 0
    for f in new_files():
        if process(f, dry): n += 1; print(("would add: " if dry else "added: ") + f)
    print(n, "files")

#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Give every file resource in our .rc files a "/* @makedep: file */" line.

Wine's makedep only knows the files an .rc file pulls in through #include and @makedep comments.  Without the comment
a changed .ico or .bmp does not rebuild the .res, and an incremental build keeps the old picture (an open picture
replaced by scripts/open_assets.py, or a genuine one put over it by vm.sh sync).

  rc-makedep.py [--check] file.rc...      (default: all .rc files that reference pictures, except stock Wine's)
"""
import os, re, subprocess, sys

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
RES = re.compile(r'^\s*\w+\s+(ICON|BITMAP|CURSOR|AVI|HTML|RCDATA|PNG|MESSAGETABLE|TYPELIB|WAVE)\s+"([^"]+)"', re.I)
STOCK = ("dlls/oledlg/", "programs/progman/")   # upstream Wine files we leave alone


def files():
    out = subprocess.check_output(["git", "ls-files", "wine/*.rc"], cwd=ROOT).decode().split()
    return [f for f in out if not any(s in f for s in STOCK)]


def fix(path, check):
    full = os.path.join(ROOT, path)
    raw = open(full, "rb").read().decode("utf-8")
    eol = "\r\n" if "\r\n" in raw else "\n"
    lines = raw.split(eol)
    done, out, added = set(re.findall(r"@makedep:\s*(\S+)", raw)), [], 0
    for line in lines:
        m = RES.match(line)
        if m and m.group(2) not in done:
            indent = line[:len(line) - len(line.lstrip())]
            out.append("%s/* @makedep: %s */" % (indent, m.group(2)))
            done.add(m.group(2))
            added += 1
        out.append(line)
    if added and not check:
        open(full, "wb").write(eol.join(out).encode("utf-8"))
    return added


if __name__ == "__main__":
    check = "--check" in sys.argv
    targets = [a for a in sys.argv[1:] if not a.startswith("--")] or files()
    total = 0
    for t in targets:
        n = fix(t, check)
        if n: print("%s: %d %s" % (t, n, "missing" if check else "added"))
        total += n
    sys.exit(1 if check and total else 0)

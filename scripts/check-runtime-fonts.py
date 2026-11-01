#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright 2026 WineExplorer contributors
"""Check that a packaged Wine runtime ships Wine's own TrueType fonts.

Wine's fonts directory holds 50 bitmap .fon files and six TrueType fonts (Marlett, Symbol, Tahoma, Tahoma Bold,
Webdings, Wingdings). DirectWrite cannot use bitmap fonts, so only the six (plus the fonts the host provides) reach
its system font collection. Tahoma is the one that matters: it is Wine's default UI font and the only family that
Wine's DirectWrite font fallback names for Latin text. In a build tree the six are symlinks into the source tree,
which a packaging step that copies a fixed list of files, or that does not follow symlinks, silently drops. Without
tahoma.ttf the fallback returns no font, whatever the host provides (244 host families made no difference), Skia
cannot create Chromium's default typeface and Chromium's font initialization recurses until the stack is gone: the
browser's main thread dies about two seconds after the start (docs/CHROMIUM_MISSING_WINE_FONTS_20261101.md).

Usage: check-runtime-fonts.py RUNTIME_ROOT [RUNTIME_ROOT ...]
RUNTIME_ROOT is a build tree (it contains server/wineserver and fonts/) or an install prefix (share/wine/fonts/).
Exit status: 0 when every root has all six fonts as readable TrueType files, 1 otherwise.
"""
import os
import sys

REQUIRED = ("marlett.ttf", "symbol.ttf", "tahoma.ttf", "tahomabd.ttf", "webdings.ttf", "wingding.ttf")
LAYOUTS = ("fonts", os.path.join("share", "wine", "fonts"))
SFNT_HEADERS = (b"\x00\x01\x00\x00", b"true", b"OTTO", b"ttcf")


def problems_in(directory):
    """Return what is wrong with the six fonts in a directory (an empty list means all are fine)."""
    problems = []
    for name in REQUIRED:
        path = os.path.join(directory, name)
        if os.path.islink(path) and not os.path.exists(path):
            problems.append("%s: dangling symlink to %s" % (name, os.readlink(path)))
        elif not os.path.exists(path):
            problems.append("%s: missing" % name)
        elif not os.path.isfile(path):
            problems.append("%s: not a regular file" % name)
        else:
            size = os.path.getsize(path)
            with open(path, "rb") as font:
                header = font.read(4)
            if size < 1024 or header not in SFNT_HEADERS:
                problems.append("%s: not a TrueType font (%d bytes, header %r)" % (name, size, header))
    return problems


def check_root(root):
    """Print the verdict for one runtime root and return True when a fonts directory is complete."""
    found = False
    for layout in LAYOUTS:
        directory = os.path.join(root, layout)
        if not os.path.isdir(directory):
            continue
        found = True
        bitmap = len([n for n in os.listdir(directory) if n.lower().endswith(".fon")])
        problems = problems_in(directory)
        if not problems:
            print("ok:   %s (%d bitmap fonts, all %d TrueType fonts present)" % (directory, bitmap, len(REQUIRED)))
            return True
        print("FAIL: %s (%d bitmap fonts)" % (directory, bitmap))
        for problem in problems:
            print("      %s" % problem)
    if not found:
        print("FAIL: %s has no fonts directory (looked for %s)" % (root, ", ".join(LAYOUTS)))
    return False


def main():
    roots = sys.argv[1:]
    if not roots or roots[0] in ("-h", "--help"):
        print(__doc__)
        return 2
    results = [check_root(root) for root in roots]
    return 0 if all(results) else 1


if __name__ == "__main__":
    sys.exit(main())

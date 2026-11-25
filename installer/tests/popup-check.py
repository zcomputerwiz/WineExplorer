#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright 2026 WineExplorer contributors
"""The popup of a screenshot, for the `popups` phase of installer/tests/safe-mode.sh (docs/DWM_ARCHITECTURE.md, P7b slice 2).

    popup-check.py BEFORE.txt AFTER.txt SCREEN.ppm COMPOSITION [strict]

BEFORE.txt and AFTER.txt are the window probe's `--windows` output (session-run.py) before and after the popup was opened,
SCREEN.ppm a QEMU screendump taken while it is open, COMPOSITION 1 when the guest has desktop composition on (0 otherwise).
The popup is the largest window that is viewable in AFTER and was not viewable in BEFORE (absent, or hidden: a tooltip window exists, 1x1 and hidden, before it shows). Asserted:

  * a popup was found (a new viewable window of at least 12x12 pixels);
  * no black box: of the 12 pixels that lie 3 pixels outside its edges (the four corners and the middle of each side) that
    are on the screen, none is pure black. A popup that Wine cannot blend used to show a black rectangle around it;
  * with composition on: the soft shadow (a failure only with the sixth argument "strict"; otherwise a note, since the shell's own popups do not use the glass path yet). On at least one side, the pixel 2 pixels outside the middle of the edge is darker
    than the pixel 30 pixels outside it (a shadow falls off over a dozen pixels), in each colour channel by no more than it
    is darker overall; a side that has less than 32 pixels of screen is not used.

Prints what it measured. Exit 0 when everything holds, 1 when not, 2 for a bad input.
"""
import importlib.util
import re
import sys
from pathlib import Path

_spec = importlib.util.spec_from_file_location("highlight_row", Path(__file__).with_name("highlight-row.py"))
_highlight_row = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(_highlight_row)

LINE = re.compile(r"window id=(\S+) viewable=(\d) size=(\d+)x(\d+) at=(-?\d+),(-?\d+) class=\S* name=(.*)")


def windows(path):
    found = {}
    for line in Path(path).read_text(errors="replace").splitlines():
        m = LINE.match(line)
        if m:
            ident, viewable, w, h, x, y, name = m.groups()
            found[ident] = dict(id=ident, viewable=viewable == "1", w=int(w), h=int(h), x=int(x), y=int(y), name=name)
    return found


def popup_of(before, after):
    new = [w for ident, w in after.items() if w["viewable"] and w["w"] >= 12 and w["h"] >= 12
           and (ident not in before or not before[ident]["viewable"])]
    return max(new, key=lambda w: w["w"] * w["h"], default=None)


def main(argv):
    if len(argv) not in (5, 6) or argv[4] not in ("0", "1") or (len(argv) == 6 and argv[5] != "strict"):
        print(__doc__)
        return 2
    before, after = windows(argv[1]), windows(argv[2])
    width, height, pixels = _highlight_row.read_ppm(argv[3])
    composition = argv[4] == "1"
    strict = len(argv) == 6
    popup = popup_of(before, after)
    if popup is None:
        print("no new viewable window of at least 12x12 pixels after the action")
        return 1
    x, y, w, h = popup["x"], popup["y"], popup["w"], popup["h"]
    print(f"popup {popup['id']} {w}x{h} at {x},{y} (name '{popup['name']}') on a {width}x{height} screen")

    def at(px, py):
        if not (0 <= px < width and 0 <= py < height):
            return None
        i = (py * width + px) * 3
        return tuple(pixels[i:i + 3])

    failed = []
    for name, px, py in (("top left", x - 3, y - 3), ("top", x + w // 2, y - 3), ("top right", x + w + 2, y - 3),
                         ("left", x - 3, y + h // 2), ("right", x + w + 2, y + h // 2),
                         ("bottom left", x - 3, y + h + 2), ("bottom", x + w // 2, y + h + 2),
                         ("bottom right", x + w + 2, y + h + 2)):
        pixel = at(px, py)
        if pixel == (0, 0, 0):
            failed.append(f"black box: {name} ({px},{py}) is black")
        print(f"  {name:12s} ({px},{py}): {pixel}")
    if composition:
        shadow = []
        for name, near, far, room in (("top", (x + w // 2, y - 2), (x + w // 2, y - 30), y),
                                      ("bottom", (x + w // 2, y + h + 1), (x + w // 2, y + h + 29), height - y - h),
                                      ("left", (x - 2, y + h // 2), (x - 30, y + h // 2), x),
                                      ("right", (x + w + 1, y + h // 2), (x + w + 29, y + h // 2), width - x - w)):
            if room < 32:
                continue
            a, b = at(*near), at(*far)
            print(f"  shadow {name}: 2 px out {a}, 30 px out {b}")
            if a and b and sum(a) + 6 <= sum(b) and all(p <= q for p, q in zip(a, b)):
                shadow.append(name)
        if not shadow:
            msg = "composition is on, but no side has a shadow (darker 2 px out than 30 px out)"
            if strict:
                failed.append(msg)
            else:
                print("  NOTE:", msg, "(informational: the shell popups do not use the glass path yet)")
        else:
            print(f"  shadow on: {', '.join(shadow)}")
    for reason in failed:
        print("FAIL:", reason)
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))

#!/usr/bin/env python3
"""Font sets for the WineExplorer prefix, driven by assets/font_substitutes.json.

  fonts.py list            show the table (MS font -> open replacement, kind, license, installed?)
  fonts.py files xp        print the genuine XP font files (references/xp_export/fonts) that the table names
  fonts.py reg             print a .reg that maps each MS family to its open replacement
                           (HKCU/Software/Wine/Fonts/Replacements); builtin rows are skipped
Applied to the VM by `scripts/vm.sh fonts xp|open`.
"""
import json, os, sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
TABLE = os.path.join(ROOT, "assets", "font_substitutes.json")
XPDIR = os.path.join(ROOT, "references", "xp_export", "fonts")


def rows():
    with open(TABLE, encoding="utf-8") as f:
        return json.load(f)["fonts"]


def xp_files():
    return sorted({n for r in rows() for n in r["ms_files"]})


def main(argv):
    cmd = argv[1] if len(argv) > 1 else "list"
    if cmd == "list":
        for r in rows():
            have = all(os.path.exists(os.path.join(XPDIR, n)) for n in r["ms_files"]) if r["ms_files"] else None
            print("%-22s -> %-18s %-8s %-14s xp-export:%s" % (
                r["ms_font"], r["replacement"], r["kind"], r["license"][:14],
                "n/a" if have is None else ("yes" if have else "MISSING")))
    elif cmd == "files":
        for n in xp_files():
            print(os.path.join(XPDIR, n))
    elif cmd == "reg":
        print("Windows Registry Editor Version 5.00\n")
        print("[HKEY_CURRENT_USER" + chr(92) + "Software" + chr(92) + "Wine" + chr(92) + "Fonts" + chr(92) + "Replacements]")
        for r in rows():
            if r["kind"] in ("metric", "approx") and r["ms_font"] != "Segoe UI":
                print('"%s"="%s"' % (r["ms_font"], r["replacement"]))
    else:
        sys.exit(__doc__)


main(sys.argv)

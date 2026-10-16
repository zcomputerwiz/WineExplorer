#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright 2026 WineExplorer contributors
"""Build an icon pack (xpres_<module>.dll and a .theme) from an icon-set mapping in assets/iconpacks.

  build_iconpack.py MAPPING FLUENT_REPO OUT_DIR

MAPPING is a JSON file (assets/iconpacks/*.json): resource id -> [Fluent icon name, palette colour].  Each icon is the
Fluent UI System Icons filled glyph (MIT), tinted with the colour and rendered at 16, 24, 32, 48 and 256 pixels into
one .ico; the icons become a resource-only DLL.  OUT_DIR receives <Name>/<Name>.theme, <Name>/Icons/xpres_<module>.dll
and the Fluent licence.  Needs rsvg-convert, icotool and the x86_64 MinGW toolchain.
"""
import json, os, re, shutil, subprocess, sys, tempfile

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
SIZES = (16, 24, 32, 48, 256)


def svg_for(repo, name, size):
    """The filled SVG of the icon, at the nearest size not below SIZE (else the largest)."""
    folder = os.path.join(repo, "assets", " ".join(w.capitalize() for w in name.split("_")), "SVG")
    found = {}
    for f in os.listdir(folder):
        m = re.fullmatch(r"ic_fluent_%s_(\d+)_filled\.svg" % re.escape(name), f)
        if m:
            found[int(m.group(1))] = os.path.join(folder, f)
    if not found:
        raise SystemExit(f"no filled SVG for {name} in {folder}")
    bigger = [s for s in found if s >= min(size, 48)]
    return found[min(bigger) if bigger else max(found)]


def build_ico(repo, name, colour, out, work):
    pngs = []
    for size in SIZES:
        text = open(svg_for(repo, name, size)).read()
        text = re.sub(r'fill="#[0-9A-Fa-f]{6}"', f'fill="{colour}"', text)
        if 'fill="' not in text:
            text = text.replace("<path ", f'<path fill="{colour}" ')
        svg = os.path.join(work, f"{name}_{size}.svg")
        png = os.path.join(work, f"{name}_{size}_{colour[1:]}.png")
        open(svg, "w").write(text)
        subprocess.run(["rsvg-convert", "-w", str(size), "-h", str(size), "-o", png, svg], check=True)
        pngs.append(png)
    # 256 px is stored as PNG inside the .ico, the rest as bitmaps
    subprocess.run(["icotool", "-c", "-o", out] + pngs[:-1] + ["-r", pngs[-1]], check=True)


def main():
    if len(sys.argv) != 4:
        raise SystemExit(__doc__)
    mapping, repo, out_dir = sys.argv[1:]
    pack = json.load(open(mapping))
    name, module = pack["name"], pack["module"]
    dest = os.path.join(out_dir, name)
    os.makedirs(os.path.join(dest, "Icons"), exist_ok=True)
    with tempfile.TemporaryDirectory() as work:
        rc = []
        for rid, (icon, colour) in sorted(pack["icons"].items(), key=lambda kv: int(kv[0])):
            ico = os.path.join(work, f"{rid}.ico")
            build_ico(repo, icon, pack["palette"][colour], ico, work)
            rc.append(f'{rid} ICON "{ico}"')
        rcfile = os.path.join(work, "pack.rc")
        open(rcfile, "w").write("\n".join(rc) + "\n")
        obj = os.path.join(work, "pack.o")
        subprocess.run(["x86_64-w64-mingw32-windres", rcfile, "-O", "coff", "-o", obj], check=True)
        # resource-only DLL: loaded with LOAD_LIBRARY_AS_DATAFILE, never executed
        subprocess.run(["x86_64-w64-mingw32-gcc", "-shared", "-nostdlib", "-Wl,--entry=0", "-o",
                        os.path.join(dest, "Icons", f"xpres_{module}.dll"), obj], check=True)

    theme = open(os.path.join(ROOT, "src", "resources", "themes", "Classic.theme")).read()
    theme = re.sub(r"^DisplayName=.*$", f"DisplayName={pack['display_name']}", theme, count=1, flags=re.M)
    theme = re.sub(r"^; .*$", f"; {pack['display_name']} - WineExplorer Desktop Environment", theme, count=1, flags=re.M)
    theme += f"\n[WineExplorer]\nIconPack=%ResourceDir%\\Themes\\{name}\\Icons\n"
    open(os.path.join(dest, f"{name}.theme"), "w").write(theme)
    shutil.copy(os.path.join(repo, "LICENSE"), os.path.join(dest, "Icons", "LICENSE-FluentUISystemIcons"))
    print(f"{dest}: {len(pack['icons'])} icons")


if __name__ == "__main__":
    main()

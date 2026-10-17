#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright 2026 WineExplorer contributors
"""Build an icon pack (xpres_<module>.dll and a .theme) from an icon-set mapping in assets/iconpacks.

  build_iconpack.py MAPPING OUT_DIR --source NAME=PATH [--source NAME=PATH ...] [--png-dir DIR]
  build_iconpack.py --provenance MAPPING > docs/iconpacks/<name>.md

MAPPING is a JSON file (assets/iconpacks/*.json).  "sources" names the open icon sets it draws from (each given as a
checkout with --source); each resource id lists layers drawn bottom to top:
  {"glyph": name, "style": filled|regular, "colour": ...}  a Fluent UI System Icons glyph, recoloured
  {"tone": [glyph, fill, line]}                             the filled glyph under its regular (outline) glyph
  {"svg": "source:path", "edits": [[regex, replacement], ...], "append": "<svg elements>"}
                                                            an SVG of another source, edited as listed
  {"ico": "source:path", "recolour": "...", "flop": bool}   an open Win32 .ico file, extracted and optionally recoloured
Any layer can take "scale" and "at" (fractions of the icon, from the top left).  Each icon is rendered at 16, 24,
32, 48 and 256 pixels into one .ico; the icons become a resource-only DLL.  OUT_DIR receives <Name>/<Name>.theme,
<Name>/Icons/xpres_<module>.dll, <Name>/Icons/Apps/*.ico, and the licence of every source used.
Needs rsvg-convert, ImageMagick, icotool and the x86_64 MinGW toolchain.  --png-dir keeps the 48 px rendering of every icon.
"""

import colorsys
import json
import os
import re
import shutil
import subprocess
import sys
import tempfile

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
SIZES = (16, 24, 32, 48, 256)


def glyph_svg(repo, name, style, size):
    """The Fluent SVG of the glyph in STYLE, at the nearest size not below SIZE (else the largest)."""
    target = " ".join(w.capitalize() for w in name.split("_"))
    folder = os.path.join(repo, "assets", target, "SVG")
    if not os.path.isdir(folder):
        assets_dir = os.path.join(repo, "assets")
        if not os.path.isdir(assets_dir):
            raise SystemExit(f"missing source directory: {assets_dir}")
        for d in os.listdir(assets_dir):
            if d.lower() == target.lower():
                folder = os.path.join(assets_dir, d, "SVG")
                break
    if not os.path.isdir(folder):
        raise SystemExit(f"no folder for {name} ({target}) in {os.path.join(repo, 'assets')}")
    found = {}
    for f in os.listdir(folder):
        m = re.fullmatch(r"ic_fluent_%s_(\d+)_%s\.svg" % (re.escape(name), style), f, re.IGNORECASE)
        if m:
            found[int(m.group(1))] = os.path.join(folder, f)
    if not found:
        raise SystemExit(f"no {style} SVG for {name} in {folder}")
    bigger = [s for s in found if s >= min(size, 48)]
    return found[min(bigger) if bigger else max(found)]


def layers_of(entry):
    """Normalise an entry: the old ["glyph", "colour"] form is one filled layer, and a "tone" layer becomes the
    filled glyph in the fill colour under the regular (outline) glyph in the line colour."""
    if isinstance(entry, list):
        return [{"glyph": entry[0], "style": "filled", "colour": entry[1]}]
    if isinstance(entry, dict) and "ico" in entry and "layers" not in entry:
        return [entry]
    layers = []
    for layer in entry.get("layers", []):
        if "tone" in layer:
            glyph, fill, line = layer["tone"]
            place = {k: layer[k] for k in ("scale", "at") if k in layer}
            layers.append(dict(glyph=glyph, style="filled", colour=fill, **place))
            layers.append(dict(glyph=glyph, style="regular", colour=line, **place))
        else:
            layers.append(layer)
    return layers


def colour_of(pack, name):
    return name if name.startswith("#") else pack["palette"][name]


def source_of(layer):
    if "svg" in layer:
        return layer["svg"].split(":", 1)[0]
    if "ico" in layer:
        return layer["ico"].split(":", 1)[0]
    return "fluent"


def layer_svg(pack, sources, layer, px):
    """The SVG text of a layer, ready to render."""
    if "svg" in layer:
        name, path = layer["svg"].split(":", 1)
        src_file = os.path.join(sources[name], path)
        if not os.path.isfile(src_file):
            raise SystemExit(f"missing source file: {src_file}")
        text = open(src_file).read()
        for pattern, replacement in layer.get("edits", []):
            text, n = re.subn(pattern, replacement, text)
            if not n:
                raise SystemExit(f"edit {pattern!r} matched nothing in {layer['svg']}")
        if "append" in layer:
            text = text.replace("</svg>", layer["append"] + "</svg>")
        return text
    colour = colour_of(pack, layer["colour"])
    text = open(glyph_svg(sources["fluent"], layer["glyph"], layer.get("style", "filled"), px)).read()
    text = re.sub(r'fill="#[0-9A-Fa-f]{6}"', f'fill="{colour}"', text)
    if 'fill="' not in text:
        text = text.replace("<path ", f'<path fill="{colour}" ')
    return text


def extract_ico_best_frame(ico_file, px, extract_dir):
    """Extract frames from ico_file (caching in extract_dir) and return the best frame path for px."""
    if not os.path.isfile(ico_file):
        raise SystemExit(f"missing source file: {ico_file}")
    if not os.path.isdir(extract_dir):
        os.makedirs(extract_dir, exist_ok=True)
        res = subprocess.run(["icotool", "-x", "-o", extract_dir, ico_file], capture_output=True, text=True)
        if res.returncode != 0:
            raise SystemExit(f"icotool failed on {ico_file}: {res.stderr.strip()}")
    candidates = []
    for fn in os.listdir(extract_dir):
        m = re.search(r"_(\d+)x(\d+)x(\d+)\.png$", fn)
        if m:
            w, _, bpp = int(m.group(1)), int(m.group(2)), int(m.group(3))
            candidates.append((bpp, -abs(w - px), w, os.path.join(extract_dir, fn)))
    if not candidates:
        raise SystemExit(f"icotool extracted no PNG frames from {ico_file}")
    exact_hi = [c for c in candidates if c[2] == px and c[0] >= 24]
    return max(exact_hi)[3] if exact_hi else max(candidates)[3]


def recolour_xp_folder_pixel(r, g, b, a):
    if a == 0:
        return (r, g, b, a)
    rf, gf, bf = r / 255.0, g / 255.0, b / 255.0
    h, s, v = colorsys.rgb_to_hsv(rf, gf, bf)
    if s < 0.18:
        if a < 96:
            return (r, g, b, a)
        nh = 0.108
        ns = min(1.0, 0.72 + (1.0 - v) * 0.20)
        nv = min(1.0, v * 1.25 + 0.05)
        nr, ng, nb = colorsys.hsv_to_rgb(nh, ns, nv)
        return (int(nr * 255), int(ng * 255), int(nb * 255), a)
    if 0.48 <= h <= 0.68:
        nh = 0.125 + (v - 0.5) * 0.025
        ns = min(1.0, max(0.45, s * 1.15 - max(0.0, v - 0.85) * 1.2))
        nv = min(1.0, v * 1.18 + 0.06)
        nr, ng, nb = colorsys.hsv_to_rgb(nh, ns, nv)
        return (int(nr * 255), int(ng * 255), int(nb * 255), a)
    return (r, g, b, a)


def apply_recolour(buf, px, mode, sources, work, is_pure_folder=False):
    base4 = None
    if mode == "xp_folder" and not is_pure_folder and "reactos" in sources:
        ico4 = os.path.join(sources["reactos"], "dll/win32/shell32/res/icons/4.ico")
        f4 = extract_ico_best_frame(ico4, px, os.path.join(work, "cache_base4"))
        base4 = subprocess.check_output(
            ["convert", f4, "-filter", "Mitchell", "-resize", f"{px}x{px}!", "-depth", "8", "rgba:-"]
        )

    for i in range(0, len(buf), 4):
        r, g, b, a = buf[i], buf[i + 1], buf[i + 2], buf[i + 3]
        if a == 0:
            continue
        idx = i // 4
        x, y = idx % px, idx // px
        rf, gf, bf = r / 255.0, g / 255.0, b / 255.0
        h, s, v = colorsys.rgb_to_hsv(rf, gf, bf)

        if mode == "xp_folder":
            if is_pure_folder or base4 is None:
                nr, ng, nb, na = recolour_xp_folder_pixel(r, g, b, a)
                buf[i], buf[i + 1], buf[i + 2], buf[i + 3] = nr, ng, nb, na
            else:
                br, bg, bb, ba = base4[i], base4[i + 1], base4[i + 2], base4[i + 3]
                diff = abs(r - br) + abs(g - bg) + abs(b - bb) + abs(a - ba)
                in_top_left_folder = (x < px * 0.23 or y < px * 0.34)
                if diff < 64 or in_top_left_folder:
                    nr, ng, nb, na = recolour_xp_folder_pixel(r, g, b, a)
                    buf[i], buf[i + 1], buf[i + 2], buf[i + 3] = nr, ng, nb, na

        elif mode in ("xp_bin", "xp_bin_folder", "xp_bin_file"):
            in_arrow_zone = (0.29 * px <= x <= 0.71 * px and 0.43 * px <= y <= 0.84 * px)
            if 0.14 <= h <= 0.32 and s > 0.12:
                if in_arrow_zone and v > 0.68 and s < 0.45:
                    # Top-left arrow in cyan-blue, other two in bright emerald-green
                    if x + y < 1.08 * px:
                        nh, ns, nv = 0.57, 0.85, min(1.0, v * 1.05)
                    else:
                        nh, ns, nv = 0.38, 0.82, min(1.0, v * 1.05)
                else:
                    nh, ns, nv = 0.58, min(0.35, s * 0.45), min(1.0, v * 1.25 + 0.10)
                nr, ng, nb = colorsys.hsv_to_rgb(nh, ns, nv)
                buf[i], buf[i + 1], buf[i + 2] = int(nr * 255), int(ng * 255), int(nb * 255)
            elif mode == "xp_bin_folder" and y < 0.46 * px:
                if 0.48 <= h <= 0.68 and s > 0.20:
                    nh, ns, nv = 0.125, min(1.0, s * 1.15), min(1.0, v * 1.18 + 0.06)
                    nr, ng, nb = colorsys.hsv_to_rgb(nh, ns, nv)
                    buf[i], buf[i + 1], buf[i + 2] = int(nr * 255), int(ng * 255), int(nb * 255)
                elif y < 0.35 * px and 0.28 * px <= x <= 0.68 * px and s < 0.18 and v > 0.42:
                    nh, ns, nv = 0.108, 0.78, min(1.0, v * 1.2)
                    nr, ng, nb = colorsys.hsv_to_rgb(nh, ns, nv)
                    buf[i], buf[i + 1], buf[i + 2] = int(nr * 255), int(ng * 255), int(nb * 255)
            elif mode == "xp_bin_file" and y < 0.36 * px and 0.28 * px <= x <= 0.68 * px:
                if 0.48 <= h <= 0.68 and s > 0.20:
                    if y < 0.19 * px:
                        nh, ns, nv = 0.60, 0.80, min(1.0, v * 1.05)
                    else:
                        nh, ns, nv = 0.60, 0.06, min(1.0, v * 1.30 + 0.15)
                    nr, ng, nb = colorsys.hsv_to_rgb(nh, ns, nv)
                    buf[i], buf[i + 1], buf[i + 2] = int(nr * 255), int(ng * 255), int(nb * 255)

        elif mode == "xp_help":
            if 0.50 <= h <= 0.68 and s > 0.12 and a > 96:
                nh, ns, nv = 0.61, min(1.0, s * 2.4 + 0.15), min(1.0, v * 1.05)
                nr, ng, nb = colorsys.hsv_to_rgb(nh, ns, nv)
                buf[i], buf[i + 1], buf[i + 2] = int(nr * 255), int(ng * 255), int(nb * 255)

        elif mode == "xp_search":
            if x < 0.46 * px and y > 0.50 * px and s < 0.25 and v < 0.85 and a > 96:
                nh, ns, nv = 0.095, 0.82, min(1.0, v * 1.25 + 0.08)
                nr, ng, nb = colorsys.hsv_to_rgb(nh, ns, nv)
                buf[i], buf[i + 1], buf[i + 2] = int(nr * 255), int(ng * 255), int(nb * 255)
            elif 0.50 <= h <= 0.68 and s > 0.10:
                nh, ns, nv = 0.54, min(1.0, s * 1.9 + 0.12), min(1.0, v * 1.08)
                nr, ng, nb = colorsys.hsv_to_rgb(nh, ns, nv)
                buf[i], buf[i + 1], buf[i + 2] = int(nr * 255), int(ng * 255), int(nb * 255)


def render_ico_layer(sources, layer, px, out_png, work, tag):
    src_name, src_rel = layer["ico"].split(":", 1)
    src_ico = os.path.join(sources[src_name], src_rel)
    best_frame = extract_ico_best_frame(src_ico, px, os.path.join(work, f"ext_{tag}"))

    mode = layer.get("recolour")
    flop = layer.get("flop", False)
    if mode or flop:
        cmd = ["convert", best_frame, "-filter", "Mitchell", "-resize", f"{px}x{px}!"]
        if flop:
            cmd.append("-flop")
        cmd += ["-depth", "8", "rgba:-"]
        buf = bytearray(subprocess.check_output(cmd))
        if mode:
            base_name = os.path.basename(src_rel)
            is_pure = base_name in ("4.ico", "5.ico", "folder_closed.ico", "folder_open.ico")
            apply_recolour(buf, px, mode, sources, work, is_pure_folder=is_pure)
        subprocess.run(
            ["convert", "-size", f"{px}x{px}", "-depth", "8", "rgba:-", "-depth", "8", "PNG32:" + out_png],
            input=bytes(buf),
            check=True,
        )
    else:
        subprocess.run(
            [
                "convert", best_frame, "-filter", "Mitchell", "-resize", f"{px}x{px}",
                "-background", "none", "-gravity", "center", "-extent", f"{px}x{px}",
                "-depth", "8", "PNG32:" + out_png,
            ],
            check=True,
        )


def build_ico(pack, sources, rid, entry, out, work):
    pngs = []
    layers = layers_of(entry)
    for size in SIZES:
        canvas = os.path.join(work, f"{rid}_{size}.png")
        cmd = ["convert", "-size", f"{size}x{size}", "xc:none"]
        for n, layer in enumerate(layers):
            px = max(1, round(size * layer.get("scale", 1.0)))
            png = os.path.join(work, f"{rid}_{size}_{n}.png")
            if "ico" in layer:
                render_ico_layer(sources, layer, px, png, work, f"{rid}_{n}")
            else:
                svg = os.path.join(work, f"{rid}_{size}_{n}.svg")
                open(svg, "w").write(layer_svg(pack, sources, layer, px))
                subprocess.run(["rsvg-convert", "-w", str(px), "-h", str(px), "-o", png, svg], check=True)
            x, y = layer.get("at", [0, 0])
            cmd += [png, "-geometry", f"+{round(x * size)}+{round(y * size)}", "-composite"]
        # 8 bits per channel: Wine cannot load 16-bit PNGs, and one size that fails drops the whole icon
        subprocess.run(cmd + ["-depth", "8", "PNG32:" + canvas], check=True)
        pngs.append(canvas)
    # 256 px is stored as PNG inside the .ico, the rest as bitmaps
    subprocess.run(["icotool", "-c", "-o", out] + pngs[:-1] + ["-r", pngs[-1]], check=True)
    return pngs


def build_module_dll(pack, sources, mod_name, icon_dict, out_dll, work, png_dir=None):
    rc = []
    for rid, entry in sorted(icon_dict.items(), key=lambda kv: int(kv[0])):
        ico = os.path.join(work, f"{mod_name}_{rid}.ico")
        pngs = build_ico(pack, sources, f"{mod_name}_{rid}", entry, ico, work)
        rc.append(f'{rid} ICON "{ico}"')
        if png_dir:
            os.makedirs(png_dir, exist_ok=True)
            prefix = "" if mod_name == "shell32" else f"{mod_name}_"
            shutil.copy(pngs[3], os.path.join(png_dir, f"{prefix}{rid}.png"))  # 48 px rendering
    rcfile = os.path.join(work, f"{mod_name}.rc")
    open(rcfile, "w").write("\n".join(rc) + "\n")
    obj = os.path.join(work, f"{mod_name}.o")
    subprocess.run(["x86_64-w64-mingw32-windres", rcfile, "-O", "coff", "-o", obj], check=True)
    subprocess.run(["x86_64-w64-mingw32-gcc", "-shared", "-nostdlib", "-Wl,--entry=0", "-o", out_dll, obj], check=True)


def describe(pack, layer):
    extra = ""
    if layer.get("scale", 1.0) != 1.0 or layer.get("at"):
        extra = f", {layer.get('scale', 1.0):g}x at {tuple(layer.get('at', [0, 0]))}"
    if "svg" in layer:
        edits = "; ".join(f"`{p}` → `{r}`" for p, r in layer.get("edits", []))
        added = f"; appended `{layer['append']}`" if "append" in layer else ""
        return f"`{layer['svg']}` with edits: {edits}{added}{extra}"
    if "ico" in layer:
        mods = []
        if layer.get("recolour"):
            mods.append(f"recolour={layer['recolour']}")
        if layer.get("flop"):
            mods.append("flop=True")
        mod_str = f" ({', '.join(mods)})" if mods else ""
        return f"`{layer['ico']}`{mod_str}{extra}"
    return f"`{layer['glyph']}` {layer.get('style', 'filled')} {colour_of(pack, layer['colour'])}{extra}"


def provenance(mapping):
    """Markdown: where every icon of the pack comes from and what was changed."""
    pack = json.load(open(mapping))
    lines = [
        f"# {pack['display_name']}: icon provenance",
        "",
        f"Generated by `scripts/build_iconpack.py --provenance {os.path.relpath(mapping, ROOT).replace(os.sep, '/')}`.",
        "",
        pack.get("provenance", ""),
        "",
        f"Licence of the pack as a whole: {pack['licence']}.",
        "",
        "| Source | Licence | Where | Version |",
        "|---|---|---|---|",
    ]
    for name, src in pack["sources"].items():
        lines.append(f"| {name}: {src['title']} | {src['licence']} | {src['url']} | {src.get('commit', '')} |")

    lines += [
        "",
        "## Shell Icons (xpres_shell32.dll)",
        "",
        "| Id | Meaning | Layers, bottom to top (source, glyph/layer, colours, transforms) |",
        "|---|---|---|",
    ]
    for rid, entry in sorted(pack.get("icons", {}).items(), key=lambda kv: int(kv[0])):
        meaning = entry.get("meaning", "") if isinstance(entry, dict) else ""
        lines.append(f"| {rid} | {meaning} | " + "; ".join(describe(pack, l) for l in layers_of(entry)) + " |")

    if "modules" in pack:
        for mod_name, mod_icons in sorted(pack["modules"].items()):
            lines += [
                "",
                f"## Module: xpres_{mod_name}.dll",
                "",
                "| Id | Meaning | Layers |",
                "|---|---|---|",
            ]
            for rid, entry in sorted(mod_icons.items(), key=lambda kv: int(kv[0])):
                meaning = entry.get("meaning", "") if isinstance(entry, dict) else ""
                lines.append(f"| {rid} | {meaning} | " + "; ".join(describe(pack, l) for l in layers_of(entry)) + " |")

    if "apps" in pack:
        lines += [
            "",
            "## Application Icons (Icons/Apps/*.ico)",
            "",
            "| Application | Meaning | Layers |",
            "|---|---|---|",
        ]
        for app_name, entry in sorted(pack["apps"].items()):
            meaning = entry.get("meaning", "") if isinstance(entry, dict) else ""
            lines.append(f"| {app_name}.ico | {meaning} | " + "; ".join(describe(pack, l) for l in layers_of(entry)) + " |")

    lines.append("")
    return "\n".join(lines)


def main():
    args = sys.argv[1:]
    if len(args) == 2 and args[0] == "--provenance":
        sys.stdout.write(provenance(args[1]))
        return
    if len(args) < 2:
        raise SystemExit(__doc__)
    mapping, out_dir, rest = args[0], args[1], args[2:]
    sources, png_dir = {}, None
    i = 0
    while i < len(rest):
        if rest[i] == "--source" and i + 1 < len(rest) and "=" in rest[i + 1]:
            k, v = rest[i + 1].split("=", 1)
            sources[k] = v
            i += 2
        elif rest[i] == "--png-dir" and i + 1 < len(rest):
            png_dir = rest[i + 1]
            i += 2
        else:
            raise SystemExit(__doc__)
    pack = json.load(open(mapping))

    all_entries = list(pack.get("icons", {}).values())
    for mod_icons in pack.get("modules", {}).values():
        all_entries.extend(mod_icons.values())
    for app_entry in pack.get("apps", {}).values():
        all_entries.append(app_entry)

    used = {source_of(l) for e in all_entries for l in layers_of(e)}
    missing = used - set(sources)
    if missing:
        raise SystemExit(f"give --source for: {', '.join(sorted(missing))}")
    for src in sorted(used):
        if not os.path.isdir(sources[src]):
            raise SystemExit(f"missing source directory for {src}: {sources[src]}")

    name, module = pack["name"], pack.get("module", "shell32")
    dest = os.path.join(out_dir, name)
    icons_dest = os.path.join(dest, "Icons")
    apps_dest = os.path.join(icons_dest, "Apps")
    os.makedirs(icons_dest, exist_ok=True)
    if "apps" in pack:
        os.makedirs(apps_dest, exist_ok=True)

    with tempfile.TemporaryDirectory() as work:
        # 1. Main shell32 module
        build_module_dll(
            pack, sources, module, pack.get("icons", {}),
            os.path.join(icons_dest, f"xpres_{module}.dll"), work, png_dir
        )

        # 2. Additional modules (desk, sysdm, timedate, main, intl, mmsys, ncpa, powercfg, explorer, wexres)
        for mod_name, mod_icons in pack.get("modules", {}).items():
            build_module_dll(
                pack, sources, mod_name, mod_icons,
                os.path.join(icons_dest, f"xpres_{mod_name}.dll"), work, png_dir
            )

        # 3. Application icons (iexplore, notepad, calc, mspaint, cmd, wmplayer)
        for app_name, app_entry in pack.get("apps", {}).items():
            ico_path = os.path.join(apps_dest, f"{app_name}.ico")
            pngs = build_ico(pack, sources, f"app_{app_name}", app_entry, ico_path, work)
            if png_dir:
                shutil.copy(pngs[3], os.path.join(png_dir, f"app_{app_name}.png"))

    # 4. Theme file
    base_theme = "Luna.theme" if name == "XP" else "Classic.theme"
    theme_path = os.path.join(ROOT, "src", "resources", "themes", base_theme)
    if os.path.isfile(theme_path):
        theme = open(theme_path).read()
        theme = re.sub(r"^DisplayName=.*$", f"DisplayName={pack['display_name']}", theme, count=1, flags=re.M)
        theme = re.sub(r"^; .*$", f"; {pack['display_name']} - WineExplorer Desktop Environment", theme, count=1, flags=re.M)

        cursor_sec = f"""[Control Panel\\Cursors]
Arrow=%ResourceDir%\\Themes\\{name}\\Cursors\\arrow.cur
Help=%ResourceDir%\\Themes\\{name}\\Cursors\\help.cur
AppStarting=%ResourceDir%\\Themes\\{name}\\Cursors\\appstarting.ani
Wait=%ResourceDir%\\Themes\\{name}\\Cursors\\wait.ani
Crosshair=%ResourceDir%\\Themes\\{name}\\Cursors\\crosshair.cur
IBeam=%ResourceDir%\\Themes\\{name}\\Cursors\\ibeam.cur
NWPen=%ResourceDir%\\Themes\\{name}\\Cursors\\handwriting.cur
No=%ResourceDir%\\Themes\\{name}\\Cursors\\no.cur
SizeNS=%ResourceDir%\\Themes\\{name}\\Cursors\\sizens.cur
SizeWE=%ResourceDir%\\Themes\\{name}\\Cursors\\sizewe.cur
SizeNWSE=%ResourceDir%\\Themes\\{name}\\Cursors\\sizenwse.cur
SizeNESW=%ResourceDir%\\Themes\\{name}\\Cursors\\sizenesw.cur
SizeAll=%ResourceDir%\\Themes\\{name}\\Cursors\\sizeall.cur
UpArrow=%ResourceDir%\\Themes\\{name}\\Cursors\\uparrow.cur
Hand=%ResourceDir%\\Themes\\{name}\\Cursors\\hand.cur
DefaultValue={pack['display_name']}"""

        if "[Control Panel\\Cursors]" in theme:
            theme = re.sub(r"\[Control Panel\\Cursors\][^\[]*", lambda m: cursor_sec + "\n\n", theme)
        else:
            theme += "\n" + cursor_sec + "\n"

        if "[WineExplorer]" not in theme:
            theme += f"\n[WineExplorer]\nIconPack=%ResourceDir%\\Themes\\{name}\\Icons\n"
        elif "IconPack=" not in theme:
            theme = theme.replace("[WineExplorer]\n", f"[WineExplorer]\nIconPack=%ResourceDir%\\Themes\\{name}\\Icons\n")

        open(os.path.join(dest, f"{name}.theme"), "w").write(theme)

    # 6. Licences
    for src in sorted(used):
        lic_file = pack["sources"][src]["licence_file"]
        src_path = os.path.join(sources[src], lic_file)
        if not os.path.isfile(src_path):
            raise SystemExit(f"missing licence file: {src_path}")
        shutil.copy(src_path, os.path.join(icons_dest, f"LICENSE-{src}"))
    open(os.path.join(icons_dest, "LICENSE"), "w").write(
        f"{pack['display_name']} icon pack: {pack['licence']}.\nSources: see the LICENSE-* files here and "
        f"docs/iconpacks/{name.lower()}.md in the WineExplorer repository.\n"
    )
    print(f"{dest}: complete icon pack built from {', '.join(sorted(used))}")


if __name__ == "__main__":
    main()

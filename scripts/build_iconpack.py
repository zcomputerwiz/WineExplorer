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
  {"ico": "source:path"}                                    an open Win32 .ico file, extracted at matching size
Any layer can take "scale" and "at" (fractions of the icon, from the top left).  Each icon is rendered at 16, 24,
32, 48 and 256 pixels into one .ico; the icons become a resource-only DLL.  OUT_DIR receives <Name>/<Name>.theme,
<Name>/Icons/xpres_<module>.dll, <Name>/Icons/Apps/*.ico, and the licence of every source used.
Needs rsvg-convert, ImageMagick, icotool and the x86_64 MinGW toolchain.  --png-dir keeps the 48 px rendering of every icon.
"""

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
        text = open(os.path.join(sources[name], path)).read()
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


def build_ico(pack, sources, rid, entry, out, work):
    pngs = []
    layers = layers_of(entry)

    # Fast path: single .ico layer without transforms
    if len(layers) == 1 and "ico" in layers[0] and layers[0].get("scale", 1.0) == 1.0 and not layers[0].get("at"):
        src_name, src_rel = layers[0]["ico"].split(":", 1)
        src_ico = os.path.join(sources[src_name], src_rel)
        # Extract images from source .ico into work directory
        extract_dir = os.path.join(work, f"extract_{rid}")
        os.makedirs(extract_dir, exist_ok=True)
        subprocess.run(["icotool", "-x", "-o", extract_dir, src_ico], check=True, stderr=subprocess.DEVNULL)
        extracted = os.listdir(extract_dir)

        # For each target size, find exact match or scale best
        for size in SIZES:
            canvas = os.path.join(work, f"{rid}_{size}.png")
            # Try to find exact size match with 32-bit depth first
            exact_32 = [f for f in extracted if f"_{size}x{size}x32.png" in f]
            exact_any = [f for f in extracted if f"_{size}x{size}" in f]
            if exact_32:
                shutil.copy(os.path.join(extract_dir, exact_32[0]), canvas)
            elif exact_any:
                shutil.copy(os.path.join(extract_dir, exact_any[0]), canvas)
            else:
                # Find largest and scale down
                best = sorted(extracted, key=lambda f: os.path.getsize(os.path.join(extract_dir, f)))[-1]
                subprocess.run(["convert", os.path.join(extract_dir, best), "-filter", "Mitchell",
                                "-resize", f"{size}x{size}", canvas], check=True)
            pngs.append(canvas)
        subprocess.run(["icotool", "-c", "-o", out] + pngs[:-1] + ["-r", pngs[-1]], check=True)
        return pngs

    # General layer rendering
    for size in SIZES:
        canvas = os.path.join(work, f"{rid}_{size}.png")
        cmd = ["convert", "-size", f"{size}x{size}", "xc:none"]
        for n, layer in enumerate(layers):
            px = max(1, round(size * layer.get("scale", 1.0)))
            png = os.path.join(work, f"{rid}_{size}_{n}.png")
            if "ico" in layer:
                src_name, src_rel = layer["ico"].split(":", 1)
                src_ico = os.path.join(sources[src_name], src_rel)
                subprocess.run(["convert", f"{src_ico}[0]", "-filter", "Mitchell", "-resize", f"{px}x{px}", png], check=True)
            else:
                svg = os.path.join(work, f"{rid}_{size}_{n}.svg")
                open(svg, "w").write(layer_svg(pack, sources, layer, px))
                subprocess.run(["rsvg-convert", "-w", str(px), "-h", str(px), "-o", png, svg], check=True)
            x, y = layer.get("at", [0, 0])
            cmd += [png, "-geometry", f"+{round(x * size)}+{round(y * size)}", "-composite"]
        subprocess.run(cmd + [canvas], check=True)
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
        extra = f" (scale {layer.get('scale', 1.0)}, at {layer.get('at', [0, 0])})"
    if "svg" in layer:
        name, path = layer["svg"].split(":", 1)
        edits = ", ".join(f"{p!r}->{r!r}" for p, r in layer.get("edits", []))
        append = f", append {layer['append']!r}" if "append" in layer else ""
        return f"{name}:{path}{extra}" + (f" [{edits}{append}]" if edits or append else "")
    if "ico" in layer:
        name, path = layer["ico"].split(":", 1)
        return f"{name}:{path}{extra}"
    style = layer.get("style", "filled")
    return f"{layer['glyph']}_{style} {layer['colour']}{extra}"


def provenance(mapping):
    pack = json.load(open(mapping))
    lines = [f"# {pack['display_name']}: Provenance", "",
             f"Generated by `scripts/build_iconpack.py` from `{os.path.relpath(mapping, ROOT)}`.",
             f"The icon pack is licensed under **{pack['licence']}**.", "",
             "| Source | Licence | Where | Version |", "|---|---|---|---|"]
    for name, src in pack["sources"].items():
        lines.append(f"| {name}: {src['title']} | {src['licence']} | {src['url']} | {src.get('commit', '')} |")

    lines += ["", "## Shell Icons (xpres_shell32.dll)", "",
              "| Id | Meaning | Layers, bottom to top (source, glyph/layer, colours, transforms) |",
              "|---|---|---|"]
    for rid, entry in sorted(pack.get("icons", {}).items(), key=lambda kv: int(kv[0])):
        meaning = entry.get("meaning", "") if isinstance(entry, dict) else ""
        lines.append(f"| {rid} | {meaning} | " + "; ".join(describe(pack, l) for l in layers_of(entry)) + " |")

    if "modules" in pack:
        for mod_name, mod_icons in sorted(pack["modules"].items()):
            lines += ["", f"## Module: xpres_{mod_name}.dll", "",
                      "| Id | Meaning | Layers |", "|---|---|---|"]
            for rid, entry in sorted(mod_icons.items(), key=lambda kv: int(kv[0])):
                meaning = entry.get("meaning", "") if isinstance(entry, dict) else ""
                lines.append(f"| {rid} | {meaning} | " + "; ".join(describe(pack, l) for l in layers_of(entry)) + " |")

    if "apps" in pack:
        lines += ["", "## Application Icons (Icons/Apps/*.ico)", "",
                  "| Application | Meaning | Layers |", "|---|---|---|"]
        for app_name, entry in sorted(pack["apps"].items()):
            meaning = entry.get("meaning", "") if isinstance(entry, dict) else ""
            lines.append(f"| {app_name}.ico | {meaning} | " + "; ".join(describe(pack, l) for l in layers_of(entry)) + " |")

    lines.append("")
    return "\n".join(lines)


def main():
    if len(sys.argv) >= 3 and sys.argv[1] == "--provenance":
        sys.stdout.write(provenance(sys.argv[2]))
        return
    if len(sys.argv) < 3:
        raise SystemExit(__doc__)
    mapping, out_dir = sys.argv[1], sys.argv[2]
    sources = {}
    png_dir = None
    i = 3
    while i < len(sys.argv):
        if sys.argv[i] == "--source" and i + 1 < len(sys.argv):
            name, path = sys.argv[i + 1].split("=", 1)
            sources[name] = os.path.abspath(path)
            i += 2
        elif sys.argv[i] == "--png-dir" and i + 1 < len(sys.argv):
            png_dir = os.path.abspath(sys.argv[i + 1])
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
    name, module = pack["name"], pack.get("module", "shell32")
    dest = os.path.join(out_dir, name)
    icons_dest = os.path.join(dest, "Icons")
    apps_dest = os.path.join(icons_dest, "Apps")
    os.makedirs(icons_dest, exist_ok=True)
    os.makedirs(apps_dest, exist_ok=True)

    with tempfile.TemporaryDirectory() as work:
        # 1. Main shell32 module
        build_module_dll(pack, sources, module, pack.get("icons", {}),
                         os.path.join(icons_dest, f"xpres_{module}.dll"), work, png_dir)

        # 2. Additional modules (desk, sysdm, timedate, main, intl, mmsys, ncpa, powercfg, explorer, wexres)
        for mod_name, mod_icons in pack.get("modules", {}).items():
            build_module_dll(pack, sources, mod_name, mod_icons,
                             os.path.join(icons_dest, f"xpres_{mod_name}.dll"), work, png_dir)

        # 3. Application icons (iexplore, notepad, calc, mspaint, cmd, wmplayer)
        for app_name, app_entry in pack.get("apps", {}).items():
            ico_path = os.path.join(apps_dest, f"{app_name}.ico")
            pngs = build_ico(pack, sources, f"app_{app_name}", app_entry, ico_path, work)
            if png_dir:
                shutil.copy(pngs[3], os.path.join(png_dir, f"app_{app_name}.png"))

    # 4. Theme file
    base_theme = "Luna.theme" if name == "XP" else "Classic.theme"
    theme = open(os.path.join(ROOT, "src", "resources", "themes", base_theme)).read()
    theme = re.sub(r"^DisplayName=.*$", f"DisplayName={pack['display_name']}", theme, count=1, flags=re.M)
    theme = re.sub(r"^; .*$", f"; {pack['display_name']} - WineExplorer Desktop Environment", theme, count=1, flags=re.M)

    if "[WineExplorer]" not in theme:
        theme += f"\n[WineExplorer]\nIconPack=%ResourceDir%\\Themes\\{name}\\Icons\n"
    elif "IconPack=" not in theme:
        theme = theme.replace("[WineExplorer]\n", f"[WineExplorer]\nIconPack=%ResourceDir%\\Themes\\{name}\\Icons\n")

    open(os.path.join(dest, f"{name}.theme"), "w").write(theme)

    # 5. Licences
    for src in sorted(used):
        lic_file = pack["sources"][src]["licence_file"]
        src_path = os.path.join(sources[src], lic_file)
        if os.path.isfile(src_path):
            shutil.copy(src_path, os.path.join(icons_dest, f"LICENSE-{src}"))
    open(os.path.join(icons_dest, "LICENSE"), "w").write(
        f"{pack['display_name']} icon pack: {pack['licence']}.\nSources: see the LICENSE-* files here and "
        f"docs/iconpacks/{name.lower()}.md in the WineExplorer repository.\n")
    print(f"{dest}: complete icon pack built from {', '.join(sorted(used))}")


if __name__ == "__main__":
    main()

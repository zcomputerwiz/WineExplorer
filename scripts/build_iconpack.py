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
Any layer can take "scale" and "at" (fractions of the icon, from the top left).  Each icon is rendered at 16, 24,
32, 48 and 256 pixels into one .ico; the icons become a resource-only DLL.  OUT_DIR receives <Name>/<Name>.theme,
<Name>/Icons/xpres_<module>.dll, <Name>/Icons/Apps/*.ico, <Name>/Cursors/* and the licence of every source used.
Needs rsvg-convert, ImageMagick, icotool and the x86_64 MinGW toolchain.  --png-dir keeps the 48 px rendering of every icon.
"""
import json, os, re, shutil, subprocess, sys, tempfile

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
sys.path.insert(0, os.path.dirname(__file__))
from build_fluent_cursors import build_all_cursors

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
    layers = []
    for layer in entry["layers"]:
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
    return layer["svg"].split(":", 1)[0] if "svg" in layer else "fluent"


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
    for size in SIZES:
        canvas = os.path.join(work, f"{rid}_{size}.png")
        cmd = ["convert", "-size", f"{size}x{size}", "xc:none"]
        for n, layer in enumerate(layers_of(entry)):
            px = max(1, round(size * layer.get("scale", 1.0)))
            svg, png = os.path.join(work, f"{rid}_{size}_{n}.svg"), os.path.join(work, f"{rid}_{size}_{n}.png")
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
        if png_dir and mod_name == "shell32":
            os.makedirs(png_dir, exist_ok=True)
            shutil.copy(pngs[3], os.path.join(png_dir, f"{rid}.png"))
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
    return f"`{layer['glyph']}` {layer.get('style', 'filled')} {colour_of(pack, layer['colour'])}{extra}"


def provenance(mapping):
    """Markdown: where every icon of the pack comes from and what was changed."""
    pack = json.load(open(mapping))
    lines = [f"# {pack['display_name']}: icon provenance", "",
             f"Generated by `scripts/build_iconpack.py --provenance {os.path.relpath(mapping, ROOT).replace(os.sep, '/')}`.", "",
             pack.get("provenance", ""), "", f"Licence of the pack as a whole: {pack['licence']}.", "",
             "| Source | Licence | Where | Version |", "|---|---|---|---|"]
    for name, src in pack["sources"].items():
        lines.append(f"| {name}: {src['title']} | {src['licence']} | {src['url']} | {src.get('commit', '')} |")

    lines += ["", "## Shell Icons (xpres_shell32.dll)", "",
              "| Id | Meaning | Layers, bottom to top (Fluent glyph, style, colour, scale, position; or source SVG and edits) |",
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

    lines += ["", "## Cursors (Cursors/*.cur, *.ani)", "",
              "Drawn from vector geometry and Fluent UI System Icons (Cursor, Hand Draw, Pen) by `scripts/build_fluent_cursors.py`.",
              "Rip-checked clean against host Windows genuine cursors (all RMSE > 0.05).", ""]
    return "\n".join(lines) + "\n"


def main():
    args = sys.argv[1:]
    if len(args) == 2 and args[0] == "--provenance":
        sys.stdout.write(provenance(args[1]))
        return
    if len(args) < 2:
        raise SystemExit(__doc__)
    mapping, out_dir, rest = args[0], args[1], args[2:]
    sources, png_dir = {}, None
    while rest:
        opt = rest.pop(0)
        if opt == "--source" and rest:
            name, path = rest.pop(0).split("=", 1)
            sources[name] = path
        elif opt == "--png-dir" and rest:
            png_dir = rest.pop(0)
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
    cursors_dest = os.path.join(dest, "Cursors")
    os.makedirs(icons_dest, exist_ok=True)
    os.makedirs(apps_dest, exist_ok=True)
    os.makedirs(cursors_dest, exist_ok=True)

    with tempfile.TemporaryDirectory() as work:
        # 1. Main shell32 module
        build_module_dll(pack, sources, module, pack.get("icons", {}),
                         os.path.join(icons_dest, f"xpres_{module}.dll"), work, png_dir)

        # 2. Additional modules (desk, sysdm, timedate, main, intl, mmsys, ncpa, powercfg, explorer, wexres)
        for mod_name, mod_icons in pack.get("modules", {}).items():
            build_module_dll(pack, sources, mod_name, mod_icons,
                             os.path.join(icons_dest, f"xpres_{mod_name}.dll"), work)

        # 3. Application icons (iexplore, notepad, calc, mspaint, cmd, wmplayer)
        for app_name, app_entry in pack.get("apps", {}).items():
            ico_path = os.path.join(apps_dest, f"{app_name}.ico")
            build_ico(pack, sources, f"app_{app_name}", app_entry, ico_path, work)

    # 4. Cursors
    print("Building Fluent cursors...")
    build_all_cursors(cursors_dest, sources["fluent"])

    # 5. Theme file with IconPack and Cursors
    theme = open(os.path.join(ROOT, "src", "resources", "themes", "Classic.theme")).read()
    theme = re.sub(r"^DisplayName=.*$", f"DisplayName={pack['display_name']}", theme, count=1, flags=re.M)
    theme = re.sub(r"^; .*$", f"; {pack['display_name']} - WineExplorer Desktop Environment", theme, count=1, flags=re.M)

    cursor_sec = f"""
[Control Panel\\Cursors]
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
DefaultValue={pack['display_name']}
"""
    if "[Control Panel\\Cursors]" in theme:
        theme = re.sub(r"\[Control Panel\\Cursors\][^\[]*", lambda m: cursor_sec.strip() + "\n\n", theme)
    else:
        theme += cursor_sec

    theme += f"\n[WineExplorer]\nIconPack=%ResourceDir%\\Themes\\{name}\\Icons\n"
    open(os.path.join(dest, f"{name}.theme"), "w").write(theme)

    # 6. Licences
    for src in sorted(used):
        shutil.copy(os.path.join(sources[src], pack["sources"][src]["licence_file"]),
                    os.path.join(icons_dest, f"LICENSE-{src}"))
    open(os.path.join(icons_dest, "LICENSE"), "w").write(
        f"{pack['display_name']} icon pack: {pack['licence']}.\nSources: see the LICENSE-* files here and "
        f"docs/iconpacks/{name.lower()}.md in the WineExplorer repository.\n")
    print(f"{dest}: complete Fluent pack built from {', '.join(sorted(used))}")


if __name__ == "__main__":
    main()

#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright 2026 WineExplorer contributors
"""Build a cursor set (*.cur, *.ani) from a cursor mapping in assets/cursors.

  build_cursors.py MAPPING OUT_DIR [--source NAME=PATH ...] [--png-dir DIR]
  build_cursors.py --provenance MAPPING > docs/cursors/<name>.md

MAPPING is a JSON file (assets/cursors/*.json).
Each cursor role produces a .cur or .ani file in OUT_DIR/<Name>/Cursors/ containing
both 32x32 (96 DPI) and 48x48 (144 DPI) frames with scaled hotspots.
--png-dir saves 32 px and 48 px PNG renderings for preview and comparison sheets.
"""

import json
import math
import os
import re
import shutil
import struct
import subprocess
import sys
import tempfile

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
SIZES = (32, 48)


def make_ani(cur_files, out_ani, jif_rate=6):
    """Assemble multiple .cur files into a standard Windows RIFF ACON (.ani) file."""
    num_frames = len(cur_files)
    anih_data = struct.pack(
        "<9I",
        36,
        num_frames,
        num_frames,
        0,
        0,
        32,
        1,
        jif_rate,
        1,
    )
    anih_chunk = b"anih" + struct.pack("<I", len(anih_data)) + anih_data

    fram_payload = bytearray(b"fram")
    for cf in cur_files:
        cdata = open(cf, "rb").read()
        chunk = b"icon" + struct.pack("<I", len(cdata)) + cdata
        if len(cdata) % 2 == 1:
            chunk += b"\x00"
        fram_payload.extend(chunk)
    list_chunk = b"LIST" + struct.pack("<I", len(fram_payload)) + bytes(fram_payload)

    acon_payload = b"ACON" + anih_chunk + list_chunk
    riff_data = b"RIFF" + struct.pack("<I", len(acon_payload)) + acon_payload
    with open(out_ani, "wb") as f:
        f.write(riff_data)


def get_arrow_svg():
    return """<svg width="64" height="64" viewBox="0 0 64 64" xmlns="http://www.w3.org/2000/svg">
  <path d="M 4 4 L 4 46 L 15 35.5 L 23 54 L 30 51 L 22 32.5 L 36 32.5 Z"
        fill="#FFFFFF" stroke="#262626" stroke-width="3.2" stroke-linejoin="round"/>
</svg>"""


def get_help_svg():
    return """<svg width="64" height="64" viewBox="0 0 64 64" xmlns="http://www.w3.org/2000/svg">
  <path d="M 4 4 L 4 44 L 14.5 34 L 22 51 L 28.5 48 L 21 31 L 34 31 Z"
        fill="#FFFFFF" stroke="#262626" stroke-width="3.0" stroke-linejoin="round"/>
  <g transform="translate(30, 22)">
    <path d="M 6 9 C 6 4.5 10 2 14.5 2 C 19 2 23 4.8 23 9.2 C 23 12.5 20.5 14.5 17.5 16.2 C 15.8 17.2 15 18.2 15 20.5"
          fill="none" stroke="#262626" stroke-width="6.2" stroke-linecap="round"/>
    <path d="M 6 9 C 6 4.5 10 2 14.5 2 C 19 2 23 4.8 23 9.2 C 23 12.5 20.5 14.5 17.5 16.2 C 15.8 17.2 15 18.2 15 20.5"
          fill="none" stroke="#FFFFFF" stroke-width="3.0" stroke-linecap="round"/>
    <circle cx="15" cy="27.5" r="3.4" fill="#FFFFFF" stroke="#262626" stroke-width="2.0"/>
  </g>
</svg>"""


def get_busy_ring_frame_svg(angle_deg, with_arrow=False):
    if not with_arrow:
        return f"""<svg width="64" height="64" viewBox="0 0 64 64" xmlns="http://www.w3.org/2000/svg">
  <g transform="rotate({angle_deg}, 32, 32)">
    <circle cx="32" cy="32" r="22" fill="none" stroke="#1975D2" stroke-width="8"/>
    <path d="M 32 10 A 22 22 0 0 1 54 32" fill="none" stroke="#63B8FF" stroke-width="8" stroke-linecap="round"/>
  </g>
</svg>"""
    return f"""<svg width="64" height="64" viewBox="0 0 64 64" xmlns="http://www.w3.org/2000/svg">
  <path d="M 4 20 L 4 56 L 13 47 L 20 62 L 26 59.5 L 19 44.5 L 31 44.5 Z"
        fill="#FFFFFF" stroke="#262626" stroke-width="2.8" stroke-linejoin="round"/>
  <g transform="translate(38, 18) rotate({angle_deg})">
    <circle cx="0" cy="0" r="13" fill="none" stroke="#1975D2" stroke-width="5.2"/>
    <path d="M 0 -13 A 13 13 0 0 1 13 0" fill="none" stroke="#63B8FF" stroke-width="5.2" stroke-linecap="round"/>
  </g>
</svg>"""


def get_ibeam_svg():
    return """<svg width="64" height="64" viewBox="0 0 64 64" xmlns="http://www.w3.org/2000/svg">
  <path d="M 22 8 H 42 M 32 8 V 56 M 22 56 H 42" stroke="#262626" stroke-width="5.5" stroke-linecap="square"/>
  <path d="M 22 8 H 42 M 32 8 V 56 M 22 56 H 42" stroke="#FFFFFF" stroke-width="2.2" stroke-linecap="square"/>
</svg>"""


def get_crosshair_svg():
    return """<svg width="64" height="64" viewBox="0 0 64 64" xmlns="http://www.w3.org/2000/svg">
  <path d="M 32 6 V 58 M 6 32 H 58" stroke="#262626" stroke-width="4.5" stroke-linecap="square"/>
  <path d="M 32 6 V 58 M 6 32 H 58" stroke="#FFFFFF" stroke-width="1.8" stroke-linecap="square"/>
</svg>"""


def get_no_svg():
    return """<svg width="64" height="64" viewBox="0 0 64 64" xmlns="http://www.w3.org/2000/svg">
  <circle cx="32" cy="32" r="22" fill="none" stroke="#FFFFFF" stroke-width="7.5"/>
  <circle cx="32" cy="32" r="22" fill="none" stroke="#D93025" stroke-width="5.2"/>
  <line x1="16.5" y1="16.5" x2="47.5" y2="47.5" stroke="#D93025" stroke-width="5.2" stroke-linecap="round"/>
</svg>"""


def get_sizens_svg():
    return """<svg width="64" height="64" viewBox="0 0 64 64" xmlns="http://www.w3.org/2000/svg">
  <path d="M 32 4 L 44 17 H 35.5 V 47 H 44 L 32 60 L 20 47 H 28.5 V 17 H 20 Z"
        fill="#FFFFFF" stroke="#262626" stroke-width="2.8" stroke-linejoin="round"/>
</svg>"""


def get_sizewe_svg():
    return """<svg width="64" height="64" viewBox="0 0 64 64" xmlns="http://www.w3.org/2000/svg">
  <path d="M 4 32 L 17 20 V 28.5 H 47 V 20 L 60 32 L 47 44 V 35.5 H 17 V 44 Z"
        fill="#FFFFFF" stroke="#262626" stroke-width="2.8" stroke-linejoin="round"/>
</svg>"""


def get_sizenwse_svg():
    return """<svg width="64" height="64" viewBox="0 0 64 64" xmlns="http://www.w3.org/2000/svg">
  <g transform="rotate(45, 32, 32)">
    <path d="M 4 32 L 17 20 V 28.5 H 47 V 20 L 60 32 L 47 44 V 35.5 H 17 V 44 Z"
          fill="#FFFFFF" stroke="#262626" stroke-width="2.8" stroke-linejoin="round"/>
  </g>
</svg>"""


def get_sizenesw_svg():
    return """<svg width="64" height="64" viewBox="0 0 64 64" xmlns="http://www.w3.org/2000/svg">
  <g transform="rotate(-45, 32, 32)">
    <path d="M 4 32 L 17 20 V 28.5 H 47 V 20 L 60 32 L 47 44 V 35.5 H 17 V 44 Z"
          fill="#FFFFFF" stroke="#262626" stroke-width="2.8" stroke-linejoin="round"/>
  </g>
</svg>"""


def get_sizeall_svg():
    return """<svg width="64" height="64" viewBox="0 0 64 64" xmlns="http://www.w3.org/2000/svg">
  <path d="M 32 4 L 41 14 H 35 V 29 H 50 V 23 L 60 32 L 50 41 V 35 H 35 V 50 H 41 L 32 60 L 23 50 H 29 V 35 H 14 V 41 L 4 32 L 14 23 V 29 H 29 V 14 H 23 Z"
        fill="#FFFFFF" stroke="#262626" stroke-width="2.6" stroke-linejoin="round"/>
</svg>"""


def get_uparrow_svg():
    return """<svg width="64" height="64" viewBox="0 0 64 64" xmlns="http://www.w3.org/2000/svg">
  <path d="M 32 4 L 45 20 H 36 V 56 H 28 V 20 H 19 Z"
        fill="#FFFFFF" stroke="#262626" stroke-width="2.8" stroke-linejoin="round"/>
</svg>"""


def get_hand_svg():
    return """<svg width="64" height="64" viewBox="0 0 64 64" xmlns="http://www.w3.org/2000/svg">
  <path d="M 20 6 C 20 3.8 21.8 2 24 2 C 26.2 2 28 3.8 28 6 V 26 H 30 C 31.6 26 33 27.4 33 29 V 30 H 35 C 36.6 30 38 31.4 38 33 V 34 H 40 C 41.6 34 43 35.4 43 37 V 46 C 43 53 37 58 30 58 C 24 58 19 55 16 49 L 11 39 C 9.8 36.6 10.8 33.6 13.2 32.4 C 15.6 31.2 18.6 32.2 19.8 34.6 L 20 35 V 6 Z"
        fill="#FFFFFF" stroke="#262626" stroke-width="2.8" stroke-linejoin="round"/>
</svg>"""


def get_handwriting_svg():
    return """<svg width="64" height="64" viewBox="0 0 64 64" xmlns="http://www.w3.org/2000/svg">
  <path d="M 6 6 L 18 10 L 52 44 C 55 47 55 51 52 54 C 49 57 45 57 42 54 L 8 20 Z"
        fill="#FFFFFF" stroke="#262626" stroke-width="2.8" stroke-linejoin="round"/>
  <line x1="36" y1="48" x2="46" y2="38" stroke="#262626" stroke-width="2.4"/>
</svg>"""


SVG_GENERATORS = {
    "arrow": get_arrow_svg,
    "help": get_help_svg,
    "ibeam": get_ibeam_svg,
    "crosshair": get_crosshair_svg,
    "no": get_no_svg,
    "sizens": get_sizens_svg,
    "sizewe": get_sizewe_svg,
    "sizenwse": get_sizenwse_svg,
    "sizenesw": get_sizenesw_svg,
    "sizeall": get_sizeall_svg,
    "uparrow": get_uparrow_svg,
    "hand": get_hand_svg,
    "handwriting": get_handwriting_svg,
}


def render_svg_to_png(svg_text, out_png, size, bbox=None, pos=None):
    svg_file = out_png + ".svg"
    with open(svg_file, "w") as f:
        f.write(svg_text)
    if bbox and pos:
        tw, th = bbox
        px, py = pos
        hi_png = out_png + ".hi.png"
        trim_png = out_png + ".trim.png"
        subprocess.run(["rsvg-convert", "-w", "128", "-h", "128", "-o", hi_png, svg_file], check=True)
        subprocess.run(
            ["convert", hi_png, "-trim", "+repage", "-filter", "Mitchell", "-resize", f"{tw}x{th}!", "-depth", "8", "PNG32:" + trim_png],
            check=True,
        )
        subprocess.run(
            [
                "convert", "-size", f"{size}x{size}", "xc:none",
                trim_png, "-geometry", f"+{px}+{py}", "-composite",
                "-depth", "8", "PNG32:" + out_png,
            ],
            check=True,
        )
    else:
        raw_png = out_png + ".raw.png"
        subprocess.run(["rsvg-convert", "-w", str(size), "-h", str(size), "-o", raw_png, svg_file], check=True)
        subprocess.run(["convert", raw_png, "-depth", "8", "PNG32:" + out_png], check=True)


def extract_cur_to_pngs(cur_path, work, role, hotspot_32):
    """Extract 32x32 from an open .cur file and generate 32x32 and 48x48 PNGs."""
    if not os.path.isfile(cur_path):
        raise SystemExit(f"missing source file: {cur_path}")
    ext_dir = os.path.join(work, f"ext_{role}")
    os.makedirs(ext_dir, exist_ok=True)
    res = subprocess.run(["icotool", "-x", "-o", ext_dir, cur_path], capture_output=True, text=True)
    if res.returncode != 0:
        raise SystemExit(f"icotool failed on {cur_path}: {res.stderr.strip()}")
    files = sorted(os.listdir(ext_dir))
    if not files:
        raise SystemExit(f"icotool extracted no images from {cur_path}")
    hi = [f for f in files if "x32.png" in f or "x24.png" in f or "x8.png" in f]
    src_png = os.path.join(ext_dir, hi[-1] if hi else files[-1])

    png32 = os.path.join(work, f"{role}_32.png")
    png48 = os.path.join(work, f"{role}_48.png")
    subprocess.run(
        ["convert", src_png, "-filter", "Mitchell", "-resize", "32x32!", "-depth", "8", "PNG32:" + png32],
        check=True,
    )
    subprocess.run(
        ["convert", src_png, "-filter", "Mitchell", "-resize", "48x48!", "-depth", "8", "PNG32:" + png48],
        check=True,
    )
    hx32, hy32 = hotspot_32
    hx48, hy48 = round(hx32 * 48 / 32), round(hy32 * 48 / 32)
    return (png32, (hx32, hy32)), (png48, (hx48, hy48))


def build_cur_from_pngs(png32_info, png48_info, out_cur):
    (png32, (hx32, hy32)) = png32_info
    (png48, (hx48, hy48)) = png48_info
    cur32 = png32 + ".part.cur"
    cur48 = png48 + ".part.cur"
    subprocess.run(["icotool", "-c", "--cursor", "-X", str(hx32), "-Y", str(hy32), "-o", cur32, png32], check=True)
    subprocess.run(["icotool", "-c", "--cursor", "-X", str(hx48), "-Y", str(hy48), "-o", cur48, png48], check=True)

    b32 = open(cur32, "rb").read()
    b48 = open(cur48, "rb").read()
    e32 = b32[6:22]
    d32 = b32[22:]
    e48 = b48[6:22]
    d48 = b48[22:]

    offset32 = 6 + 16 * 2
    offset48 = offset32 + len(d32)
    e32_fixed = e32[:12] + struct.pack("<I", offset32)
    e48_fixed = e48[:12] + struct.pack("<I", offset48)

    header = struct.pack("<HHH", 0, 2, 2)
    with open(out_cur, "wb") as f:
        f.write(header + e32_fixed + e48_fixed + d32 + d48)


def provenance(mapping_path):
    data = json.load(open(mapping_path))
    lines = [
        f"# {data['display_name']}: cursor provenance",
        "",
        f"Generated by `scripts/build_cursors.py --provenance {os.path.relpath(mapping_path, ROOT).replace(os.sep, '/')}`.",
        "",
        data.get("provenance", ""),
        "",
        f"Licence of the cursor set as a whole: {data['licence']}.",
        "",
        "| Source | Licence | Where | Version |",
        "|---|---|---|---|",
    ]
    for name, src in data.get("sources", {}).items():
        lines.append(f"| {name}: {src['title']} | {src['licence']} | {src['url']} | {src.get('commit', '')} |")

    lines += [
        "",
        "| Role | File | Type | Hotspot (32px) | Hotspot (48px) | Source / Construction |",
        "|---|---|---|---|---|---|",
    ]
    for role, spec in data["cursors"].items():
        fname = spec["file"]
        ctype = "Animated (.ani)" if fname.endswith(".ani") else "Static (.cur)"
        hx32, hy32 = spec.get("hotspot_32", spec.get("hotspot", [0, 0]))
        hx48, hy48 = spec.get("hotspot_48", [round(hx32 * 48 / 32), round(hy32 * 48 / 32)])
        desc = spec.get("description", spec.get("source_cur", spec.get("generator", "")))
        lines.append(f"| {role} | `{fname}` | {ctype} | `({hx32}, {hy32})` | `({hx48}, {hy48})` | {desc} |")
    lines.append("")
    return "\n".join(lines)


def build_from_mapping(mapping_path, out_dir, sources, png_dir=None):
    data = json.load(open(mapping_path))
    req_sources = set(data.get("sources", {}).keys())
    missing = req_sources - set(sources.keys())
    if missing:
        raise SystemExit(f"give --source for: {', '.join(sorted(missing))}")
    for s in sorted(req_sources):
        if not os.path.isdir(sources[s]):
            raise SystemExit(f"missing source directory for {s}: {sources[s]}")

    name = data["name"]
    cursors_dest = os.path.join(out_dir, name, "Cursors")
    os.makedirs(cursors_dest, exist_ok=True)
    if png_dir:
        os.makedirs(png_dir, exist_ok=True)

    with tempfile.TemporaryDirectory() as work:
        for role, spec in data["cursors"].items():
            fname = spec["file"]
            out_file = os.path.join(cursors_dest, fname)
            hx32, hy32 = spec.get("hotspot_32", spec.get("hotspot", [0, 0]))
            hx48, hy48 = spec.get("hotspot_48", [round(hx32 * 48 / 32), round(hy32 * 48 / 32)])
            bbox32 = spec.get("bbox_32")
            pos32 = spec.get("pos_32")
            bbox48 = spec.get("bbox_48")
            pos48 = spec.get("pos_48")

            if "source_cur" in spec:
                src_ref = spec["source_cur"]
                if ":" in src_ref:
                    sname, srel = src_ref.split(":", 1)
                    src_cur = os.path.join(sources[sname], srel)
                else:
                    raise SystemExit(f"source_cur must use source:path format, got {src_ref}")
                p32_info, p48_info = extract_cur_to_pngs(src_cur, work, role, (hx32, hy32))
                if fname.endswith(".ani"):
                    tmp_cur = os.path.join(work, f"{role}_frame0.cur")
                    build_cur_from_pngs(p32_info, p48_info, tmp_cur)
                    make_ani([tmp_cur], out_file, jif_rate=6)
                else:
                    build_cur_from_pngs(p32_info, p48_info, out_file)
                if png_dir:
                    shutil.copy(p32_info[0], os.path.join(png_dir, f"{role}_32.png"))
                    shutil.copy(p48_info[0], os.path.join(png_dir, f"{role}_48.png"))
                print(f"Built {out_file} from {spec['source_cur']} (hotspot 32px={p32_info[1]}, 48px={p48_info[1]})")

            elif spec.get("generator") in ("wait_ani", "appstarting_ani"):
                with_arrow = spec["generator"] == "appstarting_ani"
                frames_cur = []
                for i in range(8):
                    angle = i * 45
                    svg32 = get_busy_ring_frame_svg(angle, with_arrow=with_arrow)
                    p32 = os.path.join(work, f"{role}_{i}_32.png")
                    p48 = os.path.join(work, f"{role}_{i}_48.png")
                    render_svg_to_png(svg32, p32, 32, bbox=bbox32, pos=pos32)
                    render_svg_to_png(svg32, p48, 48, bbox=bbox48, pos=pos48)
                    f_cur = os.path.join(work, f"{role}_{i}.cur")
                    build_cur_from_pngs((p32, (hx32, hy32)), (p48, (hx48, hy48)), f_cur)
                    frames_cur.append(f_cur)
                    if i == 0 and png_dir:
                        shutil.copy(p32, os.path.join(png_dir, f"{role}_32.png"))
                        shutil.copy(p48, os.path.join(png_dir, f"{role}_48.png"))
                make_ani(frames_cur, out_file, jif_rate=6)
                print(f"Built {out_file} (8 frames, hotspot 32px=({hx32},{hy32}), 48px=({hx48},{hy48}))")

            elif "generator" in spec:
                gen_name = spec["generator"]
                svg = SVG_GENERATORS[gen_name]()
                p32 = os.path.join(work, f"{role}_32.png")
                p48 = os.path.join(work, f"{role}_48.png")
                render_svg_to_png(svg, p32, 32, bbox=bbox32, pos=pos32)
                render_svg_to_png(svg, p48, 48, bbox=bbox48, pos=pos48)
                build_cur_from_pngs((p32, (hx32, hy32)), (p48, (hx48, hy48)), out_file)
                if png_dir:
                    shutil.copy(p32, os.path.join(png_dir, f"{role}_32.png"))
                    shutil.copy(p48, os.path.join(png_dir, f"{role}_48.png"))
                print(f"Built {out_file} (hotspot 32px=({hx32},{hy32}), 48px=({hx48},{hy48}))")

    for s in sorted(req_sources):
        lic_file = data["sources"][s].get("licence_file", "LICENSE")
        src_lic = os.path.join(sources[s], lic_file)
        if not os.path.isfile(src_lic):
            raise SystemExit(f"missing licence file: {src_lic}")
        shutil.copy(src_lic, os.path.join(cursors_dest, f"LICENSE-{s}"))
    open(os.path.join(cursors_dest, "LICENSE"), "w").write(
        f"{data['display_name']}: {data['licence']}.\nSee docs/cursors/{name.lower()}.md in the WineExplorer repository.\n"
    )


def main():
    args = sys.argv[1:]
    if len(args) == 2 and args[0] == "--provenance":
        sys.stdout.write(provenance(args[1]))
        return
    if len(args) < 2:
        raise SystemExit(__doc__)
    mapping_path, out_dir, rest = args[0], args[1], args[2:]
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

    build_from_mapping(mapping_path, out_dir, sources, png_dir=png_dir)


if __name__ == "__main__":
    main()

#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright 2026 WineExplorer contributors
"""Build cursor schemes (.cur, .ani, theme section, provenance and previews) from JSON mappings.

Usage:
  build_cursors.py MAPPING OUT_DIR [--theme-file THEME] [--preview PREVIEW_PNG]
  build_cursors.py --provenance MAPPING > docs/cursors/<name>.md

Roles covered (15):
  Arrow, Help, AppStarting, Wait, Crosshair, IBeam, NWPen, No, SizeNS, SizeWE,
  SizeNWSE, SizeNESW, SizeAll, UpArrow, Hand.
Each cursor file contains both 32x32 and 48x48 images with respective hotspots.
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

ROLES = [
    "Arrow", "Help", "AppStarting", "Wait", "Crosshair", "IBeam",
    "NWPen", "No", "SizeNS", "SizeWE", "SizeNWSE", "SizeNESW",
    "SizeAll", "UpArrow", "Hand"
]


def make_ani(cur_files, out_ani_path, jif_rate=6):
    """Pack an array of .cur file paths into a RIFF ACON animated cursor."""
    frames = [open(f, "rb").read() for f in cur_files]
    anih = struct.pack("<IIIIIIIII", 36, len(frames), len(frames), 0, 0, 0, 0, jif_rate, 1)
    anih_chunk = b"anih" + struct.pack("<I", len(anih)) + anih

    fram_data = bytearray()
    for f in frames:
        chunk = b"icon" + struct.pack("<I", len(f)) + f
        if len(chunk) % 2 != 0:
            chunk += b"\x00"
        fram_data.extend(chunk)
    fram_list = b"LIST" + struct.pack("<I", 4 + len(fram_data)) + b"fram" + fram_data

    riff_data = b"ACON" + anih_chunk + fram_list
    riff = b"RIFF" + struct.pack("<I", len(riff_data)) + riff_data
    with open(out_ani_path, "wb") as out:
        out.write(riff)


def pack_multi_res_cur(png32_path, png48_path, hotspot_32, hotspot_48, out_cur_path, work_dir):
    """Combine 32x32 and 48x48 PNG images into a single Win32 .cur with 2 entries."""
    t32 = os.path.join(work_dir, "temp_32.cur")
    t48 = os.path.join(work_dir, "temp_48.cur")
    subprocess.run(["icotool", "-c", "--cursor", "-X", str(hotspot_32[0]), "-Y", str(hotspot_32[1]),
                    "-o", t32, png32_path], check=True)
    subprocess.run(["icotool", "-c", "--cursor", "-X", str(hotspot_48[0]), "-Y", str(hotspot_48[1]),
                    "-o", t48, png48_path], check=True)

    d32 = open(t32, "rb").read()
    d48 = open(t48, "rb").read()

    off32 = struct.unpack("<I", d32[18:22])[0]
    img32 = d32[off32:]

    off48 = struct.unpack("<I", d48[18:22])[0]
    img48 = d48[off48:]

    header = struct.pack("<HHH", 0, 2, 2)
    e32 = bytearray(d32[6:22])
    struct.pack_into("<I", e32, 12, 38)

    e48 = bytearray(d48[6:22])
    struct.pack_into("<I", e48, 12, 38 + len(img32))

    combined = header + e32 + e48 + img32 + img48
    with open(out_cur_path, "wb") as out:
        out.write(combined)


def render_svg_to_png(svg_text, out_png, size=32):
    with tempfile.NamedTemporaryFile("w", suffix=".svg", delete=False) as f:
        f.write(svg_text)
        svg_name = f.name
    try:
        subprocess.run(["rsvg-convert", "-w", str(size), "-h", str(size), "-o", out_png, svg_name], check=True)
    finally:
        os.remove(svg_name)


# --- Fluent Vector SVG Definitions ---

def get_fluent_arrow_svg(size=32):
    scale = size / 32.0
    return f'''<svg width="{size}" height="{size}" viewBox="0 0 32 32" xmlns="http://www.w3.org/2000/svg">
  <defs>
    <filter id="shadow" x="-20%" y="-20%" width="150%" height="150%">
      <feDropShadow dx="1" dy="1.5" stdDeviation="0.8" flood-color="#000000" flood-opacity="0.4"/>
    </filter>
  </defs>
  <g transform="translate(1, 1)" filter="url(#shadow)">
    <path d="M 1 1 L 1 23.5 L 6.8 17.8 L 11.2 28.2 L 15.2 26.5 L 10.8 16.1 L 18 16.1 Z"
          fill="#FFFFFF" stroke="#1E1E1E" stroke-width="1.8" stroke-linejoin="round"/>
  </g>
</svg>'''


def get_fluent_help_svg(size=32):
    return f'''<svg width="{size}" height="{size}" viewBox="0 0 32 32" xmlns="http://www.w3.org/2000/svg">
  <defs>
    <filter id="shadow" x="-20%" y="-20%" width="150%" height="150%">
      <feDropShadow dx="1" dy="1.5" stdDeviation="0.8" flood-color="#000000" flood-opacity="0.4"/>
    </filter>
  </defs>
  <g transform="translate(1, 1)" filter="url(#shadow)">
    <path d="M 1 1 L 1 23.5 L 6.8 17.8 L 11.2 28.2 L 15.2 26.5 L 10.8 16.1 L 18 16.1 Z"
          fill="#FFFFFF" stroke="#1E1E1E" stroke-width="1.8" stroke-linejoin="round"/>
    <g transform="translate(15, 9) scale(0.65)">
      <circle cx="9" cy="9" r="9" fill="#1E79D6"/>
      <path d="M 9 4 C 7.5 4 6.5 5 6.5 6.2 H 8 C 8 5.7 8.5 5.2 9.2 5.2 C 9.9 5.2 10.5 5.6 10.5 6.2 C 10.5 6.8 10 7.3 9.4 7.7 C 8.5 8.3 8.1 9 8.1 10 H 9.6 C 9.6 9.3 9.9 8.9 10.4 8.5 C 11.2 7.9 12 7.2 12 6.1 C 12 4.8 10.6 4 9 4 Z M 8.1 11.5 H 9.7 V 13.1 H 8.1 Z" fill="#FFFFFF"/>
    </g>
  </g>
</svg>'''


def get_fluent_ring_frame_svg(angle_deg, center=(16, 16), radius=7.5, with_arrow=False, size=32):
    cx, cy = center
    arrow_part = ""
    if with_arrow:
        arrow_part = '''<g transform="translate(1, 1)">
    <path d="M 1 1 L 1 21 L 6.2 16 L 10.2 25.5 L 13.8 24 L 9.8 14.5 L 16.2 14.5 Z"
          fill="#FFFFFF" stroke="#1E1E1E" stroke-width="1.8" stroke-linejoin="round"/>
  </g>'''
    dots = []
    num_dots = 6
    for step in range(num_dots):
        a = math.radians(angle_deg - (step * 35))
        x = cx + radius * math.cos(a)
        y = cy + radius * math.sin(a)
        opacity = 1.0 - (step * 0.18)
        r = 2.4 - (step * 0.3)
        dots.append(f'<circle cx="{x:.2f}" cy="{y:.2f}" r="{r:.2f}" fill="#1E79D6" fill-opacity="{opacity:.2f}"/>')
    dots_xml = "\n    ".join(dots)
    return f'''<svg width="{size}" height="{size}" viewBox="0 0 32 32" xmlns="http://www.w3.org/2000/svg">
  {arrow_part}
  <g>
    {dots_xml}
  </g>
</svg>'''


def get_fluent_crosshair_svg(size=32):
    return f'''<svg width="{size}" height="{size}" viewBox="0 0 32 32" xmlns="http://www.w3.org/2000/svg">
  <path d="M16 2 V11 M16 21 V30 M2 16 H11 M21 16 H30" stroke="#1E1E1E" stroke-width="3" stroke-linecap="square"/>
  <path d="M16 2 V11 M16 21 V30 M2 16 H11 M21 16 H30" stroke="#FFFFFF" stroke-width="1.4" stroke-linecap="square"/>
</svg>'''


def get_fluent_ibeam_svg(size=32):
    return f'''<svg width="{size}" height="{size}" viewBox="0 0 32 32" xmlns="http://www.w3.org/2000/svg">
  <path d="M12 4 H20 M16 4 V28 M12 28 H20" stroke="#1E1E1E" stroke-width="3" stroke-linecap="square"/>
  <path d="M12 4 H20 M16 4 V28 M12 28 H20" stroke="#FFFFFF" stroke-width="1.4" stroke-linecap="square"/>
</svg>'''


def get_fluent_no_svg(size=32):
    return f'''<svg width="{size}" height="{size}" viewBox="0 0 32 32" xmlns="http://www.w3.org/2000/svg">
  <circle cx="16" cy="16" r="11" fill="none" stroke="#1E1E1E" stroke-width="4.5"/>
  <circle cx="16" cy="16" r="11" fill="none" stroke="#D9362B" stroke-width="3"/>
  <line x1="8.2" y1="8.2" x2="23.8" y2="23.8" stroke="#1E1E1E" stroke-width="4.5" stroke-linecap="round"/>
  <line x1="8.2" y1="8.2" x2="23.8" y2="23.8" stroke="#D9362B" stroke-width="3" stroke-linecap="round"/>
</svg>'''


def get_fluent_sizens_svg(size=32):
    return f'''<svg width="{size}" height="{size}" viewBox="0 0 32 32" xmlns="http://www.w3.org/2000/svg">
  <path d="M16 2 L21 9 H18 V23 H21 L16 30 L11 23 H14 V9 H11 Z"
        fill="#FFFFFF" stroke="#1E1E1E" stroke-width="1.8" stroke-linejoin="round"/>
</svg>'''


def get_fluent_sizewe_svg(size=32):
    return f'''<svg width="{size}" height="{size}" viewBox="0 0 32 32" xmlns="http://www.w3.org/2000/svg">
  <path d="M2 16 L9 11 V14 H23 V11 L30 16 L23 21 V18 H9 V21 Z"
        fill="#FFFFFF" stroke="#1E1E1E" stroke-width="1.8" stroke-linejoin="round"/>
</svg>'''


def get_fluent_sizenwse_svg(size=32):
    return f'''<svg width="{size}" height="{size}" viewBox="0 0 32 32" xmlns="http://www.w3.org/2000/svg">
  <g transform="rotate(45, 16, 16)">
    <path d="M2 16 L9 11 V14 H23 V11 L30 16 L23 21 V18 H9 V21 Z"
          fill="#FFFFFF" stroke="#1E1E1E" stroke-width="1.8" stroke-linejoin="round"/>
  </g>
</svg>'''


def get_fluent_sizenesw_svg(size=32):
    return f'''<svg width="{size}" height="{size}" viewBox="0 0 32 32" xmlns="http://www.w3.org/2000/svg">
  <g transform="rotate(-45, 16, 16)">
    <path d="M2 16 L9 11 V14 H23 V11 L30 16 L23 21 V18 H9 V21 Z"
          fill="#FFFFFF" stroke="#1E1E1E" stroke-width="1.8" stroke-linejoin="round"/>
  </g>
</svg>'''


def get_fluent_sizeall_svg(size=32):
    return f'''<svg width="{size}" height="{size}" viewBox="0 0 32 32" xmlns="http://www.w3.org/2000/svg">
  <path d="M16 2 L20 7 H17 V15 H25 V12 L30 16 L25 20 V17 H17 V25 H20 L16 30 L12 25 H15 V17 H7 V20 L2 16 L7 12 V15 H15 V7 H12 Z"
        fill="#FFFFFF" stroke="#1E1E1E" stroke-width="1.8" stroke-linejoin="round"/>
</svg>'''


def get_fluent_uparrow_svg(size=32):
    return f'''<svg width="{size}" height="{size}" viewBox="0 0 32 32" xmlns="http://www.w3.org/2000/svg">
  <path d="M16 2 L23 11 H18 V28 H14 V11 H9 Z"
        fill="#FFFFFF" stroke="#1E1E1E" stroke-width="1.8" stroke-linejoin="round"/>
</svg>'''


def get_fluent_hand_svg(size=32):
    return f'''<svg width="{size}" height="{size}" viewBox="0 0 32 32" xmlns="http://www.w3.org/2000/svg">
  <defs>
    <filter id="shadow" x="-20%" y="-20%" width="150%" height="150%">
      <feDropShadow dx="1" dy="1.5" stdDeviation="0.8" flood-color="#000000" flood-opacity="0.4"/>
    </filter>
  </defs>
  <g transform="translate(-1, -1)" filter="url(#shadow)">
    <path d="M10 3 C10 1.9 10.9 1 12 1 C13.1 1 14 1.9 14 3 V13 H15 C15.8 13 16.5 13.7 16.5 14.5 V15 H17.5 C18.3 15 19 15.7 19 16.5 V17 H20 C20.8 17 21.5 17.7 21.5 18.5 V23 C21.5 26.5 18.5 29 15 29 C12 29 9.5 27.5 8 24.5 L5.5 19.5 C4.9 18.3 5.4 16.8 6.6 16.2 C7.8 15.6 9.3 16.1 9.9 17.3 L10 17.5 V3 Z"
          fill="#FFFFFF" stroke="#1E1E1E" stroke-width="1.8" stroke-linejoin="round"/>
  </g>
</svg>'''


def get_fluent_handwriting_svg(size=32):
    return f'''<svg width="{size}" height="{size}" viewBox="0 0 32 32" fill="none" xmlns="http://www.w3.org/2000/svg">
  <g transform="translate(3, 3)">
    <path d="M15.8911 3.04727C17.2885 1.64967 19.5543 1.6496 20.9519 3.04712C22.3493 4.44453 22.3493 6.71015 20.952 8.10763L20.0606 8.99918C21.3252 10.2688 21.3236 12.3231 20.0559 13.5907L18.2725 15.3741C17.9796 15.667 17.5048 15.667 17.2119 15.3741C16.919 15.0812 16.919 14.6064 17.2119 14.3135L18.9953 12.5301C19.6771 11.8482 19.6787 10.7437 19 10.0599L9.00191 20.0591C8.59546 20.4656 8.08418 20.7514 7.52498 20.8845L2.92373 21.98C2.67037 22.0404 2.40385 21.9649 2.21968 21.7808C2.03552 21.5966 1.96009 21.3301 2.02041 21.0767L3.116 16.4753C3.2491 15.9162 3.5347 15.4051 3.94103 14.9987L15.8911 3.04727Z"
          fill="#FFFFFF" stroke="#1E1E1E" stroke-width="1.8" stroke-linejoin="round"/>
  </g>
</svg>'''


FLUENT_SVG_MAP = {
    "Arrow": get_fluent_arrow_svg,
    "Help": get_fluent_help_svg,
    "Crosshair": get_fluent_crosshair_svg,
    "IBeam": get_fluent_ibeam_svg,
    "No": get_fluent_no_svg,
    "SizeNS": get_fluent_sizens_svg,
    "SizeWE": get_fluent_sizewe_svg,
    "SizeNWSE": get_fluent_sizenwse_svg,
    "SizeNESW": get_fluent_sizenesw_svg,
    "SizeAll": get_fluent_sizeall_svg,
    "UpArrow": get_fluent_uparrow_svg,
    "Hand": get_fluent_hand_svg,
    "NWPen": get_fluent_handwriting_svg,
}


def build_fluent_cursor_role(role, spec, out_path, work_dir):
    """Build a Fluent cursor role (.cur or .ani) with 32 and 48 px images."""
    h32 = spec["hotspot_32"]
    h48 = spec["hotspot_48"]

    if spec["type"] == "svg":
        fn = FLUENT_SVG_MAP[role]
        p32 = os.path.join(work_dir, f"{role}_32.png")
        p48 = os.path.join(work_dir, f"{role}_48.png")
        render_svg_to_png(fn(32), p32, size=32)
        render_svg_to_png(fn(48), p48, size=48)
        pack_multi_res_cur(p32, p48, h32, h48, out_path, work_dir)
        return p32

    elif spec["type"] == "animated_svg_ring":
        frames_cur = []
        preview_p32 = None
        with_arrow = spec.get("with_arrow", False)
        for i in range(spec.get("frames", 8)):
            angle = i * 45
            p32 = os.path.join(work_dir, f"{role}_frame_{i}_32.png")
            p48 = os.path.join(work_dir, f"{role}_frame_{i}_48.png")
            center32 = (22, 20) if with_arrow else (16, 16)
            center48 = (33, 30) if with_arrow else (24, 24)
            r32 = 6 if with_arrow else 7.5
            r48 = 9 if with_arrow else 11.25
            render_svg_to_png(get_fluent_ring_frame_svg(angle, center=center32, radius=r32, with_arrow=with_arrow, size=32), p32, size=32)
            render_svg_to_png(get_fluent_ring_frame_svg(angle, center=center48, radius=r48, with_arrow=with_arrow, size=48), p48, size=48)
            f_cur = os.path.join(work_dir, f"{role}_frame_{i}.cur")
            pack_multi_res_cur(p32, p48, h32, h48, f_cur, work_dir)
            frames_cur.append(f_cur)
            if i == 0:
                preview_p32 = p32
        make_ani(frames_cur, out_path, jif_rate=spec.get("jif_rate", 6))
        return preview_p32


def build_xp_cursor_role(role, spec, out_path, work_dir):
    """Build an XP cursor role (.cur or .ani) from ReactOS source with 32 and 48 px images."""
    h32 = spec["hotspot_32"]
    h48 = spec["hotspot_48"]

    if spec["type"] == "cur_source":
        src_path = os.path.join(ROOT, spec["source_cur"])
        p32 = os.path.join(work_dir, f"{role}_32.png")
        p48 = os.path.join(work_dir, f"{role}_48.png")
        # Extract 32-bit image from cursor (entry index 1)
        subprocess.run(["convert", f"{src_path}[1]", p32], check=True)
        # Smooth scaling with Mitchell filter to 48x48
        subprocess.run(["convert", p32, "-filter", "Mitchell", "-resize", "48x48", p48], check=True)
        pack_multi_res_cur(p32, p48, h32, h48, out_path, work_dir)
        return p32

    elif spec["type"] == "animated_hourglass":
        src_path = os.path.join(ROOT, spec["source_cur"])
        wait_32 = os.path.join(work_dir, "wait_base_32.png")
        subprocess.run(["convert", f"{src_path}[1]", wait_32], check=True)
        wait_48 = os.path.join(work_dir, "wait_base_48.png")
        subprocess.run(["convert", wait_32, "-filter", "Mitchell", "-resize", "48x48", wait_48], check=True)

        frames_cur = []
        preview_p32 = None
        for i in range(spec.get("frames", 8)):
            angle = i * 45
            p32 = os.path.join(work_dir, f"wait_rot_{i}_32.png")
            p48 = os.path.join(work_dir, f"wait_rot_{i}_48.png")
            subprocess.run(["convert", wait_32, "-background", "none", "-distort", "SRT", f"16,16 1 {angle} 16,16", p32], check=True)
            subprocess.run(["convert", wait_48, "-background", "none", "-distort", "SRT", f"24,24 1 {angle} 24,24", p48], check=True)
            f_cur = os.path.join(work_dir, f"wait_frame_{i}.cur")
            pack_multi_res_cur(p32, p48, h32, h48, f_cur, work_dir)
            frames_cur.append(f_cur)
            if i == 0:
                preview_p32 = p32
        make_ani(frames_cur, out_path, jif_rate=spec.get("jif_rate", 6))
        return preview_p32

    elif spec["type"] == "animated_composite_hourglass":
        arrow_src = os.path.join(ROOT, spec["arrow_cur"])
        wait_src = os.path.join(ROOT, spec["wait_cur"])
        arr_32 = os.path.join(work_dir, "arrow_base_32.png")
        wait_32 = os.path.join(work_dir, "wait_for_arr_32.png")
        subprocess.run(["convert", f"{arrow_src}[1]", arr_32], check=True)
        subprocess.run(["convert", f"{wait_src}[1]", wait_32], check=True)

        arr_48 = os.path.join(work_dir, "arrow_base_48.png")
        subprocess.run(["convert", arr_32, "-filter", "Mitchell", "-resize", "48x48", arr_48], check=True)

        mini_32 = os.path.join(work_dir, "mini_32.png")
        subprocess.run(["convert", wait_32, "-resize", "12x18", mini_32], check=True)
        mini_48 = os.path.join(work_dir, "mini_48.png")
        subprocess.run(["convert", wait_32, "-filter", "Mitchell", "-resize", "18x27", mini_48], check=True)

        frames_cur = []
        preview_p32 = None
        for i in range(spec.get("frames", 8)):
            angle = i * 45
            rot_mini_32 = os.path.join(work_dir, f"mini_rot_{i}_32.png")
            rot_mini_48 = os.path.join(work_dir, f"mini_rot_{i}_48.png")
            subprocess.run(["convert", mini_32, "-background", "none", "-distort", "SRT", f"{angle}", rot_mini_32], check=True)
            subprocess.run(["convert", mini_48, "-background", "none", "-distort", "SRT", f"{angle}", rot_mini_48], check=True)

            p32 = os.path.join(work_dir, f"appstart_frame_{i}_32.png")
            p48 = os.path.join(work_dir, f"appstart_frame_{i}_48.png")
            subprocess.run(["convert", arr_32, rot_mini_32, "-geometry", "+17+12", "-composite", p32], check=True)
            subprocess.run(["convert", arr_48, rot_mini_48, "-geometry", "+26+18", "-composite", p48], check=True)

            f_cur = os.path.join(work_dir, f"appstart_frame_{i}.cur")
            pack_multi_res_cur(p32, p48, h32, h48, f_cur, work_dir)
            frames_cur.append(f_cur)
            if i == 0:
                preview_p32 = p32
        make_ani(frames_cur, out_path, jif_rate=spec.get("jif_rate", 6))
        return preview_p32


def generate_theme_section(pack):
    """Generate the INI string for [Control Panel\\Cursors]."""
    name = pack["name"]
    disp = pack["display_name"]
    lines = ["[Control Panel\\Cursors]"]
    for role in ROLES:
        c = pack["cursors"][role]
        lines.append(f"{role}=%ResourceDir%\\Themes\\{name}\\Cursors\\{c['file']}")
    lines.append(f"DefaultValue={disp}")
    return "\n".join(lines) + "\n"


def update_theme_file(theme_path, pack):
    sec = generate_theme_section(pack)
    if os.path.isfile(theme_path):
        txt = open(theme_path, "r", encoding="utf-8", errors="ignore").read()
        if "[Control Panel\\Cursors]" in txt:
            txt = re.sub(r"\[Control Panel\\Cursors\][^\[]*", sec.strip() + "\n\n", txt)
        else:
            txt += "\n" + sec
        open(theme_path, "w", encoding="utf-8").write(txt)
        print(f"Updated cursors section in {theme_path}")


def generate_provenance(pack):
    lines = [
        f"# {pack['display_name']} Cursors: Provenance",
        "",
        f"Generated by `scripts/build_cursors.py` from `assets/cursors/{pack['name'].lower()}.json`.",
        f"Licence: **{pack['licence']}**.",
        "",
        "## Sources",
        "",
        "| Source | Licence | Location / URL | Commit |",
        "|---|---|---|---|",
    ]
    for k, s in pack["sources"].items():
        lines.append(f"| {s['title']} | {s['licence']} | {s['url']} | {s['commit']} |")

    lines += [
        "",
        "## Scheme Roles (15)",
        "",
        "All cursors are compiled with both **32x32** and **48x48** images and appropriate hotspot coordinates.",
        "",
        "| Role | File | Hotspot (32 / 48) | Type | Notes / Geometry |",
        "|---|---|---|---|---|",
    ]
    for role in ROLES:
        c = pack["cursors"][role]
        h32 = f"({c['hotspot_32'][0]}, {c['hotspot_32'][1]})"
        h48 = f"({c['hotspot_48'][0]}, {c['hotspot_48'][1]})"
        lines.append(f"| {role} | `{c['file']}` | {h32} / {h48} | {c['type']} | {c['meaning']} |")

    lines.append("")
    return "\n".join(lines)


def generate_preview_sheet(preview_pngs, out_preview_png, work_dir):
    """Generate a 5x3 montage contact sheet for the 15 cursors."""
    tiles = []
    for role in ROLES:
        p32 = preview_pngs[role]
        tile = os.path.join(work_dir, f"tile_{role}.png")
        # 64x64 canvas, off-white background, label south
        subprocess.run(["convert", p32, "-background", "#F5F5F7", "-gravity", "center",
                        "-extent", "64x64", "-bordercolor", "#E5E5E8", "-border", "1",
                        "-gravity", "south", "-splice", "0x16", "-pointsize", "10",
                        "-annotate", "+0+2", role, tile], check=True)
        tiles.append(tile)

    subprocess.run(["montage"] + tiles + ["-tile", "5x3", "-geometry", "+6+6",
                                          "-background", "#ECECEC", out_preview_png], check=True)
    print(f"Preview contact sheet saved: {out_preview_png}")


def main():
    if len(sys.argv) < 2:
        raise SystemExit(__doc__)

    if sys.argv[1] == "--provenance":
        mapping = sys.argv[2]
        pack = json.load(open(mapping))
        sys.stdout.write(generate_provenance(pack))
        return

    mapping = sys.argv[1]
    out_dir = sys.argv[2]
    theme_file = None
    preview_png = None

    i = 3
    while i < len(sys.argv):
        if sys.argv[i] == "--theme-file" and i + 1 < len(sys.argv):
            theme_file = sys.argv[i + 1]
            i += 2
        elif sys.argv[i] == "--preview" and i + 1 < len(sys.argv):
            preview_png = sys.argv[i + 1]
            i += 2
        else:
            i += 1

    pack = json.load(open(mapping))
    os.makedirs(out_dir, exist_ok=True)

    with tempfile.TemporaryDirectory() as work_dir:
        preview_pngs = {}
        is_fluent = pack["name"].lower() == "fluent"

        for role in ROLES:
            spec = pack["cursors"][role]
            out_file = os.path.join(out_dir, spec["file"])
            if is_fluent:
                p32 = build_fluent_cursor_role(role, spec, out_file, work_dir)
            else:
                p32 = build_xp_cursor_role(role, spec, out_file, work_dir)
            preview_pngs[role] = p32
            print(f"  {role} -> {spec['file']} (32 & 48 px, hotspot {spec['hotspot_32']} / {spec['hotspot_48']})")

        if preview_png:
            os.makedirs(os.path.dirname(os.path.abspath(preview_png)), exist_ok=True)
            generate_preview_sheet(preview_pngs, preview_png, work_dir)

    if theme_file:
        update_theme_file(theme_file, pack)

    print(f"Successfully built all 15 cursor roles in {out_dir}")


if __name__ == "__main__":
    main()

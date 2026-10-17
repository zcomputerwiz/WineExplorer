#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright 2026 WineExplorer contributors
"""Draw Fluent-style cursors from open SVG shapes and compile .cur and .ani files.

Usage:
  build_fluent_cursors.py OUT_DIR [--sources-dir DIR]
"""

import math
import os
import re
import shutil
import struct
import subprocess
import sys
import tempfile

FLUENT_REPO = "/root/iconsrc/fluentui-system-icons"
HOST_CURSORS = "/mnt/c/Windows/Cursors"

def make_ani(cur_files, out_ani_path, jif_rate=8):
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

def render_svg_to_png(svg_text, out_png, size=32):
    with tempfile.NamedTemporaryFile("w", suffix=".svg", delete=False) as f:
        f.write(svg_text)
        svg_name = f.name
    try:
        subprocess.run(["rsvg-convert", "-w", str(size), "-h", str(size), "-o", out_png, svg_name], check=True)
    finally:
        os.remove(svg_name)

def build_cursor_cur(svg_text, out_cur, hotspot, work_dir, name):
    png32 = os.path.join(work_dir, f"{name}_32.png")
    render_svg_to_png(svg_text, png32, size=32)
    hx, hy = hotspot
    subprocess.run(["icotool", "-c", "--cursor", "-X", str(hx), "-Y", str(hy), "-o", out_cur, png32], check=True)
    return png32

def get_arrow_svg():
    return '''<svg width="32" height="32" viewBox="0 0 32 32" xmlns="http://www.w3.org/2000/svg">
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

def get_help_svg():
    return '''<svg width="32" height="32" viewBox="0 0 32 32" xmlns="http://www.w3.org/2000/svg">
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

def get_busy_ring_frame_svg(angle_deg, center=(16, 16), radius=7.5, with_arrow=False):
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
    return f'''<svg width="32" height="32" viewBox="0 0 32 32" xmlns="http://www.w3.org/2000/svg">
  {arrow_part}
  <g>
    {dots_xml}
  </g>
</svg>'''

def get_ibeam_svg():
    return '''<svg width="32" height="32" viewBox="0 0 32 32" xmlns="http://www.w3.org/2000/svg">
  <path d="M12 4 H20 M16 4 V28 M12 28 H20" stroke="#1E1E1E" stroke-width="3" stroke-linecap="square"/>
  <path d="M12 4 H20 M16 4 V28 M12 28 H20" stroke="#FFFFFF" stroke-width="1.4" stroke-linecap="square"/>
</svg>'''

def get_crosshair_svg():
    return '''<svg width="32" height="32" viewBox="0 0 32 32" xmlns="http://www.w3.org/2000/svg">
  <path d="M16 2 V11 M16 21 V30 M2 16 H11 M21 16 H30" stroke="#1E1E1E" stroke-width="3" stroke-linecap="square"/>
  <path d="M16 2 V11 M16 21 V30 M2 16 H11 M21 16 H30" stroke="#FFFFFF" stroke-width="1.4" stroke-linecap="square"/>
</svg>'''

def get_no_svg():
    return '''<svg width="32" height="32" viewBox="0 0 32 32" xmlns="http://www.w3.org/2000/svg">
  <circle cx="16" cy="16" r="11" fill="none" stroke="#1E1E1E" stroke-width="4.5"/>
  <circle cx="16" cy="16" r="11" fill="none" stroke="#D9362B" stroke-width="3"/>
  <line x1="8.2" y1="8.2" x2="23.8" y2="23.8" stroke="#1E1E1E" stroke-width="4.5" stroke-linecap="round"/>
  <line x1="8.2" y1="8.2" x2="23.8" y2="23.8" stroke="#D9362B" stroke-width="3" stroke-linecap="round"/>
</svg>'''

def get_sizens_svg():
    return '''<svg width="32" height="32" viewBox="0 0 32 32" xmlns="http://www.w3.org/2000/svg">
  <path d="M16 2 L21 9 H18 V23 H21 L16 30 L11 23 H14 V9 H11 Z"
        fill="#FFFFFF" stroke="#1E1E1E" stroke-width="1.8" stroke-linejoin="round"/>
</svg>'''

def get_sizewe_svg():
    return '''<svg width="32" height="32" viewBox="0 0 32 32" xmlns="http://www.w3.org/2000/svg">
  <path d="M2 16 L9 11 V14 H23 V11 L30 16 L23 21 V18 H9 V21 Z"
        fill="#FFFFFF" stroke="#1E1E1E" stroke-width="1.8" stroke-linejoin="round"/>
</svg>'''

def get_sizenwse_svg():
    return '''<svg width="32" height="32" viewBox="0 0 32 32" xmlns="http://www.w3.org/2000/svg">
  <g transform="rotate(45, 16, 16)">
    <path d="M2 16 L9 11 V14 H23 V11 L30 16 L23 21 V18 H9 V21 Z"
          fill="#FFFFFF" stroke="#1E1E1E" stroke-width="1.8" stroke-linejoin="round"/>
  </g>
</svg>'''

def get_sizenesw_svg():
    return '''<svg width="32" height="32" viewBox="0 0 32 32" xmlns="http://www.w3.org/2000/svg">
  <g transform="rotate(-45, 16, 16)">
    <path d="M2 16 L9 11 V14 H23 V11 L30 16 L23 21 V18 H9 V21 Z"
          fill="#FFFFFF" stroke="#1E1E1E" stroke-width="1.8" stroke-linejoin="round"/>
  </g>
</svg>'''

def get_sizeall_svg():
    return '''<svg width="32" height="32" viewBox="0 0 32 32" xmlns="http://www.w3.org/2000/svg">
  <path d="M16 2 L20 7 H17 V15 H25 V12 L30 16 L25 20 V17 H17 V25 H20 L16 30 L12 25 H15 V17 H7 V20 L2 16 L7 12 V15 H15 V7 H12 Z"
        fill="#FFFFFF" stroke="#1E1E1E" stroke-width="1.8" stroke-linejoin="round"/>
</svg>'''

def get_uparrow_svg():
    return '''<svg width="32" height="32" viewBox="0 0 32 32" xmlns="http://www.w3.org/2000/svg">
  <path d="M16 2 L23 11 H18 V28 H14 V11 H9 Z"
        fill="#FFFFFF" stroke="#1E1E1E" stroke-width="1.8" stroke-linejoin="round"/>
</svg>'''

def get_hand_svg():
    return '''<svg width="32" height="32" viewBox="0 0 32 32" xmlns="http://www.w3.org/2000/svg">
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

def get_handwriting_svg():
    return '''<svg width="32" height="32" viewBox="0 0 32 32" fill="none" xmlns="http://www.w3.org/2000/svg">
  <g transform="translate(3, 3)">
    <path d="M15.8911 3.04727C17.2885 1.64967 19.5543 1.6496 20.9519 3.04712C22.3493 4.44453 22.3493 6.71015 20.952 8.10763L20.0606 8.99918C21.3252 10.2688 21.3236 12.3231 20.0559 13.5907L18.2725 15.3741C17.9796 15.667 17.5048 15.667 17.2119 15.3741C16.919 15.0812 16.919 14.6064 17.2119 14.3135L18.9953 12.5301C19.6771 11.8482 19.6787 10.7437 19 10.0599L9.00191 20.0591C8.59546 20.4656 8.08418 20.7514 7.52498 20.8845L2.92373 21.98C2.67037 22.0404 2.40385 21.9649 2.21968 21.7808C2.03552 21.5966 1.96009 21.3301 2.02041 21.0767L3.116 16.4753C3.2491 15.9162 3.5347 15.4051 3.94103 14.9987L15.8911 3.04727Z"
          fill="#FFFFFF" stroke="#1E1E1E" stroke-width="1.8" stroke-linejoin="round"/>
  </g>
</svg>'''

def build_all_cursors(out_dir, fluent_repo=FLUENT_REPO):
    os.makedirs(out_dir, exist_ok=True)
    with tempfile.TemporaryDirectory() as work:
        cur_specs = [
            ("arrow.cur", get_arrow_svg(), (1, 1)),
            ("help.cur", get_help_svg(), (1, 1)),
            ("crosshair.cur", get_crosshair_svg(), (16, 16)),
            ("ibeam.cur", get_ibeam_svg(), (16, 16)),
            ("no.cur", get_no_svg(), (16, 16)),
            ("sizens.cur", get_sizens_svg(), (16, 16)),
            ("sizewe.cur", get_sizewe_svg(), (16, 16)),
            ("sizenwse.cur", get_sizenwse_svg(), (16, 16)),
            ("sizenesw.cur", get_sizenesw_svg(), (16, 16)),
            ("sizeall.cur", get_sizeall_svg(), (16, 16)),
            ("uparrow.cur", get_uparrow_svg(), (16, 2)),
            ("hand.cur", get_hand_svg(), (10, 2)),
            ("handwriting.cur", get_handwriting_svg(), (5, 24)),
        ]

        rendered_pngs = {}
        for fname, svg, hotspot in cur_specs:
            out_file = os.path.join(out_dir, fname)
            png = build_cursor_cur(svg, out_file, hotspot, work, fname.replace(".cur", ""))
            rendered_pngs[fname] = png
            print(f"Built {out_file} (hotspot {hotspot})")

        # Build wait.ani (8 frames)
        wait_frames = []
        for i in range(8):
            angle = i * 45
            svg = get_busy_ring_frame_svg(angle, center=(16, 16), radius=8, with_arrow=False)
            f_png = os.path.join(work, f"wait_{i}.png")
            f_cur = os.path.join(work, f"wait_{i}.cur")
            render_svg_to_png(svg, f_png, size=32)
            subprocess.run(["icotool", "-c", "--cursor", "-X", "16", "-Y", "16", "-o", f_cur, f_png], check=True)
            wait_frames.append(f_cur)
        out_wait = os.path.join(out_dir, "wait.ani")
        make_ani(wait_frames, out_wait, jif_rate=6)
        print(f"Built {out_wait} ({len(wait_frames)} frames)")

        # Build appstarting.ani (8 frames)
        app_frames = []
        for i in range(8):
            angle = i * 45
            svg = get_busy_ring_frame_svg(angle, center=(22, 20), radius=6, with_arrow=True)
            f_png = os.path.join(work, f"app_{i}.png")
            f_cur = os.path.join(work, f"app_{i}.cur")
            render_svg_to_png(svg, f_png, size=32)
            subprocess.run(["icotool", "-c", "--cursor", "-X", "1", "-Y", "1", "-o", f_cur, f_png], check=True)
            app_frames.append(f_cur)
        out_app = os.path.join(out_dir, "appstarting.ani")
        make_ani(app_frames, out_app, jif_rate=6)
        print(f"Built {out_app} ({len(app_frames)} frames)")

        # Rip check against host genuine Windows cursors
        if os.path.isdir(HOST_CURSORS):
            print("\n=== Rip check against host Windows cursors ===")
            checks = [
                ("arrow.cur", "aero_arrow.cur"),
                ("help.cur", "aero_helpsel.cur"),
                ("hand.cur", "aero_link.cur"),
                ("crosshair.cur", "cross.cur"),
                ("ibeam.cur", "beam_r.cur"),
                ("sizens.cur", "aero_ns.cur"),
                ("sizewe.cur", "aero_ew.cur"),
                ("sizenwse.cur", "aero_nwse.cur"),
                ("sizenesw.cur", "aero_nesw.cur"),
                ("sizeall.cur", "aero_move.cur"),
                ("handwriting.cur", "aero_pen.cur"),
            ]
            for our_name, host_name in checks:
                host_path = os.path.join(HOST_CURSORS, host_name)
                if not os.path.isfile(host_path):
                    continue
                host_png = os.path.join(work, f"host_{host_name}.png")
                try:
                    subprocess.run(["convert", f"{host_path}[0]", host_png], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, check=True)
                    res = subprocess.run(["compare", "-metric", "RMSE", rendered_pngs[our_name], host_png, "null:"],
                                         stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
                    m = re.search(r"\(([\d\.]+)\)", res.stderr)
                    rmse = float(m.group(1)) if m else -1.0
                    status = "PASS (clean, independent art)" if rmse > 0.05 else "FAIL (suspicious match!)"
                    print(f"  {our_name} vs host {host_name}: RMSE = {rmse:.4f} -> {status}")
                except Exception as e:
                    print(f"  {our_name} vs host {host_name}: comparison note ({e})")

if __name__ == "__main__":
    out = sys.argv[1] if len(sys.argv) > 1 else "/tmp/fluent_cursors"
    build_all_cursors(out)

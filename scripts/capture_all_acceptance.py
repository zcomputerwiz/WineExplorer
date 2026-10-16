#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright 2026 WineExplorer contributors

import os
import re
import sys
import time
import subprocess
import shutil

WINE_PREFIX = "/root/.wine_test_shell"
SHOTS_DIR = "/root/WineExplorer/build/shots"
OUT_DIR = "/root/WineExplorer/docs/start-skins/screenshots"

def setup_prefix(dpi, style, skin):
    env = os.environ.copy()
    env["WINEPREFIX"] = WINE_PREFIX
    env["WINEDEBUG"] = "-all"

    subprocess.run(["wine", "reg", "add", "HKCU\\Control Panel\\Desktop", "/v", "LogPixels", "/t", "REG_DWORD", "/d", str(dpi), "/f"],
                   env=env, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    subprocess.run(["wine", "reg", "add", "HKCU\\Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\Advanced", "/v", "Start_Style", "/t", "REG_DWORD", "/d", str(style), "/f"],
                   env=env, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    if skin:
        subprocess.run(["wine", "reg", "add", "HKCU\\Software\\WineExplorer\\StartMenu", "/v", "Skin", "/t", "REG_SZ", "/d", skin, "/f"],
                       env=env, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    else:
        subprocess.run(["wine", "reg", "delete", "HKCU\\Software\\WineExplorer\\StartMenu", "/v", "Skin", "/f"],
                       env=env, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    subprocess.run(["wineserver", "-w"], env=env)

def deploy_dll(mode):
    src64 = f"{SHOTS_DIR}/wexshell_{mode}_64.dll"
    src32 = f"{SHOTS_DIR}/wexshell_{mode}_32.dll"
    shutil.copyfile(src64, "/usr/local/lib/wine/x86_64-windows/wexshell.dll")
    shutil.copyfile(src32, "/usr/local/lib/wine/i386-windows/wexshell.dll")

def capture_menu(dpi, style, skin, out_png, mode="branch"):
    deploy_dll(mode)
    setup_prefix(dpi, style, skin)

    display = ":99"
    env = os.environ.copy()
    env["WINEPREFIX"] = WINE_PREFIX
    env["DISPLAY"] = display
    env["WINEDEBUG"] = "-all"

    subprocess.run(["pkill", "-9", "-f", "Xvfb"], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    subprocess.run(["pkill", "-9", "-f", "explorer.exe"], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    time.sleep(0.5)

    if dpi == 144:
        screen_res = "1536x1152x24"
        desktop_geom = "1536x1152"
        btn_x, btn_y = 37, 1132
    else:
        screen_res = "1024x768x24"
        desktop_geom = "1024x768"
        btn_x, btn_y = 25, 755

    xvfb = subprocess.Popen(["Xvfb", display, "-screen", "0", screen_res, "-dpi", str(dpi)])
    time.sleep(1.0)

    try:
        shell = subprocess.Popen(["wine", "explorer.exe", f"/desktop=Shell,{desktop_geom}", "/shell"], env=env)
        time.sleep(3.5)

        subprocess.run(["xdotool", "mousemove", str(btn_x), str(btn_y), "click", "1"], env=env)
        time.sleep(1.5)

        tree_out = subprocess.check_output(["xwininfo", "-root", "-tree"], env=env, text=True)

        candidates = []
        for line in tree_out.splitlines():
            m = re.search(r'(\d+)x(\d+)\+(\d+)\+(\d+)\s+\+(\d+)\+(\d+)', line)
            if m:
                w, h, x, y, abs_x, abs_y = [int(v) for v in m.groups()]
                if 100 < h < 1000 and abs_y > 100 and w < 1000:
                    candidates.append((abs_x, abs_y, w, h))

        if candidates:
            min_x = min(c[0] for c in candidates)
            min_y = min(c[1] for c in candidates)
            max_x = max(c[0] + c[2] for c in candidates)
            max_y = max(c[1] + c[3] for c in candidates)
            crop_geom = f"{max_x - min_x}x{max_y - min_y}+{min_x}+{min_y}"
        else:
            if style == 1:
                crop_geom = "380x440+1+687" if dpi == 144 else "380x440+1+303"
            else:
                crop_geom = "362x384+1+740" if dpi == 144 else "264x376+1+364"

        print(f"[{mode}] dpi={dpi} style={style} skin='{skin}' -> crop={crop_geom} -> {os.path.basename(out_png)}")
        full_png = f"/tmp/full_cap_{dpi}_{style}.png"
        subprocess.run(["import", "-window", "root", full_png], env=env, check=True)
        os.makedirs(os.path.dirname(out_png), exist_ok=True)
        subprocess.run(["convert", full_png, "-crop", crop_geom, "+repage", out_png], check=True)
        if os.path.exists(full_png):
            os.remove(full_png)

    finally:
        subprocess.run(["wineserver", "-k"], env=env, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        if shell.poll() is None:
            shell.terminate()
            try: shell.wait(timeout=2)
            except: shell.kill()
        if xvfb.poll() is None:
            xvfb.terminate()
            try: xvfb.wait(timeout=2)
            except: xvfb.kill()
        time.sleep(0.5)

def compare_images(img1, img2, diff_out=None):
    cmd = ["compare", "-metric", "AE", img1, img2]
    if diff_out:
        cmd.append(diff_out)
    else:
        cmd.append("null:")
    res = subprocess.run(cmd, capture_output=True, text=True)
    diff_pixels = res.stderr.strip()
    return diff_pixels

def main():
    os.makedirs(OUT_DIR, exist_ok=True)
    os.makedirs(SHOTS_DIR, exist_ok=True)

    print("=================================================================")
    print(" 1. BASELINE UNMODIFIED COMPARISON (MASTER VS BRANCH)")
    print("=================================================================")
    master_c = f"{SHOTS_DIR}/master_classic_96.png"
    branch_c = f"{OUT_DIR}/baseline_classic.png"
    capture_menu(96, 0, "", master_c, "master")
    capture_menu(96, 0, "", branch_c, "branch")
    diff_c = compare_images(master_c, branch_c, f"{SHOTS_DIR}/diff_baseline_classic.png")
    print(f"-> Single-column Classic built-in diff vs master: {diff_c} pixels")

    master_x = f"{SHOTS_DIR}/master_xp_96.png"
    branch_x = f"{OUT_DIR}/baseline_xp2col.png"
    capture_menu(96, 1, "", master_x, "master")
    capture_menu(96, 1, "", branch_x, "branch")
    diff_x = compare_images(master_x, branch_x, f"{SHOTS_DIR}/diff_baseline_xp.png")
    print(f"-> Two-column XP built-in diff vs master: {diff_x} pixels")

    print("\n=================================================================")
    print(" 2. BUNDLED SKINS: 100% (96 DPI) AND 150% (144 DPI)")
    print("=================================================================")
    # Classic (single-column) bundled skins
    capture_menu(96,  0, "ClassicSkin",  f"{OUT_DIR}/ClassicSkin_classic_96.png",  "branch")
    capture_menu(144, 0, "ClassicSkin",  f"{OUT_DIR}/ClassicSkin_classic_144.png", "branch")
    capture_menu(96,  0, "Metro",        f"{OUT_DIR}/Metro_classic_96.png",        "branch")
    capture_menu(144, 0, "Metro",        f"{OUT_DIR}/Metro_classic_144.png",       "branch")

    # Two-column bundled skins
    capture_menu(96,  1, "Metallic7",    f"{OUT_DIR}/Metallic7_xp_96.png",    "branch")
    capture_menu(144, 1, "Metallic7",    f"{OUT_DIR}/Metallic7_xp_144.png",   "branch")
    capture_menu(96,  1, "ClassicSkin7", f"{OUT_DIR}/ClassicSkin7_xp_96.png",  "branch")
    capture_menu(144, 1, "ClassicSkin7", f"{OUT_DIR}/ClassicSkin7_xp_144.png", "branch")
    capture_menu(96,  1, "Midnight7",    f"{OUT_DIR}/Midnight7_xp_96.png",    "branch")
    capture_menu(144, 1, "Midnight7",    f"{OUT_DIR}/Midnight7_xp_144.png",   "branch")

    print("\n=================================================================")
    print(" 3. GENUINE THIRD-PARTY SKIN: CATPPUCCIN MOCHA (SCOTTYGROTTY)")
    print("=================================================================")
    capture_menu(96,  1, "Catppuccin Mocha", f"{OUT_DIR}/CatppuccinMocha_xp_96.png",      "branch")
    capture_menu(144, 1, "Catppuccin Mocha", f"{OUT_DIR}/CatppuccinMocha_xp_144.png",     "branch")
    capture_menu(96,  0, "Catppuccin Mocha", f"{OUT_DIR}/CatppuccinMocha_classic_96.png",  "branch")
    capture_menu(144, 0, "Catppuccin Mocha", f"{OUT_DIR}/CatppuccinMocha_classic_144.png", "branch")

    print("\n=================================================================")
    print(" 4. VIEW COMPATIBILITY FALLBACK VERIFICATION")
    print("=================================================================")
    # ClassicSkin (.skin) on Two-Column view -> falls back to built-in XP view
    fb_xp = f"{OUT_DIR}/fallback_ClassicSkin_xp_96.png"
    capture_menu(96, 1, "ClassicSkin", fb_xp, "branch")
    diff_fb_x = compare_images(branch_x, fb_xp)
    print(f"-> ClassicSkin on Two-Column View diff vs unskinned baseline: {diff_fb_x} pixels")

    # Metallic7 (.skin7) on Classic view -> falls back to built-in Classic view
    fb_cl = f"{OUT_DIR}/fallback_Metallic7_classic_96.png"
    capture_menu(96, 0, "Metallic7", fb_cl, "branch")
    diff_fb_c = compare_images(branch_c, fb_cl)
    print(f"-> Metallic7 on Classic View diff vs unskinned baseline: {diff_fb_c} pixels")

    print("\nAll acceptance captures completed successfully.")

if __name__ == "__main__":
    main()

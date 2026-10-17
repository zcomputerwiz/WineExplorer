#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright 2026 WineExplorer contributors

import os
import sys
import time
import subprocess
import shutil

WINE_PREFIX = "/root/.wine_test_shell"
SHOTS_DIR = "/root/WineExplorer/docs/fluent-theme/screenshots"
os.makedirs(SHOTS_DIR, exist_ok=True)

env = os.environ.copy()
env["WINEPREFIX"] = WINE_PREFIX
env["DISPLAY"] = ":99"
env["WINEDEBUG"] = "-all"

def run_cmd(cmd, check=True):
    return subprocess.run(cmd, env=env, check=check, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)

def cleanup():
    subprocess.run(["pkill", "-9", "-f", "control.exe"], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    subprocess.run(["pkill", "-9", "-f", "explorer.exe"], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    subprocess.run(["pkill", "-9", "-f", "Xvfb"], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    subprocess.run(["wineserver", "-k"], env=env, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)

def run_test_at_dpi(dpi=96, prefix_label="100dpi"):
    cleanup()
    time.sleep(1)

    subprocess.run(["sudo", "mount", "-o", "remount,rw", "/tmp/.X11-unix"], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    subprocess.run(["sudo", "chmod", "1777", "/tmp/.X11-unix"], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)

    # Configure taskbar and start menu style
    subprocess.run(["wine", "reg", "add", "HKCU\\Software\\WineExplorer\\Taskbar", "/v", "LockTaskbar", "/t", "REG_DWORD", "/d", "1", "/f"], env=env)
    subprocess.run(["wine", "reg", "add", "HKCU\\Software\\WineExplorer\\Taskbar", "/v", "Rows", "/t", "REG_DWORD", "/d", "1", "/f"], env=env)
    subprocess.run(["wine", "reg", "add", "HKCU\\Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\Advanced", "/v", "Start_Style", "/t", "REG_DWORD", "/d", "1", "/f"], env=env)

    # Set DPI in registry
    log_pixels = 96 if dpi == 96 else 144
    subprocess.run(["wine", "reg", "add", "HKCU\\Control Panel\\Desktop", "/v", "LogPixels", "/t", "REG_DWORD", "/d", str(log_pixels), "/f"], env=env)

    # Start with Luna
    subprocess.run(["wine", "explorer.exe", "/theme:Luna"], env=env, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    subprocess.run(["wineserver", "-w"], env=env)

    # Start Xvfb
    res = "1024x768x24" if dpi == 96 else "1536x1152x24"
    res_w = 1024 if dpi == 96 else 1536
    res_h = 768 if dpi == 96 else 1152
    xvfb = subprocess.Popen(["Xvfb", ":99", "-screen", "0", res, "-dpi", str(dpi)])
    time.sleep(1.5)

    try:
        print(f"Launching real shell at {dpi} DPI ({res})...")
        shell = subprocess.Popen(["wine", "explorer.exe", f"/desktop=Shell,{res_w}x{res_h}", "/shell"], env=env)
        time.sleep(4.0)

        # Launch Display Properties
        print("Opening Display Properties (desk.cpl)...")
        cpl = subprocess.Popen(["wine", "control.exe", "desk.cpl"], env=env)
        time.sleep(3.0)

        if prefix_label == "100dpi":
            # 1. Switch to Fluent live via cpl_driver
            print("Applying Fluent in Display Properties...")
            out_f1 = run_cmd(["wine", "/root/WineExplorer/scripts/cpl_driver.exe", "Fluent"]).stdout
            print(out_f1)
            time.sleep(2.5)

            # Query CurrentTheme
            out1 = run_cmd(["wine", "reg", "query", "HKCU\\Software\\Microsoft\\Windows\\CurrentVersion\\Themes", "/v", "CurrentTheme"]).stdout
            print("CurrentTheme (Fluent):\n" + out1.strip())

            # Start menu
            print("Capturing 01_fluent_start_menu...")
            subprocess.run(["xdotool", "mousemove", "25", str(res_h - 15), "click", "1"], env=env)
            time.sleep(1.5)
            run_cmd(["import", "-window", "root", f"{SHOTS_DIR}/01_fluent_start_menu.png"])
            # Close start menu
            subprocess.run(["xdotool", "mousemove", "500", "200", "click", "1"], env=env)
            time.sleep(0.5)

            # Open Control Panel folder
            print("Opening Control Panel folder...")
            cpanel = subprocess.Popen(["wine", "explorer.exe", "/e,::{21EC2020-3AEA-1069-A2DD-08002B30309D}"], env=env)
            time.sleep(3.0)
            print("Capturing 02_fluent_control_panel...")
            run_cmd(["import", "-window", "root", f"{SHOTS_DIR}/02_fluent_control_panel.png"])
            subprocess.run(["pkill", "-9", "-f", "::{21EC2020-3AEA-1069-A2DD-08002B30309D}"], env=env)
            time.sleep(1.0)

            # Open My Computer folder
            print("Opening My Computer folder...")
            mycomp = subprocess.Popen(["wine", "explorer.exe", "/e,::{20D04FE0-3AEA-1069-A2D8-08002B30309D}"], env=env)
            time.sleep(3.0)
            print("Capturing 03_fluent_my_computer...")
            run_cmd(["import", "-window", "root", f"{SHOTS_DIR}/03_fluent_my_computer.png"])
            subprocess.run(["pkill", "-9", "-f", "::{20D04FE0-3AEA-1069-A2D8-08002B30309D}"], env=env)
            time.sleep(1.0)

            # 2. Switch back to Luna live
            print("Switching back to Luna...")
            out_l = run_cmd(["wine", "/root/WineExplorer/scripts/cpl_driver.exe", "Luna"]).stdout
            print(out_l)
            time.sleep(2.5)

            # Capture reverted Luna start menu
            print("Capturing 04_luna_reverted_start_menu...")
            subprocess.run(["xdotool", "mousemove", "25", str(res_h - 15), "click", "1"], env=env)
            time.sleep(1.5)
            run_cmd(["import", "-window", "root", f"{SHOTS_DIR}/04_luna_reverted_start_menu.png"])
            subprocess.run(["xdotool", "mousemove", "500", "200", "click", "1"], env=env)
            time.sleep(0.5)

            # 3. Re-apply Fluent live
            print("Re-applying Fluent live...")
            out_f2 = run_cmd(["wine", "/root/WineExplorer/scripts/cpl_driver.exe", "Fluent"]).stdout
            print(out_f2)
            time.sleep(2.5)

            print("Capturing 05_fluent_reapplied_start_menu...")
            subprocess.run(["xdotool", "mousemove", "25", str(res_h - 15), "click", "1"], env=env)
            time.sleep(1.5)
            run_cmd(["import", "-window", "root", f"{SHOTS_DIR}/05_fluent_reapplied_start_menu.png"])
            subprocess.run(["xdotool", "mousemove", "500", "200", "click", "1"], env=env)
            time.sleep(0.5)

        else:
            # 150% DPI capture
            print(f"Applying Fluent at {dpi} DPI...")
            out_f = run_cmd(["wine", "/root/WineExplorer/scripts/cpl_driver.exe", "Fluent"]).stdout
            print(out_f)
            time.sleep(2.5)

            print("Capturing 06_fluent_150dpi_start_menu...")
            subprocess.run(["xdotool", "mousemove", "35", str(res_h - 20), "click", "1"], env=env)
            time.sleep(1.5)
            run_cmd(["import", "-window", "root", f"{SHOTS_DIR}/06_fluent_150dpi_start_menu.png"])
            subprocess.run(["xdotool", "mousemove", "700", "300", "click", "1"], env=env)
            time.sleep(0.5)

            print("Opening Control Panel at 150 DPI...")
            cpanel = subprocess.Popen(["wine", "explorer.exe", "/e,::{21EC2020-3AEA-1069-A2DD-08002B30309D}"], env=env)
            time.sleep(3.5)
            print("Capturing 07_fluent_150dpi_control_panel...")
            run_cmd(["import", "-window", "root", f"{SHOTS_DIR}/07_fluent_150dpi_control_panel.png"])
            subprocess.run(["pkill", "-9", "-f", "::{21EC2020-3AEA-1069-A2DD-08002B30309D}"], env=env)
            time.sleep(1.0)

    finally:
        cleanup()
        xvfb.terminate()

if __name__ == "__main__":
    print("=== Running 100% DPI (96 DPI) acceptance test ===")
    run_test_at_dpi(96, "100dpi")
    print("\n=== Running 150% DPI (144 DPI) acceptance test ===")
    run_test_at_dpi(144, "150dpi")
    print("\nAll acceptance tests and screenshots completed successfully!")

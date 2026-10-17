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

cleanup()
time.sleep(1)

# Ensure X11 directory permissions
subprocess.run(["sudo", "mount", "-o", "remount,rw", "/tmp/.X11-unix"], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
subprocess.run(["sudo", "chmod", "1777", "/tmp/.X11-unix"], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)

# Lock taskbar to 1 row at bottom
subprocess.run(["wine", "reg", "add", "HKCU\\Software\\WineExplorer\\Taskbar", "/v", "LockTaskbar", "/t", "REG_DWORD", "/d", "1", "/f"], env=env)
subprocess.run(["wine", "reg", "add", "HKCU\\Software\\WineExplorer\\Taskbar", "/v", "Rows", "/t", "REG_DWORD", "/d", "1", "/f"], env=env)
# XP Two-Column Start Menu (1)
subprocess.run(["wine", "reg", "add", "HKCU\\Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\Advanced", "/v", "Start_Style", "/t", "REG_DWORD", "/d", "1", "/f"], env=env)

# Initialize with Luna
subprocess.run(["wine", "explorer.exe", "/theme:Luna"], env=env, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
subprocess.run(["wineserver", "-w"], env=env)

# Start Xvfb
xvfb = subprocess.Popen(["Xvfb", ":99", "-screen", "0", "1024x768x24", "-dpi", "96"])
time.sleep(1.5)

try:
    print("Launching real shell...")
    shell = subprocess.Popen(["wine", "explorer.exe", "/desktop=Shell,1024x768", "/shell"], env=env)
    time.sleep(4.0)

    # Launch Display Properties
    print("Opening Display Properties (desk.cpl)...")
    cpl = subprocess.Popen(["wine", "control.exe", "desk.cpl"], env=env)
    time.sleep(3.0)

    # Capture initial Luna state with Display Properties open
    print("Capturing 01_initial_luna...")
    # Open Start menu to show Luna icons
    subprocess.run(["xdotool", "mousemove", "25", "755", "click", "1"], env=env)
    time.sleep(1.2)
    run_cmd(["import", "-window", "root", f"{SHOTS_DIR}/01_initial_luna.png"])
    # Dismiss menu by clicking inside Display Properties title bar (500, 180)
    subprocess.run(["xdotool", "mousemove", "500", "180", "click", "1"], env=env)
    time.sleep(0.5)

    # 1. Switch to Fluent live via cpl_driver
    print("Applying Fluent in Display Properties via cpl_driver...")
    out_f1 = run_cmd(["wine", "/root/WineExplorer/scripts/cpl_driver.exe", "Fluent"]).stdout
    print(out_f1)
    time.sleep(2.5)

    # Verify CurrentTheme in registry
    out1 = run_cmd(["wine", "reg", "query", "HKCU\\Software\\Microsoft\\Windows\\CurrentVersion\\Themes", "/v", "CurrentTheme"]).stdout
    print("CurrentTheme (Fluent):\n" + out1.strip())

    # Open Start menu to capture Fluent icons
    print("Capturing 02_fluent_applied...")
    subprocess.run(["xdotool", "mousemove", "25", "755", "click", "1"], env=env)
    time.sleep(1.2)
    run_cmd(["import", "-window", "root", f"{SHOTS_DIR}/02_fluent_applied.png"])
    subprocess.run(["xdotool", "mousemove", "500", "180", "click", "1"], env=env)
    time.sleep(0.5)

    # 2. Switch back to Luna live via cpl_driver
    print("Applying Luna in Display Properties via cpl_driver...")
    out_l = run_cmd(["wine", "/root/WineExplorer/scripts/cpl_driver.exe", "Luna"]).stdout
    print(out_l)
    time.sleep(2.5)

    # Verify CurrentTheme in registry
    out2 = run_cmd(["wine", "reg", "query", "HKCU\\Software\\Microsoft\\Windows\\CurrentVersion\\Themes", "/v", "CurrentTheme"]).stdout
    print("CurrentTheme (Luna):\n" + out2.strip())

    # Open Start menu to capture reverted Luna icons
    print("Capturing 03_luna_reverted...")
    subprocess.run(["xdotool", "mousemove", "25", "755", "click", "1"], env=env)
    time.sleep(1.2)
    run_cmd(["import", "-window", "root", f"{SHOTS_DIR}/03_luna_reverted.png"])
    subprocess.run(["xdotool", "mousemove", "500", "180", "click", "1"], env=env)
    time.sleep(0.5)

    # 3. Switch back to Fluent live via cpl_driver
    print("Re-applying Fluent in Display Properties via cpl_driver...")
    out_f2 = run_cmd(["wine", "/root/WineExplorer/scripts/cpl_driver.exe", "Fluent"]).stdout
    print(out_f2)
    time.sleep(2.5)

    # Verify CurrentTheme in registry
    out3 = run_cmd(["wine", "reg", "query", "HKCU\\Software\\Microsoft\\Windows\\CurrentVersion\\Themes", "/v", "CurrentTheme"]).stdout
    print("CurrentTheme (Fluent reapplied):\n" + out3.strip())

    # Open Start menu to capture re-applied Fluent icons
    print("Capturing 04_fluent_reapplied...")
    subprocess.run(["xdotool", "mousemove", "25", "755", "click", "1"], env=env)
    time.sleep(1.2)
    run_cmd(["import", "-window", "root", f"{SHOTS_DIR}/04_fluent_reapplied.png"])
    subprocess.run(["xdotool", "mousemove", "500", "180", "click", "1"], env=env)
    time.sleep(0.5)

finally:
    print("Cleaning up...")
    cleanup()
    xvfb.terminate()

print("Phase T1 test completed successfully!")

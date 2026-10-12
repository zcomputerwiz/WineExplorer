#!/usr/bin/env python3
import os
import subprocess
import time
import sys

VB = r"C:\Program Files\Oracle\VirtualBox\VBoxManage.exe"
VM_NAME = "WineExplorer-Install-Test"
EVIDENCE_DIR = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "test-evidence", "F8")
os.makedirs(EVIDENCE_DIR, exist_ok=True)

def vb(*args):
    return subprocess.run([VB, *args], check=True, capture_output=True, text=True)

def vb_silent(*args):
    return subprocess.run([VB, *args], capture_output=True, text=True)

def take_shot(filename):
    path = os.path.join(EVIDENCE_DIR, filename)
    vb("controlvm", VM_NAME, "screenshotpng", path)
    print(f"[SHOT] Saved {filename}")
    return path

def main():
    print("=== Starting WineExplorer-Install-Test headless (Zero-touch unattended test) ===")
    vb("startvm", VM_NAME, "--type", "headless")

    # Monitor progression through Stages 1, 2, 3, and Desktop
    start_time = time.time()
    last_shot = 0
    shot_count = 1

    print("Monitoring VM execution without pressing any keys...")
    
    # We will loop for up to 15 minutes, taking shots every 15-20 seconds or upon stage changes
    stage = "booting"
    
    while time.time() - start_time < 900: # 15 min max
        time.sleep(15)
        elapsed = int(time.time() - start_time)
        shot_name = f"f8_progress_{elapsed:03d}s.png"
        take_shot(shot_name)

        # Check VM status
        res = vb_silent("showvminfo", VM_NAME, "--machinereadable")
        if 'VMState="running"' not in res.stdout:
            print(f"VM state changed: {res.stdout}")
            break

    print("Monitoring loop finished. Taking final screenshot...")
    take_shot("f8_final_state.png")

if __name__ == "__main__":
    main()

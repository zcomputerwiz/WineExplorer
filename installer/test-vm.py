#!/usr/bin/env python3
"""Manage and drive the WineExplorer-Install-Test VirtualBox VM.

Usage:
  test-vm.py create [bios|efi] [iso_path]
  test-vm.py start [--gui]
  test-vm.py stop
  test-vm.py destroy
  test-vm.py shot [output_png]
  test-vm.py key [enter|f8|f3|c|d|esc|up|down|left|right|tab]
  test-vm.py type <string>
"""

import os
import subprocess
import sys
import time

VB = r"C:\Program Files\Oracle\VirtualBox\VBoxManage.exe"
VM_NAME = "WineExplorer-Install-Test"
DEFAULT_DISK = os.path.join(os.path.dirname(os.path.abspath(__file__)), "test_disk.vdi")

SCANCODES = {
    "enter": ["1c", "9c"],
    "esc": ["01", "81"],
    "f3": ["3d", "bd"],
    "f8": ["42", "c2"],
    "c": ["2e", "ae"],
    "d": ["20", "a0"],
    "r": ["13", "93"],
    "tab": ["0f", "8f"],
    "up": ["e0", "48", "e0", "c8"],
    "down": ["e0", "50", "e0", "d0"],
    "left": ["e0", "4b", "e0", "cb"],
    "right": ["e0", "4d", "e0", "cd"],
    "space": ["39", "b9"],
    "pageup": ["e0", "49", "e0", "c9"],
    "pagedown": ["e0", "51", "e0", "d1"],
}

def vb(*args):
    return subprocess.run([VB, *args], check=True, capture_output=True, text=True)

def vb_silent(*args):
    return subprocess.run([VB, *args], capture_output=True, text=True)

def destroy():
    print(f"Destroying {VM_NAME}...")
    vb_silent("controlvm", VM_NAME, "poweroff")
    time.sleep(1)
    vb_silent("unregistervm", VM_NAME, "--delete")
    if os.path.exists(DEFAULT_DISK):
        try:
            os.remove(DEFAULT_DISK)
        except OSError:
            pass

def create(firmware="bios", iso_path=None):
    destroy()
    if not iso_path:
        iso_path = os.path.join(os.path.dirname(os.path.abspath(__file__)), "wineexplorer.iso")
    if not os.path.exists(iso_path):
        raise FileNotFoundError(f"ISO not found: {iso_path}")

    print(f"Creating {VM_NAME} (firmware: {firmware})...")
    vb("createvm", "--name", VM_NAME, "--ostype", "Debian_64", "--register")
    vb("modifyvm", VM_NAME, "--memory", "4096", "--cpus", "2", "--vram", "64")
    vb("modifyvm", VM_NAME, "--graphicscontroller", "vmsvga")
    vb("modifyvm", VM_NAME, "--nic1", "nat")

    if firmware.lower() == "efi":
        vb("modifyvm", VM_NAME, "--firmware", "efi")
    else:
        vb("modifyvm", VM_NAME, "--firmware", "bios")

    # Create 20 GB disk
    print(f"Creating 20 GB disk at {DEFAULT_DISK}...")
    vb("createhd", "--filename", DEFAULT_DISK, "--size", "20480")

    # Add storage controllers
    vb("storagectl", VM_NAME, "--name", "SATA", "--add", "sata", "--controller", "IntelAhci")
    vb("storageattach", VM_NAME, "--storagectl", "SATA", "--port", "0", "--device", "0",
       "--type", "hdd", "--medium", DEFAULT_DISK)

    vb("storagectl", VM_NAME, "--name", "IDE", "--add", "ide")
    vb("storageattach", VM_NAME, "--storagectl", "IDE", "--port", "0", "--device", "0",
       "--type", "dvddrive", "--medium", os.path.abspath(iso_path))

    vb("modifyvm", VM_NAME, "--boot1", "dvd", "--boot2", "disk", "--boot3", "none")
    print(f"{VM_NAME} ready.")

def start(gui=False):
    mode = "gui" if gui else "headless"
    print(f"Starting {VM_NAME} ({mode})...")
    vb("startvm", VM_NAME, "--type", mode)

def stop():
    print(f"Stopping {VM_NAME}...")
    vb_silent("controlvm", VM_NAME, "poweroff")

def shot(out_path=None):
    if not out_path:
        out_path = os.path.join(os.path.dirname(os.path.abspath(__file__)), "test_shot.png")
    vb("controlvm", VM_NAME, "screenshotpng", os.path.abspath(out_path))
    print(f"Saved screenshot: {out_path}")
    return out_path

def key(k):
    k = k.lower()
    if k not in SCANCODES:
        raise ValueError(f"Unknown key: {k}")
    vb("controlvm", VM_NAME, "keyboardputscancode", *SCANCODES[k])

def type_string(s):
    vb("controlvm", VM_NAME, "keyboardputstring", s)

if __name__ == "__main__":
    if len(sys.argv) < 2:
        print(__doc__)
        sys.exit(1)

    cmd = sys.argv[1].lower()
    if cmd == "create":
        fw = sys.argv[2] if len(sys.argv) > 2 else "bios"
        iso = sys.argv[3] if len(sys.argv) > 3 else None
        create(fw, iso)
    elif cmd == "start":
        start("--gui" in sys.argv)
    elif cmd == "stop":
        stop()
    elif cmd == "destroy":
        destroy()
    elif cmd == "shot":
        out = sys.argv[2] if len(sys.argv) > 2 else None
        shot(out)
    elif cmd == "key":
        if len(sys.argv) < 3:
            print("Specify key name.")
            sys.exit(1)
        key(sys.argv[2])
    elif cmd == "type":
        if len(sys.argv) < 3:
            print("Specify string.")
            sys.exit(1)
        type_string(sys.argv[2])
    else:
        print(f"Unknown command: {cmd}")
        sys.exit(1)

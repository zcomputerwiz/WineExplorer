#!/usr/bin/env python3
"""Type a command into the Windows XP VM's Run box (Win+R) through VBoxManage.
Use {B} for a backslash (shells and tooling mangle them): xp_run.py 'cmd /c copy C:{B}a.txt {B}{B}vboxsvr{B}x'"""
import subprocess, sys, time
VB = r"C:\Program Files\Oracle\VirtualBox\VBoxManage.exe"
VM = "Windows XP Pro"
def vb(*a): subprocess.run([VB, "controlvm", VM, *a], check=True)
cmd = sys.argv[1].replace("{B}", chr(92))
vb("keyboardputscancode", "e0", "5b", "13", "93", "e0", "db")
time.sleep(1.5)
vb("keyboardputstring", cmd)
time.sleep(0.5)
vb("keyboardputscancode", "1c", "9c")

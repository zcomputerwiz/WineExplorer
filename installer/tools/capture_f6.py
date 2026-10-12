#!/usr/bin/env python3
import os
import subprocess
import time

env = dict(os.environ)
env["WINEPREFIX"] = "/home/wineuser/prefix_time"

p = subprocess.Popen(["wine", "setup.exe"], cwd="/home/wineuser", env=env)
time.sleep(3)

# Page 1: Regional
subprocess.run(["import", "-window", "root", "/tmp/f6_page1_regional.png"])
subprocess.run(["xdotool", "key", "alt+n"])
time.sleep(1)

# Page 2: UserInfo
subprocess.run(["import", "-window", "root", "/tmp/f6_page2_userinfo.png"])
subprocess.run(["xdotool", "key", "alt+n"])
time.sleep(1)

# Page 3: Computer Name & Admin Password
subprocess.run(["import", "-window", "root", "/tmp/f6_page3_computer.png"])
subprocess.run(["xdotool", "key", "alt+n"])
time.sleep(1)

# Page 4: Date & Time
subprocess.run(["import", "-window", "root", "/tmp/f6_page4_datetime.png"])
subprocess.run(["xdotool", "key", "alt+n"])
time.sleep(1)

# Page 5: Network
subprocess.run(["import", "-window", "root", "/tmp/f6_page5_network.png"])
subprocess.run(["xdotool", "key", "alt+n"])
time.sleep(1)

# Page 6: Workgroup
subprocess.run(["import", "-window", "root", "/tmp/f6_page6_workgroup.png"])
subprocess.run(["xdotool", "key", "alt+f"])
time.sleep(2)

# Page 7: Installing steps
subprocess.run(["import", "-window", "root", "/tmp/f6_page7_installing.png"])

time.sleep(3)
p.terminate()
subprocess.run(["wineserver", "-k"])
print("CAPTURED_ALL_PAGES")

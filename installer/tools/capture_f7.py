#!/usr/bin/env python3
import os
import subprocess
import time

env = dict(os.environ)
env["WINEPREFIX"] = "/home/wineuser/prefix_time"

p = subprocess.Popen(["wine", "oobe.exe"], cwd="/home/wineuser", env=env)
time.sleep(3)

# Page 1: Welcome
subprocess.run(["import", "-window", "root", "/tmp/f7_page1_welcome.png"])
subprocess.run(["xdotool", "key", "Return"])
time.sleep(1)

# Page 2: Updates
subprocess.run(["import", "-window", "root", "/tmp/f7_page2_updates.png"])
subprocess.run(["xdotool", "key", "Return"])
time.sleep(1)

# Page 3: Connectivity
subprocess.run(["import", "-window", "root", "/tmp/f7_page3_connectivity.png"])
subprocess.run(["xdotool", "key", "Return"])
time.sleep(1)

# Page 4: Users - Type Alice and Bob
subprocess.run(["xdotool", "type", "alice"])
subprocess.run(["xdotool", "key", "Tab"])
subprocess.run(["xdotool", "type", "bob"])
time.sleep(1)
subprocess.run(["import", "-window", "root", "/tmp/f7_page4_users.png"])
subprocess.run(["xdotool", "key", "Return"])
time.sleep(1)

# Page 5: Password - Type password for Alice
subprocess.run(["xdotool", "type", "Password123!"])
subprocess.run(["xdotool", "key", "Tab"])
subprocess.run(["xdotool", "type", "Password123!"])
time.sleep(1)
subprocess.run(["import", "-window", "root", "/tmp/f7_page5_password.png"])
subprocess.run(["xdotool", "key", "Return"])
time.sleep(1)

# Page 6: Finish
subprocess.run(["import", "-window", "root", "/tmp/f7_page6_finish.png"])

time.sleep(2)
p.terminate()
subprocess.run(["wineserver", "-k"])
print("CAPTURED_ALL_OOBE_PAGES")

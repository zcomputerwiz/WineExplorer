#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright 2026 WineExplorer contributors
# run the dcomp test while grabbing the X screen every 50 ms; report green/yellow 100x100 surface blocks seen (green then yellow)
cd /root/WE-prefetch/build/wow64
pkill -9 -x wineserver; pkill -9 Xvfb; sleep 1
export DISPLAY=:79 WINEPREFIX=/root/dc-pfx WINEDLLOVERRIDES="mscoree,mshtml="
Xvfb :79 -screen 0 1024x768x24 >/dev/null 2>&1 &
sleep 2
[ -d $WINEPREFIX ] || timeout 150 ./wine wineboot -i >/dev/null 2>&1
rm -rf /root/grab; mkdir /root/grab
( for i in $(seq 1 400); do xwd -root -silent -display :79 2>/dev/null | xwdtopnm 2>/dev/null > /root/grab/$i.ppm; sleep 0.05; done ) &
G=$!
WINEDEBUG=-all timeout 90 ./wine dlls/dcomp/tests/x86_64-windows/dcomp_test.exe dcomp 2>&1 | grep -aE "tests executed"
kill $G 2>/dev/null
python3 - <<'PY'
import glob
from PIL import Image
found={}
for f in sorted(glob.glob('/root/grab/*.ppm'), key=lambda p:int(p.split('/')[-1][:-4])):
    try: im=Image.open(f).convert('RGB')
    except Exception: continue
    small=im.resize((im.width//10, im.height//10))
    px=small.load()
    for name,c in (('green',(0,255,0)),('yellow',(255,255,0))):
        if name in found: continue
        for y in range(small.height):
            for x in range(small.width):
                p=px[x,y]
                if all(abs(p[k]-c[k])<20 for k in range(3)):
                    found[name]=(f,x*10,y*10); break
            if name in found: break
print(found)
PY
./server/wineserver -k; pkill -9 Xvfb

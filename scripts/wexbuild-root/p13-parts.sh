#!/bin/bash
# p13-parts.sh NAME WINPATH: dump Window/DWMWindow parts of one style inside the shell
cd /root
tr -d '\r' < /mnt/d/DeepThought/Projects/WineExplorer/scripts/theme-parts-probe.c > theme-parts-probe.c
x86_64-w64-mingw32-gcc -municode -o theme-parts-probe.exe theme-parts-probe.c -luxtheme -lgdi32 -luser32 || exit 1
rm -f /root/parts.bmp
PROBE=theme-parts-probe PARGS="$2" timeout 300 bash /root/p4-visual.sh on 2>&1 | grep -a -E "^(Window|DWMWindow|  part)" | tr '\n' ' ' | cut -c1-2500
echo
convert /root/parts.bmp /mnt/d/DeepThought/ChromiumLab/p13-parts-$1.png

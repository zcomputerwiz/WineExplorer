#!/bin/bash
# p14-measure.sh [NAME...]: compositor frame cost; each scenario sets RES, gates and the probe load (fast | move)
cd /root
tr -d '\r' < /mnt/d/DeepThought/Projects/WineExplorer/scripts/dwm-blur-visual-probe.c > dwm-blur-visual-probe.c
x86_64-w64-mingw32-gcc -municode -mwindows -o dwm-blur-visual-probe.exe dwm-blur-visual-probe.c -luser32 -lgdi32 -ldwmapi || exit 1
run() {
    echo "== $1 ($RES $LOAD)"
    PROBE=dwm-blur-visual-probe PARGS=$LOAD timeout 300 bash /root/p4-visual.sh on >/dev/null 2>&1
    grep -a "frames .* average" /root/p4-wine.log | tail -4 | sed 's/^.*frame_stats //'
    cp /root/p4-on.png /mnt/d/DeepThought/ChromiumLab/p14-$1.png
}
export DA=1 FX=1 MAT=1 FS=1 COL=1802811644
for s in ${@:-move1080 move4k}; do
    case $s in
    fast768)  RES=1024x768  LOAD=fast run $s ;;
    fast1080) RES=1920x1080 LOAD=fast run $s ;;
    move768)  RES=1024x768  LOAD=move run $s ;;
    move1080) RES=1920x1080 LOAD=move run $s ;;
    move4k)   RES=3840x2160 LOAD=move run $s ;;
    esac
done

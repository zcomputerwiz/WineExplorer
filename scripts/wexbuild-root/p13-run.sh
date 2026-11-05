#!/bin/bash
# p13-run.sh NAME WINPATH [extra env already exported]: gallery probe under one style, screenshot to ChromiumLab
cd /root
export PROBE=theme-gallery-probe PARGS="$2"
timeout 300 bash /root/p4-visual.sh on 2>&1 | grep -a -E "^open |^apply|^current|^switch|err:uxtheme|err:theme" | head -8
cp /root/p4-on.png /mnt/d/DeepThought/ChromiumLab/p13-$1.png

#!/bin/bash
# p13-trace.sh: parts probe on aeroblue with uxtheme traces; show what drawing DWMWindow part 7 does
export WINEDEBUG=err+all,trace+uxtheme
bash /root/p13-parts.sh aeroblue 'Z:\root\themes-local\aeroblue\aeroblue.msstyles' >/dev/null 2>&1
grep -a -n "DWMWindow\|LoadBitmap\|STREAM\|#[0-9]\|atlas\|Atlas" /root/p4-wine.log | grep -v "IsThemePartDefined" | head -60

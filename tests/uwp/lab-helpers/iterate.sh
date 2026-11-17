#!/bin/bash
# host (Git Bash): one quick Calculator iteration: build XamlShim incrementally (unless --no-build), push the changed
# files, run calc-quick.sh on a copy of the lab's base prefix and print its summary.
#   iterate.sh [--no-build] [WINEDEBUG]        env passes through: QUIET MIN MAX, and for new classes of a changed
#                                              Wine module REGCLASSES="dll=Class,Class ..." (WinRT) or REGISTER="dll ..."
#   iterate.sh --prep XAMLSHIM_ZIP             make the base prefix (calc-prep.sh) from a release zip
# The fresh, full run stays calc-run.sh: run it before a commit or a release and after Wine changes.
set -uo pipefail
pad=/c/Users/Owner/AppData/Local/Temp/claude/f10dd599a76cdd8a/cc6dd05f-7e73-49f2-be6b-aa4119777252/scratchpad
cd "$pad"
for f in calc-quick.sh calc-prep.sh quick-reg.py calc-run.sh; do ./lab.sh push $f > /dev/null; done
./lab.sh ssh 'chmod +x /home/debian/uwp-u1-20261111/claude/calc-quick.sh /home/debian/uwp-u1-20261111/claude/calc-prep.sh /home/debian/uwp-u1-20261111/claude/calc-run.sh' > /dev/null
if [ "${1:-}" = --prep ]; then
  zip=${2:?usage: iterate.sh --prep XAMLSHIM_ZIP}
  ./lab.sh push "$zip" > /dev/null
  echo "$zip" > quick-base.zipname
  ./lab.sh wait "cd /home/debian/uwp-u1-20261111/claude && ./calc-prep.sh $zip" 900
  exit
fi
base_zip=$(cat quick-base.zipname 2>/dev/null) || { echo "no base: iterate.sh --prep XAMLSHIM_ZIP"; exit 1; }
t0=$(date +%s)
if [ "${1:-}" = --no-build ]; then shift
else
  powershell.exe -NoProfile -ExecutionPolicy Bypass -File "$(cygpath -w "$pad/shim-quick.ps1")" -BaseZip "$base_zip" || exit 1
fi
t1=$(date +%s)
./lab.sh push quick.tar > /dev/null
env="QUIET=${QUIET:-15} MIN=${MIN:-15} MAX=${MAX:-120} REGISTER=\"${REGISTER:-}\" REGCLASSES=\"${REGCLASSES:-}\""
env="$env XAMLSHIM_PROBE_MEMBERS=\"${XAMLSHIM_PROBE_MEMBERS:-}\" XAMLSHIM_PROBE_TYPE=\"${XAMLSHIM_PROBE_TYPE:-}\" XAMLSHIM_RENDER=\"${XAMLSHIM_RENDER:-}\" XAMLSHIM_LATE_TREE=\"${XAMLSHIM_LATE_TREE:-}\" XAMLSHIM_TPL=\"${XAMLSHIM_TPL:-}\""
env="$env INPUT_MORE=\"${INPUT_MORE:-}\" INPUT=\"${INPUT:-}\" WINE_ROERR_STACK=${WINE_ROERR_STACK:-}"
[ -z "${WINE_ROERR_STACK:-}" ] && env="${env% WINE_ROERR_STACK=}"
./lab.sh wait "cd /home/debian/uwp-u1-20261111/claude && DISPLAY=:99 $env ./calc-quick.sh ${1:-err+all,fixme+all}" 400
# the window's pixels as the shim read them back (calc-window.png here)
rm -f calc-window.png
./lab.sh ssh 'f=/home/debian/uwp-u1-20261111/build/candidates/u11-2/quick-run/window.png; [ -f $f ] && base64 -w0 $f; true' |
  tr -d '\r\n' | base64 -d > calc-window.png 2>/dev/null
[ -s calc-window.png ] && echo "window readback: $pad/calc-window.png" || rm -f calc-window.png
# with INPUT: the numbered readbacks side by side
rm -f calc-window-all.png
./lab.sh ssh 'f=/home/debian/uwp-u1-20261111/build/candidates/u11-2/quick-run/window-all.png; [ -f $f ] && convert $f -resize 2400x /tmp/wa.png && base64 -w0 /tmp/wa.png; true' |
  tr -d '\r\n' | base64 -d > calc-window-all.png 2>/dev/null
[ -s calc-window-all.png ] && echo "readbacks: $pad/calc-window-all.png" || rm -f calc-window-all.png
echo "host: build $((t1 - t0)) s, iteration $(( $(date +%s) - t0 )) s"

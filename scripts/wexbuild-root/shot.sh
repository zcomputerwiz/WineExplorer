# usage: shot.sh <wine build dir> <prefix> <out dir> [theme names...]
# Themes are files in /root/themes-local (local-only copies, never committed).
set -u
B=$1
export WINEPREFIX=$2
OUT=$3
shift 3
THEMES=${*:-wine lunar luna win11 win10}
D=:23
export DISPLAY=$D WINEDLLOVERRIDES="mscoree,mshtml=;winemenubuilder.exe=d" WINEDEBUG=-all
mkdir -p "$OUT"
pkill -f "Xvfb $D" 2>/dev/null; sleep 1
setsid Xvfb $D -screen 0 800x600x24 >/dev/null 2>&1 < /dev/null &
sleep 2
if [ ! -d "$WINEPREFIX" ]; then
  $B/wine wineboot -i > /dev/null 2>&1
  $B/server/wineserver -w
fi
mkdir -p "$WINEPREFIX/drive_c/themes"
cp /root/themes-local/*.msstyles "$WINEPREFIX/drive_c/themes/"
cp /root/ctrls/ctrls32.exe /root/ctrls/ctrls64.exe "$WINEPREFIX/drive_c/themes/"
K='HKCU\Software\Microsoft\Windows\CurrentVersion\ThemeManager'
for t in $THEMES; do
  if [ "$t" = classic ]; then
    $B/wine reg add "$K" /v ThemeActive /d 0 /f > /dev/null 2>&1
  else
    $B/wine reg add "$K" /v ThemeActive /d 1 /f > /dev/null 2>&1
    $B/wine reg add "$K" /v DllName /d "C:\\themes\\$t.msstyles" /f > /dev/null 2>&1
    $B/wine reg add "$K" /v ColorName /d "" /f > /dev/null 2>&1
    $B/wine reg add "$K" /v SizeName /d "" /f > /dev/null 2>&1
  fi
  $B/server/wineserver -w
  for a in 32 64; do
    $B/wine ${DESK:+explorer /desktop=shot,800x600} "C:\\themes\\ctrls$a.exe" > "$OUT/$t-$a.log" 2>&1 &
    sleep 6
    import -window root "$OUT/$t-$a.png"
    $B/server/wineserver -k
    sleep 1
  done
done
pkill -f "Xvfb $D"
ls -la "$OUT"

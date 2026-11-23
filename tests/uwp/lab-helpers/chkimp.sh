# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright 2026 WineExplorer contributors
#!/bin/bash
# lab: which imports of a PE file are missing from the exports of the Wine build: chkimp.sh PE_FILE
B=/home/debian/uwp-u1-20261111/build/candidates/u12/build
f="$1"
objdump=x86_64-w64-mingw32-objdump
$objdump -p "$f" | awk '
/DLL Name:/ { dll = tolower($3); next }
/^[ \t]+[0-9a-f]+[ \t]+<none>[ \t]+[0-9a-f]+[ \t]+/ { print dll, $4 }
/^[ \t]+[0-9a-f]+[ \t]+[0-9]+[ \t]+<none>/ { print dll, "#" $2 }
' | sort -u > /tmp/imps.txt
dlls=$(awk '{print $1}' /tmp/imps.txt | sort -u)
for d in $dlls; do
  case "$d" in api-ms-*|ext-ms-*) continue;; esac
  base=${d%.dll}
  p=$(ls $B/dlls/$base/x86_64-windows/$d 2>/dev/null | head -1)
  [ -z "$p" ] && p=$(ls $B/dlls/*/x86_64-windows/$d 2>/dev/null | head -1)
  if [ -z "$p" ]; then
    if [ -f "$(dirname "$f")/$d" ]; then continue; fi
    echo "NO-DLL $d ($(grep -c "^$d " /tmp/imps.txt) imports)"; continue
  fi
  $objdump -p "$p" | awk '/^\t *\[ *[0-9]+\] /{print $3} /^\t *\[ *[0-9]+\]/{}' > /tmp/exp-$base.txt
  $objdump -p "$p" | sed -n '/\[Ordinal\/Name Pointer\] Table/,$p' | awk '/^\t\[/{print $NF}' >> /tmp/exp-$base.txt
  $objdump -p "$p" | awk '/^\t *[0-9]+\t *[0-9a-f]+ /{print "#" $1; print $3}' >> /tmp/exp-$base.txt
  grep "^$d " /tmp/imps.txt | while read dd n; do
    grep -qx -- "$n" /tmp/exp-$base.txt || echo "MISSING $d $n"
  done
done

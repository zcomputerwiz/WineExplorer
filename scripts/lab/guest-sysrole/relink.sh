# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright 2026 WineExplorer contributors
M=/var/lib/wineexplorer/machine
n=0
while IFS= read -r l; do
  t=$(readlink "$l"); nt=${t/\/opt\/wex-rt\//\/opt\/wex-rt2\/}
  if [ "$t" != "$nt" ] && [ -e "$nt" ]; then ln -sfn "$nt" "$l"; n=$((n+1)); fi
done < <(find $M/drive_c -type l -lname '/opt/wex-rt/*')
echo "relinked $n"; find $M/drive_c -type l -lname '/opt/wex-rt/*' | wc -l
pkill -x wineserver; sleep 2
rm -f $M/drive_c/windows/Installer/*.msi
bash /tmp/gsr/guest-elev-msi.sh googlechromestandaloneenterprise64.msi chrome > /tmp/gsr-out/elev-run-chrome.txt 2>&1
grep -a -E "exit code|runas" /tmp/gsr-out/elev.log | tail -2
grep -a -E "err:" /tmp/gsr-out/sdhold.log | grep -a -v -E "libEGL|winediag|ZwLoadDriver|systray|menubuilder" | cut -c1-200 | tail -4
F=/tmp/gsr-out/sdhold.log
echo "wexdbg total $(grep -a -c wexdbg $F)"; grep -a "wexdbg" $F | grep -a -v "user=S-1-5-21" | cut -c1-200 | sort | uniq -c | sort -rn | head -8

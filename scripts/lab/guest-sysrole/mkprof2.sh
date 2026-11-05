# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright 2026 WineExplorer contributors
M=/var/lib/wineexplorer/machine
for u in wexadm wextest; do
  mkdir -p $M/drive_c/users/$u/AppData/Local/Temp $M/drive_c/users/$u/AppData/Roaming
  chown -R $u:wexsys $M/drive_c/users/$u; chmod -R 2775 $M/drive_c/users/$u
done
ls -la $M/drive_c/users
python3 -I /tmp/gsr/guest-env-debug.py 'err+all,fixme+service,fixme+msi'
rm -f $M/drive_c/windows/Installer/*.msi
bash /tmp/gsr/guest-elev-msi.sh googlechromestandaloneenterprise64.msi chrome > /tmp/gsr-out/elev-run-chrome.txt 2>&1
grep -a -E "exit code" /tmp/gsr-out/elev.log
ls "$M/drive_c/Program Files (x86)/Google" "$M/drive_c/Program Files/Google" 2>&1 | head -4
grep -a -E "err:" /tmp/gsr-out/sdhold.log | grep -a -v -E "libEGL|winediag|ZwLoadDriver|systray|menubuilder" | cut -c1-230 | tail -8

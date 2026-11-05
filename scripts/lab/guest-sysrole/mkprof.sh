# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright 2026 WineExplorer contributors
for u in wexadm wextest; do
  d=/home/$u/.wine/drive_c/users/$u
  runuser -u $u -- mkdir -p $d/AppData/Local/Temp $d/AppData/Roaming $d/Desktop $d/Documents
  ls /home/$u/.wine/drive_c/users/
done

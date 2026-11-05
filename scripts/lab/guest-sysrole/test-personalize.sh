#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright 2026 WineExplorer contributors
# Self-test of scripts/personalize-prefix.sh with registry text that has two backslashes per separator.
set -eu
here=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
helper="$here/../../personalize-prefix.sh"
T=$(mktemp -d); trap 'rm -rf "$T"' EXIT
python3 -I - "$T" <<'PY'
import sys
bs = chr(92) * 2
t = sys.argv[1]
text = ('[Software' + bs + 'Wine' + bs + 'Fake] 1\n"Desktop"="C:' + bs + 'users' + bs + 'wexsetup' + bs + 'Desktop"\n'
        '"P"="C:' + bs + 'users' + bs + 'wexsetup"\n')
for name in ('user.reg', 'userdef.reg'):
    open(t + '/' + name, 'w').write(text)
PY
bash "$helper" "$T" bob
if grep -q wexsetup "$T/user.reg" "$T/userdef.reg"; then echo "FAIL: template name still present"; cat "$T/user.reg"; exit 1; fi
grep -q 'users\\\\bob\\\\Desktop' "$T/user.reg" || { echo "FAIL: user path missing"; cat "$T/user.reg"; exit 1; }
echo "PASS: personalize-prefix.sh"

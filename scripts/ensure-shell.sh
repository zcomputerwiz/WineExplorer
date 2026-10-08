#!/bin/bash
# restart the shell in the VM until the desktop is up (the first session after an install and a restart soon after another one
# often end in the login screen)
cd "$(dirname "$0")/.."
for try in 1 2 3; do
    scripts/vm.sh restart-shell >/dev/null 2>&1
    sleep 105
    scripts/vm.sh shot "$PWD/.ensure.png" >/dev/null 2>&1
    if python - <<'PY'
from PIL import Image
im = Image.open('.ensure.png').convert('RGB')
# the greeter and the black screen have a dark background and no taskbar; the shell has a taskbar row that is not dark
px = im.getpixel((640, im.size[1] - 4))
import sys
sys.exit(0 if sum(px) > 300 else 1)
PY
    then rm -f .ensure.png; echo "shell is up"; exit 0; fi
done
echo "shell did not come up"; exit 1

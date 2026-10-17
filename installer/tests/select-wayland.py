#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright 2026 WineExplorer contributors
"""Choose the optional session through the installed Welcome screen."""
from pathlib import Path
import sys
import time

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "scripts"))
import vmtest as vm

name = sys.argv[1]
width, height = vm.screen_size(name)
vm.click(name, 140, height - 129)
vm.keys(name, ["home", "down", "ret"])
# Remove the combo's blue selection highlight before reading its label.
vm.click(name, 400, height - 180)
deadline = time.monotonic() + 15
while time.monotonic() < deadline:
    if "wayland" in vm.ocr(name).lower() or "wayland" in vm.ocr(name, white_text=True).lower():
        break
else:
    raise SystemExit("Welcome screen did not show the selected Wayland session")

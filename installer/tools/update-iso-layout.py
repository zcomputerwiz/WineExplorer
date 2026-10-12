#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Regenerate setup tables from the ISO's mounted, read-only squashfs payload.

Usage (root): update-iso-layout.py INPUT_ISO MOUNTED_PAYLOAD OUTPUT_ISO
Only layout.inf and txtsetup.sif are replaced; boot metadata and squashfs stay
unchanged. Future clean builds generate these tables from squashfs directly.
"""
import os
from pathlib import Path
import subprocess
import sys
import tempfile

source, payload, destination = (Path(value).resolve() for value in sys.argv[1:4])
if os.geteuid() != 0:
    raise SystemExit("Root is required to scan protected payload directories")
if destination.exists():
    raise SystemExit(f"Refusing to append to existing ISO: {destination}")
installer = Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory(prefix="wex-iso-layout-") as directory:
    subprocess.run([sys.executable, str(installer / "tools/mklayout.py"), str(payload),
                    directory, str(installer / "setupinfs/txtsetup.sif.in")], check=True)
    subprocess.run(["xorriso", "-indev", str(source), "-outdev", str(destination),
                    "-boot_image", "any", "replay",
                    "-map", str(Path(directory) / "layout.inf"), "/setup/layout.inf",
                    "-map", str(Path(directory) / "txtsetup.sif"), "/setup/txtsetup.sif",
                    "-commit", "-end"], check=True)
print("Created ISO with payload-derived setup tables:", destination)

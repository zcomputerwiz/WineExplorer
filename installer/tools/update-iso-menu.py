#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Apply the setup-resume menu correction without rebuilding the filesystem."""
from pathlib import Path
import subprocess
import sys
import tempfile

source = Path(sys.argv[1]).resolve()
destination = Path(sys.argv[2]).resolve()
if destination.exists():
    raise SystemExit(f"Output already exists: {destination}")
with tempfile.TemporaryDirectory(prefix="wex-iso-menu-") as directory:
    config = Path(directory) / "grub.cfg"
    subprocess.run(["xorriso", "-osirrox", "on", "-indev", str(source),
                    "-extract", "/boot/grub/grub.cfg", str(config)], check=True)
    text = config.read_text()
    anchor = "if search --no-floppy --file --set=wexdisk /etc/wineexplorer/setup/stage; then\n"
    if text.count(anchor) != 1 or anchor + "    set default=0\n" in text:
        raise SystemExit("Unexpected menu, or resume correction already applied")
    config.write_text(text.replace(anchor, anchor + "    set default=0\n", 1))
    subprocess.run(["xorriso", "-indev", str(source), "-outdev", str(destination),
                    "-boot_image", "any", "replay", "-map", str(config),
                    "/boot/grub/grub.cfg", "-commit", "-end"], check=True)
print("Updated menu:", destination)

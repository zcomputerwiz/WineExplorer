#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Derive a zero-input test ISO without rebuilding or modifying its squashfs.

Usage: prepare-unattended-iso.py INTERACTIVE_ISO ANSWER_FILE OUTPUT_ISO
The explicit answer file is carried on the CD and selected by the kernel argument.
Use a disposable test disk and an explicit TargetDisk in the answer file.
"""
from pathlib import Path
import re
import subprocess
import sys
import tempfile

source, answer, destination = (Path(arg).resolve() for arg in sys.argv[1:4])
if destination.exists():
    raise SystemExit(f"Refusing to append to an existing ISO: {destination}")
if not re.search(r"(?mi)^\s*TargetDisk\s*=\s*/dev/vda\s*(?:;.*)?$", answer.read_text()):
    raise SystemExit("Test answer file must explicitly select /dev/vda")
with tempfile.TemporaryDirectory(prefix="wex-unattended-iso-") as directory:
    grub = Path(directory) / "grub.cfg"
    subprocess.run(["xorriso", "-osirrox", "on", "-indev", str(source), "-extract",
                    "/boot/grub/grub.cfg", str(grub)], check=True)
    text, count = re.subn(r"(?m)^set default=.*$", "set default=2", grub.read_text())
    if count != 1 or "wexunattend=/setup/unattend.sif" not in text:
        raise SystemExit("Unexpected boot menu; no ISO written")
    grub.write_text(text.replace("wexunattend=/setup/unattend.sif",
                                 "wexunattend=/run/live/medium/setup/unattend-test.sif"))
    subprocess.run(["xorriso", "-indev", str(source), "-outdev", str(destination),
                    "-boot_image", "any", "replay", "-map", str(grub), "/boot/grub/grub.cfg",
                    "-map", str(answer), "/setup/unattend-test.sif", "-commit", "-end"], check=True)
print("Created unattended test fixture:", destination)

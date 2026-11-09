#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Check a disk that text-mode Setup installed with Vintner Boot (docs/VINTNER_BOOT.md section 4.2).

Usage: check-vintner.py ROOT BOOT_STORAGE ESP

ROOT, BOOT_STORAGE and ESP are the installed disk's root, boot storage and EFI system partitions, mounted read-only
from a stopped VM. The installed system must have started at least once to the logon screen or desktop, so that
generation 1 was blessed.

Checks: vintner-core and vintner-platform are installed and no stock loader package is; Setup's step ended well;
GRUB has no configuration; the manifest (read with the installed static vintner binary) holds generation 1, GOOD,
for both firmwares, with the root and boot storage on its command line; the ESP holds Vintner's loader, also as the
removable path. Prints a one-line summary.
"""
import filecmp
import json
import os
import subprocess
import sys

STOCK_LOADERS = ["grub-pc", "grub-efi-amd64", "grub-efi-amd64-signed", "shim-signed", "systemd-boot"]


def fail(message):
    print(f"FAIL: {message}", file=sys.stderr)
    sys.exit(1)


def package_status(root, package):
    result = subprocess.run(["dpkg-query", f"--admindir={root}/var/lib/dpkg", "-W", "-f=${Status}", package],
                            capture_output=True, text=True)
    return result.stdout.strip() if result.returncode == 0 else ""


def main():
    if len(sys.argv) != 4:
        print(__doc__, file=sys.stderr)
        sys.exit(2)
    root, storage, esp = sys.argv[1:4]

    for package in ("vintner-core", "vintner-platform"):
        if package_status(root, package) != "install ok installed":
            fail(f"{package} is not installed")
    for package in STOCK_LOADERS:
        if package_status(root, package) == "install ok installed":
            fail(f"the stock loader {package} is still installed")

    log_path = f"{root}/var/log/vintner-install.log"
    try:
        with open(log_path, errors="replace") as log:
            log_text = log.read()
    except OSError:
        fail(f"no {log_path}")
    if "\n=== done" not in log_text:
        fail(f"Setup's Vintner step did not end well; see {log_path}")
    if os.path.exists(f"{root}/boot/grub/grub.cfg"):
        fail("GRUB has a configuration on a Vintner install")
    if os.path.exists(f"{root}/etc/default/grub.d/wineexplorer.cfg"):
        fail("WineExplorer's GRUB settings remain")

    shown = subprocess.run([f"{root}/usr/sbin/vintner", "manifest", "show", "--state-dir", f"{storage}/vintner/state"],
                           capture_output=True, text=True)
    if shown.returncode != 0:
        fail(f"vintner manifest show failed: {shown.stderr.strip()}")
    manifest = json.loads(shown.stdout)
    if manifest.get("firmware") != "both":
        fail(f"manifest firmware is {manifest.get('firmware')!r}, expected both")
    images = manifest.get("images") or []
    first = [image for image in images if image.get("generation") == 1]
    if not first:
        fail(f"no generation 1 in the manifest: {[image.get('generation') for image in images]}")
    image = first[0]
    if image.get("state") != "GOOD":
        fail(f"generation 1 is {image.get('state')}, expected GOOD")
    cmdline = image.get("cmdline", "")
    bs_partuuid = manifest.get("boot_storage", {}).get("partuuid", "")
    root_uuid = manifest.get("root", {}).get("uuid", "")
    if f"vintner.bootstorage=PARTUUID={bs_partuuid}" not in cmdline or f"root=UUID={root_uuid}" not in cmdline:
        fail(f"generation 1's command line lacks the root or boot storage: {cmdline!r}")
    if not image.get("uefi") or not image.get("bios"):
        fail("generation 1 is not published for both firmwares")

    loader = f"{root}/usr/lib/vintner/payloads/uefi/systemd-bootx64.efi"
    for path in ("EFI/vintner/systemd-bootx64.efi", "EFI/BOOT/BOOTX64.EFI"):
        if not os.path.isfile(f"{esp}/{path}") or not filecmp.cmp(f"{esp}/{path}", loader, shallow=False):
            fail(f"the ESP's {path} is not Vintner's loader")
    if os.path.exists(f"{esp}/EFI/debian"):
        fail("the ESP still has Debian's loader directory")

    print(f"vintner: generation 1 GOOD ({image.get('kernel', {}).get('abi')}), "
          f"{len(images)} image(s), cmdline {cmdline!r}")


main()

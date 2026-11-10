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
removable path; when the system declares the Advanced Options variant, generation 1 has it on both firmwares.
Prints a one-line summary.
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


def check_variant(root, storage, esp, image, cmdline):
    """The Advanced Options variant (installer/vintner/advanced.conf, docs/VINTNER_BOOT.md section 4.3), when the
    installed system declares it: published with generation 1 for both firmwares, with its tokens after the
    generation's command line. Vintner Boot's docs/variants.md and schema/manifest.md (V1) give the layout."""
    if not os.path.exists(f"{root}/etc/vintner/variants.d/advanced.conf"):
        return ""
    found = [v for v in image.get("variants") or [] if v.get("id") == "advanced"]
    if not found:
        fail(f"generation 1 has no advanced variant: {image.get('variants')!r}")
    variant = found[0]
    expected = f"{cmdline} wexmode=menu vintner.variant=advanced"
    if variant.get("cmdline") != expected:
        fail(f"the advanced variant's command line is {variant.get('cmdline')!r}, expected {expected!r}")
    # the UKIs live on boot storage, an XBOOTLDR partition, where systemd-boot also looks; the ESP has the loader only
    entry = (variant.get("uefi") or {}).get("entry", "")
    if entry != "vintner_variant-os-1-advanced" or not os.path.isfile(f"{storage}/EFI/Linux/{entry}.efi"):
        fail(f"boot storage has no UKI for the advanced variant ({entry!r})")
    try:
        with open(f"{storage}{image['bios']['dir']}/slot.cfg") as slot:
            slot_cfg = slot.read()
    except (KeyError, OSError) as error:
        fail(f"no slot.cfg for generation 1: {error}")
    for line in ('set vintner_variants="advanced"', f'set vintner_vcmdline_advanced="{expected}"'):
        if line not in slot_cfg.splitlines():
            fail(f"generation 1's slot.cfg lacks {line!r}")
    return f", variant {variant.get('title')!r}"


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

    variant = check_variant(root, storage, esp, image, cmdline)

    loader = f"{root}/usr/lib/vintner/payloads/uefi/systemd-bootx64.efi"
    for path in ("EFI/vintner/systemd-bootx64.efi", "EFI/BOOT/BOOTX64.EFI"):
        if not os.path.isfile(f"{esp}/{path}") or not filecmp.cmp(f"{esp}/{path}", loader, shallow=False):
            fail(f"the ESP's {path} is not Vintner's loader")
    if os.path.exists(f"{esp}/EFI/debian"):
        fail("the ESP still has Debian's loader directory")

    print(f"vintner: generation 1 GOOD ({image.get('kernel', {}).get('abi')}), "
          f"{len(images)} image(s), cmdline {cmdline!r}{variant}")


main()

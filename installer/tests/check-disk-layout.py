#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Check the partition layout text-mode setup writes on a fresh test disk (docs/VINTNER_BOOT.md section 4.1).

Usage: check-disk-layout.py DEVICE            checks the partition table, prints the root partition's device
       check-disk-layout.py DEVICE ROOT       also checks ROOT/etc/fstab (ROOT is the mounted root partition)

DEVICE is the whole test disk, for example an attached /dev/nbd0. Run as root, on a stopped VM's disk.
A fresh disk must be GPT with, in this order: BIOS boot (2 MiB, raw), EFI system (512 MiB, FAT), boot storage
(2 GiB, FAT labelled BOOTSTORAGE, GPT attribute 63 "no-auto" so systemd-gpt-auto-generator never mounts it) and the
ext4 root in the rest. Boot storage is never in fstab.
"""
import json
import subprocess
import sys

MIB = 1024 * 1024
EXPECTED = [
    # (description, GPT type, size in MiB or None for "the rest", file system or None for raw)
    ("BIOS boot", "21686148-6449-6E6F-744E-656564454649", 2, None),
    ("EFI system", "C12A7328-F81F-11D2-BA4B-00A0C93EC93B", 512, "vfat"),
    ("boot storage", "BC13C2FF-59E6-4262-A352-B275FD6F7172", 2048, "vfat"),
    ("root", "0FC63DAF-8483-4772-8E79-3D69D8477DE4", None, "ext4"),
]


def fail(message):
    print(f"FAIL: {message}", file=sys.stderr)
    sys.exit(1)


def guid_attr_bits(part):
    """GPT attribute bits 48-63 from sfdisk's "attrs" field, for example "RequiredPartition GUID:62,63"."""
    bits = set()
    for token in part.get("attrs", "").split():
        if token.startswith("GUID:"):
            bits.update(int(bit) for bit in token[len("GUID:"):].split(",") if bit)
    return bits


def blkid(node):
    out = subprocess.run(["blkid", "-o", "export", node], capture_output=True, text=True).stdout
    return dict(line.split("=", 1) for line in out.splitlines() if "=" in line)


def main():
    if len(sys.argv) not in (2, 3):
        print(__doc__, file=sys.stderr)
        sys.exit(2)
    device = sys.argv[1]
    result = subprocess.run(["sfdisk", "--json", device], capture_output=True, text=True)
    if result.returncode != 0:
        fail(f"cannot read the partition table of {device}: {result.stderr.strip()}")
    table = json.loads(result.stdout)["partitiontable"]
    if table.get("label") != "gpt":
        fail(f"partition table is {table.get('label')}, expected gpt")
    sector = table.get("sectorsize", 512)
    parts = table.get("partitions", [])
    if len(parts) != len(EXPECTED):
        fail(f"{len(parts)} partitions, expected {len(EXPECTED)}")

    info = []
    for number, (part, (what, guid, size_mib, fstype)) in enumerate(zip(parts, EXPECTED), start=1):
        node = part["node"]
        if part["type"].upper() != guid:
            fail(f"partition {number} ({node}) has type {part['type']}, expected {what} {guid}")
        if part["start"] * sector % MIB:
            fail(f"partition {number} ({node}) does not start on a 1 MiB boundary")
        if size_mib is not None and part["size"] * sector != size_mib * MIB:
            fail(f"partition {number} ({node}) is {part['size'] * sector // MIB} MiB, expected {size_mib} MiB")
        probe = blkid(node)
        if fstype is not None and probe.get("TYPE") != fstype:
            fail(f"partition {number} ({node}) holds {probe.get('TYPE')!r}, expected {fstype}")
        if fstype is None and probe.get("TYPE"):
            fail(f"partition {number} ({node}) must be raw but has a {probe['TYPE']} signature")
        if what == "boot storage" and probe.get("LABEL") != "BOOTSTORAGE":
            fail(f"boot storage label is {probe.get('LABEL')!r}, expected BOOTSTORAGE")
        if what == "boot storage" and 63 not in guid_attr_bits(part):
            fail(f"boot storage lacks GPT attribute 63 (no-auto); attrs are {part.get('attrs')!r}")
        info.append((what, node, probe))

    root_node = info[3][1]
    if len(sys.argv) == 3:
        fstab_path = f"{sys.argv[2]}/etc/fstab"
        with open(fstab_path) as fstab:
            entries = [line.split() for line in fstab if line.strip() and not line.lstrip().startswith("#")]
        mounts = {fields[1]: fields[0] for fields in entries if len(fields) >= 2}
        root_uuid = info[3][2].get("UUID")
        if mounts.get("/") != f"UUID={root_uuid}":
            fail(f"fstab mounts / from {mounts.get('/')!r}, expected UUID={root_uuid}")
        storage = info[2][2]
        for source in mounts.values():
            if source in (f"UUID={storage.get('UUID')}", "LABEL=BOOTSTORAGE", info[2][1]):
                fail("boot storage is listed in fstab; it must never be mounted by the installed system")
        esp_source = mounts.get("/boot/efi")
        if esp_source is not None and esp_source != f"UUID={info[1][2].get('UUID')}":
            fail(f"fstab mounts /boot/efi from {esp_source!r}, not the EFI system partition")

    print(root_node)


main()

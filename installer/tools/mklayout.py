#!/usr/bin/env python3
"""Generate layout.inf and update txtsetup.sif from a chroot rootfs.

Usage:
  mklayout.py <chroot_dir> <output_dir>

Outputs:
  <output_dir>/layout.inf
  <output_dir>/txtsetup.sif
"""

import os
import stat
import sys

def scan_chroot(chroot_dir):
    chroot_dir = os.path.abspath(chroot_dir)
    dir_to_id = {"/": 1}
    id_to_dir = {1: "/"}
    next_dir_id = 2

    files = []       # (rel_path, size, dir_id, filename)
    links = []       # (rel_path, target)
    specials = []    # (rel_path, type_str, mode)
    total_bytes = 0

    for root, dirs, filenames in os.walk(chroot_dir, topdown=True):
        rel_root = os.path.relpath(root, chroot_dir).replace("\\", "/")
        if rel_root == ".":
            rel_root = ""

        # Skip virtual kernel filesystems
        if rel_root == "":
            dirs[:] = [d for d in dirs if d not in ("proc", "sys", "dev", "run")]

        norm_dir = "/" + rel_root if rel_root else "/"

        # Assign directory id
        if norm_dir not in dir_to_id:
            dir_to_id[norm_dir] = next_dir_id
            id_to_dir[next_dir_id] = norm_dir
            next_dir_id += 1

        cur_dir_id = dir_to_id[norm_dir]

        # Scan directory entries
        for name in filenames:
            full_path = os.path.join(root, name)
            rel_path = (rel_root + "/" + name) if rel_root else name

            try:
                lstat = os.lstat(full_path)
            except OSError:
                continue

            if stat.S_ISLNK(lstat.st_mode):
                try:
                    target = os.readlink(full_path)
                    links.append((rel_path, target))
                except OSError:
                    continue
            elif stat.S_ISREG(lstat.st_mode):
                size = lstat.st_size
                total_bytes += size
                files.append((rel_path, size, cur_dir_id, name))
            elif stat.S_ISCHR(lstat.st_mode) or stat.S_ISBLK(lstat.st_mode) or stat.S_ISFIFO(lstat.st_mode):
                specials.append((rel_path, "special", oct(lstat.st_mode)))

    return dir_to_id, id_to_dir, files, links, specials, total_bytes

def write_layout(output_path, dir_to_id, id_to_dir, files, links, specials):
    with open(output_path, "w", encoding="utf-8") as f:
        f.write("; layout.inf - WineExplorer installation media file table\n")
        f.write("[Version]\n")
        f.write('Signature = "$Windows NT$"\n\n')

        f.write("[SourceDisksNames]\n")
        f.write('1 = "Debian GNU/Linux with WineExplorer",\\live\\filesystem.squashfs,,\n\n')

        f.write("[WinntDirectories]\n")
        for dir_id in sorted(id_to_dir.keys()):
            f.write(f'{dir_id} = "{id_to_dir[dir_id]}"\n')
        f.write("\n")

        f.write("[SourceDisksFiles]\n")
        # Format: filename = diskid,subdir,size,checksum,,,bootmedia,dirid,,disposition,targetname
        for rel_path, size, dir_id, filename in files:
            f.write(f"{rel_path} = 1,,{size},,,,,{dir_id},,1,{filename}\n")
        f.write("\n")

        f.write("[Links]\n")
        for rel_path, target in links:
            f.write(f'{rel_path} = "{target}"\n')
        f.write("\n")

        f.write("[SpecialFiles]\n")
        for rel_path, sp_type, mode in specials:
            f.write(f"{rel_path} = {sp_type},{mode}\n")

def write_txtsetup(template_path, output_path, total_bytes):
    total_mb = (total_bytes + 1048575) // 1048576
    required_mb = total_mb + 2048  # Add 2 GB headroom

    content = ""
    if template_path and os.path.exists(template_path):
        with open(template_path, "r", encoding="utf-8") as f:
            content = f.read()
    else:
        content = """[SetupData]
ProductName = "Debian GNU/Linux with WineExplorer"
MajorVersion = 13
MinorVersion = 0
DefaultPath = \\target
OsLoadOptions = "/fastdetect"

[DiskSpaceRequirements]
FreeSysPartDiskSpace = 512
FreeRootDiskSpace = @@REQUIRED_MB@@
"""

    if "FreeRootDiskSpace" in content:
        import re
        content = re.sub(r"FreeRootDiskSpace\s*=\s*\d+", f"FreeRootDiskSpace = {required_mb}", content)
    elif "@@REQUIRED_MB@@" in content:
        content = content.replace("@@REQUIRED_MB@@", str(required_mb))

    with open(output_path, "w", encoding="utf-8") as f:
        f.write(content)

def main():
    if len(sys.argv) < 3:
        print(__doc__)
        sys.exit(1)

    chroot_dir = sys.argv[1]
    out_dir = sys.argv[2]
    template_path = sys.argv[3] if len(sys.argv) > 3 else None

    if not os.path.exists(chroot_dir):
        print(f"Error: chroot dir {chroot_dir} does not exist")
        sys.exit(1)

    os.makedirs(out_dir, exist_ok=True)
    print(f"Scanning chroot at {chroot_dir}...")
    dir_to_id, id_to_dir, files, links, specials, total_bytes = scan_chroot(chroot_dir)

    print(f"Scanned: {len(files)} files, {len(links)} links, {len(specials)} specials, {len(id_to_dir)} dirs.")
    print(f"Total regular file size: {total_bytes / (1024*1024):.1f} MB.")

    layout_file = os.path.join(out_dir, "layout.inf")
    print(f"Writing {layout_file}...")
    write_layout(layout_file, dir_to_id, id_to_dir, files, links, specials)

    txtsetup_file = os.path.join(out_dir, "txtsetup.sif")
    print(f"Writing {txtsetup_file}...")
    write_txtsetup(template_path, txtsetup_file, total_bytes)

    print("Done.")

if __name__ == "__main__":
    main()

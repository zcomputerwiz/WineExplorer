#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Build a Windows visual style (.msstyles) resource-only DLL from source.

Usage:
    python3 scripts/build_msstyles.py SRC_DIR OUT.msstyles
"""

import re
import shutil
import struct
import subprocess
import sys
import tempfile
from pathlib import Path

VERSION_BLOCK = """#include <windows.h>
LANGUAGE LANG_ENGLISH, SUBLANG_ENGLISH_US
VS_VERSION_INFO VERSIONINFO
FILEVERSION     5,1,2600,0
PRODUCTVERSION  5,1,2600,0
FILEFLAGSMASK   0x3fL
FILEFLAGS       0x0L
FILEOS          VOS_NT_WINDOWS32
FILETYPE        VFT_DLL
FILESUBTYPE     VFT2_UNKNOWN
BEGIN
    BLOCK "StringFileInfo"
    BEGIN
        BLOCK "040904b0"
        BEGIN
            VALUE "CompanyName",      "ReactOS Project / WineExplorer"
            VALUE "FileDescription",  REACTOS_STR_FILE_DESCRIPTION
            VALUE "FileVersion",      "5.1.2600.0"
            VALUE "InternalName",     REACTOS_STR_INTERNAL_NAME
            VALUE "LegalCopyright",   "GPL-2.0-or-later"
            VALUE "OriginalFilename", REACTOS_STR_ORIGINAL_FILENAME
            VALUE "ProductName",      "WineExplorer Operating System"
            VALUE "ProductVersion",   "5.1.2600.0"
        END
    END
    BLOCK "VarFileInfo"
    BEGIN
        VALUE "Translation", 0x409, 1200
    END
END
"""


def convert_ini_to_utf16le(src_ini: Path, dst_ini: Path) -> None:
    """Convert an ASCII/UTF-8 INI file to UTF-16LE with BOM (matching ReactOS utf16le_convert)."""
    raw = src_ini.read_bytes()
    if raw.startswith(b"\xff\xfe"):
        dst_ini.write_bytes(raw)
        return
    text = raw.decode("utf-8", errors="surrogateescape")
    text = text.replace("\r\n", "\n").replace("\n", "\r\n")
    dst_ini.write_bytes(b"\xff\xfe" + text.encode("utf-16-le"))


def fix_bmp_header(bmp_path: Path) -> None:
    """Ensure bfSize in the 14-byte BITMAPFILEHEADER matches actual file length."""
    data = bytearray(bmp_path.read_bytes())
    if len(data) >= 14 and data[:2] == b"BM":
        actual_size = len(data)
        header_size = struct.unpack_from("<I", data, 2)[0]
        if header_size != actual_size:
            struct.pack_into("<I", data, 2, actual_size)
            bmp_path.write_bytes(bytes(data))


def render_svg_to_bmp(svg_path: Path, bmp_path: Path, tmp_dir: Path) -> None:
    """Render an SVG from svg/<NAME>.svg to an 8-bit per channel BMP in bitmaps/<NAME>.bmp."""
    png_path = tmp_dir / f"{svg_path.stem}.png"
    subprocess.run(["rsvg-convert", str(svg_path), "-o", str(png_path)], check=True)
    svg_text = svg_path.read_text(encoding="utf-8", errors="ignore")
    want_32bpp = "bmp-bpp: 32" in svg_text
    if not want_32bpp and bmp_path.exists():
        old_data = bmp_path.read_bytes()
        if len(old_data) >= 30 and old_data[:2] == b"BM":
            bpp = struct.unpack_from("<H", old_data, 28)[0]
            if bpp == 32:
                want_32bpp = True

    if want_32bpp:
        from PIL import Image
        im = Image.open(png_path)
        im.save(bmp_path)
    else:
        subprocess.run(
            [
                "convert",
                str(png_path),
                "-background",
                "#FF00FF",
                "-alpha",
                "remove",
                "-alpha",
                "off",
                "-depth",
                "8",
                "-type",
                "TrueColor",
                f"BMP3:{bmp_path}",
            ],
            check=True,
        )
    fix_bmp_header(bmp_path)


def build_msstyles(src_dir: Path, out_file: Path) -> None:
    if not src_dir.is_dir():
        raise SystemExit(f"Source directory not found: {src_dir}")

    rc_candidates = sorted(src_dir.glob("*.rc"))
    if not rc_candidates:
        raise SystemExit(f"No .rc file found in {src_dir}")
    src_rc = rc_candidates[0]

    with tempfile.TemporaryDirectory(prefix="msstyles_") as tmp:
        work = Path(tmp)
        for sub in ("bitmaps", "textfiles", "lang"):
            s = src_dir / sub
            if s.is_dir():
                shutil.copytree(s, work / sub)

        # Render any SVGs in src_dir/svg/*.svg into work/bitmaps/<NAME>.bmp
        svg_dir = src_dir / "svg"
        if svg_dir.is_dir():
            (work / "bitmaps").mkdir(parents=True, exist_ok=True)
            for svg_file in sorted(svg_dir.glob("*.svg")):
                target_bmp = work / "bitmaps" / f"{svg_file.stem}.bmp"
                render_svg_to_bmp(svg_file, target_bmp, work)

        # Convert textfiles/*.INI -> textfiles/*_utf16.INI
        txt_dir = work / "textfiles"
        if txt_dir.is_dir():
            for ini_file in sorted(txt_dir.glob("*.INI")):
                if ini_file.stem.endswith("_utf16"):
                    continue
                utf16_file = txt_dir / f"{ini_file.stem}_utf16.INI"
                convert_ini_to_utf16le(ini_file, utf16_file)

        # Prepare .rc with our version block replacing <reactos/version.rc>
        rc_text = src_rc.read_text(encoding="utf-8", errors="surrogateescape")
        rc_text = re.sub(r"#\s*include\s*<reactos/version\.rc>", lambda _m: VERSION_BLOCK, rc_text)
        work_rc = work / src_rc.name
        work_rc.write_text(rc_text, encoding="utf-8", errors="surrogateescape")

        # Enable all LANGUAGE_* sections referenced in the .rc
        lang_defs = sorted(set(re.findall(r"#ifdef\s+(LANGUAGE_[A-Z0-9_]+)", rc_text)))
        windres_cmd = ["x86_64-w64-mingw32-windres", "-I", str(work)]
        for d in lang_defs:
            windres_cmd.append(f"-D{d}")

        obj_file = work / "theme.o"
        windres_cmd += [str(work_rc), "-O", "coff", "-o", str(obj_file)]
        subprocess.run(windres_cmd, cwd=str(work), check=True)

        out_file.parent.mkdir(parents=True, exist_ok=True)
        subprocess.run(
            [
                "x86_64-w64-mingw32-gcc",
                "-shared",
                "-nostdlib",
                "-Wl,--entry=0",
                "-o",
                str(out_file),
                str(obj_file),
            ],
            check=True,
        )


def main() -> None:
    if len(sys.argv) != 3:
        raise SystemExit(__doc__.strip())
    src_dir = Path(sys.argv[1]).resolve()
    out_file = Path(sys.argv[2]).resolve()
    build_msstyles(src_dir, out_file)
    print(f"Built {out_file} from {src_dir}")


if __name__ == "__main__":
    main()

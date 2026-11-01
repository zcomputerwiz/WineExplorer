#!/usr/bin/env python3
#
# Independent reader and test suite for Windows XP/2003 Prefetch (PF v17, SCCA).
#
# Copyright 2026 WineExplorer contributors
#
# This program is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; either version 2 of the License, or
# (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program; if not, write to the Free Software
# Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301, USA
#
# SPDX-License-Identifier: GPL-2.0-or-later

import os
import shutil
import struct
import subprocess
import sys
import tempfile


def scca_xp_hash(path_upper: str) -> int:
    """
    Calculate the Windows XP / Server 2003 SCCA prefetch hash for an uppercase
    device path encoded as UTF-16LE without BOM or terminating NUL, per
    pf-format.asciidoc section 'Calculating the prefetch hash'.
    """
    raw = path_upper.encode("utf-16-le")
    hash_value = 0
    for byte_val in raw:
        hash_value = ((hash_value * 37) + byte_val) % 0x100000000

    hash_value = (hash_value * 314159269) % 0x100000000

    if hash_value > 0x80000000:
        hash_value = 0x100000000 - hash_value

    return (abs(hash_value) % 1000000007) % 0x100000000


def make_pf_name_py(exe_name_upper: str, hash_val: int) -> str:
    return f"{exe_name_upper[:29].upper()}-{hash_val:08X}.pf"


def _decode_utf16le_nul(buf: bytes) -> str:
    if len(buf) % 2 != 0:
        raise ValueError("UTF-16LE field byte length is not even")
    u16 = struct.unpack(f"<{len(buf) // 2}H", buf)
    if 0 not in u16:
        raise ValueError("UTF-16LE string missing NUL terminator")
    nul_idx = u16.index(0)
    return buf[: nul_idx * 2].decode("utf-16-le", errors="surrogatepass")


def parse_pf_v17(data: bytes) -> dict:
    """
    Independent pure-Python reader for uncompressed PF format version 17.
    Validates header, file information (68 bytes), Section A (metrics),
    Section B (trace chains), Section C (filename strings), and Section D
    (volumes information), checking bounds and section non-overlap.
    """
    total_len = len(data)
    if total_len < 152:
        raise ValueError(f"PF v17 file too short: {total_len} < 152")

    # 1. File header (84 bytes)
    version, sig, hdr_unknown, file_size = struct.unpack_from("<4I", data, 0)
    exe_raw = data[16:76]
    prefetch_hash, hdr_flags = struct.unpack_from("<2I", data, 76)

    assert version == 17, f"Expected format version 17, got {version}"
    assert data[4:8] == b"SCCA", f"Expected signature b'SCCA', got {data[4:8]!r}"
    assert sig == 0x41434353, f"Unexpected signature uint32: {sig:#x}"
    assert file_size == total_len, f"Header file_size {file_size} != actual length {total_len}"

    exe_name = _decode_utf16le_nul(exe_raw)
    assert 1 <= len(exe_name) <= 29, f"Invalid exe_name length {len(exe_name)}"

    # 2. File information - version 17 (68 bytes at offset 84..152)
    (
        metrics_offset,
        metrics_count,
        chains_offset,
        chains_count,
        strings_offset,
        strings_size,
        volumes_offset,
        volumes_count,
        volumes_size,
        last_run_time,
    ) = struct.unpack_from("<9IQ", data, 84)
    info_empty_16 = data[84 + 44 : 84 + 60]
    run_count, info_unknown_64 = struct.unpack_from("<2I", data, 84 + 60)

    assert metrics_offset == 152, f"Expected metrics_offset 152, got {metrics_offset}"

    metrics_size = metrics_count * 20
    chains_size = chains_count * 12

    # Validate section boundaries and non-overlapping layout
    sections = [
        ("header+info", 0, 152),
        ("section_a_metrics", metrics_offset, metrics_offset + metrics_size),
        ("section_b_chains", chains_offset, chains_offset + chains_size),
        ("section_c_strings", strings_offset, strings_offset + strings_size),
        ("section_d_volumes", volumes_offset, volumes_offset + volumes_size),
    ]
    for name, start, end in sections:
        assert 0 <= start <= end <= total_len, (
            f"Section {name} [{start}, {end}) out of file bounds [0, {total_len})"
        )

    for idx in range(len(sections)):
        for jdx in range(idx + 1, len(sections)):
            n1, s1, e1 = sections[idx]
            n2, s2, e2 = sections[jdx]
            if s1 < e1 and s2 < e2:
                assert e1 <= s2 or e2 <= s1, (
                    f"Sections {n1} [{s1}, {e1}) and {n2} [{s2}, {e2}) overlap"
                )

    # 3. Section B: Trace chains array (12 bytes per entry in v17)
    chains = []
    for i in range(chains_count):
        off = chains_offset + i * 12
        next_idx, block_count, c_flags, c_unk, used_bits, prefetched_bits = (
            struct.unpack_from("<II4B", data, off)
        )
        assert next_idx == 0xFFFFFFFF or next_idx < chains_count, (
            f"Invalid trace chain next_idx {next_idx:#x} at entry {i}"
        )
        chains.append(
            {
                "next_index": next_idx,
                "block_count": block_count,
                "flags": c_flags,
                "unknown": c_unk,
                "used_bits": used_bits,
                "prefetched_bits": prefetched_bits,
            }
        )

    # 4. Section A (File metrics array, 20 bytes per entry in v17) + Section C (Filename strings)
    strings_blob = data[strings_offset : strings_offset + strings_size]
    metrics = []
    string_ranges = []
    for i in range(metrics_count):
        off = metrics_offset + i * 20
        chain_idx, trace_count, str_off, str_chars, m_flags = struct.unpack_from(
            "<5I", data, off
        )
        assert chain_idx < chains_count, (
            f"Metrics {i} chain_idx {chain_idx} >= chains_count {chains_count}"
        )
        assert trace_count >= 1, f"Metrics {i} trace_count {trace_count} < 1"
        str_byte_end = str_off + (str_chars + 1) * 2
        assert 0 <= str_off < str_byte_end <= strings_size, (
            f"Metrics {i} string [{str_off}, {str_byte_end}) outside strings_size {strings_size}"
        )
        raw_chars = strings_blob[str_off : str_off + str_chars * 2]
        term = struct.unpack_from("<H", strings_blob, str_off + str_chars * 2)[0]
        assert term == 0, f"Metrics {i} string missing NUL terminator at {str_off + str_chars * 2}"
        filename = raw_chars.decode("utf-16-le", errors="surrogatepass")
        string_ranges.append((str_off, str_byte_end))
        metrics.append(
            {
                "chain_index": chain_idx,
                "trace_count": trace_count,
                "string_offset": str_off,
                "string_chars": str_chars,
                "flags": m_flags,
                "filename": filename,
            }
        )

    # Check Section C strings are non-overlapping
    string_ranges.sort()
    for i in range(1, len(string_ranges)):
        assert string_ranges[i - 1][1] <= string_ranges[i][0], (
            "Overlapping filename strings in Section C"
        )

    # 5. Section D: Volumes information (40-byte entries in v17)
    vol_blob = data[volumes_offset : volumes_offset + volumes_size]
    assert volumes_count * 40 <= volumes_size, (
        f"Volumes table {volumes_count * 40} exceeds volumes_size {volumes_size}"
    )
    volumes = []
    vol_subranges = [(0, volumes_count * 40)] if volumes_count > 0 else []

    for i in range(volumes_count):
        off = i * 40
        (
            dev_off,
            dev_chars,
            creation_time,
            serial_num,
            frefs_off,
            frefs_size,
            dirs_off,
            dirs_count,
            vol_unk,
        ) = struct.unpack_from("<IIQIIIIII", vol_blob, off)

        dev_byte_end = dev_off + (dev_chars + 1) * 2
        assert volumes_count * 40 <= dev_off < dev_byte_end <= volumes_size, (
            f"Volume {i} device path [{dev_off}, {dev_byte_end}) out of bounds"
        )
        dev_path = vol_blob[dev_off : dev_off + dev_chars * 2].decode(
            "utf-16-le", errors="surrogatepass"
        )
        dev_term = struct.unpack_from("<H", vol_blob, dev_off + dev_chars * 2)[0]
        assert dev_term == 0, f"Volume {i} device path missing NUL terminator"
        vol_subranges.append((dev_off, dev_byte_end))

        file_refs = []
        if frefs_size > 0:
            assert volumes_count * 40 <= frefs_off <= frefs_off + frefs_size <= volumes_size, (
                f"Volume {i} file references [{frefs_off}, {frefs_off + frefs_size}) out of bounds"
            )
            assert frefs_size >= 8, f"Volume {i} file_refs_size {frefs_size} < 8"
            fref_ver, fref_count = struct.unpack_from("<2I", vol_blob, frefs_off)
            assert fref_ver == 1, f"Volume {i} file references version {fref_ver} != 1"
            assert frefs_size == 8 + fref_count * 8, (
                f"Volume {i} file_refs_size {frefs_size} != {8 + fref_count * 8}"
            )
            for r in range(fref_count):
                ref_val = struct.unpack_from("<Q", vol_blob, frefs_off + 8 + r * 8)[0]
                file_refs.append(ref_val)
            vol_subranges.append((frefs_off, frefs_off + frefs_size))

        dir_strings = []
        assert 0 <= dirs_off <= volumes_size, (
            f"Volume {i} dir_strings_offset {dirs_off} out of bounds"
        )
        cur_dir = dirs_off
        for _ in range(dirs_count):
            assert cur_dir + 2 <= volumes_size
            d_chars = struct.unpack_from("<H", vol_blob, cur_dir)[0]
            d_end = cur_dir + 2 + (d_chars + 1) * 2
            assert d_end <= volumes_size
            d_str = vol_blob[cur_dir + 2 : cur_dir + 2 + d_chars * 2].decode(
                "utf-16-le", errors="surrogatepass"
            )
            d_term = struct.unpack_from("<H", vol_blob, cur_dir + 2 + d_chars * 2)[0]
            assert d_term == 0
            dir_strings.append(d_str)
            cur_dir = d_end
        if dirs_count > 0:
            vol_subranges.append((dirs_off, cur_dir))

        volumes.append(
            {
                "device_path": dev_path,
                "creation_time": creation_time,
                "serial_number": serial_num,
                "file_references": file_refs,
                "directory_strings": dir_strings,
                "unknown": vol_unk,
            }
        )

    vol_subranges.sort()
    for i in range(1, len(vol_subranges)):
        assert vol_subranges[i - 1][1] <= vol_subranges[i][0], (
            f"Overlapping sub-structures inside Section D: {vol_subranges}"
        )

    return {
        "version": version,
        "signature": data[4:8],
        "header_unknown": hdr_unknown,
        "file_size": file_size,
        "exe_name": exe_name,
        "prefetch_hash": prefetch_hash,
        "exe_flags": hdr_flags,
        "last_run_time": last_run_time,
        "info_empty_16": info_empty_16,
        "run_count": run_count,
        "info_unknown_64": info_unknown_64,
        "metrics": metrics,
        "chains": chains,
        "volumes": volumes,
    }


C_DRIVER_TEMPLATE = """
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "__PF_V17_H_PATH__"

static size_t u16_len( const uint16_t *s )
{
    size_t n = 0;
    while (s[n] != 0)
        n++;
    return n;
}

int main( int argc, char **argv )
{
    if (argc != 2)
    {
        fprintf( stderr, "usage: %s <out.pf>\\n", argv[0] );
        return 2;
    }

    const uint16_t *exe_full_path = u"\\\\DEVICE\\\\HARDDISKVOLUME1\\\\WINDOWS\\\\NOTEPAD.EXE";
    const uint16_t *exe_name = u"NOTEPAD.EXE";

    uint32_t hash = pf_prefetch_hash_xp( exe_full_path, u16_len( exe_full_path ) );
    char pf_name[64];
    pf_make_name( exe_name, hash, pf_name, sizeof(pf_name) );

    /* Check truncation to 29 characters in pf_make_name */
    const uint16_t *long_exe = u"ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789.EXE";
    char trunc_name[64];
    pf_make_name( long_exe, 0x01020304u, trunc_name, sizeof(trunc_name) );
    if (strcmp( trunc_name, "ABCDEFGHIJKLMNOPQRSTUVWXYZ012-01020304.pf" ) != 0)
    {
        fprintf( stderr, "unexpected truncated name: %s\\n", trunc_name );
        return 1;
    }

    struct pf_volume vols[3] = {
        { u"\\\\DEVICE\\\\HARDDISKVOLUME1", 0x01D6A1B2C3D4E5F6ULL, 0x12345678u },
        { u"\\\\DEVICE\\\\HARDDISKVOLUME2", 0x01D6A1B2C3D40000ULL, 0x9ABCDEF0u },
        { u"\\\\DEVICE\\\\HARDDISKVOLUME3", 0x01D6000011112222ULL, 0xCAFEBABEu },
    };

    struct pf_file files[3] = {
        { u"\\\\DEVICE\\\\HARDDISKVOLUME1\\\\WINDOWS\\\\SYSTEM32\\\\NTDLL.DLL", 0x00000200u },
        { u"\\\\DEVICE\\\\HARDDISKVOLUME1\\\\WINDOWS\\\\SYSTEM32\\\\KERNEL32.DLL", 0x00000200u },
        { u"\\\\DEVICE\\\\HARDDISKVOLUME1\\\\WINDOWS\\\\NOTEPAD.EXE", 0x00000002u },
    };

    uint32_t blocks[3] = { 5u, 8u, 3u };

    struct pf_info info;
    memset( &info, 0, sizeof(info) );
    info.exe_name_utf16 = exe_name;
    info.path_hash = hash;
    info.run_count = 7u;
    info.last_run_filetime = 0x01DC123456789ABCULL;
    info.exe_flags = 0x00000001u;
    info.volumes = vols;
    info.volume_count = 3u;
    info.files = files;
    info.file_count = 3u;
    info.chain_blocks = blocks;

    uint8_t *buf = NULL;
    size_t buf_len = 0;
    if (pf_write_v17( &info, &buf, &buf_len ) != 0 || !buf || buf_len == 0)
    {
        fprintf( stderr, "pf_write_v17 failed on valid input\\n" );
        return 1;
    }

    FILE *fp = fopen( argv[1], "wb" );
    if (!fp)
    {
        perror( "fopen" );
        free( buf );
        return 1;
    }
    if (fwrite( buf, 1, buf_len, fp ) != buf_len)
    {
        perror( "fwrite" );
        fclose( fp );
        free( buf );
        return 1;
    }
    fclose( fp );
    free( buf );

    /* Negative test cases: all must return -1 */

    /* 1. NULL exe name */
    struct pf_info bad = info;
    bad.exe_name_utf16 = NULL;
    if (pf_write_v17( &bad, &buf, &buf_len ) != -1)
    {
        fprintf( stderr, "expected -1 for NULL exe_name_utf16\\n" );
        return 1;
    }

    /* 2. Too many files (> 1024) */
    bad = info;
    bad.file_count = 1025u;
    if (pf_write_v17( &bad, &buf, &buf_len ) != -1)
    {
        fprintf( stderr, "expected -1 for file_count > 1024\\n" );
        return 1;
    }

    /* 3. Too many volumes (> 8) */
    bad = info;
    bad.volume_count = 9u;
    if (pf_write_v17( &bad, &buf, &buf_len ) != -1)
    {
        fprintf( stderr, "expected -1 for volume_count > 8\\n" );
        return 1;
    }

    /* 4. Oversized file path string (> 260 UTF-16 units) */
    uint16_t long_path[265];
    for (int i = 0; i < 261; i++)
        long_path[i] = (uint16_t)'A';
    long_path[261] = 0;

    struct pf_file bad_files[1] = { { long_path, 0u } };
    bad = info;
    bad.files = bad_files;
    bad.file_count = 1u;
    if (pf_write_v17( &bad, &buf, &buf_len ) != -1)
    {
        fprintf( stderr, "expected -1 for oversized file string (> 260)\\n" );
        return 1;
    }

    /* 5. Oversized volume device path (> 260 UTF-16 units) */
    struct pf_volume bad_vols[1] = { { long_path, 1ULL, 1u } };
    bad = info;
    bad.volumes = bad_vols;
    bad.volume_count = 1u;
    if (pf_write_v17( &bad, &buf, &buf_len ) != -1)
    {
        fprintf( stderr, "expected -1 for oversized volume string (> 260)\\n" );
        return 1;
    }

    /* 6. Oversized exe_name_utf16 (> 29 UTF-16 units) */
    bad = info;
    bad.exe_name_utf16 = long_exe;
    if (pf_write_v17( &bad, &buf, &buf_len ) != -1)
    {
        fprintf( stderr, "expected -1 for oversized exe_name_utf16 (> 29)\\n" );
        return 1;
    }

    printf( "HASH=%08X\\nNAME=%s\\n", (unsigned)hash, pf_name );
    return 0;
}
"""


def main() -> int:
    # 1. Verify documented XP hash reference vector from pf-format.asciidoc
    ref_path = "\\DEVICE\\HARDDISKVOLUME1\\WINDOWS\\NOTEPAD.EXE"
    expected_ref_hash = 0x189578DA
    py_hash = scca_xp_hash(ref_path)
    assert py_hash == expected_ref_hash, (
        f"Python hash {py_hash:#x} != documented XP hash {expected_ref_hash:#x}"
    )

    # 2. Check for gcc
    if shutil.which("gcc") is None:
        print("SKIP: gcc is unavailable in PATH; skipping C writer integration test.")
        return 0

    here = os.path.dirname(os.path.abspath(__file__))
    pf_h_path = os.path.join(here, "pf_v17.h").replace("\\", "/")
    pf_c_path = os.path.join(here, "pf_v17.c")

    with tempfile.TemporaryDirectory() as tmpdir:
        driver_c = os.path.join(tmpdir, "driver.c")
        driver_bin = os.path.join(tmpdir, "driver")
        sample_pf = os.path.join(tmpdir, "sample.pf")

        with open(driver_c, "w", encoding="utf-8") as f:
            f.write(C_DRIVER_TEMPLATE.replace("__PF_V17_H_PATH__", pf_h_path))

        compile_cmd = [
            "gcc",
            "-std=gnu11",
            "-Wall",
            "-Wextra",
            "-O2",
            "-o",
            driver_bin,
            driver_c,
            pf_c_path,
        ]
        subprocess.run(compile_cmd, cwd=here, check=True)

        proc = subprocess.run(
            [driver_bin, sample_pf],
            cwd=here,
            check=True,
            capture_output=True,
            text=True,
        )

        kv = {}
        for line in proc.stdout.strip().splitlines():
            if "=" in line:
                k, v = line.split("=", 1)
                kv[k.strip()] = v.strip()

        c_hash = int(kv["HASH"], 16)
        c_name = kv["NAME"]
        assert c_hash == py_hash, f"C hash {c_hash:#x} != Python hash {py_hash:#x}"
        assert c_name == make_pf_name_py("NOTEPAD.EXE", py_hash), (
            f"C name {c_name!r} != expected {make_pf_name_py('NOTEPAD.EXE', py_hash)!r}"
        )

        with open(sample_pf, "rb") as f:
            raw_pf = f.read()

        parsed = parse_pf_v17(raw_pf)

        assert parsed["version"] == 17
        assert parsed["signature"] == b"SCCA"
        assert parsed["file_size"] == len(raw_pf)
        assert parsed["exe_name"] == "NOTEPAD.EXE"
        assert parsed["prefetch_hash"] == py_hash
        assert parsed["exe_flags"] == 1
        assert parsed["run_count"] == 7
        assert parsed["last_run_time"] == 0x01DC123456789ABC

        expected_files = [
            ("\\DEVICE\\HARDDISKVOLUME1\\WINDOWS\\SYSTEM32\\NTDLL.DLL", 0x200, 5),
            ("\\DEVICE\\HARDDISKVOLUME1\\WINDOWS\\SYSTEM32\\KERNEL32.DLL", 0x200, 8),
            ("\\DEVICE\\HARDDISKVOLUME1\\WINDOWS\\NOTEPAD.EXE", 0x2, 3),
        ]
        assert len(parsed["metrics"]) == 3
        assert len(parsed["chains"]) == 3
        for idx, (exp_path, exp_flags, exp_blocks) in enumerate(expected_files):
            m = parsed["metrics"][idx]
            assert m["filename"] == exp_path
            assert m["string_chars"] == len(exp_path)
            assert m["flags"] == exp_flags
            assert m["chain_index"] == idx
            assert m["trace_count"] == 1
            c = parsed["chains"][m["chain_index"]]
            assert c["next_index"] == 0xFFFFFFFF
            assert c["block_count"] == exp_blocks

        expected_vols = [
            ("\\DEVICE\\HARDDISKVOLUME1", 0x01D6A1B2C3D4E5F6, 0x12345678),
            ("\\DEVICE\\HARDDISKVOLUME2", 0x01D6A1B2C3D40000, 0x9ABCDEF0),
            ("\\DEVICE\\HARDDISKVOLUME3", 0x01D6000011112222, 0xCAFEBABE),
        ]
        assert len(parsed["volumes"]) == 3
        for idx, (exp_dev, exp_time, exp_serial) in enumerate(expected_vols):
            v = parsed["volumes"][idx]
            assert v["device_path"] == exp_dev
            assert v["creation_time"] == exp_time
            assert v["serial_number"] == exp_serial
            assert v["file_references"] == []
            assert v["directory_strings"] == []

    print(f"OK: PF v17 round-trip and negative tests passed (hash={py_hash:08X}, name={c_name})")
    return 0


if __name__ == "__main__":
    sys.exit(main())

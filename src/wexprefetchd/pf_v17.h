/*
 * Windows XP / Server 2003 Prefetch file (version 17, SCCA) writer.
 *
 * Copyright 2026 WineExplorer contributors
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301, USA
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef WEXPREFETCHD_PF_V17_H
#define WEXPREFETCHD_PF_V17_H

#include <stddef.h>
#include <stdint.h>

#define PF_V17_VERSION             17u
#define PF_V17_SIGNATURE           0x41434353u /* "SCCA" little-endian */
#define PF_V17_HEADER_UNKNOWN      0x0000000fu
#define PF_V17_HEADER_SIZE         84u
#define PF_V17_FILE_INFO_SIZE      68u
#define PF_V17_METRICS_OFFSET      (PF_V17_HEADER_SIZE + PF_V17_FILE_INFO_SIZE) /* 152 */
#define PF_V17_METRICS_ENTRY_SIZE  20u
#define PF_V17_CHAIN_ENTRY_SIZE    12u
#define PF_V17_VOLUME_ENTRY_SIZE   40u
#define PF_V17_FILE_REFS_STUB_SIZE 8u

#define PF_V17_MAX_EXE_CHARS       29u
#define PF_V17_MAX_STRING_CHARS    260u
#define PF_V17_MAX_FILES           1024u
#define PF_V17_MAX_VOLUMES         8u

struct pf_volume
{
    const uint16_t *device_path_utf16;
    uint64_t creation_filetime;
    uint32_t serial_number;
};

struct pf_file
{
    const uint16_t *path_utf16;
    uint32_t flags;
};

struct pf_info
{
    const uint16_t *exe_name_utf16; /* up to 29 chars + NUL */
    uint32_t path_hash;
    uint32_t run_count;
    uint64_t last_run_filetime;
    uint32_t exe_flags;
    const struct pf_volume *volumes;
    unsigned volume_count;
    const struct pf_file *files;
    unsigned file_count;
    const uint32_t *chain_blocks; /* optional per-file block counts, may be NULL */
};

uint32_t pf_prefetch_hash_xp( const uint16_t *path_upper_utf16, size_t len );
int pf_write_v17( const struct pf_info *info, uint8_t **out, size_t *out_len );
void pf_make_name( const uint16_t *exe_name_upper, uint32_t hash, char *out, size_t out_size );

#endif /* WEXPREFETCHD_PF_V17_H */

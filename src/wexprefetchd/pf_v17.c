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

#include "pf_v17.h"

#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static inline void put_u16_le( uint8_t *dst, uint16_t v )
{
    dst[0] = (uint8_t)(v & 0xffu);
    dst[1] = (uint8_t)((v >> 8) & 0xffu);
}

static inline void put_u32_le( uint8_t *dst, uint32_t v )
{
    dst[0] = (uint8_t)(v & 0xffu);
    dst[1] = (uint8_t)((v >> 8) & 0xffu);
    dst[2] = (uint8_t)((v >> 16) & 0xffu);
    dst[3] = (uint8_t)((v >> 24) & 0xffu);
}

static inline void put_u64_le( uint8_t *dst, uint64_t v )
{
    put_u32_le( dst, (uint32_t)(v & 0xffffffffULL) );
    put_u32_le( dst + 4, (uint32_t)((v >> 32) & 0xffffffffULL) );
}

static void put_utf16le( uint8_t *dst, const uint16_t *src, size_t count )
{
    size_t i;

    for (i = 0; i < count; i++)
        put_u16_le( dst + (i * 2u), src[i] );
}

static int utf16_bounded_len( const uint16_t *s, size_t max_chars, size_t *out_len )
{
    size_t i;

    if (!s)
        return -1;
    for (i = 0; i <= max_chars; i++)
    {
        if (s[i] == 0)
        {
            *out_len = i;
            return 0;
        }
    }
    return -1;
}

static int checked_add_u32_size( size_t a, size_t b, size_t *out )
{
    if (SIZE_MAX - a < b)
        return -1;
    if (a + b > UINT32_MAX)
        return -1;
    *out = a + b;
    return 0;
}

uint32_t pf_prefetch_hash_xp( const uint16_t *path_upper_utf16, size_t len )
{
    uint64_t hash = 0;
    size_t i;

    if (!path_upper_utf16 && len != 0)
        return 0;

    for (i = 0; i < len; i++)
    {
        uint16_t u = path_upper_utf16[i];
        uint8_t lo = (uint8_t)(u & 0xffu);
        uint8_t hi = (uint8_t)((u >> 8) & 0xffu);

        hash = ((hash * 37ULL) + lo) & 0xffffffffULL;
        hash = ((hash * 37ULL) + hi) & 0xffffffffULL;
    }

    hash = (hash * 314159269ULL) & 0xffffffffULL;
    if (hash > 0x80000000ULL)
        hash = 0x100000000ULL - hash;

    return (uint32_t)((hash % 1000000007ULL) & 0xffffffffULL);
}

void pf_make_name( const uint16_t *exe_name_upper, uint32_t hash, char *out, size_t out_size )
{
    char prefix[PF_V17_MAX_EXE_CHARS + 1u];
    size_t i = 0;

    if (!out || out_size == 0)
        return;

    if (!exe_name_upper)
    {
        out[0] = '\0';
        return;
    }

    while (i < PF_V17_MAX_EXE_CHARS && exe_name_upper[i] != 0)
    {
        uint16_t ch = exe_name_upper[i];
        if (ch >= 'a' && ch <= 'z')
            ch = (uint16_t)(ch - 'a' + 'A');
        prefix[i] = (ch < 0x80u) ? (char)ch : '_';
        i++;
    }
    prefix[i] = '\0';

    snprintf( out, out_size, "%s-%08X.pf", prefix, (unsigned)hash );
    out[out_size - 1u] = '\0';
}

int pf_write_v17( const struct pf_info *info, uint8_t **out, size_t *out_len )
{
    size_t exe_len = 0;
    size_t metrics_offset;
    size_t metrics_size;
    size_t chains_offset;
    size_t chains_size;
    size_t strings_offset;
    size_t strings_size = 0;
    size_t volumes_offset;
    size_t volumes_size = 0;
    size_t total_size = 0;
    size_t cur_str_rel = 0;
    size_t cur_vol_rel = 0;
    uint8_t *buf = NULL;
    unsigned i;

    if (!out || !out_len)
    {
        errno = EINVAL;
        return -1;
    }
    *out = NULL;
    *out_len = 0;

    if (!info || !info->exe_name_utf16)
    {
        errno = EINVAL;
        return -1;
    }

    if (utf16_bounded_len( info->exe_name_utf16, PF_V17_MAX_EXE_CHARS, &exe_len ) != 0 || exe_len == 0)
    {
        errno = EINVAL;
        return -1;
    }

    if (info->file_count > PF_V17_MAX_FILES || info->volume_count > PF_V17_MAX_VOLUMES)
    {
        errno = EINVAL;
        return -1;
    }

    if ((info->file_count > 0 && !info->files) || (info->volume_count > 0 && !info->volumes))
    {
        errno = EINVAL;
        return -1;
    }

    for (i = 0; i < info->file_count; i++)
    {
        size_t flen = 0;
        size_t byte_len;

        if (utf16_bounded_len( info->files[i].path_utf16, PF_V17_MAX_STRING_CHARS, &flen ) != 0 || flen == 0)
        {
            errno = EINVAL;
            return -1;
        }
        byte_len = (flen + 1u) * 2u;
        if (checked_add_u32_size( strings_size, byte_len, &strings_size ) != 0)
        {
            errno = EOVERFLOW;
            return -1;
        }
    }

    if (info->volume_count > 0)
    {
        volumes_size = (size_t)info->volume_count * PF_V17_VOLUME_ENTRY_SIZE;
        for (i = 0; i < info->volume_count; i++)
        {
            size_t vlen = 0;
            size_t entry_data_bytes;

            if (utf16_bounded_len( info->volumes[i].device_path_utf16, PF_V17_MAX_STRING_CHARS, &vlen ) != 0 ||
                vlen == 0)
            {
                errno = EINVAL;
                return -1;
            }
            entry_data_bytes = (vlen + 1u) * 2u + PF_V17_FILE_REFS_STUB_SIZE;
            if (checked_add_u32_size( volumes_size, entry_data_bytes, &volumes_size ) != 0)
            {
                errno = EOVERFLOW;
                return -1;
            }
        }
    }

    metrics_offset = PF_V17_METRICS_OFFSET;
    metrics_size = (size_t)info->file_count * PF_V17_METRICS_ENTRY_SIZE;
    chains_size = (size_t)info->file_count * PF_V17_CHAIN_ENTRY_SIZE;

    if (checked_add_u32_size( metrics_offset, metrics_size, &chains_offset ) != 0 ||
        checked_add_u32_size( chains_offset, chains_size, &strings_offset ) != 0 ||
        checked_add_u32_size( strings_offset, strings_size, &volumes_offset ) != 0 ||
        checked_add_u32_size( volumes_offset, volumes_size, &total_size ) != 0)
    {
        errno = EOVERFLOW;
        return -1;
    }

    buf = calloc( 1u, total_size );
    if (!buf)
    {
        errno = ENOMEM;
        return -1;
    }

    /* File header (84 bytes) */
    put_u32_le( buf + 0, PF_V17_VERSION );
    put_u32_le( buf + 4, PF_V17_SIGNATURE );
    put_u32_le( buf + 8, PF_V17_HEADER_UNKNOWN );
    put_u32_le( buf + 12, (uint32_t)total_size );
    put_utf16le( buf + 16, info->exe_name_utf16, exe_len );
    put_u16_le( buf + 16 + (exe_len * 2u), 0u );
    put_u32_le( buf + 76, info->path_hash );
    put_u32_le( buf + 80, info->exe_flags );

    /* File information - version 17 (68 bytes, at offset 84) */
    put_u32_le( buf + 84 + 0, (uint32_t)metrics_offset );
    put_u32_le( buf + 84 + 4, (uint32_t)info->file_count );
    put_u32_le( buf + 84 + 8, (uint32_t)chains_offset );
    put_u32_le( buf + 84 + 12, (uint32_t)info->file_count );
    put_u32_le( buf + 84 + 16, (uint32_t)strings_offset );
    put_u32_le( buf + 84 + 20, (uint32_t)strings_size );
    put_u32_le( buf + 84 + 24, (uint32_t)volumes_offset );
    put_u32_le( buf + 84 + 28, (uint32_t)info->volume_count );
    put_u32_le( buf + 84 + 32, (uint32_t)volumes_size );
    put_u64_le( buf + 84 + 36, info->last_run_filetime );
    /* bytes 84+44 .. 84+59: 16 zero bytes (already zero from calloc) */
    put_u32_le( buf + 84 + 60, info->run_count );
    put_u32_le( buf + 84 + 64, 0u );

    /* Section A (metrics), Section B (trace chains), Section C (filename strings) */
    for (i = 0; i < info->file_count; i++)
    {
        size_t flen = 0;
        uint8_t *m_ent = buf + metrics_offset + ((size_t)i * PF_V17_METRICS_ENTRY_SIZE);
        uint8_t *c_ent = buf + chains_offset + ((size_t)i * PF_V17_CHAIN_ENTRY_SIZE);
        uint8_t *s_dst = buf + strings_offset + cur_str_rel;
        uint32_t blocks = info->chain_blocks ? info->chain_blocks[i] : 1u;
        uint8_t c_flags = (info->files[i].flags & 0x00000200u) ? 0x02u : 0x08u;

        (void)utf16_bounded_len( info->files[i].path_utf16, PF_V17_MAX_STRING_CHARS, &flen );

        put_u32_le( m_ent + 0, (uint32_t)i );
        put_u32_le( m_ent + 4, 1u );
        put_u32_le( m_ent + 8, (uint32_t)cur_str_rel );
        put_u32_le( m_ent + 12, (uint32_t)flen );
        put_u32_le( m_ent + 16, info->files[i].flags );

        put_u32_le( c_ent + 0, 0xffffffffu );
        put_u32_le( c_ent + 4, blocks );
        c_ent[8] = c_flags;
        c_ent[9] = 1u;
        c_ent[10] = 1u;
        c_ent[11] = 0u;

        put_utf16le( s_dst, info->files[i].path_utf16, flen );
        put_u16_le( s_dst + (flen * 2u), 0u );
        cur_str_rel += (flen + 1u) * 2u;
    }

    /* Section D (volumes information) */
    cur_vol_rel = (size_t)info->volume_count * PF_V17_VOLUME_ENTRY_SIZE;
    for (i = 0; i < info->volume_count; i++)
    {
        size_t vlen = 0;
        size_t dev_path_rel;
        size_t file_refs_rel;
        size_t dir_strings_rel;
        uint8_t *v_ent = buf + volumes_offset + ((size_t)i * PF_V17_VOLUME_ENTRY_SIZE);

        (void)utf16_bounded_len( info->volumes[i].device_path_utf16, PF_V17_MAX_STRING_CHARS, &vlen );

        dev_path_rel = cur_vol_rel;
        put_utf16le( buf + volumes_offset + dev_path_rel, info->volumes[i].device_path_utf16, vlen );
        put_u16_le( buf + volumes_offset + dev_path_rel + (vlen * 2u), 0u );
        cur_vol_rel += (vlen + 1u) * 2u;

        file_refs_rel = cur_vol_rel;
        put_u32_le( buf + volumes_offset + file_refs_rel + 0, 1u ); /* version = 1 */
        put_u32_le( buf + volumes_offset + file_refs_rel + 4, 0u ); /* count = 0 */
        cur_vol_rel += PF_V17_FILE_REFS_STUB_SIZE;

        dir_strings_rel = cur_vol_rel;

        put_u32_le( v_ent + 0, (uint32_t)dev_path_rel );
        put_u32_le( v_ent + 4, (uint32_t)vlen );
        put_u64_le( v_ent + 8, info->volumes[i].creation_filetime );
        put_u32_le( v_ent + 16, info->volumes[i].serial_number );
        put_u32_le( v_ent + 20, (uint32_t)file_refs_rel );
        put_u32_le( v_ent + 24, PF_V17_FILE_REFS_STUB_SIZE );
        put_u32_le( v_ent + 28, (uint32_t)dir_strings_rel );
        put_u32_le( v_ent + 32, 0u );
        put_u32_le( v_ent + 36, 0u );
    }

    *out = buf;
    *out_len = total_size;
    return 0;
}

/*
 * Readahead replay engine and page cache prefetching.
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

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include "readahead.h"

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/sysmacros.h>

enum ra_mode ra_mode_for_fd( int fd )
{
    const char *env = getenv( "WEXPF_READAHEAD" );
    if (env != NULL && *env != '\0')
    {
        if (strcasecmp( env, "serial" ) == 0)
            return RA_SERIAL;
        if (strcasecmp( env, "parallel" ) == 0)
            return RA_PARALLEL;
        if (strcasecmp( env, "skip" ) == 0)
            return RA_SKIP;
    }

    if (fd < 0)
        return RA_PARALLEL;

    struct stat st;
    if (fstat( fd, &st ) != 0)
        return RA_PARALLEL;

    unsigned int maj = major( st.st_dev );
    unsigned int min = minor( st.st_dev );

    char path[256];
    snprintf( path, sizeof(path), "/sys/dev/block/%u:%u/queue/rotational", maj, min );

    FILE *f = fopen( path, "r" );
    if (!f)
    {
        snprintf( path, sizeof(path), "/sys/dev/block/%u:%u/../queue/rotational", maj, min );
        f = fopen( path, "r" );
    }

    if (f)
    {
        char buf[16];
        if (fgets( buf, sizeof(buf), f ) != NULL)
        {
            fclose( f );
            if (buf[0] == '1')
                return RA_SERIAL;
            if (buf[0] == '0')
                return RA_PARALLEL;
        }
        else
        {
            fclose( f );
        }
    }

    return RA_PARALLEL;
}

size_t ra_resident_pages( int fd, uint64_t offset, uint64_t length )
{
    if (fd < 0)
        return (size_t)-1;
    if (length == 0)
        return 0;
    if (offset + length < offset)
        return (size_t)-1;

    long ps = sysconf( _SC_PAGESIZE );
    if (ps <= 0)
        ps = 4096;
    size_t page_size = (size_t)ps;

    uint64_t page_offset = offset % page_size;
    uint64_t aligned_offset = offset - page_offset;
    uint64_t total_span = length + page_offset;
    if (total_span < length)
        return (size_t)-1;

    size_t num_pages = (size_t)((total_span + page_size - 1) / page_size);
    size_t map_len = num_pages * page_size;

    struct stat st;
    if (fstat( fd, &st ) != 0)
        return (size_t)-1;
    if (aligned_offset >= (uint64_t)st.st_size)
        return 0;

    void *addr = mmap( NULL, map_len, PROT_READ, MAP_SHARED, fd, (off_t)aligned_offset );
    if (addr == MAP_FAILED)
        return (size_t)-1;

    unsigned char *vec = malloc( num_pages );
    if (!vec)
    {
        munmap( addr, map_len );
        return (size_t)-1;
    }

    if (mincore( addr, map_len, vec ) != 0)
    {
        free( vec );
        munmap( addr, map_len );
        return (size_t)-1;
    }

    size_t resident = 0;
    for (size_t i = 0; i < num_pages; i++)
    {
        if (vec[i] & 1)
            resident++;
    }

    free( vec );
    munmap( addr, map_len );
    return resident;
}

static int compare_ranges( const void *a, const void *b )
{
    const struct ra_range *ra = (const struct ra_range *)a;
    const struct ra_range *rb = (const struct ra_range *)b;

    if (ra->offset < rb->offset)
        return -1;
    if (ra->offset > rb->offset)
        return 1;
    if (ra->length < rb->length)
        return -1;
    if (ra->length > rb->length)
        return 1;
    return 0;
}

int ra_replay( const struct ra_item *items, unsigned item_count,
               enum ra_mode mode_override_or_auto, struct ra_stats *stats )
{
    struct ra_stats local_stats;
    if (stats)
        memset( stats, 0, sizeof(*stats) );
    else
        memset( &local_stats, 0, sizeof(local_stats) ), stats = &local_stats;

    if (!items && item_count > 0)
    {
        stats->errors++;
        return -1;
    }

    for (unsigned i = 0; i < item_count; i++)
    {
        if (stats->bytes_issued >= RA_MAX_TOTAL_BYTES)
            break;

        enum ra_mode mode = mode_override_or_auto;
        if (mode == RA_AUTO || (int)mode < 0 || mode > RA_SKIP)
            mode = ra_mode_for_fd( items[i].fd );

        if (mode == RA_SKIP)
        {
            stats->skipped_items++;
            continue;
        }

        if (items[i].fd < 0)
        {
            stats->errors++;
            continue;
        }

        struct stat st;
        if (fstat( items[i].fd, &st ) != 0)
        {
            stats->errors++;
            continue;
        }

        if (items[i].range_count == 0)
            continue;

        if (!items[i].ranges)
        {
            stats->errors++;
            continue;
        }

        uint64_t file_size = (st.st_size > 0) ? (uint64_t)st.st_size : 0;
        if (file_size == 0)
            continue;

        struct ra_range *copy = malloc( sizeof(struct ra_range) * items[i].range_count );
        if (!copy)
        {
            stats->errors++;
            continue;
        }

        unsigned valid_count = 0;
        for (unsigned j = 0; j < items[i].range_count; j++)
        {
            struct ra_range r = items[i].ranges[j];

            if (r.offset + r.length < r.offset) /* overflow */
            {
                stats->errors++;
                continue;
            }
            if (r.length == 0)
                continue;
            if (r.offset >= file_size) /* beyond file size */
                continue;
            if (r.offset + r.length > file_size)
                r.length = file_size - r.offset;
            if (r.length == 0)
                continue;

            copy[valid_count++] = r;
        }

        if (valid_count == 0)
        {
            free( copy );
            continue;
        }

        qsort( copy, valid_count, sizeof(struct ra_range), compare_ranges );

        /* Merge overlapping and adjacent ranges */
        unsigned merged_count = 0;
        for (unsigned j = 0; j < valid_count; j++)
        {
            if (merged_count == 0)
            {
                copy[merged_count++] = copy[j];
            }
            else
            {
                struct ra_range *prev = &copy[merged_count - 1];
                uint64_t prev_end = prev->offset + prev->length;
                if (copy[j].offset <= prev_end)
                {
                    uint64_t cur_end = copy[j].offset + copy[j].length;
                    if (cur_end > prev_end)
                        prev->length = cur_end - prev->offset;
                }
                else
                {
                    copy[merged_count++] = copy[j];
                }
            }
        }

        /* Issue readahead for merged ranges */
        for (unsigned j = 0; j < merged_count; j++)
        {
            struct ra_range *cur = &copy[j];

            if (cur->length > RA_MAX_RANGE_BYTES)
                cur->length = RA_MAX_RANGE_BYTES;

            if (stats->bytes_issued >= RA_MAX_TOTAL_BYTES)
                break;
            if (stats->bytes_issued + cur->length > RA_MAX_TOTAL_BYTES)
                cur->length = RA_MAX_TOTAL_BYTES - stats->bytes_issued;
            if (cur->length == 0)
                break;

            if (mode == RA_PARALLEL)
            {
                int rc = posix_fadvise( items[i].fd, (off_t)cur->offset, (off_t)cur->length,
                                        POSIX_FADV_WILLNEED );
                if (rc != 0)
                {
                    stats->errors++;
                }
                else
                {
                    stats->ranges_issued++;
                    stats->bytes_issued += cur->length;
                }
            }
            else /* RA_SERIAL */
            {
                ssize_t ret = readahead( items[i].fd, (off64_t)cur->offset, (size_t)cur->length );
                if (ret < 0)
                {
                    stats->errors++;
                }
                else
                {
                    stats->ranges_issued++;
                    stats->bytes_issued += cur->length;
                }
            }
        }

        free( copy );
    }

    return (stats->errors > 0) ? -1 : 0;
}

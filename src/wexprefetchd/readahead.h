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

#ifndef WEXPREFETCHD_READAHEAD_H
#define WEXPREFETCHD_READAHEAD_H

#include <stddef.h>
#include <stdint.h>

#define RA_MAX_RANGE_BYTES (64ULL * 1024 * 1024)
#define RA_MAX_TOTAL_BYTES (2ULL * 1024 * 1024 * 1024)

struct ra_range
{
    uint64_t offset;
    uint64_t length;
};

struct ra_item
{
    int fd; /* open O_RDONLY, owned by the caller */
    const struct ra_range *ranges;
    unsigned range_count;
};

enum ra_mode
{
    RA_SERIAL   = 0,
    RA_PARALLEL = 1,
    RA_SKIP     = 2,
    RA_AUTO     = 3
};

struct ra_stats
{
    uint64_t ranges_issued;
    uint64_t bytes_issued;
    uint64_t errors;
    uint64_t skipped_items;
};

enum ra_mode ra_mode_for_fd( int fd );

int ra_replay( const struct ra_item *items, unsigned item_count,
               enum ra_mode mode_override_or_auto, struct ra_stats *stats );

size_t ra_resident_pages( int fd, uint64_t offset, uint64_t length );

#endif /* WEXPREFETCHD_READAHEAD_H */

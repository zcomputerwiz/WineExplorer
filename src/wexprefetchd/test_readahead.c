/*
 * Unit tests for readahead replay engine and page cache prefetching.
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
#include <time.h>
#include <unistd.h>
#include <sys/stat.h>

#define FILE_SIZE_8MB (8ULL * 1024 * 1024)
#define HALF_SIZE_4MB (4ULL * 1024 * 1024)
#define PAGE_SZ       4096ULL
#define PAGES_4MB     (HALF_SIZE_4MB / PAGE_SZ) /* 1024 pages */

static int g_all_passed = 1;

static void report( const char *name, int ok, const char *detail )
{
    if (ok)
    {
        if (detail && *detail)
            printf( "PASS %s (%s)\n", name, detail );
        else
            printf( "PASS %s\n", name );
    }
    else
    {
        g_all_passed = 0;
        if (detail && *detail)
            printf( "FAIL %s (%s)\n", name, detail );
        else
            printf( "FAIL %s\n", name );
    }
}

static void skip( const char *name, const char *reason )
{
    printf( "SKIP %s (%s)\n", name, reason );
}

static int create_temp_file( char *out_path, size_t path_len, size_t size )
{
    const char *tmpdir = getenv( "TMPDIR" );
    if (!tmpdir || !*tmpdir)
        tmpdir = "/var/tmp";

    snprintf( out_path, path_len, "%s/wex_test_ra_XXXXXX", tmpdir );
    int fd = mkstemp( out_path );
    if (fd < 0)
    {
        perror( "mkstemp" );
        return -1;
    }

    char chunk[65536];
    for (size_t i = 0; i < sizeof(chunk); i++)
        chunk[i] = (char)(rand() & 0xff);

    size_t written = 0;
    while (written < size)
    {
        size_t to_write = sizeof(chunk);
        if (written + to_write > size)
            to_write = size - written;

        ssize_t n = write( fd, chunk, to_write );
        if (n <= 0)
        {
            perror( "write" );
            close( fd );
            unlink( out_path );
            return -1;
        }
        written += (size_t)n;
    }

    if (fsync( fd ) != 0)
    {
        perror( "fsync" );
        close( fd );
        unlink( out_path );
        return -1;
    }

    return fd;
}

static void poll_residency( int fd, uint64_t offset, uint64_t length,
                            size_t target_pages, int timeout_sec, size_t *final_pages )
{
    struct timespec start, now;
    clock_gettime( CLOCK_MONOTONIC, &start );

    while (1)
    {
        size_t res = ra_resident_pages( fd, offset, length );
        *final_pages = res;
        if (res >= target_pages)
            break;

        clock_gettime( CLOCK_MONOTONIC, &now );
        double elapsed = (double)(now.tv_sec - start.tv_sec) +
                         (double)(now.tv_nsec - start.tv_nsec) / 1e9;
        if (elapsed >= (double)timeout_sec)
            break;

        usleep( 20000 ); /* 20 ms */
    }
}

int main( void )
{
    char path[512];
    int fd = create_temp_file( path, sizeof(path), FILE_SIZE_8MB );
    if (fd < 0)
    {
        fprintf( stderr, "FATAL: Failed to create temp file\n" );
        return 1;
    }

    /* 1. Cache drop with posix_fadvise(POSIX_FADV_DONTNEED) */
    posix_fadvise( fd, 0, FILE_SIZE_8MB, POSIX_FADV_DONTNEED );
    size_t initial_res_first4 = ra_resident_pages( fd, 0, HALF_SIZE_4MB );
    int dontneed_supported = (initial_res_first4 <= (PAGES_4MB / 4)); /* <= 25% */

    if (!dontneed_supported)
    {
        skip( "initial cache drop", "POSIX_FADV_DONTNEED had no effect on this filesystem" );
    }
    else
    {
        char detail[64];
        snprintf( detail, sizeof(detail), "%zu / %zu pages resident", initial_res_first4, (size_t)PAGES_4MB );
        report( "initial cache drop", 1, detail );
    }

    /* 2. Replay range [0, 4 MiB) in RA_PARALLEL */
    if (dontneed_supported)
    {
        struct ra_range r_parallel = { 0, HALF_SIZE_4MB };
        struct ra_item item_parallel = { fd, &r_parallel, 1 };
        struct ra_stats stats_par = {0};

        int rc = ra_replay( &item_parallel, 1, RA_PARALLEL, &stats_par );
        int stats_ok = (rc == 0 && stats_par.ranges_issued == 1 &&
                        stats_par.bytes_issued == HALF_SIZE_4MB && stats_par.errors == 0);

        size_t res_par = 0;
        size_t target_90 = (size_t)(PAGES_4MB * 9 / 10); /* 90% = 921 pages */
        poll_residency( fd, 0, HALF_SIZE_4MB, target_90, 5, &res_par );

        size_t other_res = ra_resident_pages( fd, HALF_SIZE_4MB, HALF_SIZE_4MB );
        size_t cap_25 = PAGES_4MB / 4; /* 25% = 256 pages */

        char detail[128];
        snprintf( detail, sizeof(detail), "target: %zu/%zu, other: %zu/%zu",
                  res_par, (size_t)PAGES_4MB, other_res, (size_t)PAGES_4MB );

        if (res_par >= target_90 && other_res <= cap_25 && stats_ok)
        {
            report( "replay RA_PARALLEL [0, 4 MiB)", 1, detail );
        }
        else if (res_par < target_90)
        {
            skip( "replay RA_PARALLEL [0, 4 MiB)", "readahead did not fault >= 90% within 5s" );
        }
        else
        {
            report( "replay RA_PARALLEL [0, 4 MiB)", 0, detail );
        }
    }
    else
    {
        skip( "replay RA_PARALLEL [0, 4 MiB)", "cache drop not supported" );
    }

    /* 3. Drop cache again and replay in RA_SERIAL */
    if (dontneed_supported)
    {
        posix_fadvise( fd, 0, FILE_SIZE_8MB, POSIX_FADV_DONTNEED );
        size_t res_after_drop = ra_resident_pages( fd, 0, HALF_SIZE_4MB );

        if (res_after_drop > PAGES_4MB / 4)
        {
            skip( "replay RA_SERIAL [0, 4 MiB)", "cache drop had no effect on 2nd attempt" );
        }
        else
        {
            struct ra_range r_serial = { 0, HALF_SIZE_4MB };
            struct ra_item item_serial = { fd, &r_serial, 1 };
            struct ra_stats stats_ser = {0};

            int rc = ra_replay( &item_serial, 1, RA_SERIAL, &stats_ser );
            int stats_ok = (rc == 0 && stats_ser.ranges_issued == 1 &&
                            stats_ser.bytes_issued == HALF_SIZE_4MB && stats_ser.errors == 0);

            size_t res_ser = 0;
            size_t target_90 = (size_t)(PAGES_4MB * 9 / 10);
            poll_residency( fd, 0, HALF_SIZE_4MB, target_90, 5, &res_ser );

            size_t other_res = ra_resident_pages( fd, HALF_SIZE_4MB, HALF_SIZE_4MB );
            size_t cap_25 = PAGES_4MB / 4;

            char detail[128];
            snprintf( detail, sizeof(detail), "target: %zu/%zu, other: %zu/%zu",
                      res_ser, (size_t)PAGES_4MB, other_res, (size_t)PAGES_4MB );

            if (res_ser >= target_90 && other_res <= cap_25 && stats_ok)
            {
                report( "replay RA_SERIAL [0, 4 MiB)", 1, detail );
            }
            else if (res_ser < target_90)
            {
                skip( "replay RA_SERIAL [0, 4 MiB)", "readahead did not fault >= 90% within 5s" );
            }
            else
            {
                report( "replay RA_SERIAL [0, 4 MiB)", 0, detail );
            }
        }
    }
    else
    {
        skip( "replay RA_SERIAL [0, 4 MiB)", "cache drop not supported" );
    }

    /* 4. Test merging overlapping and adjacent ranges */
    {
        /*
         * Range 0: [0, 1 MiB)
         * Range 1: [512 KiB, 2 MiB)  (overlaps Range 0)
         * Range 2: [2 MiB, 3 MiB)    (adjacent to merged Range 0+1)
         * Expected merged: 1 range [0, 3 MiB), bytes = 3 MiB
         */
        struct ra_range merge_ranges[3] = {
            { 0, 1 * 1024 * 1024 },
            { 512 * 1024, 1536 * 1024 },
            { 2 * 1024 * 1024, 1 * 1024 * 1024 }
        };
        struct ra_item item = { fd, merge_ranges, 3 };
        struct ra_stats stats = {0};

        int rc = ra_replay( &item, 1, RA_PARALLEL, &stats );
        int ok = (rc == 0 && stats.ranges_issued == 1 &&
                  stats.bytes_issued == 3 * 1024 * 1024 && stats.errors == 0);
        char detail[64];
        snprintf( detail, sizeof(detail), "ranges_issued=%llu, bytes=%llu",
                  (unsigned long long)stats.ranges_issued, (unsigned long long)stats.bytes_issued );
        report( "range merging (overlapping & adjacent)", ok, detail );
    }

    /* 5. Test range cap (64 MiB limit) */
    {
        char cap_path[512];
        const char *tmpdir = getenv( "TMPDIR" );
        if (!tmpdir || !*tmpdir)
            tmpdir = "/var/tmp";
        snprintf( cap_path, sizeof(cap_path), "%s/wex_test_cap_XXXXXX", tmpdir );
        int cap_fd = mkstemp( cap_path );
        if (cap_fd >= 0)
        {
            /* Make a sparse file of 128 MiB */
            if (ftruncate( cap_fd, 128ULL * 1024 * 1024 ) == 0)
            {
                struct ra_range big_range = { 0, 100ULL * 1024 * 1024 };
                struct ra_item cap_item = { cap_fd, &big_range, 1 };
                struct ra_stats stats = {0};

                int rc = ra_replay( &cap_item, 1, RA_PARALLEL, &stats );
                int ok = (rc == 0 && stats.ranges_issued == 1 &&
                          stats.bytes_issued == RA_MAX_RANGE_BYTES && stats.errors == 0);
                char detail[64];
                snprintf( detail, sizeof(detail), "bytes_issued=%llu (cap 64 MiB)",
                          (unsigned long long)stats.bytes_issued );
                report( "range cap at 64 MiB", ok, detail );
            }
            else
            {
                skip( "range cap at 64 MiB", "ftruncate failed" );
            }
            close( cap_fd );
            unlink( cap_path );
        }
        else
        {
            skip( "range cap at 64 MiB", "mkstemp failed" );
        }
    }

    /* 6. Test total bytes cap per call (2 GiB limit) */
    {
        char cap2g_path[512];
        const char *tmpdir = getenv( "TMPDIR" );
        if (!tmpdir || !*tmpdir)
            tmpdir = "/var/tmp";
        snprintf( cap2g_path, sizeof(cap2g_path), "%s/wex_test_cap2g_XXXXXX", tmpdir );
        int cap2g_fd = mkstemp( cap2g_path );
        if (cap2g_fd >= 0)
        {
            /* Make a sparse file of 3 GiB */
            if (ftruncate( cap2g_fd, 3ULL * 1024 * 1024 * 1024 ) == 0)
            {
                /* 40 non-contiguous ranges of 64 MiB = 2.5 GiB */
                struct ra_range many_ranges[40];
                for (unsigned i = 0; i < 40; i++)
                {
                    many_ranges[i].offset = (uint64_t)i * (64ULL * 1024 * 1024 + 4096);
                    many_ranges[i].length = 64ULL * 1024 * 1024;
                }
                struct ra_item cap2g_item = { cap2g_fd, many_ranges, 40 };
                struct ra_stats stats = {0};

                int rc = ra_replay( &cap2g_item, 1, RA_PARALLEL, &stats );
                int ok = (rc == 0 && stats.bytes_issued == RA_MAX_TOTAL_BYTES && stats.errors == 0);
                char detail[64];
                snprintf( detail, sizeof(detail), "bytes_issued=%llu (cap 2 GiB)",
                          (unsigned long long)stats.bytes_issued );
                report( "total bytes cap at 2 GiB", ok, detail );
            }
            else
            {
                skip( "total bytes cap at 2 GiB", "ftruncate 3 GiB failed" );
            }
            close( cap2g_fd );
            unlink( cap2g_path );
        }
        else
        {
            skip( "total bytes cap at 2 GiB", "mkstemp failed" );
        }
    }

    /* 7. Test out-of-file ranges are ignored & clipped */
    {
        /*
         * Range 0: [10 MiB, 2 MiB) -> starts beyond 8 MiB file size -> ignored
         * Range 1: [6 MiB, 4 MiB)  -> starts at 6 MiB, clipped to 8 MiB -> length 2 MiB
         * Range 2: [20 MiB, 1 MiB) -> starts beyond EOF -> ignored
         */
        struct ra_range oof_ranges[3] = {
            { 10ULL * 1024 * 1024, 2ULL * 1024 * 1024 },
            { 6ULL * 1024 * 1024, 4ULL * 1024 * 1024 },
            { 20ULL * 1024 * 1024, 1ULL * 1024 * 1024 }
        };
        struct ra_item item = { fd, oof_ranges, 3 };
        struct ra_stats stats = {0};

        int rc = ra_replay( &item, 1, RA_PARALLEL, &stats );
        int ok = (rc == 0 && stats.ranges_issued == 1 &&
                  stats.bytes_issued == 2ULL * 1024 * 1024 && stats.errors == 0);
        char detail[64];
        snprintf( detail, sizeof(detail), "ranges_issued=%llu, bytes=%llu",
                  (unsigned long long)stats.ranges_issued, (unsigned long long)stats.bytes_issued );
        report( "out-of-file ranges ignored & clipped", ok, detail );
    }

    /* 8. Test RA_SKIP */
    {
        struct ra_range r = { 0, 1024 * 1024 };
        struct ra_item items[3] = {
            { fd, &r, 1 },
            { fd, &r, 1 },
            { fd, &r, 1 }
        };
        struct ra_stats stats = {0};

        int rc = ra_replay( items, 3, RA_SKIP, &stats );
        int ok = (rc == 0 && stats.skipped_items == 3 &&
                  stats.ranges_issued == 0 && stats.bytes_issued == 0 && stats.errors == 0);
        char detail[64];
        snprintf( detail, sizeof(detail), "skipped_items=%llu", (unsigned long long)stats.skipped_items );
        report( "RA_SKIP mode", ok, detail );
    }

    /* 9. Test environment override WEXPF_READAHEAD */
    {
        int ok = 1;

        setenv( "WEXPF_READAHEAD", "skip", 1 );
        if (ra_mode_for_fd( fd ) != RA_SKIP)
            ok = 0;

        struct ra_range r = { 0, 1024 * 1024 };
        struct ra_item item = { fd, &r, 1 };
        struct ra_stats stats = {0};
        ra_replay( &item, 1, RA_AUTO, &stats );
        if (stats.skipped_items != 1)
            ok = 0;

        setenv( "WEXPF_READAHEAD", "serial", 1 );
        if (ra_mode_for_fd( fd ) != RA_SERIAL)
            ok = 0;

        setenv( "WEXPF_READAHEAD", "parallel", 1 );
        if (ra_mode_for_fd( fd ) != RA_PARALLEL)
            ok = 0;

        unsetenv( "WEXPF_READAHEAD" );

        report( "environment override WEXPF_READAHEAD", ok, "serial/parallel/skip" );
    }

    /* 10. Test validation: invalid fd, NULL pointers, overflow */
    {
        int ok = 1;

        /* Invalid fd */
        struct ra_range r = { 0, 4096 };
        struct ra_item bad_item = { -1, &r, 1 };
        struct ra_stats stats = {0};
        int rc = ra_replay( &bad_item, 1, RA_PARALLEL, &stats );
        if (rc != -1 || stats.errors != 1 || stats.ranges_issued != 0)
            ok = 0;

        /* Closed bad fd */
        bad_item.fd = 99999;
        memset( &stats, 0, sizeof(stats) );
        rc = ra_replay( &bad_item, 1, RA_PARALLEL, &stats );
        if (rc != -1 || stats.errors != 1 || stats.ranges_issued != 0)
            ok = 0;

        /* NULL items with positive count */
        memset( &stats, 0, sizeof(stats) );
        rc = ra_replay( NULL, 5, RA_PARALLEL, &stats );
        if (rc != -1 || stats.errors != 1)
            ok = 0;

        /* NULL ranges with positive range count */
        struct ra_item null_ranges_item = { fd, NULL, 5 };
        memset( &stats, 0, sizeof(stats) );
        rc = ra_replay( &null_ranges_item, 1, RA_PARALLEL, &stats );
        if (rc != -1 || stats.errors != 1)
            ok = 0;

        /* Overflow in offset + length */
        struct ra_range overflow_range = { UINT64_MAX - 100, 200 };
        struct ra_item overflow_item = { fd, &overflow_range, 1 };
        memset( &stats, 0, sizeof(stats) );
        rc = ra_replay( &overflow_item, 1, RA_PARALLEL, &stats );
        if (rc != -1 || stats.errors != 1 || stats.ranges_issued != 0)
            ok = 0;

        /* Safe handling of NULL stats */
        struct ra_range valid_r = { 0, 4096 };
        struct ra_item valid_item = { fd, &valid_r, 1 };
        rc = ra_replay( &valid_item, 1, RA_PARALLEL, NULL );
        if (rc != 0)
            ok = 0;

        report( "caller validation (bad fd, NULL pointers, overflow)", ok, "all checked" );
    }

    /* 11. Test ra_mode_for_fd on temp file */
    {
        unsetenv( "WEXPF_READAHEAD" );
        enum ra_mode mode = ra_mode_for_fd( fd );
        int ok = (mode == RA_SERIAL || mode == RA_PARALLEL);
        const char *name = (mode == RA_SERIAL) ? "RA_SERIAL (rotational)" : "RA_PARALLEL (non-rotational/unknown)";
        report( "ra_mode_for_fd on temp file", ok, name );
    }

    close( fd );
    unlink( path );

    printf( "\nOverall result: %s\n", g_all_passed ? "ALL PASSED" : "FAILURES OCCURRED" );
    return g_all_passed ? 0 : 1;
}

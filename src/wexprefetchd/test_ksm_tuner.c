/*
 * Unit tests for the adaptive KSM tuner: the pure control logic (threshold, boost/decay, run/stop, sleep)
 * and the sysfs I/O helpers against a fake directory tree under /var/tmp.
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

#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include "ksm_tuner.h"

static int check( const char *name, int ok )
{
    if (ok)
        printf( "PASS %s\n", name );
    else
        printf( "FAIL %s\n", name );
    return ok;
}

static int write_text( const char *path, const char *content )
{
    FILE *f = fopen( path, "w" );
    if (f == NULL)
        return -1;
    if (fputs( content, f ) == EOF)
    {
        fclose( f );
        return -1;
    }
    if (fclose( f ) != 0)
        return -1;
    return 0;
}

static int read_text( const char *path, char *buf, size_t bufsz )
{
    FILE *f = fopen( path, "r" );
    size_t n;
    if (f == NULL)
        return -1;
    n = fread( buf, 1, bufsz - 1, f );
    buf[n] = '\0';
    fclose( f );
    return 0;
}

static int path_join( char *out, size_t outsz, const char *dir, const char *name )
{
    int n = snprintf( out, outsz, "%s/%s", dir, name );
    return (n > 0 && n < (int)outsz) ? 0 : -1;
}

int main( void )
{
    struct ksm_cfg cfg;
    struct ksm_state st;
    struct ksm_sample s;
    int ok = 1;
    int i;

    ksm_cfg_defaults( &cfg );

    /* --- defaults --- */
    ok &= check( "defaults",
                 cfg.npages_min == 64 && cfg.npages_max == 1250 &&
                 cfg.npages_boost == 300 && cfg.npages_decay == 50 &&
                 cfg.thres_coef_pct == 20 && cfg.thres_const_kb == 2097152 &&
                 cfg.sleep_base_ms == 10 && cfg.calm_steps_to_stop == 10 &&
                 cfg.use_kernel_advisor == 0 );

    /* --- threshold math --- */
    ok &= check( "threshold 16 GiB is 3355443", ksm_threshold_kb( &cfg, 16777216 ) == 3355443 );
    ok &= check( "threshold 4 GiB raised to 2097152", ksm_threshold_kb( &cfg, 4194304 ) == 2097152 );
    ok &= check( "threshold UINT64_MAX/100 no overflow",
                 ksm_threshold_kb( &cfg, UINT64_MAX / 100 ) == 36893488147419103ULL );
    ok &= check( "threshold UINT64_MAX no overflow",
                 ksm_threshold_kb( &cfg, UINT64_MAX ) == 3689348814741910323ULL );

    /* pressure boundary at the exact threshold (16 GiB) */
    s.mem_total_kb = 16777216;
    s.mem_available_kb = 3355442;
    s.pages_sharing = 0;
    ksm_state_init( &cfg, &st, &s );
    ok &= check( "init under pressure",
                 st.run == 1 && st.pages_to_scan == 64 && st.calm_steps == 0 && st.sleep_ms == 10 );
    s.mem_available_kb = 3355443;
    ksm_tune_step( &cfg, &st, &s );
    ok &= check( "no pressure at exact threshold",
                 st.run == 1 && st.calm_steps == 1 && st.pages_to_scan == 64 );

    /* pressure boundary at the raised threshold (4 GiB) */
    s.mem_total_kb = 4194304;
    s.mem_available_kb = 2097151;
    ksm_state_init( &cfg, &st, &s );
    ok &= check( "4 GiB below raised threshold is pressure", st.run == 1 );
    s.mem_available_kb = 2097152;
    ksm_tune_step( &cfg, &st, &s );
    ok &= check( "4 GiB at raised threshold is calm",
                 st.run == 1 && st.calm_steps == 1 && st.pages_to_scan == 64 && st.sleep_ms == 40 );

    /* --- boost to the maximum and saturation under sustained pressure --- */
    s.mem_total_kb = 16777216;
    s.mem_available_kb = 1000;
    s.pages_sharing = 0;
    ksm_state_init( &cfg, &st, &s );
    ksm_tune_step( &cfg, &st, &s );
    ok &= check( "boost 64 -> 364", st.pages_to_scan == 364 );
    ksm_tune_step( &cfg, &st, &s );
    ksm_tune_step( &cfg, &st, &s );
    ok &= check( "boost 364 -> 664 -> 964", st.pages_to_scan == 964 );
    ksm_tune_step( &cfg, &st, &s );
    ok &= check( "boost saturates at npages_max", st.pages_to_scan == 1250 );
    ksm_tune_step( &cfg, &st, &s );
    ok &= check( "sustained pressure stays at npages_max",
                 st.pages_to_scan == 1250 && st.run == 1 && st.calm_steps == 0 );

    /* --- decay to the minimum under calm --- */
    s.mem_available_kb = 16000000;
    for (i = 0; i < 8; i++)
        ksm_tune_step( &cfg, &st, &s );
    ok &= check( "decay 1250 -> 850 after 8 calm steps",
                 st.pages_to_scan == 850 && st.run == 1 && st.calm_steps == 8 );
    for (i = 0; i < 16; i++)
        ksm_tune_step( &cfg, &st, &s );
    ok &= check( "decay to npages_min", st.pages_to_scan == 64 );
    ok &= check( "run stopped after 24 calm steps", st.run == 0 );

    /* --- no wrap with extreme boost/decay --- */
    {
        struct ksm_cfg big = cfg;
        big.npages_boost = UINT32_MAX;
        big.npages_decay = UINT32_MAX;
        s.mem_available_kb = 1000;
        ksm_state_init( &big, &st, &s );
        ksm_tune_step( &big, &st, &s );
        ok &= check( "boost UINT32_MAX saturates, no wrap", st.pages_to_scan == 1250 );
        s.mem_available_kb = 16000000;
        ksm_tune_step( &big, &st, &s );
        ok &= check( "decay UINT32_MAX floors, no underflow", st.pages_to_scan == 64 );
        st.pages_to_scan = 1000;
        ksm_tune_step( &big, &st, &s );
        ok &= check( "decay UINT32_MAX from mid range", st.pages_to_scan == 64 );
        s.mem_available_kb = 1000;
        st.pages_to_scan = 1000;
        ksm_tune_step( &big, &st, &s );
        ok &= check( "boost UINT32_MAX from mid range", st.pages_to_scan == 1250 );
    }

    /* --- run stops only after calm_steps_to_stop calm steps with pages_sharing == 0 --- */
    s.mem_total_kb = 16777216;
    s.mem_available_kb = 1000;
    s.pages_sharing = 0;
    ksm_state_init( &cfg, &st, &s );
    s.mem_available_kb = 16000000;
    for (i = 0; i < 9; i++)
        ksm_tune_step( &cfg, &st, &s );
    ok &= check( "run still 1 after 9 calm steps", st.run == 1 && st.calm_steps == 9 );
    ksm_tune_step( &cfg, &st, &s );
    ok &= check( "run 0 after the 10th calm step", st.run == 0 && st.calm_steps == 10 );
    ksm_tune_step( &cfg, &st, &s );
    ok &= check( "run stays 0 while calm", st.run == 0 );
    s.mem_available_kb = 1000;
    ksm_tune_step( &cfg, &st, &s );
    ok &= check( "run restarts on pressure", st.run == 1 && st.calm_steps == 0 );

    /* run stays 1 while pages_sharing > 0, even past calm_steps_to_stop */
    s.mem_available_kb = 1000;
    s.pages_sharing = 5;
    ksm_state_init( &cfg, &st, &s );
    s.mem_available_kb = 16000000;
    for (i = 0; i < 15; i++)
        ksm_tune_step( &cfg, &st, &s );
    ok &= check( "run stays 1 while pages_sharing > 0", st.run == 1 && st.calm_steps == 15 );
    s.pages_sharing = 0;
    ksm_tune_step( &cfg, &st, &s );
    ok &= check( "run stops once pages_sharing is 0", st.run == 0 );

    /* --- sleep_ms formula --- */
    s.mem_available_kb = 0;
    s.pages_sharing = 0;
    s.mem_total_kb = 16777216;
    ksm_state_init( &cfg, &st, &s );
    ok &= check( "sleep 16 GiB is 10", st.sleep_ms == 10 );
    s.mem_total_kb = 67108864;
    ksm_state_init( &cfg, &st, &s );
    ok &= check( "sleep 64 GiB clamped to 10", st.sleep_ms == 10 );
    s.mem_total_kb = 1048576;
    ksm_state_init( &cfg, &st, &s );
    ok &= check( "sleep 1 GiB is 160", st.sleep_ms == 160 );
    s.mem_total_kb = 131072;
    ksm_state_init( &cfg, &st, &s );
    ok &= check( "sleep 128 MiB is 1280", st.sleep_ms == 1280 );
    s.mem_total_kb = 0;
    ksm_state_init( &cfg, &st, &s );
    ok &= check( "sleep 0 total is 10000", st.sleep_ms == 10000 );
    s.mem_total_kb = 1000;
    ksm_state_init( &cfg, &st, &s );
    ok &= check( "sleep tiny total clamped to 10000", st.sleep_ms == 10000 );
    s.mem_total_kb = UINT64_MAX / 100;
    ksm_state_init( &cfg, &st, &s );
    ok &= check( "sleep huge total no overflow", st.sleep_ms == 10 );

    /* --- clamp of externally set state --- */
    s.mem_total_kb = 16777216;
    s.mem_available_kb = 16000000;
    s.pages_sharing = 0;
    ksm_state_init( &cfg, &st, &s );
    st.pages_to_scan = 999999;
    ksm_tune_step( &cfg, &st, &s );
    ok &= check( "high external state clamped to npages_max", st.pages_to_scan == 1250 );
    st.pages_to_scan = 1;
    ksm_tune_step( &cfg, &st, &s );
    ok &= check( "low external state clamped to npages_min", st.pages_to_scan == 64 );

    /* --- I/O helpers against a fake tree --- */
    {
        char top[64];
        char ksm_dir[128], meminfo_path[128];
        char p_run[256], p_p2s[256], p_sleep[256], p_sharing[256], p_adv[256], p_empty[128];
        struct ksm_sample got;
        struct ksm_state last;
        struct ksm_state st2;
        struct ksm_cfg adv = cfg;
        char buf[64];

        snprintf( top, sizeof(top), "/var/tmp/ksm_tuner_test_XXXXXX" );
        if (mkdtemp( top ) == NULL)
        {
            printf( "FAIL mkdtemp under /var/tmp\n" );
            return 1;
        }
        if (path_join( ksm_dir, sizeof(ksm_dir), top, "ksm" ) ||
            path_join( meminfo_path, sizeof(meminfo_path), top, "meminfo" ) ||
            path_join( p_run, sizeof(p_run), ksm_dir, "run" ) ||
            path_join( p_p2s, sizeof(p_p2s), ksm_dir, "pages_to_scan" ) ||
            path_join( p_sleep, sizeof(p_sleep), ksm_dir, "sleep_millisecs" ) ||
            path_join( p_sharing, sizeof(p_sharing), ksm_dir, "pages_sharing" ) ||
            path_join( p_adv, sizeof(p_adv), ksm_dir, "advisor_mode" ) ||
            path_join( p_empty, sizeof(p_empty), top, "empty" ) ||
            mkdir( ksm_dir, 0755 ) != 0 ||
            mkdir( p_empty, 0755 ) != 0)
        {
            printf( "FAIL setting up the fake tree\n" );
            return 1;
        }

        /* ksm_read_sample */
        write_text( meminfo_path,
                    "MemTotal:       16384000 kB\n"
                    "MemFree:         1000000 kB\n"
                    "MemAvailable:   12345678 kB\n"
                    "Buffers:          100000 kB\n" );
        write_text( p_sharing, "42\n" );
        ok &= check( "read_sample ok",
                     ksm_read_sample( meminfo_path, ksm_dir, &got ) == 0 &&
                     got.mem_total_kb == 16384000 &&
                     got.mem_available_kb == 12345678 &&
                     got.pages_sharing == 42 );
        unlink( p_sharing );
        ok &= check( "read_sample missing pages_sharing is 0",
                     ksm_read_sample( meminfo_path, ksm_dir, &got ) == 0 &&
                     got.pages_sharing == 0 );
        write_text( meminfo_path,
                    "MemTotal:       16384000 kB\n"
                    "MemFree:         1000000 kB\n"
                    "Buffers:          100000 kB\n" );
        ok &= check( "read_sample missing MemAvailable is -1",
                     ksm_read_sample( meminfo_path, ksm_dir, &got ) == -1 );

        /* ksm_apply: only changed values are rewritten */
        write_text( p_run, "0\n" );
        write_text( p_p2s, "0\n" );
        write_text( p_sleep, "0\n" );
        write_text( p_sharing, "0\n" );
        write_text( p_adv, "[none] scan-time\n" );
        memset( &last, 0, sizeof(last) );
        last.run = -1;
        st2.pages_to_scan = 64;
        st2.sleep_ms = 10;
        st2.run = 1;
        st2.calm_steps = 0;
        ok &= check( "apply first call writes all three", ksm_apply( ksm_dir, &cfg, &st2, &last ) == 3 );
        ok &= check( "apply wrote run",
                     read_text( p_run, buf, sizeof(buf) ) == 0 && strcmp( buf, "1\n" ) == 0 );
        ok &= check( "apply wrote pages_to_scan",
                     read_text( p_p2s, buf, sizeof(buf) ) == 0 && strcmp( buf, "64\n" ) == 0 );
        ok &= check( "apply wrote sleep_millisecs",
                     read_text( p_sleep, buf, sizeof(buf) ) == 0 && strcmp( buf, "10\n" ) == 0 );
        ok &= check( "apply second call writes nothing", ksm_apply( ksm_dir, &cfg, &st2, &last ) == 0 );
        st2.pages_to_scan = 300;
        ok &= check( "apply writes only the changed value", ksm_apply( ksm_dir, &cfg, &st2, &last ) == 1 );
        ok &= check( "apply rewrote pages_to_scan",
                     read_text( p_p2s, buf, sizeof(buf) ) == 0 && strcmp( buf, "300\n" ) == 0 );
        ok &= check( "apply left run alone",
                     read_text( p_run, buf, sizeof(buf) ) == 0 && strcmp( buf, "1\n" ) == 0 );
        ok &= check( "apply left sleep_millisecs alone",
                     read_text( p_sleep, buf, sizeof(buf) ) == 0 && strcmp( buf, "10\n" ) == 0 );
        st2.run = 0;
        st2.sleep_ms = 100;
        ok &= check( "apply writes two changed values", ksm_apply( ksm_dir, &cfg, &st2, &last ) == 2 );
        st2.pages_to_scan = 500;
        ok &= check( "apply fails on a missing file", ksm_apply( p_empty, &cfg, &st2, &last ) == -1 );

        /* ksm_apply with the kernel advisor */
        adv.use_kernel_advisor = 1;
        write_text( p_adv, "[none] scan-time\n" );
        memset( &last, 0, sizeof(last) );
        last.run = -1;
        st2.run = 1;
        ok &= check( "advisor first call writes scan-time and run",
                     ksm_apply( ksm_dir, &adv, &st2, &last ) == 2 );
        ok &= check( "advisor wrote scan-time",
                     read_text( p_adv, buf, sizeof(buf) ) == 0 && strcmp( buf, "scan-time\n" ) == 0 );
        ok &= check( "advisor forced run 1",
                     read_text( p_run, buf, sizeof(buf) ) == 0 && strcmp( buf, "1\n" ) == 0 );
        ok &= check( "advisor second call writes nothing", ksm_apply( ksm_dir, &adv, &st2, &last ) == 0 );
        write_text( p_adv, "none [scan-time]\n" );
        memset( &last, 0, sizeof(last) );
        last.run = -1;
        ok &= check( "advisor already selected writes only run",
                     ksm_apply( ksm_dir, &adv, &st2, &last ) == 1 );
        unlink( p_adv );
        memset( &last, 0, sizeof(last) );
        last.run = -1;
        ok &= check( "advisor without advisor_mode file writes all three",
                     ksm_apply( ksm_dir, &adv, &st2, &last ) == 3 );

        /* remove the temp tree */
        unlink( p_run );
        unlink( p_p2s );
        unlink( p_sleep );
        unlink( p_sharing );
        unlink( p_adv );
        rmdir( ksm_dir );
        rmdir( p_empty );
        rmdir( top );
    }

    return ok ? 0 : 1;
}

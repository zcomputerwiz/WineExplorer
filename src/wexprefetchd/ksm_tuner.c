/*
 * Adaptive KSM tuner: pure control logic (threshold, boost/decay, run/stop, sleep) and the small sysfs I/O
 * helpers (ksm_read_sample, ksm_apply).
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

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "ksm_tuner.h"

#define KSM_SLEEP_MEM_KB  16777216u  /* 16 GiB in kB: the reference memory size of the sleep formula */
#define KSM_SLEEP_MIN_MS  10u
#define KSM_SLEEP_MAX_MS  10000u

void ksm_cfg_defaults( struct ksm_cfg *cfg )
{
    cfg->npages_min = 64;
    cfg->npages_max = 1250;
    cfg->npages_boost = 300;
    cfg->npages_decay = 50;
    cfg->thres_coef_pct = 20;
    cfg->thres_const_kb = 2097152;
    cfg->sleep_base_ms = 10;
    cfg->calm_steps_to_stop = 10;
    cfg->use_kernel_advisor = 0;
}

uint64_t ksm_threshold_kb( const struct ksm_cfg *cfg, uint64_t mem_total_kb )
{
    uint64_t pct = cfg->thres_coef_pct;
    uint64_t threshold;

    /* mem_total_kb * pct / 100 in 64-bit math without overflowing. */
    threshold = (mem_total_kb / 100) * pct + ((mem_total_kb % 100) * pct) / 100;
    if (threshold < cfg->thres_const_kb)
        threshold = cfg->thres_const_kb;
    return threshold;
}

uint32_t ksm_sleep_ms( const struct ksm_cfg *cfg, uint64_t mem_total_kb )
{
    uint64_t ms;

    if (mem_total_kb == 0)
        return KSM_SLEEP_MAX_MS;
    ms = (uint64_t)cfg->sleep_base_ms * KSM_SLEEP_MEM_KB / mem_total_kb;
    if (ms < KSM_SLEEP_MIN_MS)
        ms = KSM_SLEEP_MIN_MS;
    if (ms > KSM_SLEEP_MAX_MS)
        ms = KSM_SLEEP_MAX_MS;
    return (uint32_t)ms;
}

void ksm_state_init( const struct ksm_cfg *cfg, struct ksm_state *st, const struct ksm_sample *first )
{
    st->pages_to_scan = cfg->npages_min;
    st->sleep_ms = ksm_sleep_ms( cfg, first->mem_total_kb );
    st->run = (first->mem_available_kb < ksm_threshold_kb( cfg, first->mem_total_kb )) ? 1 : 0;
    st->calm_steps = 0;
}

void ksm_tune_step( const struct ksm_cfg *cfg, struct ksm_state *st, const struct ksm_sample *s )
{
    if (s->mem_available_kb < ksm_threshold_kb( cfg, s->mem_total_kb ))
    {
        uint64_t v = (uint64_t)st->pages_to_scan + cfg->npages_boost;

        st->calm_steps = 0;
        st->pages_to_scan = (v > cfg->npages_max) ? cfg->npages_max : (uint32_t)v;
        st->run = 1;
    }
    else
    {
        if (st->calm_steps != UINT32_MAX)
            st->calm_steps++;
        if (st->pages_to_scan > cfg->npages_min &&
            cfg->npages_decay >= st->pages_to_scan - cfg->npages_min)
            st->pages_to_scan = cfg->npages_min;
        else if (st->pages_to_scan > cfg->npages_min)
            st->pages_to_scan -= cfg->npages_decay;
        /*
         * Stop only when the machine has been calm long enough and KSM has nothing left to merge; with
         * shared pages still around keep KSM running at the minimum rate (the kernel keeps already merged
         * pages even after a stop).
         */
        if (st->calm_steps >= cfg->calm_steps_to_stop && s->pages_sharing == 0)
            st->run = 0;
    }

    /* keep the state in [npages_min, npages_max] even if it was set outside the band */
    if (st->pages_to_scan < cfg->npages_min)
        st->pages_to_scan = cfg->npages_min;
    if (st->pages_to_scan > cfg->npages_max)
        st->pages_to_scan = cfg->npages_max;

    st->sleep_ms = ksm_sleep_ms( cfg, s->mem_total_kb );
}

static int ksm_read_file( const char *path, char *buf, size_t bufsz )
{
    int fd;
    ssize_t n;

    if (bufsz == 0)
        return -1;
    fd = open( path, O_RDONLY );
    if (fd < 0)
        return -1;
    n = read( fd, buf, bufsz - 1 );
    close( fd );
    if (n < 0)
        return -1;
    buf[n] = '\0';
    return 0;
}

static int ksm_write_file( const char *dir, const char *name, const char *content )
{
    char path[4096];
    size_t len;
    int fd;
    ssize_t n;

    if (snprintf( path, sizeof(path), "%s/%s", dir, name ) >= (int)sizeof(path))
        return -1;
    fd = open( path, O_WRONLY | O_TRUNC );
    if (fd < 0)
        return -1;
    len = strlen( content );
    while (len > 0)
    {
        n = write( fd, content, len );
        if (n < 0)
        {
            close( fd );
            return -1;
        }
        content += n;
        len -= (size_t)n;
    }
    if (close( fd ) < 0)
        return -1;
    return 0;
}

static int ksm_parse_meminfo( const char *buf, uint64_t *total_kb, uint64_t *avail_kb )
{
    const char *p = buf;
    int got_total = 0, got_avail = 0;

    while (*p)
    {
        const char *eol = strchr( p, '\n' );
        const char *val;
        size_t len = eol ? (size_t)(eol - p) : strlen( p );

        if (len >= 9 && strncmp( p, "MemTotal:", 9 ) == 0)
        {
            val = p + 9;
            while (*val == ' ' || *val == '\t')
                val++;
            *total_kb = strtoull( val, NULL, 10 );
            got_total = 1;
        }
        else if (len >= 13 && strncmp( p, "MemAvailable:", 13 ) == 0)
        {
            val = p + 13;
            while (*val == ' ' || *val == '\t')
                val++;
            *avail_kb = strtoull( val, NULL, 10 );
            got_avail = 1;
        }
        if (eol == NULL)
            break;
        p = eol + 1;
    }
    return (got_total && got_avail) ? 0 : -1;
}

int ksm_read_sample( const char *meminfo_path, const char *ksm_dir, struct ksm_sample *out )
{
    char meminfo[8192];
    char path[4096];
    char sharing[64];
    uint64_t total = 0, avail = 0;

    if (ksm_read_file( meminfo_path, meminfo, sizeof(meminfo) ) < 0)
        return -1;
    if (ksm_parse_meminfo( meminfo, &total, &avail ) < 0)
        return -1;

    out->mem_total_kb = total;
    out->mem_available_kb = avail;
    out->pages_sharing = 0;

    if (snprintf( path, sizeof(path), "%s/pages_sharing", ksm_dir ) < (int)sizeof(path))
    {
        if (ksm_read_file( path, sharing, sizeof(sharing) ) == 0)
            out->pages_sharing = strtoull( sharing, NULL, 10 );
    }
    return 0;
}

static int ksm_advisor_scan_time_selected( const char *content )
{
    const char *t = content;
    size_t len;

    /* kernel format: the selected mode is the one in brackets, e.g. "none [scan-time]" */
    if (strstr( content, "[scan-time]" ) != NULL)
        return 1;
    /* our own bare "scan-time" write: the kernel echoes it back as "none [scan-time]", a fake tree keeps it */
    while (*t == ' ' || *t == '\t' || *t == '\r' || *t == '\n')
        t++;
    len = strlen( t );
    while (len > 0 && (t[len - 1] == ' ' || t[len - 1] == '\t' || t[len - 1] == '\r' || t[len - 1] == '\n'))
        len--;
    return (len == 9 && strncmp( t, "scan-time", 9 ) == 0);
}

int ksm_apply( const char *ksm_dir, const struct ksm_cfg *cfg, const struct ksm_state *st,
               struct ksm_state *last_written )
{
    char adv[256];
    char val[32];
    int written = 0;
    int advisor = 0;

    if (cfg->use_kernel_advisor)
    {
        char path[4096];
        if (snprintf( path, sizeof(path), "%s/advisor_mode", ksm_dir ) < (int)sizeof(path) &&
            ksm_read_file( path, adv, sizeof(adv) ) == 0)
            advisor = 1;
    }

    if (advisor)
    {
        /* the kernel's built-in advisor does the tuning: select scan-time once, keep KSM running, write nothing else */
        if (!ksm_advisor_scan_time_selected( adv ))
        {
            if (ksm_write_file( ksm_dir, "advisor_mode", "scan-time\n" ) < 0)
                return -1;
            written++;
        }
        if (last_written->run != 1)
        {
            if (ksm_write_file( ksm_dir, "run", "1\n" ) < 0)
                return -1;
            written++;
        }
        last_written->run = 1;
        return written;
    }

    if (last_written->run != st->run)
    {
        snprintf( val, sizeof(val), "%d\n", st->run );
        if (ksm_write_file( ksm_dir, "run", val ) < 0)
            return -1;
        written++;
    }
    if (last_written->pages_to_scan != st->pages_to_scan)
    {
        snprintf( val, sizeof(val), "%u\n", st->pages_to_scan );
        if (ksm_write_file( ksm_dir, "pages_to_scan", val ) < 0)
            return -1;
        written++;
    }
    if (last_written->sleep_ms != st->sleep_ms)
    {
        snprintf( val, sizeof(val), "%u\n", st->sleep_ms );
        if (ksm_write_file( ksm_dir, "sleep_millisecs", val ) < 0)
            return -1;
        written++;
    }
    *last_written = *st;
    return written;
}

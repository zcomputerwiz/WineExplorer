/*
 * Adaptive KSM (Kernel Samepage Merging) tuner, in the spirit of Red Hat's ksmtuned (GPL-2.0-or-later):
 * pages_to_scan is raised or lowered depending on memory pressure, and sleep_millisecs is derived from the
 * machine's memory size.  The pressure signal is the system's MemAvailable instead of a qemu RSS.  The
 * control logic is pure (no I/O) so it can be unit tested; the sysfs I/O helpers live in ksm_tuner.c.
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

#ifndef WEXPREFETCHD_KSM_TUNER_H
#define WEXPREFETCHD_KSM_TUNER_H

#include <stdint.h>

struct ksm_cfg
{
    uint32_t npages_min;         /* default 64 */
    uint32_t npages_max;         /* default 1250 */
    uint32_t npages_boost;       /* default 300 */
    uint32_t npages_decay;       /* default 50 */
    uint32_t thres_coef_pct;     /* default 20 */
    uint64_t thres_const_kb;     /* default 2097152 (2 GiB) */
    uint32_t sleep_base_ms;      /* default 10 */
    uint32_t calm_steps_to_stop; /* default 10 */
    int      use_kernel_advisor; /* default 0 */
};

struct ksm_sample
{
    uint64_t mem_total_kb;
    uint64_t mem_available_kb;
    uint64_t pages_sharing;
};

struct ksm_state
{
    uint32_t pages_to_scan;
    uint32_t sleep_ms;
    int      run;        /* 0 or 1 */
    uint32_t calm_steps;
};

void ksm_cfg_defaults( struct ksm_cfg *cfg );
void ksm_state_init( const struct ksm_cfg *cfg, struct ksm_state *st, const struct ksm_sample *first );
void ksm_tune_step( const struct ksm_cfg *cfg, struct ksm_state *st, const struct ksm_sample *s );

/* Pure helpers (no I/O), exposed for unit tests. */
uint64_t ksm_threshold_kb( const struct ksm_cfg *cfg, uint64_t mem_total_kb );
uint32_t ksm_sleep_ms( const struct ksm_cfg *cfg, uint64_t mem_total_kb );

/*
 * I/O helpers.  ksm_read_sample returns 0 on success and -1 if the meminfo file cannot be read or has no
 * MemAvailable (a missing pages_sharing file is not an error: it reads as 0).  ksm_apply writes only the
 * values that changed since last_written (pass a zero-initialised state with run = -1 for the first call to
 * force all writes) and returns the number of files written, or -1 on error.
 */
int ksm_read_sample( const char *meminfo_path, const char *ksm_dir, struct ksm_sample *out );
int ksm_apply( const char *ksm_dir, const struct ksm_cfg *cfg, const struct ksm_state *st,
               struct ksm_state *last_written );

#endif /* WEXPREFETCHD_KSM_TUNER_H */

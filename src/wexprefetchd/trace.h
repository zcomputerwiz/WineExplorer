/*
 * Prefetch traces: the daemon notes which images each Wine process loads, writes them to a Prefetch (.pf, version
 * 17) file per executable, and replays the list into the page cache the next time that executable starts.
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

#ifndef WEXPREFETCHD_TRACE_H
#define WEXPREFETCHD_TRACE_H

#include <stddef.h>
#include <stdint.h>
#include <sys/stat.h>
#include <sys/types.h>

#define TRACE_WINDOW_SECONDS    30   /* a process is traced for this long after its first image request */
#define TRACE_REPLAY_MIN_SECS   60   /* the same executable is replayed at most this often */
#define TRACE_MAX_SESSIONS      64
#define TRACE_MAX_PF_FILES      1024 /* .pf files kept; new executables are not traced beyond this */

/* dir is created if missing.  Tracing and replay each default to off until trace_configure is called. */
int  trace_init( const char *dir );
void trace_configure( int record, int replay );

/* fd is the image the process asked for.  fd_ok returns nonzero if the file may be read ahead (the daemon's
 * untrusted filesystem test, inverted).  Traces are kept per user.  Returns nonzero if the process should ask for
 * the paths of its trace that the daemon could not read itself (trace_take_private). */
int trace_image( pid_t pid, uid_t uid, int fd, int (*fd_ok)( int ) );
size_t trace_take_private( pid_t pid, uid_t uid, char *buf, size_t cap );

/* Finishes sessions that are older than the window or whose process is gone.  Call about once a second. */
void trace_tick( void );

/* Flushes every open session (shutdown). */
void trace_flush_all( void );

/* Exposed for tests. */
int trace_path_to_utf16( const char *utf8, uint16_t *out, size_t out_chars );
int trace_read_pf( const char *pf_path, char **paths, unsigned max_paths, uint32_t *run_count );

#endif /* WEXPREFETCHD_TRACE_H */

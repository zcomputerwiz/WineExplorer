/*
 * The elevation broker's wire records (docs/ELEVATION_BROKER_WORKORDER.md), the one definition of them. wexelevate.h
 * includes it for the clients and the broker; the broker's native unit tests include it with their own DWORD and
 * ULONGLONG (wexelevate_request.h), since they cannot include windows.h.
 *
 * Copyright 2026 WineExplorer Desktop Environment Project
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
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
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#ifndef __WINE_WEXELEVATE_PROTO_H
#define __WINE_WEXELEVATE_PROTO_H

#define WEXELEVATE_MAGIC          0x4c455857  /* "WXEL" */
#define WEXELEVATE_VERSION        1
#define WEXELEVATE_MAX_REQUEST    0x100000    /* bytes of one request */
#define WEXELEVATE_MAX_PATH_CHARS 32767
#define WEXELEVATE_MAX_ENV_CHARS  0x20000

#define WEXELEVATE_WANT_HANDLE    0x1  /* duplicate a process handle into the caller */
#define WEXELEVATE_ENV_INHERIT    0x2  /* the request carries the environment block; else the target user's default */
#define WEXELEVATE_FLAGS_MASK     0x3

/* The reply error for a request whose image is on a file system that gives no read leases (UDF, as a loop-mounted
 * ISO): the broker cannot freeze the image and refuses. This is the broker's own value, outside the Win32 error
 * range, so it names exactly this one case end to end and no other failure can look like it; every consumer maps
 * it back to ERROR_NOT_SUPPORTED before the program sees it (shell32 also shows the user what to do: copy the
 * program to a local disk). */
#define WEXELEVATE_E_NO_IMAGE_LEASE 0x20150

struct wexelevate_request
{
    DWORD magic;
    DWORD version;
    DWORD flags;
    DWORD show;           /* SW_* for the first window */
    DWORD advisory_pid;   /* ignored: the broker attributes the request to the process that opened the pipe */
    DWORD image_len;      /* WCHARs */
    DWORD cmdline_len;
    DWORD cwd_len;
    DWORD env_len;        /* WCHARs of the block, terminators included */
};

struct wexelevate_reply
{
    DWORD magic;
    DWORD error;          /* Win32 error, 0 on success */
    DWORD pid;
    DWORD tid;
    ULONGLONG handle;     /* in the caller's process, 0 unless WEXELEVATE_WANT_HANDLE */
};

#endif /* __WINE_WEXELEVATE_PROTO_H */

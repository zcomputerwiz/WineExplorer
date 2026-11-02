/*
 * WineExplorer Desktop Environment - bridge from the DWM broker to the desktop compositor
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

/* docs/DWM_ARCHITECTURE.md section 4: application DLL -> broker -> this bridge -> wexhost -> the compositor.
 * The bridge polls the broker for attribute changes and hands the ones the compositor draws to wexhost. It only
 * runs while composition is on. Nothing here draws, and nothing here is needed for the attributes to read back. */

#include <stdarg.h>
#include <stdint.h>

#include <windows.h>
#include <wexhost.h>
#include "wine/debug.h"
#include "wine/dwm_protocol.h"

WINE_DEFAULT_DEBUG_CHANNEL(wexshell);

#define DWM_BRIDGE_POLL_MS    250
#define DWM_BRIDGE_IDLE_MS    1000   /* no broker */
#define DWM_BRIDGE_PENDING    64
#define DWM_BRIDGE_RETRIES    40     /* windows that have no X window yet */

#ifndef DWMWA_WINDOW_CORNER_PREFERENCE
#define DWMWA_WINDOW_CORNER_PREFERENCE 33
#endif

HRESULT WINAPI WexDwmpPoll( UINT64 since_revision, DWORD cursor, void *out, DWORD *out_len );

struct pending { HWND hwnd; DWORD radius; int tries; };

static HANDLE bridge_thread, bridge_stop;
static HDESK bridge_desktop;
static struct pending pending[DWM_BRIDGE_PENDING];

/* DWMWCP_DEFAULT stays square for now: the corner policy of a theme era comes with the open styles (P13) */
static DWORD corner_radius( DWORD preference )
{
    switch (preference)
    {
    case 2: return 8;   /* DWMWCP_ROUND */
    case 3: return 4;   /* DWMWCP_ROUNDSMALL */
    default: return 0;  /* DEFAULT, DONOTROUND */
    }
}

static BOOL apply_corner( HWND hwnd, DWORD radius )
{
    UINT64 window;
    DWORD err;

    if (!IsWindow( hwnd )) return TRUE;  /* gone: nothing to do */
    window = (ULONG_PTR)GetPropW( hwnd, L"__wine_x11_whole_window" );
    if (!window)
    {
        TRACE( "%p has no x11 window yet\n", hwnd );
        return FALSE;
    }
    err = WexCompositionSetCorner( window, radius );
    TRACE( "%p xwin %#I64x radius %lu err %lu\n", hwnd, window, radius, err );
    return !err;
}

static void queue_retry( HWND hwnd, DWORD radius )
{
    int i, free_slot = -1;

    for (i = 0; i < DWM_BRIDGE_PENDING; i++)
    {
        if (pending[i].hwnd == hwnd) { pending[i].radius = radius; pending[i].tries = DWM_BRIDGE_RETRIES; return; }
        if (!pending[i].hwnd && free_slot < 0) free_slot = i;
    }
    if (free_slot >= 0) pending[free_slot] = (struct pending){ hwnd, radius, DWM_BRIDGE_RETRIES };
}

static void retry_pending(void)
{
    int i;

    for (i = 0; i < DWM_BRIDGE_PENDING; i++)
    {
        if (!pending[i].hwnd) continue;
        if (apply_corner( pending[i].hwnd, pending[i].radius ) || --pending[i].tries <= 0)
            pending[i].hwnd = NULL;
    }
}

static void apply_change( const struct dwm_change *change )
{
    HWND hwnd = UlongToHandle( change->hwnd );
    DWORD radius;

    if (change->attribute != DWMWA_WINDOW_CORNER_PREFERENCE) return;
    radius = corner_radius( change->value );
    if (!apply_corner( hwnd, radius )) queue_retry( hwnd, radius );
}

static DWORD WINAPI bridge_proc( void *arg )
{
    unsigned char buf[sizeof(struct dwm_poll_reply) + DWM_POLL_MAX_CHANGES * sizeof(struct dwm_change)];
    UINT64 revision = 0;
    DWORD cursor = 0;
    DWORD wait = DWM_BRIDGE_POLL_MS;

    SetThreadDesktop( bridge_desktop );
    while (WaitForSingleObject( bridge_stop, wait ) == WAIT_TIMEOUT)
    {
        const struct dwm_poll_reply *reply = (const struct dwm_poll_reply *)buf;
        const struct dwm_change *changes = (const struct dwm_change *)(buf + sizeof(*reply));
        DWORD len = sizeof(buf);
        DWORD i;

        retry_pending();
        if (WexDwmpPoll( revision, cursor, buf, &len ) || len < sizeof(*reply) ||
            len < sizeof(*reply) + reply->count * sizeof(*changes))
        {
            /* no broker (or it restarted): ask for the whole table again when it is back */
            if (wait != DWM_BRIDGE_IDLE_MS) TRACE( "no broker\n" );
            revision = 0;
            cursor = 0;
            wait = DWM_BRIDGE_IDLE_MS;
            continue;
        }
        wait = DWM_BRIDGE_POLL_MS;
        if (reply->count) TRACE( "poll: %lu changes, revision %I64u\n", (DWORD)reply->count, (UINT64)reply->revision );
        for (i = 0; i < reply->count; i++) apply_change( &changes[i] );
        if (reply->flags & DWM_POLL_MORE)
        {
            if (reply->flags & DWM_POLL_SNAPSHOT) cursor += reply->count;
            else revision = reply->revision;
            wait = 0;
        }
        else
        {
            revision = reply->revision;
            cursor = 0;
        }
    }
    return 0;
}

void dwm_bridge_start(void)
{
    DWORD enabled = 0, size = sizeof(enabled);

    if (bridge_thread) return;
    if (RegGetValueW( HKEY_CURRENT_USER, L"Software\\WineExplorer\\Shell", L"DwmService",
                      RRF_RT_REG_DWORD, NULL, &enabled, &size ) || !enabled) return;
    if (!(bridge_desktop = GetThreadDesktop( GetCurrentThreadId() ))) return;
    if (!(bridge_stop = CreateEventW( NULL, TRUE, FALSE, NULL ))) return;
    bridge_thread = CreateThread( NULL, 0, bridge_proc, NULL, 0, NULL );
    TRACE( "bridge thread %p\n", bridge_thread );
}

void dwm_bridge_stop(void)
{
    if (!bridge_thread) return;
    SetEvent( bridge_stop );
    WaitForSingleObject( bridge_thread, 3000 );
    CloseHandle( bridge_thread );
    CloseHandle( bridge_stop );
    bridge_thread = bridge_stop = NULL;
}

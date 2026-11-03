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
#include <string.h>

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
#ifndef DWMWA_USE_IMMERSIVE_DARK_MODE
#define DWMWA_USE_IMMERSIVE_DARK_MODE 20
#endif
#ifndef DWMWA_SYSTEMBACKDROP_TYPE
#define DWMWA_SYSTEMBACKDROP_TYPE 38
#endif
#ifndef DWMWA_MICA_EFFECT
#define DWMWA_MICA_EFFECT 1029
#endif

HRESULT WINAPI WexDwmpPoll( UINT64 since_revision, DWORD cursor, void *out, DWORD *out_len );
HRESULT WINAPI WexDwmpThumbList( UINT64 since_revision, void *out, DWORD *out_len );

struct pending
{
    HWND hwnd;
    DWORD radius;
    BOOL dark;
    DWORD backdrop_type;
    DWORD mica_effect;
    DWORD blur;   /* DWM_WINE_BLUR_* */
    DWORD tint;   /* DWM_ATTR_WINE_TINT */
    DWORD rgn[2]; /* DWM_ATTR_WINE_RGN_LT, _RB */
    DWORD frame;  /* DWM_ATTR_WINE_FRAME: 0 none, 1 Windows 10/11 glass frame, 2 Aero */
    int tries;
};

/* live thumbnails (P8): the broker's list, mapped to X windows on every poll, because a window may get its
 * X window after the thumbnail was registered; the compositor ignores an unchanged list */
static struct dwm_thumb thumb_list[DWM_THUMB_MAX];
static unsigned int thumb_list_count;
static UINT64 thumb_revision;

static UINT64 x_window( UINT32 hwnd )
{
    return (ULONG_PTR)GetPropW( UlongToHandle( hwnd ), L"__wine_x11_whole_window" );
}

static BOOL bridge_wayland;   /* a Wayland session: effects and thumbnails go to the compositor by shared handle */

/* The 32 hex digit shared handle of a Wayland window's wex_window_v1, which winewayland.drv publishes as a global
 * atom in the window property; FALSE while it has none. */
BOOL dwm_wayland_handle( HWND hwnd, char handle[33] )
{
    WCHAR name[40];
    ATOM atom = (ATOM)(ULONG_PTR)GetPropW( hwnd, L"__wine_wayland_window_handle" );
    UINT i, len;

    if (!atom || !(len = GlobalGetAtomNameW( atom, name, ARRAY_SIZE(name) )) || len != 32) return FALSE;
    for (i = 0; i < 32; i++) handle[i] = (char)name[i];
    handle[32] = 0;
    return TRUE;
}

/* The broker's thumbnail flags map one-to-one onto the compositor's (equal values); fill the destination's flags,
 * opacity and the two rectangles from the broker record */
static void thumb_fill( UINT32 *flags, UINT32 *opacity, INT32 *dest, INT32 *source, const struct dwm_thumb *t )
{
    *flags = (t->flags & DWM_THUMB_VISIBLE ? WEX_THUMB_VISIBLE : 0) | (t->flags & DWM_THUMB_HAS_SOURCE ? WEX_THUMB_HAS_SOURCE : 0);
    *opacity = t->opacity;
    memcpy( dest, t->dest_rect, sizeof(t->dest_rect) );
    memcpy( source, t->source, sizeof(t->source) );
}

/* The same list for a Wayland compositor: windows are named by handle, and only a changed list is sent */
static void sync_thumbnails_wayland(void)
{
    static WEX_WM_THUMB sent[WEX_THUMB_MAX];
    static DWORD sent_count;
    WEX_WM_THUMB out[WEX_THUMB_MAX];
    char dest[33], src[33];
    DWORD i, count = 0;

    memset( out, 0, sizeof(out) );
    for (i = 0; i < thumb_list_count; i++)
    {
        const struct dwm_thumb *t = &thumb_list[i];
        if (!dwm_wayland_handle( UlongToHandle( t->dest ), dest ) || !dwm_wayland_handle( UlongToHandle( t->src ), src ))
            continue;
        memcpy( out[count].dest, dest, 32 );
        memcpy( out[count].src, src, 32 );
        thumb_fill( (UINT32 *)&out[count].flags, (UINT32 *)&out[count].opacity, (INT32 *)out[count].dest_rect, (INT32 *)out[count].source_rect, t );
        count++;
    }
    if (count == sent_count && !memcmp( out, sent, count * sizeof(*out) )) return;
    if (!WexWmSetThumbnails( out, count )) memcpy( sent, out, sizeof(sent) ), sent_count = count;
}

static void sync_thumbnails( BOOL have_broker )
{
    unsigned char buf[sizeof(struct dwm_thumb_list_reply) + DWM_THUMB_MAX * sizeof(struct dwm_thumb)];
    const struct dwm_thumb_list_reply *reply = (const struct dwm_thumb_list_reply *)buf;
    struct compose_thumb out[WEX_THUMB_MAX];
    DWORD len = sizeof(buf), i, count = 0;

    if (!have_broker) thumb_list_count = 0, thumb_revision = 0;
    else if (!WexDwmpThumbList( thumb_revision, buf, &len ) && len >= sizeof(*reply) &&
             reply->count <= DWM_THUMB_MAX && len >= sizeof(*reply) + reply->count * sizeof(struct dwm_thumb) &&
             (reply->count || reply->revision != thumb_revision))
    {
        memcpy( thumb_list, buf + sizeof(*reply), reply->count * sizeof(struct dwm_thumb) );
        thumb_list_count = reply->count;
        thumb_revision = reply->revision;
        TRACE( "%u thumbnails, revision %I64u\n", thumb_list_count, thumb_revision );
    }
    if (bridge_wayland)
    {
        sync_thumbnails_wayland();
        return;
    }
    for (i = 0; i < thumb_list_count; i++)
    {
        const struct dwm_thumb *t = &thumb_list[i];
        if (!(out[count].dest_window = x_window( t->dest )) || !(out[count].src = x_window( t->src ))) continue;
        thumb_fill( &out[count].flags, &out[count].opacity, out[count].dest, out[count].source, t );
        count++;
    }
    WexCompositionSetThumbnails( out, count );
}

static HANDLE bridge_thread, bridge_stop;
static HDESK bridge_desktop;
static struct pending pending[DWM_BRIDGE_PENDING];
static BOOL materials_on;

/* DWMWCP_DEFAULT is square except for glass frames, which the compositor rounds; the corner policy of a theme era
 * comes with the open styles (P13) */
static DWORD corner_radius( DWORD preference )
{
    switch (preference)
    {
    case 1: return WEX_CORNER_SQUARE;  /* DWMWCP_DONOTROUND */
    case 2: return 8;   /* DWMWCP_ROUND */
    case 3: return 4;   /* DWMWCP_ROUNDSMALL */
    default: return 0;  /* DEFAULT */
    }
}

/* The DwmMaterials gate (HKCU\Software\WineExplorer\Shell), read once at thread start: off by default, and
 * forced off by high contrast or a disabled transparency, so the backdrop is never drawn in those modes. */
static void read_materials_gate(void)
{
    DWORD value = 0, size = sizeof(value);
    HIGHCONTRASTW hc;

    materials_on = FALSE;
    if (RegGetValueW( HKEY_CURRENT_USER, L"Software\\WineExplorer\\Shell", L"DwmMaterials",
                      RRF_RT_REG_DWORD, NULL, &value, &size ) != ERROR_SUCCESS || !value) return;
    materials_on = TRUE;
    memset( &hc, 0, sizeof(hc) );
    hc.cbSize = sizeof(hc);
    if (SystemParametersInfoW( SPI_GETHIGHCONTRAST, sizeof(hc), &hc, 0 ) && (hc.dwFlags & HCF_HIGHCONTRASTON))
    {
        materials_on = FALSE;
        return;
    }
    if (RegGetValueW( HKEY_CURRENT_USER,
                      L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
                      L"EnableTransparency", RRF_RT_REG_DWORD, NULL, &value, &size ) == ERROR_SUCCESS && !value)
        materials_on = FALSE;
    TRACE( "materials gate %s\n", materials_on ? "on" : "off" );
}

/* The backdrop the compositor draws for a window: Mica for the main-window and tabbed backdrops, or when the
 * app asked for the Mica effect without opting out; Acrylic for the transient backdrop (type 3); else the blur
 * or gradient asked for through DwmEnableBlurBehindWindow or ACCENT_POLICY, with its tint and region. */
static DWORD backdrop_value( const struct pending *p )
{
    DWORD backdrop = WEX_BACKDROP_NONE, mode = p->blur & DWM_WINE_BLUR_MODE;

    if ((p->backdrop_type == 2 || p->backdrop_type == 4) || (p->mica_effect && p->backdrop_type != 1))
        backdrop = WEX_BACKDROP_MICA;
    else if (p->backdrop_type == 3)
        backdrop = WEX_BACKDROP_ACRYLIC;
    else if (mode != DWM_WINE_BLUR_NONE)
    {
        backdrop = mode == DWM_WINE_BLUR_ACRYLIC ? WEX_BACKDROP_ACRYLIC :
                   mode == DWM_WINE_BLUR_GRADIENT ? WEX_BACKDROP_GRADIENT : WEX_BACKDROP_BLUR;
        if (p->blur & DWM_WINE_BLUR_TINTED) backdrop |= WEX_BACKDROP_TINT;
        if (p->blur & DWM_WINE_BLUR_REGION) backdrop |= WEX_BACKDROP_REGION;
    }
    if (p->dark) backdrop |= WEX_BACKDROP_DARK;
    return backdrop;
}

/* The DWM effects of a Wayland window go to the compositor as one request. 1 applied (or nothing to do), 0 not yet. */
static int apply_wayland( HWND hwnd, const struct pending *p, const char *handle )
{
    DWORD radius = p->radius, backdrop = WEX_WM_BACKDROP_NONE, flags = 0, mode = p->blur & DWM_WINE_BLUR_MODE, err;
    DWORD tint = p->tint;
    BOOL glass_frame = p->frame != 0;
    RECT region = { 0 }, wr, cr;

    /* the compositor rounds only what it is told to; a glass frame is round unless the window says square */
    if (radius == WEX_CORNER_SQUARE) radius = 0;
    else if (!radius && glass_frame) radius = p->frame == 2 ? 6 : 8;
    if (glass_frame) flags |= WEX_WM_EFFECT_SHADOW;
    if (materials_on)
    {
        if ((p->backdrop_type == 2 || p->backdrop_type == 4) || (p->mica_effect && p->backdrop_type != 1))
            backdrop = WEX_WM_BACKDROP_MICA;
        else if (p->backdrop_type == 3) backdrop = WEX_WM_BACKDROP_ACRYLIC;
        else if (mode != DWM_WINE_BLUR_NONE)
        {
            backdrop = mode == DWM_WINE_BLUR_ACRYLIC ? WEX_WM_BACKDROP_ACRYLIC :
                       mode == DWM_WINE_BLUR_GRADIENT ? WEX_WM_BACKDROP_GRADIENT : WEX_WM_BACKDROP_BLUR;
            if (p->blur & DWM_WINE_BLUR_TINTED)
            {
                flags |= WEX_WM_EFFECT_TINT;
                tint = (tint & 0xff00ff00) | (tint >> 16 & 0xff) | (tint & 0xff) << 16;   /* ABGR to ARGB */
            }
            if ((p->blur & DWM_WINE_BLUR_REGION) && GetWindowRect( hwnd, &wr ) && GetClientRect( hwnd, &cr ))
            {
                POINT origin = { 0, 0 };
                ClientToScreen( hwnd, &origin );
                SetRect( &region, (SHORT)LOWORD( p->rgn[0] ) + origin.x - wr.left,
                         (SHORT)HIWORD( p->rgn[0] ) + origin.y - wr.top,
                         (SHORT)LOWORD( p->rgn[1] ) + origin.x - wr.left,
                         (SHORT)HIWORD( p->rgn[1] ) + origin.y - wr.top );
                flags |= WEX_WM_EFFECT_REGION;
            }
        }
        if (p->dark && backdrop) flags |= WEX_WM_EFFECT_DARK;
    }
    err = WexWmSetEffects( handle, radius, backdrop, flags, tint, &region );
    TRACE( "%p wayland %s radius %lu backdrop %lu flags %#lx err %lu\n", hwnd, handle, radius, backdrop, flags, err );
    return err != ERROR_NOT_READY;   /* an old compositor or an X11 session: nothing more to try */
}

/* 1 applied, 0 waiting for the X window, -1 the window is gone. */
static int apply_window( HWND hwnd, const struct pending *p )
{
    UINT64 window;
    DWORD err;
    char handle[33];

    if (!IsWindow( hwnd )) return -1;  /* gone: nothing to do */
    window = (ULONG_PTR)GetPropW( hwnd, L"__wine_x11_whole_window" );
    if (!window)
    {
        if (dwm_wayland_handle( hwnd, handle )) return apply_wayland( hwnd, p, handle );
        TRACE( "%p has no x11 window or wayland handle yet\n", hwnd );
        return 0;
    }
    err = WexCompositionSetCorner( window, p->radius );
    TRACE( "%p xwin %#I64x radius %lu err %lu\n", hwnd, window, p->radius, err );
    if (materials_on)
    {
        DWORD backdrop = backdrop_value( p );
        RECT rgn = { (SHORT)LOWORD( p->rgn[0] ), (SHORT)HIWORD( p->rgn[0] ),
                     (SHORT)LOWORD( p->rgn[1] ), (SHORT)HIWORD( p->rgn[1] ) };
        DWORD err2 = WexCompositionSetBackdrop( window, backdrop, p->tint, &rgn );
        TRACE( "%p xwin %#I64x backdrop %lu err %lu\n", hwnd, window, backdrop, err2 );
        if (!err) err = err2;
    }
    return !err;
}

static void retry_pending(void)
{
    int i;

    for (i = 0; i < DWM_BRIDGE_PENDING; i++)
    {
        int r;
        if (!pending[i].hwnd) continue;
        if (!IsWindow( pending[i].hwnd )) { pending[i].hwnd = NULL; continue; }
        if (!pending[i].tries) continue;
        r = apply_window( pending[i].hwnd, &pending[i] );
        if (r < 0) pending[i].hwnd = NULL;
        else if (r > 0) pending[i].tries = 0;
        else if (--pending[i].tries <= 0) pending[i].hwnd = NULL;
    }
}

static void apply_change( const struct dwm_change *change )
{
    HWND hwnd = UlongToHandle( change->hwnd );
    struct pending *p = NULL;
    int i, free_slot = -1;

    if (change->attribute != DWMWA_WINDOW_CORNER_PREFERENCE &&
        change->attribute != DWMWA_USE_IMMERSIVE_DARK_MODE &&
        change->attribute != DWMWA_SYSTEMBACKDROP_TYPE &&
        change->attribute != DWMWA_MICA_EFFECT &&
        (change->attribute < DWM_ATTR_WINE_BLUR || change->attribute > DWM_ATTR_WINE_LAST))
        return;
    for (i = 0; i < DWM_BRIDGE_PENDING; i++)
    {
        if (pending[i].hwnd == hwnd) { p = &pending[i]; break; }
        if (!pending[i].hwnd && free_slot < 0) free_slot = i;
    }
    if (!p)
    {
        if (free_slot < 0) return;  /* table full */
        p = &pending[free_slot];
        *p = (struct pending){ .hwnd = hwnd };
    }
    switch (change->attribute)
    {
    case DWMWA_WINDOW_CORNER_PREFERENCE:
        p->radius = corner_radius( change->value );
        break;
    case DWMWA_USE_IMMERSIVE_DARK_MODE:
        p->dark = change->value != 0;
        break;
    case DWMWA_SYSTEMBACKDROP_TYPE:
        p->backdrop_type = change->value;
        break;
    case DWMWA_MICA_EFFECT:
        p->mica_effect = change->value;
        break;
    case DWM_ATTR_WINE_BLUR:
        p->blur = change->value;
        break;
    case DWM_ATTR_WINE_TINT:
        p->tint = change->value;
        break;
    case DWM_ATTR_WINE_RGN_LT:
    case DWM_ATTR_WINE_RGN_RB:
        p->rgn[change->attribute - DWM_ATTR_WINE_RGN_LT] = change->value;
        break;
    case DWM_ATTR_WINE_FRAME:
        p->frame = change->value;
        break;
    }
    switch (apply_window( hwnd, p ))
    {
    case -1: p->hwnd = NULL; break;   /* gone */
    case 0:  p->tries = DWM_BRIDGE_RETRIES; break;
    default: p->tries = 0; break;
    }
}

static DWORD WINAPI bridge_proc( void *arg )
{
    unsigned char buf[sizeof(struct dwm_poll_reply) + DWM_POLL_MAX_CHANGES * sizeof(struct dwm_change)];
    UINT64 revision = 0;
    DWORD cursor = 0;
    DWORD wait = DWM_BRIDGE_POLL_MS;

    SetThreadDesktop( bridge_desktop );
    read_materials_gate();
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
            sync_thumbnails( FALSE );
            continue;
        }
        wait = DWM_BRIDGE_POLL_MS;
        sync_thumbnails( TRUE );
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

void dwm_bridge_start(void);

/* On a Wayland session (no X composition): thumbnails, effects and the frame clock all go to the compositor */
void dwm_bridge_start_wayland(void)
{
    bridge_wayland = TRUE;
    dwm_bridge_start();
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
    WexCompositionSetThumbnails( NULL, 0 );
}

/* Frame clock (docs/DWM_ARCHITECTURE.md section 2.3, P6 slice 1): a per-desktop shared page the compositor
 * publishes one record into per composed frame, plus the event it pulses. dwmapi opens the same objects
 * read-only to pace DwmFlush and fill DwmGetCompositionTimingInfo. */
static HANDLE clock_mapping, clock_event, clock_thread;
static struct dwm_clock_page *clock_page;
static volatile LONG clock_stopping;

/* The compositor runs on a Unix thread that cannot touch Win32 events: this thread waits for its frames and
 * pulses the event dwmapi's DwmFlush waits on. */
static DWORD WINAPI clock_proc( void *arg )
{
    UINT64 generation = 0, last = 0;

    while (!clock_stopping)
    {
        if (WexCompositionWaitFrame( &generation, 500 )) break;
        if (generation != last) PulseEvent( clock_event );
        last = generation;
    }
    return 0;
}

void dwm_clock_start(void)
{
    DWORD enabled = 1, size = sizeof(enabled);
    WCHAR desk_name[MAX_PATH], name[MAX_PATH + 64], event_name[MAX_PATH + 64];
    HDESK desk;
    DEVMODEW mode;
    DWORD frequency;

    if (clock_page) return;
    /* rollback switch: default on */
    if (RegGetValueW( HKEY_CURRENT_USER, L"Software\\WineExplorer\\Shell", L"DwmFrameClock",
                      RRF_RT_REG_DWORD, NULL, &enabled, &size ) == ERROR_SUCCESS && !enabled) return;
    if (!(desk = GetThreadDesktop( GetCurrentThreadId() ))) return;
    if (!GetUserObjectInformationW( desk, UOI_NAME, desk_name, ARRAY_SIZE( desk_name ), NULL )) return;
    lstrcpynW( name, DWM_CLOCK_NAME, ARRAY_SIZE( name ) );
    lstrcatW( name, desk_name );
    lstrcpynW( event_name, DWM_CLOCK_EVENT_NAME, ARRAY_SIZE( event_name ) );
    lstrcatW( event_name, desk_name );

    if (!(clock_mapping = CreateFileMappingW( INVALID_HANDLE_VALUE, NULL, PAGE_READWRITE, 0,
                                              sizeof(struct dwm_clock_page), name )))
    {
        WARN( "clock mapping, error %lu\n", GetLastError() );
        return;
    }
    if (!(clock_page = MapViewOfFile( clock_mapping, FILE_MAP_WRITE, 0, 0, sizeof(*clock_page) )))
    {
        CloseHandle( clock_mapping );
        clock_mapping = NULL;
        return;
    }

    /* refresh period in QPC units (100 ns); 60 Hz when the display does not report a frequency */
    memset( &mode, 0, sizeof(mode) );
    mode.dmSize = sizeof(mode);
    frequency = 60;
    if (EnumDisplaySettingsW( NULL, ENUM_CURRENT_SETTINGS, &mode ) &&
        (mode.dmFields & DM_DISPLAYFREQUENCY) && mode.dmDisplayFrequency > 1)
        frequency = mode.dmDisplayFrequency;
    clock_page->qpc_period = 10000000 / frequency;

    if (!(clock_event = CreateEventW( NULL, TRUE, FALSE, event_name )))
    {
        UnmapViewOfFile( clock_page );
        clock_page = NULL;
        CloseHandle( clock_mapping );
        clock_mapping = NULL;
        return;
    }

    if (WexCompositionSetClock( clock_page, clock_event ))
    {
        UnmapViewOfFile( clock_page );
        clock_page = NULL;
        CloseHandle( clock_mapping );
        clock_mapping = NULL;
        CloseHandle( clock_event );
        clock_event = NULL;
        return;
    }
    clock_stopping = FALSE;
    clock_thread = CreateThread( NULL, 0, clock_proc, NULL, 0, NULL );
    TRACE( "frame clock on, period %I64d\n", clock_page->qpc_period );
}

void dwm_clock_stop(void)
{
    if (!clock_page) return;
    WexCompositionSetClock( NULL, NULL );
    clock_stopping = TRUE;
    if (clock_thread)
    {
        WaitForSingleObject( clock_thread, 2000 );
        CloseHandle( clock_thread );
        clock_thread = NULL;
    }
    UnmapViewOfFile( clock_page );
    clock_page = NULL;
    CloseHandle( clock_mapping );
    clock_mapping = NULL;
    CloseHandle( clock_event );
    clock_event = NULL;
}

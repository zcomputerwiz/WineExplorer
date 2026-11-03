/*
 * WineExplorer Desktop Environment - taskbar button previews (live thumbnails)
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

/* docs/DWM_ARCHITECTURE.md P11a: hovering a taskbar button shows a live DWM thumbnail of its window above it.
 * Off unless composition runs and HKCU\Software\WineExplorer\Shell\TaskbarThumbnails is non-zero, so the XP
 * taskbar is unchanged by default. A minimized window shows the last picture the compositor kept of it; one
 * minimized before the shell started has none, and the popup then shows its title. A window with
 * DWMWA_HAS_ICONIC_BITMAP shows the picture it supplies instead (P9). */

#include <stdint.h>
#include <stdio.h>
#include <windows.h>
#include <dwmapi.h>
#include <wexhost.h>
#include "wine/dwm_protocol.h"
#include "taskband.h"

HRESULT WINAPI WexDwmpGetAttr( HWND hwnd, DWORD attribute, DWORD *value );

/* P9: a window that set DWMWA_HAS_ICONIC_BITMAP draws its own preview. Ask it (WM_DWMSENDICONICTHUMBNAIL, the
 * largest size in lParam: width high, height low) and copy what its DwmSetIconicThumbnail left in the shared
 * section (include/wine/dwm_protocol.h). NULL when it has none: the caller falls back to the live thumbnail. */
HBITMAP iconic_thumbnail( HWND hwnd, int max_w, int max_h )
{
    BITMAPINFO info = {{ sizeof(BITMAPINFOHEADER) }};
    const struct dwm_iconic *header;
    struct dwm_iconic copy;
    HBITMAP bitmap = NULL;
    HANDLE section;
    WCHAR name[64];
    DWORD has = 0, i, *bits;
    BOOL any_alpha = FALSE;

    if (WexDwmpGetAttr( hwnd, DWMWA_HAS_ICONIC_BITMAP, &has ) != S_OK || !has) return NULL;
    SendMessageTimeoutW( hwnd, WM_DWMSENDICONICTHUMBNAIL, 0, MAKELPARAM( max_h, max_w ), SMTO_ABORTIFHUNG, 1000,
                         NULL );
    swprintf( name, ARRAY_SIZE(name), L"%s%lx", DWM_ICONIC_NAME, HandleToUlong( hwnd ) );
    if (!(section = OpenFileMappingW( FILE_MAP_READ, FALSE, name ))) return NULL;
    if ((header = MapViewOfFile( section, FILE_MAP_READ, 0, 0, DWM_ICONIC_SIZE )))
    {
        copy = *header;
        if (copy.width && copy.height && copy.width <= DWM_ICONIC_MAX && copy.height <= DWM_ICONIC_MAX)
        {
            info.bmiHeader.biWidth = copy.width;
            info.bmiHeader.biHeight = -(int)copy.height;
            info.bmiHeader.biPlanes = 1;
            info.bmiHeader.biBitCount = 32;
            info.bmiHeader.biCompression = BI_RGB;
            if ((bitmap = CreateDIBSection( NULL, &info, DIB_RGB_COLORS, (void **)&bits, NULL, 0 )))
            {
                memcpy( bits, header + 1, copy.width * copy.height * 4 );
                /* the window rewrote it while we copied: skip this picture */
                if (header->serial != copy.serial || header->width != copy.width || header->height != copy.height)
                {
                    DeleteObject( bitmap );
                    bitmap = NULL;
                }
                else
                {
                    /* a bitmap without alpha (all zero) is opaque */
                    for (i = 0; i < copy.width * copy.height && !any_alpha; i++) any_alpha = bits[i] >> 24 != 0;
                    for (i = 0; i < copy.width * copy.height && !any_alpha; i++) bits[i] |= 0xff000000;
                }
            }
        }
        UnmapViewOfFile( header );
    }
    CloseHandle( section );
    return bitmap;
}

#define PREVIEW_DELAY_MS 400
#define PREVIEW_MAX_W    200
#define PREVIEW_MAX_H    120
#define PREVIEW_MARGIN   8
#define PREVIEW_TITLE_H  20

#define PEEK_DELAY_MS    600
#define LEAVE_DELAY_MS   300

enum { TIMER_SHOW = 1, TIMER_PEEK, TIMER_LEAVE };

static HWND preview_hwnd, preview_target, preview_band;
static HTHUMBNAIL preview_thumb;
static RECT preview_button;   /* screen coordinates of the hovered button */
static HBITMAP preview_bitmap;   /* the window's own iconic picture (P9) */
static RECT preview_bitmap_rect;
static BOOL peeking, tracking;

static UINT64 x_window( HWND hwnd )
{
    return (ULONG_PTR)GetPropW( hwnd, L"__wine_x11_whole_window" );
}

/* Peek (P11c, TaskbarPeek=1): resting on the preview shows only that window over the desktop, as on Windows 7 */
static BOOL peek_enabled(void)
{
    DWORD enabled = 0, size = sizeof(enabled);
    RegGetValueW( HKEY_CURRENT_USER, L"Software\\WineExplorer\\Shell", L"TaskbarPeek", RRF_RT_REG_DWORD,
                  NULL, &enabled, &size );
    return enabled != 0;
}

static void end_peek(void)
{
    if (!peeking) return;
    WexCompositionSetPeek( 0, NULL, 0 );
    peeking = FALSE;
}

struct peek_keep { UINT64 windows[8]; DWORD count; };

/* the shell's own visible top-level windows stay: desktop icons and wallpaper, taskbar, this preview */
static BOOL CALLBACK add_shell_window( HWND hwnd, LPARAM lparam )
{
    struct peek_keep *keep = (struct peek_keep *)lparam;
    DWORD pid;
    UINT64 window;

    GetWindowThreadProcessId( hwnd, &pid );
    if (pid != GetCurrentProcessId() || !IsWindowVisible( hwnd ) || !(window = x_window( hwnd ))) return TRUE;
    keep->windows[keep->count++] = window;
    return keep->count < ARRAY_SIZE(keep->windows);
}

static void start_peek(void)
{
    struct peek_keep keep = { { 0 }, 0 };
    UINT64 target = x_window( preview_target );

    if (!target || IsIconic( preview_target )) return;
    EnumWindows( add_shell_window, (LPARAM)&keep );
    peeking = !WexCompositionSetPeek( target, keep.windows, keep.count );
}

static BOOL previews_enabled(void)
{
    DWORD enabled = 0, size = sizeof(enabled);
    BOOL composition = FALSE;

    if (WexCompositionGetEnabled( &composition ) || !composition) return FALSE;
    RegGetValueW( HKEY_CURRENT_USER, L"Software\\WineExplorer\\Shell", L"TaskbarThumbnails", RRF_RT_REG_DWORD,
                  NULL, &enabled, &size );
    return enabled != 0;
}

static void release_thumbnail(void)
{
    if (preview_thumb) DwmUnregisterThumbnail( preview_thumb );
    preview_thumb = NULL;
    if (preview_bitmap) DeleteObject( preview_bitmap );
    preview_bitmap = NULL;
}

static void show_preview(void)
{
    DWM_THUMBNAIL_PROPERTIES props = { 0 };
    BITMAP bitmap;
    RECT source;
    int w = PREVIEW_MAX_W, h = 0, cx, cy, x, y;

    release_thumbnail();
    if (!IsWindow( preview_target )) return;
    if ((preview_bitmap = iconic_thumbnail( preview_target, PREVIEW_MAX_W, PREVIEW_MAX_H )))
    {
        /* the window's own picture (P9): fit it into the popup, never enlarge it */
        GetObjectW( preview_bitmap, sizeof(bitmap), &bitmap );
        w = min( bitmap.bmWidth, PREVIEW_MAX_W );
        h = MulDiv( w, bitmap.bmHeight, bitmap.bmWidth );
        if (h > PREVIEW_MAX_H)
        {
            h = PREVIEW_MAX_H;
            w = MulDiv( h, bitmap.bmWidth, bitmap.bmHeight );
        }
        SetRect( &preview_bitmap_rect, PREVIEW_MARGIN, PREVIEW_TITLE_H + PREVIEW_MARGIN / 2,
                 PREVIEW_MARGIN + w, PREVIEW_TITLE_H + PREVIEW_MARGIN / 2 + h );
    }
    else if (window_frame_rect( preview_target, &source ) && source.right > source.left && source.bottom > source.top &&
        SUCCEEDED( DwmRegisterThumbnail( preview_hwnd, preview_target, &preview_thumb ) ))
    {
        /* keep the window's aspect ratio inside PREVIEW_MAX_W x PREVIEW_MAX_H */
        h = MulDiv( w, source.bottom - source.top, source.right - source.left );
        if (h > PREVIEW_MAX_H)
        {
            h = PREVIEW_MAX_H;
            w = MulDiv( h, source.right - source.left, source.bottom - source.top );
        }
        props.dwFlags = DWM_TNP_RECTDESTINATION | DWM_TNP_VISIBLE;
        SetRect( &props.rcDestination, PREVIEW_MARGIN, PREVIEW_TITLE_H + PREVIEW_MARGIN / 2,
                 PREVIEW_MARGIN + w, PREVIEW_TITLE_H + PREVIEW_MARGIN / 2 + h );
        props.fVisible = TRUE;
        DwmUpdateThumbnailProperties( preview_thumb, &props );
    }
    cx = w + 2 * PREVIEW_MARGIN;
    cy = PREVIEW_TITLE_H + h + PREVIEW_MARGIN;
    x = max( 0, min( preview_button.left, GetSystemMetrics( SM_CXSCREEN ) - cx ) );
    y = preview_button.top - cy - 4;
    if (y < 0) y = preview_button.bottom + 4;   /* a taskbar at the top */
    SetWindowPos( preview_hwnd, HWND_TOPMOST, x, y, cx, cy, SWP_NOACTIVATE | SWP_SHOWWINDOW );
    InvalidateRect( preview_hwnd, NULL, TRUE );
}

static LRESULT CALLBACK preview_proc( HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam )
{
    switch (msg)
    {
    case WM_TIMER:
        KillTimer( hwnd, wparam );
        if (wparam == TIMER_SHOW) show_preview();
        else if (wparam == TIMER_PEEK) start_peek();
        else if (wparam == TIMER_LEAVE)
        {
            POINT pt;
            RECT rc;
            GetCursorPos( &pt );
            GetWindowRect( hwnd, &rc );
            if (!PtInRect( &rc, pt )) taskband_preview_hide();
        }
        return 0;
    case WM_MOUSEMOVE:
        KillTimer( hwnd, TIMER_LEAVE );
        if (!tracking)
        {
            TRACKMOUSEEVENT tme = { sizeof(tme), TME_LEAVE, hwnd, 0 };
            tracking = TrackMouseEvent( &tme );
            if (!peeking && peek_enabled()) SetTimer( hwnd, TIMER_PEEK, PEEK_DELAY_MS, NULL );
        }
        return 0;
    case WM_MOUSELEAVE:
        tracking = FALSE;
        taskband_preview_hide();
        return 0;
    case WM_PAINT:
    {
        PAINTSTRUCT ps;
        RECT rc;
        WCHAR title[256];
        HDC dc = BeginPaint( hwnd, &ps );
        HFONT old = SelectObject( dc, GetStockObject( DEFAULT_GUI_FONT ) );

        GetClientRect( hwnd, &rc );
        FillRect( dc, &rc, GetSysColorBrush( COLOR_INFOBK ) );
        FrameRect( dc, &rc, GetSysColorBrush( COLOR_WINDOWFRAME ) );
        if (preview_bitmap)
        {
            BITMAP bitmap;
            HDC mem = CreateCompatibleDC( dc );
            HBITMAP prev = SelectObject( mem, preview_bitmap );
            BLENDFUNCTION blend = { AC_SRC_OVER, 0, 255, AC_SRC_ALPHA };

            GetObjectW( preview_bitmap, sizeof(bitmap), &bitmap );
            if (bitmap.bmWidth != preview_bitmap_rect.right - preview_bitmap_rect.left ||
                bitmap.bmHeight != preview_bitmap_rect.bottom - preview_bitmap_rect.top)
                SetStretchBltMode( dc, HALFTONE );
            GdiAlphaBlend( dc, preview_bitmap_rect.left, preview_bitmap_rect.top,
                           preview_bitmap_rect.right - preview_bitmap_rect.left,
                           preview_bitmap_rect.bottom - preview_bitmap_rect.top,
                           mem, 0, 0, bitmap.bmWidth, bitmap.bmHeight, blend );
            SelectObject( mem, prev );
            DeleteDC( mem );
        }
        GetWindowTextW( preview_target, title, ARRAY_SIZE(title) );
        SetRect( &rc, PREVIEW_MARGIN, 0, rc.right - PREVIEW_MARGIN, PREVIEW_TITLE_H );
        SetBkMode( dc, TRANSPARENT );
        SetTextColor( dc, GetSysColor( COLOR_INFOTEXT ) );
        DrawTextW( dc, title, -1, &rc, DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS | DT_NOPREFIX );
        SelectObject( dc, old );
        EndPaint( hwnd, &ps );
        return 0;
    }
    case WM_MOUSEACTIVATE:
        return MA_NOACTIVATE;
    }
    return DefWindowProcW( hwnd, msg, wparam, lparam );
}

/* the hovered button (NULL: none); band is the taskband window */
void taskband_preview_hover( HWND band, TASK_ITEM *item )
{
    if (!item || (item->group == item && item->group_count > 1))
    {
        taskband_preview_hide();
        return;
    }
    if (item->target_hwnd == preview_target && preview_hwnd) return;
    if (!previews_enabled()) return;
    if (!preview_hwnd)
    {
        WNDCLASSW cls = { 0 };
        cls.lpfnWndProc   = preview_proc;
        cls.hInstance     = GetModuleHandleW( NULL );
        cls.hCursor       = LoadCursorW( NULL, (LPCWSTR)IDC_ARROW );
        cls.lpszClassName = L"WexTaskbarPreview";
        RegisterClassW( &cls );
        preview_hwnd = CreateWindowExW( WS_EX_TOOLWINDOW | WS_EX_TOPMOST | WS_EX_NOACTIVATE, cls.lpszClassName,
                                        NULL, WS_POPUP, 0, 0, 1, 1, band, NULL, cls.hInstance, NULL );
        if (!preview_hwnd) return;
    }
    end_peek();
    KillTimer( preview_hwnd, TIMER_LEAVE );
    preview_target = item->target_hwnd;
    preview_band = band;
    preview_button = item->rect;
    MapWindowPoints( band, NULL, (POINT *)&preview_button, 2 );
    /* moving to another button while a preview shows switches at once, like on Windows */
    SetTimer( preview_hwnd, TIMER_SHOW, IsWindowVisible( preview_hwnd ) ? 0 : PREVIEW_DELAY_MS, NULL );
}

/* the pointer left the taskbar: hide unless it went onto the preview */
void taskband_preview_leave(void)
{
    if (!preview_hwnd || !IsWindowVisible( preview_hwnd )) taskband_preview_hide();
    else SetTimer( preview_hwnd, TIMER_LEAVE, LEAVE_DELAY_MS, NULL );
}

void taskband_preview_hide(void)
{
    if (!preview_hwnd) return;
    KillTimer( preview_hwnd, TIMER_SHOW );
    KillTimer( preview_hwnd, TIMER_PEEK );
    KillTimer( preview_hwnd, TIMER_LEAVE );
    end_peek();
    release_thumbnail();
    ShowWindow( preview_hwnd, SW_HIDE );
    preview_target = NULL;
}

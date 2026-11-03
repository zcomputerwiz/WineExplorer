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

/* P9: a window that set DWMWA_HAS_ICONIC_BITMAP draws its own previews. Ask it (msg) and copy what its
 * DwmSetIconicThumbnail / DwmSetIconicLivePreviewBitmap left in the shared section (include/wine/dwm_protocol.h);
 * *info gets the header. NULL when it has none: the caller falls back to the live picture. */
static HBITMAP iconic_fetch( HWND hwnd, UINT msg, LPARAM lparam, const WCHAR *prefix, struct dwm_iconic *info )
{
    BITMAPINFO bmi = {{ sizeof(BITMAPINFOHEADER) }};
    const struct dwm_iconic *header;
    HBITMAP bitmap = NULL;
    HANDLE section;
    WCHAR name[64];
    DWORD has = 0, i, count, *bits;
    BOOL any_alpha = FALSE;

    if (WexDwmpGetAttr( hwnd, DWMWA_HAS_ICONIC_BITMAP, &has ) != S_OK || !has) return NULL;
    SendMessageTimeoutW( hwnd, msg, 0, lparam, SMTO_ABORTIFHUNG, 1000, NULL );
    swprintf( name, ARRAY_SIZE(name), L"%s%lx", prefix, HandleToUlong( hwnd ) );
    if (!(section = OpenFileMappingW( FILE_MAP_READ, FALSE, name ))) return NULL;
    /* the header first, then as many pixels as it claims: mapping more than the section holds fails */
    if ((header = MapViewOfFile( section, FILE_MAP_READ, 0, 0, sizeof(*header) )))
    {
        *info = *header;
        UnmapViewOfFile( header );
        header = NULL;
        if (info->width && info->height && info->width <= DWM_ICONIC_MAX && info->height <= DWM_ICONIC_MAX)
            header = MapViewOfFile( section, FILE_MAP_READ, 0, 0,
                                    sizeof(*header) + (SIZE_T)info->width * info->height * 4 );
    }
    if (header)
    {
        count = info->width * info->height;
        bmi.bmiHeader.biWidth = info->width;
        bmi.bmiHeader.biHeight = -(int)info->height;
        bmi.bmiHeader.biPlanes = 1;
        bmi.bmiHeader.biBitCount = 32;
        bmi.bmiHeader.biCompression = BI_RGB;
        if ((bitmap = CreateDIBSection( NULL, &bmi, DIB_RGB_COLORS, (void **)&bits, NULL, 0 )))
        {
            memcpy( bits, header + 1, count * 4 );
            /* the window rewrote it while we copied: skip this picture */
            if (header->serial != info->serial || header->width != info->width || header->height != info->height)
            {
                DeleteObject( bitmap );
                bitmap = NULL;
            }
            else
            {
                /* a bitmap without alpha (all zero) is opaque */
                for (i = 0; i < count && !any_alpha; i++) any_alpha = bits[i] >> 24 != 0;
                for (i = 0; i < count && !any_alpha; i++) bits[i] |= 0xff000000;
            }
        }
        UnmapViewOfFile( header );
    }
    CloseHandle( section );
    return bitmap;
}

/* the largest size goes in lParam: width high, height low; *flags gets the DWM_SIT_* flags */
HBITMAP iconic_thumbnail( HWND hwnd, int max_w, int max_h, DWORD *flags )
{
    struct dwm_iconic info;
    HBITMAP bitmap = iconic_fetch( hwnd, WM_DWMSENDICONICTHUMBNAIL, MAKELPARAM( max_h, max_w ), DWM_ICONIC_NAME,
                                   &info );
    *flags = bitmap ? info.flags & DWM_ICONIC_SIT_FLAGS : 0;
    return bitmap;
}

/* With DWM_SIT_DISPLAYFRAME the picture is the window's content: a raised border and the window's caption go around
 * it, inside rect; caption_h is the caption height. */
void draw_iconic( HDC dc, HBITMAP bitmap, const RECT *rect, HWND hwnd, BOOL frame, int caption_h )
{
    BLENDFUNCTION blend = { AC_SRC_OVER, 0, 255, AC_SRC_ALPHA };
    RECT dest = *rect, caption;
    BITMAP bm;
    HDC mem;
    HBITMAP old;

    if (frame)
    {
        DrawEdge( dc, &dest, EDGE_RAISED, BF_RECT | BF_ADJUST | BF_MIDDLE );
        SetRect( &caption, dest.left, dest.top, dest.right, dest.top + min( caption_h, (dest.bottom - dest.top) / 3 ) );
        DrawCaption( hwnd, dc, &caption, DC_ACTIVE | DC_TEXT | DC_GRADIENT | DC_SMALLCAP );
        dest.top = caption.bottom;
    }
    if (dest.right <= dest.left || dest.bottom <= dest.top) return;
    GetObjectW( bitmap, sizeof(bm), &bm );
    mem = CreateCompatibleDC( dc );
    old = SelectObject( mem, bitmap );
    if (bm.bmWidth != dest.right - dest.left || bm.bmHeight != dest.bottom - dest.top)
        SetStretchBltMode( dc, HALFTONE );
    GdiAlphaBlend( dc, dest.left, dest.top, dest.right - dest.left, dest.bottom - dest.top,
                   mem, 0, 0, bm.bmWidth, bm.bmHeight, blend );
    SelectObject( mem, old );
    DeleteDC( mem );
}

#define PREVIEW_DELAY_MS 400
#define PREVIEW_MAX_W    200
#define PREVIEW_MAX_H    120
#define PREVIEW_MARGIN   8
#define PREVIEW_TITLE_H  20
#define ICONIC_CAPTION_H 12   /* the caption DWM_SIT_DISPLAYFRAME draws around a small preview */

#define PEEK_DELAY_MS    600
#define LEAVE_DELAY_MS   300

enum { TIMER_SHOW = 1, TIMER_PEEK, TIMER_LEAVE };

static HWND preview_hwnd, preview_target, preview_band;
static HTHUMBNAIL preview_thumb;
static RECT preview_button;   /* screen coordinates of the hovered button */
static HBITMAP preview_bitmap;   /* the window's own iconic picture (P9) */
static RECT preview_bitmap_rect;
static DWORD preview_flags;      /* its DWM_SIT_* flags */
static BOOL peeking, tracking;
/* Peek at a window that draws its own live preview (P9): a popup over its place shows the picture */
static HWND peek_hwnd;
static HBITMAP peek_bitmap;
static RECT peek_bitmap_rect;    /* in the popup */
static BOOL peek_frame;

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

extern BOOL dwm_wayland_handle( HWND hwnd, char handle[33] );   /* dwm_bridge.c */

static BOOL wayland_session(void)
{
    DWORD backend = 0, capabilities = 0;
    return !WexWmGetCapabilities( &backend, &capabilities ) && backend == WEX_WM_BACKEND_WAYLAND;
}

static void end_peek(void)
{
    if (peek_hwnd) ShowWindow( peek_hwnd, SW_HIDE );
    if (peek_bitmap) DeleteObject( peek_bitmap );
    peek_bitmap = NULL;
    if (!peeking) return;
    if (wayland_session()) WexWmSetPeek( NULL, NULL, 0 );
    else WexCompositionSetPeek( 0, NULL, 0 );
    peeking = FALSE;
}

static LRESULT CALLBACK peek_proc( HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam )
{
    switch (msg)
    {
    case WM_PAINT:
    {
        PAINTSTRUCT ps;
        RECT rc;
        HDC dc = BeginPaint( hwnd, &ps );
        GetClientRect( hwnd, &rc );
        if (peek_bitmap)
            draw_iconic( dc, peek_bitmap, peek_frame ? &rc : &peek_bitmap_rect, preview_target, peek_frame,
                         GetSystemMetrics( SM_CYCAPTION ) );
        EndPaint( hwnd, &ps );
        return 0;
    }
    case WM_NCHITTEST:
        return HTTRANSPARENT;
    case WM_MOUSEACTIVATE:
        return MA_NOACTIVATE;
    }
    return DefWindowProcW( hwnd, msg, wparam, lparam );
}

/* The window's live preview bitmap where the window is (its restored place when minimized); the popup is shaped to
 * the picture (and the frame, with DWM_SIT_DISPLAYFRAME) so the desktop shows around it. Returns the popup, NULL
 * when the window has no picture. */
static HWND show_iconic_peek(void)
{
    struct dwm_iconic info;
    RECT window, adjust = { 0 };
    HRGN region;
    int x, y;

    if (!(peek_bitmap = iconic_fetch( preview_target, WM_DWMSENDICONICLIVEPREVIEWBITMAP, 0, DWM_ICONIC_LIVE_NAME,
                                      &info ))) return NULL;
    /* the window's own process recorded where it goes when restored */
    SetRect( &window, info.window[0], info.window[1], info.window[2], info.window[3] );
    if (IsRectEmpty( &window ) && !GetWindowRect( preview_target, &window )) return NULL;
    peek_frame = !!(info.flags & DWM_SIT_DISPLAYFRAME);
    /* the offset is from the client area; without one the picture starts at the window's corner */
    AdjustWindowRectEx( &adjust, GetWindowLongW( preview_target, GWL_STYLE ), FALSE,
                        GetWindowLongW( preview_target, GWL_EXSTYLE ) );
    x = (peek_frame || (info.flags & DWM_ICONIC_HAS_POS)) ? -adjust.left : 0;
    y = (peek_frame || (info.flags & DWM_ICONIC_HAS_POS)) ? -adjust.top : 0;
    if (info.flags & DWM_ICONIC_HAS_POS)
    {
        x += info.x;
        y += info.y;
    }
    SetRect( &peek_bitmap_rect, x, y, x + info.width, y + info.height );
    if (!peek_hwnd)
    {
        WNDCLASSW cls = { 0 };
        cls.lpfnWndProc   = peek_proc;
        cls.hInstance     = GetModuleHandleW( NULL );
        cls.lpszClassName = L"WexIconicPeek";
        RegisterClassW( &cls );
        peek_hwnd = CreateWindowExW( WS_EX_TOOLWINDOW | WS_EX_TOPMOST | WS_EX_NOACTIVATE | WS_EX_TRANSPARENT,
                                     cls.lpszClassName, NULL, WS_POPUP, 0, 0, 1, 1, preview_hwnd, NULL,
                                     cls.hInstance, NULL );
        if (!peek_hwnd) return NULL;
    }
    region = peek_frame ? CreateRectRgn( 0, 0, window.right - window.left, window.bottom - window.top )
                        : CreateRectRgnIndirect( &peek_bitmap_rect );
    SetWindowRgn( peek_hwnd, region, FALSE );
    SetWindowPos( peek_hwnd, HWND_TOPMOST, window.left, window.top, window.right - window.left,
                  window.bottom - window.top, SWP_NOACTIVATE | SWP_SHOWWINDOW );
    /* the preview stays above the picture */
    SetWindowPos( preview_hwnd, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOACTIVATE | SWP_NOMOVE | SWP_NOSIZE );
    InvalidateRect( peek_hwnd, NULL, TRUE );
    UpdateWindow( peek_hwnd );
    return peek_hwnd;
}

struct peek_keep { UINT64 windows[8]; char handles[64][32]; DWORD count, handle_count; };

/* the shell's own visible top-level windows stay: desktop icons and wallpaper, taskbar, this preview */
static BOOL CALLBACK add_shell_window( HWND hwnd, LPARAM lparam )
{
    struct peek_keep *keep = (struct peek_keep *)lparam;
    DWORD pid;
    UINT64 window;

    char handle[33];

    GetWindowThreadProcessId( hwnd, &pid );
    if (pid != GetCurrentProcessId() || !IsWindowVisible( hwnd )) return TRUE;
    if (dwm_wayland_handle( hwnd, handle ))   /* Wayland: windows are named by shared handle */
    {
        if (keep->handle_count < ARRAY_SIZE(keep->handles)) memcpy( keep->handles[keep->handle_count++], handle, 32 );
        return TRUE;
    }
    if (!(window = x_window( hwnd ))) return TRUE;
    keep->windows[keep->count++] = window;
    return keep->count < ARRAY_SIZE(keep->windows);
}

static void start_peek(void)
{
    struct peek_keep keep = { { 0 }, { { 0 } }, 0, 0 };
    HWND shown = show_iconic_peek();
    UINT64 target = shown ? x_window( shown ) : 0;
    char handle[33];

    if (wayland_session())
    {
        HWND hwnd = shown ? shown : IsIconic( preview_target ) ? NULL : preview_target;
        if (!hwnd || !dwm_wayland_handle( hwnd, handle )) return;
        EnumWindows( add_shell_window, (LPARAM)&keep );
        peeking = !WexWmSetPeek( handle, (const char (*)[32])keep.handles, keep.handle_count );
        return;
    }
    if (!target && (!(target = x_window( preview_target )) || IsIconic( preview_target ))) return;
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

/* src_w x src_h scaled into max_w x max_h keeping the aspect ratio; larger than the source only with enlarge */
void fit_size( int src_w, int src_h, int max_w, int max_h, BOOL enlarge, int *w, int *h )
{
    *w = enlarge ? max_w : min( src_w, max_w );
    *h = MulDiv( *w, src_h, src_w );
    if (*h > max_h)
    {
        *h = max_h;
        *w = MulDiv( *h, src_w, src_h );
    }
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
    int w = PREVIEW_MAX_W, h = 0, cx, cy, x, y;   /* the popup's size without a picture */

    release_thumbnail();
    if (!IsWindow( preview_target )) return;
    if ((preview_bitmap = iconic_thumbnail( preview_target, PREVIEW_MAX_W, PREVIEW_MAX_H, &preview_flags )))
    {
        /* the window's own picture (P9): fit it into the popup, never enlarge it */
        GetObjectW( preview_bitmap, sizeof(bitmap), &bitmap );
        fit_size( bitmap.bmWidth, bitmap.bmHeight, PREVIEW_MAX_W, PREVIEW_MAX_H, FALSE, &w, &h );
        SetRect( &preview_bitmap_rect, PREVIEW_MARGIN, PREVIEW_TITLE_H + PREVIEW_MARGIN / 2,
                 PREVIEW_MARGIN + w, PREVIEW_TITLE_H + PREVIEW_MARGIN / 2 + h );
    }
    else if (window_frame_rect( preview_target, &source ) && source.right > source.left && source.bottom > source.top &&
        SUCCEEDED( DwmRegisterThumbnail( preview_hwnd, preview_target, &preview_thumb ) ))
    {
        fit_size( source.right - source.left, source.bottom - source.top, PREVIEW_MAX_W, PREVIEW_MAX_H, TRUE, &w, &h );
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
            draw_iconic( dc, preview_bitmap, &preview_bitmap_rect, preview_target,
                         preview_flags & DWM_SIT_DISPLAYFRAME, ICONIC_CAPTION_H );
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

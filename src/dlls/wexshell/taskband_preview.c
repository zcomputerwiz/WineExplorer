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
 * minimized before the shell started has none (iconic previews are P9), and the popup then shows its title. */

#include <windows.h>
#include <dwmapi.h>
#include <wexhost.h>
#include "taskband.h"

#define PREVIEW_DELAY_MS 400
#define PREVIEW_MAX_W    200
#define PREVIEW_MAX_H    120
#define PREVIEW_MARGIN   8
#define PREVIEW_TITLE_H  20

static HWND preview_hwnd, preview_target;
static HTHUMBNAIL preview_thumb;
static RECT preview_button;   /* screen coordinates of the hovered button */

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
}

static void show_preview(void)
{
    DWM_THUMBNAIL_PROPERTIES props = { 0 };
    RECT source;
    int w = PREVIEW_MAX_W, h = 0, cx, cy, x, y;

    release_thumbnail();
    if (!IsWindow( preview_target )) return;
    if (window_frame_rect( preview_target, &source ) && source.right > source.left && source.bottom > source.top &&
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
        KillTimer( hwnd, 1 );
        show_preview();
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
    preview_target = item->target_hwnd;
    preview_button = item->rect;
    MapWindowPoints( band, NULL, (POINT *)&preview_button, 2 );
    /* moving to another button while a preview shows switches at once, like on Windows */
    SetTimer( preview_hwnd, 1, IsWindowVisible( preview_hwnd ) ? 0 : PREVIEW_DELAY_MS, NULL );
}

void taskband_preview_hide(void)
{
    if (!preview_hwnd) return;
    KillTimer( preview_hwnd, 1 );
    release_thumbnail();
    ShowWindow( preview_hwnd, SW_HIDE );
    preview_target = NULL;
}

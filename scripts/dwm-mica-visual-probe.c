/*
 * WineExplorer Desktop Environment - visual probe for cached Mica backdrops (DWM P12a)
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

/* Three 400x300 sheet-of-glass windows: Mica light (40,40), Mica dark (480,40), and Glass only (40,380).
 * An argument names a log file.
 * Build: x86_64-w64-mingw32-gcc -municode -mwindows -o dwm-mica-visual-probe.exe dwm-mica-visual-probe.c -luser32 -lgdi32 -ldwmapi */

#include <windows.h>
#include <dwmapi.h>
#include <stdio.h>

#ifndef DWMWA_USE_IMMERSIVE_DARK_MODE
#define DWMWA_USE_IMMERSIVE_DARK_MODE 20
#endif
#ifndef DWMWA_SYSTEMBACKDROP_TYPE
#define DWMWA_SYSTEMBACKDROP_TYPE 38
#endif

static LRESULT CALLBACK mica_proc( HWND hwnd, UINT msg, WPARAM wp, LPARAM lp )
{
    if (msg == WM_PAINT)
    {
        PAINTSTRUCT ps;
        RECT rc;
        HDC dc = BeginPaint( hwnd, &ps );
        GetClientRect( hwnd, &rc );
        SetBkMode( dc, TRANSPARENT );
        SetTextColor( dc, RGB(0, 0, 0) );
        DrawTextW( dc, L"Mica probe", -1, &rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE );
        EndPaint( hwnd, &ps );
        return 0;
    }
    return DefWindowProcW( hwnd, msg, wp, lp );
}

int WINAPI wWinMain( HINSTANCE inst, HINSTANCE prev, LPWSTR cmdline, int show )
{
    static const struct
    {
        int x, y;
        const WCHAR *title;
        BOOL dark;
        DWORD backdrop;
    } windows[] =
    {
        { 40, 40, L"Mica light", FALSE, 2 },
        { 480, 40, L"Mica dark", TRUE, 2 },
        { 40, 380, L"Glass only", FALSE, 0 },
    };
    MARGINS margins = { -1, -1, -1, -1 };
    WNDCLASSW wc = { 0 };
    MSG msg;
    DWORD i, deadline = GetTickCount() + 40000;

    if (cmdline && *cmdline) _wfreopen( cmdline, L"w", stdout );
    wc.lpfnWndProc   = mica_proc;
    wc.hInstance     = inst;
    wc.hbrBackground = (HBRUSH)GetStockObject( BLACK_BRUSH );
    wc.lpszClassName = L"DwmMicaProbe";
    RegisterClassW( &wc );
    for (i = 0; i < ARRAYSIZE(windows); i++)
    {
        HWND hwnd = CreateWindowExW( 0, wc.lpszClassName, windows[i].title, WS_OVERLAPPEDWINDOW,
                                     windows[i].x, windows[i].y, 400, 300, NULL, NULL, inst, NULL );
        HRESULT hr_margins = S_OK, hr_backdrop = S_OK;

        hr_margins = DwmExtendFrameIntoClientArea( hwnd, &margins );
        if (windows[i].dark)
        {
            BOOL dark = TRUE;
            DwmSetWindowAttribute( hwnd, DWMWA_USE_IMMERSIVE_DARK_MODE, &dark, sizeof(dark) );
        }
        if (windows[i].backdrop)
        {
            DWORD backdrop = windows[i].backdrop;
            hr_backdrop = DwmSetWindowAttribute( hwnd, DWMWA_SYSTEMBACKDROP_TYPE, &backdrop, sizeof(DWORD) );
        }
        ShowWindow( hwnd, SW_SHOWNORMAL );
        UpdateWindow( hwnd );
        printf( "%ls: hwnd %p margins hr %08lx backdrop hr %08lx\n", windows[i].title, hwnd, hr_margins, hr_backdrop );
        fflush( stdout );
    }
    while (GetTickCount() < deadline)
    {
        while (PeekMessageW( &msg, NULL, 0, 0, PM_REMOVE )) DispatchMessageW( &msg );
        Sleep( 50 );
    }
    return 0;
}

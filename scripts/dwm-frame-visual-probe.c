/*
 * WineExplorer Desktop Environment - visual probe for modern non-client frames (DWM P10a)
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

/* Three normal windows: default (40,40), immersive dark mode (480,40), red caption with white text (40,380).
 * With DwmFrameEffects=1 each must show a flat modern caption with its title and minimize/maximize/close glyphs;
 * with the gate off the usual XP frames. An argument names a log file.
 * Build: x86_64-w64-mingw32-gcc -municode -mwindows -o dwm-frame-visual-probe.exe dwm-frame-visual-probe.c -luser32 -lgdi32 -ldwmapi */

#include <windows.h>
#include <dwmapi.h>
#include <stdio.h>

int WINAPI wWinMain( HINSTANCE inst, HINSTANCE prev, LPWSTR cmdline, int show )
{
    static const struct { int x, y; const WCHAR *title; DWORD attribute; DWORD value; } windows[] =
    {
        { 40, 40, L"Default frame", 0, 0 },
        { 480, 40, L"Dark mode frame", 20 /* DWMWA_USE_IMMERSIVE_DARK_MODE */, TRUE },
        { 40, 380, L"Red caption", 35 /* DWMWA_CAPTION_COLOR */, RGB(200, 0, 0) },
    };
    WNDCLASSW wc = { 0 };
    MSG msg;
    DWORD i, deadline = GetTickCount() + 40000;

    if (cmdline && *cmdline) _wfreopen( cmdline, L"w", stdout );
    wc.lpfnWndProc   = DefWindowProcW;
    wc.hInstance     = inst;
    wc.hbrBackground = (HBRUSH)GetStockObject( WHITE_BRUSH );
    wc.lpszClassName = L"DwmFrameProbe";
    RegisterClassW( &wc );
    for (i = 0; i < ARRAYSIZE(windows); i++)
    {
        HWND hwnd = CreateWindowExW( 0, wc.lpszClassName, windows[i].title, WS_OVERLAPPEDWINDOW,
                                     windows[i].x, windows[i].y, 400, 300, NULL, NULL, inst, NULL );
        HRESULT hr = S_OK;
        if (windows[i].attribute)
        {
            hr = DwmSetWindowAttribute( hwnd, windows[i].attribute, &windows[i].value, sizeof(DWORD) );
            if (windows[i].attribute == 35)
            {
                DWORD white = RGB(255, 255, 255);
                DwmSetWindowAttribute( hwnd, 36 /* DWMWA_TEXT_COLOR */, &white, sizeof(white) );
            }
        }
        ShowWindow( hwnd, SW_SHOWNORMAL );
        UpdateWindow( hwnd );
        printf( "%ls: hwnd %p attribute %lu hr %08lx\n", windows[i].title, hwnd, windows[i].attribute, hr );
        fflush( stdout );
    }
    while (GetTickCount() < deadline)
    {
        while (PeekMessageW( &msg, NULL, 0, 0, PM_REMOVE )) DispatchMessageW( &msg );
        Sleep( 50 );
    }
    return 0;
}

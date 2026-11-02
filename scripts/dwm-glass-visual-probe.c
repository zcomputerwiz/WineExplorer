/*
 * WineExplorer Desktop Environment - visual probe for DWM glass margins
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

/* A 400x300 window at (100,100) whose client area is black with a white 100x80 rectangle in the middle.
 * DwmExtendFrameIntoClientArea is called with margins {left 40, right 40, top 60, bottom 40}. Run under the
 * shell with Composition=1 and DwmAlpha=1, then grab the root window: the black bands inside the margins must
 * be transparent (the desktop shows through), while the black area inside the inner rectangle and the white
 * rectangle stay opaque. An argument names a log file (programs started through explorer have no stdout).
 * Build: x86_64-w64-mingw32-gcc -municode -mwindows -o dwm-glass-visual-probe.exe dwm-glass-visual-probe.c -luser32 -lgdi32 -ldwmapi */

#include <windows.h>
#include <dwmapi.h>
#include <stdio.h>

static LRESULT CALLBACK glass_proc( HWND hwnd, UINT msg, WPARAM wp, LPARAM lp )
{
    if (msg == WM_PAINT)
    {
        PAINTSTRUCT ps;
        RECT rc;
        HDC dc = BeginPaint( hwnd, &ps );
        HBRUSH black = CreateSolidBrush( RGB(0, 0, 0) ), white = CreateSolidBrush( RGB(255, 255, 255) );
        GetClientRect( hwnd, &rc );
        FillRect( dc, &rc, black );
        /* white 100x80 rectangle in the middle of the client area */
        rc.left += (rc.right - rc.left - 100) / 2;
        rc.top += (rc.bottom - rc.top - 80) / 2;
        rc.right = rc.left + 100;
        rc.bottom = rc.top + 80;
        FillRect( dc, &rc, white );
        DeleteObject( black );
        DeleteObject( white );
        EndPaint( hwnd, &ps );
        return 0;
    }
    return DefWindowProcW( hwnd, msg, wp, lp );
}

int WINAPI wWinMain( HINSTANCE inst, HINSTANCE prev, LPWSTR cmdline, int show )
{
    WNDCLASSW wc = { 0 };
    MARGINS margins = { 40, 40, 60, 40 };
    HWND hwnd;
    HRESULT hr;
    MSG msg;
    DWORD deadline = GetTickCount() + 40000;

    if (cmdline && *cmdline) _wfreopen( cmdline, L"w", stdout );

    wc.lpfnWndProc   = glass_proc;
    wc.hInstance     = inst;
    wc.lpszClassName = L"DwmGlassProbe";
    RegisterClassW( &wc );
    hwnd = CreateWindowExW( 0, L"DwmGlassProbe", L"glass", WS_OVERLAPPEDWINDOW | WS_VISIBLE,
                            100, 100, 400, 300, NULL, NULL, inst, NULL );
    UpdateWindow( hwnd );

    hr = DwmExtendFrameIntoClientArea( hwnd, &margins );
    printf( "hwnd %p DwmExtendFrameIntoClientArea hr %08lx\n", hwnd, hr );
    fflush( stdout );

    while (GetTickCount() < deadline)
    {
        while (PeekMessageW( &msg, NULL, 0, 0, PM_REMOVE ))
        {
            if (msg.message == WM_QUIT) return 0;
            DispatchMessageW( &msg );
        }
        Sleep( 50 );
    }
    return 0;
}

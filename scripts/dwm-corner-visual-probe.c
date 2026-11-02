/*
 * WineExplorer Desktop Environment - visual probe for DWM window corner preferences
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

/* Three black 300x200 popup windows at fixed places, each with another DWMWA_WINDOW_CORNER_PREFERENCE:
 * (100,100) DWMWCP_ROUND (2), (500,100) DWMWCP_ROUNDSMALL (3), (100,400) DWMWCP_DONOTROUND (1).
 * Run under the shell with Composition=1 and DwmService=1, then grab the root window: the first two must
 * show the desktop in their corner pixels, the third must stay black.
 * Build: x86_64-w64-mingw32-gcc -municode -mwindows -o dwm-corner-visual-probe.exe dwm-corner-visual-probe.c -luser32 */

#include <windows.h>
#include <stdio.h>

#define DWMWA_WINDOW_CORNER_PREFERENCE 33
#ifndef ARRAY_SIZE
#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))
#endif

static LRESULT CALLBACK proc( HWND hwnd, UINT msg, WPARAM wp, LPARAM lp )
{
    if (msg == WM_KEYDOWN && wp == VK_ESCAPE) PostQuitMessage( 0 );
    return DefWindowProcW( hwnd, msg, wp, lp );
}

int WINAPI wWinMain( HINSTANCE inst, HINSTANCE prev, LPWSTR cmdline, int show )
{
    static const struct { int x, y; int preference; } windows[] = { { 100, 100, 2 }, { 500, 100, 3 }, { 100, 400, 1 } };
    HRESULT (WINAPI *set_attribute)( HWND, DWORD, const void *, DWORD );
    HMODULE dwmapi = LoadLibraryW( L"dwmapi.dll" );
    WNDCLASSW wc = { 0 };
    MSG msg;
    DWORD i, deadline = GetTickCount() + 40000;

    /* launched through explorer there is no stdout: an argument names a log file */
    if (cmdline && *cmdline) _wfreopen( cmdline, L"w", stdout );
    wc.lpfnWndProc   = proc;
    wc.hInstance     = inst;
    wc.hbrBackground = (HBRUSH)GetStockObject( BLACK_BRUSH );
    wc.lpszClassName = L"DwmCornerProbe";
    RegisterClassW( &wc );
    set_attribute = (void *)GetProcAddress( dwmapi, "DwmSetWindowAttribute" );

    {
        WNDCLASSW bg = wc;
        bg.lpszClassName = L"DwmCornerBackdrop";
        bg.hbrBackground = (HBRUSH)GetStockObject( WHITE_BRUSH );
        RegisterClassW( &bg );
        CreateWindowExW( 0, L"DwmCornerBackdrop", L"backdrop", WS_POPUP | WS_VISIBLE, 0, 0, 1024, 768, NULL, NULL, inst, NULL );
    }
    for (i = 0; i < ARRAY_SIZE(windows); i++)
    {
        HWND hwnd = CreateWindowExW( WS_EX_TOPMOST, L"DwmCornerProbe", L"corner", WS_POPUP | WS_VISIBLE,
                                     windows[i].x, windows[i].y, 300, 200, NULL, NULL, inst, NULL );
        DWORD preference = windows[i].preference;
        HRESULT hr = set_attribute ? set_attribute( hwnd, DWMWA_WINDOW_CORNER_PREFERENCE, &preference, 4 ) : E_FAIL;
        printf( "window %p at %d,%d preference %lu hr %08lx\n", hwnd, windows[i].x, windows[i].y, preference, hr );
        fflush( stdout );
    }
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

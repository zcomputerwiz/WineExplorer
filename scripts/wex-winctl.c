/*
 * wex-winctl: show, move and report a top-level window by its title, for headless placement tests
 *
 * Copyright 2026 WineExplorer contributors
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
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see <https://www.gnu.org/licenses/>.
 */

/* wex-winctl monitors                 one line per monitor: left top right bottom (virtual-screen pixels)
 * wex-winctl TITLE state              window and client rectangles, and min/max/normal; exit 1 if there is none
 * wex-winctl TITLE max | restore      ShowWindow(SW_MAXIMIZE / SW_RESTORE)
 * wex-winctl TITLE move X Y           SetWindowPos to X,Y (window rectangle, size kept)
 * Build: x86_64-w64-mingw32-gcc -municode -O2 -o wex-winctl.exe scripts/wex-winctl.c -luser32 */

#define _WIN32_WINNT 0x0A00
#include <windows.h>
#include <stdio.h>

static BOOL CALLBACK monitor_proc( HMONITOR mon, HDC hdc, RECT *rect, LPARAM lp )
{
    printf( "%ld %ld %ld %ld\n", rect->left, rect->top, rect->right, rect->bottom );
    return TRUE;
}

int wmain( int argc, WCHAR **argv )
{
    RECT win, client;
    POINT origin = {0, 0};
    HWND hwnd;

    /* physical pixels, as the screenshots are */
    SetProcessDpiAwarenessContext( DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2 );
    if (argc >= 2 && !wcscmp( argv[1], L"monitors" ))
    {
        EnumDisplayMonitors( NULL, NULL, monitor_proc, 0 );
        return 0;
    }
    if (argc < 3)
    {
        fprintf( stderr, "usage: wex-winctl monitors | TITLE state | TITLE max | TITLE restore | TITLE move X Y\n" );
        return 2;
    }
    if (!(hwnd = FindWindowW( NULL, argv[1] ))) { printf( "none\n" ); return 1; }

    if (!wcscmp( argv[2], L"max" )) ShowWindow( hwnd, SW_MAXIMIZE );
    else if (!wcscmp( argv[2], L"restore" )) ShowWindow( hwnd, SW_RESTORE );
    else if (!wcscmp( argv[2], L"move" ) && argc >= 5)
        SetWindowPos( hwnd, NULL, _wtoi( argv[3] ), _wtoi( argv[4] ), 0, 0,
                      SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE );
    else if (wcscmp( argv[2], L"state" )) { fprintf( stderr, "unknown command\n" ); return 2; }

    GetWindowRect( hwnd, &win );
    GetClientRect( hwnd, &client );
    ClientToScreen( hwnd, &origin );
    printf( "window %ld %ld %ld %ld client %ld %ld %ld %ld %s\n", win.left, win.top, win.right, win.bottom,
            origin.x, origin.y, origin.x + client.right, origin.y + client.bottom,
            IsIconic( hwnd ) ? "min" : IsZoomed( hwnd ) ? "max" : "normal" );
    return 0;
}

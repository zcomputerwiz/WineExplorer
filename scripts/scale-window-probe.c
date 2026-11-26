/*
 * WineExplorer Desktop Environment - probe: a plain Wine window for the live output-scale check
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
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

/* A normal top-level window (title bar, border) at 150,100 with a pure blue client area, which stays until C:\scale.stop
 * exists. scripts/wexbuild-root/wayland-scale-cycle.sh finds it in a screenshot by its colour and checks that a live
 * output scale change keeps its top left and scales its size by the ratio, like a hosted native window.
 * Build: x86_64-w64-mingw32-gcc -municode -mwindows -o scale-window-probe.exe scale-window-probe.c -luser32 -lgdi32 */

#include <windows.h>

static LRESULT CALLBACK proc( HWND hwnd, UINT msg, WPARAM wp, LPARAM lp )
{
    if (msg == WM_TIMER)
    {
        if (GetFileAttributesW( L"C:\\scale.stop" ) != INVALID_FILE_ATTRIBUTES) PostQuitMessage( 0 );
        return 0;
    }
    return DefWindowProcW( hwnd, msg, wp, lp );
}

int WINAPI wWinMain( HINSTANCE inst, HINSTANCE prev, LPWSTR cmdline, int show )
{
    WNDCLASSW wc = {0};
    HWND hwnd;
    MSG msg;

    wc.lpfnWndProc = proc;
    wc.hInstance = inst;
    wc.hbrBackground = CreateSolidBrush( RGB(0, 0, 255) );
    wc.lpszClassName = L"ScaleWindowProbe";
    RegisterClassW( &wc );
    hwnd = CreateWindowExW( 0, L"ScaleWindowProbe", L"scale probe", WS_OVERLAPPEDWINDOW | WS_VISIBLE,
                            150, 100, 320, 220, NULL, NULL, inst, NULL );
    if (!hwnd) return 1;
    SetTimer( hwnd, 1, 500, NULL );
    while (GetMessageW( &msg, NULL, 0, 0 ) > 0) { TranslateMessage( &msg ); DispatchMessageW( &msg ); }
    return 0;
}

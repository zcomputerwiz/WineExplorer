/*
 * WineExplorer Desktop Environment - probe: a top-level window whose class has CS_DROPSHADOW (D3, Wayland)
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

/* Shows one orange popup window (200x120 at 400,300) of a class with CS_DROPSHADOW and runs until C:\shadow.stop
 * exists. With desktop composition on, the shell asks the compositor for the class shadow when the window is
 * shown; scripts/wexbuild-root/wayland-class-shadow.sh measures the pixels around it.
 * Build: x86_64-w64-mingw32-gcc -municode -mwindows -o class-shadow-probe.exe class-shadow-probe.c -luser32 */

#include <windows.h>

static LRESULT CALLBACK proc( HWND hwnd, UINT msg, WPARAM wp, LPARAM lp )
{
    if (msg == WM_TIMER)
    {
        if (GetFileAttributesW( L"C:\\shadow.stop" ) != INVALID_FILE_ATTRIBUTES) PostQuitMessage( 0 );
        return 0;
    }
    return DefWindowProcW( hwnd, msg, wp, lp );
}

int WINAPI wWinMain( HINSTANCE inst, HINSTANCE prev, LPWSTR cmdline, int show )
{
    WNDCLASSW wc = {0};
    HWND hwnd;
    MSG msg;

    wc.style = CS_DROPSHADOW;
    wc.lpfnWndProc = proc;
    wc.hInstance = inst;
    wc.hbrBackground = CreateSolidBrush( RGB(255, 140, 0) );
    wc.lpszClassName = L"ClassShadowProbe";
    RegisterClassW( &wc );
    hwnd = CreateWindowExW( WS_EX_TOOLWINDOW, L"ClassShadowProbe", L"class shadow", WS_POPUP | WS_VISIBLE,
                            400, 300, 200, 120, NULL, NULL, inst, NULL );
    if (!hwnd) return 1;
    SetTimer( hwnd, 1, 500, NULL );
    while (GetMessageW( &msg, NULL, 0, 0 ) > 0) { TranslateMessage( &msg ); DispatchMessageW( &msg ); }
    return 0;
}

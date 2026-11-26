/*
 * WineExplorer Desktop Environment - probe: can a program that runs across a shell restart still create windows (B121)
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

/* Every second it creates a new top-level window (and destroys the one before), and logs the count to the first argument
 * (default C:\xwin-probe.log) with the position of the newest window. It runs until C:\xwin.stop exists.
 * Build: x86_64-w64-mingw32-gcc -municode -mwindows -o xwin-probe.exe xwin-probe.c -luser32 */

#include <windows.h>
#include <stdio.h>
#include <wchar.h>

static const WCHAR *log_path = L"C:\\xwin-probe.log";
static HWND cur;
static int count;

static void say( const char *fmt, ... ) __attribute__((format(printf, 1, 2)));
static void say( const char *fmt, ... )
{
    va_list args;
    FILE *f = _wfopen( log_path, L"a" );

    if (!f) return;
    fprintf( f, "%lu ", GetTickCount() );
    va_start( args, fmt );
    vfprintf( f, fmt, args );
    va_end( args );
    fputc( 10, f );
    fclose( f );
}

static LRESULT CALLBACK proc( HWND hwnd, UINT msg, WPARAM wp, LPARAM lp )
{
    if (msg == WM_TIMER)
    {
        HWND next = CreateWindowW( L"XWinProbe", L"XWin Probe", WS_OVERLAPPEDWINDOW | WS_VISIBLE,
                                   50 + 10 * (++count % 20), 50, 300, 200, NULL, NULL, GetModuleHandleW( NULL ), NULL );
        RECT r = {0};
        if (next) GetWindowRect( next, &r );
        say( "created %d %s visible=%d rect=%ld,%ld", count, next ? "ok" : "FAILED", next && IsWindowVisible( next ),
             r.left, r.top );
        if (cur) DestroyWindow( cur );
        cur = next;
        if (GetFileAttributesW( L"C:\\xwin.stop" ) != INVALID_FILE_ATTRIBUTES) PostQuitMessage( 0 );
        return 0;
    }
    return DefWindowProcW( hwnd, msg, wp, lp );
}

int WINAPI wWinMain( HINSTANCE inst, HINSTANCE prev, LPWSTR cmdline, int show )
{
    WNDCLASSW wc = {0};
    HWND timer;
    MSG msg;

    if (cmdline && cmdline[0]) log_path = cmdline;
    wc.lpfnWndProc = proc;
    wc.hInstance = inst;
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wc.lpszClassName = L"XWinProbe";
    RegisterClassW( &wc );
    timer = CreateWindowW( L"XWinProbe", L"timer", WS_POPUP, 0, 0, 1, 1, NULL, NULL, inst, NULL );
    SetTimer( timer, 1, 1000, NULL );
    say( "started" );
    while (GetMessageW( &msg, NULL, 0, 0 ) > 0) { TranslateMessage( &msg ); DispatchMessageW( &msg ); }
    say( "ended" );
    return 0;
}

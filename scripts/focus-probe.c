/*
 * WineExplorer Desktop Environment - probe for the keyboard focus of a program that runs across a shell restart (B124)
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

/* One top-level window; every focus and activation message and, each half second, the foreground and focus windows,
 * go to the log (the first argument, default C:\focus-probe.log). The window keeps running until C:\focus.stop exists.
 * Build: x86_64-w64-mingw32-gcc -municode -mwindows -o focus-probe.exe focus-probe.c -luser32 */

#include <windows.h>
#include <stdio.h>
#include <wchar.h>

static const WCHAR *log_path = L"C:\\focus-probe.log";

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
    switch (msg)
    {
    case WM_ACTIVATE: say( "WM_ACTIVATE %s", LOWORD(wp) ? "active" : "inactive" ); break;
    case WM_SETFOCUS: say( "WM_SETFOCUS" ); break;
    case WM_KILLFOCUS: say( "WM_KILLFOCUS" ); break;
    case WM_KEYDOWN: say( "WM_KEYDOWN %lx", (unsigned long)wp ); break;
    case WM_TIMER:
        { HWND fg = GetForegroundWindow(); WCHAR cls[64] = L"", title[64] = L""; char c8[64], t8[64];
          if (fg) { GetClassNameW( fg, cls, 64 ); GetWindowTextW( fg, title, 64 ); }
          WideCharToMultiByte( CP_UTF8, 0, cls, -1, c8, sizeof(c8), NULL, NULL ); WideCharToMultiByte( CP_UTF8, 0, title, -1, t8, sizeof(t8), NULL, NULL );
          say( "fg=%p class=%s title=%s", fg, c8, t8 ); }
        say( "tick foreground=%s focus=%s active=%s", GetForegroundWindow() == hwnd ? "me" : "other",
             GetFocus() == hwnd ? "me" : "other", GetActiveWindow() == hwnd ? "me" : "other" );
        if (GetFileAttributesW( L"C:\\focus.stop" ) != INVALID_FILE_ATTRIBUTES) PostQuitMessage( 0 );
        break;
    case WM_DESTROY: PostQuitMessage( 0 ); break;
    }
    return DefWindowProcW( hwnd, msg, wp, lp );
}

int WINAPI wWinMain( HINSTANCE inst, HINSTANCE prev, LPWSTR cmdline, int show )
{
    WNDCLASSW wc = {0};
    HWND hwnd;
    MSG msg;

    if (cmdline && cmdline[0]) log_path = cmdline;
    wc.lpfnWndProc = proc;
    wc.hInstance = inst;
    wc.hCursor = LoadCursorW( NULL, (const WCHAR *)IDC_ARROW );
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wc.lpszClassName = L"FocusProbe";
    RegisterClassW( &wc );
    hwnd = CreateWindowW( L"FocusProbe", L"Focus Probe", WS_OVERLAPPEDWINDOW | WS_VISIBLE, 50, 50, 400, 300, NULL, NULL, inst, NULL );
    say( "created hwnd=%p", hwnd );
    SetForegroundWindow( hwnd );
    SetTimer( hwnd, 1, 500, NULL );
    while (GetMessageW( &msg, NULL, 0, 0 ) > 0) { TranslateMessage( &msg ); DispatchMessageW( &msg ); }
    return 0;
}

/*
 * WineExplorer Desktop Environment - probe for a program that runs across a restart of the shell (B120)
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

/* Started before the shell's explorer is killed. It makes a top-level window and a notification icon, logs
 * "survivor ready", and waits for C:\survivor.go (made after the new shell is up). Then it does what a program that
 * kept the old desktop window handle must still be able to do, one line each in C:\survivor-ARCH.log:
 *   iswindow ok|FAIL     its own window is still a window
 *   findwindow ok|FAIL   FindWindow("Shell_TrayWnd") finds the new shell's taskbar
 *   enumwindows N        EnumWindows reports N top-level windows
 *   newwin ok|FAIL       a new top-level window (the desktop window is its parent)
 *   owned ok|FAIL        a window owned by the first (an owned dialog)
 *                        (both "skipped" with the command line "nowin": on an X11 virtual desktop the program's X
 *                        windows went with the old explorer's X desktop window; creating one ends the program)
 *   taskbarcreated ok|FAIL  the "TaskbarCreated" broadcast of the restarted shell arrived (up to the go file + 20 s)
 *   trayicon ok|FAIL     the notification icon, added again on that message (Shell_NotifyIcon returned TRUE)
 * and "survivor done". It ends when C:\survivor.stop exists or after 120 s.
 * Build: x86_64-w64-mingw32-gcc -municode -mwindows -o dwm-survivor-probe64.exe dwm-survivor-probe.c -luser32 -lshell32
 *        i686-w64-mingw32-gcc  -municode -mwindows -o dwm-survivor-probe32.exe dwm-survivor-probe.c -luser32 -lshell32 */

#include <windows.h>
#include <shellapi.h>
#include <stdio.h>

#ifdef _WIN64
#define ARCH "64"
#else
#define ARCH "32"
#endif

static HWND main_window;
static UINT taskbar_created;
static int got_taskbar_created, trayicon_ok, started_done, no_windows;
static DWORD deadline, go_time;

static BOOL CALLBACK count_windows( HWND hwnd, LPARAM lp )
{
    ++*(int *)lp;
    return TRUE;
}

static BOOL add_icon( void )
{
    NOTIFYICONDATAW nid = { sizeof(nid) };

    nid.hWnd = main_window;
    nid.uID = 1;
    nid.uFlags = NIF_ICON | NIF_TIP;
    nid.hIcon = LoadIconW( NULL, (LPCWSTR)IDI_APPLICATION );
    lstrcpyW( nid.szTip, L"survivor" );
    return Shell_NotifyIconW( NIM_ADD, &nid );
}

static void run_checks( void )
{
    HWND newwin, owned;
    int count = 0;

    printf( "iswindow %s\n", IsWindow( main_window ) ? "ok" : "FAIL" );
    printf( "findwindow %s\n", FindWindowW( L"Shell_TrayWnd", NULL ) ? "ok" : "FAIL" );
    EnumWindows( count_windows, (LPARAM)&count );
    printf( "enumwindows %d\n", count );
    fflush( stdout );
    if (no_windows)
    {
        printf( "newwin skipped\n" );
        printf( "owned skipped\n" );
        fflush( stdout );
        return;
    }
    /* last: on an X11 virtual desktop the program's X windows are children of the old X desktop window, which went
     * with the old explorer, and creating one ends the program (X BadWindow) */
    newwin = CreateWindowExW( 0, L"SurvivorWnd", L"Survivor B", WS_OVERLAPPEDWINDOW, 300, 300, 200, 100, NULL, NULL,
                              GetModuleHandleW( NULL ), NULL );
    printf( "newwin %s\n", newwin && IsWindow( newwin ) ? "ok" : "FAIL" );
    fflush( stdout );
    owned = CreateWindowExW( 0, L"SurvivorWnd", L"Survivor dialog", WS_POPUP | WS_CAPTION, 320, 320, 200, 100, main_window,
                             NULL, GetModuleHandleW( NULL ), NULL );
    printf( "owned %s\n", owned && IsWindow( owned ) && GetWindow( owned, GW_OWNER ) == main_window ? "ok" : "FAIL" );
    fflush( stdout );
}

static LRESULT CALLBACK wnd_proc( HWND hwnd, UINT msg, WPARAM wp, LPARAM lp )
{
    if (taskbar_created && msg == taskbar_created)
    {
        /* the probe started after the first shell's taskbar: any message is the restarted shell's */
        got_taskbar_created = 1;
        if (add_icon()) trayicon_ok = 1;
        return 0;
    }
    if (msg == WM_TIMER)
    {
        if (!go_time)
        {
            if (GetFileAttributesW( L"C:\\survivor.go" ) != INVALID_FILE_ATTRIBUTES)
            {
                go_time = GetTickCount();
                run_checks();
            }
        }
        else if (!started_done && (got_taskbar_created || GetTickCount() - go_time > 20000))
        {
            printf( "taskbarcreated %s\n", got_taskbar_created ? "ok" : "FAIL" );
            printf( "trayicon %s\n", trayicon_ok ? "ok" : "FAIL" );
            printf( "survivor done\n" );
            fflush( stdout );
            started_done = 1;
        }
        if (GetFileAttributesW( L"C:\\survivor.stop" ) != INVALID_FILE_ATTRIBUTES || GetTickCount() > deadline)
            PostQuitMessage( 0 );
        return 0;
    }
    return DefWindowProcW( hwnd, msg, wp, lp );
}

int WINAPI wWinMain( HINSTANCE inst, HINSTANCE prev, LPWSTR cmdline, int show )
{
    WNDCLASSW wc = {0};
    WCHAR path[64];
    MSG msg;

    swprintf( path, 64, L"C:\\survivor-%hs.log", ARCH );
    _wfreopen( path, L"w", stdout );
    no_windows = cmdline && wcsstr( cmdline, L"nowin" ) != NULL;

    wc.hInstance     = inst;
    wc.lpfnWndProc   = wnd_proc;
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wc.lpszClassName = L"SurvivorWnd";
    RegisterClassW( &wc );
    taskbar_created = RegisterWindowMessageW( L"TaskbarCreated" );
    main_window = CreateWindowExW( 0, L"SurvivorWnd", L"Survivor A", WS_OVERLAPPEDWINDOW, 200, 200, 300, 150, NULL, NULL,
                                   inst, NULL );
    ShowWindow( main_window, SW_SHOWNOACTIVATE );
    add_icon();
    deadline = GetTickCount() + 120000;
    SetTimer( main_window, 1, 250, NULL );
    printf( "survivor ready\n" );
    fflush( stdout );
    while (GetMessageW( &msg, NULL, 0, 0 ))
    {
        TranslateMessage( &msg );
        DispatchMessageW( &msg );
    }
    return 0;
}

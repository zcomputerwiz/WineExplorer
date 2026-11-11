/*
 * WineExplorer Desktop Environment - probe for the DWM broker's change event
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

/* The broker (dwm.exe) sets the per-desktop event Local\__wex_dwm_change_<desktop> when an attribute changes,
 * so that the shell's bridge polls at once. Run on a desktop with DwmService=1 and without the shell's bridge
 * (Composition off), so that this probe is the only waiter:
 *   idle=PASS      nothing set: the event stays unsignaled for 400 ms
 *   signal=PASS    DwmSetWindowAttribute(DWMWA_WINDOW_CORNER_PREFERENCE): signaled within 100 ms (ms printed)
 *   again=PASS     a second, different value signals again
 * With the argument "set" it only creates a window, sets the attribute three times one second apart and
 * exits (for timing the bridge from traces).
 * Build: x86_64-w64-mingw32-gcc -municode -o dwm-change-event-probe.exe dwm-change-event-probe.c -luser32 */

#include <windows.h>
#include <stdio.h>

#define DWMWA_WINDOW_CORNER_PREFERENCE 33

static HRESULT (WINAPI *set_attribute)( HWND, DWORD, const void *, DWORD );

static double set_and_wait( HWND hwnd, HANDLE event, int value, DWORD *result )
{
    LARGE_INTEGER freq, start, end;
    HRESULT hr;

    QueryPerformanceFrequency( &freq );
    QueryPerformanceCounter( &start );
    hr = set_attribute( hwnd, DWMWA_WINDOW_CORNER_PREFERENCE, &value, sizeof(value) );
    *result = WaitForSingleObject( event, 2000 );
    QueryPerformanceCounter( &end );
    if (hr) printf( "set hr=%08lx\n", hr );
    return (end.QuadPart - start.QuadPart) * 1000.0 / freq.QuadPart;
}

int __cdecl wmain( int argc, WCHAR **argv )
{
    WCHAR desk_name[MAX_PATH], name[MAX_PATH + 64];
    HMODULE dwmapi = LoadLibraryW( L"dwmapi.dll" );
    HANDLE event;
    HWND hwnd;
    DWORD r;
    double ms;
    int fail = 0;

    if (!dwmapi || !(set_attribute = (void *)GetProcAddress( dwmapi, "DwmSetWindowAttribute" )))
    {
        printf( "no dwmapi\n" );
        return 2;
    }
    hwnd = CreateWindowExW( 0, L"STATIC", L"change event probe", WS_POPUP | WS_VISIBLE, 100, 100, 200, 100,
                            NULL, NULL, NULL, NULL );
    if (!hwnd) { printf( "no window\n" ); return 2; }

    if (argc > 1 && !lstrcmpW( argv[1], L"set" ))
    {
        int i, value;
        for (i = 0; i < 3; i++)
        {
            Sleep( 1000 );
            value = 2 + (i & 1);
            printf( "set %d at tick %lu\n", value, GetTickCount() );
            fflush( stdout );
            set_attribute( hwnd, DWMWA_WINDOW_CORNER_PREFERENCE, &value, sizeof(value) );
        }
        Sleep( 1000 );
        return 0;
    }

    if (!GetUserObjectInformationW( GetThreadDesktop( GetCurrentThreadId() ), UOI_NAME, desk_name,
                                    ARRAYSIZE( desk_name ), NULL )) return 2;
    lstrcpynW( name, L"Local\\__wex_dwm_change_", ARRAYSIZE( name ) );
    lstrcatW( name, desk_name );
    if (!(event = CreateEventW( NULL, FALSE, FALSE, name ))) { printf( "no event\n" ); return 2; }

    /* drain whatever window creation or an earlier program caused */
    while (WaitForSingleObject( event, 300 ) == WAIT_OBJECT_0) ;

    r = WaitForSingleObject( event, 400 );
    printf( "idle=%s\n", r == WAIT_TIMEOUT ? "PASS" : "FAIL" );
    if (r != WAIT_TIMEOUT) fail = 1;

    ms = set_and_wait( hwnd, event, 2, &r );
    printf( "signal=%s ms=%.1f\n", r == WAIT_OBJECT_0 && ms < 100.0 ? "PASS" : "FAIL", ms );
    if (r != WAIT_OBJECT_0 || ms >= 100.0) fail = 1;

    ms = set_and_wait( hwnd, event, 3, &r );
    printf( "again=%s ms=%.1f\n", r == WAIT_OBJECT_0 && ms < 100.0 ? "PASS" : "FAIL", ms );
    if (r != WAIT_OBJECT_0 || ms >= 100.0) fail = 1;

    DestroyWindow( hwnd );
    return fail;
}

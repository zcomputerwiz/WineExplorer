/*
 * WineExplorer Desktop Environment - visual probe for DWM live thumbnails
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

/* A 400x300 source at (50,50), left half red and right half blue, and a white 300x250 destination at (600,100)
 * that shows a thumbnail of the source in (20,20)-(220,170). Run under the shell with Composition=1 and
 * DwmService=1, then grab the root window: the destination must show the scaled red/blue source.
 * An argument names a log file (programs started through explorer have no stdout).
 * Build: x86_64-w64-mingw32-gcc -municode -mwindows -o dwm-thumbnail-visual-probe.exe dwm-thumbnail-visual-probe.c -luser32 -lgdi32 */

#include <windows.h>
#include <stdio.h>

typedef struct { DWORD dwFlags; RECT rcDestination; RECT rcSource; BYTE opacity; BOOL fVisible; BOOL fSourceClientAreaOnly; } THUMB_PROPS;
#define DWM_TNP_RECTDESTINATION 0x1
#define DWM_TNP_OPACITY         0x4
#define DWM_TNP_VISIBLE         0x8

static LRESULT CALLBACK source_proc( HWND hwnd, UINT msg, WPARAM wp, LPARAM lp )
{
    if (msg == WM_PAINT)
    {
        PAINTSTRUCT ps;
        RECT rc;
        HDC dc = BeginPaint( hwnd, &ps );
        HBRUSH red = CreateSolidBrush( RGB(255, 0, 0) ), blue = CreateSolidBrush( RGB(0, 0, 255) );
        GetClientRect( hwnd, &rc );
        rc.right /= 2;
        FillRect( dc, &rc, red );
        rc.left = rc.right;
        rc.right *= 2;
        FillRect( dc, &rc, blue );
        DeleteObject( red );
        DeleteObject( blue );
        EndPaint( hwnd, &ps );
        return 0;
    }
    return DefWindowProcW( hwnd, msg, wp, lp );
}

int WINAPI wWinMain( HINSTANCE inst, HINSTANCE prev, LPWSTR cmdline, int show )
{
    HRESULT (WINAPI *register_thumbnail)( HWND, HWND, HANDLE * );
    HRESULT (WINAPI *update_thumbnail)( HANDLE, const THUMB_PROPS * );
    HMODULE dwmapi = LoadLibraryW( L"dwmapi.dll" );
    WNDCLASSW wc = { 0 };
    THUMB_PROPS props = { 0 };
    HANDLE thumb = NULL;
    HWND source, dest;
    HRESULT hr = E_FAIL;
    MSG msg;
    DWORD deadline = GetTickCount() + 40000;

    if (cmdline && *cmdline) _wfreopen( cmdline, L"w", stdout );
    register_thumbnail = (void *)GetProcAddress( dwmapi, "DwmRegisterThumbnail" );
    update_thumbnail = (void *)GetProcAddress( dwmapi, "DwmUpdateThumbnailProperties" );

    wc.lpfnWndProc   = source_proc;
    wc.hInstance     = inst;
    wc.lpszClassName = L"DwmThumbSource";
    RegisterClassW( &wc );
    wc.lpfnWndProc   = DefWindowProcW;
    wc.hbrBackground = (HBRUSH)GetStockObject( WHITE_BRUSH );
    wc.lpszClassName = L"DwmThumbDest";
    RegisterClassW( &wc );
    source = CreateWindowExW( 0, L"DwmThumbSource", L"source", WS_POPUP | WS_VISIBLE, 50, 50, 400, 300,
                              NULL, NULL, inst, NULL );
    dest = CreateWindowExW( WS_EX_TOPMOST, L"DwmThumbDest", L"destination", WS_POPUP | WS_VISIBLE, 600, 100, 300, 250,
                            NULL, NULL, inst, NULL );
    UpdateWindow( source );
    UpdateWindow( dest );

    if (register_thumbnail && update_thumbnail && SUCCEEDED(hr = register_thumbnail( dest, source, &thumb )))
    {
        props.dwFlags = DWM_TNP_RECTDESTINATION | DWM_TNP_VISIBLE | DWM_TNP_OPACITY;
        SetRect( &props.rcDestination, 20, 20, 220, 170 );
        props.opacity = 255;
        props.fVisible = TRUE;
        hr = update_thumbnail( thumb, &props );
    }
    printf( "source %p dest %p thumbnail %p hr %08lx\n", source, dest, thumb, hr );
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

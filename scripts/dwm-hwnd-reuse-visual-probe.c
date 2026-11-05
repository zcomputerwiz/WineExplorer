/*
 * WineExplorer Desktop Environment - visual probe for DWM effect state on HWND slot reuse
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

/* HWND slot reuse (docs/DWM_UNIFICATION.md lifecycle scenario 4): window A carries DWM effects (rounded
 * corners, a Mica backdrop, dark mode); it is destroyed and a plain window B is created in its place,
 * retrying until the USER slot (the low 16 bits of the HWND) is A's again with a different generation.
 * B must not inherit A's applied state: its corners stay square and its client stays the green it paints.
 * A white full-screen window sits behind both, so a leaked rounded corner shows white at the window's
 * corner pixel while a square corner shows the frame.
 * Phase "a" (the default) keeps A alive for the screenshot; phase "cycle" runs the destroy/recreate and
 * keeps B alive. An argument names a log file.
 * Build: x86_64-w64-mingw32-gcc -municode -mwindows -o dwm-hwnd-reuse-visual-probe.exe dwm-hwnd-reuse-visual-probe.c -luser32 -lgdi32 -ldwmapi
 *        i686-w64-mingw32-gcc -municode -mwindows -o dwm-hwnd-reuse-visual-probe32.exe dwm-hwnd-reuse-visual-probe.c -luser32 -lgdi32 -ldwmapi */

#include <windows.h>
#include <dwmapi.h>
#include <stdio.h>

#ifndef DWMWA_USE_IMMERSIVE_DARK_MODE
#define DWMWA_USE_IMMERSIVE_DARK_MODE 20
#endif
#ifndef DWMWA_WINDOW_CORNER_PREFERENCE
#define DWMWA_WINDOW_CORNER_PREFERENCE 33
#endif
#ifndef DWMWA_SYSTEMBACKDROP_TYPE
#define DWMWA_SYSTEMBACKDROP_TYPE 38
#endif

#define WIN_X 40
#define WIN_Y 40
#define WIN_W 400
#define WIN_H 300
#define SLOT_MASK 0xffff   /* Wine HWND: (index << 1) + FIRST_USER_HANDLE in the low 16 bits, generation above */

static LRESULT CALLBACK a_proc( HWND hwnd, UINT msg, WPARAM wp, LPARAM lp )
{
    if (msg == WM_PAINT)
    {
        PAINTSTRUCT ps;
        RECT rc;
        HDC dc = BeginPaint( hwnd, &ps );
        GetClientRect( hwnd, &rc );
        SetBkMode( dc, TRANSPARENT );
        SetTextColor( dc, RGB(255, 255, 255) );
        DrawTextW( dc, L"A", -1, &rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE );
        EndPaint( hwnd, &ps );
        return 0;
    }
    return DefWindowProcW( hwnd, msg, wp, lp );
}

static LRESULT CALLBACK b_proc( HWND hwnd, UINT msg, WPARAM wp, LPARAM lp )
{
    static HBRUSH green = NULL;
    if (msg == WM_PAINT)
    {
        PAINTSTRUCT ps;
        RECT rc;
        HDC dc = BeginPaint( hwnd, &ps );
        GetClientRect( hwnd, &rc );
        if (!green) green = CreateSolidBrush( RGB(0, 255, 0) );
        FillRect( dc, &rc, green );
        SetBkMode( dc, TRANSPARENT );
        SetTextColor( dc, RGB(0, 0, 0) );
        DrawTextW( dc, L"B", -1, &rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE );
        EndPaint( hwnd, &ps );
        return 0;
    }
    return DefWindowProcW( hwnd, msg, wp, lp );
}

static LRESULT CALLBACK plain_proc( HWND hwnd, UINT msg, WPARAM wp, LPARAM lp )
{
    return DefWindowProcW( hwnd, msg, wp, lp );
}

static HWND make_window( HINSTANCE inst, const WCHAR *cls, const WCHAR *title )
{
    HWND hwnd = CreateWindowExW( 0, cls, title, WS_OVERLAPPEDWINDOW, WIN_X, WIN_Y, WIN_W, WIN_H,
                                 NULL, NULL, inst, NULL );
    ShowWindow( hwnd, SW_SHOWNORMAL );
    UpdateWindow( hwnd );
    return hwnd;
}

int WINAPI wWinMain( HINSTANCE inst, HINSTANCE prev, LPWSTR cmdline, int show )
{
    static const MARGINS sheet = { -1, -1, -1, -1 };
    WNDCLASSW wc = { 0 };
    MSG msg;
    DWORD deadline = GetTickCount() + 40000;
    BOOL cycle = FALSE;
    HWND backdrop, a = NULL, b = NULL;
    DWORD a_hwnd = 0, b_hwnd = 0, attempt;

    /* the first token is the phase ("a" or "cycle"); the rest names the log file */
    while (*cmdline == ' ') cmdline++;
    if (!wcsncmp( cmdline, L"cycle", 5 ))
    {
        cycle = TRUE;
        cmdline += 5;
    }
    while (*cmdline == ' ') cmdline++;
    if (*cmdline) _wfreopen( cmdline, L"w", stdout );

    wc.lpfnWndProc   = plain_proc;
    wc.hInstance     = inst;
    wc.hbrBackground = (HBRUSH)GetStockObject( WHITE_BRUSH );
    wc.lpszClassName = L"DwmReuseBackdrop";
    RegisterClassW( &wc );
    backdrop = CreateWindowExW( 0, wc.lpszClassName, L"backdrop", WS_POPUP | WS_VISIBLE,
                                0, 0, 1024, 768, NULL, NULL, inst, NULL );

    wc.lpfnWndProc   = a_proc;
    wc.hbrBackground = NULL;
    wc.lpszClassName = L"DwmReuseA";
    RegisterClassW( &wc );
    wc.lpfnWndProc   = b_proc;
    wc.hbrBackground = NULL;
    wc.lpszClassName = L"DwmReuseB";
    RegisterClassW( &wc );

    a = make_window( inst, L"DwmReuseA", L"Reuse A" );
    {
        DWORD backdrop_type = 2;   /* Mica */
        DWORD corner = 2;          /* DWMWCP_ROUND */
        BOOL dark = TRUE;
        DwmExtendFrameIntoClientArea( a, &sheet );
        DwmSetWindowAttribute( a, DWMWA_SYSTEMBACKDROP_TYPE, &backdrop_type, sizeof(backdrop_type) );
        DwmSetWindowAttribute( a, DWMWA_WINDOW_CORNER_PREFERENCE, &corner, sizeof(corner) );
        DwmSetWindowAttribute( a, DWMWA_USE_IMMERSIVE_DARK_MODE, &dark, sizeof(dark) );
    }
    a_hwnd = (DWORD)(ULONG_PTR)a;
    printf( "A hwnd %08lx slot %04lx\n", a_hwnd, a_hwnd & SLOT_MASK );
    fflush( stdout );

    if (cycle)
    {
        Sleep( 1500 );   /* let the bridge apply A's effects before the window goes away */
        DestroyWindow( a );
        a = NULL;
        for (attempt = 1; attempt <= 20; attempt++)
        {
            b = make_window( inst, L"DwmReuseB", L"Reuse B" );
            b_hwnd = (DWORD)(ULONG_PTR)b;
            printf( "B attempt %lu hwnd %08lx slot %04lx\n", attempt, b_hwnd, b_hwnd & SLOT_MASK );
            fflush( stdout );
            if ((b_hwnd & SLOT_MASK) == (a_hwnd & SLOT_MASK) && b_hwnd != a_hwnd) break;
            DestroyWindow( b );
            b = NULL;
            Sleep( 50 );
        }
        if ((b_hwnd & SLOT_MASK) != (a_hwnd & SLOT_MASK) || b_hwnd == a_hwnd)
            printf( "slot not reused after %lu attempts\n", attempt - 1 );
        else
            printf( "slot reused: A %08lx -> B %08lx (generation %04lx -> %04lx)\n",
                    a_hwnd, b_hwnd, a_hwnd >> 16, b_hwnd >> 16 );
        fflush( stdout );
    }

    while (GetTickCount() < deadline)
    {
        while (PeekMessageW( &msg, NULL, 0, 0, PM_REMOVE )) DispatchMessageW( &msg );
        Sleep( 50 );
    }
    return 0;
}

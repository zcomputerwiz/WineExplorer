/*
 * WineExplorer Desktop Environment - visual probe for live blur and Acrylic (DWM P12b)
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

/* A striped window fills the screen; over it four 360x260 windows: "Blur behind" (DwmEnableBlurBehindWindow,
 * 40,40), "Accent blur" (ACCENT_ENABLE_BLURBEHIND, 520,40), "Accent acrylic" (ACCENT_ENABLE_ACRYLICBLURBEHIND,
 * 40,380) and "Acrylic backdrop" (DWMWA_SYSTEMBACKDROP_TYPE 3, 520,380). The stripes move every 500 ms, so the
 * blur must follow what is below. With "2" as the first argument the four are "Region blur" (hRgnBlur, the left
 * half), "Tinted blur" (ACCENT_ENABLE_BLURBEHIND, flag 2, green 0x6000a000), "Tinted acrylic" (blue 0xa0800000)
 * and "Gradient" (ACCENT_ENABLE_TRANSPARENTGRADIENT, yellow 0x8000ffff). A further argument names a log file.
 * Build: x86_64-w64-mingw32-gcc -municode -mwindows -o dwm-blur-visual-probe.exe dwm-blur-visual-probe.c
 *        -luser32 -lgdi32 -ldwmapi */

#include <windows.h>
#include <dwmapi.h>
#include <stdio.h>

#ifndef DWMWA_SYSTEMBACKDROP_TYPE
#define DWMWA_SYSTEMBACKDROP_TYPE 38
#endif

struct accent_policy { DWORD state, flags, gradient, animation; };
struct composition_data { DWORD attrib; void *data; SIZE_T size; };
static BOOL (WINAPI *pSetWindowCompositionAttribute)( HWND, struct composition_data * );
static int phase;

static LRESULT CALLBACK stripes_proc( HWND hwnd, UINT msg, WPARAM wp, LPARAM lp )
{
    if (msg == WM_PAINT)
    {
        PAINTSTRUCT ps;
        RECT rc, band;
        HDC dc = BeginPaint( hwnd, &ps );
        int x;
        GetClientRect( hwnd, &rc );
        FillRect( dc, &rc, GetStockObject( WHITE_BRUSH ) );
        for (x = -64 + phase; x < rc.right; x += 64)
        {
            HBRUSH brush = CreateSolidBrush( (x / 64) & 1 ? RGB(200, 0, 0) : RGB(0, 0, 200) );
            SetRect( &band, x, 0, x + 32, rc.bottom );
            FillRect( dc, &band, brush );
            DeleteObject( brush );
        }
        EndPaint( hwnd, &ps );
        return 0;
    }
    return DefWindowProcW( hwnd, msg, wp, lp );
}

static LRESULT CALLBACK glass_proc( HWND hwnd, UINT msg, WPARAM wp, LPARAM lp )
{
    if (msg == WM_PAINT)
    {
        PAINTSTRUCT ps;
        RECT rc;
        WCHAR title[64];
        HDC dc = BeginPaint( hwnd, &ps );
        GetClientRect( hwnd, &rc );
        GetWindowTextW( hwnd, title, ARRAYSIZE(title) );
        SetBkMode( dc, TRANSPARENT );
        SetTextColor( dc, RGB(255, 255, 255) );
        DrawTextW( dc, title, -1, &rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE );
        EndPaint( hwnd, &ps );
        return 0;
    }
    return DefWindowProcW( hwnd, msg, wp, lp );
}

static BOOL set_accent( HWND hwnd, DWORD state, DWORD flags, DWORD gradient )
{
    struct accent_policy accent = { state, flags, gradient, 0 };
    struct composition_data data = { 19 /* WCA_ACCENT_POLICY */, &accent, sizeof(accent) };
    return pSetWindowCompositionAttribute && pSetWindowCompositionAttribute( hwnd, &data );
}

int WINAPI wWinMain( HINSTANCE inst, HINSTANCE prev, LPWSTR cmdline, int show )
{
    /* kind: 0 DwmEnableBlurBehindWindow, 5 system backdrop, 6 blur with a region, else the accent state */
    static const struct { int x, y; const WCHAR *title; int kind; DWORD flags, gradient; } sets[2][4] =
    {
        {
            { 40, 40, L"Blur behind", 0 },
            { 520, 40, L"Accent blur", 3 },
            { 40, 380, L"Accent acrylic", 4 },
            { 520, 380, L"Acrylic backdrop", 5 },
        },
        {
            { 40, 40, L"Region blur", 6 },
            { 520, 40, L"Tinted blur", 3, 2, 0x6000a000 },
            { 40, 380, L"Tinted acrylic", 4, 0, 0xa0800000 },
            { 520, 380, L"Gradient", 2, 0, 0x8000ffff },
        },
    };
    WNDCLASSW wc = { 0 };
    HWND stripes;
    MSG msg;
    DWORD i, deadline = GetTickCount() + 40000, next = 0;
    int set = 0;

    while (*cmdline == ' ') cmdline++;
    if (cmdline[0] == '2' && (!cmdline[1] || cmdline[1] == ' '))
    {
        set = 1;
        for (cmdline++; *cmdline == ' '; cmdline++) ;
    }
    if (*cmdline) _wfreopen( cmdline, L"w", stdout );
    pSetWindowCompositionAttribute = (void *)GetProcAddress( GetModuleHandleW( L"user32.dll" ),
                                                             "SetWindowCompositionAttribute" );
    wc.lpfnWndProc   = stripes_proc;
    wc.hInstance     = inst;
    wc.lpszClassName = L"DwmBlurStripes";
    RegisterClassW( &wc );
    stripes = CreateWindowExW( 0, wc.lpszClassName, L"Stripes", WS_POPUP, 0, 0, 1024, 740, NULL, NULL, inst, NULL );
    ShowWindow( stripes, SW_SHOWNOACTIVATE );
    UpdateWindow( stripes );

    wc.lpfnWndProc   = glass_proc;
    wc.hbrBackground = (HBRUSH)GetStockObject( BLACK_BRUSH );
    wc.lpszClassName = L"DwmBlurProbe";
    RegisterClassW( &wc );
    for (i = 0; i < 4; i++)
    {
        const typeof(sets[0][0]) *windows = sets[set];
        HWND hwnd = CreateWindowExW( 0, wc.lpszClassName, windows[i].title, WS_OVERLAPPEDWINDOW,
                                     windows[i].x, windows[i].y, 360, 260, NULL, NULL, inst, NULL );
        HRESULT hr = S_OK;
        if (windows[i].kind == 0 || windows[i].kind == 6)
        {
            DWM_BLURBEHIND bb = { DWM_BB_ENABLE, TRUE, NULL, FALSE };
            if (windows[i].kind == 6)
            {
                RECT rc;
                GetClientRect( hwnd, &rc );
                bb.dwFlags |= DWM_BB_BLURREGION;
                bb.hRgnBlur = CreateRectRgn( 0, 0, rc.right / 2, rc.bottom );
            }
            hr = DwmEnableBlurBehindWindow( hwnd, &bb );
            if (bb.hRgnBlur) DeleteObject( bb.hRgnBlur );
        }
        else if (windows[i].kind == 5)
        {
            DWORD backdrop = 3;
            hr = DwmSetWindowAttribute( hwnd, DWMWA_SYSTEMBACKDROP_TYPE, &backdrop, sizeof(backdrop) );
        }
        else hr = set_accent( hwnd, windows[i].kind, windows[i].flags, windows[i].gradient ) ?
                  S_OK : HRESULT_FROM_WIN32( GetLastError() );
        ShowWindow( hwnd, SW_SHOWNORMAL );
        UpdateWindow( hwnd );
        printf( "%ls: hwnd %p hr %08lx\n", windows[i].title, hwnd, hr );
        fflush( stdout );
    }
    while (GetTickCount() < deadline)
    {
        while (PeekMessageW( &msg, NULL, 0, 0, PM_REMOVE )) DispatchMessageW( &msg );
        if (GetTickCount() >= next)
        {
            phase = (phase + 16) % 64;
            InvalidateRect( stripes, NULL, FALSE );
            next = GetTickCount() + 500;
        }
        Sleep( 50 );
    }
    return 0;
}

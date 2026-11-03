/*
 * WineExplorer Desktop Environment - visual probe for iconic thumbnails (DWM P9)
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

/* Two 400x300 windows: Iconic probe (60,60, minimized; supplies a framed thumbnail and a framed live preview
 * bitmap) and Live probe (520,60).
 * An argument names a log file.
 * Build: x86_64-w64-mingw32-gcc -municode -mwindows -o dwm-iconic-visual-probe.exe dwm-iconic-visual-probe.c -luser32 -lgdi32 -ldwmapi */

#include <windows.h>
#include <dwmapi.h>
#include <stdio.h>

#ifndef DWMWA_FORCE_ICONIC_REPRESENTATION
#define DWMWA_FORCE_ICONIC_REPRESENTATION 7
#endif
#ifndef DWMWA_HAS_ICONIC_BITMAP
#define DWMWA_HAS_ICONIC_BITMAP 10
#endif
#ifndef WM_DWMSENDICONICTHUMBNAIL
#define WM_DWMSENDICONICTHUMBNAIL 0x0323
#endif
#ifndef WM_DWMSENDICONICLIVEPREVIEWBITMAP
#define WM_DWMSENDICONICLIVEPREVIEWBITMAP 0x0326
#endif

static LRESULT CALLBACK probe_proc( HWND hwnd, UINT msg, WPARAM wp, LPARAM lp )
{
    if (msg == WM_PAINT)
    {
        PAINTSTRUCT ps;
        RECT rc;
        WCHAR text[64];
        HDC dc = BeginPaint( hwnd, &ps );
        GetClientRect( hwnd, &rc );
        FillRect( dc, &rc, (HBRUSH)GetStockObject( WHITE_BRUSH ) );
        SetBkMode( dc, TRANSPARENT );
        SetTextColor( dc, RGB(0, 0, 0) );
        GetWindowTextW( hwnd, text, ARRAYSIZE(text) );
        DrawTextW( dc, text, -1, &rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE );
        EndPaint( hwnd, &ps );
        return 0;
    }
    if (msg == WM_DWMSENDICONICTHUMBNAIL)
    {
        int max_w = HIWORD( lp );
        int max_h = LOWORD( lp );
        int w = max_w < 160 ? max_w : 160;
        int h = max_h < 100 ? max_h : 100;
        BITMAPINFO bmi;
        void *bits = NULL;
        HBITMAP bitmap;
        HRESULT hr;
        int x, y;

        if (w <= 0) w = 160;
        if (h <= 0) h = 100;

        printf( "WM_DWMSENDICONICTHUMBNAIL: max_w %d max_h %d\n", max_w, max_h );
        fflush( stdout );

        ZeroMemory( &bmi, sizeof(bmi) );
        bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        bmi.bmiHeader.biWidth = w;
        bmi.bmiHeader.biHeight = -h;
        bmi.bmiHeader.biPlanes = 1;
        bmi.bmiHeader.biBitCount = 32;
        bmi.bmiHeader.biCompression = BI_RGB;

        bitmap = CreateDIBSection( NULL, &bmi, DIB_RGB_COLORS, &bits, NULL, 0 );
        if (bitmap && bits)
        {
            DWORD *pixels = (DWORD *)bits;
            for (y = 0; y < h; y++)
            {
                for (x = 0; x < w; x++)
                {
                    if (x < 10 || x >= w - 10 || y < 10 || y >= h - 10)
                    {
                        pixels[y * w + x] = 0xFFFFFF00;
                    }
                    else
                    {
                        DWORD r = (w > 1) ? (DWORD)((x * 255) / (w - 1)) : 0;
                        DWORD g = 255 - r;
                        DWORD b = r;
                        pixels[y * w + x] = (0xFF << 24) | (r << 16) | (g << 8) | b;
                    }
                }
            }
        }

        hr = DwmSetIconicThumbnail( hwnd, bitmap, DWM_SIT_DISPLAYFRAME );
        printf( "DwmSetIconicThumbnail: hr %08lx\n", hr );
        fflush( stdout );

        if (bitmap) DeleteObject( bitmap );
        return 0;
    }
    if (msg == WM_DWMSENDICONICLIVEPREVIEWBITMAP)
    {
        /* the client area: cyan with a dark blue 20-pixel border and the title */
        RECT rc = { 0, 0, 384, 262 };   /* the 400x300 window's client area */
        BITMAPINFO bmi = {{ sizeof(BITMAPINFOHEADER), 384, -262, 1, 32, BI_RGB }};
        DWORD *pixels = NULL;
        HBITMAP bitmap = CreateDIBSection( NULL, &bmi, DIB_RGB_COLORS, (void **)&pixels, NULL, 0 );
        HRESULT hr;
        int x, y;

        if (bitmap)
            for (y = 0; y < rc.bottom; y++)
                for (x = 0; x < rc.right; x++)
                    pixels[y * rc.right + x] = (x < 20 || x >= rc.right - 20 || y < 20 || y >= rc.bottom - 20)
                                               ? 0xff000080 : 0xff00e0e0;
        hr = DwmSetIconicLivePreviewBitmap( hwnd, bitmap, NULL, DWM_SIT_DISPLAYFRAME );
        printf( "DwmSetIconicLivePreviewBitmap: hr %08lx\n", hr );
        fflush( stdout );
        if (bitmap) DeleteObject( bitmap );
        return 0;
    }
    return DefWindowProcW( hwnd, msg, wp, lp );
}

int WINAPI wWinMain( HINSTANCE inst, HINSTANCE prev, LPWSTR cmdline, int show )
{
    static const struct
    {
        int x, y;
        const WCHAR *title;
        BOOL iconic;
    } windows[] =
    {
        { 60,  60, L"Iconic probe", TRUE  },
        { 520, 60, L"Live probe",   FALSE },
    };
    WNDCLASSW wc = { 0 };
    MSG msg;
    DWORD i, deadline = GetTickCount() + 40000;

    if (cmdline && *cmdline) _wfreopen( cmdline, L"w", stdout );
    wc.lpfnWndProc   = probe_proc;
    wc.hInstance     = inst;
    wc.hbrBackground = (HBRUSH)GetStockObject( WHITE_BRUSH );
    wc.lpszClassName = L"DwmIconicProbe";
    RegisterClassW( &wc );
    for (i = 0; i < ARRAYSIZE(windows); i++)
    {
        HWND hwnd = CreateWindowExW( 0, wc.lpszClassName, windows[i].title, WS_OVERLAPPEDWINDOW,
                                     windows[i].x, windows[i].y, 400, 300, NULL, NULL, inst, NULL );

        if (windows[i].iconic)
        {
            BOOL iconic = TRUE;
            HRESULT hr_force = DwmSetWindowAttribute( hwnd, DWMWA_FORCE_ICONIC_REPRESENTATION, &iconic, sizeof(BOOL) );
            HRESULT hr_bitmap = DwmSetWindowAttribute( hwnd, DWMWA_HAS_ICONIC_BITMAP, &iconic, sizeof(BOOL) );
            printf( "%ls: hwnd %p force hr %08lx bitmap hr %08lx\n", windows[i].title, hwnd, hr_force, hr_bitmap );
            fflush( stdout );
        }
        else
        {
            printf( "%ls: hwnd %p\n", windows[i].title, hwnd );
            fflush( stdout );
        }
        /* the iconic window starts minimized: Peek must show its live preview bitmap, not a live picture */
        ShowWindow( hwnd, windows[i].iconic ? SW_SHOWMINNOACTIVE : SW_SHOWNORMAL );
        UpdateWindow( hwnd );
    }
    while (GetTickCount() < deadline)
    {
        while (PeekMessageW( &msg, NULL, 0, 0, PM_REMOVE )) DispatchMessageW( &msg );
        Sleep( 50 );
    }
    return 0;
}

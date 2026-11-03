/*
 * WineExplorer Desktop Environment - visual style part dump (DWM P13)
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

/* Applies a .msstyles (first argument) and, for the classes DWMWindow and Window, prints every
 * defined part (1-100) with its size to the log (second argument) and draws them, state 1, into Z:\root\parts.bmp.
 * Build: x86_64-w64-mingw32-gcc -o theme-parts-probe.exe theme-parts-probe.c -luxtheme -lgdi32 -luser32 */

#include <windows.h>
#include <uxtheme.h>
#include <vssym32.h>
#include <stdio.h>

typedef HRESULT (WINAPI *OPEN)( LPCWSTR, LPCWSTR, LPCWSTR, HANDLE *, DWORD );
typedef HRESULT (WINAPI *APPLY)( HANDLE, char *, HWND );

int wmain( int argc, WCHAR **argv )
{
    static const WCHAR *classes[] = { L"Window", L"DWMWindow" };
    HMODULE ux = LoadLibraryW( L"uxtheme.dll" );
    OPEN open = (OPEN)GetProcAddress( ux, MAKEINTRESOURCEA(2) );
    APPLY apply = (APPLY)GetProcAddress( ux, MAKEINTRESOURCEA(4) );
    BITMAPINFO info = {{ sizeof(BITMAPINFOHEADER), 1600, -400, 1, 32, BI_RGB }};
    BITMAPFILEHEADER file = { 0x4d42 };
    HANDLE theme, out;
    HDC dc = CreateCompatibleDC( NULL );
    HBITMAP bmp;
    void *bits;
    int c, part, x, row;
    DWORD written;
    RECT all = { 0, 0, 1600, 400 };

    if (argc >= 3) _wfreopen( argv[2], L"w", stdout );
    if (argc < 2 || !open || !apply || FAILED(open( argv[1], NULL, NULL, &theme, 0 )) ||
        FAILED(apply( theme, NULL, NULL ))) return 1;
    bmp = CreateDIBSection( dc, &info, DIB_RGB_COLORS, &bits, NULL, 0 );
    SelectObject( dc, bmp );
    FillRect( dc, &all, GetStockObject( GRAY_BRUSH ) );
    for (c = 0; c < 2; c++)
    {
        HTHEME h = OpenThemeData( NULL, classes[c] );
        printf( "%ls: %p\n", classes[c], h );
        if (!h) continue;
        for (part = 1, x = 4, row = 0; part <= 100; part++)
        {
            SIZE size = { 0 };
            RECT r;
            if (!IsThemePartDefined( h, part, 0 )) continue;
            HRESULT hr = GetThemePartSize( h, dc, part, 1, NULL, TS_TRUE, &size );
            printf( "  part %d: %ldx%ld %lx", part, size.cx, size.cy, hr );
            if (SUCCEEDED(GetThemeRect( h, part, 1, 8002 /* TMT_ATLASRECT */, &r )))
            {
                MARGINS m = { -9, -9, -9, -9 };
                BOOL border = -9;
                int sizing = -9;
                GetThemeMargins( h, dc, part, 1, TMT_SIZINGMARGINS, NULL, &m );
                GetThemeBool( h, part, 1, TMT_BORDERONLY, &border );
                GetThemeEnumValue( h, part, 1, TMT_SIZINGTYPE, &sizing );
                printf( " atlas %ld,%ld,%ld,%ld margins %d,%d,%d,%d borderonly %d sizing %d", r.left, r.top, r.right,
                        r.bottom, m.cxLeftWidth, m.cxRightWidth, m.cyTopHeight, m.cyBottomHeight, border, sizing );
            }
            if (size.cx < 4 || size.cx > 200) size.cx = 40;   /* unknown: a sample size */
            if (size.cy < 4 || size.cy > 90) size.cy = 30;
            if (x + size.cx > 1596) { x = 4; row++; }
            SetRect( &r, x, 4 + c * 200 + row * 96, x + size.cx, 4 + c * 200 + row * 96 + size.cy );
            printf( " draw %lx\n", DrawThemeBackground( h, dc, part, 1, &r, NULL ) );
            x += size.cx + 4;
        }
    }
    GdiFlush();
    file.bfOffBits = sizeof(file) + sizeof(info.bmiHeader);
    file.bfSize = file.bfOffBits + 1600 * 400 * 4;
    out = CreateFileW( L"Z:\\root\\parts.bmp", GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, 0, NULL );
    WriteFile( out, &file, sizeof(file), &written, NULL );
    WriteFile( out, &info.bmiHeader, sizeof(info.bmiHeader), &written, NULL );
    WriteFile( out, bits, 1600 * 400 * 4, &written, NULL );
    CloseHandle( out );
    return 0;
}

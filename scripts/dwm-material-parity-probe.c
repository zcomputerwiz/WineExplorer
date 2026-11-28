/*
 * dwm-material-parity-probe: one sheet-of-glass window per system material, light and dark, for the X11 and Wayland
 * composers' tint parity check (wex_material_default_tint in src/include/wexhost.h)
 *
 * Copyright 2026 WineExplorer contributors
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
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see <https://www.gnu.org/licenses/>.
 */

/* Eight 220x150 windows whose whole client area is a system backdrop, at known places (the client's top left is the
 * window's plus the frame, so check the middle of the window): row 1 (y 40): Mica light (40), Mica dark (290),
 * Glass only (540, no backdrop type: blur behind, no tint); row 2 (y 240): Acrylic light (40), Acrylic dark (290),
 * Mica Alt light (540); row 3 (y 440), an app's own tint through ACCENT_POLICY: Acrylic with tint alpha 0 (40: the
 * blur alone) and a gradient of 50 % red (290). ARGUMENT: the seconds to stay (default 40).
 * Build: x86_64-w64-mingw32-gcc -municode -mwindows -o dwm-material-parity-probe.exe dwm-material-parity-probe.c
 *        -luser32 -lgdi32 -ldwmapi */

#include <windows.h>
#include <dwmapi.h>
#include <stdlib.h>

#ifndef DWMWA_USE_IMMERSIVE_DARK_MODE
#define DWMWA_USE_IMMERSIVE_DARK_MODE 20
#endif
#ifndef DWMWA_SYSTEMBACKDROP_TYPE
#define DWMWA_SYSTEMBACKDROP_TYPE 38
#endif

typedef struct { DWORD state, flags, gradient, animation; } ACCENT_POLICY;
typedef struct { DWORD attrib; void *data; SIZE_T size; } WINDOWCOMPOSITIONATTRIBDATA;
typedef BOOL (WINAPI *SetWindowCompositionAttribute_t)( HWND, WINDOWCOMPOSITIONATTRIBDATA * );

int WINAPI wWinMain( HINSTANCE inst, HINSTANCE prev, LPWSTR cmdline, int show )
{
    SetWindowCompositionAttribute_t set_composition_attribute =
        (SetWindowCompositionAttribute_t)GetProcAddress( GetModuleHandleW( L"user32.dll" ), "SetWindowCompositionAttribute" );
    static const struct { int x, y; const WCHAR *title; BOOL dark; DWORD backdrop, accent, gradient; } windows[] =
    {
        {  40,  40, L"Mica light",     FALSE, 2 },
        { 290,  40, L"Mica dark",      TRUE,  2 },
        { 540,  40, L"Glass only",     FALSE, 0 },
        {  40, 240, L"Acrylic light",  FALSE, 3 },
        { 290, 240, L"Acrylic dark",   TRUE,  3 },
        { 540, 240, L"Mica Alt light", FALSE, 4 },
        /* an app's own tint through ACCENT_POLICY (no backdrop type): backdrop is the accent state, 0xAABBGGRR */
        {  40, 440, L"Acrylic tint 0", FALSE, 0, 4, 0x00ffffff },
        { 290, 440, L"Gradient red 50", FALSE, 0, 2, 0x800000ff },
    };
    MARGINS margins = { -1, -1, -1, -1 };
    WNDCLASSW wc = { 0 };
    MSG msg;
    DWORD i, deadline = GetTickCount() + (cmdline && *cmdline ? _wtoi( cmdline ) : 40) * 1000;

    wc.lpfnWndProc   = DefWindowProcW;
    wc.hInstance     = inst;
    wc.hbrBackground = (HBRUSH)GetStockObject( BLACK_BRUSH );
    wc.lpszClassName = L"DwmMaterialParityProbe";
    RegisterClassW( &wc );
    for (i = 0; i < ARRAYSIZE(windows); i++)
    {
        HWND hwnd = CreateWindowExW( 0, wc.lpszClassName, windows[i].title, WS_OVERLAPPEDWINDOW,
                                     windows[i].x, windows[i].y, 220, 150, NULL, NULL, inst, NULL );

        DwmExtendFrameIntoClientArea( hwnd, &margins );
        if (windows[i].dark)
        {
            BOOL dark = TRUE;
            DwmSetWindowAttribute( hwnd, DWMWA_USE_IMMERSIVE_DARK_MODE, &dark, sizeof(dark) );
        }
        if (windows[i].backdrop)
        {
            DWORD backdrop = windows[i].backdrop;
            DwmSetWindowAttribute( hwnd, DWMWA_SYSTEMBACKDROP_TYPE, &backdrop, sizeof(DWORD) );
        }
        if (windows[i].accent && set_composition_attribute)
        {
            ACCENT_POLICY accent = { windows[i].accent, 0, windows[i].gradient, 0 };
            WINDOWCOMPOSITIONATTRIBDATA data = { 19 /* WCA_ACCENT_POLICY */, &accent, sizeof(accent) };
            set_composition_attribute( hwnd, &data );
        }
        ShowWindow( hwnd, SW_SHOWNORMAL );
        UpdateWindow( hwnd );
    }
    while (GetTickCount() < deadline)
    {
        while (PeekMessageW( &msg, NULL, 0, 0, PM_REMOVE )) DispatchMessageW( &msg );
        Sleep( 50 );
    }
    return 0;
}

/*
 * WineExplorer Desktop Environment - probe for the shadow under a popup menu (DWM P7b slice 2)
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

/* A plain orange window fills the screen and a popup menu (TrackPopupMenu) opens at (100,100) on it. The menu's window
 * class has CS_DROPSHADOW; the shadow that the compositor paints for it (SPI_GETDROPSHADOW) is what the test looks at.
 * It logs C:\menu.log: "SPI_GETDROPSHADOW n", "menu ready" and the menu window's rectangle ("menu rect l t r b" in
 * screen pixels, from the window that holds the menu), then runs until C:\menu.stop exists or 60 s have passed.
 * Build: x86_64-w64-mingw32-gcc -municode -mwindows -o dwm-menu-probe.exe dwm-menu-probe.c -luser32 -lgdi32 */

#include <windows.h>
#include <stdio.h>

static HWND owner;
static DWORD deadline;

static void log_menu_window( void )
{
    HWND menu = FindWindowW( L"#32768", NULL );
    RECT rect;

    if (menu && GetWindowRect( menu, &rect ))
        printf( "menu rect %ld %ld %ld %ld\n", rect.left, rect.top, rect.right, rect.bottom );
    else
        printf( "menu window not found\n" );
    printf( "menu ready\n" );
    fflush( stdout );
}

static LRESULT CALLBACK owner_proc( HWND hwnd, UINT msg, WPARAM wp, LPARAM lp )
{
    if (msg == WM_TIMER)
    {
        if (wp == 1)
        {
            KillTimer( hwnd, 1 );
            log_menu_window();
        }
        else if (GetFileAttributesW( L"C:\\menu.stop" ) != INVALID_FILE_ATTRIBUTES || GetTickCount() > deadline)
        {
            KillTimer( hwnd, 2 );
            EndMenu();
        }
        return 0;
    }
    return DefWindowProcW( hwnd, msg, wp, lp );
}

int WINAPI wWinMain( HINSTANCE inst, HINSTANCE prev, LPWSTR cmdline, int show )
{
    WNDCLASSW wc = {0};
    HWND backdrop;
    HMENU menu;
    BOOL shadow = FALSE;
    int i;

    _wfreopen( L"C:\\menu.log", L"w", stdout );
    SystemParametersInfoW( SPI_GETDROPSHADOW, 0, &shadow, 0 );
    printf( "SPI_GETDROPSHADOW %d\n", (int)shadow );

    wc.hInstance     = inst;
    wc.hbrBackground = CreateSolidBrush( RGB(255, 128, 0) );
    wc.lpfnWndProc   = DefWindowProcW;
    wc.lpszClassName = L"MenuBackdrop";
    RegisterClassW( &wc );
    backdrop = CreateWindowExW( 0, wc.lpszClassName, L"Backdrop", WS_POPUP, 0, 0, 1024, 740, NULL, NULL, inst, NULL );
    ShowWindow( backdrop, SW_SHOWNOACTIVATE );
    UpdateWindow( backdrop );

    wc.hbrBackground = NULL;
    wc.lpfnWndProc   = owner_proc;
    wc.lpszClassName = L"MenuOwner";
    RegisterClassW( &wc );
    owner = CreateWindowExW( WS_EX_TOOLWINDOW, wc.lpszClassName, L"Owner", WS_POPUP, 400, 400, 10, 10, NULL, NULL, inst, NULL );
    ShowWindow( owner, SW_SHOWNOACTIVATE );

    menu = CreatePopupMenu();
    for (i = 1; i <= 6; i++)
    {
        WCHAR text[32];
        swprintf( text, 32, L"Menu item %d", i );
        AppendMenuW( menu, MF_STRING, i, text );
    }
    deadline = GetTickCount() + 60000;
    SetTimer( owner, 1, 1500, NULL );   /* after the menu is up */
    SetTimer( owner, 2, 200, NULL );
    SetForegroundWindow( owner );
    TrackPopupMenu( menu, TPM_LEFTALIGN | TPM_TOPALIGN, 100, 100, 0, owner, NULL );
    return 0;
}

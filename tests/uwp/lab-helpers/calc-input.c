/*
 * WineExplorer UWP lab: types a calculation into the Store Calculator running under Wine
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
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

/*
 * usage: calc-input.exe [delay-seconds] [clicks|keys]
 *
 * Waits for the CoreWindow of the Calculator (class "Windows.UI.Core.CoreWindow"), then after the delay does 7 + 5 =
 * either with mouse clicks on the buttons (the 320x500 Standard layout; positions are relative to the window's client
 * area) or with the keys 7, +, 5, Enter. The steps are 8 seconds apart, as the shim's window readbacks are, so that
 * each readback shows the state after one step. Runs in the lab guest only, on its own display.
 */

#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static HWND core;

static void send_mouse( DWORD flags, int x, int y )
{
    INPUT input = { INPUT_MOUSE };

    input.mi.dx = x;
    input.mi.dy = y;
    input.mi.dwFlags = flags;
    SendInput( 1, &input, sizeof(input) );
}

static void click( int x, int y, const char *what )
{
    POINT pt = { x, y };
    int w = GetSystemMetrics( SM_CXSCREEN ), h = GetSystemMetrics( SM_CYSCREEN );

    ClientToScreen( core, &pt );
    send_mouse( MOUSEEVENTF_MOVE | MOUSEEVENTF_ABSOLUTE, (pt.x * 65535) / (w - 1), (pt.y * 65535) / (h - 1) );
    Sleep( 400 );
    send_mouse( MOUSEEVENTF_LEFTDOWN, 0, 0 );
    Sleep( 120 );
    send_mouse( MOUSEEVENTF_LEFTUP, 0, 0 );
    printf( "clicked %s at %ld,%ld\n", what, pt.x, pt.y );
    fflush( stdout );
}

static void tap( WORD vk, BOOL shift, const char *what )
{
    INPUT input[2] = { { INPUT_KEYBOARD }, { INPUT_KEYBOARD } };
    INPUT mod = { INPUT_KEYBOARD };

    if (shift)
    {
        mod.ki.wVk = VK_SHIFT;
        SendInput( 1, &mod, sizeof(mod) );
    }
    input[0].ki.wVk = vk;
    input[0].ki.wScan = MapVirtualKeyW( vk, MAPVK_VK_TO_VSC );
    input[1] = input[0];
    input[1].ki.dwFlags = KEYEVENTF_KEYUP;
    SendInput( 1, &input[0], sizeof(INPUT) );
    Sleep( 80 );
    SendInput( 1, &input[1], sizeof(INPUT) );
    if (shift)
    {
        mod.ki.dwFlags = KEYEVENTF_KEYUP;
        SendInput( 1, &mod, sizeof(mod) );
    }
    printf( "key %s\n", what );
    fflush( stdout );
}

int main( int argc, char **argv )
{
    int delay = argc > 1 ? atoi( argv[1] ) : 12, i;
    BOOL keys = argc > 2 && !strcmp( argv[2], "keys" );
    HWND hwnd = NULL;

    for (i = 0; i < 3000 && !(hwnd && IsWindowVisible( hwnd )); i++)
    {
        Sleep( 100 );
        hwnd = FindWindowW( L"Windows.UI.Core.CoreWindow", NULL );
    }
    if (!hwnd || !IsWindowVisible( hwnd ))
    {
        printf( "no visible CoreWindow\n" );
        return 1;
    }
    core = hwnd;
    printf( "CoreWindow %p, waiting %d s\n", hwnd, delay );
    fflush( stdout );
    Sleep( delay * 1000 );
    SetForegroundWindow( hwnd );
    Sleep( 500 );

    if (!strcmp( argc > 2 ? argv[2] : "", "settings" ))
    {
        click( 22, 56, "navigation menu" );  Sleep( 16000 );
        click( 60, 470, "Settings" );        Sleep( 16000 );
        if (argc > 3)
        {
            click( 150, 160, "App theme" ); Sleep( 8000 );
            click( 150, 160, "App theme again" ); Sleep( 8000 );
            click( 150, 300, "About" ); Sleep( 8000 );
            click( 60, 22, "Settings header" ); Sleep( 12000 );
        }
    }
    else if (keys)
    {
        tap( '7', FALSE, "7" );     Sleep( 8000 );
        tap( VK_OEM_PLUS, TRUE, "+" ); Sleep( 8000 );
        tap( '5', FALSE, "5" );     Sleep( 8000 );
        tap( VK_RETURN, FALSE, "Enter" );
    }
    else
    {
        /* the buttons of the 320x500 Standard layout, from the window's readback */
        click( 43, 325, "7" );   Sleep( 8000 );
        click( 278, 422, "+" );  Sleep( 8000 );
        click( 121, 374, "5" );  Sleep( 8000 );
        click( 278, 472, "=" );
    }
    return 0;
}

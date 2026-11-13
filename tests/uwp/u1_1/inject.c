/*
 * WineExplorer U1.1b: input injector for the presentation probe (Wine runs; the Windows run is typed by hand)
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
 * Waits for the CoreWindow of the probe (window class "Windows.UI.Core.CoreWindow" on Wine) and does what the
 * owner did by hand on Windows: moves the mouse over the window, left and right click, one wheel notch up, then types
 * a, Shift+b, Enter, Left, F5. Prints one line per step.
 */

#include <windows.h>
#include <stdio.h>

static void send_mouse( DWORD flags, int x, int y, DWORD data )
{
    INPUT input = { INPUT_MOUSE };

    input.mi.dx = x;
    input.mi.dy = y;
    input.mi.mouseData = data;
    input.mi.dwFlags = flags;
    SendInput( 1, &input, sizeof(input) );
}

static void move_to( int x, int y )
{
    int w = GetSystemMetrics( SM_CXSCREEN ), h = GetSystemMetrics( SM_CYSCREEN );

    send_mouse( MOUSEEVENTF_MOVE | MOUSEEVENTF_ABSOLUTE, (x * 65535) / (w - 1), (y * 65535) / (h - 1), 0 );
}

static void send_key( WORD vk, BOOL up, BOOL extended )
{
    INPUT input = { INPUT_KEYBOARD };

    input.ki.wVk = vk;
    input.ki.wScan = MapVirtualKeyW( vk, MAPVK_VK_TO_VSC );
    input.ki.dwFlags = (up ? KEYEVENTF_KEYUP : 0) | (extended ? KEYEVENTF_EXTENDEDKEY : 0);
    SendInput( 1, &input, sizeof(input) );
}

static void tap( WORD vk, BOOL extended )
{
    send_key( vk, FALSE, extended );
    Sleep( 80 );
    send_key( vk, TRUE, extended );
    Sleep( 200 );
}

int main( int argc, char **argv )
{
    HWND hwnd = NULL;
    RECT rect;
    POINT center;
    int i;

    for (i = 0; i < 300 && !(hwnd && IsWindowVisible( hwnd )); i++)
    {
        Sleep( 100 );
        hwnd = FindWindowW( L"Windows.UI.Core.CoreWindow", NULL );
    }
    if (!hwnd || !IsWindowVisible( hwnd ))
    {
        printf( "no visible CoreWindow\n" );
        return 1;
    }
    Sleep( 1500 );
    SetForegroundWindow( hwnd );
    GetClientRect( hwnd, &rect );
    center.x = (rect.right - rect.left) / 2;
    center.y = (rect.bottom - rect.top) / 2;
    ClientToScreen( hwnd, &center );
    printf( "window %p, client centre %ld,%ld\n", hwnd, center.x, center.y );

    move_to( center.x - 30, center.y - 20 );
    Sleep( 300 );
    move_to( center.x, center.y );
    Sleep( 300 );
    move_to( center.x + 40, center.y + 20 );
    Sleep( 300 );
    send_mouse( MOUSEEVENTF_LEFTDOWN, 0, 0, 0 );
    Sleep( 100 );
    send_mouse( MOUSEEVENTF_LEFTUP, 0, 0, 0 );
    Sleep( 300 );
    send_mouse( MOUSEEVENTF_RIGHTDOWN, 0, 0, 0 );
    Sleep( 100 );
    send_mouse( MOUSEEVENTF_RIGHTUP, 0, 0, 0 );
    Sleep( 300 );
    send_mouse( MOUSEEVENTF_WHEEL, 0, 0, 120 );
    Sleep( 300 );
    printf( "mouse done\n" );

    tap( 'A', FALSE );
    send_key( VK_SHIFT, FALSE, FALSE );
    tap( 'B', FALSE );
    send_key( VK_SHIFT, TRUE, FALSE );
    Sleep( 200 );
    tap( VK_RETURN, FALSE );
    tap( VK_LEFT, TRUE );
    tap( VK_F5, FALSE );
    printf( "keys done\n" );
    return 0;
}

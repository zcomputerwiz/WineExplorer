/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright 2026 WineExplorer contributors */
/* what Uno's Win32 runtime does: a window class registered and used with a process handle as its instance */
#include <windows.h>
#include <stdio.h>

static LRESULT CALLBACK proc( HWND hwnd, UINT msg, WPARAM wp, LPARAM lp ) { return DefWindowProcW( hwnd, msg, wp, lp ); }

int main(void)
{
    HANDLE process;
    WNDCLASSEXW wc = { sizeof(wc) };
    ATOM atom;
    HWND hwnd;

    DuplicateHandle( GetCurrentProcess(), GetCurrentProcess(), GetCurrentProcess(), &process, 0, FALSE, DUPLICATE_SAME_ACCESS );
    wc.lpfnWndProc = proc;
    wc.hInstance = (HINSTANCE)process;
    wc.lpszClassName = L"UnoPlatformDispatcherWindow";
    atom = RegisterClassExW( &wc );
    printf( "instance %p atom %#x error %lu\n", process, atom, GetLastError() );
    {
        HANDLE other;   /* .NET opens a new handle for every Process.GetCurrentProcess().Handle */
        DuplicateHandle( GetCurrentProcess(), GetCurrentProcess(), GetCurrentProcess(), &other, 0, FALSE, DUPLICATE_SAME_ACCESS );
        hwnd = CreateWindowExW( 0, L"UnoPlatformDispatcherWindow", L"", WS_OVERLAPPED, 0, 0, 0, 0, HWND_MESSAGE, NULL, (HINSTANCE)other, NULL );
        printf( "other instance %p\n", other );
    }
    printf( "hwnd %p error %lu\n", hwnd, GetLastError() );
    return hwnd ? 0 : 1;
}

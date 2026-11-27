/*
 * WineExplorer Desktop Environment - probe for windows in session 0 (the null graphics driver default)
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

/* Usage: probe
 * Creates a top-level window, as a setup program's hidden window does, and prints:
 *   session=N          the process's session id
 *   window=ok|error E  whether CreateWindowExW gave a window (IsWindow)
 *   driver=NAME        the desktop's graphics driver (HKLM\System\CurrentControlSet\Control\Video\{guid}\0000
 *                      GraphicsDriver, the guid from the desktop window's __wine_display_device_guid), or
 *                      none: DriverError when explorer could not load one
 * Session 0 has no display: its default is the null driver, and its windows exist but are never shown. */

#include <windows.h>
#include <stdio.h>

#ifndef ARRAY_SIZE
#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))
#endif

int wmain(void)
{
    WCHAR guid[64], key[128], value[256];
    DWORD session = ~0u, size;
    ATOM atom;
    HWND hwnd;

    ProcessIdToSessionId(GetCurrentProcessId(), &session);
    printf("session=%lu\n", session);

    hwnd = CreateWindowExW(0, L"STATIC", L"session 0 window probe", WS_OVERLAPPEDWINDOW, 0, 0, 200, 100,
                           NULL, NULL, NULL, NULL);
    if (hwnd && IsWindow(hwnd)) printf("window=ok\n");
    else printf("window=error %lu\n", GetLastError());

    atom = (ATOM)(ULONG_PTR)GetPropW(GetDesktopWindow(), L"__wine_display_device_guid");
    if (!atom || !GlobalGetAtomNameW(atom, guid, ARRAY_SIZE(guid)))
        lstrcpyW(guid, L"00000000-0000-0000-0000-000000000000");
    _snwprintf(key, ARRAY_SIZE(key), L"System\\CurrentControlSet\\Control\\Video\\{%s}\\0000", guid);
    size = sizeof(value);
    if (!RegGetValueW(HKEY_LOCAL_MACHINE, key, L"GraphicsDriver", RRF_RT_REG_SZ, NULL, value, &size))
        printf("driver=%ls\n", value);
    else
    {
        size = sizeof(value);
        if (!RegGetValueW(HKEY_LOCAL_MACHINE, key, L"DriverError", RRF_RT_REG_SZ, NULL, value, &size))
            printf("driver=none: %ls\n", value);
        else printf("driver=none\n");
    }
    if (hwnd) DestroyWindow(hwnd);
    return 0;
}

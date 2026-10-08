/*
 * Network Connections Control Panel Applet (ncpa.cpl)
 *
 * Copyright 2026 WineExplorer Desktop Environment Project
 *
 * Provides the authentic Control Panel interface for launching and configuring
 * Network Connections.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301, USA
 */

#include <stdarg.h>
#include <windef.h>
#include <winbase.h>
#include <winuser.h>
#include <shellapi.h>
#include <cpl.h>

#include "wine/debug.h"
#include "resource.h"

WINE_DEFAULT_DEBUG_CHANNEL(ncpa);

static HINSTANCE g_hinst = NULL;

static void open_network_connections(HWND hwnd)
{
    TRACE("Opening Network Connections\n");
    ShellExecuteW(hwnd, L"open", L"explorer.exe", L"/wlan", NULL, SW_SHOWNORMAL);
}

LONG CALLBACK CPlApplet(HWND hwnd, UINT msg, LPARAM lparam1, LPARAM lparam2)
{
    TRACE("(%p, %u, 0x%Ix, 0x%Ix)\n", hwnd, msg, lparam1, lparam2);

    switch (msg)
    {
    case CPL_INIT:
        return TRUE;

    case CPL_GETCOUNT:
        return 1;

    case CPL_INQUIRE:
    {
        CPLINFO *info = (CPLINFO *)lparam2;
        info->idIcon = IDI_NCPA;
        info->idName = IDS_NCPA_TITLE;
        info->idInfo = IDS_NCPA_DESC;
        info->lData = 0;
        return 0;
    }

    case CPL_NEWINQUIRE:
    {
        NEWCPLINFOW *info = (NEWCPLINFOW *)lparam2;
        info->dwSize = sizeof(*info);
        info->dwFlags = 0;
        info->dwHelpContext = 0;
        info->lData = 0;
        info->hIcon = LoadIconW(g_hinst, MAKEINTRESOURCEW(IDI_NCPA));
        info->szHelpFile[0] = 0;
        LoadStringW(g_hinst, IDS_NCPA_TITLE, info->szName, ARRAY_SIZE(info->szName));
        LoadStringW(g_hinst, IDS_NCPA_DESC, info->szInfo, ARRAY_SIZE(info->szInfo));
        return 0;
    }

    case CPL_DBLCLK:
        open_network_connections(hwnd);
        return 0;

    case CPL_STARTWPARMSA:
    case CPL_STARTWPARMSW:
        open_network_connections(hwnd);
        return 1;

    case CPL_STOP:
    case CPL_EXIT:
        return 0;

    default:
        return 0;
    }
}

BOOL WINAPI DllMain(HINSTANCE hinst, DWORD reason, LPVOID reserved)
{
    if (reason == DLL_PROCESS_ATTACH)
    {
        g_hinst = hinst;
        DisableThreadLibraryCalls(hinst);
    }
    return TRUE;
}

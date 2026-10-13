/*
 * WineExplorer Desktop Environment - Date and Time Control Panel Applet (timedate.cpl)
 *
 * Copyright 2026 WineExplorer Desktop Environment Project
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

#include <windows.h>
#include <commctrl.h>
#include <cpl.h>
#include <ole2.h>

#include "timedate.h"

HINSTANCE g_hInstance = NULL;

static void show_timedate_dialog(HWND hwndParent)
{
    INITCOMMONCONTROLSEX init = {
        .dwSize = sizeof(INITCOMMONCONTROLSEX),
        .dwICC = ICC_DATE_CLASSES | ICC_UPDOWN_CLASS | ICC_TAB_CLASSES
    };
    PROPSHEETPAGEW pages[2];
    PROPSHEETHEADERW psh;
    ACTCTXW context_desc = {
        .cbSize = sizeof(ACTCTXW),
        .hModule = g_hInstance,
        .lpResourceName = MAKEINTRESOURCEW(124),
        .dwFlags = ACTCTX_FLAG_HMODULE_VALID | ACTCTX_FLAG_RESOURCE_NAME_VALID,
    };
    ULONG_PTR cookie = 0;
    HANDLE context;
    BOOL activated;

    OleInitialize(NULL);
    context = CreateActCtxW(&context_desc);
    if (context == INVALID_HANDLE_VALUE) activated = FALSE;
    else activated = ActivateActCtx(context, &cookie);

    InitCommonControlsEx(&init);

    memset(pages, 0, sizeof(pages));

    /* Date & Time page */
    pages[0].dwSize = sizeof(PROPSHEETPAGEW);
    pages[0].dwFlags = PSP_DEFAULT;
    pages[0].hInstance = g_hInstance;
    pages[0].pszTemplate = MAKEINTRESOURCEW(IDD_DATEANDTIME);
    pages[0].pfnDlgProc = dateandtime_page_proc;

    /* Time Zone page */
    pages[1].dwSize = sizeof(PROPSHEETPAGEW);
    pages[1].dwFlags = PSP_DEFAULT;
    pages[1].hInstance = g_hInstance;
    pages[1].pszTemplate = MAKEINTRESOURCEW(IDD_TIMEZONE);
    pages[1].pfnDlgProc = timezone_page_proc;

    memset(&psh, 0, sizeof(psh));
    psh.dwSize = sizeof(PROPSHEETHEADERW);
    psh.dwFlags = PSH_PROPSHEETPAGE | PSH_USEICONID;
    psh.hwndParent = hwndParent;
    psh.hInstance = g_hInstance;
    psh.pszCaption = MAKEINTRESOURCEW(IDS_CPL_NAME);
    psh.nPages = ARRAY_SIZE(pages);
    psh.ppsp = pages;

    PropertySheetW(&psh);

    if (activated) DeactivateActCtx(0, cookie);
    if (context != INVALID_HANDLE_VALUE) ReleaseActCtx(context);
    OleUninitialize();
}

LONG CALLBACK CPlApplet(HWND hwnd, UINT uMsg, LPARAM lParam1, LPARAM lParam2)
{
    switch (uMsg)
    {
    case CPL_INIT:
        clock_register_class(g_hInstance);
        return TRUE;

    case CPL_GETCOUNT:
        return 1;

    case CPL_INQUIRE:
    {
        CPLINFO *info = (CPLINFO *)lParam2;
        info->idIcon = ICO_MAIN;
        info->idName = IDS_CPL_NAME;
        info->idInfo = IDS_CPL_INFO;
        info->lData = 0;
        return TRUE;
    }

    case CPL_NEWINQUIRE:
    {
        NEWCPLINFOW *info = (NEWCPLINFOW *)lParam2;
        info->dwSize = sizeof(NEWCPLINFOW);
        info->dwFlags = 0;
        info->dwHelpContext = 0;
        info->lData = 0;
        info->hIcon = LoadIconW(g_hInstance, MAKEINTRESOURCEW(ICO_MAIN));
        info->szHelpFile[0] = 0;
        LoadStringW(g_hInstance, IDS_CPL_NAME, info->szName, ARRAY_SIZE(info->szName));
        LoadStringW(g_hInstance, IDS_CPL_INFO, info->szInfo, ARRAY_SIZE(info->szInfo));
        return 0;
    }

    case CPL_DBLCLK:
        show_timedate_dialog(hwnd);
        return 0;

    case CPL_STOP:
        clock_unregister_class(g_hInstance);
        return TRUE;

    case CPL_EXIT:
        return 0;
    }
    return 0;
}

BOOL WINAPI DllMain(HINSTANCE hinstDLL, DWORD fdwReason, LPVOID lpvReserved)
{
    if (fdwReason == DLL_PROCESS_ATTACH)
    {
        DisableThreadLibraryCalls(hinstDLL);
        g_hInstance = hinstDLL;
    }
    return TRUE;
}

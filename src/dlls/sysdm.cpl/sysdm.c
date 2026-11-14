/*
 * WineExplorer Desktop Environment - System Properties Control Panel Applet (sysdm.cpl)
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

#include "sysdm.h"

HINSTANCE g_hInstance = NULL;

static void show_system_properties(HWND hwndParent, UINT start_page)
{
    INITCOMMONCONTROLSEX init = {
        .dwSize = sizeof(INITCOMMONCONTROLSEX),
        .dwICC = ICC_TAB_CLASSES | ICC_LISTVIEW_CLASSES | ICC_UPDOWN_CLASS
    };
    PROPSHEETPAGEW pages[4];
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

    /* Page 0: General */
    pages[0].dwSize = sizeof(PROPSHEETPAGEW);
    pages[0].dwFlags = PSP_DEFAULT;
    pages[0].hInstance = g_hInstance;
    pages[0].pszTemplate = MAKEINTRESOURCEW(IDD_GENERAL);
    pages[0].pfnDlgProc = general_page_proc;

    /* Page 1: Computer Name */
    pages[1].dwSize = sizeof(PROPSHEETPAGEW);
    pages[1].dwFlags = PSP_DEFAULT;
    pages[1].hInstance = g_hInstance;
    pages[1].pszTemplate = MAKEINTRESOURCEW(IDD_COMPNAME);
    pages[1].pfnDlgProc = compname_page_proc;

    /* Page 2: Hardware */
    pages[2].dwSize = sizeof(PROPSHEETPAGEW);
    pages[2].dwFlags = PSP_DEFAULT;
    pages[2].hInstance = g_hInstance;
    pages[2].pszTemplate = MAKEINTRESOURCEW(IDD_HARDWARE);
    pages[2].pfnDlgProc = hardware_page_proc;

    /* Page 3: Advanced */
    pages[3].dwSize = sizeof(PROPSHEETPAGEW);
    pages[3].dwFlags = PSP_DEFAULT;
    pages[3].hInstance = g_hInstance;
    pages[3].pszTemplate = MAKEINTRESOURCEW(IDD_ADVANCED);
    pages[3].pfnDlgProc = advanced_page_proc;

    memset(&psh, 0, sizeof(psh));
    psh.dwSize = sizeof(PROPSHEETHEADERW);
    psh.dwFlags = PSH_PROPSHEETPAGE | PSH_USEICONID;
    psh.hwndParent = hwndParent;
    psh.hInstance = g_hInstance;
    psh.pszCaption = MAKEINTRESOURCEW(IDS_CPL_TITLE);
    psh.nPages = ARRAY_SIZE(pages);
    psh.nStartPage = start_page < ARRAY_SIZE(pages) ? start_page : 0;
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
        show_system_properties(hwnd, 0);
        return 0;

    case CPL_STARTWPARMSW:
    {
        /* "control sysdm.cpl,,3" opens the Advanced tab, as on XP: the parameters are the page's number */
        const WCHAR *params = (const WCHAR *)lParam2;
        UINT page = 0;

        if (params) while (*params == ' ' || *params == ',') params++;
        if (params && *params >= '0' && *params <= '9') page = wcstoul(params, NULL, 10);
        show_system_properties(hwnd, page);
        return TRUE;
    }

    case CPL_STOP:
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

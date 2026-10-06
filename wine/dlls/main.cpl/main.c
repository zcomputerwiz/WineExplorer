/*
 * WineExplorer Desktop Environment - Mouse & Keyboard Control Panel (main.cpl)
 *
 * CPlApplet Entry Point and Property Sheet Host
 */

#include "main_private.h"

HINSTANCE g_hinst = NULL;

BOOL WINAPI DllMain(HINSTANCE hinstDLL, DWORD fdwReason, LPVOID lpvReserved)
{
    if (fdwReason == DLL_PROCESS_ATTACH)
    {
        INITCOMMONCONTROLSEX icc;
        g_hinst = hinstDLL;
        icc.dwSize = sizeof(icc);
        icc.dwICC = ICC_LISTVIEW_CLASSES | ICC_UPDOWN_CLASS | ICC_BAR_CLASSES;
        InitCommonControlsEx(&icc);
        DisableThreadLibraryCalls(hinstDLL);
    }
    return TRUE;
}

LONG mouse_applet(HWND parent)
{
    PROPSHEETPAGEW psp[5];
    PROPSHEETHEADERW psh;
    HPROPSHEETPAGE hpsp[5];
    WCHAR title[128];

    LoadStringW(g_hinst, IDS_MOUSE_TITLE, title, ARRAY_SIZE(title));

    memset(psp, 0, sizeof(psp));

    /* Page 0: Buttons */
    psp[0].dwSize = sizeof(PROPSHEETPAGEW);
    psp[0].hInstance = g_hinst;
    psp[0].pszTemplate = MAKEINTRESOURCEW(DLG_MOUSE_BUTTONS);
    psp[0].pfnDlgProc = button_page_proc;

    /* Page 1: Pointers */
    psp[1].dwSize = sizeof(PROPSHEETPAGEW);
    psp[1].hInstance = g_hinst;
    psp[1].pszTemplate = MAKEINTRESOURCEW(DLG_MOUSE_POINTER);
    psp[1].pfnDlgProc = pointer_page_proc;

    /* Page 2: Pointer Options (Motion) */
    psp[2].dwSize = sizeof(PROPSHEETPAGEW);
    psp[2].hInstance = g_hinst;
    psp[2].pszTemplate = MAKEINTRESOURCEW(DLG_MOUSE_MOTION);
    psp[2].pfnDlgProc = motion_page_proc;

    /* Page 3: Wheel */
    psp[3].dwSize = sizeof(PROPSHEETPAGEW);
    psp[3].hInstance = g_hinst;
    psp[3].pszTemplate = MAKEINTRESOURCEW(DLG_MOUSE_WHEEL);
    psp[3].pfnDlgProc = wheel_page_proc;

    /* Page 4: Hardware */
    psp[4].dwSize = sizeof(PROPSHEETPAGEW);
    psp[4].hInstance = g_hinst;
    psp[4].pszTemplate = MAKEINTRESOURCEW(DLG_HARDWARE);
    psp[4].pfnDlgProc = mouse_hardware_page_proc;

    hpsp[0] = CreatePropertySheetPageW(&psp[0]);
    hpsp[1] = CreatePropertySheetPageW(&psp[1]);
    hpsp[2] = CreatePropertySheetPageW(&psp[2]);
    hpsp[3] = CreatePropertySheetPageW(&psp[3]);
    hpsp[4] = CreatePropertySheetPageW(&psp[4]);

    memset(&psh, 0, sizeof(psh));
    psh.dwSize = sizeof(psh);
    psh.dwFlags = PSH_USEICONID | PSH_PROPTITLE;
    psh.hwndParent = parent;
    psh.hInstance = g_hinst;
    psh.pszIcon = MAKEINTRESOURCEW(IDI_MOUSE);
    psh.pszCaption = title;
    psh.nPages = 5;
    psh.nStartPage = 0;
    psh.phpage = hpsp;

    return (LONG)PropertySheetW(&psh);
}

LONG keyboard_applet(HWND parent)
{
    PROPSHEETPAGEW psp[2];
    PROPSHEETHEADERW psh;
    HPROPSHEETPAGE hpsp[2];
    WCHAR title[128];

    LoadStringW(g_hinst, IDS_KEYBD_TITLE, title, ARRAY_SIZE(title));

    memset(psp, 0, sizeof(psp));

    /* Page 0: Speed */
    psp[0].dwSize = sizeof(PROPSHEETPAGEW);
    psp[0].hInstance = g_hinst;
    psp[0].pszTemplate = MAKEINTRESOURCEW(DLG_KEYBD_SPEED);
    psp[0].pfnDlgProc = keyboard_speed_page_proc;

    /* Page 1: Hardware */
    psp[1].dwSize = sizeof(PROPSHEETPAGEW);
    psp[1].hInstance = g_hinst;
    psp[1].pszTemplate = MAKEINTRESOURCEW(DLG_HARDWARE);
    psp[1].pfnDlgProc = keyboard_hardware_page_proc;

    hpsp[0] = CreatePropertySheetPageW(&psp[0]);
    hpsp[1] = CreatePropertySheetPageW(&psp[1]);

    memset(&psh, 0, sizeof(psh));
    psh.dwSize = sizeof(psh);
    psh.dwFlags = PSH_USEICONID | PSH_PROPTITLE;
    psh.hwndParent = parent;
    psh.hInstance = g_hinst;
    psh.pszIcon = MAKEINTRESOURCEW(IDI_KEYBD);
    psh.pszCaption = title;
    psh.nPages = 2;
    psh.nStartPage = 0;
    psh.phpage = hpsp;

    return (LONG)PropertySheetW(&psh);
}

LONG CALLBACK CPlApplet(HWND hwnd, UINT msg, LPARAM lp1, LPARAM lp2)
{
    switch (msg)
    {
    case CPL_INIT:
        return TRUE;

    case CPL_GETCOUNT:
        return 2;

    case CPL_INQUIRE:
    {
        CPLINFO *info = (CPLINFO *)lp2;
        if (lp1 == 0)
        {
            info->idIcon = IDI_MOUSE;
            info->idName = IDS_MOUSE_TITLE;
            info->idInfo = IDS_MOUSE_EXPLAIN;
            info->lData = 0;
        }
        else if (lp1 == 1)
        {
            info->idIcon = IDI_KEYBD;
            info->idName = IDS_KEYBD_TITLE;
            info->idInfo = IDS_KEYBD_EXPLAIN;
            info->lData = 1;
        }
        return TRUE;
    }

    case CPL_DBLCLK:
        if (lp1 == 0)
            return mouse_applet(hwnd);
        else if (lp1 == 1)
            return keyboard_applet(hwnd);
        return FALSE;

    case CPL_STARTWPARMSW:
        if (lp1 == 0)
            return mouse_applet(hwnd);
        else if (lp1 == 1)
            return keyboard_applet(hwnd);
        return FALSE;

    case CPL_STOP:
        return TRUE;
    }

    return FALSE;
}

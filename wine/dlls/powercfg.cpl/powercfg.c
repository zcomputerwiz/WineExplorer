/*
 * Wine Power Options Control Panel Applet (powercfg.cpl)
 *
 * Copyright 2026 WineExplorer Desktop Environment Project
 *
 * Implements the authentic Windows XP / Server 2003 Power Options Control Panel
 * applet, featuring Power Schemes, Power Meter, Advanced, and Hibernate pages.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <windows.h>
#include <commctrl.h>
#include <cpl.h>
#include <shellapi.h>

#include "resource.h"

static HINSTANCE g_hInstance = NULL;

static const WCHAR *g_scheme_names[] = {
    L"Home/Office Desk",
    L"Portable/Laptop",
    L"Presentation",
    L"Always On",
    L"Minimal Power Management",
    L"Max Battery"
};

static const WCHAR *g_timeouts_monitor[] = {
    L"After 1 min", L"After 2 mins", L"After 3 mins", L"After 5 mins",
    L"After 10 mins", L"After 15 mins", L"After 20 mins", L"After 25 mins",
    L"After 30 mins", L"After 45 mins", L"After 1 hour", L"After 2 hours", L"Never"
};

static const WCHAR *g_timeouts_disks[] = {
    L"After 3 mins", L"After 5 mins", L"After 10 mins", L"After 15 mins",
    L"After 20 mins", L"After 30 mins", L"After 45 mins", L"After 1 hour",
    L"After 2 hours", L"Never"
};

static const WCHAR *g_timeouts_standby[] = {
    L"After 1 min", L"After 2 mins", L"After 5 mins", L"After 10 mins",
    L"After 15 mins", L"After 20 mins", L"After 25 mins", L"After 30 mins",
    L"After 45 mins", L"After 1 hour", L"After 2 hours", L"Never"
};

/* Page 1: Power Schemes */
static void populate_timeout_combo(HWND hCombo, const WCHAR **arr, int count, int def_idx)
{
    int i;
    if (!hCombo) return;
    SendMessageW(hCombo, CB_RESETCONTENT, 0, 0);
    for (i = 0; i < count; i++)
        SendMessageW(hCombo, CB_ADDSTRING, 0, (LPARAM)arr[i]);
    SendMessageW(hCombo, CB_SETCURSEL, def_idx, 0);
}

static void update_scheme_combos(HWND hwnd, int scheme)
{
    HWND hMonAC = GetDlgItem(hwnd, IDC_MONITOR_AC);
    HWND hMonDC = GetDlgItem(hwnd, IDC_MONITOR_DC);
    HWND hDskAC = GetDlgItem(hwnd, IDC_DISK_AC);
    HWND hDskDC = GetDlgItem(hwnd, IDC_DISK_DC);
    HWND hStbAC = GetDlgItem(hwnd, IDC_STANDBY_AC);
    HWND hStbDC = GetDlgItem(hwnd, IDC_STANDBY_DC);
    HWND hHibAC = GetDlgItem(hwnd, IDC_HIBERNATE_AC);
    HWND hHibDC = GetDlgItem(hwnd, IDC_HIBERNATE_DC);

    int mon_ac = 6, mon_dc = 3;  /* 20 min / 5 min */
    int dsk_ac = 9, dsk_dc = 4;  /* Never / 20 min */
    int stb_ac = 11, stb_dc = 6; /* Never / 20 min */

    switch (scheme)
    {
    case 0: /* Home/Office Desk */
        mon_ac = 6; mon_dc = 3; dsk_ac = 9; dsk_dc = 4; stb_ac = 11; stb_dc = 6;
        break;
    case 1: /* Portable/Laptop */
        mon_ac = 4; mon_dc = 2; dsk_ac = 5; dsk_dc = 2; stb_ac = 7; stb_dc = 4;
        break;
    case 2: /* Presentation */
        mon_ac = 12; mon_dc = 12; dsk_ac = 9; dsk_dc = 9; stb_ac = 11; stb_dc = 11;
        break;
    case 3: /* Always On */
        mon_ac = 6; mon_dc = 6; dsk_ac = 9; dsk_dc = 9; stb_ac = 11; stb_dc = 11;
        break;
    case 4: /* Minimal Power Management */
        mon_ac = 4; mon_dc = 2; dsk_ac = 4; dsk_dc = 2; stb_ac = 6; stb_dc = 3;
        break;
    case 5: /* Max Battery */
        mon_ac = 3; mon_dc = 1; dsk_ac = 3; dsk_dc = 1; stb_ac = 5; stb_dc = 2;
        break;
    }

    populate_timeout_combo(hMonAC, g_timeouts_monitor, ARRAYSIZE(g_timeouts_monitor), mon_ac);
    populate_timeout_combo(hMonDC, g_timeouts_monitor, ARRAYSIZE(g_timeouts_monitor), mon_dc);
    populate_timeout_combo(hDskAC, g_timeouts_disks, ARRAYSIZE(g_timeouts_disks), dsk_ac);
    populate_timeout_combo(hDskDC, g_timeouts_disks, ARRAYSIZE(g_timeouts_disks), dsk_dc);
    populate_timeout_combo(hStbAC, g_timeouts_standby, ARRAYSIZE(g_timeouts_standby), stb_ac);
    populate_timeout_combo(hStbDC, g_timeouts_standby, ARRAYSIZE(g_timeouts_standby), stb_dc);
    populate_timeout_combo(hHibAC, g_timeouts_standby, ARRAYSIZE(g_timeouts_standby), 11);
    populate_timeout_combo(hHibDC, g_timeouts_standby, ARRAYSIZE(g_timeouts_standby), 11);
}

static INT_PTR CALLBACK PowerSchemesPageProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam)
{
    switch (msg)
    {
    case WM_INITDIALOG:
    {
        HWND hSchemes = GetDlgItem(hwnd, IDC_SCHEME_LIST);
        int cur_scheme = 0;
        size_t i;
        HKEY hKey;

        /* Query scheme from registry */
        if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Control Panel\\PowerCfg", 0, KEY_READ, &hKey) == ERROR_SUCCESS)
        {
            WCHAR str[16];
            DWORD size = sizeof(str);
            if (RegQueryValueExW(hKey, L"CurrentPowerPolicy", NULL, NULL, (LPBYTE)str, &size) == ERROR_SUCCESS)
                cur_scheme = _wtoi(str);
            RegCloseKey(hKey);
        }

        if (cur_scheme < 0 || cur_scheme > 5) cur_scheme = 0;

        if (hSchemes)
        {
            for (i = 0; i < ARRAYSIZE(g_scheme_names); i++)
                SendMessageW(hSchemes, CB_ADDSTRING, 0, (LPARAM)g_scheme_names[i]);
            SendMessageW(hSchemes, CB_SETCURSEL, cur_scheme, 0);
        }

        update_scheme_combos(hwnd, cur_scheme);
        return TRUE;
    }

    case WM_COMMAND:
        if (LOWORD(wparam) == IDC_SCHEME_LIST && HIWORD(wparam) == CBN_SELCHANGE)
        {
            HWND hSchemes = GetDlgItem(hwnd, IDC_SCHEME_LIST);
            int idx = (int)SendMessageW(hSchemes, CB_GETCURSEL, 0, 0);
            if (idx >= 0 && idx <= 5)
            {
                update_scheme_combos(hwnd, idx);
                PropSheet_Changed(GetParent(hwnd), hwnd);
            }
            return TRUE;
        }
        break;

    case WM_NOTIFY:
    {
        LPNMHDR pnm = (LPNMHDR)lparam;
        if (pnm && (pnm->code == PSN_APPLY))
        {
            HWND hSchemes = GetDlgItem(hwnd, IDC_SCHEME_LIST);
            int idx = (int)SendMessageW(hSchemes, CB_GETCURSEL, 0, 0);
            if (idx >= 0 && idx <= 5)
            {
                HKEY hKey;
                if (RegCreateKeyExW(HKEY_CURRENT_USER, L"Control Panel\\PowerCfg", 0, NULL, 0,
                                   KEY_WRITE, NULL, &hKey, NULL) == ERROR_SUCCESS)
                {
                    WCHAR str[16];
                    wsprintfW(str, L"%u", idx);
                    RegSetValueExW(hKey, L"CurrentPowerPolicy", 0, REG_SZ, (const BYTE *)str, (lstrlenW(str) + 1) * sizeof(WCHAR));
                    RegCloseKey(hKey);
                }
            }
            return TRUE;
        }
        break;
    }
    }
    return FALSE;
}

/* Page 2: Power Meter */
static INT_PTR CALLBACK PowerMeterPageProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam)
{
    switch (msg)
    {
    case WM_INITDIALOG:
    {
        SYSTEM_POWER_STATUS sps;
        HWND hProgress = GetDlgItem(hwnd, IDC_METER_PROGRESS);
        int percent = 100;
        BOOL on_ac = TRUE;
        WCHAR pct_str[32];

        if (GetSystemPowerStatus(&sps))
        {
            on_ac = (sps.ACLineStatus == 1);
            if (sps.BatteryLifePercent <= 100)
                percent = sps.BatteryLifePercent;
        }

        if (hProgress)
        {
            SendMessageW(hProgress, PBM_SETRANGE, 0, MAKELPARAM(0, 100));
            SendMessageW(hProgress, PBM_SETPOS, percent, 0);
        }

        SetDlgItemTextW(hwnd, IDC_METER_SOURCE, on_ac ? L"AC power" : L"Batteries");

        wsprintfW(pct_str, L"%d%%", percent);
        SetDlgItemTextW(hwnd, IDC_METER_PERCENT, pct_str);

        if (on_ac)
            SetDlgItemTextW(hwnd, IDC_METER_DETAILS, L"Online (AC Connected)");
        else
            SetDlgItemTextW(hwnd, IDC_METER_DETAILS, L"Discharging");

        return TRUE;
    }
    }
    return FALSE;
}

/* Page 3: Advanced */
static INT_PTR CALLBACK AdvancedPageProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam)
{
    switch (msg)
    {
    case WM_INITDIALOG:
    {
        HWND hBtn = GetDlgItem(hwnd, IDC_ADV_PWRBTN);
        HWND hLid = GetDlgItem(hwnd, IDC_ADV_LID);

        CheckDlgButton(hwnd, IDC_ADV_SYSTRAY_ICON, BST_CHECKED);
        CheckDlgButton(hwnd, IDC_ADV_PASSWORD, BST_CHECKED);

        if (hBtn)
        {
            SendMessageW(hBtn, CB_ADDSTRING, 0, (LPARAM)L"Shut down");
            SendMessageW(hBtn, CB_ADDSTRING, 0, (LPARAM)L"Stand by");
            SendMessageW(hBtn, CB_ADDSTRING, 0, (LPARAM)L"Do nothing");
            SendMessageW(hBtn, CB_SETCURSEL, 0, 0);
        }

        if (hLid)
        {
            SendMessageW(hLid, CB_ADDSTRING, 0, (LPARAM)L"Stand by");
            SendMessageW(hLid, CB_ADDSTRING, 0, (LPARAM)L"Shut down");
            SendMessageW(hLid, CB_ADDSTRING, 0, (LPARAM)L"Do nothing");
            SendMessageW(hLid, CB_SETCURSEL, 0, 0);
        }
        return TRUE;
    }
    }
    return FALSE;
}

/* Page 4: Hibernate */
static INT_PTR CALLBACK HibernatePageProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam)
{
    switch (msg)
    {
    case WM_INITDIALOG:
    {
        MEMORYSTATUSEX mem = { sizeof(mem) };
        ULARGE_INTEGER free_bytes, total_bytes;
        WCHAR str[64];

        CheckDlgButton(hwnd, IDC_HIB_ENABLE, BST_CHECKED);

        if (GlobalMemoryStatusEx(&mem))
        {
            DWORD ram_mb = (DWORD)(mem.ullTotalPhys / (1024 * 1024));
            wsprintfW(str, L"%lu MB", ram_mb);
            SetDlgItemTextW(hwnd, IDC_HIB_REQUIRED_SPACE, str);
        }

        if (GetDiskFreeSpaceExW(NULL, &free_bytes, &total_bytes, NULL))
        {
            DWORD free_mb = (DWORD)(free_bytes.QuadPart / (1024 * 1024));
            wsprintfW(str, L"%lu MB", free_mb);
            SetDlgItemTextW(hwnd, IDC_HIB_FREE_SPACE, str);
        }
        return TRUE;
    }
    }
    return FALSE;
}

static void ShowPowerPropertySheet(HWND hwndParent)
{
    PROPSHEETPAGEW psp[4] = {0};
    PROPSHEETHEADERW psh = {0};

    psp[0].dwSize = sizeof(PROPSHEETPAGEW);
    psp[0].hInstance = g_hInstance;
    psp[0].pszTemplate = MAKEINTRESOURCEW(IDD_PAGE_POWERSCHEMES);
    psp[0].pfnDlgProc = PowerSchemesPageProc;

    psp[1].dwSize = sizeof(PROPSHEETPAGEW);
    psp[1].hInstance = g_hInstance;
    psp[1].pszTemplate = MAKEINTRESOURCEW(IDD_PAGE_POWERMETER);
    psp[1].pfnDlgProc = PowerMeterPageProc;

    psp[2].dwSize = sizeof(PROPSHEETPAGEW);
    psp[2].hInstance = g_hInstance;
    psp[2].pszTemplate = MAKEINTRESOURCEW(IDD_PAGE_ADVANCED);
    psp[2].pfnDlgProc = AdvancedPageProc;

    psp[3].dwSize = sizeof(PROPSHEETPAGEW);
    psp[3].hInstance = g_hInstance;
    psp[3].pszTemplate = MAKEINTRESOURCEW(IDD_PAGE_HIBERNATE);
    psp[3].pfnDlgProc = HibernatePageProc;

    psh.dwSize = sizeof(PROPSHEETHEADERW);
    psh.dwFlags = PSH_PROPSHEETPAGE | PSH_USEICONID;
    psh.hwndParent = hwndParent;
    psh.hInstance = g_hInstance;
    psh.pszIcon = MAKEINTRESOURCEW(IDI_CPL_POWER);
    psh.pszCaption = L"Power Options Properties";
    psh.nPages = 4;
    psh.ppsp = psp;

    PropertySheetW(&psh);
}

LONG WINAPI CPlApplet(HWND hwnd, UINT uMsg, LPARAM lParam1, LPARAM lParam2)
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
        if (info)
        {
            info->idIcon = IDI_CPL_POWER;
            info->idName = IDS_CPL_NAME;
            info->idInfo = IDS_CPL_INFO;
            info->lData = 0;
        }
        return 0;
    }

    case CPL_NEWINQUIRE:
    {
        NEWCPLINFOW *info = (NEWCPLINFOW *)lParam2;
        if (info)
        {
            info->dwSize = sizeof(NEWCPLINFOW);
            info->dwFlags = 0;
            info->dwHelpContext = 0;
            info->lData = 0;
            info->hIcon = LoadIconW(g_hInstance, MAKEINTRESOURCEW(IDI_CPL_POWER));
            LoadStringW(g_hInstance, IDS_CPL_NAME, info->szName, ARRAYSIZE(info->szName));
            LoadStringW(g_hInstance, IDS_CPL_INFO, info->szInfo, ARRAYSIZE(info->szInfo));
            info->szHelpFile[0] = '\0';
        }
        return 0;
    }

    case CPL_DBLCLK:
    case CPL_STARTWPARMSW:
        ShowPowerPropertySheet(hwnd);
        return 0;

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
        g_hInstance = hinstDLL;
        DisableThreadLibraryCalls(hinstDLL);
    }
    return TRUE;
}

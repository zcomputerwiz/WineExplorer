/*
 * WineExplorer Desktop Environment - Power & Battery Tray Applet Implementation
 *
 * Copyright 2026 WineExplorer Desktop Environment Project
 *
 * Implements the authentic Windows XP / Server 2003 system tray power and battery
 * monitor, featuring dynamic battery/AC plug icons, remaining time tooltips,
 * power scheme switching menu, and direct control panel integration.
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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <windows.h>
#include <shellapi.h>

#include "wine/debug.h"
#include "resource.h"
#include "power_backend.h"
#include "powertray.h"

#include "module.h"

WINE_DEFAULT_DEBUG_CHANNEL(explorer);

#define WM_POWERTRAY_NOTIFY  (WM_APP + 220)
#define IDT_POWERTRAY_TIMER  103
#define POWERTRAY_CLASS_NAME L"Power Tray Window"

static HWND g_hwnd_powertray = NULL;
static NOTIFYICONDATAW g_pwr_nid = {0};
static POWER_STATUS_INFO g_pwr_status = {0};
static int g_current_pwr_icon = 0;
static BOOL g_icon_added = FALSE;

static const WCHAR *g_scheme_names[] = {
    L"Home/Office Desk",
    L"Portable/Laptop",
    L"Presentation",
    L"Always On",
    L"Minimal Power Management",
    L"Max Battery"
};

static void update_pwr_tray_icon_and_tooltip(HWND hwnd)
{
    HINSTANCE hInst = explorer_hInstance;
    int new_icon_id;
    WCHAR new_tip[128] = {0};

    power_backend_get_status(&g_pwr_status);

    /* If machine is a desktop without battery and AC online, don't clutter tray unless forced */
    if (!g_pwr_status.has_battery)
    {
        if (g_icon_added)
        {
            Shell_NotifyIconW(NIM_DELETE, &g_pwr_nid);
            g_icon_added = FALSE;
        }
        return;
    }

    /* Select icon */
    if (g_pwr_status.ac_online)
    {
        new_icon_id = IDI_BATTERYPLUG;
        if (g_pwr_status.is_charging)
            wsprintfW(new_tip, L"%d%% remaining (charging)", g_pwr_status.battery_percent);
        else
            lstrcpyW(new_tip, L"On AC power");
    }
    else
    {
        new_icon_id = IDI_BATT;
        if (g_pwr_status.time_remaining_min > 0)
        {
            int hours = g_pwr_status.time_remaining_min / 60;
            int mins = g_pwr_status.time_remaining_min % 60;
            wsprintfW(new_tip, L"%d:%02d hours (%d%%) remaining", hours, mins, g_pwr_status.battery_percent);
        }
        else
        {
            wsprintfW(new_tip, L"%d%% remaining", g_pwr_status.battery_percent);
        }
    }

    if (!g_icon_added)
    {
        g_current_pwr_icon = new_icon_id;
        memset(&g_pwr_nid, 0, sizeof(g_pwr_nid));
        g_pwr_nid.cbSize = sizeof(NOTIFYICONDATAW);
        g_pwr_nid.hWnd = hwnd;
        g_pwr_nid.uID = 1003;
        g_pwr_nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
        g_pwr_nid.uCallbackMessage = WM_POWERTRAY_NOTIFY;
        g_pwr_nid.hIcon = LoadIconW(hInst, MAKEINTRESOURCEW(new_icon_id));
        lstrcpynW(g_pwr_nid.szTip, new_tip, 128);

        Shell_NotifyIconW(NIM_ADD, &g_pwr_nid);
        g_icon_added = TRUE;
    }
    else if (new_icon_id != g_current_pwr_icon || wcscmp(new_tip, g_pwr_nid.szTip) != 0)
    {
        g_current_pwr_icon = new_icon_id;
        if (g_pwr_nid.hIcon) DestroyIcon(g_pwr_nid.hIcon);
        g_pwr_nid.hIcon = LoadIconW(hInst, MAKEINTRESOURCEW(new_icon_id));
        lstrcpynW(g_pwr_nid.szTip, new_tip, 128);

        Shell_NotifyIconW(NIM_MODIFY, &g_pwr_nid);
    }
}

static void show_power_context_menu(HWND hwnd, POINT pt)
{
    HMENU hMenu = CreatePopupMenu();
    UINT cmd, i;
    if (!hMenu) return;

    AppendMenuW(hMenu, MF_STRING, IDM_PWR_PROPERTIES, L"&Adjust Power Properties");
    AppendMenuW(hMenu, MF_STRING, IDM_PWR_OPEN_METER, L"&Open Power Meter");
    AppendMenuW(hMenu, MF_SEPARATOR, 0, NULL);

    /* Populate power schemes radio list */
    for (i = 0; i <= POWER_SCHEME_MAXBATTERY; i++)
    {
        UINT flags = MF_STRING;
        if (i == g_pwr_status.active_scheme)
            flags |= MF_CHECKED;

        AppendMenuW(hMenu, flags, IDM_PWR_SCHEME_BASE + i, g_scheme_names[i]);
    }

    SetMenuDefaultItem(hMenu, IDM_PWR_PROPERTIES, FALSE);

    SetForegroundWindow(hwnd);
    cmd = TrackPopupMenu(hMenu, TPM_RIGHTBUTTON | TPM_RETURNCMD | TPM_NONOTIFY,
                         pt.x, pt.y, 0, hwnd, NULL);
    DestroyMenu(hMenu);

    if (cmd == IDM_PWR_PROPERTIES || cmd == IDM_PWR_OPEN_METER)
    {
        ShellExecuteW(NULL, L"open", L"control.exe", L"powercfg.cpl", NULL, SW_SHOWNORMAL);
    }
    else if (cmd >= IDM_PWR_SCHEME_BASE && cmd <= IDM_PWR_SCHEME_BASE + POWER_SCHEME_MAXBATTERY)
    {
        UINT scheme = cmd - IDM_PWR_SCHEME_BASE;
        power_backend_set_scheme(scheme);
        update_pwr_tray_icon_and_tooltip(hwnd);
    }
}

static LRESULT CALLBACK PowerTrayWndProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam)
{
    switch (msg)
    {
    case WM_CREATE:
        power_backend_init();
        update_pwr_tray_icon_and_tooltip(hwnd);
        SetTimer(hwnd, IDT_POWERTRAY_TIMER, 1500, NULL);
        return 0;

    case WM_TIMER:
        if (wparam == IDT_POWERTRAY_TIMER)
        {
            update_pwr_tray_icon_and_tooltip(hwnd);
        }
        return 0;

    case WM_POWERTRAY_NOTIFY:
    {
        POINT pt;
        GetCursorPos(&pt);

        switch (LOWORD(lparam))
        {
        case WM_LBUTTONDBLCLK:
            ShellExecuteW(NULL, L"open", L"control.exe", L"powercfg.cpl", NULL, SW_SHOWNORMAL);
            break;

        case WM_RBUTTONUP:
            show_power_context_menu(hwnd, pt);
            break;
        }
        return 0;
    }

    case WM_DESTROY:
        KillTimer(hwnd, IDT_POWERTRAY_TIMER);
        if (g_icon_added)
        {
            Shell_NotifyIconW(NIM_DELETE, &g_pwr_nid);
            if (g_pwr_nid.hIcon)
            {
                DestroyIcon(g_pwr_nid.hIcon);
                g_pwr_nid.hIcon = NULL;
            }
            g_icon_added = FALSE;
        }
        g_hwnd_powertray = NULL;
        return 0;
    }

    return DefWindowProcW(hwnd, msg, wparam, lparam);
}

BOOL powertray_init(HWND hwnd_tray)
{
    WNDCLASSEXW wc;
    HINSTANCE hInst;

    if (g_hwnd_powertray) return TRUE;

    memset(&wc, 0, sizeof(wc));
    hInst = explorer_hInstance;

    wc.cbSize = sizeof(WNDCLASSEXW);
    wc.lpfnWndProc = PowerTrayWndProc;
    wc.hInstance = hInst;
    wc.lpszClassName = POWERTRAY_CLASS_NAME;

    RegisterClassExW(&wc);

    g_hwnd_powertray = CreateWindowExW(0, POWERTRAY_CLASS_NAME, L"Power Tray Window",
                                       WS_OVERLAPPED, 0, 0, 0, 0,
                                       NULL, NULL, hInst, NULL);

    TRACE("PowerTray initialized with hwnd %p\n", g_hwnd_powertray);
    return (g_hwnd_powertray != NULL);
}

void powertray_shutdown(void)
{
    if (g_hwnd_powertray)
    {
        DestroyWindow(g_hwnd_powertray);
        g_hwnd_powertray = NULL;
    }
}

void powertray_update(void)
{
    if (g_hwnd_powertray)
    {
        update_pwr_tray_icon_and_tooltip(g_hwnd_powertray);
    }
}

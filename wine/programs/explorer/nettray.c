/*
 * WineExplorer Desktop Environment - Network System Tray Notification Widget Implementation
 *
 * Copyright 2026 WineExplorer Desktop Environment Project
 *
 * Implements the authentic Windows XP / Server 2003 system tray network monitor ("Connections Tray"),
 * providing animated signal strength / twin monitor icons, rich connection tooltips,
 * left-click quick connect, double-click status dialog, and authentic right-click context menus.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <windows.h>
#include <shellapi.h>

#include "wine/debug.h"
#include "resource.h"
#include "net_backend.h"
#include "wlan_dialog.h"
#include "wlan_status.h"
#include "nettray.h"

WINE_DEFAULT_DEBUG_CHANNEL(explorer);

#define WM_NETTRAY_NOTIFY  (WM_APP + 200)
#define IDT_NETTRAY_TIMER  101
#define NETTRAY_CLASS_NAME L"Connections Tray"

#define IDM_NET_DISABLE          2001
#define IDM_NET_STATUS           2002
#define IDM_NET_REPAIR           2003
#define IDM_NET_VIEW_WIRELESS    2004
#define IDM_NET_OPEN_CONNECTIONS 2005

static HWND g_hwnd_nettray = NULL;
static NOTIFYICONDATAW g_nid = {0};
static NET_STATUS_INFO g_status;
static int g_current_icon_id = 0;

static const WCHAR *get_signal_strength_str(int bars)
{
    switch (bars)
    {
    case 5: return L"Excellent";
    case 4: return L"Very Good";
    case 3: return L"Good";
    case 2: return L"Low";
    case 1: return L"Very Low";
    default: return L"No Signal";
    }
}

static void update_tray_icon_and_tooltip(HWND hwnd)
{
    HINSTANCE hInst = GetModuleHandleW(NULL);
    int new_icon_id;
    WCHAR new_tip[128] = {0};

    net_backend_get_status(&g_status);

    if (!g_status.is_connected)
    {
        new_icon_id = IDI_NET_DISCONNECT;
        lstrcpyW(new_tip, L"Wireless Network Connection\nOne or more wireless networks are available.\nClick here to view.");
    }
    else if (g_status.conn_type == NET_TYPE_WIRELESS)
    {
        int bars = max(0, min(5, g_status.signal_bars));
        new_icon_id = IDI_NET_SIG0 + bars;

        wsprintfW(new_tip, L"Wireless Network Connection (%s)\nSpeed: %u.0 Mbps\nSignal Strength: %s\nStatus: Connected",
                  g_status.active_ssid[0] ? g_status.active_ssid : L"Connected",
                  g_status.speed_mbps,
                  get_signal_strength_str(bars));
    }
    else
    {
        new_icon_id = IDI_NET_XMITRECV;
        wsprintfW(new_tip, L"Local Area Connection\nSpeed: %u Mbps\nStatus: Connected",
                  g_status.speed_mbps);
    }

    if (new_icon_id != g_current_icon_id || wcscmp(new_tip, g_nid.szTip) != 0)
    {
        g_current_icon_id = new_icon_id;
        if (g_nid.hIcon) DestroyIcon(g_nid.hIcon);
        g_nid.hIcon = LoadIconW(hInst, MAKEINTRESOURCEW(new_icon_id));
        lstrcpynW(g_nid.szTip, new_tip, 128);

        Shell_NotifyIconW(NIM_MODIFY, &g_nid);
    }
}

static LRESULT CALLBACK NetTrayWndProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam)
{
    switch (msg)
    {
    case WM_CREATE:
    {
        HINSTANCE hInst = GetModuleHandleW(NULL);
        net_backend_init();
        net_backend_get_status(&g_status);

        g_current_icon_id = g_status.is_connected ? (IDI_NET_SIG0 + g_status.signal_bars) : IDI_NET_DISCONNECT;

        memset(&g_nid, 0, sizeof(g_nid));
        g_nid.cbSize = sizeof(g_nid);
        g_nid.hWnd = hwnd;
        g_nid.uID = 1;
        g_nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
        g_nid.uCallbackMessage = WM_NETTRAY_NOTIFY;
        g_nid.hIcon = LoadIconW(hInst, MAKEINTRESOURCEW(g_current_icon_id));
        lstrcpyW(g_nid.szTip, L"Wireless Network Connection\nStatus: Initializing...");

        Shell_NotifyIconW(NIM_ADD, &g_nid);
        update_tray_icon_and_tooltip(hwnd);

        SetTimer(hwnd, IDT_NETTRAY_TIMER, 3000, NULL);
        return 0;
    }

    case WM_TIMER:
        if (wparam == IDT_NETTRAY_TIMER)
        {
            update_tray_icon_and_tooltip(hwnd);
        }
        return 0;

    case WM_NETTRAY_NOTIFY:
    {
        switch (LOWORD(lparam))
        {
        case WM_LBUTTONUP:
        case NIN_BALLOONUSERCLICK:
            /* Single click: open Wireless Quick Connect */
            wlan_dialog_show(hwnd);
            update_tray_icon_and_tooltip(hwnd);
            break;

        case WM_LBUTTONDBLCLK:
            /* Double click: open Connection Status */
            wlan_status_show(hwnd);
            update_tray_icon_and_tooltip(hwnd);
            break;

        case WM_RBUTTONUP:
        case WM_CONTEXTMENU:
        {
            HMENU hMenu = CreatePopupMenu();
            POINT pt;

            AppendMenuW(hMenu, MF_STRING, IDM_NET_DISABLE, L"&Disable");
            AppendMenuW(hMenu, MF_STRING, IDM_NET_STATUS, L"&Status");
            AppendMenuW(hMenu, MF_STRING, IDM_NET_REPAIR, L"Re&pair");
            AppendMenuW(hMenu, MF_SEPARATOR, 0, NULL);
            AppendMenuW(hMenu, MF_STRING, IDM_NET_VIEW_WIRELESS, L"&View Available Wireless Networks");
            AppendMenuW(hMenu, MF_SEPARATOR, 0, NULL);
            AppendMenuW(hMenu, MF_STRING, IDM_NET_OPEN_CONNECTIONS, L"&Open Network Connections");

            /* Default double-click action is Status */
            SetMenuDefaultItem(hMenu, IDM_NET_STATUS, FALSE);

            GetCursorPos(&pt);
            SetForegroundWindow(hwnd);
            TrackPopupMenu(hMenu, TPM_RIGHTBUTTON, pt.x, pt.y, 0, hwnd, NULL);
            DestroyMenu(hMenu);
            break;
        }
        }
        return 0;
    }

    case WM_COMMAND:
        switch (LOWORD(wparam))
        {
        case IDM_NET_DISABLE:
            net_backend_set_enabled(FALSE);
            update_tray_icon_and_tooltip(hwnd);
            break;

        case IDM_NET_STATUS:
            wlan_status_show(hwnd);
            update_tray_icon_and_tooltip(hwnd);
            break;

        case IDM_NET_REPAIR:
            SetCursor(LoadCursorW(NULL, (const WCHAR *)IDC_WAIT));
            net_backend_repair();
            SetCursor(LoadCursorW(NULL, (const WCHAR *)IDC_ARROW));
            update_tray_icon_and_tooltip(hwnd);
            MessageBoxW(hwnd, L"Windows has finished repairing the connection.",
                        L"Repair", MB_OK | MB_ICONINFORMATION);
            break;

        case IDM_NET_VIEW_WIRELESS:
        case IDM_NET_OPEN_CONNECTIONS:
            wlan_dialog_show(hwnd);
            update_tray_icon_and_tooltip(hwnd);
            break;
        }
        return 0;

    case WM_DESTROY:
        KillTimer(hwnd, IDT_NETTRAY_TIMER);
        Shell_NotifyIconW(NIM_DELETE, &g_nid);
        if (g_nid.hIcon)
        {
            DestroyIcon(g_nid.hIcon);
            g_nid.hIcon = NULL;
        }
        return 0;
    }

    return DefWindowProcW(hwnd, msg, wparam, lparam);
}

BOOL nettray_init(HWND hwnd_tray)
{
    WNDCLASSEXW wc = {0};
    HINSTANCE hInst = GetModuleHandleW(NULL);

    if (g_hwnd_nettray) return TRUE;

    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = NetTrayWndProc;
    wc.hInstance = hInst;
    wc.lpszClassName = NETTRAY_CLASS_NAME;

    RegisterClassExW(&wc);

    g_hwnd_nettray = CreateWindowExW(0, NETTRAY_CLASS_NAME, L"Network Tray Window",
                                     WS_POPUP, 0, 0, 0, 0,
                                     NULL, NULL, hInst, NULL);

    TRACE("NetTray initialized with hwnd %p\n", g_hwnd_nettray);
    return (g_hwnd_nettray != NULL);
}

void nettray_shutdown(void)
{
    if (g_hwnd_nettray)
    {
        DestroyWindow(g_hwnd_nettray);
        g_hwnd_nettray = NULL;
        UnregisterClassW(NETTRAY_CLASS_NAME, GetModuleHandleW(NULL));
    }
}

void nettray_update(void)
{
    if (g_hwnd_nettray)
    {
        update_tray_icon_and_tooltip(g_hwnd_nettray);
    }
}

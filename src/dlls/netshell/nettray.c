/*
 * WineExplorer Desktop Environment - Network System Tray Notification Widget Implementation
 *
 * Copyright 2026 WineExplorer Desktop Environment Project
 *
 * Implements the authentic Windows XP / Server 2003 system tray network monitor ("Connections Tray"),
 * providing animated signal strength for Wi-Fi, packet-level transmit/receive activity blinking for Ethernet,
 * rich connection tooltips, toggleable tray visibility matching Network Connection Properties,
 * double-click status dialog, and authentic right-click context menus.
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
#include "wexnet.h"
#include "wexwlan.h"
#include "wlan_status.h"
#include "net_props.h"
#include "nettray.h"

#include "module.h"
#include "wexres.h"

WINE_DEFAULT_DEBUG_CHANNEL(explorer);

#define WM_NETTRAY_NOTIFY  (WM_APP + 200)
#define IDT_NETTRAY_TIMER  101
#define NETTRAY_CLASS_NAME L"Connections Tray"

#define IDM_NET_DISABLE          2001
#define IDM_NET_STATUS           2002
#define IDM_NET_REPAIR           2003
#define IDM_NET_VIEW_WIRELESS    2004
#define IDM_NET_OPEN_CONNECTIONS 2005
#define IDM_NET_PROPERTIES       2006

static HWND g_hwnd_nettray = NULL;
static NOTIFYICONDATAW g_nid = {0};
static NET_STATUS_INFO g_status;
static int g_current_icon_id = 0;
static BOOL g_tray_visible = FALSE;
static ULONGLONG g_last_packets_sent = 0;
static ULONGLONG g_last_packets_recv = 0;
static BOOL g_activity_baseline;
static WCHAR g_activity_device[128];

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

static int ethernet_activity_icon(const NET_STATUS_INFO *status)
{
    BOOL valid = g_activity_baseline && !wcscmp(g_activity_device, status->active_device) &&
                 status->packets_sent >= g_last_packets_sent &&
                 status->packets_received >= g_last_packets_recv;
    BOOL xmit = valid && status->packets_sent > g_last_packets_sent;
    BOOL recv = valid && status->packets_received > g_last_packets_recv;
    g_activity_baseline = TRUE;
    lstrcpynW(g_activity_device, status->active_device, ARRAY_SIZE(g_activity_device));
    g_last_packets_sent = status->packets_sent;
    g_last_packets_recv = status->packets_received;

    if (xmit && recv)
        return IDI_NET_XMITRECV;
    else if (xmit)
        return IDI_NET_XMIT;
    else if (recv)
        return IDI_NET_RECV;
    else
        return IDI_NET_BLANK;
}

static void update_tray_icon_and_tooltip(HWND hwnd)
{
    HINSTANCE hInst = netshell_module();
    int new_icon_id = 0;
    WCHAR new_tip[128] = {0};
    BOOL should_show = net_backend_get_show_tray_icon();

    net_backend_get_status(&g_status);
    if (!g_status.has_adapter) should_show = FALSE;   /* nothing to monitor */

    if (!should_show || !g_status.is_connected || g_status.conn_type == NET_TYPE_WIRELESS)
        g_activity_baseline = FALSE;

    if (!should_show)
    {
        if (g_tray_visible)
        {
            Shell_NotifyIconW(NIM_DELETE, &g_nid);
            g_tray_visible = FALSE;
            if (g_nid.hIcon)
            {
                DestroyIcon(g_nid.hIcon);
                g_nid.hIcon = NULL;
            }
            g_current_icon_id = 0;
        }
        return;
    }

    if (!g_status.is_connected)
    {
        new_icon_id = IDI_NET_DISCONNECT;
        if (g_status.conn_type == NET_TYPE_WIRELESS)
        {
            lstrcpyW(new_tip, L"Wireless Network Connection\nOne or more wireless networks are available.\nClick here to view.");
        }
        else
        {
            lstrcpyW(new_tip, L"Local Area Connection\nA network cable is unplugged.");
        }
    }
    else if (g_status.conn_type == NET_TYPE_WIRELESS)
    {
        int bars = max(0, min(5, g_status.signal_bars));
        new_icon_id = IDI_NET_SIG0 + bars;

        wsprintfW(new_tip, L"Wireless Network Connection (%s)\nSignal Strength: %s\nStatus: Connected",
                  g_status.active_ssid[0] ? g_status.active_ssid : L"Connected",
                  get_signal_strength_str(bars));
    }
    else
    {
        /* Ethernet: authentic Windows XP packet activity blinking */
        new_icon_id = ethernet_activity_icon(&g_status);

        if (g_status.speed_mbps)
            wsprintfW(new_tip, L"Local Area Connection\nSpeed: %u Mbps\nStatus: Connected", g_status.speed_mbps);
        else
            lstrcpyW(new_tip, L"Local Area Connection\nStatus: Connected");
    }

    if (!g_tray_visible)
    {
        g_current_icon_id = new_icon_id;
        if (g_nid.hIcon) DestroyIcon(g_nid.hIcon);
        g_nid.hIcon = CopyIcon(LoadIconW(WexResModule(), MAKEINTRESOURCEW(new_icon_id)));
        lstrcpynW(g_nid.szTip, new_tip, 128);
        Shell_NotifyIconW(NIM_ADD, &g_nid);
        g_tray_visible = TRUE;
    }
    else if (new_icon_id != g_current_icon_id || wcscmp(new_tip, g_nid.szTip) != 0)
    {
        g_current_icon_id = new_icon_id;
        if (g_nid.hIcon) DestroyIcon(g_nid.hIcon);
        g_nid.hIcon = CopyIcon(LoadIconW(WexResModule(), MAKEINTRESOURCEW(new_icon_id)));
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
        net_backend_init();
        net_backend_get_status(&g_status);

        memset(&g_nid, 0, sizeof(g_nid));
        g_nid.cbSize = sizeof(g_nid);
        g_nid.hWnd = hwnd;
        g_nid.uID = 1;
        g_nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
        g_nid.uCallbackMessage = WM_NETTRAY_NOTIFY;

        g_last_packets_sent = g_status.packets_sent;
        g_last_packets_recv = g_status.packets_received;

        update_tray_icon_and_tooltip(hwnd);

        /* 500ms polling timer for responsive packet activity indicator */
        SetTimer(hwnd, IDT_NETTRAY_TIMER, 500, NULL);
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
            /* Single click: wireless opens available networks list */
            if (g_status.conn_type == NET_TYPE_WIRELESS)
            {
                wlan_dialog_show(hwnd);
                update_tray_icon_and_tooltip(hwnd);
            }
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
            if (g_status.conn_type == NET_TYPE_WIRELESS)
            {
                AppendMenuW(hMenu, MF_STRING, IDM_NET_VIEW_WIRELESS, L"&View Available Wireless Networks");
                AppendMenuW(hMenu, MF_SEPARATOR, 0, NULL);
            }
            AppendMenuW(hMenu, MF_STRING, IDM_NET_OPEN_CONNECTIONS, L"&Open Network Connections");
            AppendMenuW(hMenu, MF_SEPARATOR, 0, NULL);
            AppendMenuW(hMenu, MF_STRING, IDM_NET_PROPERTIES, L"P&roperties");

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
            wlan_dialog_show(hwnd);
            update_tray_icon_and_tooltip(hwnd);
            break;

        case IDM_NET_OPEN_CONNECTIONS:
            ShellExecuteW(NULL, L"open", L"control.exe", L"ncpa.cpl", NULL, SW_SHOWNORMAL);
            break;

        case IDM_NET_PROPERTIES:
            net_props_show(hwnd);
            update_tray_icon_and_tooltip(hwnd);
            break;
        }
        return 0;

    case WM_DESTROY:
        KillTimer(hwnd, IDT_NETTRAY_TIMER);
        if (g_tray_visible)
        {
            Shell_NotifyIconW(NIM_DELETE, &g_nid);
            g_tray_visible = FALSE;
        }
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
    HINSTANCE hInst = netshell_module();

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
        UnregisterClassW(NETTRAY_CLASS_NAME, netshell_module());
    }
}

void nettray_update(void)
{
    if (g_hwnd_nettray)
    {
        update_tray_icon_and_tooltip(g_hwnd_nettray);
    }
}

void nettray_update_visibility(void)
{
    if (g_hwnd_nettray)
    {
        update_tray_icon_and_tooltip(g_hwnd_nettray);
    }
}

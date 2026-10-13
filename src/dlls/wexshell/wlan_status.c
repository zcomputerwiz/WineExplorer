/*
 * WineExplorer Desktop Environment - Wireless Connection Status Dialog Implementation
 *
 * Copyright 2026 WineExplorer Desktop Environment Project
 *
 * Implements the authentic Windows XP / Server 2003 "Wireless Network Connection Status"
 * dialog (IDD_STATMON_GENERAL_LAN) with General/Support tabs, duration timer,
 * 5-bar signal strength meter, and live packet/byte throughput meters.
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
#include <commctrl.h>
#include <iphlpapi.h>

#include "wine/debug.h"
#include "resource.h"
#include "net_backend.h"
#include "net_props.h"
#include "wlan_status.h"

#include "module.h"
#include "wexres.h"

WINE_DEFAULT_DEBUG_CHANNEL(explorer);

#define IDT_STATUS_TIMER 1001

static NET_STATUS_INFO g_status_info;
static int g_current_tab = 0;
static ULONGLONG g_last_status_sent = 0;
static ULONGLONG g_last_status_recv = 0;

/* Format number with thousands separators e.g. 1,234,567 */
static void format_number_commas(ULONGLONG num, WCHAR *out, int max_len)
{
    WCHAR raw[32];
    int len, i, j = 0;

    wsprintfW(raw, L"%I64u", num);
    len = lstrlenW(raw);

    for (i = 0; i < len && j < max_len - 1; i++)
    {
        out[j++] = raw[i];
        if ((len - 1 - i) > 0 && (len - 1 - i) % 3 == 0)
        {
            out[j++] = L',';
        }
    }
    out[j] = L'\0';
}

/* Paint the authentic Windows 5-bar green signal meter */
static void paint_signal_bars(HWND hwnd, HDC hdc, int bars)
{
    RECT rc;
    int i;
    int bar_width = 8;
    int bar_gap = 4;
    int start_x = 2;

    GetClientRect(hwnd, &rc);
    FillRect(hdc, &rc, (HBRUSH)(COLOR_3DFACE + 1));

    for (i = 1; i <= 5; i++)
    {
        int h = 3 + (i * 2); /* 5, 7, 9, 11, 13 */
        int x = start_x + (i - 1) * (bar_width + bar_gap);
        int y = rc.bottom - 2 - h;
        RECT bar_rc = {x, y, x + bar_width, rc.bottom - 2};

        if (i <= bars)
        {
            /* Filled active signal bar */
            HBRUSH hGreen = CreateSolidBrush(RGB(0, 180, 0));
            HBRUSH hDarkGreen = CreateSolidBrush(RGB(0, 100, 0));
            FrameRect(hdc, &bar_rc, hDarkGreen);
            InflateRect(&bar_rc, -1, -1);
            FillRect(hdc, &bar_rc, hGreen);
            DeleteObject(hGreen);
            DeleteObject(hDarkGreen);
        }
        else
        {
            /* Inactive hollow/gray bar */
            HBRUSH hGray = CreateSolidBrush(RGB(180, 180, 180));
            FrameRect(hdc, &bar_rc, hGray);
            DeleteObject(hGray);
        }
    }
}

static void update_tab_visibility(HWND hwnd, int tab)
{
    BOOL is_general = (tab == 0);

    /* General Tab controls */
    int general_ids[] = {
        IDC_GRP_SM_CONN, IDC_LBL_SM_STATUS, IDC_TXT_SM_STATUS,
        IDC_LBL_SM_SSID, IDC_TXT_SM_SSID,
        IDC_LBL_SM_DUR, IDC_TXT_SM_DURATION,
        IDC_LBL_SM_SPEED, IDC_TXT_SM_SPEED,
        IDC_LBL_SM_SIGNAL, IDC_CTL_SM_SIGNAL,
        IDC_GRP_SM_ACT, IDC_LBL_SM_SENT, IDC_LBL_SM_DASH1, IDC_ICO_SM_ACTIVITY, IDC_LBL_SM_DASH2, IDC_LBL_SM_RCVD,
        IDC_LBL_SM_BYTES, IDC_TXT_SM_BYTES_TRANS, IDC_TXT_SM_BYTES_RCVD,
        IDC_LBL_SM_PKTS, IDC_TXT_SM_PKTS_TRANS, IDC_LBL_SM_DIVIDER, IDC_TXT_SM_PKTS_RCVD,
        IDC_BTN_SM_PROPERTIES, IDC_BTN_SM_DISABLE, 0
    };

    /* Support Tab controls */
    int support_ids[] = {
        IDC_GRP_SM_SUPPORT, IDC_ICO_SM_SUPPORT,
        IDC_LBL_SM_TYPE, IDC_TXT_SM_DHCP,
        IDC_LBL_SM_IP, IDC_TXT_SM_IP,
        IDC_LBL_SM_SUBNET, IDC_TXT_SM_SUBNET,
        IDC_LBL_SM_GW, IDC_TXT_SM_GATEWAY,
        IDC_BTN_SM_DETAILS, IDC_TXT_SM_SUPPORT_NOTE, IDC_BTN_SM_REPAIR, 0
    };

    int i = 0;
    while (general_ids[i])
    {
        HWND hCtrl = GetDlgItem(hwnd, general_ids[i]);
        if (hCtrl) ShowWindow(hCtrl, is_general ? SW_SHOW : SW_HIDE);
        i++;
    }

    i = 0;
    while (support_ids[i])
    {
        HWND hCtrl = GetDlgItem(hwnd, support_ids[i]);
        if (hCtrl) ShowWindow(hCtrl, is_general ? SW_HIDE : SW_SHOW);
        i++;
    }

    InvalidateRect(hwnd, NULL, TRUE);
}

static void update_status_controls(HWND hwnd)
{
    WCHAR dur_str[32];
    WCHAR bytes_sent_str[32];
    WCHAR bytes_recv_str[32];
    WCHAR pkts_sent_str[32];
    WCHAR pkts_recv_str[32];
    WCHAR speed_str[32];
    DWORD h, m, s;
    HINSTANCE hInst = explorer_hInstance;
    HICON hActIcon;
    BOOL xmit, recv;

    net_backend_get_status(&g_status_info);

    /* Status and SSID */
    SetDlgItemTextW(hwnd, IDC_TXT_SM_STATUS, g_status_info.is_connected ? L"Connected" : L"Disconnected");
    SetDlgItemTextW(hwnd, IDC_TXT_SM_SSID, g_status_info.active_ssid[0] ? g_status_info.active_ssid : L"Connected");

    /* Duration */
    h = g_status_info.duration_seconds / 3600;
    m = (g_status_info.duration_seconds % 3600) / 60;
    s = g_status_info.duration_seconds % 60;
    wsprintfW(dur_str, L"%02u:%02u:%02u", h, m, s);
    SetDlgItemTextW(hwnd, IDC_TXT_SM_DURATION, dur_str);

    /* Speed */
    if (g_status_info.speed_mbps) wsprintfW(speed_str, L"%u Mbps", g_status_info.speed_mbps);
    else speed_str[0] = 0;
    SetDlgItemTextW(hwnd, IDC_TXT_SM_SPEED, speed_str);

    /* Bytes and Packets */
    format_number_commas(g_status_info.bytes_sent, bytes_sent_str, 32);
    format_number_commas(g_status_info.bytes_received, bytes_recv_str, 32);
    format_number_commas(g_status_info.packets_sent, pkts_sent_str, 32);
    format_number_commas(g_status_info.packets_received, pkts_recv_str, 32);

    SetDlgItemTextW(hwnd, IDC_TXT_SM_BYTES_TRANS, bytes_sent_str);
    SetDlgItemTextW(hwnd, IDC_TXT_SM_BYTES_RCVD, bytes_recv_str);
    SetDlgItemTextW(hwnd, IDC_TXT_SM_PKTS_TRANS, pkts_sent_str);
    SetDlgItemTextW(hwnd, IDC_TXT_SM_PKTS_RCVD, pkts_recv_str);

    /* Activity Icon Blinking based on actual packet transmission */
    xmit = (g_last_status_sent > 0 && g_status_info.packets_sent > g_last_status_sent);
    recv = (g_last_status_recv > 0 && g_status_info.packets_received > g_last_status_recv);
    g_last_status_sent = g_status_info.packets_sent;
    g_last_status_recv = g_status_info.packets_received;

    if (xmit && recv)
        hActIcon = LoadIconW(WexResModule(), MAKEINTRESOURCEW(IDI_NET_XMITRECV));
    else if (xmit)
        hActIcon = LoadIconW(WexResModule(), MAKEINTRESOURCEW(IDI_NET_XMIT));
    else if (recv)
        hActIcon = LoadIconW(WexResModule(), MAKEINTRESOURCEW(IDI_NET_RECV));
    else
        hActIcon = LoadIconW(WexResModule(), MAKEINTRESOURCEW(IDI_NET_BLANK));

    SendMessageW(GetDlgItem(hwnd, IDC_ICO_SM_ACTIVITY), STM_SETICON, (WPARAM)hActIcon, 0);

    /* Support Tab Data */
    SetDlgItemTextW(hwnd, IDC_TXT_SM_IP, g_status_info.ip_address);
    SetDlgItemTextW(hwnd, IDC_TXT_SM_SUBNET, g_status_info.subnet_mask);
    SetDlgItemTextW(hwnd, IDC_TXT_SM_GATEWAY, g_status_info.default_gateway);

    /* Redraw Signal Bars */
    if (g_status_info.conn_type == NET_TYPE_WIRELESS)
        InvalidateRect(GetDlgItem(hwnd, IDC_CTL_SM_SIGNAL), NULL, TRUE);
}

static INT_PTR CALLBACK WlanStatusDlgProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam)
{
    switch (msg)
    {
    case WM_INITDIALOG:
    {
        HINSTANCE hInst = explorer_hInstance;
        HWND hTab = GetDlgItem(hwnd, IDC_STATMON_TAB);
        TCITEMW ti = {0};
        HICON hIconMain;

        net_backend_get_status(&g_status_info);
        g_last_status_sent = g_status_info.packets_sent;
        g_last_status_recv = g_status_info.packets_received;

        if (g_status_info.conn_type == NET_TYPE_ETHERNET)
        {
            hIconMain = LoadIconW(WexResModule(), MAKEINTRESOURCEW(IDI_NET_LAN));
            SetWindowTextW(hwnd, L"Local Area Connection Status");
        }
        else
        {
            hIconMain = LoadIconW(WexResModule(), MAKEINTRESOURCEW(IDI_NET_WIRELESS));
            SetWindowTextW(hwnd, L"Wireless Network Connection Status");
        }
        SendMessageW(hwnd, WM_SETICON, ICON_BIG, (LPARAM)hIconMain);
        SendMessageW(hwnd, WM_SETICON, ICON_SMALL, (LPARAM)hIconMain);
        SendDlgItemMessageW(hwnd, IDC_ICO_SM_SUPPORT, STM_SETICON, (WPARAM)hIconMain, 0);

        /* Initialize Tabs */
        ti.mask = TCIF_TEXT;
        ti.pszText = (LPWSTR)L"General";
        TabCtrl_InsertItemW(hTab, 0, &ti);

        ti.pszText = (LPWSTR)L"Support";
        TabCtrl_InsertItemW(hTab, 1, &ti);

        g_current_tab = 0;
        update_tab_visibility(hwnd, 0);
        update_status_controls(hwnd);

        /* 1-second update timer */
        SetTimer(hwnd, IDT_STATUS_TIMER, 1000, NULL);
        return TRUE;
    }

    case WM_TIMER:
        if (wparam == IDT_STATUS_TIMER)
        {
            update_status_controls(hwnd);
        }
        return TRUE;

    case WM_NOTIFY:
    {
        NMHDR *hdr = (NMHDR*)lparam;
        if (hdr->idFrom == IDC_STATMON_TAB && hdr->code == TCN_SELCHANGE)
        {
            g_current_tab = ((int)SendMessageW(hdr->hwndFrom, TCM_GETCURSEL, 0, 0));
            update_tab_visibility(hwnd, g_current_tab);
        }
        break;
    }

    case WM_DRAWITEM:
    {
        DRAWITEMSTRUCT *dis = (DRAWITEMSTRUCT*)lparam;
        if (dis->CtlID == IDC_CTL_SM_SIGNAL)
        {
            paint_signal_bars(dis->hwndItem, dis->hDC, g_status_info.signal_bars);
            return TRUE;
        }
        break;
    }

    case WM_COMMAND:
    {
        WORD cmd = LOWORD(wparam);
        switch (cmd)
        {
        case IDC_BTN_SM_DISABLE:
            net_backend_set_enabled(FALSE);
            KillTimer(hwnd, IDT_STATUS_TIMER);
            EndDialog(hwnd, IDOK);
            break;

        case IDC_BTN_SM_REPAIR:
            SetCursor(LoadCursorW(NULL, (const WCHAR *)IDC_WAIT));
            net_backend_repair();
            SetCursor(LoadCursorW(NULL, (const WCHAR *)IDC_ARROW));
            update_status_controls(hwnd);
            MessageBoxW(hwnd, L"Windows has finished repairing the connection.",
                        L"Repair", MB_OK | MB_ICONINFORMATION);
            break;

        case IDC_BTN_SM_DETAILS:
        {
            WCHAR details_msg[512];
            WCHAR phys_addr[32] = L"Unavailable";
            WCHAR dhcp_srv[32] = L"Unavailable";
            WCHAR dns_srv[32] = L"Unavailable";
            IP_ADAPTER_INFO adapter_buf[16];
            ULONG adapter_len = sizeof(adapter_buf);

            if (GetAdaptersInfo(adapter_buf, &adapter_len) == ERROR_SUCCESS)
            {
                PIP_ADAPTER_INFO p = adapter_buf;
                while (p)
                {
                    if (p->AddressLength >= 6)
                    {
                        wsprintfW(phys_addr, L"%02X-%02X-%02X-%02X-%02X-%02X",
                                  p->Address[0], p->Address[1], p->Address[2],
                                  p->Address[3], p->Address[4], p->Address[5]);
                    }
                    if (p->DhcpServer.IpAddress.String[0] &&
                        strcmp(p->DhcpServer.IpAddress.String, "0.0.0.0") != 0)
                    {
                        MultiByteToWideChar(CP_ACP, 0, p->DhcpServer.IpAddress.String, -1, dhcp_srv, 32);
                    }
                    else if (g_status_info.default_gateway[0])
                    {
                        lstrcpynW(dhcp_srv, g_status_info.default_gateway, 32);
                    }
                    break;
                }
            }

            {
                FIXED_INFO fixed_buf;
                ULONG fixed_len = sizeof(fixed_buf);
                if (GetNetworkParams(&fixed_buf, &fixed_len) == ERROR_SUCCESS)
                {
                    if (fixed_buf.DnsServerList.IpAddress.String[0] &&
                        strcmp(fixed_buf.DnsServerList.IpAddress.String, "0.0.0.0") != 0)
                    {
                        MultiByteToWideChar(CP_ACP, 0, fixed_buf.DnsServerList.IpAddress.String, -1, dns_srv, 32);
                    }
                }
                if (wcscmp(dns_srv, L"Unavailable") == 0 && g_status_info.default_gateway[0])
                {
                    lstrcpynW(dns_srv, g_status_info.default_gateway, 32);
                }
            }

            wsprintfW(details_msg,
                      L"Physical Address:\t%s\r\n"
                      L"IP Address:\t%s\r\n"
                      L"Subnet Mask:\t%s\r\n"
                      L"Default Gateway:\t%s\r\n"
                      L"DHCP Server:\t%s\r\n"
                      L"DNS Server:\t%s",
                      phys_addr,
                      g_status_info.ip_address,
                      g_status_info.subnet_mask,
                      g_status_info.default_gateway,
                      dhcp_srv,
                      dns_srv);
            MessageBoxW(hwnd, details_msg, L"Network Connection Details", MB_OK | MB_ICONINFORMATION);
            break;
        }

        case IDC_BTN_SM_PROPERTIES:
            net_props_show(hwnd);
            update_status_controls(hwnd);
            break;

        case IDCANCEL:
            KillTimer(hwnd, IDT_STATUS_TIMER);
            EndDialog(hwnd, IDCANCEL);
            break;
        }
        return TRUE;
    }

    case WM_DESTROY:
        KillTimer(hwnd, IDT_STATUS_TIMER);
        break;
    }

    return FALSE;
}

void wlan_status_show(HWND parent)
{
    INITCOMMONCONTROLSEX icex;
    int dlg_id;
    icex.dwSize = sizeof(icex);
    icex.dwICC = ICC_TAB_CLASSES;
    InitCommonControlsEx(&icex);
    net_backend_get_status(&g_status_info);
    dlg_id = (g_status_info.conn_type == NET_TYPE_ETHERNET) ? IDD_STATMON_GENERAL_LAN : IDD_STATMON_GENERAL_WLAN;
    DialogBoxParamW(WexResModule(), MAKEINTRESOURCEW(dlg_id), parent, WlanStatusDlgProc, 0);
}

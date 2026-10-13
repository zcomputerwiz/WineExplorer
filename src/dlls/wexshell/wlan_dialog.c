/*
 * WineExplorer Desktop Environment - Wireless Quick Connect Dialog Implementation
 *
 * Copyright 2026 WineExplorer Desktop Environment Project
 *
 * Implements the authentic Windows XP / Server 2003 "Connect to Wireless Network"
 * dialog (IDD_WZCQCFG) with available AP listing, signal strength metrics,
 * security warnings, and WPA/WPA2 key confirmation.
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

#include "wine/debug.h"
#include "resource.h"
#include "net_backend.h"
#include "wlan_dialog.h"
#include "wlan_status.h"

#include "module.h"

WINE_DEFAULT_DEBUG_CHANNEL(explorer);

static WLAN_NETWORK_ENTRY g_scan_entries[MAX_NETWORKS];
static int g_scan_count = 0;
static HIMAGELIST g_hImageList = NULL;

static void update_security_pane(HWND hwnd, int selected_idx)
{
    BOOL is_connected = FALSE;
    BOOL is_secure = TRUE;
    HWND hWarningIco = GetDlgItem(hwnd, IDC_WZCQCFG_ICO_WARN);
    HWND hWarningLbl = GetDlgItem(hwnd, IDC_WZCQCFG_LBL_NOWKINFO);
    HWND hUnsecureChk = GetDlgItem(hwnd, IDC_WZCQCFG_CHK_NOWK);
    HWND hKeyInfoLbl = GetDlgItem(hwnd, IDC_WZCQCFG_LBL_WKINFO);
    HWND hKeyLbl = GetDlgItem(hwnd, IDC_WZCQCFG_LBL_WEPK);
    HWND hKeyEdit = GetDlgItem(hwnd, IDC_WZCQCFG_WEPK);
    HWND hKey2Lbl = GetDlgItem(hwnd, IDC_WZCQCFG_LBL_WEPK2);
    HWND hKey2Edit = GetDlgItem(hwnd, IDC_WZCQCFG_WEPK2);
    HWND hShowPwdChk = GetDlgItem(hwnd, IDC_WZCQCFG_CHK_SHOWPWD);
    HWND hConnectBtn = GetDlgItem(hwnd, IDC_WZCQCFG_CONNECT);

    if (selected_idx >= 0 && selected_idx < g_scan_count)
    {
        is_connected = g_scan_entries[selected_idx].is_connected;
        is_secure = g_scan_entries[selected_idx].is_secure;
    }

    if (is_connected)
    {
        /* Already connected to this network */
        SetWindowTextW(hConnectBtn, L"&Disconnect");
        EnableWindow(hConnectBtn, TRUE);

        ShowWindow(hWarningIco, SW_HIDE);
        ShowWindow(hWarningLbl, SW_HIDE);
        ShowWindow(hUnsecureChk, SW_HIDE);

        SetWindowTextW(hKeyInfoLbl, L"You are currently connected to this network. To disconnect, click Disconnect.");
        ShowWindow(hKeyInfoLbl, SW_SHOW);
        ShowWindow(hKeyLbl, SW_HIDE);
        ShowWindow(hKeyEdit, SW_HIDE);
        ShowWindow(hKey2Lbl, SW_HIDE);
        ShowWindow(hKey2Edit, SW_HIDE);
        ShowWindow(hShowPwdChk, SW_HIDE);
    }
    else if (!is_secure)
    {
        /* Unsecured Network */
        SetWindowTextW(hConnectBtn, L"&Connect");
        ShowWindow(hKeyInfoLbl, SW_HIDE);
        ShowWindow(hKeyLbl, SW_HIDE);
        ShowWindow(hKeyEdit, SW_HIDE);
        ShowWindow(hKey2Lbl, SW_HIDE);
        ShowWindow(hKey2Edit, SW_HIDE);
        ShowWindow(hShowPwdChk, SW_HIDE);

        ShowWindow(hWarningIco, SW_SHOW);
        ShowWindow(hWarningLbl, SW_SHOW);
        ShowWindow(hUnsecureChk, SW_SHOW);

        /* Connect button enabled only if unsecure checkbox is checked */
        EnableWindow(hConnectBtn, SendMessageW(hUnsecureChk, BM_GETCHECK, 0, 0) == BST_CHECKED);
    }
    else
    {
        /* Protected Network */
        SetWindowTextW(hConnectBtn, L"&Connect");
        EnableWindow(hConnectBtn, TRUE);

        ShowWindow(hWarningIco, SW_HIDE);
        ShowWindow(hWarningLbl, SW_HIDE);
        ShowWindow(hUnsecureChk, SW_HIDE);

        SetWindowTextW(hKeyInfoLbl, L"This wireless network requires the use of a network key. To access this network, type the key, then click Connect.");
        ShowWindow(hKeyInfoLbl, SW_SHOW);
        ShowWindow(hKeyLbl, SW_SHOW);
        ShowWindow(hKeyEdit, SW_SHOW);
        ShowWindow(hKey2Lbl, SW_SHOW);
        ShowWindow(hKey2Edit, SW_SHOW);
        ShowWindow(hShowPwdChk, SW_SHOW);
    }
}

static void refresh_network_list(HWND hwnd)
{
    HWND hList = GetDlgItem(hwnd, IDC_WZCQCFG_NETWORKS);
    int i, select_idx = 0;

    SendMessageW(hList, LVM_DELETEALLITEMS, 0, 0);
    g_scan_count = net_backend_scan_wireless(g_scan_entries, MAX_NETWORKS);

    for (i = 0; i < g_scan_count; i++)
    {
        LVITEMW item = {0};
        WCHAR display_name[MAX_SSID_LEN + 16];
        WCHAR sig_str[32];

        if (g_scan_entries[i].is_connected)
        {
            wsprintfW(display_name, L"%s (Connected)", g_scan_entries[i].ssid);
            select_idx = i;
        }
        else
        {
            lstrcpyW(display_name, g_scan_entries[i].ssid);
        }

        item.mask = LVIF_TEXT | LVIF_IMAGE | LVIF_PARAM;
        item.iItem = i;
        item.iSubItem = 0;
        item.pszText = display_name;
        item.iImage = g_scan_entries[i].signal_bars;
        item.lParam = (LPARAM)i;

        SendMessageW(hList, LVM_INSERTITEMW, 0, (LPARAM)&item);

        /* Subitem 1: Signal strength text */
        wsprintfW(sig_str, L"%d%%", g_scan_entries[i].signal_percent);
        ListView_SetItemTextW(hList, i, 1, sig_str);

        /* Subitem 2: Security mode */
        ListView_SetItemTextW(hList, i, 2, g_scan_entries[i].security);
    }

    if (g_scan_count > 0)
    {
        LVITEMW lvi;
        memset(&lvi, 0, sizeof(lvi));
        lvi.state = LVIS_SELECTED | LVIS_FOCUSED;
        lvi.stateMask = LVIS_SELECTED | LVIS_FOCUSED;
        SendMessageW(hList, LVM_SETITEMSTATE, (WPARAM)select_idx, (LPARAM)&lvi);
        SendMessageW(hList, LVM_ENSUREVISIBLE, select_idx, FALSE);
    }

    update_security_pane(hwnd, select_idx);
}

static INT_PTR CALLBACK WlanSelectDlgProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam)
{
    switch (msg)
    {
    case WM_INITDIALOG:
    {
        HINSTANCE hInst = explorer_hInstance;
        HWND hList = GetDlgItem(hwnd, IDC_WZCQCFG_NETWORKS);
        HWND hWarnIco = GetDlgItem(hwnd, IDC_WZCQCFG_ICO_WARN);
        LVCOLUMNW col = {0};
        HICON hIconMain;
        int i;

        /* Set Dialog and Warning Icons */
        hIconMain = LoadIconW(hInst, MAKEINTRESOURCEW(IDI_NET_WIRELESS));
        SendMessageW(hwnd, WM_SETICON, ICON_BIG, (LPARAM)hIconMain);
        SendMessageW(hwnd, WM_SETICON, ICON_SMALL, (LPARAM)hIconMain);

        SendMessageW(hWarnIco, STM_SETICON, (LPARAM)LoadIconW(NULL, (const WCHAR *)IDI_WARNING), 0);

        /* Configure List View */
        SendMessageW(hList, LVM_SETEXTENDEDLISTVIEWSTYLE, 0, LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES);

        col.mask = LVCF_TEXT | LVCF_WIDTH;
        col.cx = 125;
        col.pszText = (LPWSTR)L"Network Name";
        SendMessageW(hList, LVM_INSERTCOLUMNW, 0, (LPARAM)&col);

        col.cx = 40;
        col.pszText = (LPWSTR)L"Signal";
        SendMessageW(hList, LVM_INSERTCOLUMNW, 1, (LPARAM)&col);

        col.cx = 65;
        col.pszText = (LPWSTR)L"Security";
        SendMessageW(hList, LVM_INSERTCOLUMNW, 2, (LPARAM)&col);

        /* Initialize 16x16 Signal ImageList */
        g_hImageList = ImageList_Create(16, 16, ILC_COLOR32 | ILC_MASK, 6, 1);
        for (i = 0; i <= 5; i++)
        {
            HICON hSig = LoadIconW(hInst, MAKEINTRESOURCEW(IDI_NET_SIG0 + i));
            ImageList_AddIcon(g_hImageList, hSig ? hSig : hIconMain);
        }
        SendMessageW(hList, LVM_SETIMAGELIST, LVSIL_SMALL, (LPARAM)g_hImageList);

        refresh_network_list(hwnd);
        return TRUE;
    }

    case WM_NOTIFY:
    {
        NMHDR *hdr = (NMHDR*)lparam;
        if (hdr->idFrom == IDC_WZCQCFG_NETWORKS && hdr->code == LVN_ITEMCHANGED)
        {
            NMLISTVIEW *nmlv = (NMLISTVIEW*)lparam;
            if (nmlv->uNewState & LVIS_SELECTED)
            {
                update_security_pane(hwnd, nmlv->iItem);
            }
        }
        break;
    }

    case WM_COMMAND:
    {
        WORD cmd = LOWORD(wparam);
        switch (cmd)
        {
        case IDC_WZCQCFG_CHK_NOWK:
        {
            HWND hUnsecureChk = GetDlgItem(hwnd, IDC_WZCQCFG_CHK_NOWK);
            HWND hConnectBtn = GetDlgItem(hwnd, IDC_WZCQCFG_CONNECT);
            EnableWindow(hConnectBtn, SendMessageW(hUnsecureChk, BM_GETCHECK, 0, 0) == BST_CHECKED);
            break;
        }

        case IDC_WZCQCFG_CHK_SHOWPWD:
        {
            HWND hShowPwdChk = GetDlgItem(hwnd, IDC_WZCQCFG_CHK_SHOWPWD);
            HWND hKeyEdit = GetDlgItem(hwnd, IDC_WZCQCFG_WEPK);
            HWND hKey2Edit = GetDlgItem(hwnd, IDC_WZCQCFG_WEPK2);
            BOOL show = (SendMessageW(hShowPwdChk, BM_GETCHECK, 0, 0) == BST_CHECKED);
            WCHAR ch = show ? 0 : L'*';

            SendMessageW(hKeyEdit, EM_SETPASSWORDCHAR, (WPARAM)ch, 0);
            SendMessageW(hKey2Edit, EM_SETPASSWORDCHAR, (WPARAM)ch, 0);
            InvalidateRect(hKeyEdit, NULL, TRUE);
            InvalidateRect(hKey2Edit, NULL, TRUE);
            break;
        }

        case IDC_WZCQCFG_REFRESH:
            refresh_network_list(hwnd);
            break;

        case IDC_WZCQCFG_ADVANCED:
            wlan_status_show(hwnd);
            break;

        case IDC_WZCQCFG_CONNECT:
        {
            HWND hList = GetDlgItem(hwnd, IDC_WZCQCFG_NETWORKS);
            int sel = (int)SendMessageW(hList, LVM_GETNEXTITEM, (WPARAM)-1, LVNI_SELECTED);
            if (sel >= 0 && sel < g_scan_count)
            {
                if (g_scan_entries[sel].is_connected)
                {
                    /* Disconnect */
                    net_backend_disconnect();
                    refresh_network_list(hwnd);
                }
                else
                {
                    WCHAR key1[128] = {0};
                    WCHAR key2[128] = {0};

                    if (g_scan_entries[sel].is_secure)
                    {
                        GetDlgItemTextW(hwnd, IDC_WZCQCFG_WEPK, key1, 128);
                        GetDlgItemTextW(hwnd, IDC_WZCQCFG_WEPK2, key2, 128);

                        if (lstrlenW(key1) == 0)
                        {
                            MessageBoxW(hwnd, L"Please enter the network security key or passphrase.",
                                        L"Wireless Configuration", MB_OK | MB_ICONEXCLAMATION);
                            SetFocus(GetDlgItem(hwnd, IDC_WZCQCFG_WEPK));
                            return TRUE;
                        }

                        if (lstrlenW(key2) > 0 && wcscmp(key1, key2) != 0)
                        {
                            MessageBoxW(hwnd, L"The network keys you typed do not match. Please reenter the network key in the confirmation text box.",
                                        L"Wireless Configuration", MB_OK | MB_ICONEXCLAMATION);
                            SetFocus(GetDlgItem(hwnd, IDC_WZCQCFG_WEPK2));
                            return TRUE;
                        }
                    }

                    /* Initiate Connection */
                    SetCursor(LoadCursorW(NULL, (const WCHAR *)IDC_WAIT));
                    net_backend_connect_wireless(g_scan_entries[sel].ssid, key1);
                    SetCursor(LoadCursorW(NULL, (const WCHAR *)IDC_ARROW));

                    MessageBoxW(hwnd, L"Windows has connected to the wireless network.",
                                L"Wireless Network Connection", MB_OK | MB_ICONINFORMATION);
                    refresh_network_list(hwnd);
                    EndDialog(hwnd, IDOK);
                }
            }
            break;
        }

        case IDCANCEL:
            EndDialog(hwnd, IDCANCEL);
            break;
        }
        return TRUE;
    }

    case WM_DESTROY:
        if (g_hImageList)
        {
            ImageList_Destroy(g_hImageList);
            g_hImageList = NULL;
        }
        break;
    }

    return FALSE;
}

void wlan_dialog_show(HWND parent)
{
    DialogBoxParamW(explorer_hInstance, MAKEINTRESOURCEW(IDD_WZCQCFG), parent, WlanSelectDlgProc, 0);
}

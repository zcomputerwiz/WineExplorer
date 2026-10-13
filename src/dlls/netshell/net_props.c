/*
 * WineExplorer Desktop Environment - Network Connection Properties Implementation
 *
 * Copyright 2026 WineExplorer Desktop Environment Project
 *
 * Implements the authentic Windows XP / Server 2003 "Local Area Connection Properties"
 * dialog matching ground truth, with General/Advanced tabs, component checklist,
 * device configuration, and the "Show icon in notification area when connected" toggle.
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
#include "wexnet.h"
#include "nettray.h"
#include "net_props.h"

#include "module.h"
#include "wexres.h"

WINE_DEFAULT_DEBUG_CHANNEL(explorer);

static int g_current_prop_tab = 0;
static NET_STATUS_INFO g_prop_status;

/* Only components the host really has are listed: IPv4 always, IPv6 when the kernel exposes it. */
static const WCHAR *comp_names[2];
static int comp_count;

static void init_components(void)
{
    comp_count = 0;
    comp_names[comp_count++] = L"Internet Protocol (TCP/IP)";
    if (GetFileAttributesA("/proc/net/if_inet6") != INVALID_FILE_ATTRIBUTES)
        comp_names[comp_count++] = L"Internet Protocol Version 6 (TCP/IPv6)";
}

static const WCHAR *get_component_desc(int index)
{
    switch (index)
    {
    case 0:
        return L"Transmission Control Protocol/Internet Protocol. The default wide area network protocol that provides communication across diverse interconnected networks.";
    case 1:
        return L"Version 6 of the Internet Protocol (IPv6), provided by the host operating system.";
    default:
        return L"";
    }
}

static void set_listview_check(HWND hList, int item, BOOL check)
{
    LVITEMW lvi;
    memset(&lvi, 0, sizeof(lvi));
    lvi.state = INDEXTOSTATEIMAGEMASK(check ? 2 : 1);
    lvi.stateMask = LVIS_STATEIMAGEMASK;
    SendMessageW(hList, LVM_SETITEMSTATE, (WPARAM)item, (LPARAM)&lvi);
}

static void set_listview_item_selected(HWND hList, int item)
{
    LVITEMW lvi;
    memset(&lvi, 0, sizeof(lvi));
    lvi.state = LVIS_SELECTED | LVIS_FOCUSED;
    lvi.stateMask = LVIS_SELECTED | LVIS_FOCUSED;
    SendMessageW(hList, LVM_SETITEMSTATE, (WPARAM)item, (LPARAM)&lvi);
}

static void update_prop_tab_visibility(HWND hwnd, int tab)
{
    BOOL is_general = (tab == 0);

    /* General Tab controls */
    int general_ids[] = {
        IDC_NETPROP_ADAPTER_ICO, IDC_NETPROP_ADAPTER_NAME, IDC_NETPROP_CONFIGURE,
        IDC_NETPROP_COMPLIST, IDC_NETPROP_INSTALL, IDC_NETPROP_UNINSTALL, IDC_NETPROP_PROPERTIES,
        IDC_NETPROP_DESCGROUP, IDC_NETPROP_DESC, IDC_NETPROP_SHOWICON, IDC_NETPROP_NOTIFY, 0
    };

    /* Advanced Tab controls */
    int advanced_ids[] = {
        IDC_NETPROP_FW_GROUP, IDC_NETPROP_FIREWALL, IDC_NETPROP_FW_LINK,
        IDC_NETPROP_ICS_GROUP, IDC_NETPROP_ICS_SHARE, IDC_NETPROP_ICS_BEACON, IDC_NETPROP_SETTINGS, 0
    };

    int i = 0;
    while (general_ids[i])
    {
        HWND hCtrl = GetDlgItem(hwnd, general_ids[i]);
        if (hCtrl) ShowWindow(hCtrl, is_general ? SW_SHOW : SW_HIDE);
        i++;
    }

    i = 0;
    while (advanced_ids[i])
    {
        HWND hCtrl = GetDlgItem(hwnd, advanced_ids[i]);
        if (hCtrl) ShowWindow(hCtrl, is_general ? SW_HIDE : SW_SHOW);
        i++;
    }

    InvalidateRect(hwnd, NULL, TRUE);
}

static INT_PTR CALLBACK LanPropertiesDlgProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam)
{
    switch (msg)
    {
    case WM_INITDIALOG:
    {
        HINSTANCE hInst = netshell_module();
        HWND hTab = GetDlgItem(hwnd, IDC_NETPROP_TAB);
        HWND hList = GetDlgItem(hwnd, IDC_NETPROP_COMPLIST);
        TCITEMW ti = {0};
        HICON hIconWin, hIconAdapter, hIconProtocol;
        HIMAGELIST himl;
        LVCOLUMNW col = {0};
        LVITEMW item = {0};
        RECT rcList;
        BOOL show_icon;
        int i;
        init_components();

        /* Set Dialog Window Icon (network cable/NIC) */
        hIconWin = LoadIconW(WexResModule(), MAKEINTRESOURCEW(IDI_NET_LAN));
        SendMessageW(hwnd, WM_SETICON, ICON_BIG, (LPARAM)hIconWin);
        SendMessageW(hwnd, WM_SETICON, ICON_SMALL, (LPARAM)hIconWin);

        /* Set Adapter Icon */
        hIconAdapter = LoadIconW(WexResModule(), MAKEINTRESOURCEW(IDI_NET_ADAPTER));
        SendDlgItemMessageW(hwnd, IDC_NETPROP_ADAPTER_ICO, STM_SETICON, (WPARAM)hIconAdapter, 0);

        /* Query Adapter Status and Name */
        net_backend_get_status(&g_prop_status);
        SetDlgItemTextW(hwnd, IDC_NETPROP_ADAPTER_NAME,
                        g_prop_status.adapter_name);

        /* Initialize Tabs */
        ti.mask = TCIF_TEXT;
        ti.pszText = (LPWSTR)L"General";
        TabCtrl_InsertItemW(hTab, 0, &ti);

        ti.pszText = (LPWSTR)L"Advanced";
        TabCtrl_InsertItemW(hTab, 1, &ti);

        /* Initialize Component ListView */
        SendMessageW(hList, LVM_SETEXTENDEDLISTVIEWSTYLE, 0,
                     (LPARAM)(LVS_EX_CHECKBOXES | LVS_EX_FULLROWSELECT | LVS_EX_SUBITEMIMAGES));

        himl = ImageList_Create(16, 16, ILC_COLOR32 | ILC_MASK, 1, 1);
        hIconProtocol = LoadIconW(WexResModule(), MAKEINTRESOURCEW(IDI_NET_PROTOCOL));
        ImageList_AddIcon(himl, hIconProtocol);
        SendMessageW(hList, LVM_SETIMAGELIST, (WPARAM)LVSIL_SMALL, (LPARAM)himl);

        GetClientRect(hList, &rcList);
        col.mask = LVCF_WIDTH;
        col.cx = rcList.right - GetSystemMetrics(SM_CXVSCROLL) - 4;
        SendMessageW(hList, LVM_INSERTCOLUMNW, 0, (LPARAM)&col);

        /* Insert Network Components */

        for (i = 0; i < comp_count; i++)
        {
            memset(&item, 0, sizeof(item));
            item.mask = LVIF_TEXT | LVIF_IMAGE;
            item.iItem = i;
            item.pszText = (LPWSTR)comp_names[i];
            item.iImage = 0;
            SendMessageW(hList, LVM_INSERTITEMW, 0, (LPARAM)&item);
            set_listview_check(hList, i, TRUE);
        }

        /* Select first item */
        set_listview_item_selected(hList, 0);
        SetDlgItemTextW(hwnd, IDC_NETPROP_DESC, get_component_desc(0));

        /* Initialize Show Icon Checkbox from registry */
        show_icon = net_backend_get_show_tray_icon();
        CheckDlgButton(hwnd, IDC_NETPROP_SHOWICON, show_icon ? BST_CHECKED : BST_UNCHECKED);
        CheckDlgButton(hwnd, IDC_NETPROP_NOTIFY, BST_CHECKED);  /* ponytail: not persisted or acted on yet */

        /* Firewall / connection sharing are Windows services; the host handles them, so show them inert */
        {
            static const int inert[] = { IDC_NETPROP_FIREWALL, IDC_NETPROP_ICS_SHARE, IDC_NETPROP_ICS_BEACON,
                                         IDC_NETPROP_FW_LINK, IDC_NETPROP_SETTINGS };
            for (i = 0; i < (int)ARRAY_SIZE(inert); i++)
            {
                EnableWindow(GetDlgItem(hwnd, inert[i]), FALSE);
                if (i < 3) CheckDlgButton(hwnd, inert[i], BST_UNCHECKED);
            }
        }

        g_current_prop_tab = 0;
        update_prop_tab_visibility(hwnd, 0);

        return TRUE;
    }

    case WM_NOTIFY:
    {
        NMHDR *hdr = (NMHDR*)lparam;
        if (hdr->idFrom == IDC_NETPROP_TAB && hdr->code == TCN_SELCHANGE)
        {
            g_current_prop_tab = ((int)SendMessageW(hdr->hwndFrom, TCM_GETCURSEL, 0, 0));
            update_prop_tab_visibility(hwnd, g_current_prop_tab);
        }
        else if (hdr->idFrom == IDC_NETPROP_COMPLIST)
        {
            if (hdr->code == LVN_ITEMCHANGED)
            {
                NMLISTVIEW *nmlv = (NMLISTVIEW*)lparam;
                if (nmlv->uNewState & LVIS_SELECTED)
                {
                    SetDlgItemTextW(hwnd, IDC_NETPROP_DESC, get_component_desc(nmlv->iItem));
                }
            }
            else if (hdr->code == NM_DBLCLK)
            {
                PostMessageW(hwnd, WM_COMMAND, MAKEWPARAM(IDC_NETPROP_PROPERTIES, BN_CLICKED), 0);
            }
        }
        break;
    }

    case WM_COMMAND:
    {
        WORD cmd = LOWORD(wparam);
        switch (cmd)
        {
        case IDOK:
        {
            BOOL show = (IsDlgButtonChecked(hwnd, IDC_NETPROP_SHOWICON) == BST_CHECKED);
            net_backend_set_show_tray_icon(show);
            nettray_update_visibility();
            EndDialog(hwnd, IDOK);
            return TRUE;
        }

        case IDCANCEL:
            EndDialog(hwnd, IDCANCEL);
            return TRUE;

        case IDC_NETPROP_CONFIGURE:
        {
            WCHAR msg_buf[256];
            wsprintfW(msg_buf, L"%s\n\nDevice Status: This device is working properly.",
                      g_prop_status.adapter_name,
                      g_prop_status.is_connected ? L"The network link is up." : L"The network link is down.");
            MessageBoxW(hwnd, msg_buf, L"Device Properties", MB_OK | MB_ICONINFORMATION);
            return TRUE;
        }

        case IDC_NETPROP_INSTALL:
            MessageBoxW(hwnd, L"Cannot install additional network components: Wine Desktop operates with host system network protocols.",
                        L"Install Component", MB_OK | MB_ICONINFORMATION);
            return TRUE;

        case IDC_NETPROP_UNINSTALL:
            MessageBoxW(hwnd, L"The selected component cannot be uninstalled because it is required by the operating system.",
                        L"Uninstall Component", MB_OK | MB_ICONWARNING);
            return TRUE;

        case IDC_NETPROP_PROPERTIES:
        {
            HWND hList = GetDlgItem(hwnd, IDC_NETPROP_COMPLIST);
            int sel = (int)SendMessageW(hList, LVM_GETNEXTITEM, (WPARAM)-1, MAKELPARAM(LVNI_SELECTED, 0));
            if (sel == 0)
            {
                WCHAR tcp_info[512];
                wsprintfW(tcp_info,
                          L"Internet Protocol (TCP/IP) Properties\n\n"
                          L"IP Address:\t\t%s\n"
                          L"Subnet Mask:\t\t%s\n"
                          L"Default Gateway:\t%s\n\n"
                          L"Addresses are managed by the host operating system.",
                          g_prop_status.ip_address, g_prop_status.subnet_mask, g_prop_status.default_gateway);
                MessageBoxW(hwnd, tcp_info, L"Internet Protocol (TCP/IP) Properties", MB_OK | MB_ICONINFORMATION);
            }
            else
            {
                MessageBoxW(hwnd, L"No additional properties are configurable for this component.",
                            L"Properties", MB_OK | MB_ICONINFORMATION);
            }
            return TRUE;
        }

        case IDC_NETPROP_FW_LINK:
        case IDC_NETPROP_SETTINGS:
            MessageBoxW(hwnd, L"Internet Connection Firewall and Sharing settings are configured and protected by the host Linux environment.",
                        L"Advanced Settings", MB_OK | MB_ICONINFORMATION);
            return TRUE;
        }
        break;
    }

    case WM_DESTROY:
    {
        HWND hList = GetDlgItem(hwnd, IDC_NETPROP_COMPLIST);
        HIMAGELIST himl = (HIMAGELIST)SendMessageW(hList, LVM_GETIMAGELIST, (WPARAM)LVSIL_SMALL, 0);
        if (himl) ImageList_Destroy(himl);
        break;
    }
    }

    return FALSE;
}

void net_props_show(HWND parent)
{
    INITCOMMONCONTROLSEX icex;
    icex.dwSize = sizeof(icex);
    icex.dwICC = ICC_TAB_CLASSES | ICC_LISTVIEW_CLASSES;
    InitCommonControlsEx(&icex);
    DialogBoxParamW(netshell_module(), MAKEINTRESOURCEW(IDD_LAN_PROPERTIES), parent, LanPropertiesDlgProc, 0);
}

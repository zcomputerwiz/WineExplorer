/*
 * WineExplorer Desktop Environment - Customize Notifications
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

#include "resource.h"
#include "notifyprops.h"
#include "module.h"


#define MAX_ICONS 64

static TRAY_ICON_INFO g_icons[MAX_ICONS];
static int g_count;
static int g_original[MAX_ICONS];

static const WCHAR *const behavior_names[] = { L"Hide when inactive", L"Always hide", L"Always show" };

static void fill_list(HWND list)
{
    HIMAGELIST images = ImageList_Create(16, 16, ILC_COLOR32 | ILC_MASK, g_count + 1, 4);
    int i;

    SendMessageW(list, LVM_DELETEALLITEMS, 0, 0);
    for (i = 0; i < g_count; i++)
    {
        LVITEMW item = { LVIF_TEXT | LVIF_IMAGE, i, 0 };
        LVITEMW sub = { LVIF_TEXT, i, 1 };

        item.iImage = g_icons[i].icon ? ImageList_AddIcon(images, g_icons[i].icon) : -1;
        item.pszText = g_icons[i].text;
        SendMessageW(list, LVM_INSERTITEMW, 0, (LPARAM)&item);
        sub.pszText = (WCHAR *)behavior_names[g_icons[i].behavior];
        SendMessageW(list, LVM_SETITEMW, 0, (LPARAM)&sub);
    }
    ImageList_Destroy((HIMAGELIST)SendMessageW(list, LVM_SETIMAGELIST, LVSIL_SMALL, (LPARAM)images));
}

static INT_PTR CALLBACK notify_proc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam)
{
    HWND list = GetDlgItem(hwnd, IDC_NOTIFY_ITEMS), combo = GetDlgItem(hwnd, IDC_COMBO_ACTION);
    int i;

    switch (msg)
    {
    case WM_INITDIALOG:
    {
        LVCOLUMNW col = { LVCF_TEXT | LVCF_WIDTH };

        col.pszText = (WCHAR *)L"Name";
        col.cx = 108;
        SendMessageW(list, LVM_INSERTCOLUMNW, 0, (LPARAM)&col);
        col.pszText = (WCHAR *)L"Behavior";
        col.cx = 106;
        SendMessageW(list, LVM_INSERTCOLUMNW, 1, (LPARAM)&col);
        SendMessageW(list, LVM_SETEXTENDEDLISTVIEWSTYLE, LVS_EX_FULLROWSELECT, LVS_EX_FULLROWSELECT);
        g_count = systray_enum_icons(g_icons, MAX_ICONS);
        for (i = 0; i < g_count; i++) g_original[i] = g_icons[i].behavior;
        fill_list(list);
        for (i = 0; i < 3; i++) SendMessageW(combo, CB_ADDSTRING, 0, (LPARAM)behavior_names[i]);
        EnableWindow(combo, FALSE);
        return TRUE;
    }

    case WM_DESTROY:
        ImageList_Destroy((HIMAGELIST)SendMessageW(list, LVM_SETIMAGELIST, LVSIL_SMALL, 0));
        return TRUE;

    case WM_NOTIFY:
        if (((NMHDR *)lparam)->hwndFrom == list && ((NMHDR *)lparam)->code == LVN_ITEMCHANGED)
        {
            int sel = (int)SendMessageW(list, LVM_GETNEXTITEM, -1, LVNI_SELECTED);

            EnableWindow(combo, sel >= 0);
            if (sel >= 0) SendMessageW(combo, CB_SETCURSEL, g_icons[sel].behavior, 0);
            return TRUE;
        }
        break;

    case WM_COMMAND:
        switch (LOWORD(wparam))
        {
        case IDC_COMBO_ACTION:
            if (HIWORD(wparam) == CBN_SELCHANGE)
            {
                int sel = (int)SendMessageW(list, LVM_GETNEXTITEM, -1, LVNI_SELECTED);
                int value = (int)SendMessageW(combo, CB_GETCURSEL, 0, 0);

                if (sel >= 0 && value >= 0)
                {
                    LVITEMW sub = { LVIF_TEXT, sel, 1 };

                    g_icons[sel].behavior = value;
                    sub.pszText = (WCHAR *)behavior_names[value];
                    SendMessageW(list, LVM_SETITEMW, 0, (LPARAM)&sub);
                }
            }
            return TRUE;
        case IDB_NOTIFY_RESTOREDEFAULTS:
            for (i = 0; i < g_count; i++) g_icons[i].behavior = NOTIFY_HIDE_WHEN_INACTIVE;
            fill_list(list);
            EnableWindow(combo, FALSE);
            return TRUE;
        case IDOK:
            for (i = 0; i < g_count; i++)
                if (g_icons[i].behavior != g_original[i]) systray_set_behavior(g_icons[i].key, g_icons[i].behavior);
            /* fall through */
        case IDCANCEL:
            for (i = 0; i < g_count; i++) if (g_icons[i].icon) DestroyIcon(g_icons[i].icon);
            EndDialog(hwnd, LOWORD(wparam));
            return TRUE;
        }
        break;
    }
    return FALSE;
}

void notifyprops_show(HWND parent)
{
    INITCOMMONCONTROLSEX init = { sizeof(init), ICC_LISTVIEW_CLASSES };

    InitCommonControlsEx(&init);
    DialogBoxParamW(explorer_hInstance, MAKEINTRESOURCEW(IDD_NOTIFY_CUSTOMIZE), parent, notify_proc, 0);
}

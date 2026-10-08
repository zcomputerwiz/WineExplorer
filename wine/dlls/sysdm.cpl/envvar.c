/*
 * WineExplorer Desktop Environment - Environment Variables Dialog (sysdm.cpl)
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
#include <stdio.h>

#include "sysdm.h"

static void init_env_listview(HWND hList)
{
    LVCOLUMNW col;

    SendMessageW(hList, LVM_SETEXTENDEDLISTVIEWSTYLE, 0, LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES);

    memset(&col, 0, sizeof(col));
    col.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_SUBITEM;
    col.pszText = (LPWSTR)L"Variable";
    col.cx = 100;
    col.iSubItem = 0;
    ListView_InsertColumnW(hList, 0, &col);

    col.pszText = (LPWSTR)L"Value";
    col.cx = 240;
    col.iSubItem = 1;
    ListView_InsertColumnW(hList, 1, &col);
}

static void load_env_vars(HWND hList, HKEY hRoot, const WCHAR *subKey)
{
    HKEY hKey;
    DWORD index = 0;
    WCHAR name[256];
    WCHAR value[1024];
    DWORD name_len, val_size, type;
    LVITEMW item;

    SendMessageW(hList, LVM_DELETEALLITEMS, 0, 0);

    if (RegOpenKeyExW(hRoot, subKey, 0, KEY_READ, &hKey) == ERROR_SUCCESS)
    {
        for (index = 0; ; index++)
        {
            name_len = ARRAY_SIZE(name);
            val_size = sizeof(value);
            if (RegEnumValueW(hKey, index, name, &name_len, NULL, &type, (BYTE*)value, &val_size) != ERROR_SUCCESS)
                break;

            memset(&item, 0, sizeof(item));
            item.mask = LVIF_TEXT;
            item.iItem = index;
            item.pszText = name;
            ListView_InsertItemW(hList, &item);

            ListView_SetItemTextW(hList, index, 1, value);
        }
        RegCloseKey(hKey);
    }
}

static INT_PTR CALLBACK envvars_dlg_proc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam)
{
    switch (msg)
    {
    case WM_INITDIALOG:
    {
        HWND hUserList = GetDlgItem(hwnd, IDC_ENV_USER_LIST);
        HWND hSysList = GetDlgItem(hwnd, IDC_ENV_SYS_LIST);

        init_env_listview(hUserList);
        init_env_listview(hSysList);

        load_env_vars(hUserList, HKEY_CURRENT_USER, L"Environment");
        load_env_vars(hSysList, HKEY_LOCAL_MACHINE, L"System\\CurrentControlSet\\Control\\Session Manager\\Environment");
        return TRUE;
    }

    case WM_COMMAND:
        switch (LOWORD(wparam))
        {
        case IDOK:
        case IDCANCEL:
            EndDialog(hwnd, LOWORD(wparam));
            return TRUE;
        }
        break;
    }
    return FALSE;
}

void show_envvars_dialog(HWND parent)
{
    DialogBoxW(g_hInstance, MAKEINTRESOURCEW(IDD_ENVVARS), parent, envvars_dlg_proc);
}

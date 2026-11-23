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
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

#include "sysdm.h"

#define ENV_NAME_MAX  256
#define ENV_VALUE_MAX 4096

static const WCHAR user_key[] = L"Environment";
static const WCHAR sys_key[] = L"System\\CurrentControlSet\\Control\\Session Manager\\Environment";

/* The edits are kept in the lists and written on OK, as the dialog of Windows XP does; a list that was not touched is
 * not written (so a user without the right to change the system variables sees no error unless they changed one). */
static BOOL user_dirty, sys_dirty;

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
    WCHAR name[ENV_NAME_MAX];
    WCHAR value[ENV_VALUE_MAX];
    DWORD name_len, val_size, type;
    LVITEMW item;

    SendMessageW(hList, LVM_DELETEALLITEMS, 0, 0);

    if (RegOpenKeyExW(hRoot, subKey, 0, KEY_READ, &hKey) == ERROR_SUCCESS)
    {
        for (index = 0; ; index++)
        {
            name_len = ARRAY_SIZE(name);
            val_size = sizeof(value) - sizeof(WCHAR);
            if (RegEnumValueW(hKey, index, name, &name_len, NULL, &type, (BYTE*)value, &val_size) != ERROR_SUCCESS)
                break;
            if (type != REG_SZ && type != REG_EXPAND_SZ) continue;
            value[val_size / sizeof(WCHAR)] = 0;

            memset(&item, 0, sizeof(item));
            item.mask = LVIF_TEXT;
            item.iItem = (int)SendMessageW(hList, LVM_GETITEMCOUNT, 0, 0);
            item.pszText = name;
            ListView_InsertItemW(hList, &item);

            ListView_SetItemTextW(hList, item.iItem, 1, value);
        }
        RegCloseKey(hKey);
    }
}

struct env_edit
{
    WCHAR name[ENV_NAME_MAX];
    WCHAR value[ENV_VALUE_MAX];
    BOOL  new_var;
};

static INT_PTR CALLBACK env_edit_proc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam)
{
    struct env_edit *e;

    switch (msg)
    {
    case WM_INITDIALOG:
        e = (struct env_edit *)lparam;
        SetWindowLongPtrW(hwnd, DWLP_USER, (LONG_PTR)e);
        SetWindowTextW(hwnd, e->new_var ? L"New Variable" : L"Edit Variable");
        SendDlgItemMessageW(hwnd, IDC_ENVE_NAME, EM_LIMITTEXT, ENV_NAME_MAX - 1, 0);
        SendDlgItemMessageW(hwnd, IDC_ENVE_VALUE, EM_LIMITTEXT, ENV_VALUE_MAX - 1, 0);
        SetDlgItemTextW(hwnd, IDC_ENVE_NAME, e->name);
        SetDlgItemTextW(hwnd, IDC_ENVE_VALUE, e->value);
        return TRUE;

    case WM_COMMAND:
        switch (LOWORD(wparam))
        {
        case IDOK:
            e = (struct env_edit *)GetWindowLongPtrW(hwnd, DWLP_USER);
            GetDlgItemTextW(hwnd, IDC_ENVE_NAME, e->name, ARRAY_SIZE(e->name));
            GetDlgItemTextW(hwnd, IDC_ENVE_VALUE, e->value, ARRAY_SIZE(e->value));
            if (!e->name[0] || wcschr(e->name, '='))
            {
                MessageBoxW(hwnd, L"The variable name must not be empty or contain an equals sign (=).",
                            L"Environment Variables", MB_OK | MB_ICONWARNING);
                return TRUE;
            }
            EndDialog(hwnd, IDOK);
            return TRUE;
        case IDCANCEL:
            EndDialog(hwnd, IDCANCEL);
            return TRUE;
        }
        break;
    }
    return FALSE;
}

static int find_item( HWND list, const WCHAR *name, int skip )
{
    int i, count = (int)SendMessageW( list , LVM_GETITEMCOUNT, 0, 0);
    WCHAR buf[ENV_NAME_MAX];

    for (i = 0; i < count; i++)
    {
        if (i == skip) continue;
        ListView_GetItemTextW( list, i, 0, buf, ARRAY_SIZE(buf) );
        if (!wcsicmp( buf, name )) return i;
    }
    return -1;
}

/* New (item < 0) or Edit of one variable of a list */
static void edit_variable(HWND parent, HWND list, int item, BOOL *dirty)
{
    struct env_edit *e = calloc( 1, sizeof(*e) );
    int other;

    if (!e) return;
    e->new_var = item < 0;
    if (item >= 0)
    {
        ListView_GetItemTextW( list, item, 0, e->name, ARRAY_SIZE(e->name) );
        ListView_GetItemTextW( list, item, 1, e->value, ARRAY_SIZE(e->value) );
    }
    if (DialogBoxParamW( g_hInstance, MAKEINTRESOURCEW(IDD_ENVVAR_EDIT), parent, env_edit_proc, (LPARAM)e ) == IDOK)
    {
        other = find_item( list, e->name, item );
        if (other >= 0)  /* the name exists already: the value of that variable is replaced, as in Windows */
        {
            ListView_SetItemTextW( list, other, 1, e->value );
            if (item >= 0) SendMessageW(list, LVM_DELETEITEM, item, 0);
        }
        else if (item >= 0)
        {
            ListView_SetItemTextW( list, item, 0, e->name );
            ListView_SetItemTextW( list, item, 1, e->value );
        }
        else
        {
            LVITEMW li = {0};
            li.mask = LVIF_TEXT;
            li.iItem = (int)SendMessageW( list , LVM_GETITEMCOUNT, 0, 0);
            li.pszText = e->name;
            ListView_InsertItemW( list, &li );
            ListView_SetItemTextW( list, li.iItem, 1, e->value );
        }
        *dirty = TRUE;
    }
    free( e );
}

static void delete_variable(HWND list, BOOL *dirty)
{
    int item = (int)SendMessageW(list, LVM_GETNEXTITEM, (WPARAM)-1, LVNI_SELECTED);
    if (item < 0) return;
    SendMessageW(list, LVM_DELETEITEM, item, 0);
    *dirty = TRUE;
}

/* The registry gets the list: a value is REG_EXPAND_SZ when it holds a %variable%, as the dialog of Windows does;
 * values that are not in the list any more are removed. */
static DWORD apply_list(HWND list, HKEY root, const WCHAR *subkey)
{
    HKEY key;
    DWORD err, i, count = (int)SendMessageW( list , LVM_GETITEMCOUNT, 0, 0), n, len;
    WCHAR name[ENV_NAME_MAX], *value;
    WCHAR (*gone)[ENV_NAME_MAX] = NULL;
    DWORD ngone = 0;

    if ((err = RegCreateKeyExW( root, subkey, 0, NULL, 0, KEY_READ | KEY_WRITE, NULL, &key, NULL ))) return err;
    if (!(value = malloc( ENV_VALUE_MAX * sizeof(WCHAR) ))) { RegCloseKey( key ); return ERROR_OUTOFMEMORY; }

    for (i = 0; i < count && !err; i++)
    {
        const WCHAR *p;
        DWORD type = REG_SZ;

        ListView_GetItemTextW( list, i, 0, name, ARRAY_SIZE(name) );
        ListView_GetItemTextW( list, i, 1, value, ENV_VALUE_MAX );
        if ((p = wcschr( value, '%' )) && wcschr( p + 1, '%' )) type = REG_EXPAND_SZ;
        len = (lstrlenW( value ) + 1) * sizeof(WCHAR);
        err = RegSetValueExW( key, name, 0, type, (BYTE *)value, len );
    }

    /* collect the names that are gone, then delete them (deleting while enumerating skips entries) */
    if (!err)
    {
        for (n = 0; ; n++)
        {
            DWORD l = ARRAY_SIZE(name);
            if (RegEnumValueW( key, n, name, &l, NULL, NULL, NULL, NULL )) break;
            if (find_item( list, name, -1 ) >= 0) continue;
            gone = realloc( gone, (ngone + 1) * sizeof(*gone) );
            if (!gone) { ngone = 0; break; }
            lstrcpyW( gone[ngone++], name );
        }
        for (i = 0; i < ngone; i++) RegDeleteValueW( key, gone[i] );
    }
    free( gone );
    free( value );
    RegCloseKey( key );
    return err;
}

static void apply_changes(HWND hwnd)
{
    DWORD err;

    if (user_dirty && (err = apply_list( GetDlgItem( hwnd, IDC_ENV_USER_LIST ), HKEY_CURRENT_USER, user_key )))
    {
        WCHAR msg[128];
        swprintf( msg, ARRAY_SIZE(msg), L"The user variables could not be saved (error %lu).", err );
        MessageBoxW( hwnd, msg, L"Environment Variables", MB_OK | MB_ICONERROR );
    }
    if (sys_dirty && (err = apply_list( GetDlgItem( hwnd, IDC_ENV_SYS_LIST ), HKEY_LOCAL_MACHINE, sys_key )))
    {
        WCHAR msg[160];
        swprintf( msg, ARRAY_SIZE(msg), L"The system variables could not be saved (error %lu). Changing them needs "
                  L"administrator rights.", err );
        MessageBoxW( hwnd, msg, L"Environment Variables", MB_OK | MB_ICONERROR );
    }
    if (user_dirty || sys_dirty)
        SendMessageTimeoutW( HWND_BROADCAST, WM_SETTINGCHANGE, 0, (LPARAM)L"Environment", SMTO_ABORTIFHUNG, 5000, NULL );
}

static INT_PTR CALLBACK envvars_dlg_proc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam)
{
    switch (msg)
    {
    case WM_INITDIALOG:
    {
        HWND hUserList = GetDlgItem(hwnd, IDC_ENV_USER_LIST);
        HWND hSysList = GetDlgItem(hwnd, IDC_ENV_SYS_LIST);

        WCHAR user[256], title[320];
        DWORD len = ARRAY_SIZE(user);

        if (GetUserNameW(user, &len))  /* the group says whose variables these are (the template has a placeholder) */
        {
            swprintf(title, ARRAY_SIZE(title), L"User variables for %ls", user);
            SetDlgItemTextW(hwnd, IDC_ENV_USER_GROUP, title);
        }
        user_dirty = sys_dirty = FALSE;
        init_env_listview(hUserList);
        init_env_listview(hSysList);

        load_env_vars(hUserList, HKEY_CURRENT_USER, user_key);
        load_env_vars(hSysList, HKEY_LOCAL_MACHINE, sys_key);
        return TRUE;
    }

    case WM_COMMAND:
    {
        HWND hUserList = GetDlgItem(hwnd, IDC_ENV_USER_LIST);
        HWND hSysList = GetDlgItem(hwnd, IDC_ENV_SYS_LIST);

        switch (LOWORD(wparam))
        {
        case IDC_ENV_USER_NEW:  edit_variable(hwnd, hUserList, -1, &user_dirty); return TRUE;
        case IDC_ENV_USER_EDIT:
        {
            int i = (int)SendMessageW(hUserList, LVM_GETNEXTITEM, (WPARAM)-1, LVNI_SELECTED);
            if (i >= 0) edit_variable(hwnd, hUserList, i, &user_dirty);
            return TRUE;
        }
        case IDC_ENV_USER_DEL:  delete_variable(hUserList, &user_dirty); return TRUE;
        case IDC_ENV_SYS_NEW:   edit_variable(hwnd, hSysList, -1, &sys_dirty); return TRUE;
        case IDC_ENV_SYS_EDIT:
        {
            int i = (int)SendMessageW(hSysList, LVM_GETNEXTITEM, (WPARAM)-1, LVNI_SELECTED);
            if (i >= 0) edit_variable(hwnd, hSysList, i, &sys_dirty);
            return TRUE;
        }
        case IDC_ENV_SYS_DEL:   delete_variable(hSysList, &sys_dirty); return TRUE;
        case IDOK:
            apply_changes(hwnd);
            EndDialog(hwnd, IDOK);
            return TRUE;
        case IDCANCEL:
            EndDialog(hwnd, IDCANCEL);
            return TRUE;
        }
        break;
    }

    case WM_NOTIFY:
    {
        NMHDR *nm = (NMHDR *)lparam;
        if (nm->code == NM_DBLCLK && (nm->idFrom == IDC_ENV_USER_LIST || nm->idFrom == IDC_ENV_SYS_LIST))
        {
            BOOL user = nm->idFrom == IDC_ENV_USER_LIST;
            HWND list = nm->hwndFrom;
            int i = (int)SendMessageW(list, LVM_GETNEXTITEM, (WPARAM)-1, LVNI_SELECTED);
            if (i >= 0) edit_variable(hwnd, list, i, user ? &user_dirty : &sys_dirty);
            return TRUE;
        }
        break;
    }
    }
    return FALSE;
}

void show_envvars_dialog(HWND parent)
{
    DialogBoxW(g_hInstance, MAKEINTRESOURCEW(IDD_ENVVARS), parent, envvars_dlg_proc);
}

/*
 * Regional and Language Options - Advanced page
 *
 * Based on the ReactOS International Control Panel (dll/cpl/intl/advanced.c)
 * Copyright 2004 ReactOS Team
 * Copyright 2026 WineExplorer Desktop Environment Project
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#include "intl.h"

/* In Wine the language for non-Unicode programs (the system locale and its code pages) comes from the Unix locale
 * (LC_CTYPE) when Wine starts, and every code page conversion table is always there.  The page shows them as they
 * are, grayed, like Windows shows settings the user may not change. */

static HWND hLangList;

static BOOL CALLBACK LocalesEnumProc(WCHAR *locale)
{
    LCID lcid = wcstoul(locale, NULL, 16);
    WCHAR name[255];
    LRESULT index;

    if (!GetLocaleInfoW(lcid, LOCALE_SLANGUAGE, name, ARRAY_SIZE(name))) return TRUE;
    if (SendMessageW(hLangList, CB_FINDSTRINGEXACT, -1, (LPARAM)name) != CB_ERR) return TRUE;
    index = SendMessageW(hLangList, CB_ADDSTRING, 0, (LPARAM)name);
    SendMessageW(hLangList, CB_SETITEMDATA, index, lcid);
    return TRUE;
}

static void InitLanguagesList(HWND hwndDlg, PGLOBALDATA pGlobalData)
{
    WCHAR current[255];

    hLangList = GetDlgItem(hwndDlg, IDC_LANGUAGE_COMBO);
    EnumSystemLocalesW(LocalesEnumProc, LCID_SUPPORTED);
    GetLocaleInfoW(pGlobalData->SystemLCID, LOCALE_SLANGUAGE, current, ARRAY_SIZE(current));
    SendMessageW(hLangList, CB_SELECTSTRING, -1, (LPARAM)current);
}

static HWND hCodePageList;

static BOOL CALLBACK CodePagesEnumProc(WCHAR *code_page)
{
    CPINFOEXW info;
    LVITEMW item = { LVIF_TEXT };
    int index;

    if (!GetCPInfoExW(wcstoul(code_page, NULL, 10), 0, &info)) return TRUE;
    item.pszText = info.CodePageName;
    item.iItem = INT_MAX;
    if ((index = SendMessageW(hCodePageList, LVM_INSERTITEMW, 0, (LPARAM)&item)) >= 0)
    {
        item.state = INDEXTOSTATEIMAGEMASK(2);
        item.stateMask = LVIS_STATEIMAGEMASK;
        SendMessageW(hCodePageList, LVM_SETITEMSTATE, index, (LPARAM)&item);
    }
    return TRUE;
}

static void InitCodePagesList(HWND hwndDlg)
{
    LVCOLUMNW column = { LVCF_WIDTH };
    RECT rc;

    hCodePageList = GetDlgItem(hwndDlg, IDC_CONV_TABLES);
    GetClientRect(hCodePageList, &rc);
    column.cx = rc.right - GetSystemMetrics(SM_CXVSCROLL);
    SendMessageW(hCodePageList, LVM_INSERTCOLUMNW, 0, (LPARAM)&column);
    SendMessageW(hCodePageList, LVM_SETEXTENDEDLISTVIEWSTYLE, 0, LVS_EX_CHECKBOXES | LVS_EX_FULLROWSELECT);
    EnumSystemCodePagesW(CodePagesEnumProc, CP_SUPPORTED);
}

/* Property page dialog callback */
INT_PTR CALLBACK
AdvancedPageProc(HWND hwndDlg, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    if (uMsg == WM_INITDIALOG)
    {
        PGLOBALDATA pGlobalData = (PGLOBALDATA)((LPPROPSHEETPAGEW)lParam)->lParam;

        InitLanguagesList(hwndDlg, pGlobalData);
        InitCodePagesList(hwndDlg);
        EnableWindow(GetDlgItem(hwndDlg, IDC_LANGUAGE_COMBO), FALSE);
        EnableWindow(GetDlgItem(hwndDlg, IDC_CONV_TABLES), FALSE);
        EnableWindow(GetDlgItem(hwndDlg, IDC_APPLY_CUR_USER_DEF_PROFILE), FALSE);
    }
    return FALSE;
}

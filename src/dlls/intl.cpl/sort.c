/*
 * PROJECT:         ReactOS International Control Panel
 * FILE:            dll/cpl/intl/sort.c
 * PURPOSE:         Sorting property page
 * PROGRAMMER:      Eric Kohl
 *
 * Copyright 2026 WineExplorer Desktop Environment Project
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
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

static BOOL bSortPage = FALSE;
static LCID userLcid;

static HWND hWndSortList = NULL;


static BOOL CALLBACK
SortTestEnumProc(PWSTR lpLocale)
{
    LCID lcid;

    lcid = wcstoul(lpLocale, NULL, 16);

    if ((LANGIDFROMLCID(lcid) == LANGIDFROMLCID(userLcid)) &&
        (SORTIDFROMLCID(lcid) != SORTIDFROMLCID(userLcid)))
        bSortPage = TRUE;

    return TRUE;
}


BOOL
IsSortPageNeeded(LCID lcid)
{
    /* Handle special case for Spanish (Spain) */
    if (lcid == MAKELCID(MAKELANGID(LANG_SPANISH, SUBLANG_SPANISH), SORT_DEFAULT) ||
        lcid == MAKELCID(MAKELANGID(LANG_SPANISH, SUBLANG_SPANISH_MODERN), SORT_DEFAULT))
        return TRUE;

    userLcid = lcid;
    bSortPage = FALSE;

    EnumSystemLocalesW(SortTestEnumProc, LCID_ALTERNATE_SORTS);

    return bSortPage;
}


static BOOL CALLBACK
SortEnumProc(LPWSTR lpLocale)
{
    LCID lcid;
    WCHAR lang[255];
    INT index;

    lcid = wcstoul(lpLocale, NULL, 16);

    if ((LANGIDFROMLCID(lcid) == LANGIDFROMLCID(userLcid)) &&
        (SORTIDFROMLCID(lcid) != SORTIDFROMLCID(userLcid)))
    {
        GetLocaleInfoW(lcid, LOCALE_SSORTNAME, lang, sizeof(lang)/sizeof(WCHAR));

        index = SendMessageW(hWndSortList,
                             CB_ADDSTRING,
                             0,
                             (LPARAM)lang);

        SendMessageW(hWndSortList,
                     CB_SETITEMDATA,
                     index,
                     (LPARAM)lcid);
    }

    return TRUE;
}

static VOID
CreateSortList(HWND hwnd, LCID lcid)
{
    WCHAR lang[255];
    INT index;

    hWndSortList = hwnd;

    /* Handle special case for Spanish (Spain) */
    if (lcid == MAKELCID(MAKELANGID(LANG_SPANISH, SUBLANG_SPANISH), SORT_DEFAULT) ||
        lcid == MAKELCID(MAKELANGID(LANG_SPANISH, SUBLANG_SPANISH_MODERN), SORT_DEFAULT))
    {
        /* Add traditional sorting */
        GetLocaleInfoW(MAKELANGID(LANG_SPANISH, SUBLANG_SPANISH),
                       LOCALE_SSORTNAME, lang, sizeof(lang)/sizeof(WCHAR));

        index = SendMessageW(hwnd,
                             CB_ADDSTRING,
                             0,
                             (LPARAM)lang);

        SendMessageW(hwnd,
                     CB_SETITEMDATA,
                     index,
                     (LPARAM)MAKELANGID(LANG_SPANISH, SUBLANG_SPANISH));

        /* Add modern sorting */
        GetLocaleInfoW(MAKELANGID(LANG_SPANISH, SUBLANG_SPANISH_MODERN),
                       LOCALE_SSORTNAME, lang, sizeof(lang)/sizeof(WCHAR));

        index = SendMessageW(hwnd,
                             CB_ADDSTRING,
                             0,
                             (LPARAM)lang);

        SendMessageW(hwnd,
                     CB_SETITEMDATA,
                     index,
                     (LPARAM)MAKELANGID(LANG_SPANISH, SUBLANG_SPANISH_MODERN));
    }
    else
    {
        userLcid = lcid;

        GetLocaleInfoW(lcid & 0xFFFF, LOCALE_SSORTNAME, lang, sizeof(lang)/sizeof(WCHAR));

        index = SendMessageW(hWndSortList,
                             CB_ADDSTRING,
                             0,
                             (LPARAM)lang);

        SendMessageW(hWndSortList,
                     CB_SETITEMDATA,
                     index,
                     (LPARAM)lcid & 0xFFFF);

        EnumSystemLocalesW(SortEnumProc, LCID_ALTERNATE_SORTS);
    }

    /* Select current locale */
    /* or should it be System and not user? */
    GetLocaleInfoW(lcid, LOCALE_SSORTNAME, lang, sizeof(lang)/sizeof(WCHAR));

    SendMessageW(hwnd,
                 CB_SELECTSTRING,
                 -1,
                 (LPARAM)lang);
}

/* Property page dialog callback */
INT_PTR CALLBACK
SortPageProc(HWND hwndDlg,
             UINT uMsg,
             WPARAM wParam,
             LPARAM lParam)
{
    PGLOBALDATA pGlobalData;

    pGlobalData = (PGLOBALDATA)GetWindowLongPtrW(hwndDlg, DWLP_USER);

    switch (uMsg)
    {
        case WM_INITDIALOG:
            pGlobalData = (PGLOBALDATA)((LPPROPSHEETPAGEW)lParam)->lParam;
            SetWindowLongPtrW(hwndDlg, DWLP_USER, (LONG_PTR)pGlobalData);

            CreateSortList(GetDlgItem(hwndDlg, IDC_SORTLIST_COMBO), pGlobalData->UserLCID);
            break;

        case WM_COMMAND:
            switch (LOWORD(wParam))
            {
                case IDC_SORTLIST_COMBO:
                    if (HIWORD(wParam) == CBN_SELCHANGE)
                    {
                        SendMessageW(GetParent(hwndDlg), PSM_CHANGED, (WPARAM)hwndDlg, 0);
                    }
                    break;
            }
            break;

        case WM_NOTIFY:
            if (((LPNMHDR)lParam)->code == (UINT)PSN_APPLY)
            {
                LCID NewLcid;
                INT iCurSel;

                iCurSel = SendDlgItemMessageW(hwndDlg,
                                             IDC_SORTLIST_COMBO,
                                             CB_GETCURSEL,
                                             0,
                                             0);
                if (iCurSel == CB_ERR)
                    break;

                NewLcid = SendDlgItemMessageW(hwndDlg,
                                             IDC_SORTLIST_COMBO,
                                             CB_GETITEMDATA,
                                             iCurSel,
                                             0);
                if (NewLcid == (LCID)CB_ERR)
                    break;

                /* Save the new LCID */
                pGlobalData->UserLCID = NewLcid;
                pGlobalData->bUserLocaleChanged = TRUE;
            }
            break;
    }

  return FALSE;
}

/* EOF */

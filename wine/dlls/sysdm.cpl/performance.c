/*
 * WineExplorer Desktop Environment - Performance Options Dialog (sysdm.cpl)
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

#include "sysdm.h"

static const WCHAR *g_visual_fx[] = {
    L"Use drop shadows for icon labels on the desktop",
    L"Show shadows under mouse pointers",
    L"Show shadows under menus",
    L"Show window contents while dragging",
    L"Smooth edges of screen fonts",
    L"Use visual styles on windows and buttons",
    L"Slide taskbar buttons",
    L"Fade or slide menus into view",
    L"Animate windows when minimizing and maximizing"
};

static void set_listview_check_state(HWND hList, int index, BOOL check)
{
    LVITEMW lvi;
    memset(&lvi, 0, sizeof(lvi));
    lvi.state = INDEXTOSTATEIMAGEMASK(check ? 2 : 1);
    lvi.stateMask = LVIS_STATEIMAGEMASK;
    SendMessageW(hList, LVM_SETITEMSTATE, (WPARAM)index, (LPARAM)&lvi);
}

static INT_PTR CALLBACK perf_dlg_proc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam)
{
    switch (msg)
    {
    case WM_INITDIALOG:
    {
        HWND hList = GetDlgItem(hwnd, IDC_PERF_TREE);
        LVCOLUMNW col;
        LVITEMW item;
        int i;

        SendMessageW(hList, LVM_SETEXTENDEDLISTVIEWSTYLE, 0, LVS_EX_CHECKBOXES | LVS_EX_FULLROWSELECT);

        memset(&col, 0, sizeof(col));
        col.mask = LVCF_WIDTH;
        col.cx = 320;
        SendMessageW(hList, LVM_INSERTCOLUMNW, 0, (LPARAM)&col);

        for (i = 0; i < ARRAY_SIZE(g_visual_fx); i++)
        {
            memset(&item, 0, sizeof(item));
            item.mask = LVIF_TEXT;
            item.iItem = i;
            item.pszText = (LPWSTR)g_visual_fx[i];
            SendMessageW(hList, LVM_INSERTITEMW, 0, (LPARAM)&item);
            set_listview_check_state(hList, i, TRUE);
        }

        CheckRadioButton(hwnd, IDC_PERF_RADIO_AUTO, IDC_PERF_RADIO_CUSTOM, IDC_PERF_RADIO_AUTO);
        return TRUE;
    }

    case WM_COMMAND:
    {
        HWND hList = GetDlgItem(hwnd, IDC_PERF_TREE);
        int i;

        switch (LOWORD(wparam))
        {
        case IDC_PERF_RADIO_BESTAPP:
            for (i = 0; i < ARRAY_SIZE(g_visual_fx); i++)
                set_listview_check_state(hList, i, TRUE);
            break;

        case IDC_PERF_RADIO_BESTPERF:
            for (i = 0; i < ARRAY_SIZE(g_visual_fx); i++)
                set_listview_check_state(hList, i, FALSE);
            break;

        case IDOK:
        {
            /* Apply visual effects settings */
            BOOL drag_full = ((((UINT)SendMessageW(hList, LVM_GETITEMSTATE, 3, LVIS_STATEIMAGEMASK)) >> 12) - 1);
            BOOL font_smooth = ((((UINT)SendMessageW(hList, LVM_GETITEMSTATE, 4, LVIS_STATEIMAGEMASK)) >> 12) - 1);

            SystemParametersInfoW(SPI_SETDRAGFULLWINDOWS, drag_full, 0, SPIF_UPDATEINIFILE | SPIF_SENDCHANGE);
            SystemParametersInfoW(SPI_SETFONTSMOOTHING, font_smooth, 0, SPIF_UPDATEINIFILE | SPIF_SENDCHANGE);

            EndDialog(hwnd, IDOK);
            return TRUE;
        }

        case IDCANCEL:
            EndDialog(hwnd, IDCANCEL);
            return TRUE;
        }
        break;
    }
    }
    return FALSE;
}

void show_performance_dialog(HWND parent)
{
    DialogBoxW(g_hInstance, MAKEINTRESOURCEW(IDD_PERFORMANCE), parent, perf_dlg_proc);
}

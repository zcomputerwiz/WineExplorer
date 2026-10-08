/*
 * WineExplorer Desktop Environment - Display Properties Themes Page
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
#include <shlwapi.h>
#include <stdio.h>

#include "resource.h"
#include "preview.h"

typedef struct _THEME_ENTRY
{
    WCHAR name[64];
    WCHAR path[MAX_PATH];
    COLORREF desktop_color;
    COLORREF active_caption;
    COLORREF inactive_caption;
} THEME_ENTRY;

static THEME_ENTRY g_themes[] =
{
    { L"Windows Classic", L"", RGB(58, 110, 165), RGB(10, 36, 106), RGB(128, 128, 128) },
    { L"Windows XP (Luna)", L"", RGB(0, 78, 152), RGB(0, 85, 230), RGB(122, 150, 225) },
    { L"Windows Server 2003", L"", RGB(58, 110, 165), RGB(10, 36, 106), RGB(128, 128, 128) },
    { L"High Contrast", L"", RGB(0, 0, 0), RGB(0, 0, 128), RGB(64, 64, 64) }
};

#define NUM_THEMES (sizeof(g_themes) / sizeof(g_themes[0]))

static void update_theme_preview(HWND hwnd, int sel)
{
    PREVIEW_DATA pd;
    HWND hPreview = GetDlgItem(hwnd, IDC_THEME_PREVIEW);

    if (sel < 0 || sel >= NUM_THEMES) sel = 0;

    pd.cr_desktop = g_themes[sel].desktop_color;
    pd.cr_active_caption = g_themes[sel].active_caption;
    pd.cr_active_text = RGB(255, 255, 255);
    pd.cr_inactive_caption = g_themes[sel].inactive_caption;
    pd.cr_inactive_text = RGB(212, 208, 200);
    pd.cr_btn_face = RGB(212, 208, 200);
    pd.cr_btn_text = RGB(0, 0, 0);
    pd.cr_window = RGB(255, 255, 255);
    pd.cr_window_text = RGB(0, 0, 0);
    pd.cr_menu = RGB(212, 208, 200);
    pd.cr_menu_text = RGB(0, 0, 0);

    preview_update(hPreview, &pd);
}

INT_PTR CALLBACK theme_page_proc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam)
{
    switch (msg)
    {
    case WM_INITDIALOG:
    {
        HWND hCombo = GetDlgItem(hwnd, IDC_THEME_LIST);
        unsigned int i;

        for (i = 0; i < NUM_THEMES; i++)
            SendMessageW(hCombo, CB_ADDSTRING, 0, (LPARAM)g_themes[i].name);

        SendMessageW(hCombo, CB_SETCURSEL, 0, 0);
        update_theme_preview(hwnd, 0);
        return TRUE;
    }

    case WM_COMMAND:
        if (LOWORD(wparam) == IDC_THEME_LIST && HIWORD(wparam) == CBN_SELCHANGE)
        {
            int sel = SendMessageW((HWND)lparam, CB_GETCURSEL, 0, 0);
            update_theme_preview(hwnd, sel);
            SendMessageW(GetParent(hwnd), PSM_CHANGED, (WPARAM)hwnd, 0);
        }
        break;

    case WM_NOTIFY:
    {
        NMHDR *nmhdr = (NMHDR *)lparam;
        if (nmhdr->code == PSN_APPLY)
        {
            HWND hCombo = GetDlgItem(hwnd, IDC_THEME_LIST);
            int sel = SendMessageW(hCombo, CB_GETCURSEL, 0, 0);
            if (sel >= 0 && sel < NUM_THEMES)
            {
                int indices[] = { COLOR_BACKGROUND, COLOR_ACTIVECAPTION, COLOR_INACTIVECAPTION };
                COLORREF colors[] = { g_themes[sel].desktop_color, g_themes[sel].active_caption, g_themes[sel].inactive_caption };
                SetSysColors(3, indices, colors);
                PostMessageW(HWND_BROADCAST, WM_SYSCOLORCHANGE, 0, 0);
            }
            return TRUE;
        }
        break;
    }
    }
    return FALSE;
}

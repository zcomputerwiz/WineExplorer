/*
 * WineExplorer Desktop Environment - Display Properties Desktop (Background) Page
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
#include <commdlg.h>
#include <shlwapi.h>
#include <stdio.h>

#include "resource.h"
#include "preview.h"

static COLORREF g_desktop_color;
static WCHAR g_current_wallpaper[MAX_PATH];
static int g_wallpaper_style = 2; /* Stretch */

static const WCHAR *position_names[] = {
    L"Center",
    L"Tile",
    L"Stretch",
    L"Fit",
    L"Fill"
};

static INT_PTR CALLBACK custdesktop_dlg_proc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam)
{
    switch (msg)
    {
    case WM_INITDIALOG:
    {
        /* Check current desktop icon settings */
        CheckDlgButton(hwnd, IDC_CUST_MYDOCS, BST_CHECKED);
        CheckDlgButton(hwnd, IDC_CUST_MYCOMP, BST_CHECKED);
        CheckDlgButton(hwnd, IDC_CUST_MYNET, BST_CHECKED);
        CheckDlgButton(hwnd, IDC_CUST_RECYCLE, BST_CHECKED);
        return TRUE;
    }

    case WM_COMMAND:
        if (LOWORD(wparam) == IDOK || LOWORD(wparam) == IDCANCEL)
        {
            EndDialog(hwnd, LOWORD(wparam));
            return TRUE;
        }
        break;
    }
    return FALSE;
}

static void update_desktop_preview(HWND hwnd)
{
    PREVIEW_DATA pd;
    HWND hPreview = GetDlgItem(hwnd, IDC_BG_PREVIEW);

    pd.cr_desktop = g_desktop_color;
    pd.cr_active_caption = GetSysColor(COLOR_ACTIVECAPTION);
    pd.cr_active_text = GetSysColor(COLOR_CAPTIONTEXT);
    pd.cr_inactive_caption = GetSysColor(COLOR_INACTIVECAPTION);
    pd.cr_inactive_text = GetSysColor(COLOR_INACTIVECAPTIONTEXT);
    pd.cr_btn_face = GetSysColor(COLOR_BTNFACE);
    pd.cr_btn_text = GetSysColor(COLOR_BTNTEXT);
    pd.cr_window = GetSysColor(COLOR_WINDOW);
    pd.cr_window_text = GetSysColor(COLOR_WINDOWTEXT);
    pd.cr_menu = GetSysColor(COLOR_MENU);
    pd.cr_menu_text = GetSysColor(COLOR_MENUTEXT);

    preview_update(hPreview, &pd);
}

static void populate_wallpapers(HWND hwnd)
{
    HWND hList = GetDlgItem(hwnd, IDC_BG_LIST);
    WCHAR windir[MAX_PATH];
    WCHAR search_path[MAX_PATH];
    WIN32_FIND_DATAW fd;
    HANDLE hFind;

    SendMessageW(hList, LB_RESETCONTENT, 0, 0);
    SendMessageW(hList, LB_ADDSTRING, 0, (LPARAM)L"(None)");

    GetWindowsDirectoryW(windir, MAX_PATH);

    /* 1. Search %WinDir%\Web\Wallpaper\*.bmp;*.jpg */
    swprintf(search_path, MAX_PATH, L"%s\\Web\\Wallpaper\\*.*", windir);
    hFind = FindFirstFileW(search_path, &fd);
    if (hFind != INVALID_HANDLE_VALUE)
    {
        do
        {
            if (PathMatchSpecW(fd.cFileName, L"*.bmp") || PathMatchSpecW(fd.cFileName, L"*.jpg") || PathMatchSpecW(fd.cFileName, L"*.png"))
            {
                WCHAR title[MAX_PATH];
                WCHAR *dot;
                lstrcpynW(title, fd.cFileName, MAX_PATH);
                dot = wcsrchr(title, L'.');
                if (dot) *dot = L'\0';
                SendMessageW(hList, LB_ADDSTRING, 0, (LPARAM)title);
            }
        } while (FindNextFileW(hFind, &fd));
        FindClose(hFind);
    }

    /* 2. Search %WinDir%\*.bmp */
    swprintf(search_path, MAX_PATH, L"%s\\*.bmp", windir);
    hFind = FindFirstFileW(search_path, &fd);
    if (hFind != INVALID_HANDLE_VALUE)
    {
        do
        {
            WCHAR title[MAX_PATH];
            WCHAR *dot;
            lstrcpynW(title, fd.cFileName, MAX_PATH);
            dot = wcsrchr(title, L'.');
            if (dot) *dot = L'\0';
            SendMessageW(hList, LB_ADDSTRING, 0, (LPARAM)title);
        } while (FindNextFileW(hFind, &fd));
        FindClose(hFind);
    }

    SendMessageW(hList, LB_SETCURSEL, 0, 0);
}

INT_PTR CALLBACK desktop_page_proc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam)
{
    switch (msg)
    {
    case WM_INITDIALOG:
    {
        HWND hPos = GetDlgItem(hwnd, IDC_BG_POSITION);
        unsigned int i;

        g_desktop_color = GetSysColor(COLOR_BACKGROUND);

        for (i = 0; i < 5; i++)
            SendMessageW(hPos, CB_ADDSTRING, 0, (LPARAM)position_names[i]);
        SendMessageW(hPos, CB_SETCURSEL, 2, 0); /* Default: Stretch */

        populate_wallpapers(hwnd);
        update_desktop_preview(hwnd);
        return TRUE;
    }

    case WM_COMMAND:
        switch (LOWORD(wparam))
        {
        case IDC_BG_LIST:
            if (HIWORD(wparam) == LBN_SELCHANGE)
            {
                SendMessageW(GetParent(hwnd), PSM_CHANGED, (WPARAM)hwnd, 0);
            }
            break;

        case IDC_BG_POSITION:
            if (HIWORD(wparam) == CBN_SELCHANGE)
            {
                g_wallpaper_style = SendMessageW((HWND)lparam, CB_GETCURSEL, 0, 0);
                SendMessageW(GetParent(hwnd), PSM_CHANGED, (WPARAM)hwnd, 0);
            }
            break;

        case IDC_BG_COLOR:
        {
            CHOOSECOLORW cc;
            static COLORREF custom_colors[16];
            memset(&cc, 0, sizeof(cc));
            cc.lStructSize = sizeof(cc);
            cc.hwndOwner = hwnd;
            cc.rgbResult = g_desktop_color;
            cc.lpCustColors = custom_colors;
            cc.Flags = CC_RGBINIT | CC_FULLOPEN;

            if (ChooseColorW(&cc))
            {
                g_desktop_color = cc.rgbResult;
                update_desktop_preview(hwnd);
                SendMessageW(GetParent(hwnd), PSM_CHANGED, (WPARAM)hwnd, 0);
            }
            break;
        }

        case IDC_BG_BROWSE:
        {
            OPENFILENAMEW ofn;
            WCHAR file_buf[MAX_PATH] = L"";
            memset(&ofn, 0, sizeof(ofn));
            ofn.lStructSize = sizeof(ofn);
            ofn.hwndOwner = hwnd;
            ofn.lpstrFilter = L"Pictures (*.bmp;*.jpg;*.png)\0*.bmp;*.jpg;*.png\0All Files (*.*)\0*.*\0";
            ofn.lpstrFile = file_buf;
            ofn.nMaxFile = MAX_PATH;
            ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;

            if (GetOpenFileNameW(&ofn))
            {
                HWND hList = GetDlgItem(hwnd, IDC_BG_LIST);
                int idx = SendMessageW(hList, LB_ADDSTRING, 0, (LPARAM)PathFindFileNameW(file_buf));
                SendMessageW(hList, LB_SETCURSEL, idx, 0);
                lstrcpynW(g_current_wallpaper, file_buf, MAX_PATH);
                SendMessageW(GetParent(hwnd), PSM_CHANGED, (WPARAM)hwnd, 0);
            }
            break;
        }

        case IDC_BG_CUSTDESK:
            DialogBoxW(GetModuleHandleW(NULL), MAKEINTRESOURCEW(IDD_CUSTDESKTOP), hwnd, custdesktop_dlg_proc);
            break;
        }
        break;

    case WM_NOTIFY:
    {
        NMHDR *nmhdr = (NMHDR *)lparam;
        if (nmhdr->code == PSN_APPLY)
        {
            int idx = COLOR_BACKGROUND;
            COLORREF clr = g_desktop_color;
            SetSysColors(1, &idx, &clr);

            if (g_current_wallpaper[0])
            {
                SystemParametersInfoW(SPI_SETDESKWALLPAPER, 0, (void*)g_current_wallpaper,
                                      SPIF_UPDATEINIFILE | SPIF_SENDCHANGE);
            }

            PostMessageW(HWND_BROADCAST, WM_SYSCOLORCHANGE, 0, 0);
            return TRUE;
        }
        break;
    }
    }
    return FALSE;
}

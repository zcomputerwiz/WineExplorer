/*
 * WineExplorer Desktop Environment - Taskbar and Start Menu Properties Implementation
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

#include "resource.h"
#include "taskbar_props.h"
#include "theme_engine.h"

static TASKBAR_SETTINGS g_settings = {
    .lock_taskbar = TRUE,
    .autohide = FALSE,
    .always_on_top = TRUE,
    .group_buttons = TRUE,
    .show_quick_launch = TRUE,
    .show_clock = TRUE,
    .hide_inactive_icons = FALSE,
    .start_menu_style = THEME_STARTMENU_TWOCOLUMN
};

TASKBAR_SETTINGS *taskbar_settings_get(void)
{
    return &g_settings;
}

void taskbar_settings_load(void)
{
    HKEY hKey;
    DWORD val, size;

    if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\Advanced",
                      0, KEY_READ, &hKey) == ERROR_SUCCESS)
    {
        size = sizeof(val);
        if (RegQueryValueExW(hKey, L"TaskbarSizeMove", NULL, NULL, (BYTE*)&val, &size) == ERROR_SUCCESS)
            g_settings.lock_taskbar = (val == 0);

        size = sizeof(val);
        if (RegQueryValueExW(hKey, L"TaskbarGlomming", NULL, NULL, (BYTE*)&val, &size) == ERROR_SUCCESS)
            g_settings.group_buttons = (val != 0);

        size = sizeof(val);
        if (RegQueryValueExW(hKey, L"Start_Style", NULL, NULL, (BYTE*)&val, &size) == ERROR_SUCCESS)
            g_settings.start_menu_style = val;
        else if (RegQueryValueExW(hKey, L"Start_ShowRun", NULL, NULL, (BYTE*)&val, &size) == ERROR_SUCCESS)
            g_settings.start_menu_style = val;

        RegCloseKey(hKey);
    }
}

void taskbar_settings_save(void)
{
    HKEY hKey;
    DWORD val;

    if (RegCreateKeyExW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\Advanced",
                        0, NULL, 0, KEY_WRITE, NULL, &hKey, NULL) == ERROR_SUCCESS)
    {
        val = g_settings.lock_taskbar ? 0 : 1;
        RegSetValueExW(hKey, L"TaskbarSizeMove", 0, REG_DWORD, (BYTE*)&val, sizeof(val));

        val = g_settings.group_buttons ? 1 : 0;
        RegSetValueExW(hKey, L"TaskbarGlomming", 0, REG_DWORD, (BYTE*)&val, sizeof(val));

        val = g_settings.start_menu_style;
        RegSetValueExW(hKey, L"Start_Style", 0, REG_DWORD, (BYTE*)&val, sizeof(val));
        RegSetValueExW(hKey, L"Start_ShowRun", 0, REG_DWORD, (BYTE*)&val, sizeof(val));

        RegCloseKey(hKey);
    }
}

static void draw_taskbar_preview(HDC hdc, const RECT *rc)
{
    RECT tb_rc = { rc->left, rc->bottom - 22, rc->right, rc->bottom };
    RECT desktop_rc = { rc->left, rc->top, rc->right, rc->bottom - 22 };
    RECT btn_rc = { tb_rc.left + 2, tb_rc.top + 2, tb_rc.left + 46, tb_rc.bottom - 2 };
    RECT task_rc = { tb_rc.left + 52, tb_rc.top + 2, tb_rc.left + 120, tb_rc.bottom - 2 };
    HFONT font, old_font;

    /* Desktop background */
    FillRect(hdc, &desktop_rc, GetSysColorBrush(COLOR_BACKGROUND));

    /* Taskbar surface */
    FillRect(hdc, &tb_rc, GetSysColorBrush(COLOR_BTNFACE));
    DrawEdge(hdc, &tb_rc, BDR_RAISEDINNER, BF_TOP);

    /* Start button */
    DrawEdge(hdc, &btn_rc, EDGE_RAISED, BF_RECT | BF_SOFT);
    font = GetStockObject(DEFAULT_GUI_FONT);
    old_font = SelectObject(hdc, font);
    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, GetSysColor(COLOR_BTNTEXT));
    DrawTextW(hdc, L"Start", -1, &btn_rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

    /* Task button */
    DrawEdge(hdc, &task_rc, EDGE_SUNKEN, BF_RECT | BF_SOFT);
    DrawTextW(hdc, L"Document", -1, &task_rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

    /* Sunken Notification Tray Area */
    if (g_settings.show_clock)
    {
        RECT tray_rc = { tb_rc.right - 56, tb_rc.top + 2, tb_rc.right - 2, tb_rc.bottom - 2 };
        DrawEdge(hdc, &tray_rc, EDGE_SUNKEN, BF_RECT);
        DrawTextW(hdc, L"12:00 PM", -1, &tray_rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    }

    SelectObject(hdc, old_font);
}

static void draw_start_preview(HDC hdc, const RECT *rc, int style)
{
    FillRect(hdc, rc, GetSysColorBrush(COLOR_BTNFACE));
    DrawEdge(hdc, (LPRECT)rc, EDGE_SUNKEN, BF_RECT);

    if (style == THEME_STARTMENU_CLASSIC)
    {
        /* Classic single column with vertical gradient stripe */
        RECT stripe_rc = { rc->left + 4, rc->top + 4, rc->left + 24, rc->bottom - 4 };
        RECT menu_rc = { rc->left + 26, rc->top + 4, rc->left + 140, rc->bottom - 4 };
        HBRUSH stripe_br = CreateSolidBrush(RGB(0, 0, 128));
        HBRUSH menu_br = CreateSolidBrush(RGB(255, 255, 255));
        HFONT font = GetStockObject(DEFAULT_GUI_FONT);
        HFONT old_font;

        FillRect(hdc, &stripe_rc, stripe_br);
        FillRect(hdc, &menu_rc, menu_br);
        DrawEdge(hdc, &menu_rc, EDGE_RAISED, BF_RECT);

        old_font = SelectObject(hdc, font);
        SetBkMode(hdc, TRANSPARENT);
        SetTextColor(hdc, RGB(0, 0, 0));
        TextOutW(hdc, menu_rc.left + 6, menu_rc.top + 8, L"Programs", 8);
        TextOutW(hdc, menu_rc.left + 6, menu_rc.top + 24, L"Documents", 9);
        TextOutW(hdc, menu_rc.left + 6, menu_rc.top + 40, L"Settings", 8);

        SelectObject(hdc, old_font);
        DeleteObject(stripe_br);
        DeleteObject(menu_br);
    }
    else if (style == THEME_STARTMENU_TWOCOLUMN)
    {
        /* Two-column layout */
        RECT header_rc = { rc->left + 4, rc->top + 4, rc->left + 160, rc->top + 20 };
        RECT left_rc = { rc->left + 4, rc->top + 20, rc->left + 80, rc->bottom - 4 };
        RECT right_rc = { rc->left + 80, rc->top + 20, rc->left + 160, rc->bottom - 4 };
        HBRUSH head_br = CreateSolidBrush(RGB(40, 100, 200));
        HBRUSH left_br = CreateSolidBrush(RGB(255, 255, 255));
        HBRUSH right_br = CreateSolidBrush(RGB(214, 230, 252));

        FillRect(hdc, &header_rc, head_br);
        FillRect(hdc, &left_rc, left_br);
        FillRect(hdc, &right_rc, right_br);

        DeleteObject(head_br);
        DeleteObject(left_br);
        DeleteObject(right_br);
    }
    else
    {
        /* Modern layout with search box at bottom */
        RECT left_rc = { rc->left + 4, rc->top + 4, rc->left + 90, rc->bottom - 22 };
        RECT right_rc = { rc->left + 90, rc->top + 4, rc->left + 160, rc->bottom - 22 };
        RECT search_rc = { rc->left + 6, rc->bottom - 20, rc->left + 158, rc->bottom - 6 };
        HBRUSH left_br = CreateSolidBrush(RGB(245, 245, 245));
        HBRUSH right_br = CreateSolidBrush(RGB(230, 230, 235));

        FillRect(hdc, &left_rc, left_br);
        FillRect(hdc, &right_rc, right_br);
        FillRect(hdc, &search_rc, GetStockObject(WHITE_BRUSH));
        DrawEdge(hdc, &search_rc, BDR_SUNKENOUTER, BF_RECT);

        DeleteObject(left_br);
        DeleteObject(right_br);
    }
}

static void center_window(HWND hwnd);

static INT_PTR CALLBACK taskbar_page_proc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam)
{
    switch (msg)
    {
    case WM_INITDIALOG:
        center_window(GetParent(hwnd));
        CheckDlgButton(hwnd, IDC_LOCKTASKBAR, g_settings.lock_taskbar ? BST_CHECKED : BST_UNCHECKED);
        CheckDlgButton(hwnd, IDC_AUTOHIDETASKBAR, g_settings.autohide ? BST_CHECKED : BST_UNCHECKED);
        CheckDlgButton(hwnd, IDC_ONTOPTASKBAR, g_settings.always_on_top ? BST_CHECKED : BST_UNCHECKED);
        CheckDlgButton(hwnd, IDC_GROUPTASKBAR, g_settings.group_buttons ? BST_CHECKED : BST_UNCHECKED);
        CheckDlgButton(hwnd, IDC_QUICKLAUNCH, g_settings.show_quick_launch ? BST_CHECKED : BST_UNCHECKED);
        CheckDlgButton(hwnd, IDC_SHOWCLOCK, g_settings.show_clock ? BST_CHECKED : BST_UNCHECKED);
        CheckDlgButton(hwnd, IDC_HIDEINACTIVEICONS, g_settings.hide_inactive_icons ? BST_CHECKED : BST_UNCHECKED);
        return TRUE;

    case WM_DRAWITEM:
    {
        DRAWITEMSTRUCT *dis = (DRAWITEMSTRUCT *)lparam;
        if (dis->CtlID == IDC_TASKBAR_PREVIEW)
        {
            draw_taskbar_preview(dis->hDC, &dis->rcItem);
            return TRUE;
        }
        break;
    }

    case WM_COMMAND:
        switch (LOWORD(wparam))
        {
        case IDC_LOCKTASKBAR:
        case IDC_AUTOHIDETASKBAR:
        case IDC_ONTOPTASKBAR:
        case IDC_GROUPTASKBAR:
        case IDC_QUICKLAUNCH:
        case IDC_HIDEINACTIVEICONS:
            SendMessageW(GetParent(hwnd), PSM_CHANGED, (WPARAM)hwnd, 0);
            break;
        case IDC_SHOWCLOCK:
            g_settings.show_clock = (IsDlgButtonChecked(hwnd, IDC_SHOWCLOCK) == BST_CHECKED);
            InvalidateRect(GetDlgItem(hwnd, IDC_TASKBAR_PREVIEW), NULL, TRUE);
            SendMessageW(GetParent(hwnd), PSM_CHANGED, (WPARAM)hwnd, 0);
            break;
        }
        return TRUE;

    case WM_NOTIFY:
    {
        NMHDR *nmhdr = (NMHDR *)lparam;
        if (nmhdr->code == PSN_APPLY)
        {
            g_settings.lock_taskbar = (IsDlgButtonChecked(hwnd, IDC_LOCKTASKBAR) == BST_CHECKED);
            g_settings.autohide = (IsDlgButtonChecked(hwnd, IDC_AUTOHIDETASKBAR) == BST_CHECKED);
            g_settings.always_on_top = (IsDlgButtonChecked(hwnd, IDC_ONTOPTASKBAR) == BST_CHECKED);
            g_settings.group_buttons = (IsDlgButtonChecked(hwnd, IDC_GROUPTASKBAR) == BST_CHECKED);
            g_settings.show_quick_launch = (IsDlgButtonChecked(hwnd, IDC_QUICKLAUNCH) == BST_CHECKED);
            g_settings.show_clock = (IsDlgButtonChecked(hwnd, IDC_SHOWCLOCK) == BST_CHECKED);
            g_settings.hide_inactive_icons = (IsDlgButtonChecked(hwnd, IDC_HIDEINACTIVEICONS) == BST_CHECKED);
            taskbar_settings_save();
            return TRUE;
        }
        break;
    }
    }
    return FALSE;
}

static INT_PTR CALLBACK startmenu_page_proc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam)
{
    static int s_preview_style = THEME_STARTMENU_CLASSIC;

    switch (msg)
    {
    case WM_INITDIALOG:
    {
        s_preview_style = g_settings.start_menu_style;

        CheckRadioButton(hwnd, IDC_START_CLASSIC, IDC_START_MODERN,
                         s_preview_style == THEME_STARTMENU_CLASSIC ? IDC_START_CLASSIC :
                         (s_preview_style == THEME_STARTMENU_TWOCOLUMN ? IDC_START_TWOCOLUMN : IDC_START_MODERN));
        return TRUE;
    }

    case WM_DRAWITEM:
    {
        DRAWITEMSTRUCT *dis = (DRAWITEMSTRUCT *)lparam;
        if (dis->CtlID == IDC_START_PREVIEW)
        {
            draw_start_preview(dis->hDC, &dis->rcItem, s_preview_style);
            return TRUE;
        }
        break;
    }

    case WM_COMMAND:
        switch (LOWORD(wparam))
        {
        case IDC_START_CLASSIC:
            s_preview_style = THEME_STARTMENU_CLASSIC;
            CheckRadioButton(hwnd, IDC_START_CLASSIC, IDC_START_MODERN, IDC_START_CLASSIC);
            InvalidateRect(GetDlgItem(hwnd, IDC_START_PREVIEW), NULL, TRUE);
            SendMessageW(GetParent(hwnd), PSM_CHANGED, (WPARAM)hwnd, 0);
            break;
        case IDC_START_TWOCOLUMN:
            s_preview_style = THEME_STARTMENU_TWOCOLUMN;
            CheckRadioButton(hwnd, IDC_START_CLASSIC, IDC_START_MODERN, IDC_START_TWOCOLUMN);
            InvalidateRect(GetDlgItem(hwnd, IDC_START_PREVIEW), NULL, TRUE);
            SendMessageW(GetParent(hwnd), PSM_CHANGED, (WPARAM)hwnd, 0);
            break;
        case IDC_START_MODERN:
            s_preview_style = THEME_STARTMENU_MODERN;
            CheckRadioButton(hwnd, IDC_START_CLASSIC, IDC_START_MODERN, IDC_START_MODERN);
            InvalidateRect(GetDlgItem(hwnd, IDC_START_PREVIEW), NULL, TRUE);
            SendMessageW(GetParent(hwnd), PSM_CHANGED, (WPARAM)hwnd, 0);
            break;
        }
        return TRUE;

    case WM_NOTIFY:
    {
        NMHDR *nmhdr = (NMHDR *)lparam;
        if (nmhdr->code == PSN_APPLY)
        {
            THEME_CONFIG *theme = theme_get_current();
            HWND hTray = FindWindowW(L"Shell_TrayWnd", NULL);
            g_settings.start_menu_style = s_preview_style;
            if (theme) theme->start_menu_style = s_preview_style;
            taskbar_settings_save();
            if (hTray) PostMessageW(hTray, WM_SETTINGCHANGE, 0, 0);
            return TRUE;
        }
        break;
    }
    }
    return FALSE;
}

static void center_window(HWND hwnd)
{
    RECT rc, wa;
    int sw = GetSystemMetrics(SM_CXSCREEN);
    int sh = GetSystemMetrics(SM_CYSCREEN);
    int x, y, w, h;

    GetWindowRect(hwnd, &rc);
    w = rc.right - rc.left;
    h = rc.bottom - rc.top;

    if (!SystemParametersInfoW(SPI_GETWORKAREA, 0, &wa, 0) || wa.right <= wa.left || wa.bottom <= wa.top)
    {
        wa.left = 0;
        wa.top = 0;
        wa.right = sw;
        wa.bottom = sh - 30;
    }

    x = wa.left + (wa.right - wa.left - w) / 2;
    y = wa.top + (wa.bottom - wa.top - h) / 2;
    if (x < 0) x = 0;
    if (y < 0) y = 0;
    if (y + h > sh - 30) y = max(0, sh - 30 - h - 10);
    SetWindowPos(hwnd, NULL, x, y, 0, 0, SWP_NOSIZE | SWP_NOZORDER);
}

static int CALLBACK prop_sheet_callback(HWND hwnd, UINT msg, LPARAM lparam)
{
    if (msg == PSCB_INITIALIZED)
    {
        center_window(hwnd);
    }
    return 0;
}

void taskbar_props_show(HWND parent)
{
    INITCOMMONCONTROLSEX init = {
        .dwSize = sizeof(INITCOMMONCONTROLSEX),
        .dwICC = ICC_TAB_CLASSES
    };
    PROPSHEETPAGEW pages[2];
    PROPSHEETHEADERW psh;
    HINSTANCE hInst = GetModuleHandleW(NULL);

    InitCommonControlsEx(&init);
    taskbar_settings_load();

    memset(pages, 0, sizeof(pages));

    /* Page 1: Taskbar */
    pages[0].dwSize = sizeof(PROPSHEETPAGEW);
    pages[0].dwFlags = PSP_DEFAULT;
    pages[0].hInstance = hInst;
    pages[0].pszTemplate = MAKEINTRESOURCEW(IDD_TASKBARPROP);
    pages[0].pfnDlgProc = taskbar_page_proc;

    /* Page 2: Start Menu */
    pages[1].dwSize = sizeof(PROPSHEETPAGEW);
    pages[1].dwFlags = PSP_DEFAULT;
    pages[1].hInstance = hInst;
    pages[1].pszTemplate = MAKEINTRESOURCEW(IDD_STARTMENUPROP);
    pages[1].pfnDlgProc = startmenu_page_proc;

    memset(&psh, 0, sizeof(psh));
    psh.dwSize = sizeof(PROPSHEETHEADERW);
    psh.dwFlags = PSH_PROPSHEETPAGE | PSH_USECALLBACK;
    psh.pfnCallback = prop_sheet_callback;
    psh.hwndParent = parent;
    psh.hInstance = hInst;
    psh.pszCaption = L"Taskbar and Start Menu Properties";
    psh.nPages = ARRAY_SIZE(pages);
    psh.ppsp = pages;

    PropertySheetW(&psh);
}

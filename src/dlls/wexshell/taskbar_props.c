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
#include "startprops.h"
#include "notifyprops.h"
#include "shellres.h"
#include "wextheme.h"
#include "module.h"
#include "wexres.h"
#include "startskin.h"


static TASKBAR_SETTINGS g_settings = {
    .lock_taskbar = TRUE,
    .autohide = FALSE,
    .always_on_top = TRUE,
    .group_buttons = TRUE,
    .show_quick_launch = TRUE,
    .show_address = FALSE,
    .show_links = FALSE,
    .show_desktop_toolbar = FALSE,
    .show_search = FALSE,
    .show_clock = TRUE,
    .hide_inactive_icons = FALSE,
    .start_menu_style = THEME_STARTMENU_TWOCOLUMN
};

TASKBAR_SETTINGS *taskbar_settings_get(void)
{
    return &g_settings;
}

static const WCHAR taskbar_key[] = L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\Taskbar";

static const struct { const WCHAR *name; size_t offset; } flag_values[] =
{
    { L"AutoHide",         offsetof(TASKBAR_SETTINGS, autohide) },
    { L"AlwaysOnTop",      offsetof(TASKBAR_SETTINGS, always_on_top) },
    { L"ShowClock",        offsetof(TASKBAR_SETTINGS, show_clock) },
    { L"HideInactiveIcons", offsetof(TASKBAR_SETTINGS, hide_inactive_icons) },
    { L"QuickLaunch",      offsetof(TASKBAR_SETTINGS, show_quick_launch) },
    { L"AddressToolbar",   offsetof(TASKBAR_SETTINGS, show_address) },
    { L"LinksToolbar",     offsetof(TASKBAR_SETTINGS, show_links) },
    { L"DesktopToolbar",   offsetof(TASKBAR_SETTINGS, show_desktop_toolbar) },
    { L"SearchToolbar",    offsetof(TASKBAR_SETTINGS, show_search) },
};

void taskbar_settings_load(void)
{
    HKEY hKey;
    DWORD val, size;
    unsigned int i;

    if (RegOpenKeyExW(HKEY_CURRENT_USER, taskbar_key, 0, KEY_READ, &hKey) == ERROR_SUCCESS)
    {
        for (i = 0; i < ARRAY_SIZE(flag_values); i++)
        {
            size = sizeof(val);
            if (RegQueryValueExW(hKey, flag_values[i].name, NULL, NULL, (BYTE *)&val, &size) == ERROR_SUCCESS)
                *(BOOL *)((char *)&g_settings + flag_values[i].offset) = val != 0;
        }
        RegCloseKey(hKey);
    }

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

        RegCloseKey(hKey);
    }
}

void taskbar_settings_save(void)
{
    HKEY hKey;
    DWORD val;
    unsigned int i;

    if (RegCreateKeyExW(HKEY_CURRENT_USER, taskbar_key, 0, NULL, 0, KEY_WRITE, NULL, &hKey, NULL) == ERROR_SUCCESS)
    {
        for (i = 0; i < ARRAY_SIZE(flag_values); i++)
        {
            val = *(BOOL *)((char *)&g_settings + flag_values[i].offset) ? 1 : 0;
            RegSetValueExW(hKey, flag_values[i].name, 0, REG_DWORD, (BYTE *)&val, sizeof(val));
        }
        RegCloseKey(hKey);
    }

    if (RegCreateKeyExW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\Advanced",
                        0, NULL, 0, KEY_WRITE, NULL, &hKey, NULL) == ERROR_SUCCESS)
    {
        val = g_settings.lock_taskbar ? 0 : 1;
        RegSetValueExW(hKey, L"TaskbarSizeMove", 0, REG_DWORD, (BYTE*)&val, sizeof(val));

        val = g_settings.group_buttons ? 1 : 0;
        RegSetValueExW(hKey, L"TaskbarGlomming", 0, REG_DWORD, (BYTE*)&val, sizeof(val));

        val = g_settings.start_menu_style;
        RegSetValueExW(hKey, L"Start_Style", 0, REG_DWORD, (BYTE*)&val, sizeof(val));

        RegCloseKey(hKey);
    }
}

static BOOL draw_shell_preview(HDC hdc, const RECT *rc, int id)
{
    HBITMAP bitmap = shellres_bitmap(L"explorer", id, 0), old;
    BITMAP bm;
    HDC mem;
    int x, y;

    if (!bitmap) return FALSE;
    GetObjectW(bitmap, sizeof(bm), &bm);
    mem = CreateCompatibleDC(hdc);
    old = SelectObject(mem, bitmap);
    FillRect(hdc, rc, GetSysColorBrush(COLOR_BTNFACE));
    x = rc->left + ((rc->right - rc->left) - bm.bmWidth) / 2;
    y = rc->top + ((rc->bottom - rc->top) - bm.bmHeight) / 2;
    BitBlt(hdc, x, y, bm.bmWidth, bm.bmHeight, mem, 0, 0, SRCCOPY);
    SelectObject(mem, old);
    DeleteDC(mem);
    DeleteObject(bitmap);
    return TRUE;
}

static void draw_taskbar_preview(HWND page, HDC hdc, const RECT *rc)
{
    RECT tb_rc = { rc->left, rc->bottom - 22, rc->right, rc->bottom };
    RECT desktop_rc = { rc->left, rc->top, rc->right, rc->bottom - 22 };
    RECT btn_rc = { tb_rc.left + 2, tb_rc.top + 2, tb_rc.left + 46, tb_rc.bottom - 2 };
    RECT task_rc = { tb_rc.left + 52, tb_rc.top + 2, tb_rc.left + 120, tb_rc.bottom - 2 };
    HFONT font, old_font;

    int bitmap = 146 + (IsDlgButtonChecked(page, IDC_LOCKTASKBAR) != BST_CHECKED) +
                 2 * (IsDlgButtonChecked(page, IDC_GROUPTASKBAR) != BST_CHECKED) +
                 4 * (IsDlgButtonChecked(page, IDC_QUICKLAUNCH) != BST_CHECKED);
    if (IsDlgButtonChecked(page, IDC_AUTOHIDETASKBAR) == BST_CHECKED) bitmap = 145;
    if (draw_shell_preview(hdc, rc, bitmap)) return;

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

static void draw_notify_preview(HWND page, HDC hdc, const RECT *rc, BOOL show_clock)
{
    RECT tray = { rc->right - 120, rc->top + 2, rc->right - 2, rc->bottom - 2 };
    HFONT old = SelectObject(hdc, GetStockObject(DEFAULT_GUI_FONT));
    HICON icon;

    int bitmap = 180 + !show_clock +
                 2 * (IsDlgButtonChecked(page, IDC_HIDEINACTIVEICONS) != BST_CHECKED);
    if (draw_shell_preview(hdc, rc, bitmap)) { SelectObject(hdc, old); return; }
    FillRect(hdc, rc, GetSysColorBrush(COLOR_BTNFACE));
    DrawEdge(hdc, &tray, EDGE_SUNKEN, BF_RECT);
    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, GetSysColor(COLOR_BTNTEXT));
    if (show_clock)
    {
        RECT clock = tray;
        clock.left = tray.right - 56;
        DrawTextW(hdc, L"12:00 PM", -1, &clock, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    }
    if ((icon = shellres_icon(15, 16)))
    {
        DrawIconEx(hdc, tray.left + 6, tray.top + (tray.bottom - tray.top - 16) / 2, icon, 16, 16, 0, NULL, DI_NORMAL);
        DestroyIcon(icon);
    }
    SelectObject(hdc, old);
}

static void draw_start_preview(HDC hdc, const RECT *rc, int style)
{
    if (draw_shell_preview(hdc, rc, style == THEME_STARTMENU_CLASSIC ? 171 : 170)) return;

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
        EnableWindow(GetDlgItem(hwnd, IDC_CUSTOMIZE), g_settings.hide_inactive_icons);
        return TRUE;

    case WM_DRAWITEM:
    {
        DRAWITEMSTRUCT *dis = (DRAWITEMSTRUCT *)lparam;
        if (dis->CtlID == IDC_TASKBAR_PREVIEW)
        {
            draw_taskbar_preview(hwnd, dis->hDC, &dis->rcItem);
            return TRUE;
        }
        if (dis->CtlID == IDC_NOTIFY_PREVIEW)
        {
            draw_notify_preview(hwnd, dis->hDC, &dis->rcItem, IsDlgButtonChecked(hwnd, IDC_SHOWCLOCK) == BST_CHECKED);
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
            InvalidateRect(GetDlgItem(hwnd, IDC_TASKBAR_PREVIEW), NULL, TRUE);
            InvalidateRect(GetDlgItem(hwnd, IDC_NOTIFY_PREVIEW), NULL, TRUE);
            EnableWindow(GetDlgItem(hwnd, IDC_CUSTOMIZE), IsDlgButtonChecked(hwnd, IDC_HIDEINACTIVEICONS) == BST_CHECKED);
            SendMessageW(GetParent(hwnd), PSM_CHANGED, (WPARAM)hwnd, 0);
            break;
        case IDC_CUSTOMIZE:
            notifyprops_show(hwnd);
            break;
        case IDC_SHOWCLOCK:
            InvalidateRect(GetDlgItem(hwnd, IDC_NOTIFY_PREVIEW), NULL, TRUE);
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
            { HWND tray = FindWindowW(L"Shell_TrayWnd", NULL); if (tray) PostMessageW(tray, WM_SETTINGCHANGE, 0, 0); }
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
        s_preview_style = g_settings.start_menu_style == THEME_STARTMENU_CLASSIC ? THEME_STARTMENU_CLASSIC : THEME_STARTMENU_TWOCOLUMN;
        CheckRadioButton(hwnd, IDC_NEWSCHOOL, IDC_OLDSCHOOL, s_preview_style == THEME_STARTMENU_CLASSIC ? IDC_OLDSCHOOL : IDC_NEWSCHOOL);
        EnableWindow(GetDlgItem(hwnd, IDC_NEWSTARTCUSTOMIZE), s_preview_style != THEME_STARTMENU_CLASSIC);
        EnableWindow(GetDlgItem(hwnd, IDC_OLDSTARTCUSTOMIZE), s_preview_style == THEME_STARTMENU_CLASSIC);
        return TRUE;

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
        case IDC_NEWSCHOOL:
        case IDC_OLDSCHOOL:
            s_preview_style = LOWORD(wparam) == IDC_OLDSCHOOL ? THEME_STARTMENU_CLASSIC : THEME_STARTMENU_TWOCOLUMN;
            CheckRadioButton(hwnd, IDC_NEWSCHOOL, IDC_OLDSCHOOL, LOWORD(wparam));
            EnableWindow(GetDlgItem(hwnd, IDC_NEWSTARTCUSTOMIZE), s_preview_style != THEME_STARTMENU_CLASSIC);
            EnableWindow(GetDlgItem(hwnd, IDC_OLDSTARTCUSTOMIZE), s_preview_style == THEME_STARTMENU_CLASSIC);
            InvalidateRect(GetDlgItem(hwnd, IDC_START_PREVIEW), NULL, TRUE);
            SendMessageW(GetParent(hwnd), PSM_CHANGED, (WPARAM)hwnd, 0);
            break;
        case IDC_NEWSTARTCUSTOMIZE:
            startprops_customize(hwnd);
            break;
        case IDC_OLDSTARTCUSTOMIZE:
            startprops_customize_classic(hwnd);
            break;
        case IDC_STARTSKINS_BTN:
            PropSheet_SetCurSel(GetParent(hwnd), NULL, 2);
            break;
        }
        return TRUE;

    case WM_NOTIFY:
    {
        NMHDR *nmhdr = (NMHDR *)lparam;
        if (nmhdr->code == PSN_APPLY)
        {
            HWND hTray = FindWindowW(L"Shell_TrayWnd", NULL);
            g_settings.start_menu_style = s_preview_style;
            theme_set_start_menu_style(s_preview_style);
            taskbar_settings_save();
            if (hTray) PostMessageW(hTray, WM_SETTINGCHANGE, 0, 0);
            return TRUE;
        }
        break;
    }
    }
    return FALSE;
}

static void draw_skin_preview_item(HDC hdc, const RECT *rc, const WCHAR *skin_name,
                                   const WCHAR *var_name, const WCHAR *opts)
{
    struct start_skin *skin;
    RECT box;
    HBRUSH bg;
    RECT left_rc, right_rc, header_rc, sel_rc;
    int mid_x;
    int hdr_h = 24;
    HFONT font, old_font;

    if (!hdc || !rc) return;
    box = *rc;

    FillRect(hdc, &box, GetSysColorBrush(COLOR_BTNFACE));
    DrawEdge(hdc, &box, EDGE_SUNKEN, BF_RECT);
    InflateRect(&box, -2, -2);

    if (!skin_name || !*skin_name || _wcsicmp(skin_name, L"None") == 0)
    {
        draw_start_preview(hdc, &box, g_settings.start_menu_style);
        return;
    }

    skin = startskin_load(skin_name, var_name, opts, STARTSKIN_TYPE_CLASSIC2, 96);
    if (!skin)
    {
        draw_start_preview(hdc, &box, g_settings.start_menu_style);
        return;
    }

    mid_x = box.left + (box.right - box.left) / 2;

    header_rc = box;
    header_rc.bottom = box.top + hdr_h;

    left_rc = box;
    left_rc.top = header_rc.bottom;
    left_rc.right = mid_x;

    right_rc = box;
    right_rc.top = header_rc.bottom;
    right_rc.left = mid_x;

    /* Header / Caption */
    if (skin->caption_bitmap.hbmp)
        startskin_draw_slices(hdc, &skin->caption_bitmap, &header_rc, &header_rc, NULL, TRUE);
    else
    {
        bg = CreateSolidBrush(skin->caption_background);
        FillRect(hdc, &header_rc, bg);
        DeleteObject(bg);
    }

    /* Avatar frame in preview */
    if (skin->user_bitmap.hbmp)
    {
        RECT av_rc;
        SetRect(&av_rc, header_rc.left + 4, header_rc.top + 2,
                header_rc.left + 4 + 20, header_rc.top + 2 + 20);
        startskin_draw_slices(hdc, &skin->user_bitmap, &av_rc, &av_rc, NULL, TRUE);
    }

    /* User name */
    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, skin->user_text_color);
    font = skin->user_font ? skin->user_font : GetStockObject(DEFAULT_GUI_FONT);
    old_font = SelectObject(hdc, font);
    {
        RECT txt_rc = header_rc;
        txt_rc.left += 28;
        DrawTextW(hdc, L"Wine User", -1, &txt_rc, DT_SINGLELINE | DT_VCENTER);
    }

    /* Left column */
    if (skin->main_bitmap.hbmp)
        startskin_draw_bg_slices(hdc, &skin->main_bitmap, &left_rc,
                                 skin->main_bitmap_slices_x, skin->main_bitmap_slices_y, 0, TRUE);
    else
    {
        bg = CreateSolidBrush(skin->main_background);
        FillRect(hdc, &left_rc, bg);
        DeleteObject(bg);
    }

    /* Right column */
    if (skin->main_bitmap.hbmp)
        startskin_draw_bg_slices(hdc, &skin->main_bitmap, &right_rc,
                                 skin->main_bitmap_slices_x, skin->main_bitmap_slices_y, 1, TRUE);
    else
    {
        bg = CreateSolidBrush(skin->main2_background);
        FillRect(hdc, &right_rc, bg);
        DeleteObject(bg);
    }

    /* Vertical separator */
    if (skin->main_separator_v.hbmp)
    {
        RECT sep_rc;
        sep_rc = left_rc;
        sep_rc.left = right_rc.left - 1;
        sep_rc.right = right_rc.left + 1;
        startskin_draw_slices(hdc, &skin->main_separator_v, &sep_rc, &sep_rc, NULL, TRUE);
    }

    /* Sample hot item selection in left column */
    SetRect(&sel_rc, left_rc.left + 2, left_rc.top + 4, left_rc.right - 2, left_rc.top + 22);
    if (skin->main_item.bmp_selection.hbmp)
        startskin_draw_selection(hdc, &skin->main_item.bmp_selection, &sel_rc,
                                 skin->main_item.sel_slices_x, skin->main_item.sel_slices_y, TRUE);
    else
    {
        bg = CreateSolidBrush(GetSysColor(COLOR_HIGHLIGHT));
        FillRect(hdc, &sel_rc, bg);
        DeleteObject(bg);
    }

    /* Sample item text */
    SetTextColor(hdc, skin->main_item.text_color[1]);
    DrawTextW(hdc, L"Internet", -1, &sel_rc, DT_SINGLELINE | DT_VCENTER);

    /* Normal item in left column */
    SetRect(&sel_rc, left_rc.left + 2, left_rc.top + 24, left_rc.right - 2, left_rc.top + 42);
    SetTextColor(hdc, skin->main_item.text_color[0]);
    DrawTextW(hdc, L"E-mail", -1, &sel_rc, DT_SINGLELINE | DT_VCENTER);

    /* Separator in left column */
    if (skin->main_item.bmp_separator.hbmp)
    {
        RECT sline;
        SetRect(&sline, left_rc.left + 4, left_rc.top + 44, left_rc.right - 4, left_rc.top + 48);
        startskin_draw_separator(hdc, &skin->main_item.bmp_separator, &sline,
                                 skin->main_item.sep_slices_x, skin->main_item.bmp_separator.height, TRUE);
    }

    /* Normal item in right column */
    SetRect(&sel_rc, right_rc.left + 4, right_rc.top + 4, right_rc.right - 4, right_rc.top + 22);
    SetTextColor(hdc, skin->main2_item.text_color[0]);
    DrawTextW(hdc, L"My Documents", -1, &sel_rc, DT_SINGLELINE | DT_VCENTER);

    SelectObject(hdc, old_font);
    startskin_free(skin);
}

static WCHAR s_page_skin[MAX_PATH] = L"None";
static WCHAR s_page_var[64] = {0};
static WCHAR s_page_opts[256] = {0};

static void enum_skins_dlg_cb(const WCHAR *name, const WCHAR *path, BOOL is_7, void *param)
{
    HWND cb = (HWND)param;
    if (SendMessageW(cb, CB_FINDSTRINGEXACT, -1, (LPARAM)name) == CB_ERR)
    {
        SendMessageW(cb, CB_ADDSTRING, 0, (LPARAM)name);
    }
}

static void update_skin_controls(HWND hwnd, BOOL from_user)
{
    HWND hSkinCombo;
    HWND hVarCombo;
    HWND hOptList;
    int sel;
    WCHAR name[MAX_PATH];

    hSkinCombo = GetDlgItem(hwnd, IDC_SKIN_COMBO);
    hVarCombo  = GetDlgItem(hwnd, IDC_SKIN_VARIATION);
    hOptList   = GetDlgItem(hwnd, IDC_SKIN_OPTIONS_LIST);
    sel = (int)SendMessageW(hSkinCombo, CB_GETCURSEL, 0, 0);
    memset(name, 0, sizeof(name));

    if (sel != CB_ERR)
        SendMessageW(hSkinCombo, CB_GETLBTEXT, sel, (LPARAM)name);

    if (!name[0] || _wcsnicmp(name, L"None", 4) == 0)
    {
        wcscpy(s_page_skin, L"None");
        s_page_var[0] = L'\0';
        s_page_opts[0] = L'\0';
        SendMessageW(hVarCombo, CB_RESETCONTENT, 0, 0);
        SendMessageW(hOptList, LVM_DELETEALLITEMS, 0, 0);
        EnableWindow(hVarCombo, FALSE);
        EnableWindow(hOptList, FALSE);
        InvalidateRect(GetDlgItem(hwnd, IDC_SKIN_PREVIEW), NULL, TRUE);
        return;
    }

    wcscpy(s_page_skin, name);
    EnableWindow(hVarCombo, TRUE);
    EnableWindow(hOptList, TRUE);

    /* Inspect skin for variations and options */
    {
        struct start_skin *skin = startskin_load(s_page_skin, NULL, NULL, STARTSKIN_TYPE_CLASSIC2, 96);
        SendMessageW(hVarCombo, CB_RESETCONTENT, 0, 0);
        SendMessageW(hOptList, LVM_DELETEALLITEMS, 0, 0);

        if (skin)
        {
            int i;
            if (skin->variation_count > 0)
            {
                int matched_idx = 0;
                for (i = 0; i < skin->variation_count; i++)
                {
                    int vidx = (int)SendMessageW(hVarCombo, CB_ADDSTRING, 0, (LPARAM)skin->variations[i].label);
                    if (_wcsicmp(skin->variations[i].label, s_page_var) == 0)
                        matched_idx = vidx;
                }
                SendMessageW(hVarCombo, CB_SETCURSEL, matched_idx, 0);
                SendMessageW(hVarCombo, CB_GETLBTEXT, matched_idx, (LPARAM)s_page_var);
            }
            else
            {
                SendMessageW(hVarCombo, CB_ADDSTRING, 0, (LPARAM)L"Default");
                SendMessageW(hVarCombo, CB_SETCURSEL, 0, 0);
                s_page_var[0] = L'\0';
                EnableWindow(hVarCombo, FALSE);
            }

            for (i = 0; i < skin->option_count; i++)
            {
                LVITEMW lvi;
                int item_idx;
                BOOL checked;

                memset(&lvi, 0, sizeof(lvi));
                lvi.mask = LVIF_TEXT | LVIF_PARAM;
                lvi.iItem = i;
                lvi.pszText = skin->options[i].label[0] ? skin->options[i].label : skin->options[i].name;
                lvi.lParam = i;
                item_idx = (int)SendMessageW(hOptList, LVM_INSERTITEMW, 0, (LPARAM)&lvi);

                if (from_user || !s_page_opts[0])
                    checked = skin->options[i].def_value;
                else
                    checked = (wcsstr(s_page_opts, skin->options[i].name) != NULL);

                memset(&lvi, 0, sizeof(lvi));
                lvi.stateMask = LVIS_STATEIMAGEMASK;
                lvi.state = INDEXTOSTATEIMAGEMASK(checked ? 2 : 1);
                SendMessageW(hOptList, LVM_SETITEMSTATE, item_idx, (LPARAM)&lvi);
            }
            startskin_free(skin);
        }
    }
    InvalidateRect(GetDlgItem(hwnd, IDC_SKIN_PREVIEW), NULL, TRUE);
}

static void collect_page_options(HWND hwnd)
{
    HWND hOptList;
    int count;
    int i;
    struct start_skin *skin;

    hOptList = GetDlgItem(hwnd, IDC_SKIN_OPTIONS_LIST);
    count = (int)SendMessageW(hOptList, LVM_GETITEMCOUNT, 0, 0);

    s_page_opts[0] = L'\0';
    if (_wcsicmp(s_page_skin, L"None") == 0) return;

    skin = startskin_load(s_page_skin, NULL, NULL, STARTSKIN_TYPE_CLASSIC2, 96);
    if (!skin) return;

    for (i = 0; i < count && i < skin->option_count; i++)
    {
        int st = (int)SendMessageW(hOptList, LVM_GETITEMSTATE, i, LVIS_STATEIMAGEMASK);
        if ((st >> 12) == 2)
        {
            if (s_page_opts[0]) wcscat(s_page_opts, L",");
            wcscat(s_page_opts, skin->options[i].name);
        }
    }
    startskin_free(skin);
}

static INT_PTR CALLBACK startskin_page_proc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam)
{
    switch (msg)
    {
    case WM_INITDIALOG:
    {
        HWND hSkinCombo = GetDlgItem(hwnd, IDC_SKIN_COMBO);
        HWND hOptList   = GetDlgItem(hwnd, IDC_SKIN_OPTIONS_LIST);
        HKEY hkey;
        DWORD type, size;
        int found;
        LVCOLUMNW col;

        SendMessageW(hOptList, LVM_SETEXTENDEDLISTVIEWSTYLE, 0, (LPARAM)(LVS_EX_CHECKBOXES | LVS_EX_FULLROWSELECT));
        memset(&col, 0, sizeof(col));
        col.mask = LVCF_WIDTH;
        col.cx = 230;
        SendMessageW(hOptList, LVM_INSERTCOLUMNW, 0, (LPARAM)&col);

        /* Read saved settings */
        wcscpy(s_page_skin, L"None");
        s_page_var[0] = L'\0';
        s_page_opts[0] = L'\0';

        if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\WineExplorer\\StartMenu", 0, KEY_READ, &hkey) == ERROR_SUCCESS)
        {
            size = sizeof(s_page_skin);
            RegQueryValueExW(hkey, L"Skin", NULL, &type, (LPBYTE)s_page_skin, &size);
            size = sizeof(s_page_var);
            RegQueryValueExW(hkey, L"SkinVariation", NULL, &type, (LPBYTE)s_page_var, &size);
            size = sizeof(s_page_opts);
            RegQueryValueExW(hkey, L"SkinOptions", NULL, &type, (LPBYTE)s_page_opts, &size);
            RegCloseKey(hkey);
        }

        SendMessageW(hSkinCombo, CB_RESETCONTENT, 0, 0);
        SendMessageW(hSkinCombo, CB_ADDSTRING, 0, (LPARAM)L"None (Built-in)");
        startskin_enumerate(enum_skins_dlg_cb, (void *)hSkinCombo);

        found = (int)SendMessageW(hSkinCombo, CB_FINDSTRINGEXACT, -1, (LPARAM)s_page_skin);
        if (found == CB_ERR) found = 0;
        SendMessageW(hSkinCombo, CB_SETCURSEL, found, 0);

        update_skin_controls(hwnd, FALSE);
        return TRUE;
    }

    case WM_DRAWITEM:
    {
        DRAWITEMSTRUCT *dis = (DRAWITEMSTRUCT *)lparam;
        if (dis->CtlID == IDC_SKIN_PREVIEW)
        {
            draw_skin_preview_item(dis->hDC, &dis->rcItem, s_page_skin, s_page_var, s_page_opts);
            return TRUE;
        }
        break;
    }

    case WM_COMMAND:
        if (LOWORD(wparam) == IDC_SKIN_COMBO && HIWORD(wparam) == CBN_SELCHANGE)
        {
            update_skin_controls(hwnd, TRUE);
            collect_page_options(hwnd);
            SendMessageW(GetParent(hwnd), PSM_CHANGED, (WPARAM)hwnd, 0);
            return TRUE;
        }
        if (LOWORD(wparam) == IDC_SKIN_VARIATION && HIWORD(wparam) == CBN_SELCHANGE)
        {
            int sel = (int)SendMessageW((HWND)lparam, CB_GETCURSEL, 0, 0);
            if (sel != CB_ERR)
            {
                SendMessageW((HWND)lparam, CB_GETLBTEXT, sel, (LPARAM)s_page_var);
                InvalidateRect(GetDlgItem(hwnd, IDC_SKIN_PREVIEW), NULL, TRUE);
                SendMessageW(GetParent(hwnd), PSM_CHANGED, (WPARAM)hwnd, 0);
            }
            return TRUE;
        }
        break;

    case WM_NOTIFY:
    {
        NMHDR *nmhdr = (NMHDR *)lparam;
        if (nmhdr->idFrom == IDC_SKIN_OPTIONS_LIST && nmhdr->code == LVN_ITEMCHANGED)
        {
            collect_page_options(hwnd);
            InvalidateRect(GetDlgItem(hwnd, IDC_SKIN_PREVIEW), NULL, TRUE);
            SendMessageW(GetParent(hwnd), PSM_CHANGED, (WPARAM)hwnd, 0);
            return TRUE;
        }
        if (nmhdr->code == PSN_APPLY)
        {
            HKEY hkey;
            HWND tray;

            collect_page_options(hwnd);

            if (RegCreateKeyExW(HKEY_CURRENT_USER, L"Software\\WineExplorer\\StartMenu", 0, NULL, 0, KEY_WRITE, NULL, &hkey, NULL) == ERROR_SUCCESS)
            {
                RegSetValueExW(hkey, L"Skin", 0, REG_SZ, (const BYTE *)s_page_skin,
                               (DWORD)((wcslen(s_page_skin) + 1) * sizeof(WCHAR)));
                RegSetValueExW(hkey, L"SkinVariation", 0, REG_SZ, (const BYTE *)s_page_var,
                               (DWORD)((wcslen(s_page_var) + 1) * sizeof(WCHAR)));
                RegSetValueExW(hkey, L"SkinOptions", 0, REG_SZ, (const BYTE *)s_page_opts,
                               (DWORD)((wcslen(s_page_opts) + 1) * sizeof(WCHAR)));
                RegCloseKey(hkey);
            }

            startskin_invalidate_active();

            tray = FindWindowW(L"Shell_TrayWnd", NULL);
            if (tray) PostMessageW(tray, WM_SETTINGCHANGE, 0, 0);
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
        .dwICC = ICC_TAB_CLASSES | ICC_LISTVIEW_CLASSES
    };
    PROPSHEETPAGEW pages[3];
    PROPSHEETHEADERW psh;

    InitCommonControlsEx(&init);
    taskbar_settings_load();

    memset(pages, 0, sizeof(pages));

    /* Page 1: Taskbar */
    pages[0].dwSize = sizeof(PROPSHEETPAGEW);
    pages[0].dwFlags = PSP_DEFAULT;
    pages[0].hInstance = WexResModule();
    pages[0].pszTemplate = MAKEINTRESOURCEW(IDD_TASKBARPROP);
    pages[0].pfnDlgProc = taskbar_page_proc;

    /* Page 2: Start Menu */
    pages[1].dwSize = sizeof(PROPSHEETPAGEW);
    pages[1].dwFlags = PSP_DEFAULT;
    pages[1].hInstance = WexResModule();
    pages[1].pszTemplate = MAKEINTRESOURCEW(IDD_STARTMENUPROP);
    pages[1].pfnDlgProc = startmenu_page_proc;

    /* Page 3: Start Menu Skin */
    pages[2].dwSize = sizeof(PROPSHEETPAGEW);
    pages[2].dwFlags = PSP_DEFAULT;
    pages[2].hInstance = WexResModule();
    pages[2].pszTemplate = MAKEINTRESOURCEW(IDD_STARTSKINPROP);
    pages[2].pfnDlgProc = startskin_page_proc;

    memset(&psh, 0, sizeof(psh));
    psh.dwSize = sizeof(PROPSHEETHEADERW);
    psh.dwFlags = PSH_PROPSHEETPAGE | PSH_USECALLBACK;
    psh.pfnCallback = prop_sheet_callback;
    psh.hwndParent = parent;
    psh.hInstance = WexResModule();
    psh.pszCaption = L"Taskbar and Start Menu Properties";
    psh.nPages = ARRAY_SIZE(pages);
    psh.ppsp = pages;

    PropertySheetW(&psh);
}

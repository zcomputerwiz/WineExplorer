/*
 * WineExplorer Desktop Environment - Mouse Properties: Buttons Page
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

#include "main_private.h"

typedef struct _BUTTONS_STATE
{
    BOOL swapped;
    UINT dbl_click_speed;
    BOOL click_lock;
    DWORD click_lock_time;
    BOOL folder_open;
    HBITMAP hbm_mouse;
    HICON hico_folder_closed;
    HICON hico_folder_open;
} BUTTONS_STATE;

static BUTTONS_STATE g_state;

static void read_registry_settings(BUTTONS_STATE *st)
{
    HKEY hkey;
    st->swapped = GetSystemMetrics(SM_SWAPBUTTON);
    st->dbl_click_speed = GetDoubleClickTime();
    st->click_lock = FALSE;
    st->click_lock_time = 1200;
    st->folder_open = FALSE;

    SystemParametersInfoW(SPI_GETMOUSECLICKLOCK, 0, &st->click_lock, 0);
    SystemParametersInfoW(SPI_GETMOUSECLICKLOCKTIME, 0, &st->click_lock_time, 0);

    if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Control Panel\\Mouse", 0, KEY_READ, &hkey) == ERROR_SUCCESS)
    {
        WCHAR buf[32];
        DWORD len = sizeof(buf);
        if (RegQueryValueExW(hkey, L"SwapMouseButtons", NULL, NULL, (LPBYTE)buf, &len) == ERROR_SUCCESS)
            st->swapped = (_wtoi(buf) != 0);

        len = sizeof(buf);
        if (RegQueryValueExW(hkey, L"DoubleClickSpeed", NULL, NULL, (LPBYTE)buf, &len) == ERROR_SUCCESS)
        {
            UINT spd = _wtoi(buf);
            if (spd >= 100 && spd <= 1500) st->dbl_click_speed = spd;
        }
        RegCloseKey(hkey);
    }
}

static void save_registry_settings(const BUTTONS_STATE *st)
{
    HKEY hkey;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, L"Control Panel\\Mouse", 0, NULL,
                        REG_OPTION_NON_VOLATILE, KEY_WRITE, NULL, &hkey, NULL) == ERROR_SUCCESS)
    {
        WCHAR buf[32];
        swprintf(buf, 32, L"%d", st->swapped ? 1 : 0);
        RegSetValueExW(hkey, L"SwapMouseButtons", 0, REG_SZ, (const BYTE *)buf, (lstrlenW(buf) + 1) * sizeof(WCHAR));

        swprintf(buf, 32, L"%u", st->dbl_click_speed);
        RegSetValueExW(hkey, L"DoubleClickSpeed", 0, REG_SZ, (const BYTE *)buf, (lstrlenW(buf) + 1) * sizeof(WCHAR));

        RegCloseKey(hkey);
    }
}

static void draw_mouse_preview(HDC hdc, const RECT *rc, BOOL swapped)
{
    /* Background */
    FillRect(hdc, rc, GetSysColorBrush(COLOR_BTNFACE));

    /* Draw base bitmap or simulated mouse */
    if (g_state.hbm_mouse)
    {
        HDC mdc = CreateCompatibleDC(hdc);
        HBITMAP old_bm = SelectObject(mdc, g_state.hbm_mouse);
        BITMAP bm;
        HBRUSH hl_brush;
        RECT btn_rect;
        int dst_x, dst_y;

        GetObjectW(g_state.hbm_mouse, sizeof(bm), &bm);

        dst_x = rc->left + (rc->right - rc->left - bm.bmWidth) / 2;
        dst_y = rc->top + (rc->bottom - rc->top - bm.bmHeight) / 2;

        BitBlt(hdc, dst_x, dst_y, bm.bmWidth, bm.bmHeight, mdc, 0, 0, SRCCOPY);
        SelectObject(mdc, old_bm);
        DeleteDC(mdc);

        /* Overlay highlight for primary active button */
        hl_brush = CreateSolidBrush(GetSysColor(COLOR_HIGHLIGHT));
        if (!swapped)
        {
            /* Left button area */
            btn_rect.left = dst_x + 8;
            btn_rect.top = dst_y + 8;
            btn_rect.right = dst_x + bm.bmWidth / 2 - 2;
            btn_rect.bottom = dst_y + 24;
        }
        else
        {
            /* Right button area */
            btn_rect.left = dst_x + bm.bmWidth / 2 + 2;
            btn_rect.top = dst_y + 8;
            btn_rect.right = dst_x + bm.bmWidth - 8;
            btn_rect.bottom = dst_y + 24;
        }
        FrameRect(hdc, &btn_rect, hl_brush);
        InflateRect(&btn_rect, -1, -1);
        FrameRect(hdc, &btn_rect, hl_brush);
        DeleteObject(hl_brush);
    }
}

INT_PTR CALLBACK clicklock_settings_proc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam)
{
    switch (msg)
    {
    case WM_INITDIALOG:
    {
        HWND hSlider = GetDlgItem(hwnd, IDT_CLICKLOCK_TIME_SETTINGS);
        SendMessageW(hSlider, TBM_SETRANGE, TRUE, MAKELONG(200, 2200));
        SendMessageW(hSlider, TBM_SETPOS, TRUE, g_state.click_lock_time);
        return TRUE;
    }

    case WM_COMMAND:
        if (LOWORD(wparam) == IDOK)
        {
            HWND hSlider = GetDlgItem(hwnd, IDT_CLICKLOCK_TIME_SETTINGS);
            g_state.click_lock_time = (DWORD)SendMessageW(hSlider, TBM_GETPOS, 0, 0);
            EndDialog(hwnd, IDOK);
            return TRUE;
        }
        else if (LOWORD(wparam) == IDCANCEL)
        {
            EndDialog(hwnd, IDCANCEL);
            return TRUE;
        }
        break;
    }
    return FALSE;
}

INT_PTR CALLBACK button_page_proc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam)
{
    switch (msg)
    {
    case WM_INITDIALOG:
    {
        HWND hSwap = GetDlgItem(hwnd, IDBTN_BUTTONSWAP);
        HWND hSlider = GetDlgItem(hwnd, MOUSE_CLICKSCROLL);
        HWND hLock = GetDlgItem(hwnd, IDCK_CLICKLOCK);
        int pos;

        read_registry_settings(&g_state);

        g_state.hbm_mouse = LoadBitmapW(g_hinst, MAKEINTRESOURCEW(IDB_MOUSE));
        g_state.hico_folder_closed = LoadIconW(g_hinst, MAKEINTRESOURCEW(ICON_FOLDER_CLOSED));
        g_state.hico_folder_open = LoadIconW(g_hinst, MAKEINTRESOURCEW(ICON_FOLDER_OPEN));

        SendMessageW(hSwap, BM_SETCHECK, g_state.swapped ? BST_CHECKED : BST_UNCHECKED, 0);
        SendMessageW(hLock, BM_SETCHECK, g_state.click_lock ? BST_CHECKED : BST_UNCHECKED, 0);

        /* Slider: 0 = Slow (900ms), 10 = Fast (200ms) */
        SendMessageW(hSlider, TBM_SETRANGE, TRUE, MAKELONG(0, 10));
        pos = 10 - (int)((g_state.dbl_click_speed - 200) * 10 / 700);
        if (pos < 0) pos = 0;
        if (pos > 10) pos = 10;
        SendMessageW(hSlider, TBM_SETPOS, TRUE, pos);

        SendDlgItemMessageW(hwnd, MOUSE_DBLCLK_TEST_AREA, STM_SETICON,
                            (WPARAM)g_state.hico_folder_closed, 0);

        return TRUE;
    }

    case WM_DRAWITEM:
    {
        DRAWITEMSTRUCT *dis = (DRAWITEMSTRUCT *)lparam;
        if (dis->CtlID == MOUSE_MOUSEBMP)
        {
            draw_mouse_preview(dis->hDC, &dis->rcItem, g_state.swapped);
            return TRUE;
        }
        break;
    }

    case WM_HSCROLL:
    {
        HWND hSlider = GetDlgItem(hwnd, MOUSE_CLICKSCROLL);
        if ((HWND)lparam == hSlider)
        {
            int pos = (int)SendMessageW(hSlider, TBM_GETPOS, 0, 0);
            /* pos 0 = 900ms, pos 10 = 200ms */
            g_state.dbl_click_speed = 900 - (pos * 70);
            SetDoubleClickTime(g_state.dbl_click_speed);
            SendMessageW(GetParent(hwnd), PSM_CHANGED, (WPARAM)hwnd, 0);
        }
        break;
    }

    case WM_COMMAND:
        switch (LOWORD(wparam))
        {
        case IDBTN_BUTTONSWAP:
            g_state.swapped = (IsDlgButtonChecked(hwnd, IDBTN_BUTTONSWAP) == BST_CHECKED);
            InvalidateRect(GetDlgItem(hwnd, MOUSE_MOUSEBMP), NULL, TRUE);
            SendMessageW(GetParent(hwnd), PSM_CHANGED, (WPARAM)hwnd, 0);
            break;

        case IDCK_CLICKLOCK:
            g_state.click_lock = (IsDlgButtonChecked(hwnd, IDCK_CLICKLOCK) == BST_CHECKED);
            SendMessageW(GetParent(hwnd), PSM_CHANGED, (WPARAM)hwnd, 0);
            break;

        case IDBTN_CLICKLOCK_SETTINGS:
            if (DialogBoxParamW(g_hinst, MAKEINTRESOURCEW(IDD_CLICKLOCK_SETTINGS_DLG),
                                hwnd, clicklock_settings_proc, 0) == IDOK)
            {
                SendMessageW(GetParent(hwnd), PSM_CHANGED, (WPARAM)hwnd, 0);
            }
            break;

        case MOUSE_DBLCLK_TEST_AREA:
            if (HIWORD(wparam) == STN_DBLCLK || HIWORD(wparam) == STN_CLICKED)
            {
                g_state.folder_open = !g_state.folder_open;
                SendDlgItemMessageW(hwnd, MOUSE_DBLCLK_TEST_AREA, STM_SETICON,
                                    (WPARAM)(g_state.folder_open ? g_state.hico_folder_open : g_state.hico_folder_closed), 0);
            }
            break;
        }
        break;

    case WM_NOTIFY:
    {
        NMHDR *nmhdr = (NMHDR *)lparam;
        if (nmhdr->code == PSN_APPLY)
        {
            SwapMouseButton(g_state.swapped);
            SetDoubleClickTime(g_state.dbl_click_speed);
            SystemParametersInfoW(SPI_SETMOUSECLICKLOCK, 0, (void *)(ULONG_PTR)g_state.click_lock,
                                  SPIF_UPDATEINIFILE | SPIF_SENDCHANGE);
            SystemParametersInfoW(SPI_SETMOUSECLICKLOCKTIME, 0, (void *)(ULONG_PTR)g_state.click_lock_time,
                                  SPIF_UPDATEINIFILE | SPIF_SENDCHANGE);
            save_registry_settings(&g_state);
            return TRUE;
        }
        break;
    }

    case WM_DESTROY:
        if (g_state.hbm_mouse) DeleteObject(g_state.hbm_mouse);
        break;
    }

    return FALSE;
}

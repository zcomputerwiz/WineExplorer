/*
 * WineExplorer Desktop Environment - Display Properties Monitor Preview Implementation
 *
 * Renders authentic Windows CRT / LCD monitor with live sample desktop,
 * active window, inactive window, menu bar, and message box dialog preview.
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
#include "preview.h"

#define PREVIEW_CLASS L"DeskCplPreview"

static PREVIEW_DATA g_preview_data;

static void init_default_preview_data(void)
{
    g_preview_data.cr_desktop = GetSysColor(COLOR_BACKGROUND);
    g_preview_data.cr_active_caption = GetSysColor(COLOR_ACTIVECAPTION);
    g_preview_data.cr_active_text = GetSysColor(COLOR_CAPTIONTEXT);
    g_preview_data.cr_inactive_caption = GetSysColor(COLOR_INACTIVECAPTION);
    g_preview_data.cr_inactive_text = GetSysColor(COLOR_INACTIVECAPTIONTEXT);
    g_preview_data.cr_btn_face = GetSysColor(COLOR_BTNFACE);
    g_preview_data.cr_btn_text = GetSysColor(COLOR_BTNTEXT);
    g_preview_data.cr_window = GetSysColor(COLOR_WINDOW);
    g_preview_data.cr_window_text = GetSysColor(COLOR_WINDOWTEXT);
    g_preview_data.cr_menu = GetSysColor(COLOR_MENU);
    g_preview_data.cr_menu_text = GetSysColor(COLOR_MENUTEXT);
}

void preview_update(HWND hwnd_preview, const PREVIEW_DATA *data)
{
    if (data)
    {
        memcpy(&g_preview_data, data, sizeof(PREVIEW_DATA));
    }
    else
    {
        init_default_preview_data();
    }
    if (hwnd_preview && IsWindow(hwnd_preview))
        InvalidateRect(hwnd_preview, NULL, TRUE);
}

static void draw_monitor_casing(HDC hdc, const RECT *rc, RECT *screen_rc)
{
    RECT casing_rc = *rc;
    RECT base_rc;
    RECT neck_rc;
    int stand_w = (rc->right - rc->left) / 3;
    int stand_h = 10;
    int base_w = (rc->right - rc->left) / 2;
    HBRUSH casing_brush = CreateSolidBrush(RGB(180, 180, 185));
    HBRUSH dark_casing = CreateSolidBrush(RGB(130, 130, 135));
    HBRUSH shadow_brush = CreateSolidBrush(RGB(80, 80, 80));

    casing_rc.bottom -= stand_h + 6;

    /* Monitor Stand Base */
    base_rc.left = (rc->left + rc->right - base_w) / 2;
    base_rc.right = base_rc.left + base_w;
    base_rc.bottom = rc->bottom - 1;
    base_rc.top = base_rc.bottom - 5;
    FillRect(hdc, &base_rc, casing_brush);
    FrameRect(hdc, &base_rc, shadow_brush);

    /* Monitor Stand Neck */
    neck_rc.left = (rc->left + rc->right - stand_w) / 2;
    neck_rc.right = neck_rc.left + stand_w;
    neck_rc.top = casing_rc.bottom;
    neck_rc.bottom = base_rc.top;
    FillRect(hdc, &neck_rc, dark_casing);
    FrameRect(hdc, &neck_rc, shadow_brush);

    /* Monitor Frame Bezel */
    FillRect(hdc, &casing_rc, casing_brush);
    DrawEdge(hdc, &casing_rc, EDGE_RAISED, BF_RECT);

    /* Screen display area */
    *screen_rc = casing_rc;
    InflateRect(screen_rc, -8, -8);
    DrawEdge(hdc, screen_rc, EDGE_SUNKEN, BF_RECT);
    InflateRect(screen_rc, -1, -1);

    DeleteObject(casing_brush);
    DeleteObject(dark_casing);
    DeleteObject(shadow_brush);
}

static void draw_preview_screen(HDC hdc, const RECT *screen_rc)
{
    HBRUSH bg_brush = CreateSolidBrush(g_preview_data.cr_desktop);
    HFONT font, small_font;
    LOGFONTW lf;

    FillRect(hdc, screen_rc, bg_brush);
    DeleteObject(bg_brush);

    memset(&lf, 0, sizeof(lf));
    lf.lfHeight = -9;
    lf.lfWeight = FW_BOLD;
    lstrcpyW(lf.lfFaceName, L"Tahoma");
    font = CreateFontIndirectW(&lf);

    lf.lfHeight = -8;
    lf.lfWeight = FW_NORMAL;
    small_font = CreateFontIndirectW(&lf);

    /* Inactive Window */
    {
        RECT win_rc;
        RECT cap_rc;
        HBRUSH inact_brush;

        win_rc.left = screen_rc->left + 10;
        win_rc.top = screen_rc->top + 8;
        win_rc.right = screen_rc->left + (screen_rc->right - screen_rc->left) * 3 / 4;
        win_rc.bottom = screen_rc->top + (screen_rc->bottom - screen_rc->top) * 3 / 4;

        FillRect(hdc, &win_rc, GetSysColorBrush(COLOR_BTNFACE));
        DrawEdge(hdc, &win_rc, EDGE_RAISED, BF_RECT);

        /* Inactive Title bar */
        cap_rc = win_rc;
        cap_rc.left += 2; cap_rc.top += 2; cap_rc.right -= 2; cap_rc.bottom = cap_rc.top + 14;
        inact_brush = CreateSolidBrush(g_preview_data.cr_inactive_caption);
        FillRect(hdc, &cap_rc, inact_brush);
        DeleteObject(inact_brush);

        SelectObject(hdc, font);
        SetBkMode(hdc, TRANSPARENT);
        SetTextColor(hdc, g_preview_data.cr_inactive_text);
        cap_rc.left += 3;
        DrawTextW(hdc, L"Inactive Window", -1, &cap_rc, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    }

    /* Active Window */
    {
        RECT win_rc;
        RECT cap_rc;
        RECT menu_rc;
        RECT client_rc;
        HBRUSH act_brush;
        HBRUSH menu_brush;
        HBRUSH win_brush;

        win_rc.left = screen_rc->left + 22;
        win_rc.top = screen_rc->top + 20;
        win_rc.right = screen_rc->right - 12;
        win_rc.bottom = screen_rc->bottom - 12;

        FillRect(hdc, &win_rc, GetSysColorBrush(COLOR_BTNFACE));
        DrawEdge(hdc, &win_rc, EDGE_RAISED, BF_RECT);

        /* Active Title bar */
        cap_rc = win_rc;
        cap_rc.left += 2; cap_rc.top += 2; cap_rc.right -= 2; cap_rc.bottom = cap_rc.top + 14;
        act_brush = CreateSolidBrush(g_preview_data.cr_active_caption);
        FillRect(hdc, &cap_rc, act_brush);
        DeleteObject(act_brush);

        SelectObject(hdc, font);
        SetBkMode(hdc, TRANSPARENT);
        SetTextColor(hdc, g_preview_data.cr_active_text);
        cap_rc.left += 3;
        DrawTextW(hdc, L"Active Window", -1, &cap_rc, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

        /* Menu Bar */
        menu_rc = win_rc;
        menu_rc.left += 2; menu_rc.top = cap_rc.bottom + 1; menu_rc.right -= 2; menu_rc.bottom = menu_rc.top + 12;
        menu_brush = CreateSolidBrush(g_preview_data.cr_menu);
        FillRect(hdc, &menu_rc, menu_brush);
        DeleteObject(menu_brush);

        SelectObject(hdc, small_font);
        SetTextColor(hdc, g_preview_data.cr_menu_text);
        menu_rc.left += 4;
        DrawTextW(hdc, L"Normal  Disabled  Selected", -1, &menu_rc, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

        /* Window Client area */
        client_rc = win_rc;
        client_rc.left += 3; client_rc.top = menu_rc.bottom + 1; client_rc.right -= 3; client_rc.bottom -= 3;
        win_brush = CreateSolidBrush(g_preview_data.cr_window);
        FillRect(hdc, &client_rc, win_brush);
        DeleteObject(win_brush);
        DrawEdge(hdc, &client_rc, EDGE_SUNKEN, BF_RECT);

        SetTextColor(hdc, g_preview_data.cr_window_text);
        client_rc.left += 4; client_rc.top += 4;
        DrawTextW(hdc, L"Window Text", -1, &client_rc, DT_LEFT | DT_TOP);
    }

    /* Message Box */
    {
        RECT msg_rc;
        RECT cap_rc;
        RECT text_rc;
        RECT btn_rc;
        HBRUSH act_brush;

        msg_rc.left = screen_rc->left + 50;
        msg_rc.top = screen_rc->top + 45;
        msg_rc.right = screen_rc->right - 25;
        msg_rc.bottom = screen_rc->bottom - 18;

        FillRect(hdc, &msg_rc, GetSysColorBrush(COLOR_BTNFACE));
        DrawEdge(hdc, &msg_rc, EDGE_RAISED, BF_RECT);

        /* Message Box Title bar */
        cap_rc = msg_rc;
        cap_rc.left += 2; cap_rc.top += 2; cap_rc.right -= 2; cap_rc.bottom = cap_rc.top + 13;
        act_brush = CreateSolidBrush(g_preview_data.cr_active_caption);
        FillRect(hdc, &cap_rc, act_brush);
        DeleteObject(act_brush);

        SelectObject(hdc, font);
        SetBkMode(hdc, TRANSPARENT);
        SetTextColor(hdc, g_preview_data.cr_active_text);
        cap_rc.left += 3;
        DrawTextW(hdc, L"Message Box", -1, &cap_rc, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

        /* Text */
        text_rc = msg_rc;
        text_rc.left += 8; text_rc.top = cap_rc.bottom + 6;
        SelectObject(hdc, small_font);
        SetTextColor(hdc, GetSysColor(COLOR_BTNTEXT));
        DrawTextW(hdc, L"Message Text", -1, &text_rc, DT_LEFT | DT_TOP);

        /* OK Button */
        btn_rc.right = msg_rc.right - 12;
        btn_rc.left = btn_rc.right - 35;
        btn_rc.bottom = msg_rc.bottom - 6;
        btn_rc.top = btn_rc.bottom - 14;

        FillRect(hdc, &btn_rc, GetSysColorBrush(COLOR_BTNFACE));
        DrawEdge(hdc, &btn_rc, EDGE_RAISED, BF_RECT);
        DrawTextW(hdc, L"OK", -1, &btn_rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    }

    DeleteObject(font);
    DeleteObject(small_font);
}

static LRESULT CALLBACK preview_wnd_proc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam)
{
    switch (msg)
    {
    case WM_PAINT:
    {
        PAINTSTRUCT ps;
        RECT client_rc, screen_rc;
        HDC hdc, mem_dc;
        HBITMAP mem_bm, old_bm;

        hdc = BeginPaint(hwnd, &ps);
        mem_dc = CreateCompatibleDC(hdc);
        GetClientRect(hwnd, &client_rc);
        mem_bm = CreateCompatibleBitmap(hdc, client_rc.right, client_rc.bottom);
        old_bm = SelectObject(mem_dc, mem_bm);

        FillRect(mem_dc, &client_rc, GetSysColorBrush(COLOR_BTNFACE));
        draw_monitor_casing(mem_dc, &client_rc, &screen_rc);
        draw_preview_screen(mem_dc, &screen_rc);

        BitBlt(hdc, 0, 0, client_rc.right, client_rc.bottom, mem_dc, 0, 0, SRCCOPY);

        SelectObject(mem_dc, old_bm);
        DeleteObject(mem_bm);
        DeleteDC(mem_dc);

        EndPaint(hwnd, &ps);
        return 0;
    }

    case WM_ERASEBKGND:
        return 1;

    default:
        return DefWindowProcW(hwnd, msg, wparam, lparam);
    }
}

void preview_register_class(HINSTANCE hinst)
{
    WNDCLASSW wc;
    memset(&wc, 0, sizeof(wc));
    wc.lpfnWndProc = preview_wnd_proc;
    wc.hInstance = hinst;
    wc.lpszClassName = PREVIEW_CLASS;
    wc.hCursor = LoadCursorW(NULL, (LPCWSTR)IDC_ARROW);
    RegisterClassW(&wc);
    init_default_preview_data();
}

void preview_unregister_class(HINSTANCE hinst)
{
    UnregisterClassW(PREVIEW_CLASS, hinst);
}

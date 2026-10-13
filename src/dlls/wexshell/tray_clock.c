/*
 * WineExplorer Desktop Environment - System Tray, Clock & Applets Implementation
 *
 * Implements:
 * - Sunken notification area in Classic mode & themed notification area in Visual Styles mode
 * - Locale-formatted tray clock with 1-second update timer
 * - Interactive calendar popup & Date/Time control panel launch
 * - "Show Desktop" (Minimize All / Restore) toggle button
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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define COBJMACROS
#include <windows.h>
#include <commctrl.h>
#include <shellapi.h>
#include <shlobj.h>
#include <uxtheme.h>

#include "theme_engine.h"
#include "tray_clock.h"
#include "taskbar_props.h"
#include "module.h"


#define IDT_CLOCK_TIMER 1

static HWND g_tray_clock_hwnd = NULL;
static HWND g_clock_tip = NULL;
static HWND g_calendar_popup_hwnd = NULL;

static LRESULT CALLBACK tray_clock_proc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam)
{
    switch (msg)
    {
    case WM_PAINT:
    {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);
        RECT rc;
        WCHAR time_str[64];
        HFONT font, old_font;
        LOGFONTW lf;

        GetClientRect(hwnd, &rc);

        /* the notification area of the taskbar under the clock */
        DrawThemeParentBackground(hwnd, hdc, NULL);

        /* Format local short time (e.g. 5:45 PM) */
        GetTimeFormatW(LOCALE_USER_DEFAULT, TIME_NOSECONDS, NULL, NULL, time_str, sizeof(time_str)/sizeof(WCHAR));

        {
            NONCLIENTMETRICSW ncm = { sizeof(ncm) };
            COLORREF color = GetSysColor(COLOR_BTNTEXT);
            HTHEME clock = theme_open(hwnd, L"TrayNotify::Clock");

            SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(ncm), &ncm, 0);
            lf = ncm.lfStatusFont;
            if (clock)
            {
                GetThemeColor(clock, 0, 0, TMT_TEXTCOLOR, &color);
                CloseThemeData(clock);
            }
            font = CreateFontIndirectW(&lf);
            old_font = SelectObject(hdc, font);
            SetBkMode(hdc, TRANSPARENT);
            SetTextColor(hdc, color);
        }

        {
            /* a taller taskbar also shows the day of the week and the date under the time, like Windows XP */
            WCHAR lines[3][64];
            TEXTMETRICW tm;
            int count = 1, line_h, i, y;

            lstrcpynW(lines[0], time_str, ARRAY_SIZE(lines[0]));
            GetTextMetricsW(hdc, &tm);
            line_h = tm.tmHeight;
            if (rc.bottom - rc.top >= 3 * line_h)
            {
                GetDateFormatW(LOCALE_USER_DEFAULT, 0, NULL, L"dddd", lines[1], ARRAY_SIZE(lines[1]));
                GetDateFormatW(LOCALE_USER_DEFAULT, DATE_SHORTDATE, NULL, NULL, lines[2], ARRAY_SIZE(lines[2]));
                count = 3;
            }
            y = rc.top + (rc.bottom - rc.top - count * line_h) / 2;
            for (i = 0; i < count; i++, y += line_h)
            {
                RECT line = { rc.left, y, rc.right, y + line_h };
                DrawTextW(hdc, lines[i], -1, &line, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
            }
        }

        SelectObject(hdc, old_font);
        DeleteObject(font);

        EndPaint(hwnd, &ps);
        return 0;
    }

    case WM_CREATE:
    {
        TOOLINFOW ti = { sizeof(ti), TTF_SUBCLASS };
        g_clock_tip = CreateWindowExW(WS_EX_TOPMOST, TOOLTIPS_CLASSW, NULL, WS_POPUP | TTS_ALWAYSTIP | TTS_NOPREFIX,
                                      0, 0, 0, 0, hwnd, NULL, explorer_hInstance, NULL);
        ti.hwnd = hwnd;
        ti.uId = 1;
        ti.lpszText = LPSTR_TEXTCALLBACKW;
        GetClientRect(hwnd, &ti.rect);
        SendMessageW(g_clock_tip, TTM_ADDTOOLW, 0, (LPARAM)&ti);
        return 0;
    }

    case WM_SIZE:
    {
        TOOLINFOW ti = { sizeof(ti) };
        ti.hwnd = hwnd;
        ti.uId = 1;
        GetClientRect(hwnd, &ti.rect);
        SendMessageW(g_clock_tip, TTM_NEWTOOLRECTW, 0, (LPARAM)&ti);
        return 0;
    }

    case WM_NOTIFY:
        if (((NMHDR *)lparam)->hwndFrom == g_clock_tip && ((NMHDR *)lparam)->code == TTN_GETDISPINFOW)
        {
            /* the tip shows the full date, like "Thursday, October 08, 2026" */
            NMTTDISPINFOW *info = (NMTTDISPINFOW *)lparam;
            GetDateFormatW(LOCALE_USER_DEFAULT, DATE_LONGDATE, NULL, NULL, info->szText, ARRAY_SIZE(info->szText));
            return 0;
        }
        break;

    case WM_CONTEXTMENU:
        /* the taskbar's menu, with the date and notification items first */
        SendMessageW(g_clock_tip, TTM_ACTIVATE, FALSE, 0);
        SendMessageW(GetAncestor(hwnd, GA_ROOT), WM_APP + 31, 0, lparam);
        SendMessageW(g_clock_tip, TTM_ACTIVATE, TRUE, 0);
        return 0;

    case WM_TIMER:
        if (wparam == IDT_CLOCK_TIMER)
        {
            InvalidateRect(hwnd, NULL, FALSE);
        }
        return 0;

    case WM_TIMECHANGE:
        InvalidateRect(hwnd, NULL, FALSE);
        return 0;

    case WM_LBUTTONDBLCLK:
        /* Launch Date and Time Control Panel */
        ShellExecuteW(NULL, L"open", L"control.exe", L"timedate.cpl", NULL, SW_SHOWNORMAL);
        return 0;

    case WM_ERASEBKGND:
        return 1;

    default:
        return DefWindowProcW(hwnd, msg, wparam, lparam);
    }
}

HWND tray_clock_create(HWND parent_tray, int x, int y, int width, int height)
{
    WNDCLASSW wc;

    if (g_tray_clock_hwnd)
    {
        tray_clock_resize(x, y, width, height);
        return g_tray_clock_hwnd;
    }

    memset(&wc, 0, sizeof(wc));
    wc.lpfnWndProc = tray_clock_proc;
    wc.hInstance = explorer_hInstance;
    wc.lpszClassName = TRAY_CLOCK_CLASS;
    wc.hCursor = LoadCursorW(NULL, (const WCHAR *)IDC_ARROW);
    wc.style = CS_DBLCLKS;
    RegisterClassW(&wc);

    g_tray_clock_hwnd = CreateWindowExW(0, TRAY_CLOCK_CLASS, NULL,
                                        WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS,
                                        x, y, width, height,
                                        parent_tray, NULL, wc.hInstance, NULL);

    SetTimer(g_tray_clock_hwnd, IDT_CLOCK_TIMER, 1000, NULL);
    return g_tray_clock_hwnd;
}

void tray_clock_resize(int x, int y, int width, int height)
{
    if (g_tray_clock_hwnd)
        SetWindowPos(g_tray_clock_hwnd, NULL, x, y, width, height, SWP_NOZORDER | SWP_NOACTIVATE);
}

void tray_clock_destroy(void)
{
    if (g_tray_clock_hwnd)
    {
        KillTimer(g_tray_clock_hwnd, IDT_CLOCK_TIMER);
        DestroyWindow(g_tray_clock_hwnd);
        g_tray_clock_hwnd = NULL;
    }
}

void tray_clock_show(BOOL show)
{
    if (g_tray_clock_hwnd) ShowWindow(g_tray_clock_hwnd, show ? SW_SHOWNA : SW_HIDE);
}

void tray_clock_update(void)
{
    if (g_tray_clock_hwnd)
        InvalidateRect(g_tray_clock_hwnd, NULL, FALSE);
}

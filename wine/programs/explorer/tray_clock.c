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

#include "wine/debug.h"
#include "theme_engine.h"
extern RECT systray_get_notify_rect(void);
#include "tray_clock.h"

WINE_DEFAULT_DEBUG_CHANNEL(explorer);

#define IDT_CLOCK_TIMER 1

static HWND g_tray_clock_hwnd = NULL;
static HWND g_show_desktop_hwnd = NULL;
static HWND g_calendar_popup_hwnd = NULL;
static BOOL g_all_minimized = FALSE;

/* Top-level window enumeration for Minimize All / Restore Toggle */
typedef struct _MINIMIZE_ENUM_DATA
{
    HWND windows[256];
    int count;
    BOOL any_restored;
} MINIMIZE_ENUM_DATA;

static BOOL CALLBACK enum_windows_for_minimize(HWND hwnd, LPARAM lparam)
{
    MINIMIZE_ENUM_DATA *data = (MINIMIZE_ENUM_DATA*)lparam;
    DWORD process_id;
    DWORD style, exstyle;

    if (!IsWindowVisible(hwnd)) return TRUE;
    GetWindowThreadProcessId(hwnd, &process_id);
    if (process_id == GetCurrentProcessId()) return TRUE;

    style = GetWindowLongW(hwnd, GWL_STYLE);
    exstyle = GetWindowLongW(hwnd, GWL_EXSTYLE);

    if (style & WS_CHILD) return TRUE;
    if ((exstyle & WS_EX_TOOLWINDOW) && !(exstyle & WS_EX_APPWINDOW)) return TRUE;
    if (GetWindow(hwnd, GW_OWNER) != NULL && !(exstyle & WS_EX_APPWINDOW)) return TRUE;

    if (data->count < 256)
    {
        data->windows[data->count++] = hwnd;
        if (!IsIconic(hwnd)) data->any_restored = TRUE;
    }
    return TRUE;
}

void show_desktop_toggle(void)
{
    MINIMIZE_ENUM_DATA data;
    int i;

    memset(&data, 0, sizeof(data));
    EnumWindows(enum_windows_for_minimize, (LPARAM)&data);

    if (data.any_restored && !g_all_minimized)
    {
        /* Minimize all top-level windows */
        for (i = 0; i < data.count; i++)
        {
            if (!IsIconic(data.windows[i]))
                ShowWindow(data.windows[i], SW_MINIMIZE);
        }
        g_all_minimized = TRUE;
    }
    else
    {
        /* Restore all windows */
        for (i = 0; i < data.count; i++)
        {
            if (IsIconic(data.windows[i]))
                ShowWindow(data.windows[i], SW_RESTORE);
        }
        g_all_minimized = FALSE;
    }
}

/* Tray Clock Implementation */

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

        /* Fill background to blend seamlessly into sunken notification area */
        {
            HTHEME tray = theme_open(hwnd, L"TrayNotifyHoriz::TrayNotify");
            if (tray)
            {
                /* the tray image spans icons and clock: draw it as the taskbar sees it */
                HWND root = GetAncestor(hwnd, GA_ROOT);
                RECT area = systray_get_notify_rect(), client;
                POINT origin = {0, 0};
                GetClientRect(root, &client);
                area.top = client.top;
                area.bottom = client.bottom;
                MapWindowPoints(hwnd, root, &origin, 1);
                OffsetRect(&area, -origin.x, -origin.y);
                DrawThemeBackground(tray, hdc, TNP_BACKGROUND, 0, &area, NULL);
                CloseThemeData(tray);
            }
            else
                FillRect(hdc, &rc, GetSysColorBrush(COLOR_BTNFACE));
        }

        /* Format local short time (e.g. 5:45 PM) */
        GetTimeFormatW(LOCALE_USER_DEFAULT, TIME_NOSECONDS, NULL, NULL, time_str, sizeof(time_str)/sizeof(WCHAR));

        memset(&lf, 0, sizeof(lf));
        lf.lfHeight = -11;
        lf.lfWeight = FW_NORMAL;
        lstrcpyW(lf.lfFaceName, L"Tahoma");
        font = CreateFontIndirectW(&lf);

        old_font = SelectObject(hdc, font);
        SetBkMode(hdc, TRANSPARENT);
        SetTextColor(hdc, GetSysColor(COLOR_BTNTEXT));

        DrawTextW(hdc, time_str, -1, &rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

        SelectObject(hdc, old_font);
        DeleteObject(font);

        EndPaint(hwnd, &ps);
        return 0;
    }

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
    wc.hInstance = GetModuleHandleW(NULL);
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

void tray_clock_update(void)
{
    if (g_tray_clock_hwnd)
        InvalidateRect(g_tray_clock_hwnd, NULL, FALSE);
}

/* Quick Launch toolbar: "Show Desktop" followed by the shortcuts in
 * %APPDATA%\Microsoft\Internet Explorer\Quick Launch, behind a rebar gripper. */

#define QL_GRIPPER_W 9
#define QL_ITEM_W    23   /* 16 pixel icon with 3 pixels either side, like a Luna toolbar button */
#define QL_MAX_ITEMS 16

typedef struct _QL_ITEM
{
    WCHAR path[MAX_PATH];   /* empty = Show Desktop */
    WCHAR name[MAX_PATH];
    HICON icon;
} QL_ITEM;

static QL_ITEM g_ql_items[QL_MAX_ITEMS];
static int g_ql_count;
static int g_ql_hot = -1, g_ql_down = -1;
static BOOL g_ql_tracking;
static HWND g_ql_tooltip;

static void ql_load(void)
{
    WCHAR dir[MAX_PATH], pattern[MAX_PATH];
    WIN32_FIND_DATAW fd;
    HANDLE find;

    if (g_ql_count) return;
    lstrcpyW(g_ql_items[0].name, L"Show Desktop");
    g_ql_count = 1;

    if (FAILED(SHGetFolderPathW(NULL, CSIDL_APPDATA, NULL, SHGFP_TYPE_CURRENT, dir))) return;
    lstrcatW(dir, L"\\Microsoft\\Internet Explorer\\Quick Launch");
    swprintf(pattern, MAX_PATH, L"%s\\*", dir);
    if ((find = FindFirstFileW(pattern, &fd)) == INVALID_HANDLE_VALUE) return;
    do
    {
        SHFILEINFOW info;
        QL_ITEM *item;
        WCHAR *dot;

        if ((fd.dwFileAttributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_HIDDEN)) || g_ql_count >= QL_MAX_ITEMS) continue;
        if (!wcsicmp(fd.cFileName, L"desktop.ini") || !wcsicmp(fd.cFileName, L"Show Desktop.scf")) continue;
        item = &g_ql_items[g_ql_count];
        swprintf(item->path, MAX_PATH, L"%s\\%s", dir, fd.cFileName);
        lstrcpynW(item->name, fd.cFileName, MAX_PATH);
        if ((dot = wcsrchr(item->name, '.'))) *dot = 0;
        memset(&info, 0, sizeof(info));
        if (SHGetFileInfoW(item->path, 0, &info, sizeof(info), SHGFI_ICON | SHGFI_SMALLICON)) item->icon = info.hIcon;
        g_ql_count++;
    } while (FindNextFileW(find, &fd));
    FindClose(find);
}

int show_desktop_button_get_width(void)
{
    ql_load();
    return QL_GRIPPER_W + g_ql_count * QL_ITEM_W;
}

static int ql_hit(int x)
{
    int i = (x - QL_GRIPPER_W) / QL_ITEM_W;
    return (x >= QL_GRIPPER_W && i >= 0 && i < g_ql_count) ? i : -1;
}

static void ql_item_rect(HWND hwnd, int i, RECT *rc)
{
    GetClientRect(hwnd, rc);
    rc->left = QL_GRIPPER_W + i * QL_ITEM_W;
    rc->right = rc->left + QL_ITEM_W;
    rc->top += 3;
    rc->bottom -= 3;
}

static void ql_draw_show_desktop(HDC hdc, const RECT *rc)
{
    RECT sky = { rc->left, rc->top, rc->left + 14, rc->top + 10 }, desk;

    /* a desktop with a window on it */
    FillRect(hdc, &sky, GetSysColorBrush(COLOR_BACKGROUND));
    FrameRect(hdc, &sky, GetSysColorBrush(COLOR_BTNSHADOW));
    desk = sky;
    InflateRect(&desk, -2, -2);
    desk.bottom -= 2;
    FillRect(hdc, &desk, GetSysColorBrush(COLOR_WINDOW));
    desk.bottom = desk.top + 2;
    FillRect(hdc, &desk, GetSysColorBrush(COLOR_ACTIVECAPTION));
}

static void ql_paint(HWND hwnd, HDC hdc)
{
    RECT rc, grip;
    HTHEME rebar = theme_open(hwnd, L"TaskBar::Rebar");
    HTHEME toolbar = theme_open(hwnd, L"TaskBar::Toolbar");
    int i;

    GetClientRect(hwnd, &rc);
    if (!theme_draw_taskbar_background(hwnd, hdc)) FillRect(hdc, &rc, GetSysColorBrush(COLOR_BTNFACE));

    grip = rc;
    grip.left += 2; grip.right = grip.left + 3; grip.top += 3; grip.bottom -= 3;
    if (rebar) DrawThemeBackground(rebar, hdc, RP_GRIPPER, 0, &grip, NULL);
    else DrawEdge(hdc, &grip, EDGE_RAISED, BF_RECT);

    for (i = 0; i < g_ql_count; i++)
    {
        RECT item;
        int state = (i == g_ql_down && i == g_ql_hot) ? 3 : (i == g_ql_hot ? 2 : 1), off = state == 3 ? 1 : 0;

        ql_item_rect(hwnd, i, &item);
        if (state > 1)
        {
            if (toolbar) DrawThemeBackground(toolbar, hdc, TP_BUTTON, state, &item, NULL);
            else DrawEdge(hdc, &item, state == 3 ? EDGE_SUNKEN : EDGE_RAISED, BF_RECT);
        }
        if (g_ql_items[i].icon)
            DrawIconEx(hdc, item.left + 3 + off, item.top + (item.bottom - item.top - 16) / 2 + off,
                       g_ql_items[i].icon, 16, 16, 0, NULL, DI_NORMAL);
        else
        {
            RECT glyph = { item.left + 4 + off, item.top + 4 + off, item.left + 18 + off, item.top + 16 + off };
            ql_draw_show_desktop(hdc, &glyph);
        }
    }
    if (rebar) CloseThemeData(rebar);
    if (toolbar) CloseThemeData(toolbar);
}

static void ql_update_tooltips(HWND hwnd)
{
    TOOLINFOW ti = { sizeof(ti) };
    int i;

    if (!g_ql_tooltip) return;
    for (i = 0; i < QL_MAX_ITEMS; i++)
    {
        ti.hwnd = hwnd; ti.uId = i;
        SendMessageW(g_ql_tooltip, TTM_DELTOOLW, 0, (LPARAM)&ti);
    }
    for (i = 0; i < g_ql_count; i++)
    {
        ti.uFlags = TTF_SUBCLASS; ti.hwnd = hwnd; ti.uId = i;
        ql_item_rect(hwnd, i, &ti.rect);
        ti.lpszText = g_ql_items[i].name;
        SendMessageW(g_ql_tooltip, TTM_ADDTOOLW, 0, (LPARAM)&ti);
    }
}

static LRESULT CALLBACK show_desktop_button_proc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam)
{
    switch (msg)
    {
    case WM_CREATE:
        g_ql_tooltip = CreateWindowExW(WS_EX_TOPMOST, TOOLTIPS_CLASSW, NULL, WS_POPUP | TTS_ALWAYSTIP | TTS_NOPREFIX,
                                       0, 0, 0, 0, hwnd, NULL, GetModuleHandleW(NULL), NULL);
        return 0;

    case WM_SIZE:
        ql_update_tooltips(hwnd);
        return 0;

    case WM_PAINT:
    {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);
        ql_paint(hwnd, hdc);
        EndPaint(hwnd, &ps);
        return 0;
    }

    case WM_MOUSEMOVE:
    {
        int hot = ql_hit((short)LOWORD(lparam));
        if (!g_ql_tracking)
        {
            TRACKMOUSEEVENT tme = { sizeof(tme), TME_LEAVE, hwnd, 0 };
            TrackMouseEvent(&tme);
            g_ql_tracking = TRUE;
        }
        if (hot != g_ql_hot) { g_ql_hot = hot; InvalidateRect(hwnd, NULL, FALSE); }
        return 0;
    }

    case WM_MOUSELEAVE:
        g_ql_tracking = FALSE;
        g_ql_hot = g_ql_down = -1;
        InvalidateRect(hwnd, NULL, FALSE);
        return 0;

    case WM_LBUTTONDOWN:
        g_ql_down = ql_hit((short)LOWORD(lparam));
        SetCapture(hwnd);
        InvalidateRect(hwnd, NULL, FALSE);
        return 0;

    case WM_LBUTTONUP:
    {
        int i = ql_hit((short)LOWORD(lparam)), was_down = g_ql_down;
        ReleaseCapture();
        g_ql_down = -1;
        InvalidateRect(hwnd, NULL, FALSE);
        if (i >= 0 && i == was_down)
        {
            if (!g_ql_items[i].path[0]) show_desktop_toggle();
            else ShellExecuteW(hwnd, NULL, g_ql_items[i].path, NULL, NULL, SW_SHOWNORMAL);
        }
        return 0;
    }

    case WM_ERASEBKGND:
        return 1;

    default:
        return DefWindowProcW(hwnd, msg, wparam, lparam);
    }
}

HWND show_desktop_button_create(HWND parent_tray, int x, int y, int width, int height)
{
    WNDCLASSW wc;

    if (g_show_desktop_hwnd)
    {
        show_desktop_button_resize(x, y, width, height);
        return g_show_desktop_hwnd;
    }

    ql_load();
    memset(&wc, 0, sizeof(wc));
    wc.lpfnWndProc = show_desktop_button_proc;
    wc.hInstance = GetModuleHandleW(NULL);
    wc.lpszClassName = SHOW_DESKTOP_CLASS;
    wc.hCursor = LoadCursorW(NULL, (const WCHAR *)IDC_ARROW);
    RegisterClassW(&wc);

    {
        RECT parent;
        GetClientRect(parent_tray, &parent);
        g_show_desktop_hwnd = CreateWindowExW(0, SHOW_DESKTOP_CLASS, NULL,
                                              WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS,
                                              x, 0, show_desktop_button_get_width(), parent.bottom,
                                              parent_tray, NULL, wc.hInstance, NULL);
    }

    return g_show_desktop_hwnd;
}

void show_desktop_button_resize(int x, int y, int width, int height)
{
    RECT parent;

    if (!g_show_desktop_hwnd) return;
    GetClientRect(GetParent(g_show_desktop_hwnd), &parent);
    SetWindowPos(g_show_desktop_hwnd, NULL, x, 0, show_desktop_button_get_width(), parent.bottom,
                 SWP_NOZORDER | SWP_NOACTIVATE);
}

void show_desktop_button_destroy(void)
{
    if (g_show_desktop_hwnd)
    {
        DestroyWindow(g_show_desktop_hwnd);
        g_show_desktop_hwnd = NULL;
    }
}

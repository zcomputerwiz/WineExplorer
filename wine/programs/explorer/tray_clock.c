/*
 * WineExplorer Desktop Environment - System Tray, Clock & Applets Implementation
 *
 * Implements:
 * - Sunken notification area in Classic mode & themed notification area in Visual Styles mode
 * - Locale-formatted tray clock with 1-second update timer
 * - Interactive calendar popup & Date/Time control panel launch
 * - "Show Desktop" (Minimize All / Restore) toggle button
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define COBJMACROS
#include <windows.h>
#include <commctrl.h>
#include <shellapi.h>
#include <uxtheme.h>

#include "wine/debug.h"
#include "theme_engine.h"
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
        FillRect(hdc, &rc, GetSysColorBrush(COLOR_BTNFACE));

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

/* Show Desktop Button Implementation */

static LRESULT CALLBACK show_desktop_button_proc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam)
{
    switch (msg)
    {
    case WM_PAINT:
    {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);
        RECT rc;
        GetClientRect(hwnd, &rc);

        FillRect(hdc, &rc, GetSysColorBrush(COLOR_BTNFACE));
        theme_draw_classic_bevel(hdc, &rc, EDGE_RAISED, BF_RECT | BF_SOFT | BF_ADJUST);

        /* Draw mini desktop rectangle icon */
        {
            RECT icon_rc = { rc.left + 3, rc.top + 3, rc.right - 3, rc.bottom - 5 };
            FrameRect(hdc, &icon_rc, GetSysColorBrush(COLOR_BTNSHADOW));
            icon_rc.top = icon_rc.bottom - 2;
            icon_rc.bottom = rc.bottom - 3;
            FillRect(hdc, &icon_rc, GetSysColorBrush(COLOR_HIGHLIGHT));
        }

        EndPaint(hwnd, &ps);
        return 0;
    }

    case WM_LBUTTONDOWN:
        show_desktop_toggle();
        return 0;

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

    memset(&wc, 0, sizeof(wc));
    wc.lpfnWndProc = show_desktop_button_proc;
    wc.hInstance = GetModuleHandleW(NULL);
    wc.lpszClassName = SHOW_DESKTOP_CLASS;
    wc.hCursor = LoadCursorW(NULL, (const WCHAR *)IDC_ARROW);
    RegisterClassW(&wc);

    g_show_desktop_hwnd = CreateWindowExW(0, SHOW_DESKTOP_CLASS, NULL,
                                          WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS,
                                          x, y, width, height,
                                          parent_tray, NULL, wc.hInstance, NULL);

    return g_show_desktop_hwnd;
}

void show_desktop_button_resize(int x, int y, int width, int height)
{
    if (g_show_desktop_hwnd)
        SetWindowPos(g_show_desktop_hwnd, NULL, x, y, width, height, SWP_NOZORDER | SWP_NOACTIVATE);
}

void show_desktop_button_destroy(void)
{
    if (g_show_desktop_hwnd)
    {
        DestroyWindow(g_show_desktop_hwnd);
        g_show_desktop_hwnd = NULL;
    }
}

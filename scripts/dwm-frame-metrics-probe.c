/*
 * WineExplorer - DWM frame, extended border, and caption geometry probe
 *
 * Copyright 2026 WineExplorer Desktop Environment Project
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#define _WIN32_WINNT 0x0A00
#define NTDDI_VERSION 0x0A000007

#include <windows.h>
#include <dwmapi.h>
#include <io.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#ifndef DPI_AWARENESS_CONTEXT_UNAWARE
#define DPI_AWARENESS_CONTEXT_UNAWARE ((HANDLE)(LONG_PTR)-1)
#endif
#ifndef DPI_AWARENESS_CONTEXT_SYSTEM_AWARE
#define DPI_AWARENESS_CONTEXT_SYSTEM_AWARE ((HANDLE)(LONG_PTR)-2)
#endif
#ifndef DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2
#define DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2 ((HANDLE)(LONG_PTR)-4)
#endif

#ifndef SM_CXPADDEDBORDER
#define SM_CXPADDEDBORDER 92
#endif

#define NOT_FOUND ((HRESULT)0x8007007f)

typedef HRESULT (WINAPI *pfn_DwmGetWindowAttribute)(HWND, DWORD, PVOID, DWORD);
typedef HRESULT (WINAPI *pfn_DwmFlush)(void);
typedef HANDLE (WINAPI *pfn_SetThreadDpiAwarenessContext)(HANDLE);
typedef UINT (WINAPI *pfn_GetDpiForWindow)(HWND);
typedef int (WINAPI *pfn_GetSystemMetricsForDpi)(int, UINT);

struct monitor_candidate
{
    RECT rc;
    UINT dpi;
};

struct monitor_list
{
    struct monitor_candidate items[16];
    int count;
};

struct window_kind
{
    int id;
    DWORD style;
    DWORD exstyle;
    int maximize;
};

static FILE *g_con_out = NULL;
static FILE *g_file_out = NULL;
static pfn_DwmGetWindowAttribute pDwmGetWindowAttribute = NULL;
static pfn_DwmFlush pDwmFlush = NULL;
static pfn_SetThreadDpiAwarenessContext pSetThreadDpiAwarenessContext = NULL;
static pfn_GetDpiForWindow pGetDpiForWindow = NULL;
static pfn_GetSystemMetricsForDpi pGetSystemMetricsForDpi = NULL;

static void init_output(const char *path)
{
    int fd = _dup(_fileno(stdout));
    if (fd >= 0)
    {
        g_con_out = _fdopen(fd, "w");
        if (g_con_out)
            setvbuf(g_con_out, NULL, _IONBF, 0);
    }
    CreateDirectoryA("Z:\\tmp", NULL);
    g_file_out = freopen(path, "w", stdout);
    if (!g_file_out) g_file_out = freopen(strrchr(path, '\\') + 1, "w", stdout);  /* no Z: on Windows: use the current directory */
    if (g_file_out)
        setvbuf(stdout, NULL, _IONBF, 0);
}

static void emit_line(const char *fmt, ...)
{
    va_list ap;
    if (g_file_out)
    {
        va_start(ap, fmt);
        vfprintf(stdout, fmt, ap);
        va_end(ap);
        fflush(stdout);
    }
    if (g_con_out)
    {
        va_start(ap, fmt);
        vfprintf(g_con_out, fmt, ap);
        va_end(ap);
        fflush(g_con_out);
    }
}

static void pump_messages(void)
{
    MSG msg;
    while (PeekMessageA(&msg, NULL, 0, 0, PM_REMOVE))
    {
        TranslateMessage(&msg);
        DispatchMessageA(&msg);
    }
}

static LRESULT CALLBACK probe_wndproc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    return DefWindowProcA(hwnd, msg, wp, lp);
}

static int get_metric_for_dpi(int idx, UINT dpi)
{
    if (pGetSystemMetricsForDpi)
        return pGetSystemMetricsForDpi(idx, dpi);
    return GetSystemMetrics(idx);
}

static UINT get_window_dpi(HWND hwnd)
{
    if (pGetDpiForWindow)
    {
        UINT d = pGetDpiForWindow(hwnd);
        if (d > 0)
            return d;
    }
    HDC hdc = GetDC(NULL);
    UINT d = hdc ? (UINT)GetDeviceCaps(hdc, LOGPIXELSX) : 96u;
    if (hdc)
        ReleaseDC(NULL, hdc);
    return d ? d : 96u;
}

static BOOL CALLBACK enum_mon_proc(HMONITOR hmon, HDC hdc, LPRECT lprc, LPARAM lparam)
{
    struct monitor_list *list = (struct monitor_list *)lparam;
    MONITORINFO mi;
    (void)hdc;
    (void)lprc;
    if (list->count >= 16)
        return FALSE;
    memset(&mi, 0, sizeof(mi));
    mi.cbSize = sizeof(mi);
    if (GetMonitorInfoA(hmon, &mi))
    {
        list->items[list->count].rc = mi.rcWork;
        list->items[list->count].dpi = 0;
        list->count++;
    }
    return TRUE;
}

static void probe_window_kind(UINT target_dpi, const RECT *mon_rc, const char *cls_name,
                              HINSTANCE hinst, const struct window_kind *wk)
{
    int x = mon_rc->left + 80;
    int y = mon_rc->top + 80;
    int w = 400;
    int h = 300;
    HWND hwnd;
    UINT actual_dpi;
    RECT rc_win = {0}, rc_cli = {0}, rc_ext = {0}, rc_cap = {0};
    HRESULT hr_ext = NOT_FOUND, hr_cap = NOT_FOUND;
    int sm_cxframe, sm_cyframe, sm_cxpaddedborder, sm_cycaption, sm_cysmcaption;
    int sm_cxsize, sm_cysize, sm_cxsizeframe, sm_cysizeframe, sm_cxmintrack;
    long d_left, d_top, d_right, d_bottom;

    hwnd = CreateWindowExA(wk->exstyle, cls_name, "dwm_frame_probe",
                           wk->style, x, y, w, h, NULL, NULL, hinst, NULL);
    if (!hwnd)
    {
        emit_line("target_dpi=%u kind=%d create_failed=1 gle=%lu\n",
                  target_dpi, wk->id, (unsigned long)GetLastError());
        return;
    }

    ShowWindow(hwnd, wk->maximize ? SW_SHOWMAXIMIZED : SW_SHOWNORMAL);
    UpdateWindow(hwnd);
    pump_messages();
    if (pDwmFlush)
        pDwmFlush();
    Sleep(30);
    pump_messages();

    actual_dpi = get_window_dpi(hwnd);
    sm_cxframe = get_metric_for_dpi(SM_CXFRAME, actual_dpi);
    sm_cyframe = get_metric_for_dpi(SM_CYFRAME, actual_dpi);
    sm_cxpaddedborder = get_metric_for_dpi(SM_CXPADDEDBORDER, actual_dpi);
    sm_cycaption = get_metric_for_dpi(SM_CYCAPTION, actual_dpi);
    sm_cysmcaption = get_metric_for_dpi(SM_CYSMCAPTION, actual_dpi);
    sm_cxsize = get_metric_for_dpi(SM_CXSIZE, actual_dpi);
    sm_cysize = get_metric_for_dpi(SM_CYSIZE, actual_dpi);
    sm_cxsizeframe = get_metric_for_dpi(SM_CXSIZEFRAME, actual_dpi);
    sm_cysizeframe = get_metric_for_dpi(SM_CYSIZEFRAME, actual_dpi);
    sm_cxmintrack = get_metric_for_dpi(SM_CXMINTRACK, actual_dpi);

    GetWindowRect(hwnd, &rc_win);
    GetClientRect(hwnd, &rc_cli);
    MapWindowPoints(hwnd, NULL, (POINT *)&rc_cli, 2);

    if (pDwmGetWindowAttribute)
    {
        hr_ext = pDwmGetWindowAttribute(hwnd, DWMWA_EXTENDED_FRAME_BOUNDS, &rc_ext, sizeof(rc_ext));
        hr_cap = pDwmGetWindowAttribute(hwnd, DWMWA_CAPTION_BUTTON_BOUNDS, &rc_cap, sizeof(rc_cap));
    }

    d_left = rc_ext.left - rc_win.left;
    d_top = rc_ext.top - rc_win.top;
    d_right = rc_win.right - rc_ext.right;
    d_bottom = rc_win.bottom - rc_ext.bottom;

    emit_line("target_dpi=%u actual_dpi=%u kind=%d style=0x%08lx exstyle=0x%08lx maximized=%d "
              "sm_cxframe=%d sm_cyframe=%d sm_cxpaddedborder=%d sm_cycaption=%d sm_cysmcaption=%d "
              "sm_cxsize=%d sm_cysize=%d sm_cxsizeframe=%d sm_cysizeframe=%d sm_cxmintrack=%d "
              "win_l=%ld win_t=%ld win_r=%ld win_b=%ld "
              "cli_l=%ld cli_t=%ld cli_r=%ld cli_b=%ld "
              "ext_hr=0x%08lx ext_l=%ld ext_t=%ld ext_r=%ld ext_b=%ld "
              "cap_hr=0x%08lx cap_l=%ld cap_t=%ld cap_r=%ld cap_b=%ld "
              "delta_l=%ld delta_t=%ld delta_r=%ld delta_b=%ld\n",
              target_dpi, actual_dpi, wk->id,
              (unsigned long)wk->style, (unsigned long)wk->exstyle, wk->maximize,
              sm_cxframe, sm_cyframe, sm_cxpaddedborder, sm_cycaption, sm_cysmcaption,
              sm_cxsize, sm_cysize, sm_cxsizeframe, sm_cysizeframe, sm_cxmintrack,
              rc_win.left, rc_win.top, rc_win.right, rc_win.bottom,
              rc_cli.left, rc_cli.top, rc_cli.right, rc_cli.bottom,
              (unsigned long)hr_ext, rc_ext.left, rc_ext.top, rc_ext.right, rc_ext.bottom,
              (unsigned long)hr_cap, rc_cap.left, rc_cap.top, rc_cap.right, rc_cap.bottom,
              d_left, d_top, d_right, d_bottom);

    DestroyWindow(hwnd);
    pump_messages();
}

int main(void)
{
    static const UINT target_dpis[4] = {96, 120, 144, 192};
    static const struct window_kind kinds[5] = {
        {0, WS_OVERLAPPEDWINDOW, 0, 0},
        {1, WS_POPUPWINDOW, 0, 0},
        {2, WS_OVERLAPPEDWINDOW, WS_EX_TOOLWINDOW, 0},
        {3, WS_POPUP | WS_BORDER, 0, 0},
        {4, WS_OVERLAPPEDWINDOW, 0, 1}
    };
    HMODULE user32, dwmapi;
    WNDCLASSA wc;
    struct monitor_list mons;
    int i, j, k;

    init_output("Z:\\tmp\\dwm-frame-metrics-probe.txt");

    user32 = GetModuleHandleA("user32.dll");
    dwmapi = LoadLibraryA("dwmapi.dll");

    if (user32)
    {
        pSetThreadDpiAwarenessContext = (pfn_SetThreadDpiAwarenessContext)(void *)
            GetProcAddress(user32, "SetThreadDpiAwarenessContext");
        pGetDpiForWindow = (pfn_GetDpiForWindow)(void *)
            GetProcAddress(user32, "GetDpiForWindow");
        pGetSystemMetricsForDpi = (pfn_GetSystemMetricsForDpi)(void *)
            GetProcAddress(user32, "GetSystemMetricsForDpi");
    }
    if (dwmapi)
    {
        pDwmGetWindowAttribute = (pfn_DwmGetWindowAttribute)(void *)
            GetProcAddress(dwmapi, "DwmGetWindowAttribute");
        pDwmFlush = (pfn_DwmFlush)(void *)
            GetProcAddress(dwmapi, "DwmFlush");
    }

    if (pSetThreadDpiAwarenessContext)
        pSetThreadDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

    memset(&wc, 0, sizeof(wc));
    wc.lpfnWndProc = probe_wndproc;
    wc.hInstance = GetModuleHandleA(NULL);
    wc.lpszClassName = "dwm_frame_metrics_probe_cls";
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    RegisterClassA(&wc);

    memset(&mons, 0, sizeof(mons));
    EnumDisplayMonitors(NULL, NULL, enum_mon_proc, (LPARAM)&mons);
    if (mons.count == 0)
    {
        mons.items[0].rc.left = 0;
        mons.items[0].rc.top = 0;
        mons.items[0].rc.right = 1024;
        mons.items[0].rc.bottom = 768;
        mons.count = 1;
    }

    for (i = 0; i < mons.count; i++)
    {
        HWND htmp = CreateWindowExA(0, wc.lpszClassName, "", WS_POPUP,
                                    mons.items[i].rc.left + 50, mons.items[i].rc.top + 50,
                                    100, 100, NULL, NULL, wc.hInstance, NULL);
        if (htmp)
        {
            mons.items[i].dpi = get_window_dpi(htmp);
            DestroyWindow(htmp);
        }
        else
        {
            mons.items[i].dpi = 96;
        }
    }

    for (i = 0; i < 4; i++)
    {
        static const HANDLE ctx_list[3] = {
            DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2,
            DPI_AWARENESS_CONTEXT_SYSTEM_AWARE,
            DPI_AWARENESS_CONTEXT_UNAWARE
        };
        UINT tdpi = target_dpis[i];
        int match_mon = -1;
        HANDLE match_ctx = DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2;
        int c;

        for (c = 0; c < 3 && match_mon < 0; c++)
        {
            if (pSetThreadDpiAwarenessContext)
                pSetThreadDpiAwarenessContext(ctx_list[c]);
            for (j = 0; j < mons.count; j++)
            {
                HWND htmp = CreateWindowExA(0, wc.lpszClassName, "", WS_POPUP,
                                            mons.items[j].rc.left + 50, mons.items[j].rc.top + 50,
                                            100, 100, NULL, NULL, wc.hInstance, NULL);
                if (htmp)
                {
                    UINT d = get_window_dpi(htmp);
                    DestroyWindow(htmp);
                    if (d == tdpi)
                    {
                        match_mon = j;
                        match_ctx = ctx_list[c];
                        break;
                    }
                }
            }
        }

        if (pSetThreadDpiAwarenessContext)
            pSetThreadDpiAwarenessContext(match_ctx);

        if (match_mon < 0)
        {
            emit_line("target_dpi=%u actual_dpi=%u dpi_unavailable=1 "
                      "sm_cxframe=%d sm_cyframe=%d sm_cxpaddedborder=%d sm_cycaption=%d sm_cysmcaption=%d "
                      "sm_cxsize=%d sm_cysize=%d sm_cxsizeframe=%d sm_cysizeframe=%d sm_cxmintrack=%d\n",
                      tdpi, mons.items[0].dpi,
                      get_metric_for_dpi(SM_CXFRAME, tdpi),
                      get_metric_for_dpi(SM_CYFRAME, tdpi),
                      get_metric_for_dpi(SM_CXPADDEDBORDER, tdpi),
                      get_metric_for_dpi(SM_CYCAPTION, tdpi),
                      get_metric_for_dpi(SM_CYSMCAPTION, tdpi),
                      get_metric_for_dpi(SM_CXSIZE, tdpi),
                      get_metric_for_dpi(SM_CYSIZE, tdpi),
                      get_metric_for_dpi(SM_CXSIZEFRAME, tdpi),
                      get_metric_for_dpi(SM_CYSIZEFRAME, tdpi),
                      get_metric_for_dpi(SM_CXMINTRACK, tdpi));
            continue;
        }

        for (k = 0; k < 5; k++)
            probe_window_kind(tdpi, &mons.items[match_mon].rc, wc.lpszClassName, wc.hInstance, &kinds[k]);
    }

    if (dwmapi)
        FreeLibrary(dwmapi);
    if (g_con_out)
        fclose(g_con_out);
    return 0;
}

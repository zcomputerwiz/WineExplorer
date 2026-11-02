/*
 * WineExplorer - DWM corner radius, arc coverage, and drop-shadow falloff probe
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
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#ifndef DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2
#define DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2 ((HANDLE)(LONG_PTR)-4)
#endif

#ifndef CAPTUREBLT
#define CAPTUREBLT 0x40000000
#endif

#ifndef DWMWA_USE_IMMERSIVE_DARK_MODE
#define DWMWA_USE_IMMERSIVE_DARK_MODE 20
#endif
#ifndef DWMWA_WINDOW_CORNER_PREFERENCE
#define DWMWA_WINDOW_CORNER_PREFERENCE 33
#endif
#ifndef DWMWA_BORDER_COLOR
#define DWMWA_BORDER_COLOR 34
#endif
#ifndef DWMWA_CAPTION_COLOR
#define DWMWA_CAPTION_COLOR 35
#endif
#ifndef DWMWA_TEXT_COLOR
#define DWMWA_TEXT_COLOR 36
#endif

#define NOT_FOUND ((HRESULT)0x8007007f)
#define MAX_SHADOW_SCAN 96

typedef HRESULT (WINAPI *pfn_DwmGetWindowAttribute)(HWND, DWORD, PVOID, DWORD);
typedef HRESULT (WINAPI *pfn_DwmSetWindowAttribute)(HWND, DWORD, LPCVOID, DWORD);
typedef HRESULT (WINAPI *pfn_DwmFlush)(void);
typedef HANDLE (WINAPI *pfn_SetThreadDpiAwarenessContext)(HANDLE);
typedef UINT (WINAPI *pfn_GetDpiForWindow)(HWND);

static FILE *g_con_out = NULL;
static FILE *g_file_out = NULL;
static pfn_DwmGetWindowAttribute pDwmGetWindowAttribute = NULL;
static pfn_DwmSetWindowAttribute pDwmSetWindowAttribute = NULL;
static pfn_DwmFlush pDwmFlush = NULL;

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

static LRESULT CALLBACK backdrop_wndproc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    if (msg == WM_ERASEBKGND)
    {
        RECT rc;
        GetClientRect(hwnd, &rc);
        FillRect((HDC)wp, &rc, (HBRUSH)GetStockObject(WHITE_BRUSH));
        return 1;
    }
    if (msg == WM_PAINT)
    {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);
        RECT rc;
        GetClientRect(hwnd, &rc);
        FillRect(hdc, &rc, (HBRUSH)GetStockObject(WHITE_BRUSH));
        EndPaint(hwnd, &ps);
        return 0;
    }
    return DefWindowProcA(hwnd, msg, wp, lp);
}

static LRESULT CALLBACK test_wndproc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    if (msg == WM_ERASEBKGND)
    {
        RECT rc;
        GetClientRect(hwnd, &rc);
        FillRect((HDC)wp, &rc, (HBRUSH)GetStockObject(BLACK_BRUSH));
        return 1;
    }
    if (msg == WM_PAINT)
    {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);
        RECT rc;
        GetClientRect(hwnd, &rc);
        FillRect(hdc, &rc, (HBRUSH)GetStockObject(BLACK_BRUSH));
        EndPaint(hwnd, &ps);
        return 0;
    }
    return DefWindowProcA(hwnd, msg, wp, lp);
}

static int pixel_luma(const DWORD *bits, int stride, int w, int h, int x, int y)
{
    DWORD px;
    int r, g, b;
    if (x < 0 || y < 0 || x >= w || y >= h)
        return 255;
    px = bits[y * stride + x];
    r = (int)((px >> 16) & 0xff);
    g = (int)((px >> 8) & 0xff);
    b = (int)(px & 0xff);
    return (r + g + b) / 3;
}

static void measure_case(HWND hwnd_bg, HWND hwnd_test, DWORD corner_pref, int active, UINT dpi)
{
    HRESULT hr_set = NOT_FOUND, hr_ext = NOT_FOUND;
    RECT rc_ext = {0};
    int scr_w = GetSystemMetrics(SM_CXSCREEN);
    int scr_h = GetSystemMetrics(SM_CYSCREEN);
    HDC hdc_scr, hdc_mem;
    BITMAPINFO bmi;
    void *dib_bits = NULL;
    HBITMAP hdib, old_bmp;
    const DWORD *pixels;
    int d, side_d;
    int diag_steps = 0, edge_radius = 0;
    double diag_dist = 0.0;
    int arc_cov[8] = {0};
    int sh_left = 0, sh_right = 0, sh_top = 0, sh_bottom = 0;
    int darkest_val = 255, darkest_dist = 0;
    int mid_x, mid_y, c_x, c_y;
    double vert_offset;

    if (pDwmSetWindowAttribute)
        hr_set = pDwmSetWindowAttribute(hwnd_test, DWMWA_WINDOW_CORNER_PREFERENCE,
                                        &corner_pref, sizeof(corner_pref));

    if (active)
    {
        SetWindowPos(hwnd_bg, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
        SetWindowPos(hwnd_test, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
        SetForegroundWindow(hwnd_test);
        SetActiveWindow(hwnd_test);
    }
    else
    {
        SetForegroundWindow(hwnd_bg);
        SetActiveWindow(hwnd_bg);
        SetWindowPos(hwnd_test, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    }

    InvalidateRect(hwnd_bg, NULL, TRUE);
    UpdateWindow(hwnd_bg);
    InvalidateRect(hwnd_test, NULL, TRUE);
    UpdateWindow(hwnd_test);
    pump_messages();
    if (pDwmFlush)
        pDwmFlush();

    Sleep(400);
    pump_messages();

    if (pDwmGetWindowAttribute)
        hr_ext = pDwmGetWindowAttribute(hwnd_test, DWMWA_EXTENDED_FRAME_BOUNDS, &rc_ext, sizeof(rc_ext));
    if (FAILED(hr_ext) || rc_ext.right <= rc_ext.left)
        GetWindowRect(hwnd_test, &rc_ext);

    hdc_scr = GetDC(NULL);
    hdc_mem = CreateCompatibleDC(hdc_scr);
    memset(&bmi, 0, sizeof(bmi));
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = scr_w;
    bmi.bmiHeader.biHeight = -scr_h;
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;

    hdib = CreateDIBSection(hdc_scr, &bmi, DIB_RGB_COLORS, &dib_bits, NULL, 0);
    old_bmp = (HBITMAP)SelectObject(hdc_mem, hdib);
    BitBlt(hdc_mem, 0, 0, scr_w, scr_h, hdc_scr, 0, 0, SRCCOPY | CAPTUREBLT);
    GdiFlush();

    pixels = (const DWORD *)dib_bits;
    c_x = rc_ext.left;
    c_y = rc_ext.bottom - 1;

    /* Walk inward along bottom-left diagonal (c_x + d, c_y - d) to find 50% coverage crossing */
    diag_steps = 0;
    for (d = 0; d < 24; d++)
    {
        int lum = pixel_luma(pixels, scr_w, scr_w, scr_h, c_x + d, c_y - d);
        int cov = 255 - lum;
        if (cov >= 128)
        {
            diag_steps = d;
            break;
        }
    }
    diag_dist = (double)diag_steps * 1.41421356;

    /* Walk along bottom edge (c_x + d, c_y - 1) to find tangent radius (<50% coverage) */
    edge_radius = 0;
    for (d = 24; d >= 0; d--)
    {
        int lum = pixel_luma(pixels, scr_w, scr_w, scr_h, c_x + d, c_y - 1);
        if (255 - lum < 128)
        {
            edge_radius = d + 1;
            break;
        }
    }

    /* Sample alpha-equivalent coverage (0..255) along the corner arc / diagonal */
    for (d = 0; d < 8; d++)
    {
        int lum = pixel_luma(pixels, scr_w, scr_w, scr_h, c_x + d, c_y - d);
        arc_cov[d] = 255 - lum;
    }

    mid_x = (rc_ext.left + rc_ext.right) / 2;
    mid_y = (rc_ext.top + rc_ext.bottom) / 2;

    for (side_d = 1; side_d <= MAX_SHADOW_SCAN; side_d++)
    {
        int lum = pixel_luma(pixels, scr_w, scr_w, scr_h, rc_ext.left - side_d, mid_y);
        if (lum < darkest_val) { darkest_val = lum; darkest_dist = side_d; }
        if (sh_left == 0 && lum >= 253) sh_left = side_d;
    }
    for (side_d = 1; side_d <= MAX_SHADOW_SCAN; side_d++)
    {
        int lum = pixel_luma(pixels, scr_w, scr_w, scr_h, rc_ext.right - 1 + side_d, mid_y);
        if (lum < darkest_val) { darkest_val = lum; darkest_dist = side_d; }
        if (sh_right == 0 && lum >= 253) sh_right = side_d;
    }
    for (side_d = 1; side_d <= MAX_SHADOW_SCAN; side_d++)
    {
        int lum = pixel_luma(pixels, scr_w, scr_w, scr_h, mid_x, rc_ext.top - side_d);
        if (lum < darkest_val) { darkest_val = lum; darkest_dist = side_d; }
        if (sh_top == 0 && lum >= 253) sh_top = side_d;
    }
    for (side_d = 1; side_d <= MAX_SHADOW_SCAN; side_d++)
    {
        int lum = pixel_luma(pixels, scr_w, scr_w, scr_h, mid_x, rc_ext.bottom - 1 + side_d);
        if (lum < darkest_val) { darkest_val = lum; darkest_dist = side_d; }
        if (sh_bottom == 0 && lum >= 253) sh_bottom = side_d;
    }

    vert_offset = (double)(sh_bottom - sh_top) / 2.0;

    emit_line("corner_pref=%lu active=%d dpi=%u set_hr=0x%08lx "
              "ext_l=%ld ext_t=%ld ext_r=%ld ext_b=%ld "
              "corner_radius_px=%d diag_steps=%d diag_dist_px=%.2f "
              "arc_cov0=%d arc_cov1=%d arc_cov2=%d arc_cov3=%d "
              "arc_cov4=%d arc_cov5=%d arc_cov6=%d arc_cov7=%d "
              "shadow_left=%d shadow_right=%d shadow_top=%d shadow_bottom=%d "
              "darkest_val=%d darkest_dist=%d vert_offset_est=%.1f\n",
              (unsigned long)corner_pref, active, dpi, (unsigned long)hr_set,
              rc_ext.left, rc_ext.top, rc_ext.right, rc_ext.bottom,
              edge_radius, diag_steps, diag_dist,
              arc_cov[0], arc_cov[1], arc_cov[2], arc_cov[3],
              arc_cov[4], arc_cov[5], arc_cov[6], arc_cov[7],
              sh_left, sh_right, sh_top, sh_bottom,
              darkest_val, darkest_dist, vert_offset);

    SelectObject(hdc_mem, old_bmp);
    DeleteObject(hdib);
    DeleteDC(hdc_mem);
    ReleaseDC(NULL, hdc_scr);
}

int main(void)
{
    static const DWORD prefs[4] = {0, 1, 2, 3};
    HMODULE user32, dwmapi;
    pfn_SetThreadDpiAwarenessContext pSetDpiCtx = NULL;
    pfn_GetDpiForWindow pGetDpi = NULL;
    WNDCLASSA wc_bg, wc_test;
    HWND hwnd_bg, hwnd_test;
    DWORD black_col = 0x00000000;
    BOOL dark_on = TRUE;
    UINT dpi = 96;
    int scr_w, scr_h, i, act;

    init_output("Z:\\tmp\\dwm-corner-shadow-probe.txt");

    user32 = GetModuleHandleA("user32.dll");
    dwmapi = LoadLibraryA("dwmapi.dll");
    if (user32)
    {
        pSetDpiCtx = (pfn_SetThreadDpiAwarenessContext)(void *)
            GetProcAddress(user32, "SetThreadDpiAwarenessContext");
        pGetDpi = (pfn_GetDpiForWindow)(void *)GetProcAddress(user32, "GetDpiForWindow");
    }
    if (dwmapi)
    {
        pDwmGetWindowAttribute = (pfn_DwmGetWindowAttribute)(void *)
            GetProcAddress(dwmapi, "DwmGetWindowAttribute");
        pDwmSetWindowAttribute = (pfn_DwmSetWindowAttribute)(void *)
            GetProcAddress(dwmapi, "DwmSetWindowAttribute");
        pDwmFlush = (pfn_DwmFlush)(void *)GetProcAddress(dwmapi, "DwmFlush");
    }

    if (pSetDpiCtx)
        pSetDpiCtx(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

    scr_w = GetSystemMetrics(SM_CXSCREEN);
    scr_h = GetSystemMetrics(SM_CYSCREEN);

    memset(&wc_bg, 0, sizeof(wc_bg));
    wc_bg.lpfnWndProc = backdrop_wndproc;
    wc_bg.hInstance = GetModuleHandleA(NULL);
    wc_bg.lpszClassName = "dwm_cs_backdrop_cls";
    wc_bg.hbrBackground = (HBRUSH)GetStockObject(WHITE_BRUSH);
    RegisterClassA(&wc_bg);

    memset(&wc_test, 0, sizeof(wc_test));
    wc_test.lpfnWndProc = test_wndproc;
    wc_test.hInstance = wc_bg.hInstance;
    wc_test.lpszClassName = "dwm_cs_test_cls";
    wc_test.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);
    RegisterClassA(&wc_test);

    hwnd_bg = CreateWindowExA(0, wc_bg.lpszClassName, "", WS_POPUP | WS_VISIBLE,
                              0, 0, scr_w, scr_h, NULL, NULL, wc_bg.hInstance, NULL);
    hwnd_test = CreateWindowExA(0, wc_test.lpszClassName, "",
                                WS_OVERLAPPED | WS_CAPTION | WS_THICKFRAME | WS_VISIBLE,
                                300, 300, 400, 300, hwnd_bg, NULL, wc_bg.hInstance, NULL);

    if (pDwmSetWindowAttribute && hwnd_test)
    {
        pDwmSetWindowAttribute(hwnd_test, DWMWA_USE_IMMERSIVE_DARK_MODE, &dark_on, sizeof(dark_on));
        pDwmSetWindowAttribute(hwnd_test, DWMWA_CAPTION_COLOR, &black_col, sizeof(black_col));
        pDwmSetWindowAttribute(hwnd_test, DWMWA_BORDER_COLOR, &black_col, sizeof(black_col));
        pDwmSetWindowAttribute(hwnd_test, DWMWA_TEXT_COLOR, &black_col, sizeof(black_col));
    }

    if (pGetDpi && hwnd_test)
        dpi = pGetDpi(hwnd_test);

    ShowWindow(hwnd_bg, SW_SHOW);
    ShowWindow(hwnd_test, SW_SHOW);
    pump_messages();

    for (i = 0; i < 4; i++)
    {
        for (act = 1; act >= 0; act--)
            measure_case(hwnd_bg, hwnd_test, prefs[i], act, dpi);
    }

    DestroyWindow(hwnd_test);
    DestroyWindow(hwnd_bg);
    if (dwmapi)
        FreeLibrary(dwmapi);
    if (g_con_out)
        fclose(g_con_out);
    return 0;
}

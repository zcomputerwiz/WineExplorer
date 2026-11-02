/*
 * WineExplorer - DWM system backdrop (Mica/Acrylic) and colorization probe
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

#ifndef DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2
#define DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2 ((HANDLE)(LONG_PTR)-4)
#endif

#ifndef CAPTUREBLT
#define CAPTUREBLT 0x40000000
#endif

#ifndef DWMWA_USE_IMMERSIVE_DARK_MODE
#define DWMWA_USE_IMMERSIVE_DARK_MODE 20
#endif
#ifndef DWMWA_SYSTEMBACKDROP_TYPE
#define DWMWA_SYSTEMBACKDROP_TYPE 38
#endif

#define NOT_FOUND ((HRESULT)0x8007007f)

typedef HRESULT (WINAPI *pfn_DwmGetColorizationColor)(DWORD *, BOOL *);
typedef HRESULT (WINAPI *pfn_DwmGetWindowAttribute)(HWND, DWORD, PVOID, DWORD);
typedef HRESULT (WINAPI *pfn_DwmSetWindowAttribute)(HWND, DWORD, LPCVOID, DWORD);
typedef HRESULT (WINAPI *pfn_DwmExtendFrameIntoClientArea)(HWND, const MARGINS *);
typedef HRESULT (WINAPI *pfn_DwmFlush)(void);
typedef HANDLE (WINAPI *pfn_SetThreadDpiAwarenessContext)(HANDLE);

static const MARGINS g_margins_neg1 = {-1, -1, -1, -1};

static FILE *g_con_out = NULL;
static FILE *g_file_out = NULL;
static pfn_DwmGetColorizationColor pDwmGetColorizationColor = NULL;
static pfn_DwmGetWindowAttribute pDwmGetWindowAttribute = NULL;
static pfn_DwmSetWindowAttribute pDwmSetWindowAttribute = NULL;
static pfn_DwmExtendFrameIntoClientArea pDwmExtendFrameIntoClientArea = NULL;
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

static void wait_compose(int ms)
{
    int i;
    for (i = 0; i < ms / 10; i++)
    {
        Sleep(10);
        pump_messages();
    }
}

static void sample_region(int left, int top, int w, int h,
                          int *pt_r, int *pt_g, int *pt_b,
                          double *mean_r, double *mean_g, double *mean_b,
                          double *lq_r, double *lq_g, double *lq_b,
                          double *rq_r, double *rq_g, double *rq_b)
{
    HDC hdc_scr, hdc_mem;
    BITMAPINFO bmi;
    void *dib_bits = NULL;
    HBITMAP hdib, old_bmp;
    const DWORD *pixels;
    int x, y, i;
    double sum_r = 0.0, sum_g = 0.0, sum_b = 0.0, total_n = 0.0;
    double l_sum_r = 0.0, l_sum_g = 0.0, l_sum_b = 0.0, l_n = 0.0;
    double r_sum_r = 0.0, r_sum_g = 0.0, r_sum_b = 0.0, r_n = 0.0;

    hdc_scr = GetDC(NULL);
    hdc_mem = CreateCompatibleDC(hdc_scr);
    memset(&bmi, 0, sizeof(bmi));
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = w;
    bmi.bmiHeader.biHeight = -h;
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;

    hdib = CreateDIBSection(hdc_scr, &bmi, DIB_RGB_COLORS, &dib_bits, NULL, 0);
    old_bmp = (HBITMAP)SelectObject(hdc_mem, hdib);
    BitBlt(hdc_mem, 0, 0, w, h, hdc_scr, left, top, SRCCOPY | CAPTUREBLT);
    GdiFlush();

    pixels = (const DWORD *)dib_bits;
    for (i = 0; i < 9; i++)
    {
        int gx = ((i % 3) + 1) * w / 4;
        int gy = ((i / 3) + 1) * h / 4;
        DWORD px = pixels[gy * w + gx];
        pt_r[i] = (int)((px >> 16) & 0xff);
        pt_g[i] = (int)((px >> 8) & 0xff);
        pt_b[i] = (int)(px & 0xff);
    }

    for (y = 0; y < h; y++)
    {
        for (x = 0; x < w; x++)
        {
            DWORD px = pixels[y * w + x];
            int r = (int)((px >> 16) & 0xff);
            int g = (int)((px >> 8) & 0xff);
            int b = (int)(px & 0xff);
            sum_r += r;
            sum_g += g;
            sum_b += b;
            total_n += 1.0;
            if (x < w / 4)
            {
                l_sum_r += r;
                l_sum_g += g;
                l_sum_b += b;
                l_n += 1.0;
            }
            else if (x >= (3 * w) / 4)
            {
                r_sum_r += r;
                r_sum_g += g;
                r_sum_b += b;
                r_n += 1.0;
            }
        }
    }

    if (total_n < 1.0) total_n = 1.0;
    if (l_n < 1.0) l_n = 1.0;
    if (r_n < 1.0) r_n = 1.0;

    *mean_r = sum_r / total_n;
    *mean_g = sum_g / total_n;
    *mean_b = sum_b / total_n;
    *lq_r = l_sum_r / l_n;
    *lq_g = l_sum_g / l_n;
    *lq_b = l_sum_b / l_n;
    *rq_r = r_sum_r / r_n;
    *rq_g = r_sum_g / r_n;
    *rq_b = r_sum_b / r_n;

    SelectObject(hdc_mem, old_bmp);
    DeleteObject(hdib);
    DeleteDC(hdc_mem);
    ReleaseDC(NULL, hdc_scr);
}

static void paint_calibrated_backdrop(HWND hwnd, HDC hdc)
{
    RECT rc, r_left, r_right, r_cell;
    HBRUSH br_red, br_blue, br_white, br_black;
    int w, h, x, y, strip_y0, strip_y1;

    GetClientRect(hwnd, &rc);
    w = rc.right - rc.left;
    h = rc.bottom - rc.top;

    br_red = CreateSolidBrush(RGB(255, 0, 0));
    br_blue = CreateSolidBrush(RGB(0, 0, 255));
    br_white = (HBRUSH)GetStockObject(WHITE_BRUSH);
    br_black = (HBRUSH)GetStockObject(BLACK_BRUSH);

    r_left.left = 0;
    r_left.top = 0;
    r_left.right = w / 2;
    r_left.bottom = h;
    FillRect(hdc, &r_left, br_red);

    r_right.left = w / 2;
    r_right.top = 0;
    r_right.right = w;
    r_right.bottom = h;
    FillRect(hdc, &r_right, br_blue);

    /* 16 px checkerboard strip along the top */
    strip_y0 = 0;
    strip_y1 = 16;
    for (y = strip_y0; y < strip_y1; y += 16)
    {
        for (x = 0; x < w; x += 16)
        {
            int odd = (((x / 16) + ((y - strip_y0) / 16)) & 1);
            r_cell.left = x;
            r_cell.top = y;
            r_cell.right = (x + 16 < w) ? (x + 16) : w;
            r_cell.bottom = (y + 16 < strip_y1) ? (y + 16) : strip_y1;
            FillRect(hdc, &r_cell, odd ? br_white : br_black);
        }
    }

    DeleteObject(br_red);
    DeleteObject(br_blue);
}

static LRESULT CALLBACK bg_wndproc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    if (msg == WM_ERASEBKGND)
    {
        paint_calibrated_backdrop(hwnd, (HDC)wp);
        return 1;
    }
    if (msg == WM_PAINT)
    {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);
        paint_calibrated_backdrop(hwnd, hdc);
        EndPaint(hwnd, &ps);
        return 0;
    }
    return DefWindowProcA(hwnd, msg, wp, lp);
}

static LRESULT CALLBACK fg_wndproc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
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

static void run_backdrop_case(HWND hwnd_bg, HWND hwnd_fg, DWORD sbt, BOOL dark_mode)
{
    HRESULT hr_dark = NOT_FOUND, hr_sbt = NOT_FOUND, hr_ext = NOT_FOUND;
    DWORD read_sbt = 0;
    RECT rc_cli = {0};
    int cw, ch;
    int pt_r[9] = {0}, pt_g[9] = {0}, pt_b[9] = {0};
    int ref_r[9] = {0}, ref_g[9] = {0}, ref_b[9] = {0};
    double mean_r = 0.0, mean_g = 0.0, mean_b = 0.0;
    double lq_r = 0.0, lq_g = 0.0, lq_b = 0.0;
    double rq_r = 0.0, rq_g = 0.0, rq_b = 0.0;
    double ref_mean_r = 0.0, ref_mean_g = 0.0, ref_mean_b = 0.0;
    double ref_lq_r = 0.0, ref_lq_g = 0.0, ref_lq_b = 0.0;
    double ref_rq_r = 0.0, ref_rq_g = 0.0, ref_rq_b = 0.0;

    if (pDwmSetWindowAttribute)
    {
        hr_dark = pDwmSetWindowAttribute(hwnd_fg, DWMWA_USE_IMMERSIVE_DARK_MODE,
                                         &dark_mode, sizeof(dark_mode));
        hr_sbt = pDwmSetWindowAttribute(hwnd_fg, DWMWA_SYSTEMBACKDROP_TYPE,
                                        &sbt, sizeof(sbt));
    }
    if (pDwmGetWindowAttribute)
        pDwmGetWindowAttribute(hwnd_fg, DWMWA_SYSTEMBACKDROP_TYPE, &read_sbt, sizeof(read_sbt));
    /* Re-extend the frame after the backdrop attributes change. */
    if (pDwmExtendFrameIntoClientArea)
        hr_ext = pDwmExtendFrameIntoClientArea(hwnd_fg, &g_margins_neg1);

    /* Keep both windows topmost, backdrop behind the test window. */
    SetWindowPos(hwnd_bg, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    SetWindowPos(hwnd_fg, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
    ShowWindow(hwnd_fg, SW_SHOW);
    SetForegroundWindow(hwnd_fg);
    SetActiveWindow(hwnd_fg);
    InvalidateRect(hwnd_bg, NULL, TRUE);
    UpdateWindow(hwnd_bg);
    InvalidateRect(hwnd_fg, NULL, TRUE);
    UpdateWindow(hwnd_fg);
    pump_messages();
    if (pDwmFlush)
        pDwmFlush();

    /* Let DWM composite the system backdrop before sampling. */
    wait_compose(600);

    GetClientRect(hwnd_fg, &rc_cli);
    MapWindowPoints(hwnd_fg, NULL, (POINT *)&rc_cli, 2);
    cw = rc_cli.right - rc_cli.left;
    ch = rc_cli.bottom - rc_cli.top;
    if (cw <= 0 || ch <= 0)
        return;

    sample_region(rc_cli.left, rc_cli.top, cw, ch,
                  pt_r, pt_g, pt_b,
                  &mean_r, &mean_g, &mean_b,
                  &lq_r, &lq_g, &lq_b,
                  &rq_r, &rq_g, &rq_b);

    /* Reference: same 9 points with the test window hidden (backdrop window alone). */
    ShowWindow(hwnd_fg, SW_HIDE);
    pump_messages();
    if (pDwmFlush)
        pDwmFlush();
    wait_compose(600);
    sample_region(rc_cli.left, rc_cli.top, cw, ch,
                  ref_r, ref_g, ref_b,
                  &ref_mean_r, &ref_mean_g, &ref_mean_b,
                  &ref_lq_r, &ref_lq_g, &ref_lq_b,
                  &ref_rq_r, &ref_rq_g, &ref_rq_b);
    ShowWindow(hwnd_fg, SW_SHOW);
    pump_messages();
    if (pDwmFlush)
        pDwmFlush();
    wait_compose(600);

    emit_line("backdrop=%lu read_backdrop=%lu dark_mode=%d "
              "sbt_hr=0x%08lx dark_hr=0x%08lx ext_hr=0x%08lx "
              "p0_r=%d p0_g=%d p0_b=%d p1_r=%d p1_g=%d p1_b=%d p2_r=%d p2_g=%d p2_b=%d "
              "p3_r=%d p3_g=%d p3_b=%d p4_r=%d p4_g=%d p4_b=%d p5_r=%d p5_g=%d p5_b=%d "
              "p6_r=%d p6_g=%d p6_b=%d p7_r=%d p7_g=%d p7_b=%d p8_r=%d p8_g=%d p8_b=%d "
              "mean_r=%.2f mean_g=%.2f mean_b=%.2f "
              "left_q_r=%.2f left_q_g=%.2f left_q_b=%.2f "
              "right_q_r=%.2f right_q_g=%.2f right_q_b=%.2f "
              "grad_r=%.2f grad_g=%.2f grad_b=%.2f "
              "ref_p0_r=%d ref_p0_g=%d ref_p0_b=%d "
              "ref_p1_r=%d ref_p1_g=%d ref_p1_b=%d "
              "ref_p2_r=%d ref_p2_g=%d ref_p2_b=%d "
              "ref_p3_r=%d ref_p3_g=%d ref_p3_b=%d "
              "ref_p4_r=%d ref_p4_g=%d ref_p4_b=%d "
              "ref_p5_r=%d ref_p5_g=%d ref_p5_b=%d "
              "ref_p6_r=%d ref_p6_g=%d ref_p6_b=%d "
              "ref_p7_r=%d ref_p7_g=%d ref_p7_b=%d "
              "ref_p8_r=%d ref_p8_g=%d ref_p8_b=%d\n",
              (unsigned long)sbt, (unsigned long)read_sbt, (int)dark_mode,
              (unsigned long)hr_sbt, (unsigned long)hr_dark, (unsigned long)hr_ext,
              pt_r[0], pt_g[0], pt_b[0], pt_r[1], pt_g[1], pt_b[1], pt_r[2], pt_g[2], pt_b[2],
              pt_r[3], pt_g[3], pt_b[3], pt_r[4], pt_g[4], pt_b[4], pt_r[5], pt_g[5], pt_b[5],
              pt_r[6], pt_g[6], pt_b[6], pt_r[7], pt_g[7], pt_b[7], pt_r[8], pt_g[8], pt_b[8],
              mean_r, mean_g, mean_b,
              lq_r, lq_g, lq_b,
              rq_r, rq_g, rq_b,
              rq_r - lq_r,
              rq_g - lq_g,
              rq_b - lq_b,
              ref_r[0], ref_g[0], ref_b[0],
              ref_r[1], ref_g[1], ref_b[1],
              ref_r[2], ref_g[2], ref_b[2],
              ref_r[3], ref_g[3], ref_b[3],
              ref_r[4], ref_g[4], ref_b[4],
              ref_r[5], ref_g[5], ref_b[5],
              ref_r[6], ref_g[6], ref_b[6],
              ref_r[7], ref_g[7], ref_b[7],
              ref_r[8], ref_g[8], ref_b[8]);
}

int main(void)
{
    static const DWORD backdrops[4] = {2, 3, 4, 1};
    HMODULE user32, dwmapi;
    pfn_SetThreadDpiAwarenessContext pSetDpiCtx = NULL;
    WNDCLASSA wc_bg, wc_fg;
    HWND hwnd_bg, hwnd_fg;
    DWORD col_val = 0;
    BOOL col_opaque = FALSE;
    HRESULT hr_col = NOT_FOUND;
    int i, d, sw, sh;

    init_output("Z:\\tmp\\dwm-backdrop-color-probe.txt");

    user32 = GetModuleHandleA("user32.dll");
    dwmapi = LoadLibraryA("dwmapi.dll");
    if (user32)
        pSetDpiCtx = (pfn_SetThreadDpiAwarenessContext)(void *)
            GetProcAddress(user32, "SetThreadDpiAwarenessContext");
    if (dwmapi)
    {
        pDwmGetColorizationColor = (pfn_DwmGetColorizationColor)(void *)
            GetProcAddress(dwmapi, "DwmGetColorizationColor");
        pDwmGetWindowAttribute = (pfn_DwmGetWindowAttribute)(void *)
            GetProcAddress(dwmapi, "DwmGetWindowAttribute");
        pDwmSetWindowAttribute = (pfn_DwmSetWindowAttribute)(void *)
            GetProcAddress(dwmapi, "DwmSetWindowAttribute");
        pDwmExtendFrameIntoClientArea = (pfn_DwmExtendFrameIntoClientArea)(void *)
            GetProcAddress(dwmapi, "DwmExtendFrameIntoClientArea");
        pDwmFlush = (pfn_DwmFlush)(void *)GetProcAddress(dwmapi, "DwmFlush");
    }

    if (pSetDpiCtx)
        pSetDpiCtx(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

    if (pDwmGetColorizationColor)
        hr_col = pDwmGetColorizationColor(&col_val, &col_opaque);
    emit_line("colorization_hr=0x%08lx colorization_color=0x%08lx colorization_opaque=%d\n",
              (unsigned long)hr_col, (unsigned long)col_val, (int)col_opaque);

    memset(&wc_bg, 0, sizeof(wc_bg));
    wc_bg.lpfnWndProc = bg_wndproc;
    wc_bg.hInstance = GetModuleHandleA(NULL);
    wc_bg.lpszClassName = "dwm_bc_bg_cls";
    RegisterClassA(&wc_bg);

    memset(&wc_fg, 0, sizeof(wc_fg));
    wc_fg.lpfnWndProc = fg_wndproc;
    wc_fg.hInstance = wc_bg.hInstance;
    wc_fg.lpszClassName = "dwm_bc_fg_cls";
    wc_fg.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);
    RegisterClassA(&wc_fg);

    sw = GetSystemMetrics(SM_CXSCREEN);
    sh = GetSystemMetrics(SM_CYSCREEN);

    /* Fullscreen calibrated backdrop window; the test window is centered on it so
       its client area straddles the red/blue split. Both are top-level so the
       HWND_TOPMOST z-order (backdrop behind, test window above) is well defined. */
    hwnd_bg = CreateWindowExA(0, wc_bg.lpszClassName, "", WS_POPUP,
                              0, 0, sw, sh, NULL, NULL, wc_bg.hInstance, NULL);
    hwnd_fg = CreateWindowExA(0, wc_fg.lpszClassName, "", WS_OVERLAPPEDWINDOW,
                              (sw - 400) / 2, (sh - 300) / 2, 400, 300,
                              NULL, NULL, wc_bg.hInstance, NULL);

    /* Extend the frame before the window is shown so DWM treats the black
       client pixels as transparent backdrop areas. */
    if (pDwmExtendFrameIntoClientArea)
        pDwmExtendFrameIntoClientArea(hwnd_fg, &g_margins_neg1);

    SetWindowPos(hwnd_bg, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
    SetWindowPos(hwnd_fg, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
    ShowWindow(hwnd_bg, SW_SHOW);
    ShowWindow(hwnd_fg, SW_SHOW);
    wait_compose(600);

    for (i = 0; i < 4; i++)
    {
        for (d = 0; d <= 1; d++)
            run_backdrop_case(hwnd_bg, hwnd_fg, backdrops[i], (BOOL)d);
    }

    DestroyWindow(hwnd_fg);
    DestroyWindow(hwnd_bg);
    if (dwmapi)
        FreeLibrary(dwmapi);
    if (g_con_out)
        fclose(g_con_out);
    return 0;
}

/*
 * WineExplorer - DWM composition timing, flush latency, thumbnail size, and iconic probe
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

#ifndef WM_DWMSENDICONICTHUMBNAIL
#define WM_DWMSENDICONICTHUMBNAIL 0x0323
#endif
#ifndef WM_DWMSENDICONICLIVEPREVIEWBITMAP
#define WM_DWMSENDICONICLIVEPREVIEWBITMAP 0x0326
#endif
#ifndef DWMWA_FORCE_ICONIC_REPRESENTATION
#define DWMWA_FORCE_ICONIC_REPRESENTATION 7
#endif
#ifndef DWMWA_HAS_ICONIC_BITMAP
#define DWMWA_HAS_ICONIC_BITMAP 10
#endif
#ifndef DWMWA_CLOAK
#define DWMWA_CLOAK 13
#endif
#ifndef DWMWA_CLOAKED
#define DWMWA_CLOAKED 14
#endif

#define NOT_FOUND ((HRESULT)0x8007007f)
#define MAX_ICONIC_LOGS 64

typedef HRESULT (WINAPI *pfn_DwmGetCompositionTimingInfo)(HWND, DWM_TIMING_INFO *);
typedef HRESULT (WINAPI *pfn_DwmFlush)(void);
typedef HRESULT (WINAPI *pfn_DwmRegisterThumbnail)(HWND, HWND, PHTHUMBNAIL);
typedef HRESULT (WINAPI *pfn_DwmUpdateThumbnailProperties)(HTHUMBNAIL, const DWM_THUMBNAIL_PROPERTIES *);
typedef HRESULT (WINAPI *pfn_DwmQueryThumbnailSourceSize)(HTHUMBNAIL, PSIZE);
typedef HRESULT (WINAPI *pfn_DwmUnregisterThumbnail)(HTHUMBNAIL);
typedef HRESULT (WINAPI *pfn_DwmGetWindowAttribute)(HWND, DWORD, PVOID, DWORD);
typedef HRESULT (WINAPI *pfn_DwmSetWindowAttribute)(HWND, DWORD, LPCVOID, DWORD);
typedef HRESULT (WINAPI *pfn_DwmSetIconicThumbnail)(HWND, HBITMAP, DWORD);
typedef HRESULT (WINAPI *pfn_DwmSetIconicLivePreviewBitmap)(HWND, HBITMAP, POINT *, DWORD);
typedef HRESULT (WINAPI *pfn_DwmInvalidateIconicBitmaps)(HWND);

struct iconic_log_entry {
    int seq;
    UINT msg, hiword, loword;
    DWORD timestamp_ms;
    HRESULT reply_hr;
};

static FILE *g_con_out = NULL;
static FILE *g_file_out = NULL;
static pfn_DwmGetCompositionTimingInfo pDwmGetCompositionTimingInfo = NULL;
static pfn_DwmFlush pDwmFlush = NULL;
static pfn_DwmRegisterThumbnail pDwmRegisterThumbnail = NULL;
static pfn_DwmUpdateThumbnailProperties pDwmUpdateThumbnailProperties = NULL;
static pfn_DwmQueryThumbnailSourceSize pDwmQueryThumbnailSourceSize = NULL;
static pfn_DwmUnregisterThumbnail pDwmUnregisterThumbnail = NULL;
static pfn_DwmGetWindowAttribute pDwmGetWindowAttribute = NULL;
static pfn_DwmSetWindowAttribute pDwmSetWindowAttribute = NULL;
static pfn_DwmSetIconicThumbnail pDwmSetIconicThumbnail = NULL;
static pfn_DwmSetIconicLivePreviewBitmap pDwmSetIconicLivePreviewBitmap = NULL;
static pfn_DwmInvalidateIconicBitmaps pDwmInvalidateIconicBitmaps = NULL;

static struct iconic_log_entry g_logs[MAX_ICONIC_LOGS];
static int g_log_count = 0;
static DWORD g_start_tick = 0;
static HBITMAP g_iconic_bmp = NULL;

static void init_output(const char *path)
{
    int fd = _dup(_fileno(stdout));
    if (fd >= 0)
    {
        g_con_out = _fdopen(fd, "w");
        if (g_con_out) setvbuf(g_con_out, NULL, _IONBF, 0);
    }
    CreateDirectoryA("Z:\\tmp", NULL);
    g_file_out = freopen(path, "w", stdout);
    if (g_file_out) setvbuf(stdout, NULL, _IONBF, 0);
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

static HBITMAP create_dib_128x96(int w, int h)
{
    BITMAPINFO bmi;
    void *bits = NULL;
    HDC hdc = GetDC(NULL);
    HBITMAP hbmp;
    int i, total = w * h;

    memset(&bmi, 0, sizeof(bmi));
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = w;
    bmi.bmiHeader.biHeight = -h;
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;

    hbmp = CreateDIBSection(hdc, &bmi, DIB_RGB_COLORS, &bits, NULL, 0);
    if (hdc) ReleaseDC(NULL, hdc);
    if (hbmp && bits)
    {
        DWORD *px = (DWORD *)bits;
        for (i = 0; i < total; i++) px[i] = 0xff2060c0u;
    }
    return hbmp;
}

static LRESULT CALLBACK probe_wndproc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    if (msg == WM_DWMSENDICONICTHUMBNAIL || msg == WM_DWMSENDICONICLIVEPREVIEWBITMAP)
    {
        UINT hi = (UINT)HIWORD(lp), lo = (UINT)LOWORD(lp);
        HRESULT hr = NOT_FOUND;
        POINT pt = {0, 0};

        if (msg == WM_DWMSENDICONICTHUMBNAIL && pDwmSetIconicThumbnail)
        {
            hr = pDwmSetIconicThumbnail(hwnd, g_iconic_bmp, 0);
            if (FAILED(hr) && hi > 0 && lo > 0)
            {
                int cw = (hi < 128) ? (int)hi : 128;
                int ch = (lo < 96) ? (int)lo : 96;
                HBITMAP hsub = create_dib_128x96(cw, ch);
                if (hsub)
                {
                    hr = pDwmSetIconicThumbnail(hwnd, hsub, 0);
                    DeleteObject(hsub);
                }
            }
        }
        else if (msg == WM_DWMSENDICONICLIVEPREVIEWBITMAP && pDwmSetIconicLivePreviewBitmap)
        {
            hr = pDwmSetIconicLivePreviewBitmap(hwnd, g_iconic_bmp, &pt, 0);
        }

        if (g_log_count < MAX_ICONIC_LOGS)
        {
            g_logs[g_log_count].seq = g_log_count;
            g_logs[g_log_count].msg = msg;
            g_logs[g_log_count].hiword = hi;
            g_logs[g_log_count].loword = lo;
            g_logs[g_log_count].timestamp_ms = GetTickCount() - g_start_tick;
            g_logs[g_log_count].reply_hr = hr;
            g_log_count++;
        }
        return 0;
    }
    return DefWindowProcA(hwnd, msg, wp, lp);
}

static HRESULT query_timing_info(HWND hwnd, DWM_TIMING_INFO *ti)
{
    HRESULT hr = NOT_FOUND;
    memset(ti, 0, sizeof(*ti));
    ti->cbSize = sizeof(*ti);
    if (!pDwmGetCompositionTimingInfo) return NOT_FOUND;
    hr = pDwmGetCompositionTimingInfo(NULL, ti);
    if (FAILED(hr))
    {
        memset(ti, 0, sizeof(*ti));
        ti->cbSize = sizeof(*ti);
        hr = pDwmGetCompositionTimingInfo(hwnd, ti);
    }
    return hr;
}

static void print_timing_info(int sample_idx, HRESULT hr, const DWM_TIMING_INFO *ti,
                              long long delta_refresh, long long delta_frame)
{
    emit_line("timing_sample=%d hr=0x%08lx cbSize=%u rateRefresh_num=%u rateRefresh_den=%u "
              "qpcRefreshPeriod=%llu rateCompose_num=%u rateCompose_den=%u qpcVBlank=%llu "
              "cRefresh=%llu cDXRefresh=%u qpcCompose=%llu cFrame=%llu cDXPresent=%u "
              "cRefreshFrame=%llu cFrameSubmitted=%llu cDXPresentSubmitted=%u "
              "cFrameConfirmed=%llu cDXPresentConfirmed=%u cRefreshConfirmed=%llu "
              "cDXRefreshConfirmed=%u cFramesLate=%llu cFramesOutstanding=%u "
              "cFrameDisplayed=%llu qpcFrameDisplayed=%llu cRefreshFrameDisplayed=%llu "
              "cFrameComplete=%llu qpcFrameComplete=%llu cFramePending=%llu qpcFramePending=%llu "
              "cFramesDisplayed=%llu cFramesComplete=%llu cFramesPending=%llu "
              "cFramesAvailable=%llu cFramesDropped=%llu cFramesMissed=%llu "
              "cRefreshNextDisplayed=%llu cRefreshNextPresented=%llu cRefreshesDisplayed=%llu "
              "cRefreshesPresented=%llu cRefreshStarted=%llu cPixelsReceived=%llu "
              "cPixelsDrawn=%llu cBuffersEmpty=%llu delta_cRefresh=%lld delta_cFrame=%lld\n",
              sample_idx, (unsigned long)hr, ti->cbSize,
              ti->rateRefresh.uiNumerator, ti->rateRefresh.uiDenominator, (unsigned long long)ti->qpcRefreshPeriod,
              ti->rateCompose.uiNumerator, ti->rateCompose.uiDenominator, (unsigned long long)ti->qpcVBlank,
              (unsigned long long)ti->cRefresh, ti->cDXRefresh, (unsigned long long)ti->qpcCompose,
              (unsigned long long)ti->cFrame, ti->cDXPresent, (unsigned long long)ti->cRefreshFrame,
              (unsigned long long)ti->cFrameSubmitted, ti->cDXPresentSubmitted,
              (unsigned long long)ti->cFrameConfirmed, ti->cDXPresentConfirmed,
              (unsigned long long)ti->cRefreshConfirmed, ti->cDXRefreshConfirmed,
              (unsigned long long)ti->cFramesLate, ti->cFramesOutstanding,
              (unsigned long long)ti->cFrameDisplayed, (unsigned long long)ti->qpcFrameDisplayed,
              (unsigned long long)ti->cRefreshFrameDisplayed,
              (unsigned long long)ti->cFrameComplete, (unsigned long long)ti->qpcFrameComplete,
              (unsigned long long)ti->cFramePending, (unsigned long long)ti->qpcFramePending,
              (unsigned long long)ti->cFramesDisplayed, (unsigned long long)ti->cFramesComplete,
              (unsigned long long)ti->cFramesPending, (unsigned long long)ti->cFramesAvailable,
              (unsigned long long)ti->cFramesDropped, (unsigned long long)ti->cFramesMissed,
              (unsigned long long)ti->cRefreshNextDisplayed, (unsigned long long)ti->cRefreshNextPresented,
              (unsigned long long)ti->cRefreshesDisplayed, (unsigned long long)ti->cRefreshesPresented,
              (unsigned long long)ti->cRefreshStarted, (unsigned long long)ti->cPixelsReceived,
              (unsigned long long)ti->cPixelsDrawn, (unsigned long long)ti->cBuffersEmpty,
              delta_refresh, delta_frame);
}

static void probe_thumbnail_state(HWND hwnd_dst, HWND hwnd_src, int state_id)
{
    HTHUMBNAIL thumb = NULL;
    SIZE sz = {0, 0};
    HRESULT hr_reg = NOT_FOUND, hr_q = NOT_FOUND, hr_cloak = S_OK;
    DWORD cloaked_val = 0;
    BOOL cloak_on = TRUE, cloak_off = FALSE;

    if (state_id == 1)
    {
        ShowWindow(hwnd_src, SW_MINIMIZE);
        pump_messages();
        Sleep(100);
        pump_messages();
    }
    else if (state_id == 2)
    {
        if (pDwmSetWindowAttribute)
            hr_cloak = pDwmSetWindowAttribute(hwnd_src, DWMWA_CLOAK, &cloak_on, sizeof(cloak_on));
        if (pDwmGetWindowAttribute)
            pDwmGetWindowAttribute(hwnd_src, DWMWA_CLOAKED, &cloaked_val, sizeof(cloaked_val));
        pump_messages();
        Sleep(50);
    }

    if (pDwmRegisterThumbnail) hr_reg = pDwmRegisterThumbnail(hwnd_dst, hwnd_src, &thumb);
    if (SUCCEEDED(hr_reg) && pDwmQueryThumbnailSourceSize) hr_q = pDwmQueryThumbnailSourceSize(thumb, &sz);
    if (thumb && pDwmUnregisterThumbnail) pDwmUnregisterThumbnail(thumb);

    emit_line("thumbnail_state=%d reg_hr=0x%08lx query_hr=0x%08lx cloak_hr=0x%08lx cloaked=0x%08lx cx=%ld cy=%ld\n",
              state_id, (unsigned long)hr_reg, (unsigned long)hr_q,
              (unsigned long)hr_cloak, (unsigned long)cloaked_val, sz.cx, sz.cy);

    if (state_id == 1)
    {
        ShowWindow(hwnd_src, SW_RESTORE);
        pump_messages();
        Sleep(100);
        pump_messages();
    }
    else if (state_id == 2 && pDwmSetWindowAttribute)
    {
        pDwmSetWindowAttribute(hwnd_src, DWMWA_CLOAK, &cloak_off, sizeof(cloak_off));
        pump_messages();
    }
}

int main(void)
{
    HMODULE dwmapi;
    WNDCLASSA wc;
    HWND hwnd_dst, hwnd_src;
    DWM_TIMING_INFO ti0, ti1;
    HRESULT hr0, hr1, flush_hr = NOT_FOUND, hr_force = NOT_FOUND, hr_has = NOT_FOUND, hr_inv = NOT_FOUND;
    LARGE_INTEGER freq, t0, t1;
    double min_us = 1e18, max_us = 0.0, sum_us = 0.0;
    int i, ok_calls = 0;
    BOOL one = TRUE;
    HTHUMBNAIL live_thumb = NULL;
    DWORD pump_end;

    init_output("Z:\\tmp\\dwm-timing-iconic-probe.txt");
    g_start_tick = GetTickCount();

    dwmapi = LoadLibraryA("dwmapi.dll");
    if (dwmapi)
    {
        pDwmGetCompositionTimingInfo = (pfn_DwmGetCompositionTimingInfo)(void *)GetProcAddress(dwmapi, "DwmGetCompositionTimingInfo");
        pDwmFlush = (pfn_DwmFlush)(void *)GetProcAddress(dwmapi, "DwmFlush");
        pDwmRegisterThumbnail = (pfn_DwmRegisterThumbnail)(void *)GetProcAddress(dwmapi, "DwmRegisterThumbnail");
        pDwmUpdateThumbnailProperties = (pfn_DwmUpdateThumbnailProperties)(void *)GetProcAddress(dwmapi, "DwmUpdateThumbnailProperties");
        pDwmQueryThumbnailSourceSize = (pfn_DwmQueryThumbnailSourceSize)(void *)GetProcAddress(dwmapi, "DwmQueryThumbnailSourceSize");
        pDwmUnregisterThumbnail = (pfn_DwmUnregisterThumbnail)(void *)GetProcAddress(dwmapi, "DwmUnregisterThumbnail");
        pDwmGetWindowAttribute = (pfn_DwmGetWindowAttribute)(void *)GetProcAddress(dwmapi, "DwmGetWindowAttribute");
        pDwmSetWindowAttribute = (pfn_DwmSetWindowAttribute)(void *)GetProcAddress(dwmapi, "DwmSetWindowAttribute");
        pDwmSetIconicThumbnail = (pfn_DwmSetIconicThumbnail)(void *)GetProcAddress(dwmapi, "DwmSetIconicThumbnail");
        pDwmSetIconicLivePreviewBitmap = (pfn_DwmSetIconicLivePreviewBitmap)(void *)GetProcAddress(dwmapi, "DwmSetIconicLivePreviewBitmap");
        pDwmInvalidateIconicBitmaps = (pfn_DwmInvalidateIconicBitmaps)(void *)GetProcAddress(dwmapi, "DwmInvalidateIconicBitmaps");
    }

    g_iconic_bmp = create_dib_128x96(128, 96);

    memset(&wc, 0, sizeof(wc));
    wc.lpfnWndProc = probe_wndproc;
    wc.hInstance = GetModuleHandleA(NULL);
    wc.lpszClassName = "dwm_timing_iconic_cls";
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    RegisterClassA(&wc);

    hwnd_dst = CreateWindowExA(0, wc.lpszClassName, "dwm_dst", WS_OVERLAPPEDWINDOW | WS_VISIBLE,
                               100, 100, 400, 300, NULL, NULL, wc.hInstance, NULL);
    hwnd_src = CreateWindowExA(0, wc.lpszClassName, "dwm_src", WS_OVERLAPPEDWINDOW | WS_VISIBLE,
                               160, 160, 320, 240, NULL, NULL, wc.hInstance, NULL);
    ShowWindow(hwnd_dst, SW_SHOW);
    ShowWindow(hwnd_src, SW_SHOW);
    UpdateWindow(hwnd_dst);
    UpdateWindow(hwnd_src);
    pump_messages();

    hr0 = query_timing_info(hwnd_dst, &ti0);
    print_timing_info(0, hr0, &ti0, 0, 0);
    Sleep(100);
    pump_messages();
    hr1 = query_timing_info(hwnd_dst, &ti1);
    print_timing_info(1, hr1, &ti1,
                      (long long)(ti1.cRefresh - ti0.cRefresh),
                      (long long)(ti1.cFrame - ti0.cFrame));

    QueryPerformanceFrequency(&freq);
    for (i = 0; i < 30; i++)
    {
        double us = 0.0;
        QueryPerformanceCounter(&t0);
        flush_hr = pDwmFlush ? pDwmFlush() : NOT_FOUND;
        QueryPerformanceCounter(&t1);
        if (freq.QuadPart > 0)
            us = (double)(t1.QuadPart - t0.QuadPart) * 1000000.0 / (double)freq.QuadPart;
        if (us < min_us) min_us = us;
        if (us > max_us) max_us = us;
        sum_us += us;
        if (SUCCEEDED(flush_hr)) ok_calls++;
    }
    if (min_us > max_us) min_us = 0.0;
    emit_line("dwmflush_calls=30 ok_calls=%d last_hr=0x%08lx min_us=%.2f mean_us=%.2f max_us=%.2f\n",
              ok_calls, (unsigned long)flush_hr, min_us, sum_us / 30.0, max_us);

    probe_thumbnail_state(hwnd_dst, hwnd_src, 0);
    probe_thumbnail_state(hwnd_dst, hwnd_src, 1);
    probe_thumbnail_state(hwnd_dst, hwnd_src, 2);

    if (pDwmRegisterThumbnail && SUCCEEDED(pDwmRegisterThumbnail(hwnd_dst, hwnd_src, &live_thumb)))
    {
        DWM_THUMBNAIL_PROPERTIES props = {0};
        props.dwFlags = DWM_TNP_RECTDESTINATION | DWM_TNP_VISIBLE;
        props.rcDestination.left = 10;
        props.rcDestination.top = 10;
        props.rcDestination.right = 210;
        props.rcDestination.bottom = 160;
        props.fVisible = TRUE;
        if (pDwmUpdateThumbnailProperties) pDwmUpdateThumbnailProperties(live_thumb, &props);
    }

    if (pDwmSetWindowAttribute)
    {
        hr_force = pDwmSetWindowAttribute(hwnd_src, DWMWA_FORCE_ICONIC_REPRESENTATION, &one, sizeof(one));
        hr_has = pDwmSetWindowAttribute(hwnd_src, DWMWA_HAS_ICONIC_BITMAP, &one, sizeof(one));
    }
    if (pDwmInvalidateIconicBitmaps)
        hr_inv = pDwmInvalidateIconicBitmaps(hwnd_src);

    emit_line("iconic_setup=1 force_hr=0x%08lx has_bmp_hr=0x%08lx invalidate_hr=0x%08lx\n",
              (unsigned long)hr_force, (unsigned long)hr_has, (unsigned long)hr_inv);

    pump_end = GetTickCount() + 3000;
    while ((LONG)(pump_end - GetTickCount()) > 0)
    {
        pump_messages();
        Sleep(15);
    }

    if (g_log_count == 0)
    {
        emit_line("iconic_messages=0\n");
    }
    else
    {
        for (i = 0; i < g_log_count; i++)
        {
            emit_line("iconic_messages=%d seq=%d msg=0x%04x hiword=%u loword=%u timestamp_ms=%lu reply_hr=0x%08lx\n",
                      g_log_count, g_logs[i].seq, g_logs[i].msg, g_logs[i].hiword, g_logs[i].loword,
                      (unsigned long)g_logs[i].timestamp_ms, (unsigned long)g_logs[i].reply_hr);
        }
    }

    if (live_thumb && pDwmUnregisterThumbnail) pDwmUnregisterThumbnail(live_thumb);
    if (g_iconic_bmp) DeleteObject(g_iconic_bmp);
    DestroyWindow(hwnd_src);
    DestroyWindow(hwnd_dst);
    if (dwmapi) FreeLibrary(dwmapi);
    if (g_con_out) fclose(g_con_out);
    return 0;
}

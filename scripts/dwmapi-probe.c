/*
 * WineExplorer - Desktop Window Manager (dwmapi) API probe
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
#include <setjmp.h>
#include <stdio.h>
#include <string.h>

#define NOT_FOUND ((HRESULT)0x8007007f)

typedef HRESULT (WINAPI *pfn_DwmIsCompositionEnabled)(BOOL *);
typedef HRESULT (WINAPI *pfn_DwmGetColorizationColor)(DWORD *, BOOL *);
typedef HRESULT (WINAPI *pfn_DwmGetWindowAttribute)(HWND, DWORD, PVOID, DWORD);
typedef HRESULT (WINAPI *pfn_DwmSetWindowAttribute)(HWND, DWORD, LPCVOID, DWORD);
typedef HRESULT (WINAPI *pfn_DwmExtendFrameIntoClientArea)(HWND, const MARGINS *);
typedef HRESULT (WINAPI *pfn_DwmEnableBlurBehindWindow)(HWND, const DWM_BLURBEHIND *);
typedef HRESULT (WINAPI *pfn_DwmGetCompositionTimingInfo)(HWND, DWM_TIMING_INFO *);
typedef HRESULT (WINAPI *pfn_DwmFlush)(void);
typedef HRESULT (WINAPI *pfn_DwmEnableComposition)(UINT);
typedef HRESULT (WINAPI *pfn_DwmRegisterThumbnail)(HWND, HWND, PHTHUMBNAIL);
typedef HRESULT (WINAPI *pfn_DwmUpdateThumbnailProperties)(HTHUMBNAIL, const DWM_THUMBNAIL_PROPERTIES *);
typedef HRESULT (WINAPI *pfn_DwmQueryThumbnailSourceSize)(HTHUMBNAIL, PSIZE);
typedef HRESULT (WINAPI *pfn_DwmUnregisterThumbnail)(HTHUMBNAIL);
typedef HRESULT (WINAPI *pfn_DwmSetIconicThumbnail)(HWND, HBITMAP, DWORD);
typedef HRESULT (WINAPI *pfn_DwmSetIconicLivePreviewBitmap)(HWND, HBITMAP, POINT *, DWORD);
typedef HRESULT (WINAPI *pfn_DwmInvalidateIconicBitmaps)(HWND);
typedef HRESULT (WINAPI *pfn_DwmModifyPreviousDxFrameDuration)(HWND, INT, BOOL);
typedef HRESULT (WINAPI *pfn_DwmGetGraphicsStreamTransformHint)(UINT, MilMatrix3x2D *);
typedef HRESULT (WINAPI *pfn_DwmTransitionOwnedWindow)(HWND, enum DWMTRANSITION_OWNEDWINDOW_TARGET);
typedef HRESULT (WINAPI *pfn_DwmShowContact)(DWORD, enum DWM_SHOWCONTACT);

typedef LONG (NTAPI *pfn_RtlGetVersion)(OSVERSIONINFOW *);
typedef const char *(CDECL *pfn_wine_get_version)(void);

static jmp_buf g_probe_jmp;
static volatile BOOL g_probe_catch = FALSE;
static volatile DWORD g_probe_code = 0;

static LONG WINAPI probe_veh(EXCEPTION_POINTERS *ep)
{
    if (g_probe_catch && ep && ep->ExceptionRecord)
    {
        g_probe_code = ep->ExceptionRecord->ExceptionCode;
        g_probe_catch = FALSE;
        longjmp(g_probe_jmp, 1);
    }
    return EXCEPTION_CONTINUE_SEARCH;
}

#define CALL_GUARDED(var, expr) do { \
    g_probe_code = 0; \
    g_probe_catch = TRUE; \
    if (setjmp(g_probe_jmp) == 0) { \
        (var) = (expr); \
        g_probe_catch = FALSE; \
    } else { \
        (var) = (HRESULT)g_probe_code; \
    } \
} while (0)

#define CALL_DWM(fn, var, expr) do { \
    if (fn) { CALL_GUARDED(var, expr); } \
    else { (var) = NOT_FOUND; } \
} while (0)

static LRESULT CALLBACK probe_wndproc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    return DefWindowProcA(hwnd, msg, wp, lp);
}

int main(void)
{
    WNDCLASSA wc = {0};
    HWND hwnd, hwnd_src;
    MSG msg;
    PVOID veh_handle;
    HMODULE dwmapi, ntdll;

    pfn_DwmIsCompositionEnabled pDwmIsCompositionEnabled;
    pfn_DwmGetColorizationColor pDwmGetColorizationColor;
    pfn_DwmGetWindowAttribute pDwmGetWindowAttribute;
    pfn_DwmSetWindowAttribute pDwmSetWindowAttribute;
    pfn_DwmExtendFrameIntoClientArea pDwmExtendFrameIntoClientArea;
    pfn_DwmEnableBlurBehindWindow pDwmEnableBlurBehindWindow;
    pfn_DwmGetCompositionTimingInfo pDwmGetCompositionTimingInfo;
    pfn_DwmFlush pDwmFlush;
    pfn_DwmEnableComposition pDwmEnableComposition;
    pfn_DwmRegisterThumbnail pDwmRegisterThumbnail;
    pfn_DwmUpdateThumbnailProperties pDwmUpdateThumbnailProperties;
    pfn_DwmQueryThumbnailSourceSize pDwmQueryThumbnailSourceSize;
    pfn_DwmUnregisterThumbnail pDwmUnregisterThumbnail;
    pfn_DwmSetIconicThumbnail pDwmSetIconicThumbnail;
    pfn_DwmSetIconicLivePreviewBitmap pDwmSetIconicLivePreviewBitmap;
    pfn_DwmInvalidateIconicBitmaps pDwmInvalidateIconicBitmaps;
    pfn_DwmModifyPreviousDxFrameDuration pDwmModifyPreviousDxFrameDuration;
    pfn_DwmGetGraphicsStreamTransformHint pDwmGetGraphicsStreamTransformHint;
    pfn_DwmTransitionOwnedWindow pDwmTransitionOwnedWindow;
    pfn_DwmShowContact pDwmShowContact;

    BOOL orig_enabled = FALSE, enabled = FALSE, enabled_again = FALSE, opaque = FALSE;
    BOOL nc_enabled = FALSE, set_ncpaint = TRUE, read_ncpaint = FALSE;
    BOOL set_dark = TRUE, read_dark = FALSE, force_iconic = TRUE;
    DWORD color = 0, cloaked = 0, corner = 0;
    DWORD set_policy = DWMNCRP_ENABLED, read_policy = 0;
    DWORD set_backdrop = DWMSBT_MAINWINDOW, read_backdrop = 0;
    DWORD set_corner = DWMWCP_ROUND, read_corner = 0;
    RECT rc_frame = {0, 0, 0, 0}, rc_cap = {0, 0, 0, 0}, bmp_rc = {0, 0, 64, 64};
    MARGINS m0 = {0, 0, 0, 0}, m1 = {-1, -1, -1, -1};
    POINT pt = {0, 0};
    SIZE src_sz = {0, 0};
    HTHUMBNAIL thumb = NULL;
    DWM_THUMBNAIL_PROPERTIES props;
    DWM_BLURBEHIND bb;
    DWM_TIMING_INFO ti;
    MilMatrix3x2D xform;
    OSVERSIONINFOW vi;
    LARGE_INTEGER freq, t0, t1;
    double elapsed_us = 0.0;
    HRESULT hr, set_hr, read_hr, hr_dis, hr_en, hr_thumb, hr_prev;
    HDC hdc, memdc;
    HBITMAP hbmp;
    HGDIOBJ old_bmp;
    LONG vstatus;
    const char *wine_ver;
    int is_wine;
    pfn_RtlGetVersion pRtlGetVersion;
    pfn_wine_get_version pwine_get_version;

    setvbuf(stdout, NULL, _IONBF, 0);
    veh_handle = AddVectoredExceptionHandler(1, probe_veh);

    wc.lpfnWndProc = probe_wndproc;
    wc.hInstance = GetModuleHandleA(NULL);
    wc.lpszClassName = "dwmapi_probe_class";
    RegisterClassA(&wc);

    hwnd = CreateWindowExA(0, wc.lpszClassName, "dwmapi probe main",
                           WS_OVERLAPPEDWINDOW | WS_VISIBLE,
                           100, 100, 400, 300, NULL, NULL, wc.hInstance, NULL);
    ShowWindow(hwnd, SW_SHOW);
    UpdateWindow(hwnd);

    hwnd_src = CreateWindowExA(0, wc.lpszClassName, "dwmapi probe src",
                               WS_OVERLAPPEDWINDOW | WS_VISIBLE,
                               150, 150, 300, 200, NULL, NULL, wc.hInstance, NULL);
    ShowWindow(hwnd_src, SW_SHOW);
    UpdateWindow(hwnd_src);

    while (PeekMessageA(&msg, NULL, 0, 0, PM_REMOVE))
    {
        TranslateMessage(&msg);
        DispatchMessageA(&msg);
    }

    dwmapi = LoadLibraryA("dwmapi.dll");

    pDwmIsCompositionEnabled = dwmapi ? (pfn_DwmIsCompositionEnabled)GetProcAddress(dwmapi, "DwmIsCompositionEnabled") : NULL;
    pDwmGetColorizationColor = dwmapi ? (pfn_DwmGetColorizationColor)GetProcAddress(dwmapi, "DwmGetColorizationColor") : NULL;
    pDwmGetWindowAttribute = dwmapi ? (pfn_DwmGetWindowAttribute)GetProcAddress(dwmapi, "DwmGetWindowAttribute") : NULL;
    pDwmSetWindowAttribute = dwmapi ? (pfn_DwmSetWindowAttribute)GetProcAddress(dwmapi, "DwmSetWindowAttribute") : NULL;
    pDwmExtendFrameIntoClientArea = dwmapi ? (pfn_DwmExtendFrameIntoClientArea)GetProcAddress(dwmapi, "DwmExtendFrameIntoClientArea") : NULL;
    pDwmEnableBlurBehindWindow = dwmapi ? (pfn_DwmEnableBlurBehindWindow)GetProcAddress(dwmapi, "DwmEnableBlurBehindWindow") : NULL;
    pDwmGetCompositionTimingInfo = dwmapi ? (pfn_DwmGetCompositionTimingInfo)GetProcAddress(dwmapi, "DwmGetCompositionTimingInfo") : NULL;
    pDwmFlush = dwmapi ? (pfn_DwmFlush)GetProcAddress(dwmapi, "DwmFlush") : NULL;
    pDwmEnableComposition = dwmapi ? (pfn_DwmEnableComposition)GetProcAddress(dwmapi, "DwmEnableComposition") : NULL;
    pDwmRegisterThumbnail = dwmapi ? (pfn_DwmRegisterThumbnail)GetProcAddress(dwmapi, "DwmRegisterThumbnail") : NULL;
    pDwmUpdateThumbnailProperties = dwmapi ? (pfn_DwmUpdateThumbnailProperties)GetProcAddress(dwmapi, "DwmUpdateThumbnailProperties") : NULL;
    pDwmQueryThumbnailSourceSize = dwmapi ? (pfn_DwmQueryThumbnailSourceSize)GetProcAddress(dwmapi, "DwmQueryThumbnailSourceSize") : NULL;
    pDwmUnregisterThumbnail = dwmapi ? (pfn_DwmUnregisterThumbnail)GetProcAddress(dwmapi, "DwmUnregisterThumbnail") : NULL;
    pDwmSetIconicThumbnail = dwmapi ? (pfn_DwmSetIconicThumbnail)GetProcAddress(dwmapi, "DwmSetIconicThumbnail") : NULL;
    pDwmSetIconicLivePreviewBitmap = dwmapi ? (pfn_DwmSetIconicLivePreviewBitmap)GetProcAddress(dwmapi, "DwmSetIconicLivePreviewBitmap") : NULL;
    pDwmInvalidateIconicBitmaps = dwmapi ? (pfn_DwmInvalidateIconicBitmaps)GetProcAddress(dwmapi, "DwmInvalidateIconicBitmaps") : NULL;
    pDwmModifyPreviousDxFrameDuration = dwmapi ? (pfn_DwmModifyPreviousDxFrameDuration)GetProcAddress(dwmapi, "DwmModifyPreviousDxFrameDuration") : NULL;
    pDwmGetGraphicsStreamTransformHint = dwmapi ? (pfn_DwmGetGraphicsStreamTransformHint)GetProcAddress(dwmapi, "DwmGetGraphicsStreamTransformHint") : NULL;
    pDwmTransitionOwnedWindow = dwmapi ? (pfn_DwmTransitionOwnedWindow)GetProcAddress(dwmapi, "DwmTransitionOwnedWindow") : NULL;
    pDwmShowContact = dwmapi ? (pfn_DwmShowContact)GetProcAddress(dwmapi, "DwmShowContact") : NULL;

    /* DwmIsCompositionEnabled */
    CALL_DWM(pDwmIsCompositionEnabled, hr, pDwmIsCompositionEnabled(&enabled));
    orig_enabled = enabled;
    printf("DwmIsCompositionEnabled hr=0x%08lx enabled=%d\n", (unsigned long)hr, enabled);

    /* DwmGetColorizationColor */
    CALL_DWM(pDwmGetColorizationColor, hr, pDwmGetColorizationColor(&color, &opaque));
    printf("DwmGetColorizationColor hr=0x%08lx color=0x%08lx opaque=%d\n", (unsigned long)hr, (unsigned long)color, opaque);

    /* DwmGetWindowAttribute */
    CALL_DWM(pDwmGetWindowAttribute, hr, pDwmGetWindowAttribute(hwnd, DWMWA_NCRENDERING_ENABLED, &nc_enabled, sizeof(nc_enabled)));
    printf("DwmGetWindowAttribute hr=0x%08lx attribute=DWMWA_NCRENDERING_ENABLED enabled=%d\n", (unsigned long)hr, nc_enabled);

    CALL_DWM(pDwmGetWindowAttribute, hr, pDwmGetWindowAttribute(hwnd, DWMWA_EXTENDED_FRAME_BOUNDS, &rc_frame, sizeof(rc_frame)));
    printf("DwmGetWindowAttribute hr=0x%08lx attribute=DWMWA_EXTENDED_FRAME_BOUNDS left=%ld top=%ld right=%ld bottom=%ld\n",
           (unsigned long)hr, rc_frame.left, rc_frame.top, rc_frame.right, rc_frame.bottom);

    CALL_DWM(pDwmGetWindowAttribute, hr, pDwmGetWindowAttribute(hwnd, DWMWA_CLOAKED, &cloaked, sizeof(cloaked)));
    printf("DwmGetWindowAttribute hr=0x%08lx attribute=DWMWA_CLOAKED cloaked=0x%08lx\n", (unsigned long)hr, (unsigned long)cloaked);

    CALL_DWM(pDwmGetWindowAttribute, hr, pDwmGetWindowAttribute(hwnd, DWMWA_WINDOW_CORNER_PREFERENCE, &corner, sizeof(corner)));
    printf("DwmGetWindowAttribute hr=0x%08lx attribute=DWMWA_WINDOW_CORNER_PREFERENCE corner=%lu\n", (unsigned long)hr, (unsigned long)corner);

    CALL_DWM(pDwmGetWindowAttribute, hr, pDwmGetWindowAttribute(hwnd, DWMWA_CAPTION_BUTTON_BOUNDS, &rc_cap, sizeof(rc_cap)));
    printf("DwmGetWindowAttribute hr=0x%08lx attribute=DWMWA_CAPTION_BUTTON_BOUNDS left=%ld top=%ld right=%ld bottom=%ld\n",
           (unsigned long)hr, rc_cap.left, rc_cap.top, rc_cap.right, rc_cap.bottom);

    /* DwmSetWindowAttribute (then read back) */
    CALL_DWM(pDwmSetWindowAttribute, set_hr, pDwmSetWindowAttribute(hwnd, DWMWA_NCRENDERING_POLICY, &set_policy, sizeof(set_policy)));
    CALL_DWM(pDwmGetWindowAttribute, read_hr, pDwmGetWindowAttribute(hwnd, DWMWA_NCRENDERING_POLICY, &read_policy, sizeof(read_policy)));
    printf("DwmSetWindowAttribute hr=0x%08lx attribute=DWMWA_NCRENDERING_POLICY set=%lu read_hr=0x%08lx read_val=%lu\n",
           (unsigned long)set_hr, (unsigned long)set_policy, (unsigned long)read_hr, (unsigned long)read_policy);

    CALL_DWM(pDwmSetWindowAttribute, set_hr, pDwmSetWindowAttribute(hwnd, DWMWA_ALLOW_NCPAINT, &set_ncpaint, sizeof(set_ncpaint)));
    CALL_DWM(pDwmGetWindowAttribute, read_hr, pDwmGetWindowAttribute(hwnd, DWMWA_ALLOW_NCPAINT, &read_ncpaint, sizeof(read_ncpaint)));
    printf("DwmSetWindowAttribute hr=0x%08lx attribute=DWMWA_ALLOW_NCPAINT set=%d read_hr=0x%08lx read_val=%d\n",
           (unsigned long)set_hr, set_ncpaint, (unsigned long)read_hr, read_ncpaint);

    CALL_DWM(pDwmSetWindowAttribute, set_hr, pDwmSetWindowAttribute(hwnd, DWMWA_USE_IMMERSIVE_DARK_MODE, &set_dark, sizeof(set_dark)));
    CALL_DWM(pDwmGetWindowAttribute, read_hr, pDwmGetWindowAttribute(hwnd, DWMWA_USE_IMMERSIVE_DARK_MODE, &read_dark, sizeof(read_dark)));
    printf("DwmSetWindowAttribute hr=0x%08lx attribute=DWMWA_USE_IMMERSIVE_DARK_MODE set=%d read_hr=0x%08lx read_val=%d\n",
           (unsigned long)set_hr, set_dark, (unsigned long)read_hr, read_dark);

    CALL_DWM(pDwmSetWindowAttribute, set_hr, pDwmSetWindowAttribute(hwnd, DWMWA_SYSTEMBACKDROP_TYPE, &set_backdrop, sizeof(set_backdrop)));
    CALL_DWM(pDwmGetWindowAttribute, read_hr, pDwmGetWindowAttribute(hwnd, DWMWA_SYSTEMBACKDROP_TYPE, &read_backdrop, sizeof(read_backdrop)));
    printf("DwmSetWindowAttribute hr=0x%08lx attribute=DWMWA_SYSTEMBACKDROP_TYPE set=%lu read_hr=0x%08lx read_val=%lu\n",
           (unsigned long)set_hr, (unsigned long)set_backdrop, (unsigned long)read_hr, (unsigned long)read_backdrop);

    CALL_DWM(pDwmSetWindowAttribute, set_hr, pDwmSetWindowAttribute(hwnd, DWMWA_WINDOW_CORNER_PREFERENCE, &set_corner, sizeof(set_corner)));
    CALL_DWM(pDwmGetWindowAttribute, read_hr, pDwmGetWindowAttribute(hwnd, DWMWA_WINDOW_CORNER_PREFERENCE, &read_corner, sizeof(read_corner)));
    printf("DwmSetWindowAttribute hr=0x%08lx attribute=DWMWA_WINDOW_CORNER_PREFERENCE set=%lu read_hr=0x%08lx read_val=%lu\n",
           (unsigned long)set_hr, (unsigned long)set_corner, (unsigned long)read_hr, (unsigned long)read_corner);

    /* DwmExtendFrameIntoClientArea (margins 0 and -1) */
    CALL_DWM(pDwmExtendFrameIntoClientArea, hr, pDwmExtendFrameIntoClientArea(hwnd, &m0));
    printf("DwmExtendFrameIntoClientArea hr=0x%08lx margins=0 cxLeft=%d cxRight=%d cyTop=%d cyBottom=%d\n",
           (unsigned long)hr, m0.cxLeftWidth, m0.cxRightWidth, m0.cyTopHeight, m0.cyBottomHeight);

    CALL_DWM(pDwmExtendFrameIntoClientArea, hr, pDwmExtendFrameIntoClientArea(hwnd, &m1));
    printf("DwmExtendFrameIntoClientArea hr=0x%08lx margins=-1 cxLeft=%d cxRight=%d cyTop=%d cyBottom=%d\n",
           (unsigned long)hr, m1.cxLeftWidth, m1.cxRightWidth, m1.cyTopHeight, m1.cyBottomHeight);

    /* DwmEnableBlurBehindWindow */
    memset(&bb, 0, sizeof(bb));
    bb.dwFlags = DWM_BB_ENABLE;
    bb.fEnable = TRUE;
    CALL_DWM(pDwmEnableBlurBehindWindow, hr, pDwmEnableBlurBehindWindow(hwnd, &bb));
    printf("DwmEnableBlurBehindWindow hr=0x%08lx enable=%d\n", (unsigned long)hr, bb.fEnable);

    /* DwmGetCompositionTimingInfo (print every field) */
    memset(&ti, 0, sizeof(ti));
    ti.cbSize = sizeof(ti);
    if (pDwmGetCompositionTimingInfo)
    {
        CALL_GUARDED(hr, pDwmGetCompositionTimingInfo(NULL, &ti));
        if (FAILED(hr))
        {
            memset(&ti, 0, sizeof(ti));
            ti.cbSize = sizeof(ti);
            CALL_GUARDED(hr, pDwmGetCompositionTimingInfo(hwnd, &ti));
        }
    }
    else
    {
        hr = NOT_FOUND;
    }
    printf("DwmGetCompositionTimingInfo hr=0x%08lx cbSize=%u rateRefresh=%u/%u qpcRefreshPeriod=%llu rateCompose=%u/%u "
           "qpcVBlank=%llu cRefresh=%llu cDXRefresh=%u qpcCompose=%llu cFrame=%llu cDXPresent=%u cRefreshFrame=%llu "
           "cFrameSubmitted=%llu cDXPresentSubmitted=%u cFrameConfirmed=%llu cDXPresentConfirmed=%u cRefreshConfirmed=%llu "
           "cDXRefreshConfirmed=%u cFramesLate=%llu cFramesOutstanding=%u cFrameDisplayed=%llu qpcFrameDisplayed=%llu "
           "cRefreshFrameDisplayed=%llu cFrameComplete=%llu qpcFrameComplete=%llu cFramePending=%llu qpcFramePending=%llu "
           "cFramesDisplayed=%llu cFramesComplete=%llu cFramesPending=%llu cFramesAvailable=%llu cFramesDropped=%llu "
           "cFramesMissed=%llu cRefreshNextDisplayed=%llu cRefreshNextPresented=%llu cRefreshesDisplayed=%llu "
           "cRefreshesPresented=%llu cRefreshStarted=%llu cPixelsReceived=%llu cPixelsDrawn=%llu cBuffersEmpty=%llu\n",
           (unsigned long)hr, ti.cbSize,
           ti.rateRefresh.uiNumerator, ti.rateRefresh.uiDenominator, (unsigned long long)ti.qpcRefreshPeriod,
           ti.rateCompose.uiNumerator, ti.rateCompose.uiDenominator, (unsigned long long)ti.qpcVBlank,
           (unsigned long long)ti.cRefresh, ti.cDXRefresh, (unsigned long long)ti.qpcCompose,
           (unsigned long long)ti.cFrame, ti.cDXPresent, (unsigned long long)ti.cRefreshFrame,
           (unsigned long long)ti.cFrameSubmitted, ti.cDXPresentSubmitted,
           (unsigned long long)ti.cFrameConfirmed, ti.cDXPresentConfirmed,
           (unsigned long long)ti.cRefreshConfirmed, ti.cDXRefreshConfirmed,
           (unsigned long long)ti.cFramesLate, ti.cFramesOutstanding,
           (unsigned long long)ti.cFrameDisplayed, (unsigned long long)ti.qpcFrameDisplayed,
           (unsigned long long)ti.cRefreshFrameDisplayed,
           (unsigned long long)ti.cFrameComplete, (unsigned long long)ti.qpcFrameComplete,
           (unsigned long long)ti.cFramePending, (unsigned long long)ti.qpcFramePending,
           (unsigned long long)ti.cFramesDisplayed, (unsigned long long)ti.cFramesComplete,
           (unsigned long long)ti.cFramesPending, (unsigned long long)ti.cFramesAvailable,
           (unsigned long long)ti.cFramesDropped, (unsigned long long)ti.cFramesMissed,
           (unsigned long long)ti.cRefreshNextDisplayed, (unsigned long long)ti.cRefreshNextPresented,
           (unsigned long long)ti.cRefreshesDisplayed, (unsigned long long)ti.cRefreshesPresented,
           (unsigned long long)ti.cRefreshStarted, (unsigned long long)ti.cPixelsReceived,
           (unsigned long long)ti.cPixelsDrawn, (unsigned long long)ti.cBuffersEmpty);

    /* DwmFlush (time it) */
    QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&t0);
    CALL_DWM(pDwmFlush, hr, pDwmFlush());
    QueryPerformanceCounter(&t1);
    if (freq.QuadPart > 0)
        elapsed_us = (double)(t1.QuadPart - t0.QuadPart) * 1000000.0 / (double)freq.QuadPart;
    printf("DwmFlush hr=0x%08lx elapsed_us=%.1f\n", (unsigned long)hr, elapsed_us);

    /* DwmEnableComposition(DWM_EC_DISABLECOMPOSITION and ENABLECOMPOSITION, restore after) */
    CALL_DWM(pDwmEnableComposition, hr_dis, pDwmEnableComposition(DWM_EC_DISABLECOMPOSITION));
    printf("DwmEnableComposition hr=0x%08lx action=DWM_EC_DISABLECOMPOSITION\n", (unsigned long)hr_dis);

    CALL_DWM(pDwmEnableComposition, hr_en, pDwmEnableComposition(DWM_EC_ENABLECOMPOSITION));
    printf("DwmEnableComposition hr=0x%08lx action=DWM_EC_ENABLECOMPOSITION\n", (unsigned long)hr_en);

    if (pDwmEnableComposition)
        CALL_GUARDED(hr, pDwmEnableComposition(orig_enabled ? DWM_EC_ENABLECOMPOSITION : DWM_EC_DISABLECOMPOSITION));

    /* DwmIsCompositionEnabled again */
    CALL_DWM(pDwmIsCompositionEnabled, hr, pDwmIsCompositionEnabled(&enabled_again));
    printf("DwmIsCompositionEnabled hr=0x%08lx enabled=%d\n", (unsigned long)hr, enabled_again);

    /* DwmRegisterThumbnail/DwmUpdateThumbnailProperties/DwmQueryThumbnailSourceSize/DwmUnregisterThumbnail */
    CALL_DWM(pDwmRegisterThumbnail, hr, pDwmRegisterThumbnail(hwnd, hwnd_src, &thumb));
    printf("DwmRegisterThumbnail hr=0x%08lx handle=0x%p\n", (unsigned long)hr, (void *)thumb);

    memset(&props, 0, sizeof(props));
    props.dwFlags = DWM_TNP_RECTDESTINATION | DWM_TNP_VISIBLE | DWM_TNP_OPACITY;
    props.rcDestination.left = 10;
    props.rcDestination.top = 10;
    props.rcDestination.right = 100;
    props.rcDestination.bottom = 100;
    props.fVisible = TRUE;
    props.opacity = 200;
    CALL_DWM(pDwmUpdateThumbnailProperties, hr, pDwmUpdateThumbnailProperties(thumb, &props));
    printf("DwmUpdateThumbnailProperties hr=0x%08lx opacity=%u visible=%d\n", (unsigned long)hr, (unsigned)props.opacity, props.fVisible);

    CALL_DWM(pDwmQueryThumbnailSourceSize, hr, pDwmQueryThumbnailSourceSize(thumb, &src_sz));
    printf("DwmQueryThumbnailSourceSize hr=0x%08lx cx=%ld cy=%ld\n", (unsigned long)hr, src_sz.cx, src_sz.cy);

    CALL_DWM(pDwmUnregisterThumbnail, hr, pDwmUnregisterThumbnail(thumb));
    printf("DwmUnregisterThumbnail hr=0x%08lx\n", (unsigned long)hr);

    /* DwmSetIconicThumbnail and DwmSetIconicLivePreviewBitmap with DWMWA_FORCE_ICONIC_REPRESENTATION */
    if (pDwmSetWindowAttribute)
        CALL_GUARDED(hr, pDwmSetWindowAttribute(hwnd, DWMWA_FORCE_ICONIC_REPRESENTATION, &force_iconic, sizeof(force_iconic)));

    hdc = GetDC(hwnd);
    memdc = CreateCompatibleDC(hdc);
    hbmp = CreateCompatibleBitmap(hdc, 64, 64);
    old_bmp = SelectObject(memdc, hbmp);
    FillRect(memdc, &bmp_rc, (HBRUSH)GetStockObject(WHITE_BRUSH));
    SelectObject(memdc, old_bmp);
    DeleteDC(memdc);
    ReleaseDC(hwnd, hdc);

    CALL_DWM(pDwmSetIconicThumbnail, hr_thumb, pDwmSetIconicThumbnail(hwnd, hbmp, 0));
    printf("DwmSetIconicThumbnail hr=0x%08lx sit_flags=0\n", (unsigned long)hr_thumb);

    CALL_DWM(pDwmSetIconicLivePreviewBitmap, hr_prev, pDwmSetIconicLivePreviewBitmap(hwnd, hbmp, &pt, 0));
    printf("DwmSetIconicLivePreviewBitmap hr=0x%08lx sit_flags=0\n", (unsigned long)hr_prev);

    DeleteObject(hbmp);

    /* DwmInvalidateIconicBitmaps */
    CALL_DWM(pDwmInvalidateIconicBitmaps, hr, pDwmInvalidateIconicBitmaps(hwnd));
    printf("DwmInvalidateIconicBitmaps hr=0x%08lx\n", (unsigned long)hr);

    /* DwmModifyPreviousDxFrameDuration */
    CALL_DWM(pDwmModifyPreviousDxFrameDuration, hr, pDwmModifyPreviousDxFrameDuration(hwnd, 1, FALSE));
    printf("DwmModifyPreviousDxFrameDuration hr=0x%08lx refreshes=1 relative=0\n", (unsigned long)hr);

    /* DwmGetGraphicsStreamTransformHint */
    memset(&xform, 0, sizeof(xform));
    CALL_DWM(pDwmGetGraphicsStreamTransformHint, hr, pDwmGetGraphicsStreamTransformHint(0, &xform));
    printf("DwmGetGraphicsStreamTransformHint hr=0x%08lx s11=%.2f s12=%.2f s21=%.2f s22=%.2f dx=%.2f dy=%.2f\n",
           (unsigned long)hr, xform.S_11, xform.S_12, xform.S_21, xform.S_22, xform.DX, xform.DY);

    /* DwmTransitionOwnedWindow */
    CALL_DWM(pDwmTransitionOwnedWindow, hr, pDwmTransitionOwnedWindow(hwnd, DWMTRANSITION_OWNEDWINDOW_REPOSITION));
    printf("DwmTransitionOwnedWindow hr=0x%08lx target=DWMTRANSITION_OWNEDWINDOW_REPOSITION\n", (unsigned long)hr);

    /* DwmShowContact */
    CALL_DWM(pDwmShowContact, hr, pDwmShowContact(0, DWMSC_ALL));
    printf("DwmShowContact hr=0x%08lx contact_id=0 show=DWMSC_ALL\n", (unsigned long)hr);

    /* Windows version (RtlGetVersion) and whether under Wine (wine_get_version) */
    memset(&vi, 0, sizeof(vi));
    vi.dwOSVersionInfoSize = sizeof(vi);
    ntdll = GetModuleHandleA("ntdll.dll");
    pRtlGetVersion = ntdll ? (pfn_RtlGetVersion)GetProcAddress(ntdll, "RtlGetVersion") : NULL;
    pwine_get_version = ntdll ? (pfn_wine_get_version)GetProcAddress(ntdll, "wine_get_version") : NULL;

    if (pRtlGetVersion)
        CALL_GUARDED(vstatus, pRtlGetVersion(&vi));
    else
        vstatus = -1;

    wine_ver = pwine_get_version ? pwine_get_version() : NULL;
    is_wine = (wine_ver != NULL);

    printf("header hr=0x%08lx major=%lu minor=%lu build=%lu is_wine=%d wine=%s\n",
           (unsigned long)(vstatus == 0 ? 0 : 0x80004005),
           vi.dwMajorVersion, vi.dwMinorVersion, vi.dwBuildNumber,
           is_wine, wine_ver ? wine_ver : "none");

    if (dwmapi)
        FreeLibrary(dwmapi);

    if (veh_handle)
        RemoveVectoredExceptionHandler(veh_handle);

    DestroyWindow(hwnd_src);
    DestroyWindow(hwnd);

    return 0;
}

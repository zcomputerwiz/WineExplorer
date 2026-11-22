/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright 2026 WineExplorer contributors
 *
 * Frame-time probe for the desktop's graphics path (compositor, themed frames, GDI present).
 * It runs three phases in one window and prints one summary line per phase to stdout and to the file named
 * by argv[1] (default gfx-perf.log):
 *
 *   idle     the window sits still for IDLE_MS (no painting): the cost of doing nothing
 *   animate  a full-client repaint every frame for ANIM_MS (moving bars, double-buffered BitBlt, DwmFlush when
 *            composition is on): frame interval and paint time
 *   resize   the window is resized in 8-pixel steps between two sizes for RESIZE_MS, each step followed by a
 *            repaint: time of SetWindowPos plus the paint that answers it (the resize latency a user sees)
 *
 * Numbers per phase: frames, mean/median/p95/p99/max of the frame interval (ms), mean/max paint time (ms).
 * The sampler of the process CPU is separate (scripts/gfx-perf-sample.sh): run it beside this probe.
 *
 *   x86_64-w64-mingw32-gcc -O2 -o gfx-perf-probe64.exe scripts/gfx-perf-probe.c -lgdi32 -luser32 -ldwmapi
 */
#include <windows.h>
#include <dwmapi.h>
#include <stdio.h>
#include <stdlib.h>

#define IDLE_MS    5000
#define ANIM_MS    8000
#define RESIZE_MS  8000

static double qpc_ms(void)
{
    static LARGE_INTEGER freq;
    LARGE_INTEGER t;
    if (!freq.QuadPart) QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&t);
    return 1000.0 * t.QuadPart / freq.QuadPart;
}

static HWND hwnd;
static int bars_x;
static double paint_ms_last;

static void paint_frame(void)
{
    RECT rc;
    HDC dc, mem;
    HBITMAP bmp, old;
    HBRUSH brush;
    int i, w, h;
    double t0 = qpc_ms();

    GetClientRect(hwnd, &rc);
    w = rc.right; h = rc.bottom;
    if (w <= 0 || h <= 0) return;
    dc = GetDC(hwnd);
    mem = CreateCompatibleDC(dc);
    bmp = CreateCompatibleBitmap(dc, w, h);
    old = SelectObject(mem, bmp);
    brush = CreateSolidBrush(RGB(20, 20, 60));
    FillRect(mem, &rc, brush);
    DeleteObject(brush);
    for (i = 0; i < 24; i++)
    {
        RECT bar = { (bars_x + i * 40) % (w + 40) - 40, (i * h) / 24, 0, ((i + 1) * h) / 24 };
        bar.right = bar.left + 30;
        brush = CreateSolidBrush(RGB(40 + i * 8, 200 - i * 6, 90 + i * 4));
        FillRect(mem, &bar, brush);
        DeleteObject(brush);
    }
    BitBlt(dc, 0, 0, w, h, mem, 0, 0, SRCCOPY);
    SelectObject(mem, old);
    DeleteObject(bmp);
    DeleteDC(mem);
    ReleaseDC(hwnd, dc);
    paint_ms_last = qpc_ms() - t0;
}

static int cmp(const void *a, const void *b)
{
    double x = *(const double *)a, y = *(const double *)b;
    return x < y ? -1 : x > y;
}

static FILE *logf;

static void report(const char *phase, double *intervals, double *paints, int n, double seconds)
{
    double sum = 0, psum = 0, pmax = 0;
    int i;
    if (n < 2)
    {
        fprintf(stdout, "%s frames=%d\n", phase, n);
        if (logf) fprintf(logf, "%s frames=%d\n", phase, n);
        return;
    }
    for (i = 0; i < n; i++) { sum += intervals[i]; psum += paints[i]; if (paints[i] > pmax) pmax = paints[i]; }
    qsort(intervals, n, sizeof(double), cmp);
    for (i = 0; i < 2; i++)
    {
        FILE *f = i ? logf : stdout;
        if (!f) continue;
        fprintf(f, "%s frames=%d fps=%.1f interval_ms mean=%.2f median=%.2f p95=%.2f p99=%.2f max=%.2f "
                "paint_ms mean=%.2f max=%.2f\n", phase, n, n / seconds, sum / n, intervals[n / 2],
                intervals[(int)(n * 0.95)], intervals[(int)(n * 0.99)], intervals[n - 1], psum / n, pmax);
        fflush(f);
    }
}

static LRESULT CALLBACK proc(HWND h, UINT msg, WPARAM wp, LPARAM lp)
{
    if (msg == WM_DESTROY) { PostQuitMessage(0); return 0; }
    if (msg == WM_ERASEBKGND) return 1;
    return DefWindowProcW(h, msg, wp, lp);
}

static void pump(void)
{
    MSG msg;
    while (PeekMessageW(&msg, NULL, 0, 0, PM_REMOVE)) { TranslateMessage(&msg); DispatchMessageW(&msg); }
}

#define MAXF 20000
static double intervals[MAXF], paints[MAXF];

int main(int argc, char **argv)
{
    WNDCLASSW wc = { 0 };
    double start, last, now;
    int n, composed;
    BOOL enabled = FALSE;

    logf = fopen(argc > 1 ? argv[1] : "gfx-perf.log", "w");
    wc.lpfnWndProc = proc;
    wc.hInstance = GetModuleHandleW(NULL);
    wc.lpszClassName = L"WexGfxPerf";
    wc.hCursor = LoadCursorW(NULL, (LPCWSTR)IDC_ARROW);
    RegisterClassW(&wc);
    hwnd = CreateWindowW(L"WexGfxPerf", L"gfx-perf", WS_OVERLAPPEDWINDOW | WS_VISIBLE, 100, 80, 800, 600,
                         NULL, NULL, wc.hInstance, NULL);
    DwmIsCompositionEnabled(&enabled);
    composed = enabled;
    fprintf(stdout, "composition=%d\n", composed);
    if (logf) fprintf(logf, "composition=%d\n", composed);
    pump();
    Sleep(1000);
    paint_frame();

    /* idle */
    start = qpc_ms();
    while (qpc_ms() - start < IDLE_MS) { pump(); Sleep(20); }
    if (logf) { fprintf(logf, "idle seconds=%d\n", IDLE_MS / 1000); fflush(logf); }
    fprintf(stdout, "idle seconds=%d\n", IDLE_MS / 1000);

    /* animate */
    n = 0; start = last = qpc_ms();
    while ((now = qpc_ms()) - start < ANIM_MS && n < MAXF)
    {
        bars_x += 6;
        pump();
        paint_frame();
        if (composed) DwmFlush(); else Sleep(1);
        now = qpc_ms();
        intervals[n] = now - last; paints[n] = paint_ms_last; last = now; n++;
    }
    report("animate", intervals, paints, n, (qpc_ms() - start) / 1000);

    /* resize */
    n = 0; start = last = qpc_ms();
    {
        int w = 800, dir = 8;
        while ((now = qpc_ms()) - start < RESIZE_MS && n < MAXF)
        {
            double t0 = qpc_ms();
            w += dir;
            if (w > 1100 || w < 500) dir = -dir;
            SetWindowPos(hwnd, NULL, 0, 0, w, 600 + (w - 800) / 2, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
            pump();
            paint_frame();
            if (composed) DwmFlush(); else Sleep(1);
            now = qpc_ms();
            intervals[n] = now - last; paints[n] = now - t0; last = now; n++;
        }
    }
    report("resize", intervals, paints, n, (qpc_ms() - start) / 1000);
    fprintf(stdout, "done\n");
    if (logf) { fprintf(logf, "done\n"); fclose(logf); }
    DestroyWindow(hwnd);
    return 0;
}

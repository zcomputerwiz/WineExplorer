/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Copyright 2026 WineExplorer contributors */
/* Black-box probe (Windows host): does positioning a window send WM_NCHITTEST to another window of the same thread that lies under the top
 * left corner of the new position? Window A (visible, with a window procedure that counts WM_NCHITTEST) covers the point just above and to the
 * left of where window B is moved; B is moved and resized with SetWindowPos and shown. Windows sends no WM_NCHITTEST to A from inside
 * SetWindowPos (the count stays 0), which is what the owner hint of win32u's apply_window_pos has to do too. */
#include <windows.h>
#include <stdio.h>

static int nchittest;

static LRESULT CALLBACK proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    if (msg == WM_NCHITTEST) nchittest++;
    return DefWindowProcW(hwnd, msg, wp, lp);
}

int main(void)
{
    WNDCLASSW cls = {0};
    HWND a, b;
    int before;

    cls.lpfnWndProc = proc;
    cls.hInstance = GetModuleHandleW(NULL);
    cls.lpszClassName = L"nchittest_probe";
    RegisterClassW(&cls);
    a = CreateWindowW(L"nchittest_probe", L"A", WS_POPUP | WS_VISIBLE, 100, 100, 300, 300, 0, 0, cls.hInstance, 0);
    b = CreateWindowW(L"nchittest_probe", L"B", WS_POPUP, 500, 500, 50, 50, 0, 0, cls.hInstance, 0);
    UpdateWindow(a);
    {
        MSG m;
        while (PeekMessageW(&m, 0, 0, 0, PM_REMOVE)) { TranslateMessage(&m); DispatchMessageW(&m); }
    }
    before = nchittest;
    /* the top left corner of B's new position is inside A: the point (150, 150) is above and left of (151, 151) */
    SetWindowPos(b, HWND_TOP, 151, 151, 120, 120, SWP_SHOWWINDOW);
    printf("WM_NCHITTEST sent to A during SetWindowPos: %d (before: %d)\n", nchittest - before, before);
    SetWindowPos(b, HWND_TOP, 160, 160, 120, 120, 0);
    printf("WM_NCHITTEST sent to A after a second move: %d\n", nchittest - before);
    return 0;
}

/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright 2026 WineExplorer contributors
 * Separate-process Win32 z-order probe for the opt-in Wayland lab.
 */
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static COLORREF color;
static LRESULT CALLBACK proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    if (msg == WM_PAINT)
    {
        PAINTSTRUCT paint;
        RECT rect;
        HDC dc = BeginPaint(hwnd, &paint);
        HBRUSH brush = CreateSolidBrush(color);
        GetClientRect(hwnd, &rect); FillRect(dc, &rect, brush);
        DeleteObject(brush); EndPaint(hwnd, &paint); return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}
static HWND find(const char *name)
{
    WCHAR title[64];
    unsigned i;
    for (i = 0; name[i] && i < 63; ++i) title[i] = name[i];
    title[i] = 0;
    return FindWindowW(L"WineWaylandStackProbe", title);
}
int main(int argc, char **argv)
{
    HWND hwnd, after;
    MSG msg;
    WNDCLASSW cls = {0};
    if (argc != 4) return 1;
    SetProcessDPIAware();
    if (!strcmp(argv[1], "set"))
    {
        hwnd = find(argv[2]);
        if (!hwnd) return 2;
        if (!strcmp(argv[3], "top")) after = HWND_TOP;
        else if (!strcmp(argv[3], "bottom")) after = HWND_BOTTOM;
        else if (!strcmp(argv[3], "topmost")) after = HWND_TOPMOST;
        else if (!strcmp(argv[3], "normal")) after = HWND_NOTOPMOST;
        else if (!strcmp(argv[3], "close")) return !PostMessageW(hwnd, WM_CLOSE, 0, 0);
        else if (!(after = find(argv[3]))) return 3;
        printf("SET %s after=%s foreground=%p atom=%p\n", argv[2], argv[3],
               GetForegroundWindow(), GetPropW(hwnd, L"__wine_wayland_window_handle"));
        return !SetWindowPos(hwnd, after, 0, 0, 0, 0,
                             SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    }
    if (strcmp(argv[1], "create")) return 4;
    unsigned rgb = strtoul(argv[3], NULL, 16);
    color = RGB(rgb >> 16, (rgb >> 8) & 255, rgb & 255);
    cls.lpfnWndProc = proc; cls.hInstance = GetModuleHandleW(NULL);
    cls.lpszClassName = L"WineWaylandStackProbe";
    RegisterClassW(&cls);
    WCHAR title[64];
    unsigned i;
    for (i = 0; argv[2][i] && i < 63; ++i) title[i] = argv[2][i];
    title[i] = 0;
    hwnd = CreateWindowExW(WS_EX_NOACTIVATE, cls.lpszClassName, title, WS_POPUP,
                          150, 150, 200, 200, NULL, NULL, cls.hInstance, NULL);
    if (!hwnd) return 5;
    ShowWindow(hwnd, SW_SHOWNOACTIVATE);
    while (IsWindow(hwnd))
    {
        while (PeekMessageW(&msg, NULL, 0, 0, PM_REMOVE))
        { TranslateMessage(&msg); DispatchMessageW(&msg); }
        Sleep(10);
    }
    return 0;
}

/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright 2026 WineExplorer contributors
 * Solid, stationary windows for independent compositor pixel measurements.
 */
#include <windows.h>
#include <stdio.h>
#include <string.h>

static const COLORREF colors[] = { RGB(255,0,0), RGB(0,255,0), RGB(0,0,255),
                                  RGB(255,255,0), RGB(0,255,255), RGB(255,0,255) };
static LRESULT CALLBACK proc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam)
{
    if (message == WM_NCCREATE)
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)((CREATESTRUCTW *)lparam)->lpCreateParams);
    if (message == WM_PAINT)
    {
        PAINTSTRUCT paint;
        RECT rect;
        HDC dc = BeginPaint(hwnd, &paint);
        HBRUSH brush = CreateSolidBrush(colors[GetWindowLongPtrW(hwnd, GWLP_USERDATA)]);
        GetClientRect(hwnd, &rect);
        FillRect(dc, &rect, brush);
        DeleteObject(brush);
        EndPaint(hwnd, &paint);
        return 0;
    }
    return DefWindowProcW(hwnd, message, wparam, lparam);
}

int main(int argc, char **argv)
{
    WNDCLASSW cls = {0};
    HWND windows[6];
    MSG message;
    DWORD end;
    unsigned int i, count = argc > 1 && !strcmp(argv[1], "--second") ? 1 : 6;
    int offset = count == 1 ? 700 : 0;
    SetProcessDPIAware();
    cls.hInstance = GetModuleHandleW(NULL);
    cls.lpfnWndProc = proc;
    cls.lpszClassName = L"WineWaylandPositionProbe";
    if (!RegisterClassW(&cls)) return 1;
    for (i = 0; i < count; ++i)
    {
        windows[i] = CreateWindowExW(WS_EX_NOACTIVATE, cls.lpszClassName, L"Zone placement probe",
            WS_POPUP, offset + 40 + (i % 3) * 180, 50 + (i / 3) * 170, 120, 100,
            NULL, NULL, cls.hInstance, (void *)(ULONG_PTR)i);
        if (!windows[i]) return 2;
        ShowWindow(windows[i], SW_SHOWNOACTIVATE);
    }
    end = GetTickCount() + 45000;
    while ((LONG)(end - GetTickCount()) > 0)
    {
        while (PeekMessageW(&message, NULL, 0, 0, PM_REMOVE))
        { TranslateMessage(&message); DispatchMessageW(&message); }
        Sleep(10);
    }
    for (i = 0; i < count; ++i) DestroyWindow(windows[i]);
    return 0;
}

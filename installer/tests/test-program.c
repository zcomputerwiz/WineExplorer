/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Minimal Win32/Win64 GUI test application for WoW64 verification.
 * Draws a window confirming that 32-bit or 64-bit PE execution works.
 */
#include <windows.h>

#ifdef _WIN64
#define TEST_TITLE "Win64 Test Program"
#define TEST_TEXT  "Native Win64 Test Window (64-bit)"
#define TEST_CLASS "WexTest64WndClass"
#define WND_X 150
#define WND_Y 150
#else
#define TEST_TITLE "Win32 Test Program"
#define TEST_TEXT  "WoW64 Win32 Test Window (32-bit)"
#define TEST_CLASS "WexTest32WndClass"
#define WND_X 100
#define WND_Y 100
#endif

static LRESULT CALLBACK WndProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    switch (uMsg) {
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);
        RECT rc;
        GetClientRect(hwnd, &rc);
        DrawTextA(hdc, TEST_TEXT, -1, &rc,
                  DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        EndPaint(hwnd, &ps);
        return 0;
    }
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcA(hwnd, uMsg, wParam, lParam);
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow)
{
    const char szClassName[] = TEST_CLASS;
    WNDCLASSA wc = {0};
    HWND hwnd;
    MSG msg;

    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = szClassName;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);

    if (!RegisterClassA(&wc))
        return 1;

    hwnd = CreateWindowExA(
        0,
        szClassName,
        TEST_TITLE,
        WS_OVERLAPPEDWINDOW | WS_VISIBLE,
        WND_X, WND_Y, 420, 200,
        NULL, NULL, hInstance, NULL
    );

    if (!hwnd)
        return 1;

    ShowWindow(hwnd, nCmdShow);
    UpdateWindow(hwnd);

    while (GetMessageA(&msg, NULL, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageA(&msg);
    }

    return (int)msg.wParam;
}

/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright 2026 WineExplorer contributors
 *
 * Black-box probe (Windows 11 and Wine): what IsGUIThread returns on a new thread before and after calls that may make it a GUI
 * thread (CoreMessagingXP asks IsGUIThread(FALSE) on a new thread to choose how it sets up its completion port).
 */
#include <windows.h>
#include <stdio.h>

static DWORD WINAPI t_plain(void *arg)
{
    BOOL a = IsGUIThread(FALSE), b = IsGUIThread(FALSE);
    printf("new thread: IsGUIThread(FALSE) %d, again %d\n", a, b);
    return 0;
}

static DWORD WINAPI t_metrics(void *arg)
{
    GetSystemMetrics(SM_CXSCREEN);
    printf("after GetSystemMetrics: %d\n", IsGUIThread(FALSE));
    GetForegroundWindow();
    printf("after GetForegroundWindow: %d\n", IsGUIThread(FALSE));
    GetWindowThreadProcessId(GetDesktopWindow(), NULL);
    printf("after GetDesktopWindow/GetWindowThreadProcessId: %d\n", IsGUIThread(FALSE));
    return 0;
}

static DWORD WINAPI t_dc(void *arg)
{
    HDC dc = GetDC(NULL);
    printf("after GetDC(NULL): %d\n", IsGUIThread(FALSE));
    ReleaseDC(NULL, dc);
    return 0;
}

static DWORD WINAPI t_peek(void *arg)
{
    MSG msg;
    PeekMessageW(&msg, NULL, 0, 0, PM_NOREMOVE);
    printf("after PeekMessage: %d\n", IsGUIThread(FALSE));
    return 0;
}

static DWORD WINAPI t_window(void *arg)
{
    HWND h = CreateWindowExW(0, L"STATIC", NULL, 0, 0, 0, 0, 0, HWND_MESSAGE, NULL, NULL, NULL);
    printf("after CreateWindowEx: %d\n", IsGUIThread(FALSE));
    DestroyWindow(h);
    return 0;
}

static DWORD WINAPI t_convert(void *arg)
{
    BOOL a, b;
    SetLastError(0xdeadbeef);
    a = IsGUIThread(TRUE);
    printf("IsGUIThread(TRUE) %d (error %lu), then IsGUIThread(FALSE) %d\n", a, GetLastError(), b = IsGUIThread(FALSE));
    return 0;
}

static DWORD WINAPI t_event(void *arg)
{
    HANDLE e = CreateEventW(NULL, FALSE, FALSE, NULL);
    WaitForSingleObject(e, 0);
    CloseHandle(e);
    printf("after kernel-only calls: %d\n", IsGUIThread(FALSE));
    return 0;
}

static LRESULT CALLBACK probe_proc(HWND h, UINT m, WPARAM w, LPARAM l) { return DefWindowProcW(h, m, w, l); }

/* single calls a thread may make first (Notepad's UI thread calls RegisterClassW before CoreMessagingXP asks) */
static DWORD WINAPI t_one(void *arg)
{
    int which = (int)(INT_PTR)arg;
    const char *name = "?";
    switch (which)
    {
    case 0: { WNDCLASSW wc = {0}; wc.lpfnWndProc = probe_proc; wc.hInstance = GetModuleHandleW(NULL);
              wc.lpszClassName = L"GuiThreadProbeClass"; RegisterClassW(&wc); name = "RegisterClassW"; break; }
    case 1: { WNDCLASSEXW wc = {sizeof(wc)}; GetClassInfoExW(NULL, L"BUTTON", &wc); name = "GetClassInfoExW(BUTTON)"; break; }
    case 2: LoadCursorW(NULL, (LPCWSTR)IDC_ARROW); name = "LoadCursorW(IDC_ARROW)"; break;
    case 3: LoadIconW(NULL, (LPCWSTR)IDI_APPLICATION); name = "LoadIconW(IDI_APPLICATION)"; break;
    case 4: SetThreadDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2); name = "SetThreadDpiAwarenessContext"; break;
    case 5: { HDC dc = CreateCompatibleDC(NULL); DeleteDC(dc); name = "CreateCompatibleDC"; break; }
    case 6: GetDpiForSystem(); name = "GetDpiForSystem"; break;
    case 7: { RECT r; SystemParametersInfoW(SPI_GETWORKAREA, 0, &r, 0); name = "SystemParametersInfoW(SPI_GETWORKAREA)"; break; }
    case 8: RegisterWindowMessageW(L"GuiThreadProbeMessage"); name = "RegisterWindowMessageW"; break;
    case 9: GetKeyState(VK_SHIFT); name = "GetKeyState"; break;
    case 10: IsWindow(GetDesktopWindow()); name = "IsWindow(GetDesktopWindow())"; break;
    case 11: GetStockObject(DEFAULT_GUI_FONT); name = "GetStockObject"; break;
    }
    printf("after %s: %d\n", name, IsGUIThread(FALSE));
    return 0;
}

static void run(LPTHREAD_START_ROUTINE f)
{
    HANDLE t = CreateThread(NULL, 0, f, NULL, 0, NULL);
    WaitForSingleObject(t, INFINITE);
    CloseHandle(t);
}

int main(void)
{
    setvbuf(stdout, NULL, _IONBF, 0);
    printf("main thread: %d\n", IsGUIThread(FALSE));
    run(t_plain);
    run(t_event);
    run(t_metrics);
    run(t_dc);
    run(t_peek);
    run(t_window);
    run(t_convert);
    {
        int i;
        for (i = 0; i <= 11; i++)
        {
            HANDLE t = CreateThread(NULL, 0, t_one, (void *)(INT_PTR)i, 0, NULL);
            WaitForSingleObject(t, INFINITE);
            CloseHandle(t);
        }
    }
    return 0;
}

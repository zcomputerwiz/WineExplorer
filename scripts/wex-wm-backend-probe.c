/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright 2026 WineExplorer contributors
 * Exercise backend ABI/start-stop behavior on an isolated X11 desktop.
 */
#include <windows.h>
#include <stdio.h>
#include <wexhost.h>
static unsigned failures;
static void CALLBACK event_callback(const WEX_WM_EVENT *event, void *context) { }
static void check(const char *label, DWORD actual, DWORD expected)
{
    if (actual != expected)
    {
        printf("FAIL %s actual=%lu expected=%lu\n", label, actual, expected);
        ++failures;
    }
}
int main(int argc, char **argv)
{
    DWORD backend, capabilities;
    UINT64 desktop = (ULONG_PTR)GetPropW(GetDesktopWindow(), L"__wine_x11_whole_window");
    WNDCLASSW cls = {0};
    HWND host;
    RECT client = {0,0,100,100}, screen = {10,10,110,110};
    unsigned i;
    if (argc > 1 && !freopen(argv[1], "w", stdout)) return 2;
    setvbuf(stdout, NULL, _IONBF, 0);
    printf("PROCESS bits=%u desktop=%I64x\n", (unsigned)(8*sizeof(void *)), desktop);
    if (!desktop) return 2;
    check("unstarted", WexWmGetCapabilities(&backend, &capabilities), ERROR_NOT_READY);
    check("no capabilities", capabilities, 0);
    check("invalid backend", WexWmStartEx(99,desktop,event_callback,NULL), ERROR_INVALID_PARAMETER);
    cls.hInstance = GetModuleHandleW(NULL);
    cls.lpfnWndProc = DefWindowProcW;
    cls.lpszClassName = L"WexBackendProbe";
    RegisterClassW(&cls);
    host = CreateWindowW(cls.lpszClassName,L"Backend host",WS_OVERLAPPEDWINDOW,10,10,160,140,NULL,NULL,cls.hInstance,NULL);
    ShowWindow(host,SW_SHOWNOACTIVATE);
    for (i=0;i<2;i++)
    {
        check("start", WexWmStartEx(WEX_WM_BACKEND_X11,desktop,event_callback,NULL), ERROR_SUCCESS);
        check("capabilities", WexWmGetCapabilities(&backend,&capabilities), ERROR_SUCCESS);
        check("selected X11", backend, WEX_WM_BACKEND_X11);
        check("X parent capability", capabilities, WEX_WM_CAP_X11_PARENT);
        check("duplicate start", WexWmStart(desktop,event_callback,NULL), ERROR_ALREADY_EXISTS);
        check("invalid host", WexWmBindHost(0,NULL,&client,&screen), ERROR_INVALID_WINDOW_HANDLE);
        check("missing native window", WexWmBindHost(0,host,&client,&screen), ERROR_INVALID_WINDOW_HANDLE);
        WexWmStop();
        check("stopped", WexWmGetCapabilities(&backend,&capabilities), ERROR_NOT_READY);
        WexWmStop();
    }
    DestroyWindow(host);
    printf("BACKEND_ABI_%s bits=%u failures=%u\n",failures ? "FAILED" : "PASSED",(unsigned)(8*sizeof(void *)),failures);
    return failures != 0;
}

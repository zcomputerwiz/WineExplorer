/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright 2026 WineExplorer contributors
 * Ordinary Wine windows must work without the private compositor protocols.
 */
#include <windows.h>
#include <stdio.h>
#include <wexwayland.h>

static unsigned failures,paints;
static void check(const char *label,BOOL value)
{
    if (!value) { printf("FAIL %s\n",label); failures++; }
}
static void pump(void)
{
    DWORD start=GetTickCount();
    MSG msg;
    do {
        while (PeekMessageW(&msg,NULL,0,0,PM_REMOVE)) { TranslateMessage(&msg); DispatchMessageW(&msg); }
        Sleep(10);
    } while (GetTickCount()-start<300);
}
static LRESULT CALLBACK window_proc(HWND hwnd,UINT msg,WPARAM wp,LPARAM lp)
{
    if (msg==WM_PAINT)
    {
        PAINTSTRUCT paint;
        HDC dc=BeginPaint(hwnd,&paint);
        HBRUSH brush=CreateSolidBrush(RGB(0x33,0x88,0x55));
        FillRect(dc,&paint.rcPaint,brush); DeleteObject(brush); EndPaint(hwnd,&paint);
        paints++; return 0;
    }
    return DefWindowProcW(hwnd,msg,wp,lp);
}
int main(void)
{
    WNDCLASSW cls={0};
    HWND hwnd;
    RECT client;
    cls.lpfnWndProc=window_proc; cls.hInstance=GetModuleHandleW(NULL); cls.lpszClassName=L"WexStockProbe";
    RegisterClassW(&cls);
    hwnd=CreateWindowW(cls.lpszClassName,L"Stock labwc Wine smoke",WS_OVERLAPPEDWINDOW,
        80,80,300,220,NULL,NULL,cls.hInstance,NULL);
    check("ordinary window created",hwnd!=NULL);
    ShowWindow(hwnd,SW_SHOW); UpdateWindow(hwnd); pump();
    check("ordinary window painted",IsWindowVisible(hwnd) && paints);
    check("private hosting absent",!GetPropW(hwnd,WEX_WAYLAND_HOST_PROP));
#ifdef WEX_WAYLAND_RESIZE_PROTOCOL_PROP
    check("private resize absent",!GetPropW(hwnd,WEX_WAYLAND_RESIZE_PROTOCOL_PROP));
#endif
    SetWindowPos(hwnd,NULL,80,80,400,280,SWP_NOZORDER); pump();
    GetClientRect(hwnd,&client);
    check("ordinary size applied",client.right>300 && client.bottom>220);
    ShowWindow(hwnd,SW_MAXIMIZE); pump();
    GetClientRect(hwnd,&client);
    check("ordinary maximize",IsZoomed(hwnd) && client.right>400);
    ShowWindow(hwnd,SW_RESTORE); pump();
    check("ordinary restore",!IsZoomed(hwnd) && !IsIconic(hwnd));
    ShowWindow(hwnd,SW_MINIMIZE); pump();
    if (!IsIconic(hwnd) && GetEnvironmentVariableW(L"WEX_STOCK_ALLOW_MINIMIZE_UNSUPPORTED",NULL,0))
        puts("STOCK_MINIMIZE_UNSUPPORTED (known production baseline)");
    else check("ordinary minimize",IsIconic(hwnd));
    ShowWindow(hwnd,SW_RESTORE); pump(); check("ordinary unminimize",!IsIconic(hwnd));
    DestroyWindow(hwnd); pump(); check("ordinary close",!IsWindow(hwnd));
    printf("STOCK_WAYLAND_%s bits=%u failures=%u\n",failures?"FAILED":"PASSED",(unsigned)(8*sizeof(void *)),failures);
    return failures!=0;
}

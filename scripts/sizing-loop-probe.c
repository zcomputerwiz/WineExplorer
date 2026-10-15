/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright 2026 WineExplorer contributors
 * Unhosted keyboard move/size loop: a timer must not imply mouse release.
 */
#include <windows.h>
#include <stdio.h>
#include <string.h>
static unsigned ticks;
static BOOL entered,exited;
static LRESULT CALLBACK proc(HWND hwnd,UINT msg,WPARAM wp,LPARAM lp)
{
    if (msg==WM_ENTERSIZEMOVE) entered=TRUE;
    if (msg==WM_EXITSIZEMOVE) exited=TRUE;
    if (msg==WM_TIMER)
    {
        ticks++;
        if (ticks==2) PostMessageW(hwnd,WM_KEYDOWN,VK_RETURN,0);
        return 0;
    }
    return DefWindowProcW(hwnd,msg,wp,lp);
}
int main(int argc,char **argv)
{
    WNDCLASSW cls={0};
    HWND hwnd;
    MSG msg;
    BOOL broken=argc==2 && !strcmp(argv[1],"expect-broken");
    cls.lpfnWndProc=proc; cls.hInstance=GetModuleHandleW(NULL); cls.lpszClassName=L"WexSizingLoopProbe";
    RegisterClassW(&cls);
    hwnd=CreateWindowW(cls.lpszClassName,L"Keyboard loop",WS_OVERLAPPEDWINDOW,20,20,320,240,NULL,NULL,cls.hInstance,NULL);
    ShowWindow(hwnd,SW_SHOW); SetForegroundWindow(hwnd);
    while (PeekMessageW(&msg,NULL,0,0,PM_REMOVE)) DispatchMessageW(&msg);
    if (GetKeyState(VK_LBUTTON)&0x8000) return 2;
    SetTimer(hwnd,1,30,NULL);
    SendMessageW(hwnd,WM_SYSCOMMAND,SC_MOVE|HTCAPTION,0);
    KillTimer(hwnd,1);
    DestroyWindow(hwnd);
    printf("KEYBOARD_LOOP bits=%u entered=%u exited=%u timers=%u expected=%u\n",
           (unsigned)(8*sizeof(void *)),entered,exited,ticks,broken?1:2);
    return !(entered && exited && ticks==(broken?1:2));
}

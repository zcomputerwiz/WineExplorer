/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright 2026 WineExplorer contributors
 * Small lab actions and geometry/focus readback, used with real pointer clicks.
 */
#include <windows.h>
#include <stdio.h>
#include <string.h>
static LRESULT CALLBACK proc(HWND hwnd, UINT message, WPARAM wp, LPARAM lp)
{
    if (message == WM_PAINT)
    {
        PAINTSTRUCT paint;
        RECT rect;
        HBRUSH brush = CreateSolidBrush(RGB(255,255,0));
        HDC dc = BeginPaint(hwnd,&paint);
        GetClientRect(hwnd,&rect); FillRect(dc,&rect,brush);
        DeleteObject(brush); EndPaint(hwnd,&paint); return 0;
    }
    return DefWindowProcW(hwnd,message,wp,lp);
}
int main(int argc, char **argv)
{
    HWND hwnd = FindWindowW(L"Notepad",NULL), focus;
    RECT work, rect;
    WCHAR title[128];
    POINT point;
    if (argc > 1)
    {
        printf("ACTION %s hwnd=%p style=%08lx\n",argv[1],hwnd,GetWindowLongW(hwnd,GWL_STYLE));
        if (!strcmp(argv[1],"close")) PostMessageW(hwnd,WM_CLOSE,0,0);
        else if (!strcmp(argv[1],"minimize")) ShowWindow(hwnd,SW_MINIMIZE);
        else if (!strcmp(argv[1],"restore")) { ShowWindow(hwnd,SW_RESTORE); SetForegroundWindow(hwnd); }
        else if (!strcmp(argv[1],"maximize")) { ShowWindow(hwnd,SW_MAXIMIZE); SetForegroundWindow(hwnd); }
        else if (!strcmp(argv[1],"warp"))
        {
            printf("WARP %d\n",SetCursorPos(950,600));
            Sleep(100); GetCursorPos(&point); printf("POINTER %ld %ld\n",point.x,point.y);
        }
        else if (!strcmp(argv[1],"topmost") || !strcmp(argv[1],"alpha"))
        {
            WNDCLASSW cls = {0};
            MSG message;
            DWORD end = GetTickCount() + 30000;
            BOOL alpha = !strcmp(argv[1],"alpha");
            cls.lpfnWndProc=proc; cls.hInstance=GetModuleHandleW(NULL); cls.lpszClassName=L"WineWaylandLayerProbe";
            RegisterClassW(&cls);
            hwnd = CreateWindowExW(WS_EX_TOPMOST | WS_EX_NOACTIVATE | (alpha ? WS_EX_LAYERED : 0),
                cls.lpszClassName,L"Topmost layer probe",WS_POPUP,700,700,300,100,NULL,NULL,cls.hInstance,NULL);
            if (alpha) SetLayeredWindowAttributes(hwnd,0,128,LWA_ALPHA);
            ShowWindow(hwnd,SW_SHOWNOACTIVATE);
            while ((LONG)(end-GetTickCount())>0)
            { while (PeekMessageW(&message,NULL,0,0,PM_REMOVE)) { TranslateMessage(&message); DispatchMessageW(&message); } Sleep(10); }
            DestroyWindow(hwnd); return 0;
        }
    }
    SystemParametersInfoW(SPI_GETWORKAREA,0,&work,0);
    fprintf(stdout,"WORK %ld %ld %ld %ld\n",work.left,work.top,work.right,work.bottom);
    hwnd=FindWindowW(L"Notepad",NULL);
    printf("STYLE %08lx\n",GetWindowLongW(hwnd,GWL_STYLE));
    if (hwnd) { GetWindowRect(hwnd,&rect); printf("NOTEPAD %ld %ld %ld %ld minimized=%d\n",rect.left,rect.top,rect.right,rect.bottom,IsIconic(hwnd)); }
    focus=GetForegroundWindow(); GetWindowTextW(focus,title,128); printf("FOCUS %p %ls\n",focus,title);
    return 0;
}

/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright 2026 WineExplorer contributors
 * Small lab actions and geometry/focus readback, used with real pointer clicks.
 */
#include <windows.h>
#include <stdio.h>
#include <string.h>
static BOOL warp_mode;
static LRESULT CALLBACK proc(HWND hwnd, UINT message, WPARAM wp, LPARAM lp)
{
    if (warp_mode && message == WM_KEYDOWN && wp == 'W')
    {
        printf("WARP_OWN %d\n",SetCursorPos(950,600)); fflush(stdout); return 0;
    }
    if (warp_mode && message == WM_MOUSEMOVE)
    {
        POINT point = {(short)LOWORD(lp),(short)HIWORD(lp)};
        ClientToScreen(hwnd,&point);
        printf("MOTION %ld %ld\n",point.x,point.y); fflush(stdout);
    }
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
        if (!strcmp(argv[1],"zorder"))
        {
            for (hwnd=GetTopWindow(NULL);hwnd;hwnd=GetWindow(hwnd,GW_HWNDNEXT))
            {
                if (!IsWindowVisible(hwnd)) continue;
                GetWindowTextW(hwnd,title,128);
                printf("Z %p ex=%08lx role=%p atom=%p %ls\n",hwnd,GetWindowLongW(hwnd,GWL_EXSTYLE),
                    GetPropW(hwnd,L"__wine_wayland_role"),GetPropW(hwnd,L"__wine_wayland_window_handle"),title);
            }
            return 0;
        }
        else if (!strcmp(argv[1],"composition-rects"))
        {
            static const WCHAR *names[] = {L"Composition probe controls", L"Composition alpha ramp",
                L"Composition uniform alpha", L"Composition color key", L"Class shadow popup"};
            unsigned i;
            for (i=0;i<5;i++)
            {
                hwnd=FindWindowW(NULL,names[i]);
                if (!hwnd || !GetWindowRect(hwnd,&rect)) return 2;
                printf("RECT %u %ld %ld %ld %ld\n",i,rect.left,rect.top,rect.right,rect.bottom);
            }
            return 0;
        }
        else if (!strcmp(argv[1],"composition-cycles"))
        {
            hwnd = FindWindowW(L"WineExplorerCompositionProbe",L"Composition probe controls");
            if (!hwnd) return 2;
            SendMessageW(hwnd,WM_KEYDOWN,'5',0); return 0;
        }
        else if (!strcmp(argv[1],"close")) PostMessageW(hwnd,WM_CLOSE,0,0);
        else if (!strcmp(argv[1],"minimize")) ShowWindow(hwnd,SW_MINIMIZE);
        else if (!strcmp(argv[1],"restore")) { ShowWindow(hwnd,SW_RESTORE); SetForegroundWindow(hwnd); }
        else if (!strcmp(argv[1],"maximize")) { ShowWindow(hwnd,SW_MAXIMIZE); SetForegroundWindow(hwnd); }
        else if (!strcmp(argv[1],"warp"))
        {
            printf("WARP %d\n",SetCursorPos(950,600));
            Sleep(100); GetCursorPos(&point); printf("POINTER %ld %ld\n",point.x,point.y);
        }
        else if (!strcmp(argv[1],"topmost") || !strcmp(argv[1],"alpha") || !strcmp(argv[1],"warp-own"))
        {
            WNDCLASSW cls = {0};
            MSG message;
            DWORD end = GetTickCount() + 30000;
            BOOL alpha = !strcmp(argv[1],"alpha");
            warp_mode = !strcmp(argv[1],"warp-own");
            cls.lpfnWndProc=proc; cls.hInstance=GetModuleHandleW(NULL); cls.lpszClassName=L"WineWaylandLayerProbe";
            RegisterClassW(&cls);
            hwnd = CreateWindowExW(warp_mode ? 0 : WS_EX_TOPMOST | WS_EX_NOACTIVATE | (alpha ? WS_EX_LAYERED : 0),
                cls.lpszClassName,warp_mode ? L"Pointer warp probe" : L"Topmost layer probe",
                warp_mode ? WS_OVERLAPPEDWINDOW : WS_POPUP,700,warp_mode ? 400 : 700,300,warp_mode ? 240 : 100,NULL,NULL,cls.hInstance,NULL);
            if (alpha) SetLayeredWindowAttributes(hwnd,0,128,LWA_ALPHA);
            ShowWindow(hwnd,warp_mode ? SW_SHOW : SW_SHOWNOACTIVATE);
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

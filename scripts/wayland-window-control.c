/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright 2026 WineExplorer contributors
 * Small lab actions and geometry/focus readback, used with real pointer clicks.
 */
#include <windows.h>
#include <commctrl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wexhost.h>
static BOOL warp_mode;
static DWORD (WINAPI *native_title)(UINT64,WCHAR *,DWORD);
static DWORD (WINAPI *native_info)(UINT64,WEX_NATIVE_WINDOW *);
static BOOL CALLBACK child_info(HWND hwnd, LPARAM context)
{
    WCHAR name[128], title[128];
    RECT rect;
    GetClassNameW(hwnd,name,128);
    GetWindowTextW(hwnd,title,128);
    GetWindowRect(hwnd,&rect);
    printf("CHILD %p parent=%p class=%ls title=%ls visible=%d style=%08lx rect=%ld,%ld,%ld,%ld\n",
           hwnd,GetParent(hwnd),name,title,IsWindowVisible(hwnd),GetWindowLongW(hwnd,GWL_STYLE),
           rect.left,rect.top,rect.right,rect.bottom);
    if (!lstrcmpW(name,L"SysListView32"))
    {
        DWORD pid;
        HANDLE process;
        RECT *remote;
        GetWindowThreadProcessId(hwnd,&pid);
        process=OpenProcess(PROCESS_VM_OPERATION|PROCESS_VM_READ|PROCESS_VM_WRITE,FALSE,pid);
        remote=process ? VirtualAllocEx(process,NULL,sizeof(rect),MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE) : NULL;
        printf("  LIST count=%ld\n",(long)SendMessageW(hwnd,LVM_GETITEMCOUNT,0,0));
        if (remote)
        {
            ZeroMemory(&rect,sizeof(rect));
            WriteProcessMemory(process,remote,&rect,sizeof(rect),NULL);
            SendMessageW(hwnd,LVM_GETITEMRECT,0,(LPARAM)remote); /* LVIR_BOUNDS */
            ReadProcessMemory(process,remote,&rect,sizeof(rect),NULL);
            printf("  ITEM0 %ld,%ld,%ld,%ld\n",rect.left,rect.top,rect.right,rect.bottom);
            VirtualFreeEx(process,remote,0,MEM_RELEASE);
        }
        if (process) CloseHandle(process);
    }
    return TRUE;
}
static void CALLBACK native_event(const WEX_WM_EVENT *event, void *context)
{
    WCHAR title[256] = {0};
    WEX_NATIVE_WINDOW info = {0};
    if (event->type != WEX_WM_MAP && event->type != WEX_WM_OBSERVED) return;
    native_title(event->window,title,256);
    native_info(event->window,&info);
    printf("NATIVE %llu flags=%#lx states=%#lx logical=%ld,%ld,%ld,%ld app=%s title=%ls\n",
           (unsigned long long)event->window,event->flags,event->states,event->rect.left,event->rect.top,
           event->rect.right,event->rect.bottom,info.app_id,title);
}
static LRESULT CALLBACK proc(HWND hwnd, UINT message, WPARAM wp, LPARAM lp)
{
    if (warp_mode && message == WM_KEYDOWN && wp == 'W')
    {
        POINT before;
        GetCursorPos(&before);
        printf("WARP_BEFORE %ld %ld foreground=%p own=%p\n",before.x,before.y,GetForegroundWindow(),hwnd);
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
    if (argc > 1 && !strcmp(argv[1],"inventory"))
    {
        HMODULE module = LoadLibraryW(L"wexhost.dll");
        DWORD (WINAPI *start)(DWORD,UINT64,WEX_WM_CALLBACK,void *) = module ?
            (void *)GetProcAddress(module,"WexWmStartEx") : NULL;
        void (WINAPI *stop)(void) = module ? (void *)GetProcAddress(module,"WexWmStop") : NULL;
        native_title = module ? (void *)GetProcAddress(module,"WexWmGetTitle") : NULL;
        native_info = module ? (void *)GetProcAddress(module,"WexWmGetNativeInfo") : NULL;
        if (!start || !stop || !native_title || !native_info) return 2;
        if (start(WEX_WM_BACKEND_WAYLAND,0,native_event,NULL)) return 3;
        Sleep(1500);
        stop();
        return 0;
    }
    if (argc == 3 && !strcmp(argv[1],"children"))
    {
        hwnd=FindWindowA(NULL,argv[2]);
        if (!hwnd) return 2;
        EnumChildWindows(hwnd,child_info,0);
        return 0;
    }
    if (argc > 1)
    {
        printf("ACTION %s hwnd=%p style=%08lx\n",argv[1],hwnd,GetWindowLongW(hwnd,GWL_STYLE));
        if (!strcmp(argv[1],"shell-ready"))
        {
            hwnd=FindWindowW(L"Shell_TrayWnd",NULL);
            if (!hwnd || !IsWindowVisible(hwnd)) return 2;
            printf("SHELL_READY %p\n",hwnd); return 0;
        }
        else if (!strcmp(argv[1],"zorder"))
        {
            GUITHREADINFO gui = {sizeof(gui)};
            GetCursorPos(&point);
            GetGUIThreadInfo(0,&gui);
            printf("POINTER %ld,%ld hit=%p active=%p capture=%p focus=%p\n",point.x,point.y,
                WindowFromPoint(point),gui.hwndActive,gui.hwndCapture,gui.hwndFocus);
            for (hwnd=GetTopWindow(NULL);hwnd;hwnd=GetWindow(hwnd,GW_HWNDNEXT))
            {
                if (!IsWindowVisible(hwnd)) continue;
                GetWindowTextW(hwnd,title,128);
                printf("Z %p ex=%08lx role=%p atom=%p %ls\n",hwnd,GetWindowLongW(hwnd,GWL_EXSTYLE),
                    GetPropW(hwnd,L"__wine_wayland_role"),GetPropW(hwnd,L"__wine_wayland_window_handle"),title);
                GetWindowRect(hwnd,&rect);
                printf("  owner=%p lastpopup=%p\n",GetWindow(hwnd,GW_OWNER),GetLastActivePopup(hwnd));
                if (argc == 4)
                    printf("  HIT %ld\n", (long)SendMessageW(hwnd,WM_NCHITTEST,0,
                           MAKELPARAM(atoi(argv[2]),atoi(argv[3]))));
                printf("  style=%08lx rect=%ld,%ld,%ld,%ld foreground=%d host=%p group=%p focus=%p size=%p\n",
                    GetWindowLongW(hwnd,GWL_STYLE),rect.left,rect.top,rect.right,rect.bottom,GetForegroundWindow()==hwnd,
                    GetPropW(hwnd,L"__wine_wayland_native_host"),GetPropW(hwnd,L"__wine_wayland_native_group"),
                    GetPropW(hwnd,L"__wine_wayland_native_focus"),GetPropW(hwnd,L"__wine_wayland_resize_enable"));
                GetClientRect(hwnd,&rect); point.x=point.y=0; ClientToScreen(hwnd,&point);
                printf("  client=%ld,%ld %ldx%ld protocol=%p capture=%p scale120=%p\n",point.x,point.y,rect.right,rect.bottom,
                    GetPropW(hwnd,L"__wine_wayland_resize_protocol"),GetPropW(hwnd,L"__wine_wayland_native_pointer_capture"),
                    GetPropW(hwnd,L"__wine_wayland_scale_120"));
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
        else if (!strcmp(argv[1],"composition-close"))
        {
            hwnd=FindWindowW(L"WineExplorerCompositionProbe",L"Composition probe controls");
            if (!hwnd) return 2;
            PostMessageW(hwnd,WM_KEYDOWN,VK_ESCAPE,0); return 0;
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
            cls.hCursor=LoadCursorW(NULL,(LPCWSTR)IDC_ARROW);
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

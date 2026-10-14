/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright 2026 WineExplorer contributors
 * Observe actual Explorer proxies; no direct bridge binding calls.
 */
#include <windows.h>
#include <stdio.h>
#include <wexwayland.h>
static unsigned count;
static HWND taskband;
static BOOL CALLBACK find_taskband(HWND hwnd,LPARAM arg)
{
    WCHAR name[128];
    GetClassNameW(hwnd,name,128);
    if (!lstrcmpW(name,L"MSTaskListWClass")) taskband=hwnd;
    return TRUE;
}
static void task_point(void)
{
    RECT rect;
    POINT point={20,0};
    GetClientRect(taskband,&rect); point.y=rect.bottom/2;
    ClientToScreen(taskband,&point);
    printf("TASK_POINT %ld %ld\n",point.x,point.y);
}
static BOOL menu_ready(void)
{
    HWND popup=FindWindowW(L"#32768",NULL);
    return popup && IsWindowVisible(popup);
}
static BOOL CALLBACK count_host(HWND hwnd,LPARAM arg)
{
    WCHAR name[128];
    GetClassNameW(hwnd,name,128);
    if (!lstrcmpW(name,L"WineExplorerLinuxWindow")) count++;
    return TRUE;
}
static void pump(void)
{
    MSG msg;
    while (PeekMessageW(&msg,NULL,0,0,PM_REMOVE)) { TranslateMessage(&msg); DispatchMessageW(&msg); }
    Sleep(10);
}
#define WAIT(label,expr) do { DWORD start=GetTickCount(); BOOL ok; \
    do { pump(); ok=!!(expr); } while (!ok && GetTickCount()-start<15000); \
    if (!ok) { printf("FAIL %s\n",label); return 1; } } while (0)
static void pixel(HWND hwnd,const char *phase)
{
    RECT client;
    POINT point;
    GetClientRect(hwnd,&client); point.x=client.right-20; point.y=client.bottom-20;
    ClientToScreen(hwnd,&point);
    printf("PIXEL_%s %ld %ld\n",phase,point.x,point.y);
}
static BOOL size_is(HWND hwnd,int width,int height)
{
    RECT client;
    GetClientRect(hwnd,&client);
    return client.right==width && client.bottom==height;
}
static BOOL gate(const char *report,const char *suffix)
{
    char path[1024];
    FILE *file;
    snprintf(path,sizeof(path),"%s.%s",report,suffix);
    if (!(file=fopen(path,"r"))) return FALSE;
    fclose(file); return TRUE;
}
int main(int argc,char **argv)
{
    HWND host,old;
    RECT bounds;
    POINT point;
    DWORD shell_pid;
    HANDLE shell_process;
    BOOL wow=FALSE;
    if (argc!=3 || !freopen(argv[2],"w",stdout)) return 2;
    setvbuf(stdout,NULL,_IONBF,0);
    WAIT("taskbar ready",FindWindowW(L"Shell_TrayWnd",NULL));
    GetWindowThreadProcessId(FindWindowW(L"Shell_TrayWnd",NULL),&shell_pid);
    shell_process=OpenProcess(PROCESS_QUERY_INFORMATION,FALSE,shell_pid);
    if (!shell_process || !IsWow64Process(shell_process,&wow) || wow!=(sizeof(void *)==4))
    { puts("FAIL shell architecture"); if (shell_process) CloseHandle(shell_process); return 1; }
    CloseHandle(shell_process);
    puts("READY_SHELL");
    if (!strcmp(argv[1],"disabled"))
    {
        DWORD start=GetTickCount();
        while (GetTickCount()-start<1000) pump();
        EnumWindows(count_host,0);
        if (count) { puts("FAIL default session adopted native windows"); return 1; }
        puts("SHELL_ADOPTION_DISABLED_PASSED"); return 0;
    }
    WAIT("native proxy",(host=FindWindowW(L"WineExplorerLinuxWindow",L"L2 Wine association peer"))!=NULL);
    WAIT("proxy group active",(ULONG_PTR)GetPropW(host,WEX_WAYLAND_GROUP_PROP)==WEX_WAYLAND_GROUP_ACTIVE);
    EnumWindows(count_host,0);
    if (count!=1 || !size_is(host,240,180)) { puts("FAIL proxy count or client size"); return 1; }
    if (GetWindowLongW(host,GWL_STYLE)&(WS_THICKFRAME|WS_MAXIMIZEBOX)) { puts("FAIL unfinished resize exposed"); return 1; }
    GetWindowRect(host,&bounds); point.x=bounds.right-1; point.y=(bounds.top+bounds.bottom)/2;
    if (WindowFromPoint(point)!=host) { puts("FAIL frame edge hit testing"); return 1; }
    pixel(host,"INITIAL"); puts("READY_TITLE");
    WAIT("native title",FindWindowW(L"WineExplorerLinuxWindow",L"L2 Shell renamed")==host);
    puts("READY_RESIZE");
    WAIT("native resize",size_is(host,260,200) &&
         (ULONG_PTR)GetPropW(host,WEX_WAYLAND_GROUP_PROP)==WEX_WAYLAND_GROUP_ACTIVE);
    GetWindowRect(host,&bounds); point.x=bounds.right-1; point.y=(bounds.top+bounds.bottom)/2;
    if (WindowFromPoint(point)!=host) { puts("FAIL resized frame edge hit testing"); return 1; }
    pixel(host,"RESIZED"); puts("READY_CAPTURE");
    WAIT("capture gate",gate(argv[2],"captured"));
    puts("READY_NATIVE_CLICK");
    WAIT("native click activates logical proxy",GetForegroundWindow()==host &&
         (ULONG_PTR)GetPropW(host,WEX_WAYLAND_FOCUS_PROP)==WEX_WAYLAND_FOCUS_NATIVE);
    PostMessageW(host,WM_SYSCOMMAND,SC_KEYMENU,' ');
    WAIT("real system menu has frame keyboard focus",(ULONG_PTR)GetPropW(host,WEX_WAYLAND_FOCUS_PROP)==WEX_WAYLAND_FOCUS_FRAME);
    puts("READY_MENU_CAPTURE");
    WAIT("menu capture gate",gate(argv[2],"menu"));
    PostMessageW(host,WM_CANCELMODE,0,0);
    WAIT("closing real menu restores native focus",(ULONG_PTR)GetPropW(host,WEX_WAYLAND_FOCUS_PROP)==WEX_WAYLAND_FOCUS_NATIVE);
    EnumChildWindows(FindWindowW(L"Shell_TrayWnd",NULL),find_taskband,0);
    if (!taskband) { puts("FAIL taskband missing"); return 1; }
    task_point(); puts("READY_TASK_MINIMIZE");
    WAIT("proxy minimized",IsIconic(host)); puts("READY_MINIMIZED");
    WAIT("native minimize confirmation",gate(argv[2],"restore"));
    puts("READY_TASK_RESTORE");
    WAIT("task button restores native focus",!IsIconic(host) && GetForegroundWindow()==host &&
         (ULONG_PTR)GetPropW(host,WEX_WAYLAND_FOCUS_PROP)==WEX_WAYLAND_FOCUS_NATIVE);
    puts("READY_TASK_MENU_CANCEL");
    WAIT("task menu opens",menu_ready()); puts("MENU_CANCEL");
    WAIT("task menu closes",!menu_ready());
    puts("READY_TASK_DEACTIVATE");
    WAIT("desktop click deactivates proxy",GetForegroundWindow()!=host);
    puts("READY_TASK_ACTIVATE");
    WAIT("inactive task button activates native focus",!IsIconic(host) && GetForegroundWindow()==host &&
         (ULONG_PTR)GetPropW(host,WEX_WAYLAND_FOCUS_PROP)==WEX_WAYLAND_FOCUS_NATIVE);
    puts("READY_TASK_MENU_MINIMIZE");
    WAIT("task menu opens",menu_ready()); puts("MENU_MINIMIZE");
    WAIT("task menu minimizes",IsIconic(host));
    puts("READY_TASK_MENU_RESTORE");
    WAIT("task menu opens",menu_ready()); puts("MENU_RESTORE");
    WAIT("task menu restores native focus",!IsIconic(host) && GetForegroundWindow()==host &&
         (ULONG_PTR)GetPropW(host,WEX_WAYLAND_FOCUS_PROP)==WEX_WAYLAND_FOCUS_NATIVE);
    puts("READY_NATIVE_MINIMIZE");
    WAIT("observed native minimize",IsIconic(host)); puts("READY_NATIVE_RESTORE");
    WAIT("observed native restore",!IsIconic(host) &&
         (ULONG_PTR)GetPropW(host,WEX_WAYLAND_GROUP_PROP)==WEX_WAYLAND_GROUP_ACTIVE);
    old=host; puts("READY_CSD");
    WAIT("CSD proxy withdrawn",!IsWindow(old));
    count=0; EnumWindows(count_host,0);
    if (count) { puts("FAIL CSD duplicate frame"); return 1; }
    puts("READY_SSD");
    WAIT("SSD proxy returns",(host=FindWindowW(L"WineExplorerLinuxWindow",L"L2 Shell renamed"))!=NULL &&
         (ULONG_PTR)GetPropW(host,WEX_WAYLAND_GROUP_PROP)==WEX_WAYLAND_GROUP_ACTIVE);
    puts("READY_TASK_MENU_CLOSE");
    WAIT("task menu opens",menu_ready()); puts("MENU_CLOSE");
    WAIT("native close withdraws proxy",!IsWindow(host));
    count=0; EnumWindows(count_host,0);
    if (count) { puts("FAIL stale shell proxy"); return 1; }
    printf("SHELL_ADOPTION_PASSED bits=%u\n",(unsigned)(8*sizeof(void *)));
    return 0;
}

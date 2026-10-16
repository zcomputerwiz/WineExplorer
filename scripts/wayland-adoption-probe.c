/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright 2026 WineExplorer contributors
 * Observe actual Explorer proxies; no direct bridge binding calls.
 */
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
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
static BOOL moving(HWND hwnd)
{
    GUITHREADINFO info={sizeof(info)};
    return GetGUIThreadInfo(GetWindowThreadProcessId(hwnd,NULL),&info) &&
           (info.flags & GUI_INMOVESIZE);
}
static BOOL move_focus_ready(HWND hwnd)
{
    static DWORD previous=~0u;
    DWORD state=(moving(hwnd) ? 1 : 0) | (GetForegroundWindow()==hwnd ? 2 : 0) |
                ((ULONG_PTR)GetPropW(hwnd,WEX_WAYLAND_FOCUS_PROP)<<2);
    if (state!=previous) { printf("MOVE_STATE %lu\n",state); previous=state; }
    return state==(1|2|(WEX_WAYLAND_FOCUS_FRAME<<2));
}
static BOOL move_position_ready(HWND hwnd,const RECT *original)
{
    static RECT previous;
    RECT rect;
    if (!GetWindowRect(hwnd,&rect)) return FALSE;
    if (!EqualRect(&previous,&rect))
    { printf("MOVE_RECT %ld %ld %ld %ld\n",rect.left,rect.top,rect.right,rect.bottom); previous=rect; }
    return rect.left==original->left+64 && rect.top==original->top+32;
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
static BOOL CALLBACK dump_host(HWND hwnd, LPARAM unused)
{
    RECT bounds, region_box;
    HRGN region;
    int type;
    if (!GetPropW(hwnd,WEX_WAYLAND_HOST_PROP)) return TRUE;
    region=CreateRectRgn(0,0,0,0);
    type=GetWindowRgn(hwnd,region); GetRgnBox(region,&region_box); GetWindowRect(hwnd,&bounds);
    printf("HOST_STATE hwnd=%p foreground=%p iconic=%d group=%llu focus=%llu bounds=%ld,%ld,%ld,%ld region=%d:%ld,%ld,%ld,%ld\n",
           hwnd,GetForegroundWindow(),IsIconic(hwnd),
           (unsigned long long)(ULONG_PTR)GetPropW(hwnd,WEX_WAYLAND_GROUP_PROP),
           (unsigned long long)(ULONG_PTR)GetPropW(hwnd,WEX_WAYLAND_FOCUS_PROP),
           bounds.left,bounds.top,bounds.right,bounds.bottom,type,
           region_box.left,region_box.top,region_box.right,region_box.bottom);
    DeleteObject(region);
    return TRUE;
}
#define WAIT(label,expr) do { DWORD start=GetTickCount(); BOOL ok; \
    do { pump(); ok=!!(expr); } while (!ok && GetTickCount()-start<15000); \
    if (!ok) { EnumWindows(dump_host,0); printf("FAIL %s\n",label); return 1; } } while (0)
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
static BOOL limits_match(HWND hwnd, int min_width, int min_height, int max_width, int max_height)
{
    MINMAXINFO info={0};
    RECT minimum={0,0,min_width,min_height}, maximum={0,0,max_width,max_height};
    DWORD style=GetWindowLongW(hwnd,GWL_STYLE), ex_style=GetWindowLongW(hwnd,GWL_EXSTYLE);
    info.ptMinTrackSize=(POINT){33333,44444};
    info.ptMaxTrackSize=(POINT){11111,22222};
    AdjustWindowRectEx(&minimum,style,FALSE,ex_style);
    AdjustWindowRectEx(&maximum,style,FALSE,ex_style);
    SendMessageW(hwnd,WM_GETMINMAXINFO,0,(LPARAM)&info);
    return info.ptMinTrackSize.x==(min_width ? minimum.right-minimum.left : 33333) &&
           info.ptMinTrackSize.y==(min_height ? minimum.bottom-minimum.top : 44444) &&
           info.ptMaxTrackSize.x==(max_width ? maximum.right-maximum.left : 11111) &&
           info.ptMaxTrackSize.y==(max_height ? maximum.bottom-maximum.top : 22222);
}
static BOOL gate(const char *report,const char *suffix)
{
    char path[1024];
    FILE *file;
    snprintf(path,sizeof(path),"%s.%s",report,suffix);
    if (!(file=fopen(path,"r"))) return FALSE;
    fclose(file); return TRUE;
}
static int test_move(HWND host,const char *report,const char *phase,BOOL cancel,BOOL drag)
{
    RECT bounds,final;
    POINT point;
    GetWindowRect(host,&bounds);
    printf("MOVE_ORIGINAL_%s %ld %ld %ld %ld\n",phase,bounds.left,bounds.top,bounds.right,bounds.bottom);
    if (drag) printf("PRESS_POINT %ld %ld\n",(bounds.left+bounds.right)/2,bounds.top+10);
    printf("READY_TASK_MENU_MOVE_%s\n",phase);
    if (!drag) { WAIT("task move menu opens",menu_ready()); printf("MENU_MOVE_%s\n",phase); }
    WAIT("task move has frame keyboard focus",move_focus_ready(host));
    WAIT("task move has physical pointer capture",GetPropW(host,WEX_WAYLAND_CAPTURE_PROP));
    WAIT("move cursor reaches caption",GetCursorPos(&point) &&
         point.x>=bounds.left && point.x<bounds.right &&
         point.y>=bounds.top && point.y<bounds.top+GetSystemMetrics(SM_CYCAPTION)+8);
    printf("MOVE_POINT_%s %ld %ld\n",phase,point.x+64,point.y+32);
    printf("READY_MOVE_INPUT_%s\n",phase);
    WAIT("task move changes frame position",move_position_ready(host,&bounds));
    printf("READY_MOVE_FINISH_%s\n",phase);
    if (!strcmp(phase,"MOVE_CANCELMODE"))
    {
        WAIT("cancel mode gate",gate(report,"cancel-mode"));
        PostMessageW(host,WM_CANCELMODE,0,0);
    }
    WAIT("task move exits with native focus",!moving(host) && GetForegroundWindow()==host &&
         (ULONG_PTR)GetPropW(host,WEX_WAYLAND_FOCUS_PROP)==WEX_WAYLAND_FOCUS_NATIVE);
    WAIT("task move releases physical pointer capture",!GetPropW(host,WEX_WAYLAND_CAPTURE_PROP));
    /* WM_EXITSIZEMOVE and physical focus/capture changes can precede the
     * final Escape rollback SetWindowPos. Await geometry independently. */
    WAIT("task move final position",GetWindowRect(host,&final) &&
         (cancel ? EqualRect(&bounds,&final) : move_position_ready(host,&bounds)));
    pixel(host,phase);
    point.x=point.y=0; ClientToScreen(host,&point);
    printf("NATIVE_%s %ld %ld 260 200\n",phase,point.x,point.y);
    printf("READY_MOVE_CAPTURE_%s\n",phase);
    WAIT("move capture gate",gate(report,phase));
    return 0;
}
static int stress_readoption(HWND host,unsigned iterations)
{
    for (unsigned i=0;i<iterations;++i)
    {
        HWND old=host;
        printf("READY_STRESS_CSD_%u\n",i);
        WAIT("stress CSD withdraws frame",!IsWindow(old));
        count=0; EnumWindows(count_host,0);
        if (count) { puts("FAIL stress CSD duplicate frame"); return 1; }
        printf("READY_STRESS_SSD_%u\n",i);
        WAIT("stress SSD adopts frame",(host=FindWindowW(L"WineExplorerLinuxWindow",L"L2 Wine association peer")) &&
             (ULONG_PTR)GetPropW(host,WEX_WAYLAND_GROUP_PROP)==WEX_WAYLAND_GROUP_ACTIVE);
        count=0; EnumWindows(count_host,0);
        if (count!=1 || !size_is(host,240,180)) { puts("FAIL stress frame count or size"); return 1; }
    }
    printf("SHELL_READOPTION_STRESS_PASSED bits=%u iterations=%u\n",(unsigned)(8*sizeof(void *)),iterations);
    return 0;
}
static int owners(const char *report)
{
    HWND parent,child,old;
    WAIT("owner proxy",(parent=FindWindowW(L"WineExplorerLinuxWindow",L"L2 Wine association peer")) &&
         (ULONG_PTR)GetPropW(parent,WEX_WAYLAND_GROUP_PROP)==WEX_WAYLAND_GROUP_ACTIVE);
    puts("READY_OWNER_CHILD");
    WAIT("owned proxy",(child=FindWindowW(L"WineExplorerLinuxWindow",L"L2 Shell child")) &&
         GetWindow(child,GW_OWNER)==parent &&
         (ULONG_PTR)GetPropW(child,WEX_WAYLAND_GROUP_PROP)==WEX_WAYLAND_GROUP_ACTIVE);
    count=0; EnumWindows(count_host,0);
    if (count!=2 || !size_is(child,240,180)) { puts("FAIL owned proxy count or size"); return 1; }
    pixel(child,"CHILD"); puts("READY_OWNER_CLEAR");
    WAIT("owner removed",!GetWindow(child,GW_OWNER));
    puts("READY_OWNER_SET");
    WAIT("owner restored",GetWindow(child,GW_OWNER)==parent);
    old=parent; puts("READY_OWNER_CSD");
    WAIT("unsupported parent frame removed",!IsWindow(old));
    WAIT("child survives parent frame withdrawal",IsWindow(child) && !GetWindow(child,GW_OWNER) &&
         (ULONG_PTR)GetPropW(child,WEX_WAYLAND_GROUP_PROP)==WEX_WAYLAND_GROUP_ACTIVE);
    puts("READY_OWNER_SSD");
    WAIT("returning owner proxy relinked",(parent=FindWindowW(L"WineExplorerLinuxWindow",L"L2 Wine association peer")) &&
         GetWindow(child,GW_OWNER)==parent &&
         (ULONG_PTR)GetPropW(parent,WEX_WAYLAND_GROUP_PROP)==WEX_WAYLAND_GROUP_ACTIVE);
    count=0; EnumWindows(count_host,0);
    if (count!=2) { puts("FAIL returning owner proxy count"); return 1; }
    old=parent; puts("READY_OWNER_UNMAP");
    WAIT("unmapped parent proxy removed",!IsWindow(old));
    WAIT("native child survives unmapping",IsWindow(child) && !GetWindow(child,GW_OWNER) &&
         (ULONG_PTR)GetPropW(child,WEX_WAYLAND_GROUP_PROP)==WEX_WAYLAND_GROUP_ACTIVE);
    puts("READY_OWNER_REMAP");
    WAIT("remapped parent returns",(parent=FindWindowW(L"WineExplorerLinuxWindow",L"L2 Wine association peer")) &&
         (ULONG_PTR)GetPropW(parent,WEX_WAYLAND_GROUP_PROP)==WEX_WAYLAND_GROUP_ACTIVE);
    WAIT("remap observation gate",gate(report,"remapped"));
    if (GetWindow(child,GW_OWNER)) { puts("FAIL remap restored stale ownership"); return 1; }
    count=0; EnumWindows(count_host,0);
    if (count!=2) { puts("FAIL remapped owner proxy count"); return 1; }
    puts("READY_OWNER_RELATE");
    WAIT("explicit remapped parent assignment",GetWindow(child,GW_OWNER)==parent);
    puts("READY_OWNER_CLOSE");
    WAIT("closed parent removed",!IsWindow(parent));
    WAIT("native child survives parent closure",IsWindow(child) && !GetWindow(child,GW_OWNER) &&
         (ULONG_PTR)GetPropW(child,WEX_WAYLAND_GROUP_PROP)==WEX_WAYLAND_GROUP_ACTIVE);
    count=0; EnumWindows(count_host,0);
    if (count!=1) { puts("FAIL surviving child proxy count"); return 1; }
    puts("READY_CHILD_CLOSE");
    WAIT("child close gate",gate(report,"close-child"));
    PostMessageW(child,WM_CLOSE,0,0);
    WAIT("child proxy removed",!IsWindow(child));
    count=0; EnumWindows(count_host,0);
    if (count) { puts("FAIL stale owned proxy"); return 1; }
    printf("SHELL_OWNERS_PASSED bits=%u\n",(unsigned)(8*sizeof(void *)));
    return 0;
}
static BOOL edge_hit_ready(HWND host)
{
    RECT bounds;
    POINT point;
    GetWindowRect(host,&bounds);
    point.x=bounds.right-1; point.y=(bounds.top+bounds.bottom)/2;
    return WindowFromPoint(point)==host;
}
static int run(int argc,char **argv)
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
    if (!strcmp(argv[1],"owners")) return owners(argv[2]);
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
    if (getenv("WEX_SHELL_TEST_REBIND_STRESS") && atoi(getenv("WEX_SHELL_TEST_REBIND_STRESS"))>0)
        return stress_readoption(host,atoi(getenv("WEX_SHELL_TEST_REBIND_STRESS")));
    EnumWindows(count_host,0);
    if (count!=1 || !size_is(host,240,180)) { puts("FAIL proxy count or client size"); return 1; }
    if (GetWindowLongW(host,GWL_STYLE)&(WS_THICKFRAME|WS_MAXIMIZEBOX)) { puts("FAIL unfinished resize exposed"); return 1; }
    puts("READY_LIMITED");
    WAIT("native maximum constraints",limits_match(host,120,90,320,240));
    puts("READY_FIXED");
    WAIT("native fixed-size constraints",limits_match(host,240,180,240,180));
    puts("READY_HEIGHT_LIMIT");
    WAIT("native single-axis maximum",limits_match(host,120,90,0,200));
    puts("READY_UNLIMITED");
    WAIT("native maximum reset",limits_match(host,120,90,0,0));
    WAIT("frame edge hit testing",edge_hit_ready(host));
    pixel(host,"INITIAL"); puts("READY_TITLE");
    WAIT("native title",FindWindowW(L"WineExplorerLinuxWindow",L"L2 Shell renamed")==host);
    puts("READY_RESIZE");
    WAIT("native resize",size_is(host,260,200) &&
         (ULONG_PTR)GetPropW(host,WEX_WAYLAND_GROUP_PROP)==WEX_WAYLAND_GROUP_ACTIVE);
    WAIT("resized frame edge hit testing",edge_hit_ready(host));
    pixel(host,"RESIZED"); puts("READY_CAPTURE");
    WAIT("capture gate",gate(argv[2],"captured"));
    puts("READY_NATIVE_CLICK");
    /* Foreground/focus may already match before the harness injects input.
     * Do not open a menu which that still-pending body click would dismiss. */
    WAIT("native body click completed",gate(argv[2],"native-clicked"));
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
    if (test_move(host,argv[2],"MOVE_COMMIT",FALSE,FALSE) ||
        test_move(host,argv[2],"MOVE_CANCEL",TRUE,FALSE) ||
        test_move(host,argv[2],"MOVE_DRAG",FALSE,TRUE) ||
        test_move(host,argv[2],"MOVE_CANCELMODE",FALSE,FALSE)) return 1;
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
int main(int argc,char **argv)
{
    BOOL drag=TRUE;
    int result;
    /* These move checks observe the frame before terminal input, so explicitly
     * select full-window dragging instead of assuming the prefix preference. */
    if (!SystemParametersInfoW(SPI_GETDRAGFULLWINDOWS,0,&drag,0) ||
        !SystemParametersInfoW(SPI_SETDRAGFULLWINDOWS,TRUE,NULL,0)) return 2;
    result=run(argc,argv);
    if (!SystemParametersInfoW(SPI_SETDRAGFULLWINDOWS,drag,NULL,0)) return 2;
    return result;
}

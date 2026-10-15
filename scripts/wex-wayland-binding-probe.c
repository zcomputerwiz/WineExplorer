/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright 2026 WineExplorer contributors
 * Real wexhost binding API, independent bridge/driver Wayland connections.
 */
#include <windows.h>
#include <stdio.h>
#include <wexhost.h>
#include <wexwayland.h>
static CRITICAL_SECTION lock;
static UINT64 native_id;
static unsigned failures;
struct worker_args { UINT64 id; HWND hwnd; DWORD bind, configure, release; };
static DWORD WINAPI wrong_thread(void *arg)
{
    struct worker_args *worker=arg;
    worker->bind=WexWmBindHost(worker->id,worker->hwnd,NULL,NULL);
    worker->configure=WexWmConfigure(worker->id,NULL,NULL);
    worker->release=WexWmRelease(worker->id,NULL);
    return 0;
}
static void check(const char *label,BOOL valid)
{
    if (!valid) { printf("FAIL %s\n",label); failures++; }
}
static BOOL same_region(HRGN a, HRGN b)
{
    HRGN difference=CreateRectRgn(0,0,0,0);
    BOOL same=CombineRgn(difference,a,b,RGN_XOR)==NULLREGION;
    DeleteObject(difference);
    return same;
}
static void CALLBACK observed(const WEX_WM_EVENT *event,void *context)
{
    if (event->type != WEX_WM_MAP) return;
    EnterCriticalSection(&lock);
    if (!native_id) native_id=event->window;
    LeaveCriticalSection(&lock);
}
static void pump(DWORD delay)
{
    DWORD start=GetTickCount();
    MSG msg;
    do {
        while (PeekMessageW(&msg,NULL,0,0,PM_REMOVE)) { TranslateMessage(&msg); DispatchMessageW(&msg); }
        Sleep(10);
    } while (GetTickCount()-start<delay);
}
static BOOL wait_prop(HWND hwnd,LPCWSTR prop,ULONG_PTR value)
{
    DWORD start=GetTickCount();
    do { pump(10); if ((ULONG_PTR)GetPropW(hwnd,prop)==value) return TRUE; }
    while (GetTickCount()-start<10000);
    return FALSE;
}
static BOOL wait_binding(UINT64 id,HWND hwnd)
{
    DWORD start=GetTickCount(), result;
    do {
        result=WexWmBindHost(id,hwnd,NULL,NULL);
        if (result != ERROR_IO_PENDING) return result == ERROR_SUCCESS;
        pump(10);
    } while (GetTickCount()-start<10000);
    return FALSE;
}
static HWND create_proxy(HWND owner)
{
    RECT size={0,0,240,180};
    AdjustWindowRectEx(&size,WS_OVERLAPPEDWINDOW,FALSE,0);
    return CreateWindowW(L"WexBindingProbe",L"Bridge native proxy",WS_OVERLAPPEDWINDOW,
                         80,80,size.right-size.left,size.bottom-size.top,owner,NULL,GetModuleHandleW(NULL),NULL);
}
static void check_mask(HWND hwnd, HRGN outer)
{
    RECT bounds, client;
    POINT origin={0};
    HRGN expected, actual, hole;
    GetWindowRect(hwnd,&bounds); GetClientRect(hwnd,&client); ClientToScreen(hwnd,&origin);
    OffsetRect(&client,origin.x-bounds.left,origin.y-bounds.top);
    OffsetRect(&bounds,-bounds.left,-bounds.top);
    expected=CreateRectRgnIndirect(&bounds);
    if (outer) CombineRgn(expected,expected,outer,RGN_AND);
    hole=CreateRectRgnIndirect(&client); CombineRgn(expected,expected,hole,RGN_DIFF);
    actual=CreateRectRgn(0,0,0,0);
    check("live outer shape retains client hole",GetWindowRgn(hwnd,actual)!=ERROR && same_region(expected,actual));
    DeleteObject(expected); DeleteObject(actual); DeleteObject(hole);
}
static HRGN change_shape(HWND hwnd, int width, int height)
{
    HRGN shape=CreateRoundRectRgn(0,0,width,height,24,24), copy=CreateRectRgn(0,0,0,0);
    CombineRgn(copy,shape,NULL,RGN_COPY);
    check("live rounded shape installed",SetWindowRgn(hwnd,shape,FALSE));
    check_mask(hwnd,copy);
    return copy;
}
int main(int argc,char **argv)
{
    WNDCLASSW cls={0};
    HWND owner,proxy,second;
    RECT bounds,client;
    POINT point;
    DWORD start,backend,capabilities;
    UINT64 id;
    WEX_NATIVE_WINDOW info;
    char controller_name[33], gate[1024];
    FILE *ready;
    struct worker_args worker;
    HANDLE thread;
    HRGN original,region;
    if (argc!=4 || !freopen(argv[3],"w",stdout)) return 2;
    setvbuf(stdout,NULL,_IONBF,0);
    InitializeCriticalSection(&lock);
    check("AUTO remains disabled",WexWmStart(0,observed,NULL)==ERROR_NOT_SUPPORTED);
    check("explicit backend",WexWmStartEx(WEX_WM_BACKEND_WAYLAND,0,observed,NULL)==ERROR_SUCCESS);
    check("frame capability",WexWmGetCapabilities(&backend,&capabilities)==ERROR_SUCCESS &&
          backend==WEX_WM_BACKEND_WAYLAND && (capabilities&WEX_WM_CAP_FOREIGN_FRAME));
    start=GetTickCount();
    do { EnterCriticalSection(&lock); id=native_id; LeaveCriticalSection(&lock); if (id) break; pump(10); }
    while (GetTickCount()-start<10000);
    check("native view observed",id!=0);
    check("native identity",WexWmGetNativeInfo(id,&info)==ERROR_SUCCESS && !strcmp(info.token,argv[1]));
    cls.hInstance=GetModuleHandleW(NULL); cls.lpfnWndProc=DefWindowProcW; cls.lpszClassName=L"WexBindingProbe";
    RegisterClassW(&cls);
    owner=CreateWindowW(cls.lpszClassName,L"Binding owner",WS_OVERLAPPEDWINDOW,20,20,260,180,NULL,NULL,cls.hInstance,NULL);
    ShowWindow(owner,SW_SHOWNOACTIVATE);
    proxy=create_proxy(owner);
    GetWindowRect(proxy,&bounds);
    original=CreateRoundRectRgn(0,0,bounds.right-bounds.left,bounds.bottom-bounds.top,8,8);
    region=CreateRectRgn(0,0,0,0); CombineRgn(region,original,NULL,RGN_COPY);
    check("original region installed",SetWindowRgn(proxy,region,FALSE));
    check("original region readable before binding",GetWindowRgn(proxy,original)!=ERROR);
    check("stale native cannot bind",WexWmBindHost(~(UINT64)0,proxy,NULL,NULL)==ERROR_INVALID_WINDOW_HANDLE);
    check("bind before mapping is pending",WexWmBindHost(id,proxy,NULL,NULL)==ERROR_IO_PENDING);
    check("owner retained",GetWindow(proxy,GW_OWNER)==owner);
    ShowWindow(proxy,SW_SHOWNOACTIVATE);
    check("binding becomes active",wait_binding(id,proxy));
    worker.id=id; worker.hwnd=proxy;
    thread=CreateThread(NULL,0,wrong_thread,&worker,0,NULL);
    check("wrong-thread test worker",thread!=NULL);
    if (thread)
    {
        WaitForSingleObject(thread,10000); CloseHandle(thread);
        check("bind rejects wrong thread",worker.bind==ERROR_INVALID_THREAD_ID);
        check("configure rejects wrong thread",worker.configure==ERROR_INVALID_THREAD_ID);
        check("release rejects wrong thread",worker.release==ERROR_INVALID_THREAD_ID);
    }
    GlobalGetAtomNameA((ATOM)(ULONG_PTR)GetPropW(proxy,WEX_WAYLAND_CONTROLLER_PROP),controller_name,sizeof(controller_name));
    check("controller differs across observers",strcmp(controller_name,argv[2])!=0);
    GetClientRect(proxy,&client); point.x=client.right-20; point.y=client.bottom-20; ClientToScreen(proxy,&point);
    check("client excluded from Win32 hit testing",WindowFromPoint(point)!=proxy);
    second=create_proxy(owner);
    check("one frame per native view",WexWmBindHost(id,second,NULL,NULL)==ERROR_BUSY);
    DestroyWindow(second);
    SetForegroundWindow(proxy);
    check("native focus",wait_prop(proxy,WEX_WAYLAND_FOCUS_PROP,WEX_WAYLAND_FOCUS_NATIVE));
    pump(100); check("logical foreground retained",GetForegroundWindow()==proxy);
    SendMessageW(proxy,WM_ENTERMENULOOP,FALSE,0);
    check("menu gives frame keyboard focus",wait_prop(proxy,WEX_WAYLAND_FOCUS_PROP,WEX_WAYLAND_FOCUS_FRAME));
    SendMessageW(proxy,WM_ENTERSIZEMOVE,0,0);
    SendMessageW(proxy,WM_EXITMENULOOP,FALSE,0);
    pump(100); check("nested loop retains frame focus",(ULONG_PTR)GetPropW(proxy,WEX_WAYLAND_FOCUS_PROP)==WEX_WAYLAND_FOCUS_FRAME);
    SendMessageW(proxy,WM_EXITSIZEMOVE,0,0);
    check("last loop restores native focus",wait_prop(proxy,WEX_WAYLAND_FOCUS_PROP,WEX_WAYLAND_FOCUS_NATIVE));
    GetWindowRect(proxy,&bounds);
    SetWindowPos(proxy,NULL,0,0,bounds.right-bounds.left+1,bounds.bottom-bounds.top,SWP_NOMOVE|SWP_NOZORDER|SWP_NOACTIVATE);
    check("size mismatch safely detaches",wait_prop(proxy,WEX_WAYLAND_GROUP_PROP,WEX_WAYLAND_GROUP_NOT_READY));
    SetWindowPos(proxy,NULL,0,0,bounds.right-bounds.left,bounds.bottom-bounds.top,SWP_NOMOVE|SWP_NOZORDER|SWP_NOACTIVATE);
    check("matching size reattaches",wait_binding(id,proxy));
    check("client hole survives focus and geometry changes",WindowFromPoint(point)!=proxy);
    printf("HOSTING_PIXEL %ld %ld\n",point.x,point.y);
    puts("READY_CONTROLLER_END");
    /* The harness captures native rendering before this probe releases it. */
    snprintf(gate,sizeof(gate),"%s.continue",argv[3]);
    start=GetTickCount();
    do { ready=fopen(gate,"r"); if (ready) break; pump(10); }
    while (GetTickCount()-start<10000);
    check("render capture completion gate",ready!=NULL);
    if (ready) fclose(ready);
    check("explicit release",WexWmRelease(id,NULL)==ERROR_SUCCESS);
    check("release clears group",wait_prop(proxy,WEX_WAYLAND_GROUP_PROP,WEX_WAYLAND_GROUP_DETACHED));
    check("release removes identity props",!GetPropW(proxy,WEX_WAYLAND_NATIVE_PROP) && !GetPropW(proxy,WEX_WAYLAND_CONTROLLER_PROP));
    check("release restores client hit testing",WindowFromPoint(point)==proxy);
    region=CreateRectRgn(0,0,0,0);
    check("release restores original window region",GetWindowRgn(proxy,region)!=ERROR && EqualRgn(original,region));
    DeleteObject(region); DeleteObject(original);
    check("release drops native atom reference",!GlobalFindAtomA(argv[1]));
    check("released identifier invalid",WexWmGetNativeInfo(id,&info)==ERROR_INVALID_WINDOW_HANDLE);
    WexWmStop();
    EnterCriticalSection(&lock); native_id=0; LeaveCriticalSection(&lock);
    check("backend restart",WexWmStartEx(WEX_WM_BACKEND_WAYLAND,0,observed,NULL)==ERROR_SUCCESS);
    start=GetTickCount();
    do { EnterCriticalSection(&lock); id=native_id; LeaveCriticalSection(&lock); if (id) break; pump(10); }
    while (GetTickCount()-start<10000);
    second=create_proxy(owner);
    check("rebind new host before map",WexWmBindHost(id,second,NULL,NULL)==ERROR_IO_PENDING);
    ShowWindow(second,SW_SHOWNOACTIVATE);
    check("second host active",wait_binding(id,second));
    DestroyWindow(second); pump(100);
    check("destroy drops bridge atom reference",!GlobalFindAtomA(argv[1]));
    second=create_proxy(owner);
    check("destroy permits another frame",WexWmBindHost(id,second,NULL,NULL)==ERROR_IO_PENDING);
    ShowWindow(second,SW_SHOWNOACTIVATE);
    check("replacement host active",wait_binding(id,second));
    GetWindowRect(second,&bounds);
    original=change_shape(second,bounds.right-bounds.left,bounds.bottom-bounds.top);
    DeleteObject(original);
    SetWindowPos(second,NULL,0,0,bounds.right-bounds.left+40,bounds.bottom-bounds.top+30,
                 SWP_NOMOVE|SWP_NOZORDER|SWP_NOACTIVATE);
    original=change_shape(second,bounds.right-bounds.left+40,bounds.bottom-bounds.top+30);
    DeleteObject(original);
    check("live shape can become rectangular",SetWindowRgn(second,NULL,FALSE));
    check_mask(second,NULL);
    SetWindowPos(second,NULL,0,0,bounds.right-bounds.left,bounds.bottom-bounds.top,
                 SWP_NOMOVE|SWP_NOZORDER|SWP_NOACTIVATE);
    original=change_shape(second,bounds.right-bounds.left,bounds.bottom-bounds.top);
    check("shape changes preserve native binding",wait_binding(id,second));
    WexWmStop(); pump(100);
    region=CreateRectRgn(0,0,0,0);
    check("stop restores latest outer shape",GetWindowRgn(second,region)!=ERROR && same_region(original,region));
    if (!same_region(original,region))
    {
        RECT expected, actual;
        int expected_type=GetRgnBox(original,&expected), actual_type=GetRgnBox(region,&actual);
        printf("SHAPE expected=%d:%ld,%ld,%ld,%ld actual=%d:%ld,%ld,%ld,%ld\n",
               expected_type,expected.left,expected.top,expected.right,expected.bottom,
               actual_type,actual.left,actual.top,actual.right,actual.bottom);
        CombineRgn(region,region,original,RGN_XOR); GetRgnBox(region,&actual);
        printf("SHAPE difference=%ld,%ld,%ld,%ld\n",actual.left,actual.top,actual.right,actual.bottom);
    }
    DeleteObject(region); DeleteObject(original);
    check("stop removes binding props",!GetPropW(second,WEX_WAYLAND_NATIVE_PROP));
    check("stop clears group",wait_prop(second,WEX_WAYLAND_GROUP_PROP,WEX_WAYLAND_GROUP_DETACHED));
    check("stop drops native atom reference",!GlobalFindAtomA(argv[1]));
    DestroyWindow(second); DestroyWindow(proxy); DestroyWindow(owner);
    DeleteCriticalSection(&lock);
    printf("ASSOCIATION_%s bits=%u failures=%u\n",failures?"FAILED":"PASSED",(unsigned)(8*sizeof(void *)),failures);
    return failures!=0;
}

/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright 2026 WineExplorer contributors
 * Real wexhost binding API, independent bridge/driver Wayland connections.
 */
#include <windows.h>
#include <stdio.h>
#include <uxtheme.h>
#include <wexhost.h>
#include <wexwayland.h>
static CRITICAL_SECTION lock;
static UINT64 native_id;
static unsigned failures;
static BOOL refresh_during_mask;
static BOOL resize_dispatch(HWND hwnd,UINT msg,WPARAM wp,LPARAM lp);
static LRESULT CALLBACK proxy_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    if (resize_dispatch(hwnd,msg,wp,lp)) return 0;
    if (msg == WM_WINDOWPOSCHANGED && refresh_during_mask &&
        GetPropW(hwnd,L"__wine_wayland_frame_mask_updating"))
    {
        RECT rect;
        refresh_during_mask=FALSE;
        GetWindowRect(hwnd,&rect);
        SetWindowRgn(hwnd,CreateRoundRectRgn(0,0,rect.right-rect.left,rect.bottom-rect.top,24,24),FALSE);
    }
    return DefWindowProcW(hwnd,msg,wp,lp);
}
static HRESULT (WINAPI *open_theme)(LPCWSTR,LPCWSTR,LPCWSTR,HANDLE *,DWORD);
static HRESULT (WINAPI *apply_theme)(HANDLE,HWND,LPCWSTR);
static HRESULT (WINAPI *close_theme)(HANDLE);
static WCHAR previous_theme[MAX_PATH], previous_color[128], previous_size[128];
static BOOL previous_active;
static void apply_style(LPCWSTR path, LPCWSTR color, LPCWSTR size)
{
    HANDLE theme=NULL;
    HRESULT result=path ? open_theme(path,color,size,&theme,0) : S_OK;
    if (SUCCEEDED(result)) result=apply_theme(theme,NULL,NULL);
    if (theme) close_theme(theme);
    if (FAILED(result)) { printf("FAIL apply test theme %#lx\n",result); failures++; }
}
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
#ifdef WEX_WAYLAND_RESIZE_PROTOCOL_PROP
static UINT resize_message;
static DWORD resize_started;
static BOOL resize_running,resize_rollback;
static unsigned resize_next,resize_commit,resize_stages[4];
static RECT resize_client,resize_margins;
static void resize_command(HWND hwnd,unsigned serial,unsigned command)
{
    SetPropW(hwnd,WEX_WAYLAND_RESIZE_SERIAL_PROP,(HANDLE)(ULONG_PTR)serial);
    SetPropW(hwnd,WEX_WAYLAND_RESIZE_COMMAND_PROP,(HANDLE)(ULONG_PTR)command);
    SetWindowPos(hwnd,NULL,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_NOZORDER|SWP_NOACTIVATE);
}
static void resize_request(HWND hwnd,unsigned serial)
{
    int extra=serial==1 ? 20 : serial==2 ? 40 : 0;
    SetPropW(hwnd,WEX_WAYLAND_RESIZE_X_PROP,(HANDLE)(LONG_PTR)resize_client.left);
    SetPropW(hwnd,WEX_WAYLAND_RESIZE_Y_PROP,(HANDLE)(LONG_PTR)resize_client.top);
    SetPropW(hwnd,WEX_WAYLAND_RESIZE_WIDTH_PROP,(HANDLE)(LONG_PTR)(240+extra));
    SetPropW(hwnd,WEX_WAYLAND_RESIZE_HEIGHT_PROP,(HANDLE)(LONG_PTR)(180+extra));
    SetPropW(hwnd,WEX_WAYLAND_RESIZE_EDGES_PROP,(HANDLE)10); /* bottom | right */
    resize_command(hwnd,serial,WEX_WAYLAND_RESIZE_REQUEST);
}
static BOOL resize_dispatch(HWND hwnd,UINT msg,WPARAM wp,LPARAM lp)
{
    if (!resize_running) return FALSE;
    if (msg==WM_TIMER && wp==0x57455852)
    {
        if (GetTickCount()-resize_started>15000)
        {
            check("resize transport bounded completion",FALSE);
            resize_running=FALSE;
            PostMessageW(hwnd,WM_KEYDOWN,VK_RETURN,0);
        }
        else if (resize_next && GetPropW(hwnd,WEX_WAYLAND_CAPTURE_PROP) &&
                 (ULONG_PTR)GetPropW(hwnd,WEX_WAYLAND_FOCUS_PROP)==WEX_WAYLAND_FOCUS_FRAME)
        {
            unsigned serial=resize_next;
            resize_next=0;
            resize_request(hwnd,serial);
        }
        else if (resize_commit) resize_command(hwnd,resize_commit,WEX_WAYLAND_RESIZE_COMMIT);
        return TRUE;
    }
    if (msg==resize_message)
    {
        unsigned serial=wp,stage=lp;
        RECT rect;
        DWORD configure=(ULONG_PTR)GetPropW(hwnd,WEX_WAYLAND_RESIZE_EVENT_CONFIGURE_PROP);
        check("resize transition serial",serial>=1 && serial<=3);
        check("resize transition stage",stage<=WEX_WAYLAND_RESIZE_FAILED);
        if (serial>3 || !serial || stage>WEX_WAYLAND_RESIZE_FAILED) return TRUE;
        resize_stages[serial]|=1u<<stage;
        printf("RESIZE_EVENT serial=%u stage=%u configure=%lu\n",serial,stage,configure);
        if (stage==WEX_WAYLAND_RESIZE_ACCEPTED && serial==1)
        {
            resize_command(hwnd,serial,WEX_WAYLAND_RESIZE_COMMIT); /* deliberately early */
            pump(50); /* nested UI dispatch must not replace the current snapshot */
            check("resize event snapshot survives reentrancy",configure==
                (ULONG_PTR)GetPropW(hwnd,WEX_WAYLAND_RESIZE_EVENT_CONFIGURE_PROP));
        }
        if (stage==WEX_WAYLAND_RESIZE_ACCEPTED && serial==2)
            resize_command(hwnd,1,WEX_WAYLAND_RESIZE_COMMIT); /* stale, cannot finish serial 2 */
        if (stage==WEX_WAYLAND_RESIZE_ROLLING_BACK) resize_rollback=TRUE;
        if (stage==WEX_WAYLAND_RESIZE_NATIVE_READY)
        {
            check("native ready follows acknowledgement",resize_stages[serial]&(1u<<WEX_WAYLAND_RESIZE_ACKNOWLEDGED));
            if (serial==2 && !resize_rollback)
                resize_command(hwnd,serial,WEX_WAYLAND_RESIZE_CANCEL);
            else
            {
                rect.left=(LONG)(ULONG_PTR)GetPropW(hwnd,WEX_WAYLAND_RESIZE_EVENT_X_PROP);
                rect.top=(LONG)(ULONG_PTR)GetPropW(hwnd,WEX_WAYLAND_RESIZE_EVENT_Y_PROP);
                rect.right=(LONG)(ULONG_PTR)GetPropW(hwnd,WEX_WAYLAND_RESIZE_EVENT_WIDTH_PROP);
                rect.bottom=(LONG)(ULONG_PTR)GetPropW(hwnd,WEX_WAYLAND_RESIZE_EVENT_HEIGHT_PROP);
                check("native ready has positive size",rect.right>0 && rect.bottom>0);
                SetWindowPos(hwnd,NULL,rect.left-resize_margins.left,rect.top-resize_margins.top,
                    rect.right+resize_margins.left+resize_margins.right,
                    rect.bottom+resize_margins.top+resize_margins.bottom,SWP_NOZORDER|SWP_NOACTIVATE);
                RedrawWindow(hwnd,NULL,NULL,RDW_FRAME|RDW_INVALIDATE|RDW_UPDATENOW);
                resize_commit=serial; /* next UI tick, after the frame's buffer flush */
            }
        }
        else if (stage==WEX_WAYLAND_RESIZE_COMPLETE || stage==WEX_WAYLAND_RESIZE_CANCELLED)
        {
            check("resize keeps group active",(ULONG_PTR)GetPropW(hwnd,WEX_WAYLAND_GROUP_PROP)==WEX_WAYLAND_GROUP_ACTIVE);
            check("resize completion retains capture",GetPropW(hwnd,WEX_WAYLAND_CAPTURE_PROP)!=NULL);
            check("cancel publishes rollback",serial!=2 || stage==WEX_WAYLAND_RESIZE_CANCELLED);
            resize_commit=0;
            if (serial<3) resize_next=serial+1;
            else { resize_running=FALSE; PostMessageW(hwnd,WM_KEYDOWN,VK_RETURN,0); }
        }
        else if (stage==WEX_WAYLAND_RESIZE_FAILED)
        { check("resize transaction did not fail",FALSE); resize_running=FALSE; PostMessageW(hwnd,WM_KEYDOWN,VK_RETURN,0); }
        return TRUE;
    }
    return FALSE;
}
static void resize_checks(HWND hwnd)
{
    RECT bounds,client;
    POINT origin={0};
    unsigned serial,expected;
    if (!GetEnvironmentVariableW(L"WEX_ASSOCIATION_RESIZE",NULL,0)) return;
    check("resize v10 protocol advertised",(ULONG_PTR)GetPropW(hwnd,WEX_WAYLAND_RESIZE_PROTOCOL_PROP)==10);
    GetWindowRect(hwnd,&bounds); GetClientRect(hwnd,&client); ClientToScreen(hwnd,&origin);
    resize_client=client; OffsetRect(&resize_client,origin.x,origin.y);
    SetRect(&resize_margins,origin.x-bounds.left,origin.y-bounds.top,
            bounds.right-origin.x-client.right,bounds.bottom-origin.y-client.bottom);
    resize_message=RegisterWindowMessageW(WEX_WAYLAND_RESIZE_EVENT_MESSAGE);
    resize_next=1; resize_started=GetTickCount(); resize_running=TRUE;
    SetTimer(hwnd,0x57455852,20,NULL);
    /* Borrow the established move loop's real physical frame capture. This
     * tests explicit negotiation without enabling a sizing affordance. */
    SendMessageW(hwnd,WM_SYSCOMMAND,SC_MOVE|HTCAPTION,0);
    KillTimer(hwnd,0x57455852);
    for (serial=1;serial<=3;serial++)
    {
        expected=(1u<<WEX_WAYLAND_RESIZE_ACCEPTED)|(1u<<WEX_WAYLAND_RESIZE_ACKNOWLEDGED)|
                 (1u<<WEX_WAYLAND_RESIZE_NATIVE_READY)|(1u<<(serial==2 ? WEX_WAYLAND_RESIZE_CANCELLED : WEX_WAYLAND_RESIZE_COMPLETE));
        check("resize delivers every transaction stage",(resize_stages[serial]&expected)==expected);
    }
    check("resize restores native focus after completion",wait_prop(hwnd,WEX_WAYLAND_FOCUS_PROP,WEX_WAYLAND_FOCUS_NATIVE));
    check("early frame commit rejected",resize_stages[1]&(1u<<WEX_WAYLAND_RESIZE_UNAVAILABLE));
    check("old serial completion rejected",resize_stages[1]&(1u<<WEX_WAYLAND_RESIZE_STALE));
    check("resize releases capture after completion",wait_prop(hwnd,WEX_WAYLAND_CAPTURE_PROP,0));
    GetClientRect(hwnd,&client);
    check("resize test restores original client size",client.right==240 && client.bottom==180);
    puts("RESIZE_TRANSPORT_COMPLETE");
}
#else
static BOOL resize_dispatch(HWND hwnd,UINT msg,WPARAM wp,LPARAM lp) { return FALSE; }
static void resize_checks(HWND hwnd)
{
    check("resize test requires the L2 Wine topic header",
          !GetEnvironmentVariableW(L"WEX_ASSOCIATION_RESIZE",NULL,0));
}
#endif
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
static void repaint_frame(HWND hwnd)
{
    RedrawWindow(hwnd,NULL,NULL,RDW_FRAME|RDW_INVALIDATE|RDW_UPDATENOW);
    pump(50);
}
static void check_themed_shape(HWND host, HWND reference)
{
    HRGN outer=CreateRectRgn(0,0,0,0);
    repaint_frame(reference); repaint_frame(host);
    check("reference has themed outer shape",GetWindowRgn(reference,outer)!=ERROR);
    check("reference caption has rounded corner",!PtInRegion(outer,0,0));
    check_mask(host,outer);
    DeleteObject(outer);
}
static void check_theme_changes(HWND host)
{
    HWND reference=create_proxy(NULL);
    RECT bounds;
    DWORD properties=GetThemeAppProperties();
    SetWindowPos(reference,NULL,550,80,0,0,SWP_NOSIZE|SWP_NOZORDER|SWP_NOACTIVATE);
    ShowWindow(reference,SW_SHOWNOACTIVATE);
    check_themed_shape(host,reference);
    GetWindowRect(host,&bounds);
    SetWindowPos(host,NULL,0,0,bounds.right-bounds.left+40,bounds.bottom-bounds.top+30,
                 SWP_NOMOVE|SWP_NOZORDER|SWP_NOACTIVATE);
    SetWindowPos(reference,NULL,0,0,bounds.right-bounds.left+40,bounds.bottom-bounds.top+30,
                 SWP_NOMOVE|SWP_NOZORDER|SWP_NOACTIVATE);
    check_themed_shape(host,reference);
    SetThemeAppProperties(properties & ~STAP_ALLOW_NONCLIENT);
    repaint_frame(host); check_mask(host,NULL);
    SetThemeAppProperties(properties | STAP_ALLOW_NONCLIENT);
    check_themed_shape(host,reference);
    SetWindowPos(host,NULL,0,0,bounds.right-bounds.left,bounds.bottom-bounds.top,
                 SWP_NOMOVE|SWP_NOZORDER|SWP_NOACTIVATE);
    SetWindowPos(reference,NULL,0,0,bounds.right-bounds.left,bounds.bottom-bounds.top,
                 SWP_NOMOVE|SWP_NOZORDER|SWP_NOACTIVATE);
    check_themed_shape(host,reference);
    check("theme changes leave frame ownership with uxtheme",
          !GetPropW(host,L"__wine_app_frame_region"));
    SetThemeAppProperties(properties);
    DestroyWindow(reference);
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
    {
        HMODULE module=LoadLibraryW(L"uxtheme.dll");
        WCHAR path[MAX_PATH];
        open_theme=(void *)GetProcAddress(module,MAKEINTRESOURCEA(2));
        close_theme=(void *)GetProcAddress(module,MAKEINTRESOURCEA(3));
        apply_theme=(void *)GetProcAddress(module,MAKEINTRESOURCEA(4));
        if (!open_theme || !apply_theme || !close_theme ||
            !GetEnvironmentVariableW(L"WEX_FRAME_TEST_STYLE",path,MAX_PATH)) return 2;
        previous_active=SUCCEEDED(GetCurrentThemeName(previous_theme,MAX_PATH,
                                                     previous_color,128,previous_size,128));
        apply_style(path,NULL,NULL);
        check("test theme active",IsThemeActive());
    }
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
    cls.hInstance=GetModuleHandleW(NULL); cls.lpfnWndProc=proxy_proc; cls.lpszClassName=L"WexBindingProbe";
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
    resize_checks(proxy);
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
    check_theme_changes(second);
    check("themed resize returns to active native binding",wait_binding(id,second));
    DestroyWindow(second); pump(100);
    check("destroy drops bridge atom reference",!GlobalFindAtomA(argv[1]));
    second=create_proxy(owner);
    check("destroy permits another frame",WexWmBindHost(id,second,NULL,NULL)==ERROR_IO_PENDING);
    ShowWindow(second,SW_SHOWNOACTIVATE);
    check("replacement host active",wait_binding(id,second));
    GetWindowRect(second,&bounds);
    original=change_shape(second,bounds.right-bounds.left,bounds.bottom-bounds.top);
    DeleteObject(original);
    refresh_during_mask=TRUE;
    SetWindowPos(second,NULL,0,0,bounds.right-bounds.left+1,bounds.bottom-bounds.top,
                 SWP_NOMOVE|SWP_NOZORDER|SWP_NOACTIVATE);
    pump(100);
    check("outer shape refreshed inside mask installation",!refresh_during_mask);
    original=CreateRoundRectRgn(0,0,bounds.right-bounds.left+1,bounds.bottom-bounds.top,24,24);
    check_mask(second,original); DeleteObject(original);
    SetWindowPos(second,NULL,0,0,bounds.right-bounds.left,bounds.bottom-bounds.top,
                 SWP_NOMOVE|SWP_NOZORDER|SWP_NOACTIVATE);
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
    DestroyWindow(second);
    EnterCriticalSection(&lock); native_id=0; LeaveCriticalSection(&lock);
    check("themed release backend restart",WexWmStartEx(WEX_WM_BACKEND_WAYLAND,0,observed,NULL)==ERROR_SUCCESS);
    start=GetTickCount();
    do { EnterCriticalSection(&lock); id=native_id; LeaveCriticalSection(&lock); if (id) break; pump(10); }
    while (GetTickCount()-start<10000);
    second=create_proxy(owner);
    check("themed release bind before map",WexWmBindHost(id,second,NULL,NULL)==ERROR_IO_PENDING);
    ShowWindow(second,SW_SHOWNOACTIVATE);
    check("themed release host active",wait_binding(id,second));
    repaint_frame(second);
    WexWmStop();
    check("themed release clears borrowed region property",!GetPropW(second,L"__wine_wayland_frame_outer_region"));
    check("themed release preserves theme ownership",!GetPropW(second,L"__wine_app_frame_region"));
    GetWindowRect(second,&bounds);
    SetWindowPos(second,NULL,0,0,bounds.right-bounds.left+40,bounds.bottom-bounds.top+30,
                 SWP_NOMOVE|SWP_NOZORDER|SWP_NOACTIVATE);
    repaint_frame(second);
    region=CreateRectRgn(0,0,0,0);
    check("released frame keeps automatic themed shape",GetWindowRgn(second,region)!=ERROR && !PtInRegion(region,0,0));
    GetRgnBox(region,&client);
    check("released theme resizes beyond the former mask",client.right==bounds.right-bounds.left+40 &&
          client.bottom==bounds.bottom-bounds.top+30);
    DeleteObject(region);
    DestroyWindow(second); DestroyWindow(proxy); DestroyWindow(owner);
    DeleteCriticalSection(&lock);
    apply_style(previous_active ? previous_theme : NULL,previous_color,previous_size);
    printf("ASSOCIATION_%s bits=%u failures=%u\n",failures?"FAILED":"PASSED",(unsigned)(8*sizeof(void *)),failures);
    return failures!=0;
}

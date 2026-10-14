/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright 2026 WineExplorer contributors
 * Generic native association atoms on an owned Wine proxy HWND.
 */
#include <windows.h>
#include <stdio.h>
#include <wexwayland.h>
static unsigned failures;
static void check(const char *label,BOOL valid)
{
    if (!valid) { printf("FAIL %s\n",label); failures++; }
}
static BOOL wait_prop(HWND hwnd,LPCWSTR prop,ULONG_PTR status)
{
    DWORD start=GetTickCount();
    MSG msg;
    do
    {
        while (PeekMessageW(&msg,NULL,0,0,PM_REMOVE)) { TranslateMessage(&msg); DispatchMessageW(&msg); }
        if ((ULONG_PTR)GetPropW(hwnd,prop) == status) return TRUE;
        Sleep(10);
    } while (GetTickCount()-start < 10000);
    return FALSE;
}
static BOOL wait_status(HWND hwnd,ULONG_PTR status)
{
    return wait_prop(hwnd,WEX_WAYLAND_ASSOCIATION_PROP,status);
}
#ifdef WEX_TEST_HOSTING
static void notify(HWND hwnd)
{
    SetWindowPos(hwnd,NULL,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_NOACTIVATE|SWP_NOZORDER|SWP_FRAMECHANGED);
}
static void pump_for(DWORD delay)
{
    DWORD start=GetTickCount();
    MSG msg;
    do {
        while (PeekMessageW(&msg,NULL,0,0,PM_REMOVE)) { TranslateMessage(&msg); DispatchMessageW(&msg); }
        Sleep(10);
    } while (GetTickCount()-start<delay);
}
static void hosting_checks(HWND proxy,HWND parent)
{
    RECT window,client;
    POINT origin={0};
    HRGN ring,hole;
    GetWindowRect(proxy,&window); GetClientRect(proxy,&client); ClientToScreen(proxy,&origin);
    ring=CreateRectRgn(0,0,window.right-window.left,window.bottom-window.top);
    hole=CreateRectRgn(origin.x-window.left,origin.y-window.top,
        origin.x-window.left+client.right,origin.y-window.top+client.bottom);
    CombineRgn(ring,ring,hole,RGN_DIFF); DeleteObject(hole);
    check("client input exclusion",SetWindowRgn(proxy,ring,TRUE));
    origin.x+=client.right/2; origin.y+=client.bottom/2;
    check("client hole excluded from Win32 hit testing",WindowFromPoint(origin)!=proxy);
    SetPropW(proxy,WEX_WAYLAND_HOST_PROP,(HANDLE)1); notify(proxy);
    check("explicit hosting active",wait_prop(proxy,WEX_WAYLAND_GROUP_PROP,WEX_WAYLAND_GROUP_ACTIVE));
    SetForegroundWindow(proxy);
    check("native focus acknowledged",wait_prop(proxy,WEX_WAYLAND_FOCUS_PROP,WEX_WAYLAND_FOCUS_NATIVE));
    pump_for(200);
    check("native keyboard focus retains logical foreground",GetForegroundWindow()==proxy);
    SetPropW(proxy,WEX_WAYLAND_FOCUS_REQUEST_PROP,(HANDLE)WEX_WAYLAND_FOCUS_FRAME); notify(proxy);
    check("temporary frame focus",wait_prop(proxy,WEX_WAYLAND_FOCUS_PROP,WEX_WAYLAND_FOCUS_FRAME));
    check("frame keeps logical foreground",GetForegroundWindow()==proxy);
    SetPropW(proxy,WEX_WAYLAND_FOCUS_REQUEST_PROP,(HANDLE)WEX_WAYLAND_FOCUS_RESTORE_NATIVE); notify(proxy);
    check("native focus restored",wait_prop(proxy,WEX_WAYLAND_FOCUS_PROP,WEX_WAYLAND_FOCUS_NATIVE));
    check("restored native retains logical foreground",GetForegroundWindow()==proxy);
    SetForegroundWindow(parent);
    check("outside focus observed",wait_prop(proxy,WEX_WAYLAND_FOCUS_PROP,WEX_WAYLAND_FOCUS_NONE));
    SetPropW(proxy,WEX_WAYLAND_FOCUS_REQUEST_PROP,(HANDLE)WEX_WAYLAND_FOCUS_RESTORE_NATIVE); notify(proxy);
    pump_for(200);
    check("late menu completion cannot steal foreground",GetForegroundWindow()==parent);
    SetWindowPos(proxy,NULL,0,0,window.right-window.left+1,window.bottom-window.top,
        SWP_NOMOVE|SWP_NOACTIVATE|SWP_NOZORDER);
    check("mismatched resize fails safely",wait_prop(proxy,WEX_WAYLAND_GROUP_PROP,WEX_WAYLAND_GROUP_NOT_READY));
    check("resize retains reservation",wait_status(proxy,WEX_WAYLAND_ASSOCIATION_PREPARED));
    SetWindowPos(proxy,NULL,0,0,window.right-window.left,window.bottom-window.top,
        SWP_NOMOVE|SWP_NOACTIVATE|SWP_NOZORDER);
    check("matching geometry can reattach",wait_prop(proxy,WEX_WAYLAND_GROUP_PROP,WEX_WAYLAND_GROUP_ACTIVE));
    RemovePropW(proxy,WEX_WAYLAND_HOST_PROP); notify(proxy);
    check("disable hosting detaches",wait_prop(proxy,WEX_WAYLAND_GROUP_PROP,WEX_WAYLAND_GROUP_DETACHED));
    check("disable retains reservation",wait_status(proxy,WEX_WAYLAND_ASSOCIATION_PREPARED));
    SetPropW(proxy,WEX_WAYLAND_HOST_PROP,(HANDLE)1); notify(proxy);
    check("explicit hosting can resume",wait_prop(proxy,WEX_WAYLAND_GROUP_PROP,WEX_WAYLAND_GROUP_ACTIVE));
    origin.x=client.right-20; origin.y=client.bottom-20; ClientToScreen(proxy,&origin);
    printf("HOSTING_PIXEL %ld %ld\n",origin.x,origin.y);
    printf("HOSTING_CHECKS_COMPLETE group=%llu focus=%llu foreground=%p\n",
        (unsigned long long)(ULONG_PTR)GetPropW(proxy,WEX_WAYLAND_GROUP_PROP),
        (unsigned long long)(ULONG_PTR)GetPropW(proxy,WEX_WAYLAND_FOCUS_PROP),GetForegroundWindow());
}
#endif
int main(int argc,char **argv)
{
    WNDCLASSW cls={0};
    HWND parent,proxy;
    ATOM native,controller,invalid;
    RECT size={0,0,240,180};
    if (argc!=4 || !freopen(argv[3],"w",stdout)) return 2;
    setvbuf(stdout,NULL,_IONBF,0);
    native=GlobalAddAtomA(argv[1]); controller=GlobalAddAtomA(argv[2]);
    check("global atoms",native && controller);
    cls.hInstance=GetModuleHandleW(NULL); cls.lpfnWndProc=DefWindowProcW;
    cls.lpszClassName=L"WexAssociationProbe";
    RegisterClassW(&cls);
    parent=CreateWindowW(cls.lpszClassName,L"Association owner",WS_OVERLAPPEDWINDOW,
        20,20,260,180,NULL,NULL,cls.hInstance,NULL);
    ShowWindow(parent,SW_SHOWNOACTIVATE);
    AdjustWindowRectEx(&size,WS_OVERLAPPEDWINDOW,FALSE,0);
    proxy=CreateWindowW(cls.lpszClassName,L"Owned native proxy",WS_OVERLAPPEDWINDOW,
        80,80,size.right-size.left,size.bottom-size.top,parent,NULL,cls.hInstance,NULL);
    check("owned HWND",proxy && GetWindow(proxy,GW_OWNER)==parent);
    SetPropW(proxy,WEX_WAYLAND_NATIVE_PROP,(HANDLE)(ULONG_PTR)native);
    SetPropW(proxy,WEX_WAYLAND_CONTROLLER_PROP,(HANDLE)(ULONG_PTR)controller);
    ShowWindow(proxy,SW_SHOWNOACTIVATE);
    check("owned proxy independent association",wait_status(proxy,WEX_WAYLAND_ASSOCIATION_PREPARED));
    check("reservation alone does not host",!GetPropW(proxy,WEX_WAYLAND_GROUP_PROP));
    check("owner preserved",GetWindow(proxy,GW_OWNER)==parent);
    invalid=GlobalAddAtomA("invalid-native-controller");
    SetPropW(proxy,WEX_WAYLAND_CONTROLLER_PROP,(HANDLE)(ULONG_PTR)invalid);
    SetWindowPos(proxy,NULL,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_NOACTIVATE|SWP_NOZORDER|SWP_FRAMECHANGED);
    check("invalid controller rejected",wait_status(proxy,WEX_WAYLAND_ASSOCIATION_UNAVAILABLE));
    SetPropW(proxy,WEX_WAYLAND_CONTROLLER_PROP,(HANDLE)(ULONG_PTR)controller);
    SetWindowPos(proxy,NULL,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_NOACTIVATE|SWP_NOZORDER|SWP_FRAMECHANGED);
    check("association restored",wait_status(proxy,WEX_WAYLAND_ASSOCIATION_PREPARED));
    GlobalDeleteAtom(invalid);
#ifdef WEX_TEST_HOSTING
    hosting_checks(proxy,parent);
#endif
    puts("READY_CONTROLLER_END");
    check("controller expiration clears status",wait_status(proxy,WEX_WAYLAND_ASSOCIATION_UNBOUND));
#ifdef WEX_TEST_HOSTING
    check("controller expiration detaches group",wait_prop(proxy,WEX_WAYLAND_GROUP_PROP,WEX_WAYLAND_GROUP_DETACHED));
#endif
    DestroyWindow(proxy); DestroyWindow(parent);
    GlobalDeleteAtom(native); GlobalDeleteAtom(controller);
    printf("ASSOCIATION_%s bits=%u failures=%u\n",failures?"FAILED":"PASSED",(unsigned)(8*sizeof(void *)),failures);
    return failures != 0;
}

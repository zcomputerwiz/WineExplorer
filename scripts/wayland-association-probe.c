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
static BOOL wait_status(HWND hwnd,ULONG_PTR status)
{
    DWORD start=GetTickCount();
    MSG msg;
    do
    {
        while (PeekMessageW(&msg,NULL,0,0,PM_REMOVE)) { TranslateMessage(&msg); DispatchMessageW(&msg); }
        if ((ULONG_PTR)GetPropW(hwnd,WEX_WAYLAND_ASSOCIATION_PROP) == status) return TRUE;
        Sleep(10);
    } while (GetTickCount()-start < 10000);
    return FALSE;
}
int main(int argc,char **argv)
{
    WNDCLASSW cls={0};
    HWND parent,proxy;
    ATOM native,controller,invalid;
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
    proxy=CreateWindowW(cls.lpszClassName,L"Owned native proxy",WS_OVERLAPPEDWINDOW,
        80,80,300,220,parent,NULL,cls.hInstance,NULL);
    check("owned HWND",proxy && GetWindow(proxy,GW_OWNER)==parent);
    SetPropW(proxy,WEX_WAYLAND_NATIVE_PROP,(HANDLE)(ULONG_PTR)native);
    SetPropW(proxy,WEX_WAYLAND_CONTROLLER_PROP,(HANDLE)(ULONG_PTR)controller);
    ShowWindow(proxy,SW_SHOWNOACTIVATE);
    check("owned proxy independent association",wait_status(proxy,WEX_WAYLAND_ASSOCIATION_PREPARED));
    check("owner preserved",GetWindow(proxy,GW_OWNER)==parent);
    invalid=GlobalAddAtomA("invalid-native-controller");
    SetPropW(proxy,WEX_WAYLAND_CONTROLLER_PROP,(HANDLE)(ULONG_PTR)invalid);
    SetWindowPos(proxy,NULL,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_NOACTIVATE|SWP_NOZORDER|SWP_FRAMECHANGED);
    check("invalid controller rejected",wait_status(proxy,WEX_WAYLAND_ASSOCIATION_UNAVAILABLE));
    SetPropW(proxy,WEX_WAYLAND_CONTROLLER_PROP,(HANDLE)(ULONG_PTR)controller);
    SetWindowPos(proxy,NULL,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_NOACTIVATE|SWP_NOZORDER|SWP_FRAMECHANGED);
    check("association restored",wait_status(proxy,WEX_WAYLAND_ASSOCIATION_PREPARED));
    GlobalDeleteAtom(invalid);
    puts("READY_CONTROLLER_END");
    check("controller expiration clears status",wait_status(proxy,WEX_WAYLAND_ASSOCIATION_UNBOUND));
    DestroyWindow(proxy); DestroyWindow(parent);
    GlobalDeleteAtom(native); GlobalDeleteAtom(controller);
    printf("ASSOCIATION_%s bits=%u failures=%u\n",failures?"FAILED":"PASSED",(unsigned)(8*sizeof(void *)),failures);
    return failures != 0;
}

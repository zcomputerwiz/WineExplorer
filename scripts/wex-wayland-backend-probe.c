/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright 2026 WineExplorer contributors
 * Real PE/Unix Wayland inventory/control probe; graphics remain isolated X11.
 */
#include <windows.h>
#include <stdio.h>
#include <wexhost.h>
static CRITICAL_SECTION lock;
static WEX_WM_EVENT snapshot;
static BOOL withdrawn;
static unsigned failures;
static void CALLBACK event(const WEX_WM_EVENT *value, void *context)
{
    EnterCriticalSection(&lock);
    if (value->type == WEX_WM_MAP && !snapshot.window) snapshot = *value;
    else if (value->window == snapshot.window && value->type == WEX_WM_OBSERVED) snapshot = *value;
    else if (value->window == snapshot.window && value->type == WEX_WM_WITHDRAW) withdrawn = TRUE;
    LeaveCriticalSection(&lock);
}
static BOOL wait_state(DWORD state, BOOL enabled, BOOL close)
{
    DWORD start = GetTickCount();
    do
    {
        BOOL done;
        EnterCriticalSection(&lock);
        done = close ? withdrawn : snapshot.window && (!!(snapshot.states & state) == enabled);
        LeaveCriticalSection(&lock);
        if (done) return TRUE;
        Sleep(10);
    } while (GetTickCount()-start < 10000);
    return FALSE;
}
static void check(const char *label,BOOL condition)
{
    if (!condition) { printf("FAIL %s\n",label); failures++; }
}
int main(int argc,char **argv)
{
    DWORD backend,capabilities;
    WEX_NATIVE_WINDOW info;
    WCHAR title[1024];
    UINT64 window;
    if (argc < 2 || !freopen(argv[1],"w",stdout)) return 2;
    setvbuf(stdout,NULL,_IONBF,0);
    InitializeCriticalSection(&lock);
    check("AUTO not adopting",WexWmStart(0,event,NULL) == ERROR_NOT_SUPPORTED);
    check("explicit Wayland start",WexWmStartEx(WEX_WM_BACKEND_WAYLAND,0,event,NULL) == ERROR_SUCCESS);
    check("capability call",WexWmGetCapabilities(&backend,&capabilities) == ERROR_SUCCESS);
    check("observes without frame claim",backend == WEX_WM_BACKEND_WAYLAND &&
        capabilities == (WEX_WM_CAP_OBSERVE|WEX_WM_CAP_NATIVE_ENV));
    puts("READY_NATIVE");
    check("native inventory",wait_state(WEX_WM_STATE_MINIMIZED,FALSE,FALSE));
    EnterCriticalSection(&lock);
    window = snapshot.window;
    check("native logical client geometry",snapshot.rect.right > snapshot.rect.left && snapshot.rect.bottom > snapshot.rect.top);
    LeaveCriticalSection(&lock);
    check("native identity",WexWmGetNativeInfo(window,&info) == ERROR_SUCCESS && strlen(info.token) == 32);
    check("normal native endpoint",!strcmp(info.wayland_display,"wayland-0"));
    check("UTF16 native title",WexWmGetTitle(window,title,1024) == ERROR_SUCCESS && !lstrcmpW(title,L"WexWaylandBackend"));
    check("minimize request",WexWmSetState(window,WEX_WM_STATE_MINIMIZED) == ERROR_SUCCESS);
    check("observed minimize",wait_state(WEX_WM_STATE_MINIMIZED,TRUE,FALSE));
    check("restore request",WexWmSetState(window,0) == ERROR_SUCCESS);
    check("observed restore",wait_state(WEX_WM_STATE_MINIMIZED,FALSE,FALSE));
    check("activation request",WexWmFocus(window) == ERROR_SUCCESS);
    check("observed activation",wait_state(WEX_WM_STATE_FOCUSED,TRUE,FALSE));
    check("graceful close",WexWmClose(window) == ERROR_SUCCESS);
    check("withdraw delivery",wait_state(0,FALSE,TRUE));
    check("stale identity rejected",WexWmGetNativeInfo(window,&info) == ERROR_INVALID_WINDOW_HANDLE);
    WexWmStop();
    check("stopped",WexWmGetCapabilities(&backend,&capabilities) == ERROR_NOT_READY);
    WexWmStop();
    check("restart",WexWmStartEx(WEX_WM_BACKEND_WAYLAND,0,event,NULL) == ERROR_SUCCESS);
    check("old identifiers stay invalid",WexWmGetNativeInfo(window,&info) == ERROR_INVALID_WINDOW_HANDLE);
    if (argc > 2)
    {
        DWORD start = GetTickCount(), status;
        puts("READY_DISCONNECT");
        do { status = WexWmGetNativeInfo(window,&info); if (status == ERROR_NOT_READY) break; Sleep(10); }
        while (GetTickCount()-start < 10000);
        check("transport loss wakes reader",status == ERROR_NOT_READY);
    }
    WexWmStop();
    DeleteCriticalSection(&lock);
    printf("WAYLAND_BACKEND_%s bits=%u failures=%u\n",failures?"FAILED":"PASSED",(unsigned)(8*sizeof(void *)),failures);
    return failures != 0;
}

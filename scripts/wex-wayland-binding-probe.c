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
static RECT native_observed;
static SIZE native_initial;
static BOOL native_withdrawn;
static int output_scale=100;
static unsigned failures;
static BOOL refresh_during_mask;
static BOOL resize_dispatch(HWND hwnd,UINT msg,WPARAM wp,LPARAM lp);
static BOOL controller_dispatch(HWND hwnd,UINT msg,WPARAM wp,LPARAM lp);
static LRESULT CALLBACK proxy_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    if (resize_dispatch(hwnd,msg,wp,lp)) return 0;
    if (controller_dispatch(hwnd,msg,wp,lp)) return 0;
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
    if (event->type != WEX_WM_MAP && event->type != WEX_WM_OBSERVED && event->type != WEX_WM_WITHDRAW) return;
    EnterCriticalSection(&lock);
    if (!native_id && event->type==WEX_WM_MAP)
    {
        native_id=event->window;
        native_initial.cx=event->rect.right-event->rect.left;
        native_initial.cy=event->rect.bottom-event->rect.top;
    }
    if (native_id==event->window)
    {
        if (event->type==WEX_WM_WITHDRAW) native_withdrawn=TRUE;
        else native_observed=event->rect;
    }
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
static BOOL resize_loop_test,resize_button_up,resize_queue_after_up,resize_timer_after_up;
static RECT resize_waiting_rect;
static void resize_apply(HWND hwnd,const RECT *rect)
{
    SetWindowPos(hwnd,NULL,rect->left-resize_margins.left,rect->top-resize_margins.top,
        rect->right+resize_margins.left+resize_margins.right,
        rect->bottom+resize_margins.top+resize_margins.bottom,SWP_NOZORDER|SWP_NOACTIVATE);
    RedrawWindow(hwnd,NULL,NULL,RDW_FRAME|RDW_INVALIDATE|RDW_UPDATENOW);
}
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
        if (resize_button_up && !(GetKeyState(VK_LBUTTON)&0x8000))
        {
            GUITHREADINFO info={sizeof(info)};
            GetGUIThreadInfo(GetCurrentThreadId(),&info);
            check("timer after button-up retains sizing loop",(info.flags&GUI_INMOVESIZE)!=0 && GetCapture()==hwnd);
            if (!resize_timer_after_up)
            {
                resize_timer_after_up=TRUE;
                resize_command(hwnd,3,WEX_WAYLAND_RESIZE_COMMIT); /* force a queue-drain reply */
                return TRUE;
            }
            if (!resize_queue_after_up) return TRUE;
            resize_apply(hwnd,&resize_waiting_rect);
            resize_button_up=FALSE;
            resize_commit=3;
        }
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
        if (resize_button_up && resize_timer_after_up && serial==3 && stage==WEX_WAYLAND_RESIZE_UNAVAILABLE)
        {
            GUITHREADINFO info={sizeof(info)};
            GetGUIThreadInfo(GetCurrentThreadId(),&info);
            check("queue drain after button-up retains sizing loop",(info.flags&GUI_INMOVESIZE)!=0 && GetCapture()==hwnd);
            resize_queue_after_up=TRUE;
        }
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
                if (serial==3 && resize_loop_test)
                {
                    INPUT input={0};
                    resize_waiting_rect=rect;
                    resize_button_up=TRUE;
                    input.type=INPUT_MOUSE; input.mi.dwFlags=MOUSEEVENTF_LEFTUP;
                    check("inject real sizing button-up",SendInput(1,&input,sizeof(input))==1);
                }
                else
                {
                    resize_apply(hwnd,&rect);
                    resize_commit=serial; /* next UI tick, after the frame's buffer flush */
                }
            }
        }
        else if (stage==WEX_WAYLAND_RESIZE_COMPLETE || stage==WEX_WAYLAND_RESIZE_CANCELLED)
        {
            check("resize keeps group active",(ULONG_PTR)GetPropW(hwnd,WEX_WAYLAND_GROUP_PROP)==WEX_WAYLAND_GROUP_ACTIVE);
            check("resize completion retains capture",GetPropW(hwnd,WEX_WAYLAND_CAPTURE_PROP)!=NULL);
            check("cancel publishes rollback",serial!=2 || stage==WEX_WAYLAND_RESIZE_CANCELLED);
            resize_commit=0;
            if (serial<3) resize_next=serial+1;
            else { resize_running=FALSE; if (!resize_loop_test) PostMessageW(hwnd,WM_KEYDOWN,VK_RETURN,0); }
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
    resize_loop_test=GetEnvironmentVariableW(L"WEX_ASSOCIATION_RESIZE_LOOP",NULL,0)!=0;
    if (resize_loop_test)
    {
        INPUT input={0};
        MSG msg;
        SetCursorPos(bounds.left+40,bounds.top+12);
        input.type=INPUT_MOUSE; input.mi.dwFlags=MOUSEEVENTF_LEFTDOWN;
        check("inject real sizing button-down",SendInput(1,&input,sizeof(input))==1);
        Sleep(30);
        /* Retrieve (update key state), but do not dispatch a caption click
         * which would enter a second interactive loop. */
        while (PeekMessageW(&msg,NULL,0,0,PM_REMOVE)) {}
        check("sizing begins with button held",(GetKeyState(VK_LBUTTON)&0x8000)!=0);
    }
    resize_next=1; resize_started=GetTickCount(); resize_running=TRUE;
    SetTimer(hwnd,0x57455852,20,NULL);
    /* Borrow the established move loop's real physical frame capture. This
     * tests explicit negotiation without enabling a sizing affordance. */
    SendMessageW(hwnd,WM_SYSCOMMAND,SC_MOVE|HTCAPTION,0);
    KillTimer(hwnd,0x57455852);
    if (resize_loop_test)
    {
        check("timer was dispatched after swallowed button-up",resize_timer_after_up);
        check("transaction queue drained before loop exit",resize_queue_after_up);
    }
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
/* The controller test supplies input, not resize requests or frame commits. */
#ifdef WEX_WAYLAND_RESIZE_ENABLE_PROP
static BOOL controller_running,controller_cancel,controller_input,controller_terminal,controller_finished;
static BOOL controller_idle,controller_local_cancel;
static BOOL controller_failed;
static BOOL controller_live,controller_multi,controller_wait_preview;
static BOOL controller_preview_captured;
static BOOL controller_keyboard,controller_edge_chosen;
static unsigned controller_previews;
static unsigned controller_abort;
static BOOL controller_aborted;
static BOOL controller_capture_lost;
static HWND controller_thief,controller_frame;
static BOOL controller_rapid,controller_rapid_sent;
static unsigned controller_accepts;
static DWORD controller_preview_time;
static char controller_preview_gate[1024];
static RECT controller_initial_client;
static BOOL native_matches_frame(HWND hwnd)
{
    RECT observed,client;
    POINT origin={0};
    EnterCriticalSection(&lock); observed=native_observed; LeaveCriticalSection(&lock);
    observed.left=MulDiv(observed.left,output_scale,100);
    observed.top=MulDiv(observed.top,output_scale,100);
    observed.right=MulDiv(observed.right,output_scale,100);
    observed.bottom=MulDiv(observed.bottom,output_scale,100);
    GetClientRect(hwnd,&client); ClientToScreen(hwnd,&origin); OffsetRect(&client,origin.x,origin.y);
    return abs(observed.left-client.left)<=1 && abs(observed.top-client.top)<=1 &&
        abs(observed.right-client.right)<=1 && abs(observed.bottom-client.bottom)<=1;
}
static UINT controller_message;
static DWORD controller_started;
static POINT controller_pointer;
static unsigned controller_events;
static int controller_dx,controller_dy;
static BOOL controller_dispatch(HWND hwnd,UINT msg,WPARAM wp,LPARAM lp)
{
    if (!controller_running) return FALSE;
    if (controller_abort==2 && controller_capture_lost)
        check("interrupted frame never reacquires capture",GetCapture()!=controller_frame);
    if (msg==WM_SIZING)
    {
        printf("CONTROLLER_SIZING edge=%lu\n",(unsigned long)wp);
        if (controller_local_cancel && !controller_terminal)
        { controller_terminal=TRUE; PostMessageW(hwnd,WM_KEYDOWN,VK_ESCAPE,0); }
    }
    if (msg==WM_TIMER && wp==0x57455843)
    {
        if (controller_wait_preview && GetFileAttributesA(controller_preview_gate)!=INVALID_FILE_ATTRIBUTES)
        {
            controller_wait_preview=FALSE; controller_terminal=TRUE;
            PostMessageW(hwnd,WM_KEYDOWN,VK_RETURN,0);
        }
        if (controller_idle && controller_live && controller_previews &&
            !controller_terminal && GetTickCount()-controller_preview_time>2200)
        {
            check("idle preview keeps capture",GetCapture()==hwnd);
            check("idle preview does not time out",!(controller_events&(1u<<WEX_WAYLAND_RESIZE_ROLLING_BACK)));
            controller_terminal=TRUE; PostMessageW(hwnd,WM_KEYDOWN,VK_ESCAPE,0);
        }
        if (GetTickCount()-controller_started>10000)
        {
            check("controller bounded completion",FALSE);
            PostMessageW(hwnd,WM_CANCELMODE,0,0);
        }
        else if (controller_keyboard && !controller_edge_chosen &&
                 (ULONG_PTR)GetPropW(hwnd,WEX_WAYLAND_FOCUS_PROP)==WEX_WAYLAND_FOCUS_FRAME)
        {
            controller_edge_chosen=TRUE;
            PostMessageW(hwnd,WM_KEYDOWN,VK_RIGHT,0);
        }
        else if (!controller_input && GetPropW(hwnd,WEX_WAYLAND_CAPTURE_PROP) &&
                 (ULONG_PTR)GetPropW(hwnd,WEX_WAYLAND_FOCUS_PROP)==WEX_WAYLAND_FOCUS_FRAME)
        {
            INPUT input={0};
            controller_input=TRUE;
            if (controller_keyboard) GetCursorPos(&controller_pointer);
            input.type=INPUT_MOUSE;
            input.mi.dx=((controller_pointer.x+controller_dx)*65536+32768)/GetSystemMetrics(SM_CXSCREEN);
            input.mi.dy=((controller_pointer.y+controller_dy)*65536+32768)/GetSystemMetrics(SM_CYSCREEN);
            input.mi.dwFlags=MOUSEEVENTF_MOVE|MOUSEEVENTF_ABSOLUTE;
            check("controller input injection",SendInput(1,&input,sizeof(input))==1);
            { POINT pos; GetCursorPos(&pos); printf("CONTROLLER_CURSOR %ld,%ld capture=%p\n",pos.x,pos.y,GetCapture()); }
            puts("CONTROLLER_INPUT");
        }
        return TRUE;
    }
    if (msg==controller_message)
    {
        controller_events|=1u<<lp;
        printf("CONTROLLER_EVENT serial=%lu stage=%ld\n",(unsigned long)wp,(long)lp);
        if (lp==WEX_WAYLAND_RESIZE_NATIVE_READY)
            printf("CONTROLLER_GEOMETRY %ld,%ld %ldx%ld\n",
                (LONG)(LONG_PTR)GetPropW(hwnd,WEX_WAYLAND_RESIZE_EVENT_X_PROP),
                (LONG)(LONG_PTR)GetPropW(hwnd,WEX_WAYLAND_RESIZE_EVENT_Y_PROP),
                (LONG)(LONG_PTR)GetPropW(hwnd,WEX_WAYLAND_RESIZE_EVENT_WIDTH_PROP),
                (LONG)(LONG_PTR)GetPropW(hwnd,WEX_WAYLAND_RESIZE_EVENT_HEIGHT_PROP));
        if (lp==WEX_WAYLAND_RESIZE_ACCEPTED)
        {
            controller_accepts++;
            if (controller_rapid && !controller_rapid_sent)
            {
                INPUT input[2]={{0}};
                unsigned i;
                controller_rapid_sent=TRUE;
                for (i=0;i<2;i++)
                {
                    input[i].type=INPUT_MOUSE;
                    input[i].mi.dx=((controller_pointer.x+(2+i)*controller_dx)*65536+32768)/GetSystemMetrics(SM_CXSCREEN);
                    input[i].mi.dy=(controller_pointer.y*65536+32768)/GetSystemMetrics(SM_CYSCREEN);
                    input[i].mi.dwFlags=MOUSEEVENTF_MOVE|MOUSEEVENTF_ABSOLUTE;
                }
                check("rapid replacement input",SendInput(2,input,sizeof(input[0]))==2);
            }
        }
        if (lp==WEX_WAYLAND_RESIZE_ACCEPTED && controller_abort &&
            controller_previews && !controller_aborted)
        {
            controller_aborted=TRUE;
            if (controller_abort==1) PostMessageW(hwnd,WM_CANCELMODE,0,0);
            else if (controller_abort==3) DestroyWindow(hwnd);
            else if (controller_abort==4) puts("READY_NATIVE_CLOSE");
            else
            {
                ShowWindow(controller_thief,SW_SHOW);
                SetForegroundWindow(controller_thief);
                SetCapture(controller_thief);
                controller_capture_lost=TRUE;
                check("external capture transfer succeeds",GetCapture()==controller_thief);
            }
        }
        if (lp==WEX_WAYLAND_RESIZE_NATIVE_READY && !controller_terminal && !controller_idle && !controller_live)
        {
            controller_terminal=TRUE;
            PostMessageW(hwnd,WM_KEYDOWN,controller_cancel?VK_ESCAPE:VK_RETURN,0);
        }
        if (lp==WEX_WAYLAND_RESIZE_COMPLETE || lp==WEX_WAYLAND_RESIZE_CANCELLED)
        {
            controller_finished=TRUE;
            if (controller_live && !controller_terminal && lp==WEX_WAYLAND_RESIZE_COMPLETE)
            {
                GUITHREADINFO info={sizeof(info)};
                GetGUIThreadInfo(GetCurrentThreadId(),&info);
                check("live preview remains inside sizing loop",(info.flags&GUI_INMOVESIZE) && GetCapture()==hwnd);
                controller_previews++; controller_preview_time=GetTickCount();
                if (controller_multi && controller_previews==1)
                {
                    INPUT input={0};
                    input.type=INPUT_MOUSE;
                    input.mi.dx=((controller_pointer.x+2*controller_dx)*65536+32768)/GetSystemMetrics(SM_CXSCREEN);
                    input.mi.dy=((controller_pointer.y+2*controller_dy)*65536+32768)/GetSystemMetrics(SM_CYSCREEN);
                    input.mi.dwFlags=MOUSEEVENTF_MOVE|MOUSEEVENTF_ABSOLUTE;
                    check("second preview input",SendInput(1,&input,sizeof(input))==1);
                }
                else if (!controller_idle)
                {
                    if (!controller_cancel && controller_previews==1 && controller_preview_gate[0] && !controller_preview_captured)
                    {
                        POINT origin={0}; RECT client;
                        ClientToScreen(hwnd,&origin); GetClientRect(hwnd,&client);
                        printf("PREVIEW_PIXEL %ld %ld\n",
                            origin.x<controller_initial_client.left?origin.x+3:origin.x+client.right-5,
                            origin.y+client.bottom/2);
                        puts("READY_LIVE_PREVIEW"); controller_wait_preview=TRUE;
                        controller_preview_captured=TRUE;
                    }
                    else
                    { controller_terminal=TRUE; PostMessageW(hwnd,WM_KEYDOWN,controller_cancel?VK_ESCAPE:VK_RETURN,0); }
                }
            }
        }
        if (lp==WEX_WAYLAND_RESIZE_FAILED)
        { controller_finished=TRUE; check("controller transaction succeeded",controller_failed || controller_abort==4); }
        return TRUE;
    }
    return FALSE;
}
static void controller_checks(HWND hwnd)
{
    static const unsigned edge_bits[]={0,4,8,1,5,9,2,6,10};
    RECT original,client,after,expected_rect;
    unsigned i,expected,edge,bits;
    BOOL previous_drag=TRUE,drag;
    char fixture[32]={0};
    BOOL limited,chosen,fixed,height,minimum,matrix,external,all_edges,native_grid;
    if (!GetEnvironmentVariableW(L"WEX_ASSOCIATION_RESIZE_CONTROLLER",NULL,0)) return;
    controller_frame=hwnd;
    controller_message=RegisterWindowMessageW(WEX_WAYLAND_RESIZE_EVENT_MESSAGE);
    controller_failed=GetEnvironmentVariableW(L"WEX_ASSOCIATION_RESIZE_FAILED",NULL,0)!=0;
    GetEnvironmentVariableA("WEX_ASSOCIATION_RESIZE_FIXTURE",fixture,sizeof(fixture));
    limited=!strcmp(fixture,"limited"); chosen=!strcmp(fixture,"chosen"); fixed=!strcmp(fixture,"fixed");
    height=!strcmp(fixture,"height"); minimum=!strcmp(fixture,"minimum"); controller_keyboard=!strcmp(fixture,"keyboard");
    controller_rapid=!strcmp(fixture,"rapid");
    external=GetEnvironmentVariableW(L"WEX_ASSOCIATION_NATIVE_APP",NULL,0)!=0;
    {
        char app[32]={0};
        GetEnvironmentVariableA("WEX_ASSOCIATION_NATIVE_APP",app,sizeof(app));
        native_grid=!strcmp(app,"foot");
    }
    all_edges=external && (!strcmp(fixture,"edges") || !strcmp(fixture,"edges-coarse"));
    {
        char mode[32]={0};
        GetEnvironmentVariableA("WEX_ASSOCIATION_ABORT",mode,sizeof(mode));
        controller_abort=!strcmp(mode,"cancel")?1:!strcmp(mode,"capture")?2:
            !strcmp(mode,"frame-close")?3:!strcmp(mode,"native-close")?4:0;
        if (controller_abort==2)
            controller_thief=CreateWindowW(L"WexBindingProbe",L"Capture owner",WS_OVERLAPPEDWINDOW,
                600,500,160,120,NULL,NULL,GetModuleHandleW(NULL),NULL);
    }
    matrix=limited || chosen || fixed || height || minimum || controller_keyboard || (external && !all_edges);
    SystemParametersInfoW(SPI_GETDRAGFULLWINDOWS,0,&previous_drag,0);
    drag=!GetEnvironmentVariableW(L"WEX_ASSOCIATION_DRAG_OFF",NULL,0);
    check("set drag mode",SystemParametersInfoW(SPI_SETDRAGFULLWINDOWS,drag,NULL,0));
    SetPropW(hwnd,WEX_WAYLAND_RESIZE_ENABLE_PROP,(HANDLE)1);
    controller_live=drag;
    for (i=0;i<(controller_failed||controller_abort||controller_rapid?1:external&&!all_edges?3:matrix?2:22);i++)
    {
        INPUT input={0};
        int actual_dx,actual_dy;
        if (failures) break;
        GetWindowRect(hwnd,&original); GetClientRect(hwnd,&client);
        edge=i<2 || i>=19?WMSZ_RIGHT:i==2?WMSZ_LEFT:1+(i-3)/2;
        bits=edge_bits[edge];
        controller_dx=i<3?20:(bits&4)?-20:(bits&8)?20:0;
        controller_dy=i<3?0:(bits&1)?-12:(bits&2)?12:0;
        if (i>=3 && (i-3)%2) { controller_dx=-controller_dx; controller_dy=-controller_dy; }
        if (!strcmp(fixture,"edges-coarse")) { controller_dx*=2; controller_dy*=3; }
        if (matrix)
        {
            edge=chosen?WMSZ_TOPLEFT:controller_keyboard||fixed?WMSZ_RIGHT:WMSZ_BOTTOMRIGHT;
            bits=edge_bits[edge];
            if (limited) { controller_dx=i?-80:120; controller_dy=i?-60:80; }
            if (height) { controller_dx=i?-120:120; controller_dy=i?-20:80; }
            if (minimum) { controller_dx=i?120:-200; controller_dy=i?90:-150; }
            if (chosen) { controller_dx=i?12:-20; controller_dy=i?14:-20; }
            if (fixed || controller_keyboard) { controller_dx=i?-20:20; controller_dy=0; }
            if (external)
            {
                edge=WMSZ_BOTTOMRIGHT; bits=edge_bits[edge];
                controller_dx=i==2?MulDiv(native_initial.cx,output_scale,100)-client.right:32;
                controller_dy=i==2?MulDiv(native_initial.cy,output_scale,100)-client.bottom:32;
            }
        }
        actual_dx=controller_dx; actual_dy=controller_dy;
        if (controller_rapid) actual_dx*=3;
        if (limited && !i) { actual_dx=80; actual_dy=60; }
        if (height && !i) actual_dy=20;
        if (minimum && !i) { actual_dx=-120; actual_dy=-90; }
        if (chosen && !i) { actual_dx=-12; actual_dy=-14; }
        if (fixed) { actual_dx=0; actual_dy=0; }
        controller_pointer.x=(bits&4)?original.left+2:(bits&8)?original.right-2:(original.left+original.right)/2;
        controller_pointer.y=(bits&1)?original.top+2:(bits&2)?original.bottom-2:(original.top+original.bottom)/2;
        input.type=INPUT_MOUSE;
        input.mi.dx=(controller_pointer.x*65536+32768)/GetSystemMetrics(SM_CXSCREEN);
        input.mi.dy=(controller_pointer.y*65536+32768)/GetSystemMetrics(SM_CYSCREEN);
        input.mi.dwFlags=MOUSEEVENTF_MOVE|MOUSEEVENTF_ABSOLUTE;
        check("controller initial pointer",SendInput(1,&input,sizeof(input))==1);
        pump(50); GetCursorPos(&controller_pointer);
        printf("CONTROLLER_BEGIN case=%u pointer=%ld,%ld frame=%ld,%ld,%ld,%ld\n",i,
            controller_pointer.x,controller_pointer.y,original.left,original.top,original.right,original.bottom);
        controller_cancel=i==1 || i>=19; controller_idle=i==19; controller_local_cancel=i==20;
        controller_multi=i==21 || controller_abort; controller_previews=0; controller_wait_preview=FALSE;
        controller_aborted=FALSE;
        controller_capture_lost=FALSE;
        controller_rapid_sent=FALSE; controller_accepts=0;
        if (matrix) { controller_cancel=FALSE; controller_idle=FALSE; }
        if (external && !all_edges) controller_cancel=i==1;
        controller_edge_chosen=FALSE;
        controller_input=FALSE; controller_terminal=FALSE;
        controller_finished=FALSE; controller_events=0;
        controller_started=GetTickCount(); controller_running=TRUE;
        if (!i)
        {
            POINT origin={0};
            GetClientRect(hwnd,&controller_initial_client); ClientToScreen(hwnd,&origin);
            OffsetRect(&controller_initial_client,origin.x,origin.y);
        }
        SetTimer(hwnd,0x57455843,20,NULL);
        /* Queue the command with the current cursor position. SendMessage
         * retains the caller's previous GetMessagePos, which can belong to the
         * preceding resize even though GetCursorPos already moved. */
        PostMessageW(hwnd,WM_SYSCOMMAND,SC_SIZE|(controller_keyboard?0:edge),
                     MAKELPARAM(controller_pointer.x,controller_pointer.y));
        pump(10);
        if (controller_abort==1 || controller_abort==2)
        {
            DWORD start=GetTickCount();
            while (!(controller_events&(1u<<WEX_WAYLAND_RESIZE_CANCELLED)) && GetTickCount()-start<5000) pump(10);
        }
        KillTimer(hwnd,0x57455843); controller_running=FALSE;
        if (controller_multi && controller_live && !controller_abort) check("two published previews precede Escape",controller_previews>=2);
        if (!controller_local_cancel) check("controller finishes before sizing loop exits",controller_finished);
        else check("local cancellation sends no transaction",controller_events==0);
        if (controller_rapid) check("new intent supersedes pending request",controller_rapid_sent && controller_accepts>=2);
        if (controller_abort==3)
        {
            GUITHREADINFO info={sizeof(info)};
            check("frame closed during pending resize",controller_aborted && !IsWindow(hwnd));
            GetGUIThreadInfo(GetCurrentThreadId(),&info);
            check("closed frame releases sizing state",!(info.flags&GUI_INMOVESIZE) && GetCapture()==NULL);
            continue;
        }
        if (controller_abort==4)
        {
            BOOL withdrawn;
            EnterCriticalSection(&lock); withdrawn=native_withdrawn; LeaveCriticalSection(&lock);
            check("native close observed during sizing",controller_aborted && withdrawn);
            check("native close fails pending transaction",controller_events&(1u<<WEX_WAYLAND_RESIZE_FAILED));
            check("native close releases capture",GetCapture()==NULL && wait_prop(hwnd,WEX_WAYLAND_CAPTURE_PROP,0));
            check("native close detaches hosting",wait_prop(hwnd,WEX_WAYLAND_GROUP_PROP,WEX_WAYLAND_GROUP_DETACHED));
            check("native close clears hosted focus",wait_prop(hwnd,WEX_WAYLAND_FOCUS_PROP,WEX_WAYLAND_FOCUS_NONE));
            continue;
        }
        if (controller_failed)
        {
            expected=(1u<<WEX_WAYLAND_RESIZE_ACCEPTED)|(1u<<WEX_WAYLAND_RESIZE_ACKNOWLEDGED)|
                (1u<<WEX_WAYLAND_RESIZE_ROLLING_BACK)|(1u<<WEX_WAYLAND_RESIZE_FAILED);
            check("failed-stage recovery delivered",(controller_events&expected)==expected);
            check("failed loop released capture",GetCapture()==NULL && wait_prop(hwnd,WEX_WAYLAND_CAPTURE_PROP,0));
            check("failed scene detached",wait_prop(hwnd,WEX_WAYLAND_GROUP_PROP,WEX_WAYLAND_GROUP_DETACHED));
            check("failed hosted focus cleared",wait_prop(hwnd,WEX_WAYLAND_FOCUS_PROP,WEX_WAYLAND_FOCUS_NONE));
            continue;
        }
        check("controller retains hosting",(ULONG_PTR)GetPropW(hwnd,WEX_WAYLAND_GROUP_PROP)==WEX_WAYLAND_GROUP_ACTIVE);
        if (controller_abort)
        {
            GUITHREADINFO info={sizeof(info)};
            expected=(1u<<WEX_WAYLAND_RESIZE_ROLLING_BACK)|(1u<<WEX_WAYLAND_RESIZE_CANCELLED);
            check("external interruption delivered",controller_aborted);
            check("interrupted transaction rolls back",(controller_events&expected)==expected);
            GetWindowRect(hwnd,&after); original.right+=controller_dx;
            check("interruption preserves last coherent preview",EqualRect(&after,&original));
            GetGUIThreadInfo(GetCurrentThreadId(),&info);
            check("interruption exits sizing loop",!(info.flags&GUI_INMOVESIZE));
            check("interruption clears frame capture",wait_prop(hwnd,WEX_WAYLAND_CAPTURE_PROP,0));
            if (controller_abort==2)
            {
                /* defwnd releases thread capture on loop exit, even if another
                 * window acquired it. The bridge must never recapture itself. */
                check("interrupted frame leaves capture released",GetCapture()!=hwnd);
                check("capture owner retains foreground",GetForegroundWindow()==controller_thief);
                ReleaseCapture(); DestroyWindow(controller_thief); controller_thief=NULL;
                SetForegroundWindow(hwnd);
            }
            else check("cancel releases Win32 capture",GetCapture()==NULL);
            check("interruption native focus recovers",wait_prop(hwnd,WEX_WAYLAND_FOCUS_PROP,WEX_WAYLAND_FOCUS_NATIVE));
            continue;
        }
        expected=(1u<<WEX_WAYLAND_RESIZE_ACCEPTED)|(1u<<WEX_WAYLAND_RESIZE_ACKNOWLEDGED)|
            (1u<<WEX_WAYLAND_RESIZE_NATIVE_READY)|(1u<<(controller_cancel && !controller_live?WEX_WAYLAND_RESIZE_CANCELLED:WEX_WAYLAND_RESIZE_COMPLETE));
        if (!controller_local_cancel) check("controller receives complete transaction",(controller_events&expected)==expected);
        check("controller restores native focus",wait_prop(hwnd,WEX_WAYLAND_FOCUS_PROP,WEX_WAYLAND_FOCUS_NATIVE));
        check("controller releases capture",wait_prop(hwnd,WEX_WAYLAND_CAPTURE_PROP,0));
        if (external)
        {
            DWORD start=GetTickCount();
            while (!native_matches_frame(hwnd) && GetTickCount()-start<3000) pump(10);
            check("real native committed geometry matches frame",native_matches_frame(hwnd));
            GetWindowRect(hwnd,&after);
            check("real client keeps stationary edges",
                abs((bits&4?after.right:after.left)-(bits&4?original.right:original.left))<=1 &&
                abs((bits&1?after.bottom:after.top)-(bits&1?original.bottom:original.top))<=1);
            if (all_edges && !native_grid && !controller_cancel)
            {
                expected_rect=original;
                if (bits&4) expected_rect.left+=actual_dx;
                if (bits&8) expected_rect.right+=actual_dx;
                if (bits&1) expected_rect.top+=actual_dy;
                if (bits&2) expected_rect.bottom+=actual_dy;
                check("real client sized edge within one physical pixel",
                    abs(after.left-expected_rect.left)<=1 && abs(after.top-expected_rect.top)<=1 &&
                    abs(after.right-expected_rect.right)<=1 && abs(after.bottom-expected_rect.bottom)<=1);
            }
            if (controller_cancel) check("real client Escape restores original",EqualRect(&after,&original));
            if (i==2 && !all_edges)
            { GetClientRect(hwnd,&after); check("real client original size restored",after.right==MulDiv(native_initial.cx,output_scale,100) && after.bottom==MulDiv(native_initial.cy,output_scale,100)); }
            continue;
        }
        expected_rect=original;
        if (!controller_cancel)
        {
            if (bits&4) expected_rect.left+=actual_dx;
            if (bits&8) expected_rect.right+=actual_dx;
            if (bits&1) expected_rect.top+=actual_dy;
            if (bits&2) expected_rect.bottom+=actual_dy;
        }
        GetWindowRect(hwnd,&after);
        check("controller edge anchoring",EqualRect(&expected_rect,&after));
        GetClientRect(hwnd,&after);
        check("controller client size",after.right==client.right+
            (expected_rect.right-expected_rect.left)-(original.right-original.left) &&
            after.bottom==client.bottom+(expected_rect.bottom-expected_rect.top)-(original.bottom-original.top));
        if (controller_cancel)
        { GetWindowRect(hwnd,&after); check("Escape restores loop original geometry",EqualRect(&original,&after)); }
    }
    RemovePropW(hwnd,WEX_WAYLAND_RESIZE_ENABLE_PROP);
    check("restore drag mode",SystemParametersInfoW(SPI_SETDRAGFULLWINDOWS,previous_drag,NULL,0));
    puts("RESIZE_CONTROLLER_COMPLETE");
}
#else
static BOOL controller_dispatch(HWND hwnd,UINT msg,WPARAM wp,LPARAM lp) { return FALSE; }
static void controller_checks(HWND hwnd)
{
    check("controller test requires the L2 Wine topic header",
          !GetEnvironmentVariableW(L"WEX_ASSOCIATION_RESIZE_CONTROLLER",NULL,0));
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
    if (GetEnvironmentVariableW(L"WEX_ASSOCIATION_NATIVE_APP",NULL,0))
    { size.right=MulDiv(native_initial.cx,output_scale,100); size.bottom=MulDiv(native_initial.cy,output_scale,100); }
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
        char value[16];
        if (GetEnvironmentVariableA("WEX_ASSOCIATION_SCALE",value,sizeof(value))) output_scale=atoi(value);
        check("supported output scale",output_scale==100 || output_scale==150);
    }
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
#ifdef WEX_WAYLAND_RESIZE_ENABLE_PROP
    snprintf(controller_preview_gate,sizeof(controller_preview_gate),"%s.preview",argv[3]);
#endif
    controller_checks(proxy);
    if (failures && GetEnvironmentVariableW(L"WEX_ASSOCIATION_RESIZE_CONTROLLER",NULL,0))
    {
        WexWmStop();
        apply_style(previous_active?previous_theme:NULL,previous_color,previous_size);
        printf("ASSOCIATION_FAILED bits=%u failures=%u\n",(unsigned)(8*sizeof(void *)),failures);
        return 1;
    }
    if (GetEnvironmentVariableW(L"WEX_ASSOCIATION_RESIZE_FAILED",NULL,0) ||
        GetEnvironmentVariableW(L"WEX_ASSOCIATION_NATIVE_APP",NULL,0) ||
        GetEnvironmentVariableW(L"WEX_ASSOCIATION_ABORT",NULL,0) ||
        GetEnvironmentVariableW(L"WEX_ASSOCIATION_SHORT",NULL,0))
    {
        if (IsWindow(proxy) && (ULONG_PTR)GetPropW(proxy,WEX_WAYLAND_GROUP_PROP)==WEX_WAYLAND_GROUP_ACTIVE)
        {
            GetClientRect(proxy,&client); point.x=client.right-20; point.y=client.bottom-20;
            ClientToScreen(proxy,&point);
        }
        printf("HOSTING_PIXEL %ld %ld\n",point.x,point.y);
        puts("READY_CONTROLLER_END");
        snprintf(gate,sizeof(gate),"%s.continue",argv[3]);
        start=GetTickCount();
        do { ready=fopen(gate,"r"); if (ready) break; pump(10); } while (GetTickCount()-start<10000);
        check("failed recovery capture gate",ready!=NULL);
        if (ready) fclose(ready);
        WexWmStop(); DestroyWindow(proxy); DestroyWindow(owner);
        DeleteObject(original); DeleteCriticalSection(&lock);
        apply_style(previous_active?previous_theme:NULL,previous_color,previous_size);
        printf("ASSOCIATION_%s bits=%u failures=%u\n",failures?"FAILED":"PASSED",(unsigned)(8*sizeof(void *)),failures);
        return failures!=0;
    }
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

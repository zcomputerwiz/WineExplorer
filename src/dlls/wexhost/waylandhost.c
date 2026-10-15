/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright 2026 WineExplorer contributors
 * Explicit Wayland proxy binding. All HWND operations run on its owning thread.
 */
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <windef.h>
#include <winbase.h>
#include <winuser.h>
#include <wingdi.h>
#include <commctrl.h>
#include <winternl.h>
#include "wexhost_unix.h"
#include "waylandhost.h"
#include "wexwayland.h"

/* Optional in-process uxtheme contract. Keep building at the production Wine
 * pin, whose uxtheme simply ignores these properties. Never cross Wayland IPC. */
#ifndef WEX_WAYLAND_FRAME_OUTER_PROP
#define WEX_WAYLAND_FRAME_OUTER_PROP L"__wine_wayland_frame_outer_region"
#define WEX_WAYLAND_FRAME_MASK_UPDATING_PROP L"__wine_wayland_frame_mask_updating"
#endif

#ifdef WEX_WAYLAND_RESIZE_PROTOCOL_PROP
struct deferred_key { struct deferred_key *next; MSG msg; };
#endif
struct host_binding
{
    struct host_binding *next;
    UINT64 window;
    HWND hwnd;
    ATOM native, controller;
    HRGN outer, mask;
    RECT client, bounds;
    unsigned refs, focus_depth;
    BOOL removed, updating, mask_dirty, mask_queued;
#ifdef WEX_WAYLAND_RESIZE_PROTOCOL_PROP
    unsigned resize_serial;
    BOOL resize_pending, resize_cancelled, terminal_pending, restore_pending;
    MSG terminal;
    struct deferred_key *keys, *keys_tail;
#endif
};
static struct host_binding *bindings;
static DWORD binding_thread;
static UINT mask_message;
#ifdef WEX_WAYLAND_RESIZE_PROTOCOL_PROP
static UINT resize_message;
static HHOOK sizing_hook;
static struct host_binding *sizing_binding;
static void stop_sizing_filter(struct host_binding *binding);
static void clear_deferred_keys(struct host_binding *binding,BOOL replay);
#endif
static const UINT_PTR subclass_id = 0x57455848;
static LRESULT CALLBACK host_subclass(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp,
                                     UINT_PTR id, DWORD_PTR ref);

static void unref(struct host_binding *binding)
{
    if (--binding->refs) return;
    if (binding->native) GlobalDeleteAtom(binding->native);
    if (binding->controller) GlobalDeleteAtom(binding->controller);
    if (binding->outer) DeleteObject(binding->outer);
    if (binding->mask) DeleteObject(binding->mask);
#ifdef WEX_WAYLAND_RESIZE_PROTOCOL_PROP
    clear_deferred_keys(binding,FALSE);
#endif
    free(binding);
}
static struct host_binding *find_binding(UINT64 window)
{
    struct host_binding *binding;
    for (binding=bindings;binding;binding=binding->next)
        if (binding->window == window) return binding;
    return NULL;
}
/* The installed ring belongs to the bridge; any other region is a new shape
 * supplied by the window. Keep it separately so neither a later resize nor
 * release reinstalls the stale bind-time shape. NULL means no explicit shape. */
static BOOL refresh_shape(struct host_binding *binding, BOOL *changed)
{
    HRGN current=CreateRectRgn(0,0,0,0);
    HRGN difference;
    int type;
    if (!current) return FALSE;
    if (GetWindowRgn(binding->hwnd,current) == ERROR)
    { DeleteObject(current); current=NULL; }
    *changed=!binding->mask || !current || !EqualRgn(current,binding->mask);
    if (*changed && binding->mask && current)
    {
        /* Server round-tripping can split equivalent rectangles differently. */
        difference=CreateRectRgn(0,0,0,0);
        if (!difference) { DeleteObject(current); return FALSE; }
        type=CombineRgn(difference,current,binding->mask,RGN_XOR);
        DeleteObject(difference);
        if (type == ERROR) { DeleteObject(current); return FALSE; }
        *changed=type != NULLREGION;
    }
    if (*changed)
    {
        if (current && !SetPropW(binding->hwnd,WEX_WAYLAND_FRAME_OUTER_PROP,current))
        { DeleteObject(current); return FALSE; }
        if (!current) RemovePropW(binding->hwnd,WEX_WAYLAND_FRAME_OUTER_PROP);
        if (binding->outer) DeleteObject(binding->outer);
        binding->outer=current;
    }
    else if (current) DeleteObject(current);
    return TRUE;
}
static BOOL set_mask(HWND hwnd, HRGN region)
{
    BOOL result;
    if (!SetPropW(hwnd,WEX_WAYLAND_FRAME_MASK_UPDATING_PROP,(HANDLE)1)) return FALSE;
    /* Repaint asynchronously: synchronous themed painting could replace the
     * region while update_mask is still installing it. */
    result=SetWindowRgn(hwnd,region,FALSE);
    RemovePropW(hwnd,WEX_WAYLAND_FRAME_MASK_UPDATING_PROP);
    if (result) RedrawWindow(hwnd,NULL,NULL,RDW_FRAME|RDW_INVALIDATE);
    return result;
}
static void detach(struct host_binding *binding, BOOL destroying)
{
    struct host_binding **cursor;
    if (binding->removed) return;
    binding->removed = TRUE;
#ifdef WEX_WAYLAND_RESIZE_PROTOCOL_PROP
    stop_sizing_filter(binding);
#endif
    for (cursor=&bindings;*cursor;cursor=&(*cursor)->next)
        if (*cursor == binding) { *cursor=binding->next; break; }
    RemoveWindowSubclass(binding->hwnd,host_subclass,subclass_id);
    RemovePropW(binding->hwnd,WEX_WAYLAND_HOST_PROP);
    RemovePropW(binding->hwnd,WEX_WAYLAND_NATIVE_PROP);
    RemovePropW(binding->hwnd,WEX_WAYLAND_CONTROLLER_PROP);
    RemovePropW(binding->hwnd,WEX_WAYLAND_FOCUS_REQUEST_PROP);
    if (!destroying)
    {
        BOOL changed;
        SetWindowPos(binding->hwnd,NULL,0,0,0,0,
                     SWP_NOMOVE|SWP_NOSIZE|SWP_NOZORDER|SWP_NOACTIVATE|SWP_FRAMECHANGED);
        refresh_shape(binding,&changed);
        if (set_mask(binding->hwnd,binding->outer)) binding->outer=NULL;
    }
    RemovePropW(binding->hwnd,WEX_WAYLAND_FRAME_OUTER_PROP);
    RemovePropW(binding->hwnd,WEX_WAYLAND_FRAME_MASK_UPDATING_PROP);
    unref(binding); /* list reference; callback/caller retains its own */
}
static BOOL update_mask(struct host_binding *binding)
{
    RECT bounds, client;
    POINT origin={0};
    HRGN ring, hole, mask, previous;
    BOOL result, changed;
    if (binding->removed) return FALSE;
    if (binding->updating) { binding->mask_dirty=TRUE; return TRUE; }
    if (!GetWindowRect(binding->hwnd,&bounds) || !GetClientRect(binding->hwnd,&client) ||
        !ClientToScreen(binding->hwnd,&origin)) return FALSE;
    OffsetRect(&client,origin.x-bounds.left,origin.y-bounds.top);
    OffsetRect(&bounds,-bounds.left,-bounds.top);
    if (!refresh_shape(binding,&changed)) return FALSE;
    if (!changed && EqualRect(&client,&binding->client) && EqualRect(&bounds,&binding->bounds)) return TRUE;
    ring=CreateRectRgnIndirect(&bounds);
    hole=CreateRectRgnIndirect(&client);
    if (!ring || !hole) { if (ring) DeleteObject(ring); if (hole) DeleteObject(hole); return FALSE; }
    if (binding->outer) CombineRgn(ring,ring,binding->outer,RGN_AND);
    CombineRgn(ring,ring,hole,RGN_DIFF);
    DeleteObject(hole);
    mask=CreateRectRgn(0,0,0,0);
    if (!mask || CombineRgn(mask,ring,NULL,RGN_COPY) == ERROR)
    { if (mask) DeleteObject(mask); DeleteObject(ring); return FALSE; }
    previous=binding->mask;
    binding->mask=mask;
    binding->updating=TRUE;
    result=set_mask(binding->hwnd,ring);
    if (!result)
    { DeleteObject(ring); DeleteObject(mask); binding->mask=previous; }
    else
    {
        if (previous) DeleteObject(previous);
        binding->client=client; binding->bounds=bounds;
    }
    binding->updating=FALSE;
    /* Themed non-client painting may synchronously install a fresh outer
     * region inside SetWindowRgn. Reconcile after that call stack unwinds,
     * rather than silently losing that update or recursively repainting. */
    if (binding->mask_dirty && !binding->removed && !binding->mask_queued)
    {
        binding->mask_dirty=FALSE;
        binding->mask_queued=PostMessageW(binding->hwnd,mask_message,0,0);
        if (!binding->mask_queued) return FALSE;
    }
    return result && !binding->removed;
}
static void request_focus(struct host_binding *binding, ULONG_PTR target)
{
    if (binding->removed || GetForegroundWindow() != binding->hwnd) return;
    SetPropW(binding->hwnd,WEX_WAYLAND_FOCUS_REQUEST_PROP,(HANDLE)target);
    SetWindowPos(binding->hwnd,NULL,0,0,0,0,
                 SWP_NOMOVE|SWP_NOSIZE|SWP_NOZORDER|SWP_NOACTIVATE|SWP_FRAMECHANGED);
}
#ifdef WEX_WAYLAND_RESIZE_PROTOCOL_PROP
static void clear_deferred_keys(struct host_binding *binding,BOOL replay)
{
    struct deferred_key *key;
    while ((key=binding->keys))
    {
        binding->keys=key->next;
        if (replay) PostMessageW(binding->hwnd,key->msg.message,key->msg.wParam,key->msg.lParam);
        free(key);
    }
    binding->keys_tail=NULL;
}
static void cancel_resize(struct host_binding *binding)
{
    if (!binding->resize_pending || binding->resize_cancelled || binding->removed) return;
    binding->resize_cancelled=TRUE;
    SetPropW(binding->hwnd,WEX_WAYLAND_RESIZE_SERIAL_PROP,(HANDLE)(ULONG_PTR)binding->resize_serial);
    SetPropW(binding->hwnd,WEX_WAYLAND_RESIZE_COMMAND_PROP,(HANDLE)WEX_WAYLAND_RESIZE_CANCEL);
    SetWindowPos(binding->hwnd,NULL,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_NOZORDER|SWP_NOACTIVATE);
}
static LRESULT CALLBACK sizing_filter(int code,WPARAM wp,LPARAM lp)
{
    struct host_binding *binding=sizing_binding;
    MSG *msg=(MSG *)lp;
    LRESULT result;
    if (code!=MSGF_SIZE || !binding || !binding->resize_pending || binding->removed ||
        GetCapture()!=binding->hwnd) return CallNextHookEx(NULL,code,wp,lp);
    binding->refs++;
    if (msg->message==WM_LBUTTONUP || (msg->message==WM_KEYDOWN &&
        (msg->wParam==VK_RETURN || msg->wParam==VK_ESCAPE)))
    {
        if (!binding->terminal_pending || (msg->message==WM_KEYDOWN && msg->wParam==VK_ESCAPE))
        {
            binding->terminal=*msg;
            binding->terminal_pending=TRUE;
        }
        if (msg->message==WM_KEYDOWN && msg->wParam==VK_ESCAPE) cancel_resize(binding);
        result=1;
    }
    else if (binding->terminal_pending ||
             !((msg->message>=WM_KEYFIRST && msg->message<=WM_KEYLAST) ||
               (msg->message>=WM_MOUSEFIRST && msg->message<=WM_MOUSELAST)))
    {
        /* Retrieving button-up updates Wine's key state even though the hook
         * consumes it. Letting a subsequent timer/event reach defwnd's dispatch
         * branch would therefore exit the loop before publication. Dispatch
         * non-input here and consume the message even if that dispatch completes
         * the transaction or destroys the binding. Freeze motion/keys until the
         * saved terminal input is replayed. */
        if (binding->terminal_pending && msg->message>=WM_KEYFIRST && msg->message<=WM_KEYLAST)
        {
            struct deferred_key *key=calloc(1,sizeof(*key));
            if (key)
            {
                key->msg=*msg;
                if (binding->keys_tail) binding->keys_tail->next=key;
                else binding->keys=key;
                binding->keys_tail=key;
            }
            else cancel_resize(binding);
        }
        else if (!((msg->message>=WM_KEYFIRST && msg->message<=WM_KEYLAST) ||
              (msg->message>=WM_MOUSEFIRST && msg->message<=WM_MOUSELAST)))
        {
            TranslateMessage(msg);
            DispatchMessageW(msg);
        }
        result=1;
    }
    else result=CallNextHookEx(NULL,code,wp,lp);
    unref(binding);
    return result;
}
static void stop_sizing_filter(struct host_binding *binding)
{
    if (sizing_binding!=binding) return;
    UnhookWindowsHookEx(sizing_hook);
    sizing_hook=NULL; sizing_binding=NULL;
    binding->terminal_pending=FALSE;
    clear_deferred_keys(binding,FALSE);
    unref(binding);
}
static void start_sizing_filter(struct host_binding *binding)
{
    if (sizing_binding || (ULONG_PTR)GetPropW(binding->hwnd,WEX_WAYLAND_RESIZE_PROTOCOL_PROP)<10) return;
    sizing_hook=SetWindowsHookExW(WH_MSGFILTER,sizing_filter,NULL,GetCurrentThreadId());
    if (sizing_hook) { sizing_binding=binding; binding->refs++; }
}
static void resize_transition(struct host_binding *binding,WPARAM serial,LPARAM stage)
{
    if (stage==WEX_WAYLAND_RESIZE_ACCEPTED)
    {
        binding->resize_serial=serial;
        binding->resize_pending=TRUE;
        binding->resize_cancelled=FALSE;
    }
    else if (serial==binding->resize_serial &&
             (stage==WEX_WAYLAND_RESIZE_COMPLETE || stage==WEX_WAYLAND_RESIZE_CANCELLED ||
              stage==WEX_WAYLAND_RESIZE_FAILED))
    {
        binding->resize_pending=FALSE;
        if (binding->terminal_pending && !binding->removed)
        {
            MSG terminal=binding->terminal;
            binding->terminal_pending=FALSE;
            PostMessageW(binding->hwnd,terminal.message,terminal.wParam,terminal.lParam);
            clear_deferred_keys(binding,GetForegroundWindow()==binding->hwnd);
        }
        if (binding->restore_pending && !binding->focus_depth && !binding->removed)
        {
            binding->restore_pending=FALSE;
            if (stage!=WEX_WAYLAND_RESIZE_FAILED) request_focus(binding,WEX_WAYLAND_FOCUS_RESTORE_NATIVE);
        }
    }
}
#endif
static LRESULT CALLBACK host_subclass(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp,
                                     UINT_PTR id, DWORD_PTR ref)
{
    struct host_binding *binding=(struct host_binding *)ref;
    LRESULT result;
    binding->refs++;
#ifdef WEX_WAYLAND_RESIZE_PROTOCOL_PROP
    if (msg==resize_message) resize_transition(binding,wp,lp);
    if (msg==WM_ENTERSIZEMOVE) start_sizing_filter(binding);
    if (msg==WM_EXITSIZEMOVE) stop_sizing_filter(binding);
    if (msg==WM_CANCELMODE || (msg==WM_CAPTURECHANGED && (HWND)lp!=hwnd)) cancel_resize(binding);
#endif
    if (msg == mask_message)
    {
        binding->mask_queued=FALSE;
        binding->mask_dirty=FALSE;
        update_mask(binding);
    }
    if (msg == WM_ENTERMENULOOP || msg == WM_ENTERSIZEMOVE)
    {
        if (!binding->focus_depth++) request_focus(binding,WEX_WAYLAND_FOCUS_FRAME);
    }
    else if (msg == WM_EXITMENULOOP || msg == WM_EXITSIZEMOVE)
    {
        if (binding->focus_depth && !--binding->focus_depth)
#ifdef WEX_WAYLAND_RESIZE_PROTOCOL_PROP
        {
            if (binding->resize_pending) binding->restore_pending=TRUE;
            else request_focus(binding,WEX_WAYLAND_FOCUS_RESTORE_NATIVE);
        }
#else
            request_focus(binding,WEX_WAYLAND_FOCUS_RESTORE_NATIVE);
#endif
    }
    else if (msg == WM_NCDESTROY) detach(binding,TRUE);
    result=DefSubclassProc(hwnd,msg,wp,lp);
    /* The window procedure may refresh its outer shape during this message. */
    if (msg == WM_WINDOWPOSCHANGED && !binding->removed) update_mask(binding);
    unref(binding);
    return result;
}

DWORD wayland_host_bind(UINT64 window, HWND host)
{
    struct wm_binding_params params={0};
    struct host_binding *binding;
    DWORD result, pid;
    if (GetWindowThreadProcessId(host,&pid) != GetCurrentThreadId() || pid != GetCurrentProcessId() ||
        (bindings && binding_thread != GetCurrentThreadId())) return ERROR_INVALID_THREAD_ID;
    if (!mask_message && !(mask_message=RegisterWindowMessageW(L"__wine_wexhost_refresh_frame_mask")))
        return ERROR_NOT_ENOUGH_MEMORY;
#ifdef WEX_WAYLAND_RESIZE_PROTOCOL_PROP
    if (!resize_message && !(resize_message=RegisterWindowMessageW(WEX_WAYLAND_RESIZE_EVENT_MESSAGE)))
        return ERROR_NOT_ENOUGH_MEMORY;
#endif
    if ((binding=find_binding(window)))
    {
        if (binding->hwnd != host) return ERROR_BUSY;
        return (ULONG_PTR)GetPropW(host,WEX_WAYLAND_GROUP_PROP) == WEX_WAYLAND_GROUP_ACTIVE ?
               ERROR_SUCCESS : ERROR_IO_PENDING;
    }
    for (binding=bindings;binding;binding=binding->next)
        if (binding->hwnd == host) return ERROR_BUSY;
    if (GetPropW(host,WEX_WAYLAND_NATIVE_PROP) || GetPropW(host,WEX_WAYLAND_CONTROLLER_PROP) ||
        GetPropW(host,WEX_WAYLAND_HOST_PROP) || GetPropW(host,WEX_WAYLAND_FRAME_OUTER_PROP) ||
        GetPropW(host,WEX_WAYLAND_FRAME_MASK_UPDATING_PROP)) return ERROR_ALREADY_EXISTS;
    params.window=window;
    WEXHOST_CALL(wm_binding,&params);
    if (params.err) return params.err;
    if (!(binding=calloc(1,sizeof(*binding)))) return ERROR_NOT_ENOUGH_MEMORY;
    binding->refs=2; /* list plus this call */
    binding->window=window; binding->hwnd=host;
    binding->native=GlobalAddAtomA(params.token);
    binding->controller=GlobalAddAtomA(params.controller);
    binding->next=bindings; bindings=binding; binding_thread=GetCurrentThreadId();
    result=ERROR_NOT_ENOUGH_MEMORY;
    if (!binding->native || !binding->controller ||
        !SetWindowSubclass(host,host_subclass,subclass_id,(DWORD_PTR)binding)) goto failed;
    if (!SetPropW(host,WEX_WAYLAND_NATIVE_PROP,(HANDLE)(ULONG_PTR)binding->native) ||
        !SetPropW(host,WEX_WAYLAND_CONTROLLER_PROP,(HANDLE)(ULONG_PTR)binding->controller) ||
        !SetPropW(host,WEX_WAYLAND_HOST_PROP,(HANDLE)1) || !update_mask(binding)) goto failed;
    SetWindowPos(host,NULL,0,0,0,0,
                 SWP_NOMOVE|SWP_NOSIZE|SWP_NOZORDER|SWP_NOACTIVATE|SWP_FRAMECHANGED);
    result=binding->removed ? ERROR_INVALID_WINDOW_HANDLE : ERROR_IO_PENDING;
    unref(binding);
    return result;
failed:
    detach(binding,FALSE);
    unref(binding);
    return result;
}
DWORD wayland_host_configure(UINT64 window)
{
    struct host_binding *binding;
    DWORD result;
    if (bindings && binding_thread != GetCurrentThreadId()) return ERROR_INVALID_THREAD_ID;
    if (!(binding=find_binding(window))) return ERROR_INVALID_WINDOW_HANDLE;
    binding->refs++;
    result=update_mask(binding) ? ERROR_SUCCESS : ERROR_INVALID_WINDOW_HANDLE;
    unref(binding);
    return result;
}
DWORD wayland_host_release(UINT64 window)
{
    struct host_binding *binding;
    if (bindings && binding_thread != GetCurrentThreadId()) return ERROR_INVALID_THREAD_ID;
    if (!(binding=find_binding(window))) return ERROR_SUCCESS;
    binding->refs++; detach(binding,FALSE); unref(binding);
    return ERROR_SUCCESS;
}
void wayland_host_stop(void)
{
    if (bindings && binding_thread != GetCurrentThreadId()) return;
    while (bindings) wayland_host_release(bindings->window);
}

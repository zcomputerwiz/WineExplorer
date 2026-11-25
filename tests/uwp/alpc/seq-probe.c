/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright 2026 WineExplorer contributors
 *
 * Black-box probe (Windows 11 and Wine): CoreMessagingXP's startup sequence for a window thread, step by step, to find which step makes a
 * wait packet completion arrive as a posted message 0x60 (wparam 1, lparam 0) that ends MsgWaitForMultipleObjectsEx and is delivered by
 * PeekMessage (as measured in Notepad), instead of a (3,0) call inside the wait (as measured with simple probes).
 * Steps: Init2, SetTimer(hwnd, 1, 0x7fffffff), SendMessageCallbackW(0x60, 5, 5), a second packet on the port associated in the (5,5)
 * handler together with a Drain2(hwnd, NULL, NULL, NULL) call, and the first packet associated before or after the callback.
 */
#include <windows.h>
#include <winternl.h>
#include <stdio.h>

typedef ULONG_PTR (WINAPI *init2_t)(HWND, ULONG *, ULONG_PTR, ULONG_PTR);
typedef ULONG (WINAPI *drain2_t)(HWND, void *, void *, void *);
typedef INT (WINAPI *sched_t)(HWND);
typedef LONG (WINAPI *create_t)(HANDLE *, ACCESS_MASK, OBJECT_ATTRIBUTES *);
typedef LONG (WINAPI *assoc_t)(HANDLE, HANDLE, HANDLE, PVOID, PVOID, LONG, ULONG_PTR, BOOLEAN *);

static init2_t pInit2;
static drain2_t pDrain2;
static sched_t pSched;
static create_t pCreate;
static assoc_t pAssoc;

static int count;
static ULONG_PTR wps[8], lps[8];
static const char *where[8];
static const char *cur = "?";
static HANDLE g_port, g_ev2;
static int handler_drain, handler_assoc;
static OBJECT_ATTRIBUTES oa;

static LRESULT CALLBACK wndproc(HWND h, UINT m, WPARAM w, LPARAM l)
{
    if (m == 0x60 && count < 8) { wps[count] = w; lps[count] = l; where[count] = cur; count++; }
    if (m == 0x60 && w == 5 && l == 5)
    {
        if (handler_assoc) { HANDLE p2; pCreate(&p2, 1, &oa); pAssoc(p2, g_port, g_ev2, (PVOID)0x1c9a6160790ull, (PVOID)0, 0, 0, NULL); }
        { static char b1[256], b2[256], cx[256];
          if (handler_drain == 1) pDrain2(h, NULL, NULL, NULL);
          else if (handler_drain == 2) pDrain2(h, NULL, b1, b2);
          else if (handler_drain == 3) pDrain2(h, cx, b1, b2); }
    }
    return DefWindowProcW(h, m, w, l);
}

static HANDLE trigger;
static DWORD WINAPI signaler(void *arg) { Sleep(300); SetEvent(trigger); return 0; }

struct runargs { const char *title; int timer, callback, assoc_before_cb, hdrain, hassoc, peek_first; };

static DWORD WINAPI run_thread(void *arg)
{
    struct runargs *a = arg;
    void run1(const char *title, int timer, int callback, int assoc_before_cb, int hdrain, int hassoc, int peek_first);
    run1(a->title, a->timer, a->callback, a->assoc_before_cb, a->hdrain, a->hassoc, a->peek_first);
    return 0;
}

static void run(const char *title, int timer, int callback, int assoc_before_cb, int hdrain, int hassoc, int peek_first)
{
    struct runargs a = {title, timer, callback, assoc_before_cb, hdrain, hassoc, peek_first};
    HANDLE t = CreateThread(NULL, 0, run_thread, &a, 0, NULL);
    WaitForSingleObject(t, 20000);
    CloseHandle(t);
}

void run1(const char *title, int timer, int callback, int assoc_before_cb, int hdrain, int hassoc, int peek_first)
{
    WNDCLASSW wc = {0};
    HWND hwnd;
    ULONG out;
    HANDLE packet, ev, zev = CreateEventW(NULL, FALSE, FALSE, NULL), handles[1];
    MSG msg;
    DWORD st;
    int i;
    BOOL r;

    handler_drain = hdrain; handler_assoc = hassoc;
    wc.lpfnWndProc = wndproc; wc.hInstance = GetModuleHandleA(NULL); wc.lpszClassName = L"SeqProbe";
    RegisterClassW(&wc);
    ev = CreateEventW(NULL, FALSE, FALSE, NULL); g_ev2 = CreateEventW(NULL, FALSE, FALSE, NULL);
    trigger = ev;
    if (assoc_before_cb == 2)   /* Notepad: the packet is created and associated with a private port before the window exists */
    {
        typedef LONG (WINAPI *cio_t)(HANDLE *, ACCESS_MASK, OBJECT_ATTRIBUTES *, ULONG);
        cio_t pCIO = (void *)GetProcAddress(GetModuleHandleA("ntdll.dll"), "NtCreateIoCompletion");
        HANDLE pp;
        pCIO(&pp, 0x1f0003, NULL, 0);
        pCreate(&packet, 1, &oa);
        pAssoc(packet, pp, ev, (PVOID)0x40000002, (PVOID)0, 0, 0, NULL);
    }
    hwnd = CreateWindowExW(0, L"SeqProbe", NULL, 0, 0, 0, 0, 0, HWND_MESSAGE, NULL, wc.hInstance, NULL);
    g_port = (HANDLE)pInit2(hwnd, &out, 0, 0);
    if (timer) SetTimer(hwnd, 1, 0x7fffffff, NULL);
    if (assoc_before_cb != 2) pCreate(&packet, 1, &oa);
    if (assoc_before_cb == 1) pAssoc(packet, g_port, ev, (PVOID)0x1c9a6160520ull, (PVOID)0, 0, 0, NULL);
    if (callback) SendMessageCallbackW(hwnd, 0x60, 5, 5, NULL, 1);
    if (assoc_before_cb == 2)
    {
        typedef LONG (WINAPI *cancel_t)(HANDLE, BOOLEAN);
        cancel_t pCancel = (void *)GetProcAddress(GetModuleHandleA("ntdll.dll"), "NtCancelWaitCompletionPacket");
        LONG cs = pCancel(packet, FALSE);
        if (cs) printf("[cancel status %#lx] ", cs);
        pAssoc(packet, g_port, ev, (PVOID)0x1c9a6160520ull, (PVOID)0, 0, 0, NULL);
    }
    if (assoc_before_cb == 0) pAssoc(packet, g_port, ev, (PVOID)0x1c9a6160520ull, (PVOID)0, 0, 0, NULL);
    handles[0] = zev;
    count = 0; cur = "Peek(setup)";
    if (peek_first) PeekMessageW(&msg, NULL, 0, 0, PM_REMOVE);
    CreateThread(NULL, 0, signaler, NULL, 0, NULL);
    SetTimer(NULL, 0, 2000, NULL);
    cur = "MsgWait";
    st = MsgWaitForMultipleObjectsEx(1, handles, INFINITE, 0x1cff, 0);
    printf("%-60s wait %#lx", title, st);
    for (i = 0; i < count; i++) printf(" | %s: (%#Ix,%#Ix)", where[i], wps[i], lps[i]);
    cur = "Peek";
    r = PeekMessageW(&msg, NULL, 0, 0, PM_REMOVE);
    printf(" || Peek -> %d", r);
    if (r) printf(" msg %#x wp %#Ix", msg.message, msg.wParam);
    for (i = 0; i < count; i++) if (where[i][0] == 'P' && where[i][1] == 'e' && where[i][4] == 0) printf(" | %s: (%#Ix,%#Ix)", where[i], wps[i], lps[i]);
    printf("\n");
    DestroyWindow(hwnd);
}

int main(void)
{
    HMODULE ntdll = GetModuleHandleA("ntdll.dll"), win32u = LoadLibraryA("win32u.dll"), user32 = LoadLibraryA("user32.dll");
    setvbuf(stdout, NULL, _IONBF, 0);
    pInit2 = (void *)GetProcAddress(win32u, "NtUserInitThreadCoreMessagingIocp2");
    pDrain2 = (void *)GetProcAddress(win32u, "NtUserDrainThreadCoreMessagingCompletions2");
    pSched = (void *)GetProcAddress(user32, (LPCSTR)2582);
    pCreate = (void *)GetProcAddress(ntdll, "NtCreateWaitCompletionPacket");
    pAssoc = (void *)GetProcAddress(ntdll, "NtAssociateWaitCompletionPacket");
    InitializeObjectAttributes(&oa, NULL, 0, NULL, NULL);
    run("A: Init2, packet",                                              0, 0, 1, 0, 0, 0);
    run("B: + timer 0x7fffffff",                                         1, 0, 1, 0, 0, 0);
    run("C: + callback(5,5) [handler does nothing], packet after",       1, 1, 0, 0, 0, 1);
    run("D: C, handler calls Drain2(NULL x3)",                           1, 1, 0, 1, 0, 1);
    run("E: C, handler associates a second packet",                     1, 1, 0, 0, 1, 1);
    run("F: C, handler: second packet + Drain2(NULL x3)  (Notepad)",    1, 1, 0, 1, 1, 1);
    run("G: F with the first packet associated before the callback",    1, 1, 1, 1, 1, 1);
    run("H: F but the handler calls Drain2(NULL, buf, buf)",            1, 1, 0, 2, 1, 1);
    run("I: F but the handler calls Drain2(ctx, buf, buf)",             1, 1, 0, 3, 1, 1);
    run("J: Drain2(NULL x3) in handler, no second packet, packet after cb", 1, 1, 0, 1, 0, 1);
    run("K: Notepad order: packet+private port first, cancel, re-associate", 1, 1, 2, 1, 1, 1);
    run("L: K without the handler's Drain2 and second packet",           1, 1, 2, 0, 0, 1);
    return 0;
}

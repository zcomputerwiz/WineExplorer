/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright 2026 WineExplorer contributors
 *
 * Black-box probe (Windows 11 and Wine): a thread set up like a CoreMessagingXP worker (message-only window registered with
 * NtUserInitThreadCoreMessagingIocp2, timer, deferred callback (5,5), wait completion packet with a zero APC context on an
 * auto-reset event) whose window procedure answers the (1,0) wake the way CoreMessagingXP does: reset the event, then call
 * NtUserDrainThreadCoreMessagingCompletions2 with output buffers. Records where each message 0x60 arrives (in the wait, in
 * PeekMessage, in DispatchMessage, nested in the drain call), whether it was sent, and whether a second signal of the event
 * wakes the thread again with and without re-associating the packet.
 */
#include <windows.h>
#include <winternl.h>
#include <stdio.h>

typedef ULONG_PTR (WINAPI *init2_t)(HWND, ULONG *, ULONG_PTR, ULONG_PTR);
typedef ULONG (WINAPI *drain2_t)(HWND, void *, void *, void *);
typedef LONG (WINAPI *create_t)(HANDLE *, ACCESS_MASK, OBJECT_ATTRIBUTES *);
typedef LONG (WINAPI *assoc_t)(HANDLE, HANDLE, HANDLE, PVOID, PVOID, LONG, ULONG_PTR, BOOLEAN *);

static init2_t pInit2;
static ULONG_PTR (WINAPI *p2669)(HWND, ULONG *);   /* user32 ordinal 2669, CoreMessagingXP's way in */
static int use_user32;

static ULONG_PTR init_window(HWND hwnd, ULONG *out)
{
    return use_user32 ? p2669(hwnd, out) : pInit2(hwnd, out, 0, 0);
}
static drain2_t pDrain2;
static create_t pCreate;
static assoc_t pAssoc;

#define MAXLOG 96
struct entry { DWORD ms; const char *where; WPARAM wp; LPARAM lp; DWORD ism; };
static struct entry log_[MAXLOG];
static int nlog;
static const char *cur = "?";
static DWORD t0;
static int mode;              /* 0: no re-association, 1: re-associate after a (3,x)/(8,x) seen in the drain, 2: always after the drain */
static HANDLE g_port, g_packet, g_event;
static HWND g_hwnd;
static ULONG drain_ret[8];
static int ndrain;
static int seen_in_drain;
static ULONG_PTR sentinel_changed;

static void add(const char *where, WPARAM wp, LPARAM lp, DWORD ism)
{
    if (nlog >= MAXLOG) return;
    log_[nlog].ms = GetTickCount() - t0; log_[nlog].where = where; log_[nlog].wp = wp; log_[nlog].lp = lp; log_[nlog].ism = ism;
    nlog++;
}

static LRESULT CALLBACK wndproc(HWND h, UINT m, WPARAM w, LPARAM l)
{
    if (m != 0x60) return DefWindowProcW(h, m, w, l);
    add(cur, w, l, InSendMessageEx(NULL));
    if (!strcmp(cur, "drain")) seen_in_drain = 1;
    if (mode >= 0 && w == 1 && strcmp(cur, "drain"))
    {
        ULONG_PTR b[3][4];
        const char *saved = cur;
        int i, j;

        for (i = 0; i < 3; i++) for (j = 0; j < 4; j++) b[i][j] = 0xa5a5a5a5a5a5a5a5ull;
        ResetEvent(g_event);
        seen_in_drain = 0;
        cur = "drain";
        if (ndrain < 8) drain_ret[ndrain++] = pDrain2(h, b[0], b[1], b[2]);
        cur = saved;
        for (i = 0; i < 3; i++) for (j = 0; j < 4; j++) if (b[i][j] != 0xa5a5a5a5a5a5a5a5ull) sentinel_changed |= 1u << (i * 4 + j);
        if (mode == 2 || (mode == 1 && seen_in_drain))
        {
            LONG st = pAssoc(g_packet, g_port, g_event, (PVOID)0x40000002, (PVOID)0, 0, 0, NULL);
            add(st ? "reassoc-failed" : "reassoc", 0, st, 0);
        }
    }
    return 0;
}

static DWORD WINAPI signaler(void *arg)
{
    Sleep(150); SetEvent(g_event);
    Sleep(300); SetEvent(g_event);
    return 0;
}

static DWORD WINAPI scenario(void *arg)
{
    WNDCLASSW wc = {0};
    OBJECT_ATTRIBUTES oa;
    HANDLE zev = CreateEventW(NULL, FALSE, FALSE, NULL);
    ULONG out = 0;
    MSG msg;
    DWORD end, st;
    int i, waits = 0;

    mode = (int)(INT_PTR)arg;
    nlog = 0; ndrain = 0; sentinel_changed = 0;
    InitializeObjectAttributes(&oa, NULL, 0, NULL, NULL);
    wc.lpfnWndProc = wndproc; wc.hInstance = GetModuleHandleA(NULL); wc.lpszClassName = L"Drain3Probe";
    RegisterClassW(&wc);
    g_hwnd = CreateWindowExW(0, L"Drain3Probe", NULL, 0, 0, 0, 0, 0, HWND_MESSAGE, NULL, wc.hInstance, NULL);
    g_port = (HANDLE)init_window(g_hwnd, &out);
    SetTimer(g_hwnd, 1, 0x7fffffff, NULL);
    g_event = CreateEventW(NULL, FALSE, FALSE, NULL);
    pCreate(&g_packet, 1, &oa);
    pAssoc(g_packet, g_port, g_event, (PVOID)0x40000002, (PVOID)0, 0, 0, NULL);
    SendMessageCallbackW(g_hwnd, 0x60, 5, 5, NULL, 1);
    t0 = GetTickCount();
    CloseHandle(CreateThread(NULL, 0, signaler, NULL, 0, NULL));
    end = t0 + 900;
    while (GetTickCount() < end && waits < 40)
    {
        cur = "wait";
        {
            DWORD now = GetTickCount();
            st = MsgWaitForMultipleObjectsEx(1, &zev, now < end ? end - now : 0, 0x1cff, 0);
        }
        waits++;
        add(st == 1 ? "wait-ret-input" : st == WAIT_TIMEOUT ? "wait-timeout" : "wait-ret-other", st, GetQueueStatus(QS_ALLINPUT), 0);
        for (;;)
        {
            BOOL r;
            cur = "peek";
            r = PeekMessageW(&msg, NULL, 0, 0, PM_REMOVE);
            if (!r) break;
            if (msg.message != 0x60) add("peek-other-msg", msg.message, msg.wParam, 0);
            cur = "dispatch";
            DispatchMessageW(&msg);
        }
    }
    printf("mode %d (%s): port %p, %d waits, Drain2 returns:", mode,
           mode == 0 ? "no re-association" : mode == 1 ? "re-associate if 0x60 came in the drain" : "always re-associate",
           g_port, waits);
    for (i = 0; i < ndrain; i++) printf(" %lu", drain_ret[i]);
    printf("; drain buffers written: %#Ix\n", sentinel_changed);
    for (i = 0; i < nlog; i++)
        printf("  [%4lu ms] %-15s wp %#Ix lp %#Ix%s\n", log_[i].ms, log_[i].where, log_[i].wp, log_[i].lp,
               log_[i].ism & ISMEX_SEND ? " (sent)" : log_[i].ism ? " (other ism)" : "");
    DestroyWindow(g_hwnd);
    CloseHandle(g_packet); CloseHandle(g_event); CloseHandle(zev);
    return 0;
}

/* the sequence of the Wine test (dlls/user32/tests/msg.c, test_core_messaging_completions): two plain registered windows,
 * NtSetIoCompletion(key 0x7777, apc 0, info 0x6666), a 300 ms message wait, PeekMessage(0x60..0x60) loop, one drain */
static LONG (WINAPI *pSetIo)(HANDLE, ULONG_PTR, ULONG_PTR, LONG, ULONG_PTR);
static LONG (WINAPI *pRemoveIo)(HANDLE, ULONG_PTR *, ULONG_PTR *, void *, LARGE_INTEGER *);

static DWORD WINAPI unit(void *arg)
{
    WNDCLASSW wc = {0};
    HWND h1, h2;
    HANDLE port;
    ULONG out;
    MSG msg;
    DWORD st;
    ULONG_PTR key, apc, io[2];
    LARGE_INTEGER zero = {{0}};
    LONG rs;
    int i;

    wc.lpfnWndProc = wndproc; wc.hInstance = GetModuleHandleA(NULL); wc.lpszClassName = L"Drain3Unit";
    RegisterClassW(&wc);
    h1 = CreateWindowExW(0, L"Drain3Unit", NULL, 0, 0, 0, 0, 0, HWND_MESSAGE, NULL, wc.hInstance, NULL);
    h2 = CreateWindowExW(0, L"Drain3Unit", NULL, 0, 0, 0, 0, 0, HWND_MESSAGE, NULL, wc.hInstance, NULL);
    port = (HANDLE)init_window(h1, &out);
    init_window(h2, &out);
    nlog = 0; t0 = GetTickCount(); mode = -1;
    pSetIo(port, 0x7777, 0, 0, 0x6666);
    cur = "wait";
    st = MsgWaitForMultipleObjectsEx(0, NULL, 300, QS_ALLINPUT, 0);
    add("wait-returned", st, 0, 0);
    cur = "peek";
    while (PeekMessageW(&msg, h1, 0x60, 0x60, PM_REMOVE)) { add("peeked", msg.wParam, msg.lParam, 0); cur = "dispatch"; DispatchMessageW(&msg); cur = "peek"; }
    cur = "unit-drain";
    add("drain-returned", pDrain2(h1, NULL, NULL, NULL), 0, 0);
    rs = pRemoveIo(port, &key, &apc, io, &zero);
    add(rs ? "port-empty" : "port-left", key, apc, rs);
    printf("unit (msg.c sequence):\n");
    for (i = 0; i < nlog; i++)
        printf("  [%4lu ms] %-15s wp %#Ix lp %#Ix%s\n", log_[i].ms, log_[i].where, log_[i].wp, log_[i].lp,
               log_[i].ism & ISMEX_SEND ? " (sent)" : "");
    DestroyWindow(h1); DestroyWindow(h2);
    return 0;
}

int main(void)
{
    HMODULE ntdll = GetModuleHandleA("ntdll.dll"), win32u = LoadLibraryA("win32u.dll");
    int m;

    setvbuf(stdout, NULL, _IONBF, 0);
    LoadLibraryA("user32.dll");
    pInit2 = (void *)GetProcAddress(win32u, "NtUserInitThreadCoreMessagingIocp2");
    pDrain2 = (void *)GetProcAddress(win32u, "NtUserDrainThreadCoreMessagingCompletions2");
    pCreate = (void *)GetProcAddress(ntdll, "NtCreateWaitCompletionPacket");
    pAssoc = (void *)GetProcAddress(ntdll, "NtAssociateWaitCompletionPacket");
    p2669 = (void *)GetProcAddress(GetModuleHandleA("user32.dll"), (LPCSTR)2669);
    use_user32 = getenv("DRAIN3_USER32") != NULL && p2669;
    printf("registration through %s\n", use_user32 ? "user32 ordinal 2669" : "win32u NtUserInitThreadCoreMessagingIocp2(hwnd, out, 0, 0)");
    pSetIo = (void *)GetProcAddress(ntdll, "NtSetIoCompletion");
    pRemoveIo = (void *)GetProcAddress(ntdll, "NtRemoveIoCompletion");
    if (!pInit2 || !pDrain2 || !pCreate || !pAssoc || !pSetIo || !pRemoveIo) { printf("missing exports\n"); return 1; }
    {
        HANDLE t = CreateThread(NULL, 0, unit, NULL, 0, NULL);
        WaitForSingleObject(t, INFINITE);
        CloseHandle(t);
    }
    /* each scenario on its own thread: Init2 binds the window to the thread's queue once */
    for (m = 0; m < 3; m++)
    {
        HANDLE t = CreateThread(NULL, 0, scenario, (void *)(INT_PTR)m, 0, NULL);
        WaitForSingleObject(t, INFINITE);
        CloseHandle(t);
    }
    return 0;
}

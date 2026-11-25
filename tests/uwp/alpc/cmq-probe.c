/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright 2026 WineExplorer contributors
 *
 * Black-box probe of win32u's NtUserInitThreadCoreMessagingIocp2 / NtUserDrainThreadCoreMessagingCompletions2 on Windows 11.
 * The signatures are inferred from how the Windows App SDK's CoreMessagingXP calls them (window, pointer to a dword; the
 * result is a handle). This program only observes: the handle's object type, how a completion on that handle (made
 * with a wait completion packet on an event) affects a wait for window messages, the queue status, and what a drain call
 * returns.
 * Build: x86_64-w64-mingw32-gcc -O2 -o cmq-probe.exe cmq-probe.c
 */
#include <windows.h>
#include <winternl.h>
#include <stdio.h>
#include <stdlib.h>

typedef LONG (NTAPI *NtCreateWaitCompletionPacket_t)(HANDLE *, ACCESS_MASK, OBJECT_ATTRIBUTES *);
typedef LONG (NTAPI *NtAssociateWaitCompletionPacket_t)(HANDLE, HANDLE, HANDLE, PVOID, PVOID, LONG, ULONG_PTR, BOOLEAN *);
typedef LONG (NTAPI *NtRemoveIoCompletion_t)(HANDLE, PVOID *, PVOID *, IO_STATUS_BLOCK *, LARGE_INTEGER *);
typedef LONG (NTAPI *NtQueryObject_t)(HANDLE, int, void *, ULONG, ULONG *);
typedef LONG (NTAPI *NtSetIoCompletion_t)(HANDLE, PVOID, PVOID, LONG, ULONG_PTR);
typedef ULONG_PTR (NTAPI *init2_t)(HWND, ULONG *, ULONG_PTR, ULONG_PTR);
typedef ULONG_PTR (NTAPI *drain2_t)(HWND, void *, void *, void *);

static NtCreateWaitCompletionPacket_t pNtCreateWaitCompletionPacket;
static NtAssociateWaitCompletionPacket_t pNtAssociateWaitCompletionPacket;
static NtRemoveIoCompletion_t pNtRemoveIoCompletion;
static NtQueryObject_t pNtQueryObject;
static NtSetIoCompletion_t pNtSetIoCompletion;
static init2_t pInit2;
static drain2_t pDrain2;

static HWND hwnd;
static HANDLE port, event;
static DWORD t0;
static int in_drain;

static DWORD main_tid;
static int depth;
static int drain_in_proc;

static LRESULT CALLBACK wndproc(HWND h, UINT msg, WPARAM wp, LPARAM lp)
{
    if (msg == 0x401 && GetCurrentThreadId() != main_tid)
    {
        printf("  [%4lu ms] other thread: window procedure got 0x401, sleeping 500 ms\n", GetTickCount() - t0);
        Sleep(500);
        return 0;
    }
    if (msg == 0x60 && drain_in_proc)
    {
        ULONG_PTR x[3][4];
        ULONG_PTR rv;
        int i;
        memset(x, 0xcc, sizeof(x));
        printf("  [%4lu ms] wndproc 0x60 wp %#Ix lp %#Ix: calling Drain2(hwnd, x0, x1, x2)\n", GetTickCount() - t0, wp, lp);
        rv = pDrain2(h, x[0], x[1], x[2]);
        printf("     -> return %#Ix\n", rv);
        for (i = 0; i < 3; i++)
            printf("     x%d: %#Ix %#Ix %#Ix %#Ix\n", i, x[i][0], x[i][1], x[i][2], x[i][3]);
        memset(x, 0xcc, sizeof(x));
        rv = pDrain2(h, NULL, x[1], x[2]);
        printf("     again Drain2(hwnd, NULL, x1, x2) -> return %#Ix; x1: %#Ix %#Ix, x2: %#Ix %#Ix\n", rv, x[1][0], x[1][1], x[2][0], x[2][1]);
        return 0;
    }
    if (msg == 0x60 && lp == 0x31 && depth == 0)
    {
        DWORD r2;
        depth++;
        printf("  [%4lu ms] (depth %d) handling 0x60/0x31: queue another completion (0x32), then nested MsgWait 400 ms\n", GetTickCount() - t0, depth);
        pNtSetIoCompletion(port, (PVOID)0xd2, (PVOID)0x32, 0, 0);
        r2 = MsgWaitForMultipleObjectsEx(0, NULL, 400, QS_ALLINPUT, 0);
        printf("  [%4lu ms] (depth %d) nested MsgWait returned %#lx\n", GetTickCount() - t0, depth, r2);
        {
            MSG m;
            BOOL g;
            SetTimer(h, 9, 300, NULL);
            g = GetMessageW(&m, NULL, 0, 0);
            printf("  [%4lu ms] (depth %d) nested GetMessage returned %d msg %#x\n", GetTickCount() - t0, depth, g, m.message);
            KillTimer(h, 9);
        }
        depth--;
        return 0;
    }
    if (msg != WM_GETMINMAXINFO && msg != WM_NCCREATE && msg != WM_NCCALCSIZE && msg != WM_CREATE)
        printf("  [%4lu ms] wndproc hwnd %p msg %#x wp %#Ix lp %#Ix (InSendMessage %d, tid %lu)\n",
                GetTickCount() - t0, h, msg, wp, lp, InSendMessage(), GetCurrentThreadId());
    return DefWindowProcW(h, msg, wp, lp);
}

static DWORD CALLBACK setter(void *arg)
{
    Sleep((DWORD)(ULONG_PTR)arg);
    printf("  [%4lu ms] (other thread) SetEvent\n", GetTickCount() - t0);
    SetEvent(event);
    return 0;
}

static DWORD CALLBACK setter2(void *arg)
{
    Sleep(150);
    printf("  [%4lu ms] (other thread) queue completion 0x41\n", GetTickCount() - t0);
    pNtSetIoCompletion(port, (PVOID)0xe1, (PVOID)0x41, 0, 0);
    return 0;
}

static void status(const char *when)
{
    DWORD a = GetQueueStatus(QS_ALLINPUT | QS_ALLPOSTMESSAGE | 0x8000 /* QS_SMRESULT */);
    printf("  [%4lu ms] %s: GetQueueStatus new %#lx pending %#lx\n", GetTickCount() - t0, when, a >> 16, a & 0xffff);
}

static void arm(void)
{
    HANDLE packet;
    OBJECT_ATTRIBUTES oa = {sizeof(oa)};
    BOOLEAN already = 0;
    LONG st;

    st = pNtCreateWaitCompletionPacket(&packet, 1, &oa);
    st = pNtAssociateWaitCompletionPacket(packet, port, event, (PVOID)0x1234, (PVOID)0x5678, 0, 0, &already);
    printf("  associate packet: status %#lx already %d\n", st, already);
}

static void drain_pump(const char *label)
{
    MSG m;
    int n = 0;

    while (PeekMessageW(&m, NULL, 0, 0, PM_REMOVE)) { n++; DispatchMessageW(&m); }
    printf("  [%4lu ms] %s: PeekMessage(PM_REMOVE) returned %d messages\n", GetTickCount() - t0, label, n);
}

static void try_remove(const char *label)
{
    PVOID key = (PVOID)-1, apc = (PVOID)-1;
    IO_STATUS_BLOCK iosb = {0};
    LARGE_INTEGER to = {0};
    LONG st = pNtRemoveIoCompletion(port, &key, &apc, &iosb, &to);

    printf("  %s: NtRemoveIoCompletion status %#lx key %p apc %p\n", label, st, key, apc);
}

int main(void)
{
    HMODULE ntdll = GetModuleHandleA("ntdll.dll"), win32u = LoadLibraryA("win32u.dll");
    WNDCLASSW wc = {0};
    ULONG out = 0xdeadbeef;
    ULONG_PTR ret;
    BYTE buf[2][512];
    unsigned char otype[512];
    ULONG len;
    DWORD r;
    int i;

    LoadLibraryA("user32.dll");
    main_tid = GetCurrentThreadId();
    pNtCreateWaitCompletionPacket = (void *)GetProcAddress(ntdll, "NtCreateWaitCompletionPacket");
    pNtAssociateWaitCompletionPacket = (void *)GetProcAddress(ntdll, "NtAssociateWaitCompletionPacket");
    pNtRemoveIoCompletion = (void *)GetProcAddress(ntdll, "NtRemoveIoCompletion");
    pNtQueryObject = (void *)GetProcAddress(ntdll, "NtQueryObject");
    pNtSetIoCompletion = (void *)GetProcAddress(ntdll, "NtSetIoCompletion");
    pInit2 = (void *)GetProcAddress(win32u, "NtUserInitThreadCoreMessagingIocp2");
    pDrain2 = (void *)GetProcAddress(win32u, "NtUserDrainThreadCoreMessagingCompletions2");
    if (!pInit2 || !pDrain2) { printf("missing exports\n"); return 1; }

    wc.lpfnWndProc = wndproc; wc.hInstance = GetModuleHandleA(NULL); wc.lpszClassName = L"CmqProbe";
    RegisterClassW(&wc);
    hwnd = CreateWindowExW(0, L"CmqProbe", NULL, 0, 0, 0, 0, 0, HWND_MESSAGE, NULL, wc.hInstance, NULL);
    printf("window %p\n", hwnd);
    t0 = GetTickCount();
    event = CreateEventW(NULL, FALSE, FALSE, NULL);

    status("before init");
    ret = pInit2(hwnd, &out, 0, 0);
    port = (HANDLE)ret;
    printf("Init2: return %#Ix, out dword %#lx, last error %lu\n", ret, out, GetLastError());
    if (getenv("CMQ_SECOND_WINDOW"))
    {
        HWND w2 = CreateWindowExW(0, L"CmqProbe", NULL, 0, 0, 0, 0, 0, HWND_MESSAGE, NULL, wc.hInstance, NULL);
        ULONG out2 = 0xdeadbeef;
        ULONG_PTR r2 = pInit2(w2, &out2, 0, 0);
        printf("Init2 with a second window %p: return %#Ix, out dword %#lx (same handle: %d)\n", w2, r2, out2, (HANDLE)r2 == port);
        out2 = 0xdeadbeef;
        r2 = pInit2(hwnd, &out2, 0, 0);
        printf("Init2 with the first window again: return %#Ix, out dword %#lx\n", r2, out2);
    }
    memset(otype, 0, sizeof(otype));
    if (!pNtQueryObject(port, 2, otype, sizeof(otype), &len))
    {
        UNICODE_STRING *n = (UNICODE_STRING *)otype;
        printf("object type: %.*ls\n", n->Length / 2, n->Buffer);
    }
    else printf("NtQueryObject failed (handle not valid in this process?)\n");
    {
        ULONG flags = 0;
        BOOL ok = GetHandleInformation(port, &flags);
        printf("GetHandleInformation: %d flags %#lx\n", ok, flags);
    }

    printf("\n== test 1: event set 300 ms into MsgWaitForMultipleObjectsEx(0, NULL, 1500, QS_ALLINPUT)\n");
    arm();
    t0 = GetTickCount();
    CreateThread(NULL, 0, setter, (void *)300, 0, NULL);
    r = MsgWaitForMultipleObjectsEx(0, NULL, 1500, QS_ALLINPUT, 0);
    printf("  [%4lu ms] MsgWait returned %#lx\n", GetTickCount() - t0, r);
    status("after wake");
    drain_pump("pump");
    try_remove("remove after pump");

    printf("\n== test 2: same, but with the wait mask QS_POSTMESSAGE only\n");
    arm();
    t0 = GetTickCount();
    CreateThread(NULL, 0, setter, (void *)300, 0, NULL);
    r = MsgWaitForMultipleObjectsEx(0, NULL, 1500, QS_POSTMESSAGE, 0);
    printf("  [%4lu ms] MsgWait returned %#lx\n", GetTickCount() - t0, r);
    status("after wake");
    try_remove("remove");

    printf("\n== test 3: event set 300 ms into GetMessage (a 1500 ms timer stops it)\n");
    arm();
    SetTimer(hwnd, 7, 1500, NULL);
    t0 = GetTickCount();
    CreateThread(NULL, 0, setter, (void *)300, 0, NULL);
    {
        MSG m;
        BOOL g = GetMessageW(&m, NULL, 0, 0);
        printf("  [%4lu ms] GetMessage returned %d, msg %#x hwnd %p\n", GetTickCount() - t0, g, m.message, m.hwnd);
        if (g > 0) DispatchMessageW(&m);
    }
    KillTimer(hwnd, 7);
    status("after GetMessage");
    try_remove("remove");

    printf("\n== test 4: event already set when the packet is armed, then the queue is polled\n");
    SetEvent(event);
    arm();
    t0 = GetTickCount();
    status("right after arm");
    r = MsgWaitForMultipleObjectsEx(0, NULL, 500, QS_ALLINPUT, 0);
    printf("  [%4lu ms] MsgWait returned %#lx\n", GetTickCount() - t0, r);
    status("after");
    try_remove("remove");

    printf("\n== test 5: Drain2 with pointer arguments (zeroed buffers), return value and what it writes\n");
    for (i = 0; i < 2; i++) memset(buf[i], 0, sizeof(buf[i]));
    arm();
    SetEvent(event);
    Sleep(100);
    status("completion queued, before drain");
    ret = pDrain2(hwnd, NULL, buf[0], buf[1]);
    printf("  Drain2(hwnd, NULL, b0, b1): return %#Ix\n", ret);
    printf("  b0:"); for (i = 0; i < 32; i++) printf(" %02x", buf[0][i]); printf("\n");
    printf("  b1:"); for (i = 0; i < 32; i++) printf(" %02x", buf[1][i]); printf("\n");
    status("after drain");
    try_remove("remove after drain");

    printf("\n== test 7: three completions (apc 1, 2, 3) queued with the thread outside a wait, then one PeekMessage(PM_NOREMOVE)\n");
    pNtSetIoCompletion(port, (PVOID)0xa1, (PVOID)1, 0, 0);
    pNtSetIoCompletion(port, (PVOID)0xa2, (PVOID)2, 0, 0);
    pNtSetIoCompletion(port, (PVOID)0xa3, (PVOID)3, 0, 0);
    Sleep(50);
    status("queued, no wait");
    printf("  calling PeekMessage(PM_NOREMOVE)\n");
    { MSG m; BOOL p = PeekMessageW(&m, NULL, 0, 0, PM_NOREMOVE); printf("  PeekMessage returned %d\n", p); }
    try_remove("remove after peek");

    printf("\n== test 8: one completion queued with the thread outside a wait, then GetQueueStatus only, then Sleep, then SleepEx alertable\n");
    pNtSetIoCompletion(port, (PVOID)0xb1, (PVOID)0x11, 0, 0);
    status("after queueing");
    Sleep(100);
    printf("  after Sleep(100) (nothing should have run)\n");
    SleepEx(100, TRUE);
    printf("  after SleepEx\n");
    try_remove("remove");

    printf("\n== test 9: Drain2 variants with nothing pending, then with one completion pending\n");
    ret = pDrain2(hwnd, NULL, NULL, NULL);
    printf("  Drain2(hwnd, NULL, NULL, NULL) with nothing pending: return %#Ix\n", ret);
    ret = pDrain2(NULL, NULL, NULL, NULL);
    printf("  Drain2(NULL, ...) with nothing pending: return %#Ix\n", ret);
    pNtSetIoCompletion(port, (PVOID)0xc1, (PVOID)0x21, 0, 0);
    printf("  one completion queued; calling Drain2(hwnd, NULL, NULL, NULL)\n");
    ret = pDrain2(hwnd, NULL, NULL, NULL);
    printf("  return %#Ix\n", ret);
    try_remove("remove");

    printf("\n== test 10: completion with the apc context 0 and an IO status (key 0x77, status 0xc0000001, information 9)\n");
    pNtSetIoCompletion(port, (PVOID)0x77, (PVOID)0, (LONG)0xc0000001, 9);
    r = MsgWaitForMultipleObjectsEx(0, NULL, 300, QS_ALLINPUT, 0);
    printf("  MsgWait returned %#lx\n", r);

    printf("\n== test 20: which of key / apc context / status / information reaches the message (wait packet and NtSetIoCompletion)\n");
    {
        static const struct { ULONG_PTR key, apc; LONG status; ULONG_PTR info; } c[] = {
            {0x1111, 0, 0, 0}, {0, 0x2222, 0, 0}, {0x3333, 0x4444, 0, 0}, {0x5555, 0, 0, 0x6666}, {0x7777, 0, (LONG)0xc0000005, 0},
            {0x8888, 0x9999, 0x103, 0xaaaa}};
        unsigned int k;
        for (k = 0; k < sizeof(c) / sizeof(c[0]); k++)
        {
            printf("  NtSetIoCompletion(key %#Ix, apc %#Ix, status %#lx, info %#Ix):\n", c[k].key, c[k].apc, c[k].status, c[k].info);
            pNtSetIoCompletion(port, (PVOID)c[k].key, (PVOID)c[k].apc, c[k].status, c[k].info);
            MsgWaitForMultipleObjectsEx(0, NULL, 300, QS_ALLINPUT, 0);
        }
        for (k = 0; k < 3; k++)
        {
            HANDLE packet;
            OBJECT_ATTRIBUTES oa = {sizeof(oa)};
            BOOLEAN already = 0;
            printf("  wait packet: key %#Ix, apc %#Ix, status %#lx, info %#Ix (event set after arming):\n", c[k].key, c[k].apc, c[k].status, c[k].info);
            pNtCreateWaitCompletionPacket(&packet, 1, &oa);
            pNtAssociateWaitCompletionPacket(packet, port, event, (PVOID)c[k].key, (PVOID)c[k].apc, c[k].status, c[k].info, &already);
            SetEvent(event);
            MsgWaitForMultipleObjectsEx(0, NULL, 300, QS_ALLINPUT, 0);
        }
    }

    printf("\n== test 21: Drain2 with sentinel-filled buffers, a completion queued (key 0xc0de, apc 0)\n");
    {
        BYTE s[3][64];
        int k;
        memset(s, 0xcc, sizeof(s));
        pNtSetIoCompletion(port, (PVOID)0xc0de, (PVOID)0, 0, 0);
        ret = pDrain2(hwnd, s[0], s[1], s[2]);
        printf("  Drain2(hwnd, s0, s1, s2): return %#Ix\n", ret);
        for (k = 0; k < 3; k++)
        {
            int i, changed = 0;
            for (i = 0; i < 64; i++) if (s[k][i] != 0xcc) changed++;
            printf("  s%d: %d bytes changed:", k, changed);
            for (i = 0; i < 24; i++) printf(" %02x", s[k][i]);
            printf("\n");
        }
        memset(s, 0xcc, sizeof(s));
        pNtSetIoCompletion(port, (PVOID)0xc0df, (PVOID)0, 0, 0);
        ret = pDrain2(hwnd, NULL, s[1], s[2]);
        printf("  Drain2(hwnd, NULL, s1, s2): return %#Ix\n", ret);
        for (k = 1; k < 3; k++)
        {
            int i, changed = 0;
            for (i = 0; i < 64; i++) if (s[k][i] != 0xcc) changed++;
            printf("  s%d: %d bytes changed:", k, changed);
            for (i = 0; i < 24; i++) printf(" %02x", s[k][i]);
            printf("\n");
        }
    }

    printf("\n== test 30: the window procedure calls Drain2 itself on each delivery (sentinel-filled output buffers)\n");
    {
        static const struct { ULONG_PTR key, apc; } c[] = {{0x1234, 0}, {0x5678, 0x9abc}, {0xdddd, 0}};
        unsigned int k;
        drain_in_proc = 1;
        for (k = 0; k < sizeof(c) / sizeof(c[0]); k++)
        {
            printf("  completion key %#Ix apc %#Ix via NtSetIoCompletion, wait:\n", c[k].key, c[k].apc);
            pNtSetIoCompletion(port, (PVOID)c[k].key, (PVOID)c[k].apc, 0, 0);
            MsgWaitForMultipleObjectsEx(0, NULL, 300, QS_ALLINPUT, 0);
            try_remove("  port after");
        }
        {
            HANDLE packet;
            OBJECT_ATTRIBUTES oa = {sizeof(oa)};
            BOOLEAN already = 0;
            printf("  wait packet key 0xabcd0000 apc 0, event set, wait:\n");
            pNtCreateWaitCompletionPacket(&packet, 1, &oa);
            pNtAssociateWaitCompletionPacket(packet, port, event, (PVOID)0xabcd0000, (PVOID)0, 0, 0, &already);
            SetEvent(event);
            MsgWaitForMultipleObjectsEx(0, NULL, 300, QS_ALLINPUT, 0);
            try_remove("  port after");
        }
        drain_in_proc = 0;
    }

    printf("\n== test 40: PostMessage / SendMessage of 0x60 and neighbours: what does the window procedure see (the top bit of wparam)?\n");
    {
        static const UINT msgs[] = {0x60, 0x61, 0x5f, 0x401};
        static const WPARAM wps[] = {5, 6, 1, 0x80000005, 0xffffffff};
        unsigned int a, b;
        MSG m;
        for (a = 0; a < sizeof(msgs) / sizeof(msgs[0]); a++)
            for (b = 0; b < sizeof(wps) / sizeof(wps[0]); b++)
            {
                PostMessageW(hwnd, msgs[a], wps[b], 4);
                printf("  PostMessage(msg %#x, wp %#Ix, lp 4): ", msgs[a], wps[b]);
                if (PeekMessageW(&m, hwnd, 0, 0, PM_REMOVE))
                {
                    printf("PeekMessage gave msg %#x wp %#Ix lp %#Ix; ", m.message, m.wParam, m.lParam);
                    DispatchMessageW(&m);
                }
                else printf("no message; ");
                printf("\n");
            }
    }

    printf("\n== test 41: where does the special handling of 0x60 apply? an unregistered window of this thread, windows of a thread with and without Init2\n");
    {
        HWND wu = CreateWindowExW(0, L"CmqProbe", NULL, 0, 0, 0, 0, 0, HWND_MESSAGE, NULL, GetModuleHandleW(NULL), NULL);
        MSG m;
        printf("  unregistered window %p of the registered thread: PostMessage(0x60, 5, 4)\n", wu);
        PostMessageW(wu, 0x60, 5, 4);
        while (PeekMessageW(&m, NULL, 0, 0, PM_REMOVE)) { printf("    PeekMessage: msg %#x wp %#Ix\n", m.message, m.wParam); DispatchMessageW(&m); }
        printf("  SendMessage(0x60, 5, 4) to the registered window:\n");
        SendMessageW(hwnd, 0x60, 5, 4);
        while (PeekMessageW(&m, NULL, 0, 0, PM_REMOVE)) { printf("    PeekMessage: msg %#x wp %#Ix\n", m.message, m.wParam); DispatchMessageW(&m); }
        printf("  SendNotifyMessage(0x60, 5, 4) to the registered window:\n");
        SendNotifyMessageW(hwnd, 0x60, 5, 4);
        while (PeekMessageW(&m, NULL, 0, 0, PM_REMOVE)) { printf("    PeekMessage: msg %#x wp %#Ix\n", m.message, m.wParam); DispatchMessageW(&m); }
        printf("  PostMessage(0x60, 3, 0) and PostMessage(0x60, 8, 9) to the registered window:\n");
        PostMessageW(hwnd, 0x60, 3, 0);
        PostMessageW(hwnd, 0x60, 8, 9);
        while (PeekMessageW(&m, NULL, 0, 0, PM_REMOVE)) { printf("    PeekMessage: msg %#x wp %#Ix lp %#Ix\n", m.message, m.wParam, m.lParam); DispatchMessageW(&m); }
        DestroyWindow(wu);
    }
    {
        struct t41 { HWND hwnd; HANDLE ready, done; int init; } d[2] = {{0}, {0}};
        DWORD CALLBACK worker(void *p)
        {
            struct t41 *d = p;
            MSG m;
            ULONG ex = 0;
            d->hwnd = CreateWindowExW(0, L"CmqProbe", NULL, 0, 0, 0, 0, 0, HWND_MESSAGE, NULL, GetModuleHandleW(NULL), NULL);
            if (d->init) pInit2(d->hwnd, &ex, 0, 0);
            SetEvent(d->ready);
            WaitForSingleObject(d->done, 5000);
            printf("  [worker %lu, init %d] before pumping:\n", GetCurrentThreadId(), d->init);
            while (PeekMessageW(&m, NULL, 0, 0, PM_REMOVE)) { printf("    PeekMessage: msg %#x wp %#Ix lp %#Ix\n", m.message, m.wParam, m.lParam); DispatchMessageW(&m); }
            printf("  [worker] done pumping\n");
            return 0;
        }
        int k;
        for (k = 0; k < 2; k++)
        {
            HANDLE th;
            d[k].init = k;
            d[k].ready = CreateEventW(NULL, FALSE, FALSE, NULL);
            d[k].done = CreateEventW(NULL, FALSE, FALSE, NULL);
            th = CreateThread(NULL, 0, worker, &d[k], 0, NULL);
            WaitForSingleObject(d[k].ready, 3000);
            printf("  main thread posts 0x60 (wp 5, lp 4) to the window of a thread %s Init2:\n", k ? "with" : "without");
            PostMessageW(d[k].hwnd, 0x60, 5, 4);
            printf("  (PostMessage returned)\n");
            SetEvent(d[k].done);
            WaitForSingleObject(th, 3000);
            CloseHandle(th);
        }
    }

    printf("\n== test 12: nested waits inside the window procedure (completion 0x31 starts it; 0x32 arrives during the nested waits)\n");
    pNtSetIoCompletion(port, (PVOID)0xd1, (PVOID)0x31, 0, 0);
    r = MsgWaitForMultipleObjectsEx(0, NULL, 1000, QS_ALLINPUT, 0);
    printf("  [%4lu ms] outer MsgWait returned %#lx\n", GetTickCount() - t0, r);
    try_remove("remove (0x32 should have been taken by a wait if nested dispatch happens)");

    printf("\n== test 13: completion arrives while this thread is blocked in SendMessage to another thread's window\n");
    {
        HANDLE ready = CreateEventW(NULL, FALSE, FALSE, NULL);
        HWND w2 = NULL;
        DWORD CALLBACK other(void *p)
        {
            MSG m;
            w2 = CreateWindowExW(0, L"CmqProbe", NULL, 0, 0, 0, 0, 0, HWND_MESSAGE, NULL, GetModuleHandleA(NULL), NULL);
            SetEvent(ready);
            while (GetMessageW(&m, NULL, 0, 0) > 0) DispatchMessageW(&m);
            return 0;
        }
        HANDLE th = CreateThread(NULL, 0, other, NULL, 0, NULL);
        LRESULT lr;
        WaitForSingleObject(ready, 2000);
        t0 = GetTickCount();
        CreateThread(NULL, 0, setter2, NULL, 0, NULL);
        printf("  [%4lu ms] SendMessage to the other thread's window (it sleeps 500 ms); a completion is queued at 150 ms\n", GetTickCount() - t0);
        lr = SendMessageW(w2, 0x401, 0, 0);
        printf("  [%4lu ms] SendMessage returned %Id\n", GetTickCount() - t0, lr);
        try_remove("remove (still queued if it was not dispatched during the SendMessage wait)");
        PostMessageW(w2, WM_QUIT, 0, 0);
        WaitForSingleObject(th, 2000);
    }

    printf("\n== test 6: NtSetIoCompletion on the handle directly\n");
    t0 = GetTickCount();
    {
        LONG st = pNtSetIoCompletion(port, (PVOID)0xaa, (PVOID)0xbb, 0, 0);
        printf("  NtSetIoCompletion status %#lx\n", st);
    }
    status("after set");
    r = MsgWaitForMultipleObjectsEx(0, NULL, 500, QS_ALLINPUT, 0);
    printf("  [%4lu ms] MsgWait returned %#lx\n", GetTickCount() - t0, r);
    try_remove("remove");
    return 0;
}

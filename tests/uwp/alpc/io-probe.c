/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright 2026 WineExplorer contributors
 *
 * Black-box probe (Windows 11 and Wine): which waits deliver a packet queued with NtSetIoCompletion on the port of a thread that
 * called NtUserInitThreadCoreMessagingIocp2 (Sleep, SleepEx alertable, WaitForSingleObject, MsgWait), and which (key, apc,
 * status, info) combinations with a zero APC context are delivered as message 0x60.
 */
#include <windows.h>
#include <winternl.h>
#include <stdio.h>

typedef ULONG_PTR (WINAPI *init2_t)(HWND, ULONG *, ULONG_PTR, ULONG_PTR);
typedef LONG (WINAPI *setio_t)(HANDLE, ULONG_PTR, ULONG_PTR, LONG, ULONG_PTR);
typedef LONG (WINAPI *remove_t)(HANDLE, PVOID *, PVOID *, IO_STATUS_BLOCK *, LARGE_INTEGER *);

static int count;
static ULONG_PTR last_wp, last_lp;
static LRESULT CALLBACK wndproc(HWND h, UINT m, WPARAM w, LPARAM l)
{
    if (m == 0x60) { count++; last_wp = w; last_lp = l; }
    return DefWindowProcW(h, m, w, l);
}

static HANDLE port;
static setio_t pSet;
static remove_t pRemove;

static int leftover(void)
{
    PVOID key, apc; IO_STATUS_BLOCK io; LARGE_INTEGER zero = {0};
    int n = 0;
    while (!pRemove(port, &key, &apc, &io, &zero)) n++;
    return n;
}

int main(void)
{
    HMODULE ntdll = GetModuleHandleA("ntdll.dll"), win32u = LoadLibraryA("win32u.dll");
    init2_t pInit2 = (void *)GetProcAddress(win32u, "NtUserInitThreadCoreMessagingIocp2");
    WNDCLASSW wc = {0};
    HWND hwnd;
    ULONG out;
    HANDLE ev = CreateEventW(NULL, FALSE, FALSE, NULL);
    MSG msg;
    int i;
    struct { ULONG_PTR key, apc; LONG status; ULONG_PTR info; } c[] = {
        {0x11, 0x21, 0, 0}, {0x7777, 0, 0, 0}, {0x7777, 0, 0, 0x6666}, {0x7777, 0, 5, 0}, {0, 0, 0, 0}, {0xabcd0000, 0, 0, 0}, {0x7777, 0x1, 0, 0},
    };

    pSet = (void *)GetProcAddress(ntdll, "NtSetIoCompletion");
    pRemove = (void *)GetProcAddress(ntdll, "NtRemoveIoCompletion");
    LoadLibraryA("user32.dll");
    wc.lpfnWndProc = wndproc; wc.hInstance = GetModuleHandleA(NULL); wc.lpszClassName = L"IoProbe";
    RegisterClassW(&wc);
    hwnd = CreateWindowExW(0, L"IoProbe", NULL, 0, 0, 0, 0, 0, HWND_MESSAGE, NULL, wc.hInstance, NULL);
    port = (HANDLE)pInit2(hwnd, &out, 0, 0);

    printf("== which wait delivers a queued packet (key 0x11, apc 0x21)\n");
    {
        const char *names[] = {"Sleep(100)", "SleepEx(100, TRUE)", "WaitForSingleObject(ev, 100)", "WaitForSingleObjectEx(ev, 100, TRUE)", "MsgWaitForMultipleObjectsEx(0, 100, QS_ALLINPUT)", "PeekMessage only", "GetQueueStatus + Sleep(0)"};
        for (i = 0; i < 7; i++)
        {
            count = 0;
            pSet(port, 0x11, 0x21, 0, 0);
            switch (i)
            {
            case 0: Sleep(100); break;
            case 1: SleepEx(100, TRUE); break;
            case 2: WaitForSingleObject(ev, 100); break;
            case 3: WaitForSingleObjectEx(ev, 100, TRUE); break;
            case 4: MsgWaitForMultipleObjectsEx(0, NULL, 100, QS_ALLINPUT, 0); break;
            case 5: PeekMessageW(&msg, NULL, 0, 0, PM_REMOVE); break;
            case 6: GetQueueStatus(QS_ALLINPUT); Sleep(0); break;
            }
            printf("  %-52s delivered %d message(s) (wp %#Ix lp %#Ix), port left with %d packet(s)\n", names[i], count, count ? last_wp : 0, count ? last_lp : 0, leftover());
        }
    }

    printf("== NtSetIoCompletion packets, MsgWaitForMultipleObjectsEx(0, 300, QS_ALLINPUT)\n");
    for (i = 0; i < (int)(sizeof(c) / sizeof(c[0])); i++)
    {
        DWORD st;
        count = 0;
        pSet(port, c[i].key, c[i].apc, c[i].status, c[i].info);
        st = MsgWaitForMultipleObjectsEx(0, NULL, 300, QS_ALLINPUT, 0);
        printf("  key %#Ix apc %#Ix status %ld info %#Ix: wait %#lx, delivered %d (wp %#Ix lp %#Ix), port left %d\n", c[i].key, c[i].apc, c[i].status, c[i].info,
               st, count, count ? last_wp : 0, count ? last_lp : 0, leftover());
    }
    return 0;
}

/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright 2026 WineExplorer contributors
 *
 * Black-box probe (Windows 11 and Wine): the wparam of message 0x60 for a wait packet with a zero APC context when the thread waits like
 * CoreMessagingXP does: MsgWaitForMultipleObjectsEx(1, &event, INFINITE, 0x1cff, flags), the packet's target signaled by another thread,
 * with variations in the conditions set up before (timer, deferred callback, ordinal 2582, wake mask, flags, a second registered window).
 */
#include <windows.h>
#include <winternl.h>
#include <stdio.h>

typedef ULONG_PTR (WINAPI *init2_t)(HWND, ULONG *, ULONG_PTR, ULONG_PTR);
typedef INT (WINAPI *sched_t)(HWND);
typedef LONG (WINAPI *create_t)(HANDLE *, ACCESS_MASK, OBJECT_ATTRIBUTES *);
typedef LONG (WINAPI *assoc_t)(HANDLE, HANDLE, HANDLE, PVOID, PVOID, LONG, ULONG_PTR, BOOLEAN *);

static int count;
static ULONG_PTR wps[8], lps[8];
static HWND hwnds[8];
static LRESULT CALLBACK wndproc(HWND h, UINT m, WPARAM w, LPARAM l)
{
    if (m == 0x60 && count < 8) { hwnds[count] = h; wps[count] = w; lps[count] = l; count++; }
    return DefWindowProcW(h, m, w, l);
}

static HANDLE trigger_event;
static DWORD WINAPI signaler(void *arg) { Sleep(150); SetEvent(trigger_event); return 0; }

static init2_t pInit2;
static sched_t pSched;
static create_t pCreate;
static assoc_t pAssoc;

static void run(const char *title, DWORD mask, DWORD flags, int timer, int callback, int sched, int second_window, int wait_handle)
{
    WNDCLASSW wc = {0};
    HWND hwnd, hwnd2 = NULL;
    ULONG out;
    HANDLE port, packet, ev, zev = CreateEventW(NULL, FALSE, FALSE, NULL), handles[1];
    OBJECT_ATTRIBUTES oa;
    MSG msg;
    int i;

    InitializeObjectAttributes(&oa, NULL, 0, NULL, NULL);
    wc.lpfnWndProc = wndproc; wc.hInstance = GetModuleHandleA(NULL); wc.lpszClassName = L"WakeProbe";
    RegisterClassW(&wc);
    hwnd = CreateWindowExW(0, L"WakeProbe", NULL, 0, 0, 0, 0, 0, HWND_MESSAGE, NULL, wc.hInstance, NULL);
    if (second_window) hwnd2 = CreateWindowExW(0, L"WakeProbe", NULL, 0, 0, 0, 0, 0, HWND_MESSAGE, NULL, wc.hInstance, NULL);
    port = (HANDLE)pInit2(hwnd, &out, 0, 0);
    if (second_window) pInit2(hwnd2, &out, 0, 0);
    if (timer) SetTimer(hwnd, 1, 0x7fffffff, NULL);
    if (callback) SendMessageCallbackW(hwnd, 0x60, 5, 5, NULL, 1);
    ev = CreateEventW(NULL, FALSE, FALSE, NULL);   /* auto-reset target, like the first event of the thread */
    trigger_event = ev;
    pCreate(&packet, 1, &oa);
    pAssoc(packet, port, ev, (PVOID)0x1c9a6160520ull, (PVOID)0, 0, 0, NULL);
    if (sched) pSched(hwnd);
    handles[0] = zev;
    CreateThread(NULL, 0, signaler, NULL, 0, NULL);
    count = 0;
    {
        DWORD end = GetTickCount() + 700, st = 0, n = 0;
        /* a loop like the real one: wait, then drain the queue, until the time is over */
        while (GetTickCount() < end && n < 20)
        {
            DWORD left = end - GetTickCount();
            st = wait_handle ? MsgWaitForMultipleObjectsEx(1, handles, left, mask, flags) : MsgWaitForMultipleObjectsEx(0, NULL, left, mask, flags);
            n++;
            while (PeekMessageW(&msg, NULL, 0, 0, PM_REMOVE)) { if (count < 8 && msg.message == 0x60) { hwnds[count] = msg.hwnd; wps[count] = msg.wParam; lps[count] = msg.lParam | 0x1000000000000ull; count++; } DispatchMessageW(&msg); }
        }
        printf("%-62s last wait %#lx after %lu waits; deliveries (lp bit 48 = a posted message):", title, st, n);
    }
    for (i = 0; i < count; i++) printf(" [%s wp %#Ix lp %#Ix]", hwnds[i] == hwnd ? "w1" : "w2", wps[i], lps[i]);
    printf("\n");
    DestroyWindow(hwnd); if (hwnd2) DestroyWindow(hwnd2);
    CloseHandle(packet); CloseHandle(ev); CloseHandle(zev);
}

int main(void)
{
    HMODULE ntdll = GetModuleHandleA("ntdll.dll"), win32u = LoadLibraryA("win32u.dll"), user32 = LoadLibraryA("user32.dll");
    pInit2 = (void *)GetProcAddress(win32u, "NtUserInitThreadCoreMessagingIocp2");
    pSched = (void *)GetProcAddress(user32, (LPCSTR)2582);
    pCreate = (void *)GetProcAddress(ntdll, "NtCreateWaitCompletionPacket");
    pAssoc = (void *)GetProcAddress(ntdll, "NtAssociateWaitCompletionPacket");
    /* each run uses its own thread: Init2 binds the window to the thread's queue once */
    run("plain, mask QS_ALLINPUT, no event handle",                         QS_ALLINPUT, 0, 0, 0, 0, 0, 0);
    run("mask 0x1cff, one event handle",                                    0x1cff, 0, 0, 0, 0, 0, 1);
    run("mask 0x1cff, one handle, timer, callback (5,5)",                   0x1cff, 0, 1, 1, 0, 0, 1);
    run("mask 0x1cff, one handle, timer, callback, ordinal 2582",           0x1cff, 0, 1, 1, 1, 0, 1);
    run("mask 0x1cff, flags MWMO_INPUTAVAILABLE",                           0x1cff, MWMO_INPUTAVAILABLE, 0, 0, 0, 0, 1);
    run("mask 0x1cff, flags MWMO_ALERTABLE",                                0x1cff, MWMO_ALERTABLE, 0, 0, 0, 0, 1);
    run("mask 0x1cff, two registered windows",                              0x1cff, 0, 0, 0, 0, 1, 1);
    return 0;
}

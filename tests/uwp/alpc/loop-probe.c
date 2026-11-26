/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright 2026 WineExplorer contributors
 *
 * The CoreMessaging worker's steady-state round, alone in one thread: ordinal 2582 (ScheduleDispatchNotification),
 * SetTimer(hwnd,1,0), SetTimer(hwnd,1,0x7fffffff), Drain2, MsgWait. Does the round keep itself going without any other
 * thread (Windows 11), or does it stop (Wine)? Prints, per round, what the wait returned and the messages that arrived.
 */
#include <windows.h>
#include <stdio.h>

typedef ULONG_PTR (WINAPI *init2_t)(HWND, ULONG *, ULONG_PTR, ULONG_PTR);
typedef ULONG_PTR (WINAPI *drain2_t)(HWND, ULONG_PTR *, ULONG_PTR *, ULONG_PTR *);
typedef ULONG_PTR (WINAPI *sched_t)(HWND, ULONG_PTR, ULONG_PTR *);

static char log[16][64]; static int nlog;
static void add(const char *fmt, UINT a, ULONG_PTR b, LONG_PTR c)
{
    if (nlog < 16) sprintf(log[nlog++], fmt, a, (unsigned)b, (unsigned)c);
}

static LRESULT CALLBACK wndproc(HWND h, UINT m, WPARAM w, LPARAM l)
{
    if (m == 0x60) add("0x%x(%x,%x)", m, w, l);
    else if (m == WM_TIMER) add("TIMER%.0x(%x,%x)", 0, w, l);
    return DefWindowProcW(h, m, w, l);
}

int main(void)
{
    HMODULE w32 = LoadLibraryA("win32u.dll");
    init2_t init2 = (void *)GetProcAddress(w32, "NtUserInitThreadCoreMessagingIocp2");
    drain2_t drain2 = (void *)GetProcAddress(w32, "NtUserDrainThreadCoreMessagingCompletions2");
    sched_t sched = (void *)GetProcAddress(w32, "NtUserScheduleDispatchNotification");
    HINSTANCE inst = GetModuleHandleA(NULL);
    WNDCLASSW wc = {0};
    HWND hwnd;
    ULONG out;
    ULONG_PTR a, b, c, ret;
    int round, stalled = 0;
    HANDLE ev;
    MSG msg;

    setvbuf(stdout, NULL, _IONBF, 0);
    LoadLibraryA("user32.dll");
    if (!init2 || !drain2 || !sched) { printf("no Init2/Drain2/Schedule exports\n"); return 1; }
    wc.lpfnWndProc = wndproc; wc.hInstance = inst; wc.lpszClassName = L"LoopProbe";
    RegisterClassW(&wc);
    hwnd = CreateWindowExW(0, L"LoopProbe", NULL, 0, 0, 0, 0, 0, HWND_MESSAGE, NULL, inst, NULL);
    ev = CreateEventW(NULL, FALSE, FALSE, NULL);
    ret = init2(hwnd, &out, 0, 0);
    printf("Init2 %s\n", ret ? "ok" : "failed");

    for (round = 0; round < 40; round++)
    {
        DWORD r;
        int i;
        nlog = 0;
        a = 0;
        sched(hwnd, 0, &a);
        SetTimer(hwnd, 1, 0, NULL);
        SetTimer(hwnd, 1, 0x7fffffff, NULL);
        a = b = c = 0;
        ret = drain2(hwnd, &a, &b, &c);
        r = MsgWaitForMultipleObjectsEx(1, &ev, 300, QS_ALLINPUT, 0);
        while (PeekMessageW(&msg, NULL, 0, 0, PM_REMOVE)) { TranslateMessage(&msg); DispatchMessageW(&msg); }
        printf("round %2d: drain2 %Iu, wait %s", round, ret, r == WAIT_TIMEOUT ? "TIMEOUT" : r == WAIT_OBJECT_0 ? "event" : r == WAIT_OBJECT_0 + 1 ? "message" : "other");
        for (i = 0; i < nlog; i++) printf(" %s", log[i]);
        printf("\n");
        if (r == WAIT_TIMEOUT) stalled++;
    }
    printf("rounds that timed out: %d of 40\n", stalled);
    return 0;
}

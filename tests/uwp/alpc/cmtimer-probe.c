/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright 2026 WineExplorer contributors
 *
 * Black-box probe (Windows 11 and Wine): what a window timer becomes on a window registered with CoreMessaging
 * (user32 ordinal 2669, NtUserInitThreadCoreMessagingIocp2). CoreMessagingXP sets timer 1 on its message-only window to
 * 0x7fffffff at start-up and, in its wake handler, to 0 and back to 0x7fffffff; its threads never dispatch WM_TIMER, but
 * they get wakes (message 0x60, wparam 1) that no completion explains. Each case runs on a new thread for 400 ms and
 * records every message the window procedure gets and every message PeekMessage returns.
 */
#include <windows.h>
#include <stdio.h>

static ULONG_PTR (WINAPI *p2669)(HWND, ULONG *);

#define MAXLOG 64
struct entry { DWORD ms; const char *where; UINT msg; WPARAM wp; LPARAM lp; };
static struct entry log_[MAXLOG];
static int nlog;
static const char *cur = "?";
static DWORD t0;

static void add(const char *where, UINT msg, WPARAM wp, LPARAM lp)
{
    if (nlog >= MAXLOG) return;
    log_[nlog].ms = GetTickCount() - t0; log_[nlog].where = where; log_[nlog].msg = msg;
    log_[nlog].wp = wp; log_[nlog].lp = lp;
    nlog++;
}

static LRESULT CALLBACK wndproc(HWND h, UINT m, WPARAM w, LPARAM l)
{
    if (m == 0x60 || m == WM_TIMER) { add(cur, m, w, l); return 0; }
    return DefWindowProcW(h, m, w, l);
}

struct test { const char *name; int reg; UINT id; UINT first; UINT second; };

static DWORD WINAPI run(void *arg)
{
    const struct test *t = arg;
    HANDLE zev = CreateEventW(NULL, FALSE, FALSE, NULL);
    ULONG out = 0;
    ULONG_PTR port = 0;
    HWND hwnd;
    MSG msg;
    DWORD end;

    nlog = 0;
    hwnd = CreateWindowExW(0, L"CmTimerProbe", NULL, 0, 0, 0, 0, 0, HWND_MESSAGE, NULL, GetModuleHandleW(NULL), NULL);
    if (t->reg) port = p2669(hwnd, &out);
    t0 = GetTickCount();
    SetTimer(hwnd, t->id, t->first, NULL);
    if (t->second) SetTimer(hwnd, t->id, t->second, NULL);
    end = t0 + 400;
    while (GetTickCount() < end)
    {
        DWORD now = GetTickCount(), st;

        cur = "wait";
        st = MsgWaitForMultipleObjectsEx(1, &zev, now < end ? end - now : 0, QS_ALLINPUT, 0);
        if (st == WAIT_TIMEOUT) break;
        for (;;)
        {
            cur = "peek";
            if (!PeekMessageW(&msg, NULL, 0, 0, PM_REMOVE)) break;
            add("peek-returned", msg.message, msg.wParam, msg.lParam);
            cur = "dispatch";
            DispatchMessageW(&msg);
            if (nlog >= MAXLOG - 2) break;
        }
        if (nlog >= MAXLOG - 2) break;
    }
    printf("%s: registered %d (port %p), timer %u: %u then %u\n", t->name, t->reg, (void *)port, t->id, t->first, t->second);
    {
        int i;
        for (i = 0; i < nlog && i < 24; i++)
            printf("  [%3lu ms] %-14s msg %#x wp %#llx lp %#llx\n", log_[i].ms, log_[i].where, log_[i].msg,
                   (unsigned long long)log_[i].wp, (unsigned long long)log_[i].lp);
        if (nlog > 24) printf("  ... %d entries in all\n", nlog);
    }
    KillTimer(hwnd, t->id);
    DestroyWindow(hwnd);
    CloseHandle(zev);
    return 0;
}

int main(void)
{
    static const struct test tests[] =
    {
        {"A registered, timer 1 = 0", 1, 1, 0, 0},
        {"B registered, timer 1 = 0x7fffffff then 0 (wake handler order)", 1, 1, 0x7fffffff, 0},
        {"C registered, timer 1 = 0 then 0x7fffffff", 1, 1, 0, 0x7fffffff},
        {"D registered, timer 2 = 0", 1, 2, 0, 0},
        {"E registered, timer 1 = 100", 1, 1, 100, 0},
        {"F not registered, timer 1 = 0", 0, 1, 0, 0},
    };
    WNDCLASSW wc = {0};
    HMODULE user32 = GetModuleHandleA("user32.dll");
    int i;

    p2669 = (void *)GetProcAddress(user32, (const char *)(ULONG_PTR)2669);
    if (!p2669) { printf("user32 ordinal 2669 missing\n"); return 1; }
    wc.lpfnWndProc = wndproc; wc.hInstance = GetModuleHandleW(NULL); wc.lpszClassName = L"CmTimerProbe";
    RegisterClassW(&wc);
    for (i = 0; i < (int)(sizeof(tests) / sizeof(tests[0])); i++)
    {
        HANDLE th = CreateThread(NULL, 0, run, (void *)&tests[i], 0, NULL);
        WaitForSingleObject(th, 5000);
        CloseHandle(th);
    }
    return 0;
}

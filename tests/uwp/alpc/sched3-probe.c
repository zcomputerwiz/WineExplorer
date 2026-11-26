/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright 2026 WineExplorer contributors
 *
 * Black-box probe (Windows 11 and Wine): the arguments of user32 ordinal 2582 (win32u NtUserScheduleDispatchNotification).
 * CoreMessagingXP calls it as (hwnd, 0, 6) before its dispatcher (6) rounds and with other third arguments elsewhere. A window is
 * registered with ordinal 2669 (as CoreMessagingXP does); each case calls 2582 with different arguments and records every message
 * 0x60 the window procedure gets (where: inside the call, in PeekMessage, in DispatchMessage; whether it was sent) and every
 * posted 0x60 PeekMessage returns.
 */
#include <windows.h>
#include <stdio.h>

static ULONG_PTR (WINAPI *p2669)(HWND, ULONG *);
static ULONG_PTR (WINAPI *p2582)(HWND, ULONG_PTR, ULONG_PTR, ULONG_PTR);
static const char *where = "?";
static char log_[1024];

static void note(const char *s)
{
    if (strlen(log_) + strlen(s) + 2 < sizeof(log_)) { strcat(log_, s); strcat(log_, " "); }
}

static LRESULT CALLBACK wndproc(HWND h, UINT m, WPARAM w, LPARAM l)
{
    char buf[96];
    if (m != 0x60) return DefWindowProcW(h, m, w, l);
    sprintf(buf, "proc(%llx,%llx)@%s%s", (unsigned long long)w, (unsigned long long)l, where,
            (InSendMessageEx(NULL) & ISMEX_SEND) ? "/sent" : "");
    note(buf);
    return 0;
}

static void pump(const char *tag)
{
    MSG msg;
    char buf[64];
    where = tag;
    while (PeekMessageW(&msg, NULL, 0, 0, PM_REMOVE))
    {
        if (msg.message == 0x60) { sprintf(buf, "got(%llx,%llx)", (unsigned long long)msg.wParam, (unsigned long long)msg.lParam); note(buf); }
        where = "dispatch";
        DispatchMessageW(&msg);
        where = tag;
    }
}

struct test { const char *name; ULONG_PTR a2, a3, a4; };

static DWORD WINAPI run(void *arg)
{
    const struct test *t = arg;
    HWND hwnd;
    ULONG out = 0;
    ULONG_PTR r;

    hwnd = CreateWindowExW(0, L"Sched3Probe", NULL, 0, 0, 0, 0, 0, HWND_MESSAGE, NULL, GetModuleHandleW(NULL), NULL);
    p2669(hwnd, &out);
    pump("setup");
    log_[0] = 0;
    where = "call";
    r = p2582(hwnd, t->a2, t->a3, t->a4);
    {
        char buf[32]; sprintf(buf, "ret=%llx", (unsigned long long)r); note(buf);
    }
    pump("peek");
    note("|wait");
    {
        HANDLE e = CreateEventW(NULL, FALSE, FALSE, NULL);
        where = "wait";
        MsgWaitForMultipleObjectsEx(1, &e, 100, QS_ALLINPUT, 0);
        CloseHandle(e);
    }
    pump("peek2");
    printf("%-32s %s\n", t->name, log_);
    DestroyWindow(hwnd);
    return 0;
}

int main(void)
{
    static const struct test tests[] =
    {
        {"2582(hwnd, 0, 0, 0)", 0, 0, 0},
        {"2582(hwnd, 0, 6, 0)", 0, 6, 0},
        {"2582(hwnd, 0, 5, 0)", 0, 5, 0},
        {"2582(hwnd, 0, 0x1234, 0)", 0, 0x1234, 0},
        {"2582(hwnd, 1, 6, 0)", 1, 6, 0},
        {"2582(hwnd, 0, 6, 7)", 0, 6, 7},
    };
    WNDCLASSW wc = {0};
    HMODULE user32 = GetModuleHandleA("user32.dll");
    int i;

    setvbuf(stdout, NULL, _IONBF, 0);
    p2669 = (void *)GetProcAddress(user32, (const char *)(ULONG_PTR)2669);
    p2582 = (void *)GetProcAddress(user32, (const char *)(ULONG_PTR)2582);
    if (!p2669 || !p2582) { printf("ordinals missing\n"); return 1; }
    wc.lpfnWndProc = wndproc; wc.hInstance = GetModuleHandleW(NULL); wc.lpszClassName = L"Sched3Probe";
    RegisterClassW(&wc);
    for (i = 0; i < (int)(sizeof(tests) / sizeof(tests[0])); i++)
    {
        HANDLE th = CreateThread(NULL, 0, run, (void *)&tests[i], 0, NULL);
        WaitForSingleObject(th, 5000);
        CloseHandle(th);
    }
    return 0;
}

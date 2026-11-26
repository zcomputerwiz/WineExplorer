/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright 2026 WineExplorer contributors
 *
 * Black-box probe (Windows 11 and Wine): when a zero-APC wait completion on a thread's CoreMessaging port (user32 ordinal 2669)
 * becomes the posted message 0x60 (1, 0), measured by its place among the thread's other posted messages. The thread posts
 * itself M1, then another thread signals the packet's event (the thread is busy, not waiting), then the thread posts M2, and only
 * then retrieves its messages with PeekMessage (no wait first); finally it waits with MsgWaitForMultipleObjectsEx and retrieves
 * again. Windows: if the kernel posts the wake when the packet completes, the order is M1, (1), M2.
 */
#include <windows.h>
#include <winternl.h>
#include <stdio.h>

typedef LONG (WINAPI *create_t)(HANDLE *, ACCESS_MASK, OBJECT_ATTRIBUTES *);
typedef LONG (WINAPI *assoc_t)(HANDLE, HANDLE, HANDLE, PVOID, PVOID, LONG, ULONG_PTR, BOOLEAN *);

static ULONG_PTR (WINAPI *p2669)(HWND, ULONG *);
static create_t pCreate;
static assoc_t pAssoc;
static HANDLE g_event;
static char order[512];
static const WCHAR *class_name = L"WakeOrderProbe";
static LRESULT ret3 = 0;   /* what the procedure returns for (3, x) */
static const char *where = "?";

#define M1 (WM_APP + 1)
#define M2 (WM_APP + 2)

static void note(const char *s)
{
    if (strlen(order) + strlen(s) + 2 < sizeof(order)) { strcat(order, s); strcat(order, " "); }
}

static LRESULT CALLBACK wndproc(HWND h, UINT m, WPARAM w, LPARAM l)
{
    char buf[64];
    if (m == 0x60) { sprintf(buf, "proc(0x60,%llx)@%s", (unsigned long long)w, where); note(buf); return w == 3 ? ret3 : 0; }
    if (m == M1) { note("M1"); return 0; }
    if (m == M2) { note("M2"); return 0; }
    return DefWindowProcW(h, m, w, l);
}

static DWORD WINAPI signaler(void *arg)
{
    SetEvent(g_event);
    return 0;
}

static void pump(const char *tag)
{
    MSG msg;
    char buf[64];
    where = tag;
    while (PeekMessageW(&msg, NULL, 0, 0, PM_REMOVE))
    {
        if (msg.message == 0x60) { sprintf(buf, "got(0x60,%llx)", (unsigned long long)msg.wParam); note(buf); }
        DispatchMessageW(&msg);
    }
}

static DWORD WINAPI run(void *arg)
{
    WNDCLASSW wc = {0};
    OBJECT_ATTRIBUTES oa;
    HANDLE port, packet, th, dummy = CreateEventW(NULL, FALSE, FALSE, NULL);
    ULONG out = 0;
    HWND hwnd;
    DWORD st;

    InitializeObjectAttributes(&oa, NULL, 0, NULL, NULL);
    wc.lpfnWndProc = wndproc; wc.hInstance = GetModuleHandleW(NULL); wc.lpszClassName = class_name;
    RegisterClassW(&wc);
    hwnd = CreateWindowExW(0, class_name, NULL, 0, 0, 0, 0, 0, HWND_MESSAGE, NULL, wc.hInstance, NULL);
    port = (HANDLE)p2669(hwnd, &out);
    g_event = CreateEventW(NULL, FALSE, FALSE, NULL);
    pCreate(&packet, 1, &oa);
    pAssoc(packet, port, g_event, (PVOID)0x1234, NULL, 0, 0, NULL);
    pump("start");
    order[0] = 0;

    PostMessageW(hwnd, M1, 0, 0);
    th = CreateThread(NULL, 0, signaler, NULL, 0, NULL);
    WaitForSingleObject(th, INFINITE); CloseHandle(th);
    Sleep(50);                                  /* busy, not in a message wait */
    PostMessageW(hwnd, M2, 0, 0);
    printf("GetQueueStatus before retrieval: %#lx\n", GetQueueStatus(QS_ALLINPUT));
    pump("peek");
    note("|wait");
    where = "wait";
    st = MsgWaitForMultipleObjectsEx(1, &dummy, 300, QS_ALLINPUT, 0);
    { char buf[32]; sprintf(buf, "wait=%lu", st); note(buf); }
    pump("peek2");
    printf("order: %s\n", order);
    DestroyWindow(hwnd);
    return 0;
}

int main(int argc, char **argv)
{
    HMODULE ntdll = GetModuleHandleA("ntdll.dll");
    HANDLE th;

    p2669 = (void *)GetProcAddress(GetModuleHandleA("user32.dll"), (const char *)(ULONG_PTR)2669);
    pCreate = (create_t)GetProcAddress(ntdll, "NtCreateWaitCompletionPacket");
    pAssoc = (assoc_t)GetProcAddress(ntdll, "NtAssociateWaitCompletionPacket");
    if (!p2669 || !pCreate || !pAssoc) { printf("missing entry points\n"); return 1; }
    setvbuf(stdout, NULL, _IONBF, 0);
    if (argc > 2) ret3 = (LRESULT)_strtoi64(argv[2], NULL, 0);
    if (argc > 1) { static WCHAR name[64]; MultiByteToWideChar(CP_ACP, 0, argv[1], -1, name, 64); class_name = name; }
    printf("class %ls, (3,x) returns %lld\n", class_name, (long long)ret3);
    th = CreateThread(NULL, 0, run, NULL, 0, NULL);
    WaitForSingleObject(th, 10000);
    return 0;
}

/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright 2026 WineExplorer contributors
 *
 * Black-box probe (Windows 11 and Wine): which message retrieval calls (PeekMessage with PM_REMOVE / PM_NOREMOVE / a filter, GetMessage,
 * GetQueueStatus, MsgWait, a wait while the packet's target also is the handle waited for) deliver a completion queued on the port of a
 * thread that called NtUserInitThreadCoreMessagingIocp2, with which wparam/lparam and whether the call then reports a message.
 */
#include <windows.h>
#include <winternl.h>
#include <stdio.h>

typedef ULONG_PTR (WINAPI *init2_t)(HWND, ULONG *, ULONG_PTR, ULONG_PTR);
typedef LONG (WINAPI *setio_t)(HANDLE, ULONG_PTR, ULONG_PTR, LONG, ULONG_PTR);
typedef LONG (WINAPI *create_t)(HANDLE *, ACCESS_MASK, OBJECT_ATTRIBUTES *);
typedef LONG (WINAPI *assoc_t)(HANDLE, HANDLE, HANDLE, PVOID, PVOID, LONG, ULONG_PTR, BOOLEAN *);
typedef LONG (WINAPI *remove_t)(HANDLE, PVOID *, PVOID *, IO_STATUS_BLOCK *, LARGE_INTEGER *);

static int count;
static ULONG_PTR wps[4], lps[4];
static LRESULT CALLBACK wndproc(HWND h, UINT m, WPARAM w, LPARAM l)
{
    if (m == 0x60 && count < 4) { wps[count] = w; lps[count] = l; count++; }
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

static void report(const char *what, ULONG_PTR apc, const char *result)
{
    int i, left = leftover();
    printf("  apc %-5#Ix %-46s %s; delivered %d:", apc, what, result, count);
    for (i = 0; i < count; i++) printf(" (wp %#Ix lp %#Ix)", wps[i], lps[i]);
    printf("; left in the port %d\n", left);
}

int main(void)
{
    HMODULE ntdll = GetModuleHandleA("ntdll.dll"), win32u = LoadLibraryA("win32u.dll");
    init2_t pInit2 = (void *)GetProcAddress(win32u, "NtUserInitThreadCoreMessagingIocp2");
    WNDCLASSW wc = {0};
    HWND hwnd;
    ULONG out;
    MSG msg;
    char buf[64];
    int a, k;
    static const ULONG_PTR apcs[] = {0, 0x21};

    pSet = (void *)GetProcAddress(ntdll, "NtSetIoCompletion");
    pRemove = (void *)GetProcAddress(ntdll, "NtRemoveIoCompletion");
    LoadLibraryA("user32.dll");
    wc.lpfnWndProc = wndproc; wc.hInstance = GetModuleHandleA(NULL); wc.lpszClassName = L"PeekProbe";
    RegisterClassW(&wc);
    hwnd = CreateWindowExW(0, L"PeekProbe", NULL, 0, 0, 0, 0, 0, HWND_MESSAGE, NULL, wc.hInstance, NULL);
    port = (HANDLE)pInit2(hwnd, &out, 0, 0);
    for (a = 0; a < 2; a++)
        for (k = 0; k < 7; k++)
        {
            BOOL r = 0;
            DWORD st = 0;
            count = 0;
            pSet(port, 0x11, apcs[a], 0, 0);
            switch (k)
            {
            case 0: r = PeekMessageW(&msg, NULL, 0, 0, PM_REMOVE); sprintf(buf, "returned %d", r); report("PeekMessage(PM_REMOVE)", apcs[a], buf); break;
            case 1: r = PeekMessageW(&msg, NULL, 0, 0, PM_NOREMOVE); sprintf(buf, "returned %d", r); report("PeekMessage(PM_NOREMOVE)", apcs[a], buf); break;
            case 2: r = PeekMessageW(&msg, hwnd, 0x60, 0x60, PM_REMOVE); sprintf(buf, "returned %d", r); report("PeekMessage(hwnd, 0x60..0x60, PM_REMOVE)", apcs[a], buf); break;
            case 3: r = PeekMessageW(&msg, NULL, WM_USER, WM_USER, PM_REMOVE); sprintf(buf, "returned %d", r); report("PeekMessage(WM_USER, PM_REMOVE)", apcs[a], buf); break;
            case 4: PostMessageW(hwnd, WM_USER, 0, 0); r = GetMessageW(&msg, NULL, 0, 0); sprintf(buf, "GetMessage returned msg %#x", msg.message); report("GetMessage (a posted WM_USER is there)", apcs[a], buf); break;
            case 5: st = GetQueueStatus(QS_ALLINPUT); sprintf(buf, "status %#lx", st); report("GetQueueStatus(QS_ALLINPUT)", apcs[a], buf); break;
            case 6: st = MsgWaitForMultipleObjectsEx(0, NULL, 100, QS_ALLINPUT, 0); sprintf(buf, "wait %#lx", st); report("MsgWait(0 handles, 100 ms)", apcs[a], buf); break;
            }
            while (PeekMessageW(&msg, NULL, 0, 0, PM_REMOVE | PM_QS_POSTMESSAGE)) ;
        }
    return 0;
}

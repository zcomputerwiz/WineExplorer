/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright 2026 WineExplorer contributors
 *
 * Black-box probe (Windows 11 and Wine): which posted messages (PeekMessage returns) appear after the CoreMessaging completion
 * machinery runs: a completion delivered in a wait, Drain2 called inside the handler, and ordinal 2582. The hook log of the WinUI 3
 * test app sees posted 0x60 messages with wparam 0x80000001 that no user-mode caller posts.
 */
#include <windows.h>
#include <winternl.h>
#include <stdio.h>

typedef ULONG_PTR (WINAPI *init2_t)(HWND, ULONG *, ULONG_PTR, ULONG_PTR);
typedef ULONG (WINAPI *drain2_t)(HWND, void *, void *, void *);
typedef INT (WINAPI *sched_t)(HWND);
typedef LONG (WINAPI *setio_t)(HANDLE, ULONG_PTR, ULONG_PTR, LONG, ULONG_PTR);
typedef LONG (WINAPI *create_t)(HANDLE *, ACCESS_MASK, OBJECT_ATTRIBUTES *);
typedef LONG (WINAPI *assoc_t)(HANDLE, HANDLE, HANDLE, PVOID, PVOID, LONG, ULONG_PTR, BOOLEAN *);

static drain2_t pDrain2;
static int drain_in_proc;
static HWND hw;
static const char *what = "";

static LRESULT CALLBACK wndproc(HWND h, UINT m, WPARAM w, LPARAM l)
{
    if (m == 0x60)
    {
        printf("    proc 0x60 wp %#Ix lp %#Ix\n", w, l);
        if (drain_in_proc) { ULONG r = pDrain2(h, NULL, NULL, NULL); printf("    (Drain2 inside the handler returned %lu)\n", r); }
    }
    return DefWindowProcW(h, m, w, l);
}

static void show_posted(void)
{
    MSG msg;
    int n = 0;
    while (PeekMessageW(&msg, NULL, 0, 0, PM_REMOVE))
    {
        printf("    posted: msg %#x wp %#Ix lp %#Ix\n", msg.message, msg.wParam, msg.lParam);
        DispatchMessageW(&msg);
        if (++n > 20) break;
    }
    if (!n) printf("    (no posted messages)\n");
}

int main(void)
{
    HMODULE ntdll = GetModuleHandleA("ntdll.dll"), win32u = LoadLibraryA("win32u.dll"), user32 = LoadLibraryA("user32.dll");
    init2_t pInit2 = (void *)GetProcAddress(win32u, "NtUserInitThreadCoreMessagingIocp2");
    sched_t pSched = (void *)GetProcAddress(user32, (LPCSTR)2582);
    setio_t pSet = (void *)GetProcAddress(ntdll, "NtSetIoCompletion");
    create_t pCreate = (void *)GetProcAddress(ntdll, "NtCreateWaitCompletionPacket");
    assoc_t pAssoc = (void *)GetProcAddress(ntdll, "NtAssociateWaitCompletionPacket");
    WNDCLASSW wc = {0};
    ULONG out;
    HANDLE port, packet, ev = CreateEventW(NULL, TRUE, FALSE, NULL);
    OBJECT_ATTRIBUTES oa;
    int i;

    pDrain2 = (void *)GetProcAddress(win32u, "NtUserDrainThreadCoreMessagingCompletions2");
    InitializeObjectAttributes(&oa, NULL, 0, NULL, NULL);
    wc.lpfnWndProc = wndproc; wc.hInstance = GetModuleHandleA(NULL); wc.lpszClassName = L"KPostProbe";
    RegisterClassW(&wc);
    hw = CreateWindowExW(0, L"KPostProbe", NULL, 0, 0, 0, 0, 0, HWND_MESSAGE, NULL, wc.hInstance, NULL);
    port = (HANDLE)pInit2(hw, &out, 0, 0);
    for (i = 0; i < 4; i++)
    {
        drain_in_proc = i == 1 || i == 3;
        printf("== %s, handler %s Drain2\n", i < 2 ? "NtSetIoCompletion apc 0" : "wait packet (event set), apc 0", drain_in_proc ? "calls" : "does not call");
        if (i < 2) pSet(port, 0x11, 0, 0, 0);
        else { ResetEvent(ev); pCreate(&packet, 1, &oa); pAssoc(packet, port, ev, (PVOID)0x1234abcd, (PVOID)0, 0, 0, NULL); SetEvent(ev); }
        MsgWaitForMultipleObjectsEx(0, NULL, 200, QS_ALLINPUT, 0);
        show_posted();
    }
    drain_in_proc = 0;
    printf("== ordinal 2582\n"); pSched(hw); MsgWaitForMultipleObjectsEx(0, NULL, 100, QS_ALLINPUT, 0); show_posted();
    printf("== Drain2 called directly with a packet queued (key 0x11 apc 0x21)\n");
    pSet(port, 0x11, 0x21, 0, 0); printf("    Drain2 -> %lu\n", pDrain2(hw, NULL, NULL, NULL)); show_posted();
    return 0;
}

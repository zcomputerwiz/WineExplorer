/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright 2026 WineExplorer contributors
 *
 * Black-box probe (Windows 11 and Wine): NtAssociateWaitCompletionPacket with a target that is ALREADY signaled, on the port of a thread
 * that called NtUserInitThreadCoreMessagingIocp2: does a completion arrive (as 0x60 wp 3 / the port), what does AlreadySignaled report
 * (with and without the pointer), and is an auto-reset event consumed? */
#include <windows.h>
#include <winternl.h>
#include <stdio.h>

typedef ULONG_PTR (WINAPI *init2_t)(HWND, ULONG *, ULONG_PTR, ULONG_PTR);
typedef LONG (WINAPI *create_t)(HANDLE *, ACCESS_MASK, OBJECT_ATTRIBUTES *);
typedef LONG (WINAPI *assoc_t)(HANDLE, HANDLE, HANDLE, PVOID, PVOID, LONG, ULONG_PTR, BOOLEAN *);
typedef LONG (WINAPI *remove_t)(HANDLE, PVOID *, PVOID *, IO_STATUS_BLOCK *, LARGE_INTEGER *);

static const char *where = "?";
static LRESULT CALLBACK wndproc(HWND h, UINT m, WPARAM w, LPARAM l)
{
    if (m == 0x60) printf("    proc 0x60 wp %#Ix lp %#Ix [%s]\n", w, l, where);
    return DefWindowProcW(h, m, w, l);
}

static void pump(DWORD ms) { where = "wait"; MsgWaitForMultipleObjectsEx(0, NULL, ms, QS_ALLINPUT, 0); { MSG m; while (PeekMessageW(&m, NULL, 0, 0, PM_REMOVE)) DispatchMessageW(&m); } }

int main(void)
{
    HMODULE ntdll = GetModuleHandleA("ntdll.dll"), win32u = LoadLibraryA("win32u.dll");
    init2_t pInit2 = (void *)GetProcAddress(win32u, "NtUserInitThreadCoreMessagingIocp2");
    create_t pCreate = (void *)GetProcAddress(ntdll, "NtCreateWaitCompletionPacket");
    assoc_t pAssoc = (void *)GetProcAddress(ntdll, "NtAssociateWaitCompletionPacket");
    remove_t pRemove = (void *)GetProcAddress(ntdll, "NtRemoveIoCompletion");
    WNDCLASSW wc = {0};
    HWND hwnd;
    ULONG out;
    HANDLE port, packet;
    OBJECT_ATTRIBUTES oa;
    int round, manual;

    LoadLibraryA("user32.dll");
    wc.lpfnWndProc = wndproc; wc.hInstance = GetModuleHandleA(NULL); wc.lpszClassName = L"AsProbe";
    RegisterClassW(&wc);
    hwnd = CreateWindowExW(0, L"AsProbe", NULL, 0, 0, 0, 0, 0, HWND_MESSAGE, NULL, wc.hInstance, NULL);
    port = (HANDLE)pInit2(hwnd, &out, 0, 0);
    InitializeObjectAttributes(&oa, NULL, 0, NULL, NULL);
    pump(50);
    for (round = 0; round < 4; round++)
    {
        BOOLEAN already = 0xee, *pa = round & 1 ? NULL : &already;
        HANDLE ev;
        LONG st;
        PVOID key, apc;
        IO_STATUS_BLOCK io;
        LARGE_INTEGER zero = {0};

        manual = round >= 2;
        ev = CreateEventW(NULL, manual, TRUE, NULL);
        printf("== round %d: %s-reset event, already signaled, AlreadySignaled pointer %s\n", round, manual ? "manual" : "auto", pa ? "given" : "NULL");
        pCreate(&packet, 1, &oa);
        st = pAssoc(packet, port, ev, (PVOID)0xabcd0000, (PVOID)0, 0, 0, pa);
        printf("  associate: status %#lx already %d\n", st, pa ? already : -1);
        printf("  event after associate: %s\n", WaitForSingleObject(ev, 0) == WAIT_OBJECT_0 ? "still signaled" : "not signaled (consumed)");
        if (!manual) SetEvent(ev);
        pump(300);
        st = pRemove(port, &key, &apc, &io, &zero);
        printf("  port after the wait: NtRemoveIoCompletion status %#lx%s\n", st, st == 0x102 ? " (empty)" : " (a packet)");
        CloseHandle(packet);
        CloseHandle(ev);
    }
    return 0;
}

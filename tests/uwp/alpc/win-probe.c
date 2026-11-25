/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright 2026 WineExplorer contributors
 *
 * Black-box probe (Windows 11 and Wine): NtUserInitThreadCoreMessagingIocp2 with several windows of one thread: which registrations
 * succeed, and which registered windows receive a completion (apc zero and non-zero), in which order.
 */
#include <windows.h>
#include <winternl.h>
#include <stdio.h>

typedef ULONG_PTR (WINAPI *init2_t)(HWND, ULONG *, ULONG_PTR, ULONG_PTR);
typedef LONG (WINAPI *setio_t)(HANDLE, ULONG_PTR, ULONG_PTR, LONG, ULONG_PTR);

static HWND w[4];
static int got[8], order[8], n;

static LRESULT CALLBACK wndproc(HWND h, UINT m, WPARAM wp, LPARAM lp)
{
    if (m == 0x60 && n < 8) { int i; for (i = 0; i < 4; i++) if (w[i] == h) got[n] = i + 1; order[n] = (int)wp; n++; }
    return DefWindowProcW(h, m, wp, lp);
}

int main(void)
{
    HMODULE ntdll = GetModuleHandleA("ntdll.dll"), win32u = LoadLibraryA("win32u.dll");
    init2_t pInit2 = (void *)GetProcAddress(win32u, "NtUserInitThreadCoreMessagingIocp2");
    setio_t pSet = (void *)GetProcAddress(ntdll, "NtSetIoCompletion");
    WNDCLASSW wc = {0};
    HANDLE port = NULL;
    int i, round;

    LoadLibraryA("user32.dll");
    wc.lpfnWndProc = wndproc; wc.hInstance = GetModuleHandleA(NULL); wc.lpszClassName = L"WinProbe";
    RegisterClassW(&wc);
    for (i = 0; i < 4; i++) w[i] = CreateWindowExW(0, L"WinProbe", NULL, 0, 0, 0, 0, 0, HWND_MESSAGE, NULL, wc.hInstance, NULL);
    for (i = 0; i < 4; i++)
    {
        ULONG out = 0xdeadbeef;
        HANDLE h = (HANDLE)pInit2(w[i], &out, 0, 0);
        printf("Init2(window %d) -> %p, existing %#lx, error %lu\n", i + 1, h, out, GetLastError());
        if (h && !port) port = h;
    }
    for (round = 0; round < 2; round++)
    {
        n = 0;
        pSet(port, 0x11, round ? 0x21 : 0, 0, 0);
        MsgWaitForMultipleObjectsEx(0, NULL, 300, QS_ALLINPUT, 0);
        printf("apc %#x: %d delivery(ies):", round ? 0x21 : 0, n);
        for (i = 0; i < n; i++) printf(" window %d (wp %d)", got[i], order[i]);
        printf("\n");
    }
    return 0;
}

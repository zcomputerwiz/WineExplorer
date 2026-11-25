/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright 2026 WineExplorer contributors
 *
 * Black-box probe (Windows 11 and Wine): message 0x60 wparam/lparam delivered for a packet with a zero APC context, depending on the packet's
 * key (small, 32-bit, 64-bit pointer-like), for NtSetIoCompletion and for a wait completion packet.
 */
#include <windows.h>
#include <winternl.h>
#include <stdio.h>

typedef ULONG_PTR (WINAPI *init2_t)(HWND, ULONG *, ULONG_PTR, ULONG_PTR);
typedef LONG (WINAPI *setio_t)(HANDLE, ULONG_PTR, ULONG_PTR, LONG, ULONG_PTR);
typedef LONG (WINAPI *create_t)(HANDLE *, ACCESS_MASK, OBJECT_ATTRIBUTES *);
typedef LONG (WINAPI *assoc_t)(HANDLE, HANDLE, HANDLE, PVOID, PVOID, LONG, ULONG_PTR, BOOLEAN *);

static int count;
static ULONG_PTR last_wp, last_lp;
static LRESULT CALLBACK wndproc(HWND h, UINT m, WPARAM w, LPARAM l)
{
    if (m == 0x60) { count++; last_wp = w; last_lp = l; }
    return DefWindowProcW(h, m, w, l);
}

int main(void)
{
    HMODULE ntdll = GetModuleHandleA("ntdll.dll"), win32u = LoadLibraryA("win32u.dll");
    init2_t pInit2 = (void *)GetProcAddress(win32u, "NtUserInitThreadCoreMessagingIocp2");
    setio_t pSet = (void *)GetProcAddress(ntdll, "NtSetIoCompletion");
    create_t pCreate = (void *)GetProcAddress(ntdll, "NtCreateWaitCompletionPacket");
    assoc_t pAssoc = (void *)GetProcAddress(ntdll, "NtAssociateWaitCompletionPacket");
    WNDCLASSW wc = {0};
    HWND hwnd;
    ULONG out;
    HANDLE port, packet, ev = CreateEventW(NULL, TRUE, FALSE, NULL);
    OBJECT_ATTRIBUTES oa;
    static const ULONG_PTR keys[] = {0, 0x11, 0xabcd0000, 0x7fffffff, 0x80000000, 0xffffffff, 0x100000000ull, 0x1c9a6160520ull, 0x1c9a6160521ull, 0x7ff800000000ull, 0xffff000012345678ull, 0x40000002, 0x40000402};
    int i, k;

    LoadLibraryA("user32.dll");
    InitializeObjectAttributes(&oa, NULL, 0, NULL, NULL);
    wc.lpfnWndProc = wndproc; wc.hInstance = GetModuleHandleA(NULL); wc.lpszClassName = L"KeyProbe";
    RegisterClassW(&wc);
    hwnd = CreateWindowExW(0, L"KeyProbe", NULL, 0, 0, 0, 0, 0, HWND_MESSAGE, NULL, wc.hInstance, NULL);
    port = (HANDLE)pInit2(hwnd, &out, 0, 0);
    for (k = 0; k < 2; k++)
        for (i = 0; i < (int)(sizeof(keys) / sizeof(keys[0])); i++)
        {
            count = 0;
            if (k == 0) pSet(port, keys[i], 0, 0, 0);
            else { ResetEvent(ev); pCreate(&packet, 1, &oa); pAssoc(packet, port, ev, (PVOID)keys[i], (PVOID)0, 0, 0, NULL); SetEvent(ev); }
            MsgWaitForMultipleObjectsEx(0, NULL, 150, QS_ALLINPUT, 0);
            printf("%s key %#llx apc 0: delivered %d (wp %#Ix lp %#Ix)\n", k ? "wait packet  " : "SetIoCompletion", (unsigned long long)keys[i], count, count ? last_wp : 0, count ? last_lp : 0);
            if (k) CloseHandle(packet);
        }
    return 0;
}

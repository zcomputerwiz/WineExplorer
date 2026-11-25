/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright 2026 WineExplorer contributors
 *
 * Black-box probe (Windows 11 and Wine): when is the notification of PostMessageW(hwnd, 0x60, 5, 4) delivered, and does it make
 * QS_SENDMESSAGE appear? Variations: a lone post, then a PeekMessage that excludes 0x60, one that includes it, a GetMessage-less wait.
 */
#include <windows.h>
#include <stdio.h>

static const char *where = "?";
static LRESULT CALLBACK wndproc(HWND h, UINT m, WPARAM w, LPARAM l)
{
    if (m == 0x60) printf("    proc 0x60 wp %#Ix lp %#Ix [%s] InSendMessageEx %#lx\n", w, l, where, InSendMessageEx(NULL));
    return DefWindowProcW(h, m, w, l);
}

int main(void)
{
    WNDCLASSW wc = {0};
    HWND hwnd;
    MSG msg;
    BOOL r;
    DWORD qs;

    wc.lpfnWndProc = wndproc; wc.hInstance = GetModuleHandleA(NULL); wc.lpszClassName = L"Post2Probe";
    RegisterClassW(&wc);
    hwnd = CreateWindowExW(0, L"Post2Probe", NULL, 0, 0, 0, 0, 0, HWND_MESSAGE, NULL, wc.hInstance, NULL);
    while (PeekMessageW(&msg, NULL, 0, 0, PM_REMOVE)) DispatchMessageW(&msg);

    printf("== lone PostMessageW(0x60, 5, 4)\n");
    where = "PostMessageW";
    PostMessageW(hwnd, 0x60, 5, 4);
    where = "after";
    qs = GetQueueStatus(QS_ALLINPUT);
    printf("  GetQueueStatus %#lx (QS_SENDMESSAGE %s, QS_POSTMESSAGE %s)\n", qs, (qs >> 16) & QS_SENDMESSAGE ? "set" : "clear", (qs >> 16) & QS_POSTMESSAGE ? "set" : "clear");
    where = "PeekMessage(WM_USER..WM_USER+5, PM_REMOVE)";
    r = PeekMessageW(&msg, hwnd, WM_USER, WM_USER + 5, PM_REMOVE);
    printf("  excluding filter: returned %d\n", r);
    where = "PeekMessage(0x60..0x60, PM_NOREMOVE)";
    r = PeekMessageW(&msg, hwnd, 0x60, 0x60, PM_NOREMOVE);
    printf("  including filter, PM_NOREMOVE: returned %d (wp %#Ix)\n", r, r ? msg.wParam : 0);
    where = "PeekMessage(0x60..0x60, PM_REMOVE)";
    r = PeekMessageW(&msg, hwnd, 0x60, 0x60, PM_REMOVE);
    printf("  including filter, PM_REMOVE: returned %d (wp %#Ix)\n", r, r ? msg.wParam : 0);
    if (r) { where = "DispatchMessage"; DispatchMessageW(&msg); }
    where = "PeekMessage any";
    r = PeekMessageW(&msg, NULL, 0, 0, PM_REMOVE);
    printf("  any: returned %d\n", r);

    printf("== lone PostMessageW(0x60, 5, 4), then PeekMessageW(NULL hwnd, 0, 0, PM_REMOVE)\n");
    where = "PostMessageW"; PostMessageW(hwnd, 0x60, 5, 4);
    where = "PeekMessage any";
    r = PeekMessageW(&msg, NULL, 0, 0, PM_REMOVE);
    printf("  returned %d (wp %#Ix)\n", r, r ? msg.wParam : 0);
    if (r) { where = "DispatchMessage"; DispatchMessageW(&msg); }

    printf("== PostMessageW(0x60, 5, 4) to a window of another thread: skipped (see order probe)\n");
    DestroyWindow(hwnd);
    return 0;
}

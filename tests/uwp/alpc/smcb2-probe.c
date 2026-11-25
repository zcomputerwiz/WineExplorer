/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright 2026 WineExplorer contributors
 *
 * Black-box probe: after SendMessageCallbackW(hwnd, WM_USER+1, 5, 5, NULL, 1) to a window of this thread, which later user32
 * call makes the (deferred) message reach the window procedure first? Runs on Windows 11 and on Wine. */
#include <windows.h>
#include <stdio.h>

static int got;
static LRESULT CALLBACK wndproc(HWND h, UINT m, WPARAM w, LPARAM l)
{
    if (m == WM_USER + 1) got++;
    return DefWindowProcW(h, m, w, l);
}

static HWND mk(const WNDCLASSW *wc) { return CreateWindowExW(0, wc->lpszClassName, NULL, 0, 0, 0, 0, 0, HWND_MESSAGE, NULL, wc->hInstance, NULL); }
#define STEP(name, expr) do { expr; printf("  %-46s -> %s\n", name, got ? "DELIVERED" : "not yet"); if (got) goto done; } while (0)

int main(void)
{
    WNDCLASSW wc = {0};
    HWND hwnd, other;
    MSG msg;
    DWORD_PTR res;

    wc.lpfnWndProc = wndproc; wc.hInstance = GetModuleHandleA(NULL); wc.lpszClassName = L"Smcb2Probe";
    RegisterClassW(&wc);
    hwnd = mk(&wc);
    SendMessageCallbackW(hwnd, WM_USER + 1, 5, 5, NULL, 1);
    printf("after SendMessageCallbackW:\n");
    STEP("GetQueueStatus(QS_ALLINPUT)", GetQueueStatus(QS_ALLINPUT));
    STEP("SetTimer", SetTimer(hwnd, 1, 0x7fffffff, NULL));
    STEP("CreateWindowExW (message window)", other = mk(&wc));
    STEP("SendMessageW to another window of this thread", SendMessageW(other, WM_NULL, 0, 0));
    STEP("PostMessageW to self (not pumped)", PostMessageW(other, WM_USER + 5, 0, 0));
    STEP("InSendMessage / GetMessageTime", GetMessageTime());
    STEP("PeekMessageW(PM_NOREMOVE, WM_PAINT..WM_PAINT)", PeekMessageW(&msg, NULL, WM_PAINT, WM_PAINT, PM_NOREMOVE));
    STEP("PeekMessageW(PM_NOREMOVE, other filter)", PeekMessageW(&msg, other, WM_USER + 9, WM_USER + 9, PM_NOREMOVE));
    STEP("SendMessageTimeoutW to another window", SendMessageTimeoutW(other, WM_NULL, 0, 0, SMTO_NORMAL, 100, &res));
    STEP("WaitForSingleObject(0) (not a message wait)", WaitForSingleObject(GetCurrentThread(), 0));
    STEP("MsgWaitForMultipleObjects(0 timeout, QS_POSTMESSAGE)", MsgWaitForMultipleObjects(0, NULL, FALSE, 0, QS_POSTMESSAGE));
    STEP("MsgWaitForMultipleObjectsEx(0, QS_SENDMESSAGE, 0)", MsgWaitForMultipleObjectsEx(0, NULL, 0, QS_SENDMESSAGE, 0));
    STEP("MsgWaitForMultipleObjectsEx(MWMO_INPUTAVAILABLE)", MsgWaitForMultipleObjectsEx(0, NULL, 0, QS_ALLINPUT, MWMO_INPUTAVAILABLE));
    STEP("PeekMessageW(PM_NOREMOVE any)", PeekMessageW(&msg, NULL, 0, 0, PM_NOREMOVE));
    STEP("PeekMessageW(PM_REMOVE any)", PeekMessageW(&msg, NULL, 0, 0, PM_REMOVE));
    printf("  still not delivered at the end\n");
done:
    return 0;
}

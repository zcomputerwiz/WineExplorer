/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright 2026 WineExplorer contributors
 *
 * Black-box probe (Windows 11 and Wine): when user32 starts the Text Services Framework for a thread. Notepad's UI thread gets it
 * inside ShowWindow(main window, SW_SHOWDEFAULT): user32 calls imm32!CtfImmTIMActivate(hkl, 1, ...), msctf creates a
 * CicMarshalWndClass window and loads TextInputFramework.dll, and an "MSCTFIME UI" window follows. Each case runs on a new thread
 * and prints, after each step: the thread's windows (class names), whether msctf.dll and TextInputFramework.dll are loaded, the
 * default IME window, and whether msctf has a thread manager for the thread (TF_GetThreadMgr).
 */
#define COBJMACROS
#include <windows.h>
#include <imm.h>
#include <msctf.h>
#include <stdio.h>

static HRESULT (WINAPI *pTF_GetThreadMgr)(ITfThreadMgr **);
static BOOL (WINAPI *pCtfImmTIMActivate)(HKL, BOOL);

static BOOL CALLBACK enum_proc(HWND hwnd, LPARAM lp)
{
    WCHAR cls[64] = L"";
    GetClassNameW(hwnd, cls, 64);
    printf(" %ls%s", cls, IsWindowVisible(hwnd) ? "(v)" : "");
    return TRUE;
}

static void state(const char *step, HWND main)
{
    HMODULE msctf = GetModuleHandleW(L"msctf.dll");
    ITfThreadMgr *mgr = NULL;
    HRESULT hr = E_FAIL;

    if (msctf && !pTF_GetThreadMgr) pTF_GetThreadMgr = (void *)GetProcAddress(msctf, "TF_GetThreadMgr");
    if (pTF_GetThreadMgr) hr = pTF_GetThreadMgr(&mgr);
    printf("  %-28s msctf %d tif %d defime %d mgr %s  windows:", step, msctf != NULL,
           GetModuleHandleW(L"TextInputFramework.dll") != NULL, main ? ImmGetDefaultIMEWnd(main) != NULL : -1,
           !pTF_GetThreadMgr ? "n/a" : (SUCCEEDED(hr) && mgr) ? "yes" : "no");
    if (mgr) ITfThreadMgr_Release(mgr);
    EnumThreadWindows(GetCurrentThreadId(), enum_proc, 0);
    /* message-only windows are not enumerated by EnumThreadWindows */
    {
        HWND h = NULL;
        while ((h = FindWindowExW(HWND_MESSAGE, h, NULL, NULL)))
            if (GetWindowThreadProcessId(h, NULL) == GetCurrentThreadId()) { WCHAR c[64] = L""; GetClassNameW(h, c, 64); printf(" [msg]%ls", c); }
    }
    printf("\n");
}

static void pump(void)
{
    MSG msg;
    DWORD end = GetTickCount() + 150;
    while (GetTickCount() < end)
    {
        while (PeekMessageW(&msg, NULL, 0, 0, PM_REMOVE)) { TranslateMessage(&msg); DispatchMessageW(&msg); }
        Sleep(10);
    }
}

static HWND make(DWORD style, HWND parent)
{
    return CreateWindowExW(0, L"TsfProbe", L"tsf probe", style, 100, 100, 300, 200, parent, NULL, GetModuleHandleW(NULL), NULL);
}

static DWORD WINAPI run(void *arg)
{
    int c = (int)(INT_PTR)arg;
    HWND h = NULL;

    switch (c)
    {
    case 0:
        printf("A: overlapped window, created hidden, then SW_SHOWNOACTIVATE, then SetForegroundWindow, then a second show\n");
        state("start", NULL);
        h = make(WS_OVERLAPPEDWINDOW, NULL); pump(); state("created", h);
        ShowWindow(h, SW_SHOWNOACTIVATE); pump(); state("SW_SHOWNOACTIVATE", h);
        SetForegroundWindow(h); pump(); state("SetForegroundWindow", h);
        ShowWindow(h, SW_HIDE); ShowWindow(h, SW_SHOW); pump(); state("hide + SW_SHOW", h);
        break;
    case 1:
        printf("B: overlapped window, ShowWindow(SW_SHOWDEFAULT) (Notepad's call)\n");
        h = make(WS_OVERLAPPEDWINDOW, NULL); pump(); state("created", h);
        ShowWindow(h, SW_SHOWDEFAULT); pump(); state("SW_SHOWDEFAULT", h);
        break;
    case 2:
        printf("C: created with WS_VISIBLE\n");
        h = make(WS_OVERLAPPEDWINDOW | WS_VISIBLE, NULL); pump(); state("created visible", h);
        break;
    case 3:
        printf("D: message-only window only\n");
        h = make(0, HWND_MESSAGE); pump(); state("created", h);
        break;
    case 4:
        printf("E: ImmDisableTextFrameService(this thread) first, then SW_SHOWDEFAULT\n");
        ImmDisableTextFrameService(GetCurrentThreadId());
        h = make(WS_OVERLAPPEDWINDOW, NULL); pump(); state("created", h);
        ShowWindow(h, SW_SHOWDEFAULT); pump(); state("SW_SHOWDEFAULT", h);
        break;
    case 5:
        printf("F: CtfImmTIMActivate(hkl, TRUE) called directly on a thread with one hidden window\n");
        h = make(WS_OVERLAPPEDWINDOW, NULL); pump(); state("created", h);
        if (pCtfImmTIMActivate)
        {
            BOOL r = pCtfImmTIMActivate(GetKeyboardLayout(0), TRUE);
            pump(); printf("  CtfImmTIMActivate returned %d\n", r); state("after CtfImmTIMActivate", h);
        }
        else printf("  no CtfImmTIMActivate\n");
        break;
    case 7:
        printf("H: CtfImmTIMActivate(hkl, FALSE) called directly on a thread with one hidden window\n");
        h = make(WS_OVERLAPPEDWINDOW, NULL); pump(); state("created", h);
        if (pCtfImmTIMActivate)
        {
            ULONG_PTR before = 0, after = 0;
            BOOL r;
#ifdef __i386__
            __asm__ __volatile__( "movl %%esp, %0" : "=r" (before) );
#endif
            r = pCtfImmTIMActivate(GetKeyboardLayout(0), FALSE);
#ifdef __i386__
            __asm__ __volatile__( "movl %%esp, %0" : "=r" (after) );
#endif
            pump(); printf("  CtfImmTIMActivate returned %d, stack moved by %ld\n", r, (long)(after - before)); state("after CtfImmTIMActivate", h);
        }
        break;
    case 6:
        printf("G: hidden window, SetFocus on it (no show)\n");
        h = make(WS_OVERLAPPEDWINDOW, NULL); pump(); state("created", h);
        SetFocus(h); pump(); state("SetFocus", h);
        SetActiveWindow(h); pump(); state("SetActiveWindow", h);
        break;
    }
    if (h) DestroyWindow(h);
    return 0;
}

int main(int argc, char **argv)
{
    WNDCLASSW wc = {0};
    HMODULE imm = LoadLibraryW(L"imm32.dll");
    int c;

    setvbuf(stdout, NULL, _IONBF, 0);
    pCtfImmTIMActivate = (void *)GetProcAddress(imm, "CtfImmTIMActivate");
    wc.lpfnWndProc = DefWindowProcW; wc.hInstance = GetModuleHandleW(NULL); wc.lpszClassName = L"TsfProbe";
    wc.hCursor = LoadCursorW(NULL, (LPCWSTR)IDC_ARROW);
    RegisterClassW(&wc);
    printf("%d-bit\n", (int)sizeof(void *) * 8);
    for (c = 0; c <= 7; c++)
    {
        HANDLE th;
        if (argc > 1 && atoi(argv[1]) != c) continue;
        th = CreateThread(NULL, 0, run, (void *)(INT_PTR)c, 0, NULL);
        WaitForSingleObject(th, 10000);
        CloseHandle(th);
    }
    return 0;
}

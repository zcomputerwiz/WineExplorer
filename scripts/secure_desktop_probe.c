/*
 * Secure desktop research probe (docs/ELEVATION_SECURE_DESKTOP_WORKORDER.md, phase R): what a process on
 * WinSta0\Default can do to a window on a second desktop (WinSta0\Winlogon) that is the input desktop.
 * The runner is scripts/secure_desktop_probe.sh (X11 on Xvfb, virtual desktop); the Wayland run uses the same
 * program with a headless compositor.  Later phases reuse it as the acceptance probe.
 *
 * Modes:
 *   main <dir> [sys]   orchestrator on Default: creates desktops, starts target and attackers, switches, reports
 *   target <dir>       window owner on the second desktop (logs what reaches it to <dir>\target.log)
 *   attacker <dir> <hwnd> <tid> <button> <tag>   the operation battery, run from Default ("limited" also
 *                      impersonates a restricted token for the access checks)
 *   switcher <dir>     opens Winlogon, switches to it and sleeps (killed by main: does the input desktop return?)
 *   tmpswitch <dir>    creates a private desktop, switches to it and sleeps
 *   where <dir> <name> writes the name of its own thread desktop
 *   query [<dir>]      prints the name of the input desktop
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright 2026 WineExplorer Desktop Environment Project
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */
#define _WIN32_WINNT 0x0600
#include <windows.h>
#include <sddl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char dir[MAX_PATH];
static FILE *out;
static volatile LONG kb_ll, mouse_ll, hk_getmsg, hk_cwp, hk_kb;

static void say(const char *fmt, ...)
{
    va_list ap; char buf[512];
    va_start(ap, fmt); vsnprintf(buf, sizeof(buf), fmt, ap); va_end(ap);
    fprintf(out, "%s\n", buf); fflush(out);
}

static void path(char *buf, const char *name) { snprintf(buf, MAX_PATH, "%s\\%s", dir, name); }
static int exists(const char *name) { char p[MAX_PATH]; path(p, name); return GetFileAttributesA(p) != INVALID_FILE_ATTRIBUTES; }
static void touch(const char *name) { char p[MAX_PATH]; FILE *f; path(p, name); f = fopen(p, "w"); if (f) fclose(f); }
static void pump(DWORD ms)
{
    DWORD end = GetTickCount() + ms; MSG m;
    while ((long)(end - GetTickCount()) > 0)
    {
        while (PeekMessageW(&m, 0, 0, 0, PM_REMOVE)) { TranslateMessage(&m); DispatchMessageW(&m); }
        Sleep(10);
    }
}
static int wait_file(const char *name, DWORD ms)
{
    DWORD end = GetTickCount() + ms;
    while ((long)(end - GetTickCount()) > 0) { if (exists(name)) return 1; pump(50); }
    return 0;
}
/* host sync: write <name>.at and wait for <name>.go */
static void sync_point(const char *name)
{
    char a[64], g[64];
    snprintf(a, sizeof(a), "%s.at", name); snprintf(g, sizeof(g), "%s.go", name);
    touch(a);
    if (!wait_file(g, 60000)) say("SYNC-TIMEOUT %s", name);
}

static void input_desktop_name(char *buf, int len)
{
    HDESK d; DWORD n;
    SetLastError(0);
    d = OpenInputDesktop(0, FALSE, DESKTOP_READOBJECTS);
    strcpy(buf, "?");
    if (!d) { snprintf(buf, len, "(OpenInputDesktop returned NULL, err %lu)", GetLastError()); return; }
    GetUserObjectInformationA(d, UOI_NAME, buf, len, &n);
    CloseHandle(d);  /* CloseDesktop */
}
static void thread_desktop_name(char *buf, int len)
{
    DWORD n; strcpy(buf, "?");
    GetUserObjectInformationA(GetThreadDesktop(GetCurrentThreadId()), UOI_NAME, buf, len, &n);
}

/* ---------------- target ---------------- */
static FILE *tlog;
static LRESULT CALLBACK tproc(HWND h, UINT m, WPARAM w, LPARAM l)
{
    switch (m)
    {
    case WM_COMMAND: fprintf(tlog, "WM_COMMAND id=%u code=%u\n", LOWORD(w), HIWORD(w)); fflush(tlog); break;
    case WM_KEYDOWN: fprintf(tlog, "WM_KEYDOWN vk=%#x\n", (unsigned)w); fflush(tlog); break;
    case WM_CHAR: fprintf(tlog, "WM_CHAR %#x\n", (unsigned)w); fflush(tlog); break;
    case WM_LBUTTONDOWN: fprintf(tlog, "WM_LBUTTONDOWN %d,%d\n", (short)LOWORD(l), (short)HIWORD(l)); fflush(tlog); break;
    case WM_USER + 1: case WM_USER + 2: fprintf(tlog, "WM_USER+%u\n", m - WM_USER); fflush(tlog); return 77;
    case WM_DESTROY: PostQuitMessage(0); break;
    }
    return DefWindowProcW(h, m, w, l);
}
static WNDPROC btn_orig;
static LRESULT CALLBACK btnproc(HWND h, UINT m, WPARAM w, LPARAM l)
{
    if (m == WM_KEYDOWN) { fprintf(tlog, "BUTTON WM_KEYDOWN vk=%#x\n", (unsigned)w); fflush(tlog); }
    if (m == WM_LBUTTONDOWN) { fprintf(tlog, "BUTTON WM_LBUTTONDOWN %d,%d\n", (short)LOWORD(l), (short)HIWORD(l)); fflush(tlog); }
    return CallWindowProcW(btn_orig, h, m, w, l);
}
static int run_target(void)
{
    WNDCLASSW wc = {0}; HWND win, btn, edit; MSG msg; char p[MAX_PATH]; FILE *f; char dn[64];
    path(p, "target.log"); tlog = fopen(p, "w");
    wc.lpfnWndProc = tproc; wc.hInstance = GetModuleHandleW(0); wc.lpszClassName = L"SDTarget";
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    RegisterClassW(&wc);
    win = CreateWindowExW(0, L"SDTarget", L"SECURE-TARGET", WS_OVERLAPPEDWINDOW | WS_VISIBLE, 100, 100, 400, 300, 0, 0, wc.hInstance, 0);
    btn = CreateWindowExW(0, L"BUTTON", L"Approve", WS_CHILD | WS_VISIBLE, 20, 20, 120, 40, win, (HMENU)1001, wc.hInstance, 0);
    edit = CreateWindowExW(0, L"EDIT", L"orig", WS_CHILD | WS_VISIBLE | WS_BORDER, 20, 80, 200, 24, win, (HMENU)1002, wc.hInstance, 0);
    (void)edit;
    btn_orig = (WNDPROC)SetWindowLongPtrW(btn, GWLP_WNDPROC, (LONG_PTR)btnproc);
    { RECT dr; GetWindowRect(GetDesktopWindow(), &dr);
      fprintf(tlog, "METRICS screen %dx%d desktop-window %ld,%ld-%ld,%ld fg-is-self=%d\n", GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN), dr.left, dr.top, dr.right, dr.bottom, 0); fflush(tlog); }
    SetFocus(win); SetForegroundWindow(win);
    thread_desktop_name(dn, sizeof(dn));
    path(p, "target.info"); f = fopen(p, "w");
    fprintf(f, "%llu %lu %llu %s %lu\n", (unsigned long long)(ULONG_PTR)win, GetCurrentThreadId(), (unsigned long long)(ULONG_PTR)btn, dn, GetCurrentProcessId());
    fclose(f);
    while (GetMessageW(&msg, 0, 0, 0) > 0) { TranslateMessage(&msg); DispatchMessageW(&msg); }
    return 0;
}

/* ---------------- hooks (attacker) ---------------- */
static void hooklog(const char *n)
{
    char p[MAX_PATH]; FILE *f;
    snprintf(p, sizeof(p), "%s\\hooks.log", dir);
    if (!dir[0]) { GetEnvironmentVariableA("SDDIR", dir, sizeof(dir)); snprintf(p, sizeof(p), "%s\\hooks.log", dir); }
    f = fopen(p, "a"); if (f) { fprintf(f, "HOOK %s ran in pid %lu\n", n, GetCurrentProcessId()); fclose(f); }
}
static LRESULT CALLBACK ll_kb(int c, WPARAM w, LPARAM l) { if (c == HC_ACTION) { InterlockedIncrement(&kb_ll); hooklog("KEYBOARD_LL"); } return CallNextHookEx(0, c, w, l); }
static LRESULT CALLBACK ll_ms(int c, WPARAM w, LPARAM l) { if (c == HC_ACTION) { InterlockedIncrement(&mouse_ll); hooklog("MOUSE_LL"); } return CallNextHookEx(0, c, w, l); }
static LRESULT CALLBACK h_gm(int c, WPARAM w, LPARAM l) { InterlockedIncrement(&hk_getmsg); hooklog("GETMESSAGE"); return CallNextHookEx(0, c, w, l); }
static LRESULT CALLBACK h_cw(int c, WPARAM w, LPARAM l) { InterlockedIncrement(&hk_cwp); hooklog("CALLWNDPROC"); return CallNextHookEx(0, c, w, l); }
static LRESULT CALLBACK hg_gm(int c, WPARAM w, LPARAM l) { hooklog("GLOBAL-GETMESSAGE"); return CallNextHookEx(0, c, w, l); }
static LRESULT CALLBACK hg_cw(int c, WPARAM w, LPARAM l) { hooklog("GLOBAL-CALLWNDPROC"); return CallNextHookEx(0, c, w, l); }
static LRESULT CALLBACK h_kb(int c, WPARAM w, LPARAM l) { InterlockedIncrement(&hk_kb); hooklog("KEYBOARD"); return CallNextHookEx(0, c, w, l); }

static int ecount;
static HWND efound;
static HWND etarget;
static BOOL CALLBACK enum_cb2(HWND h, LPARAM l)
{
    WCHAR cls[64]; GetClassNameW(h, cls, 64);
    ecount++;
    if (h == etarget || !wcscmp(cls, L"SDTarget")) efound = h;
    return TRUE;
}

static void ind(const char *where)
{
    char b[64]; input_desktop_name(b, sizeof(b)); say("  [input desktop %s: %s]", where, b);
}

static void stat(const char *op, const char *what, BOOL ok)
{
    say("OP %-34s %-8s err=%lu  %s", op, ok ? "ok" : "FAIL", ok ? 0 : GetLastError(), what);
}

static DWORD WINAPI td_thread(LPVOID unused)
{
    HDESK dw = OpenDesktopA("Winlogon", 0, FALSE, DESKTOP_CREATEWINDOW | DESKTOP_HOOKCONTROL | DESKTOP_JOURNALRECORD | DESKTOP_WRITEOBJECTS);
    BOOL ok;
    stat("OpenDesktop(Winlogon, CREATEWINDOW..)", "", dw != 0);
    if (dw)
    {
        ok = SetThreadDesktop(dw);
        stat("SetThreadDesktop(Winlogon) fresh thread", "", ok);
        if (ok)
        {
            HWND w = CreateWindowExW(0, L"STATIC", L"ATTACKER-ON-SECURE", WS_POPUP | WS_VISIBLE, 0, 0, 50, 50, 0, 0, 0, 0);
            stat("CreateWindow on Winlogon", "", w != 0);
            if (w) DestroyWindow(w);
        }
    }
    return 0;
}

static BOOL start_proc(const char *cmd, const char *desk, HANDLE tok, PROCESS_INFORMATION *pi);
static int run_where(const char *outname)
{
    char p[MAX_PATH], dn[64]; FILE *f;
    thread_desktop_name(dn, sizeof(dn));
    snprintf(p, sizeof(p), "%s\\%s", dir, outname); f = fopen(p, "w"); fprintf(f, "%s\n", dn); fclose(f);
    return 0;
}

static void sd_tests(const char *tag)
{
    static const char *names[] = { "SecSys", "SecAdm", "SecDenyAll" };
    int i; HDESK h;
    for (i = 0; i < 3; i++)
    {
        PROCESS_INFORMATION pi; char cmd[1024], exe[MAX_PATH], desk[64], of[64], p[MAX_PATH], line[64] = "(no output)"; FILE *f;
        h = OpenDesktopA(names[i], 0, FALSE, DESKTOP_SWITCHDESKTOP);
        say("SD %-10s OpenDesktop(SWITCHDESKTOP): %s err=%lu", names[i], h ? "granted" : "DENIED", h ? 0 : GetLastError());
        if (h) CloseDesktop(h);
        h = OpenDesktopA(names[i], 0, FALSE, DESKTOP_ENUMERATE);
        say("SD %-10s OpenDesktop(ENUMERATE): %s err=%lu", names[i], h ? "granted" : "DENIED", h ? 0 : GetLastError());
        if (h) CloseDesktop(h);
        h = OpenDesktopA(names[i], 0, FALSE, DESKTOP_CREATEWINDOW);
        say("SD %-10s OpenDesktop(CREATEWINDOW): %s err=%lu", names[i], h ? "granted" : "DENIED", h ? 0 : GetLastError());
        if (h) CloseDesktop(h);
        GetModuleFileNameA(0, exe, MAX_PATH);
        snprintf(of, sizeof(of), "where.%s.%s", tag, names[i]);
        snprintf(cmd, sizeof(cmd), "\"%s\" where %s %s", exe, dir, of);
        snprintf(desk, sizeof(desk), "WinSta0\\%s", names[i]);
        if (start_proc(cmd, desk, NULL, &pi))
        {
            WaitForSingleObject(pi.hProcess, 10000);
            snprintf(p, sizeof(p), "%s\\%s", dir, of); f = fopen(p, "r"); if (f) { fgets(line, sizeof(line), f); fclose(f); }
            say("SD %-10s child started with lpDesktop=%s: thread desktop = %s", names[i], desk, strtok(line, "\n"));
        }
        else say("SD %-10s CreateProcess(lpDesktop=%s) failed err=%lu", names[i], desk, GetLastError());
    }
}

static int run_attacker(HWND target, DWORD ttid, HWND btn, const char *tag)
{
    char p[MAX_PATH], dn[64];
    HHOOK h; BOOL ok; UINT n; INPUT in[2]; DWORD_PTR res; HDESK dw, dd, di;
    HWND r;
    WCHAR buf[64]; HDESK orig;

    snprintf(p, sizeof(p), "%s\\attacker.%s.log", dir, tag);
    out = fopen(p, "w");
    {
        char tn[64], in2[64]; HANDLE tk; DWORD len; BOOL restricted = FALSE; TOKEN_ELEVATION el = {0};
        thread_desktop_name(tn, sizeof(tn)); input_desktop_name(in2, sizeof(in2));
        OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &tk);
        restricted = IsTokenRestricted(tk);
        GetTokenInformation(tk, TokenElevation, &el, sizeof(el), &len);
        say("ATTACKER %s on desktop %s; input desktop is %s; token restricted=%d elevated=%lu", tag, tn, in2, restricted, el.TokenIsElevated);
        CloseHandle(tk);
    }
    orig = GetThreadDesktop(GetCurrentThreadId());
    if (!strcmp(tag, "limited"))
    {
        HANDLE tk, rt = NULL, it = NULL; BYTE sb[SECURITY_MAX_SID_SIZE]; DWORD sl = sizeof(sb); SID_AND_ATTRIBUTES rs = {0};
        BYTE gb[4096]; DWORD gl, i, deny = 0; TOKEN_GROUPS *g = (TOKEN_GROUPS *)gb; BYTE pb[4096]; HANDLE cur;
        CreateWellKnownSid(WinWorldSid, NULL, sb, &sl); rs.Sid = sb;
        OpenProcessToken(GetCurrentProcess(), TOKEN_ALL_ACCESS, &tk);
        if (CreateRestrictedToken(tk, DISABLE_MAX_PRIVILEGE | LUA_TOKEN, 0, NULL, 0, NULL, 1, &rs, &rt) &&
            DuplicateTokenEx(rt, TOKEN_ALL_ACCESS, NULL, SecurityImpersonation, TokenImpersonation, &it) && SetThreadToken(NULL, it))
        {
            OpenThreadToken(GetCurrentThread(), TOKEN_QUERY, TRUE, &cur);
            GetTokenInformation(cur, TokenGroups, gb, sizeof(gb), &gl);
            for (i = 0; i < g->GroupCount; i++) if (g->Groups[i].Attributes & SE_GROUP_USE_FOR_DENY_ONLY) deny++;
            GetTokenInformation(cur, TokenPrivileges, pb, sizeof(pb), &gl);
            say("LIMITED thread impersonates a restricted token: restricted=%d deny-only groups=%lu privileges=%lu", IsTokenRestricted(cur), deny, ((TOKEN_PRIVILEGES *)pb)->PrivilegeCount);
        }
        else say("LIMITED: could not impersonate a restricted token err=%lu", GetLastError());
    }
    SetLastError(0);

    /* window visibility across desktops */
    SetLastError(0); say("IsWindow(target)=%d", IsWindow(target));
    SetLastError(0); r = FindWindowW(L"SDTarget", NULL); stat("FindWindow(SDTarget)", r ? "found" : "not found", r != 0);
    ecount = 0; efound = 0; etarget = target;
    SetLastError(0); EnumWindows(enum_cb2, 0);
    say("OP EnumWindows                       count=%d target-found=%d", ecount, efound != 0);
    SetLastError(0); say("GetWindowTextW(target) len=%d", GetWindowTextW(target, buf, 64));
    { RECT rc; ok = GetWindowRect(target, &rc); stat("GetWindowRect(target)", ok ? "rect readable" : "", ok); }
    dw = OpenDesktopA("Winlogon", 0, FALSE, DESKTOP_ENUMERATE | DESKTOP_READOBJECTS);
    stat("OpenDesktop(Winlogon, ENUM|READ)", "", dw != 0);
    if (dw)
    {
        ecount = 0; efound = 0;
        SetLastError(0); EnumDesktopWindows(dw, enum_cb2, 0);
        say("OP EnumDesktopWindows(Winlogon)      count=%d target-found=%d", ecount, efound != 0);
        CloseDesktop(dw);
    }
    dd = OpenDesktopA("Winlogon", 0, FALSE, DESKTOP_SWITCHDESKTOP);
    stat("OpenDesktop(Winlogon, SWITCHDESKTOP)", "", dd != 0);
    /* messages */
    ind("before messages");
    SetLastError(0); ok = PostMessageW(btn, BM_CLICK, 0, 0); stat("PostMessage(BM_CLICK)", "", ok);
    SetLastError(0); ok = PostMessageW(target, WM_COMMAND, MAKEWPARAM(1001, BN_CLICKED), (LPARAM)btn); stat("PostMessage(WM_COMMAND)", "", ok);
    SetLastError(0); res = 0; ok = SendMessageTimeoutW(target, WM_COMMAND, MAKEWPARAM(1001, BN_CLICKED), (LPARAM)btn, SMTO_ABORTIFHUNG, 2000, &res);
    stat("SendMessageTimeout(WM_COMMAND)", "", ok);
    SetLastError(0); res = 0; ok = SendMessageTimeoutW(btn, BM_CLICK, 0, 0, SMTO_ABORTIFHUNG, 2000, &res); stat("SendMessageTimeout(BM_CLICK)", "", ok);
    SetLastError(0); res = 0; ok = SendMessageTimeoutW(target, WM_USER + 1, 0, 0, SMTO_ABORTIFHUNG, 2000, &res);
    say("OP SendMessageTimeout(WM_USER+1)         %s err=%lu result=%lld", ok ? "ok" : "FAIL", ok ? 0 : GetLastError(), (long long)res);
    SetLastError(0); ok = PostThreadMessageW(ttid, WM_USER + 2, 0, 0); stat("PostThreadMessage(WM_USER+2)", "", ok);
    SetLastError(0); ok = (BOOL)SendMessageW(GetDlgItem(target, 1002), WM_SETTEXT, 0, (LPARAM)L"HIJACKED"); stat("SendMessage(edit WM_SETTEXT)", "", ok);
    { WCHAR t[32] = L""; SetLastError(0); GetWindowTextW(GetDlgItem(target, 1002), t, 32); say("OP GetWindowText(edit)               %ls", t); }

    /* input synthesis */
    ind("before SendInput");
    memset(in, 0, sizeof(in));
    in[0].type = INPUT_KEYBOARD; in[0].ki.wVk = 'A';
    in[1].type = INPUT_KEYBOARD; in[1].ki.wVk = 'A'; in[1].ki.dwFlags = KEYEVENTF_KEYUP;
    SetLastError(0); n = SendInput(2, in, sizeof(INPUT)); say("OP SendInput(kbd A)                   returned %u err=%lu", n, n ? 0 : GetLastError());
    { RECT rc; GetWindowRect(btn, &rc);
      memset(in, 0, sizeof(in));
      in[0].type = INPUT_MOUSE; in[0].mi.dx = (rc.left + 20) * 65535 / GetSystemMetrics(SM_CXSCREEN); in[0].mi.dy = (rc.top + 10) * 65535 / GetSystemMetrics(SM_CYSCREEN);
      in[0].mi.dwFlags = MOUSEEVENTF_MOVE | MOUSEEVENTF_ABSOLUTE;
      in[1].type = INPUT_MOUSE; in[1].mi.dwFlags = MOUSEEVENTF_LEFTDOWN;
      SetLastError(0); n = SendInput(2, in, sizeof(INPUT)); say("OP SendInput(mouse move+ldown)         returned %u err=%lu", n, n ? 0 : GetLastError());
      in[0].mi.dwFlags = MOUSEEVENTF_LEFTUP; SetLastError(0); n = SendInput(1, in, sizeof(INPUT)); say("OP SendInput(mouse lup)               returned %u err=%lu", n, n ? 0 : GetLastError());
    }
    keybd_event('B', 0, 0, 0); keybd_event('B', 0, KEYEVENTF_KEYUP, 0); say("OP keybd_event(B)                    void (see target log)");
    mouse_event(MOUSEEVENTF_LEFTDOWN, 0, 0, 0, 0); mouse_event(MOUSEEVENTF_LEFTUP, 0, 0, 0, 0); say("OP mouse_event(click)                 void (see target log)");


    /* host-device input path exported by win32u (what the drivers use): hardware origin, so no cross-desktop check as SendInput has */
    {
        typedef ULONG_PTR (WINAPI *callhwnd_t)(HWND, DWORD_PTR, DWORD);
        struct { UINT flags; const INPUT *input; LPARAM lparam; } hp;
        callhwnd_t fn = (callhwnd_t)GetProcAddress(GetModuleHandleA("win32u.dll"), "NtUserCallHwndParam");
        INPUT hi[1]; RECT rc; ULONG_PTR r1, r2, r3, r4;
        ind("before win32u hardware path");
        if (!fn) say("OP win32u NtUserCallHwndParam(SendHardwareInput)  export not found");
        else
        {
            DWORD wpid = 0; ULONG_PTR wtid = fn(target, (DWORD_PTR)&wpid, 17);
            say("  (dispatch check: NtUserCallHwndParam code 17 GetWindowThread -> tid %lu pid %lu, expected tid %lu)", (unsigned long)wtid, wpid, ttid);
            memset(hi, 0, sizeof(hi)); hi[0].type = INPUT_KEYBOARD; hi[0].ki.wVk = 'W'; hi[0].ki.wScan = 0x11; hp.flags = 0; hp.input = hi; hp.lparam = 0;
            r1 = fn(target, (DWORD_PTR)&hp, 26);
            hi[0].ki.dwFlags = KEYEVENTF_KEYUP; r2 = fn(target, (DWORD_PTR)&hp, 26);
            GetWindowRect(btn, &rc);
            memset(hi, 0, sizeof(hi)); hi[0].type = INPUT_MOUSE; hi[0].mi.dx = rc.left + 20; hi[0].mi.dy = rc.top + 10;
            hi[0].mi.dwFlags = MOUSEEVENTF_MOVE | MOUSEEVENTF_ABSOLUTE | MOUSEEVENTF_LEFTDOWN; r3 = fn(target, (DWORD_PTR)&hp, 26);
            hi[0].mi.dwFlags = MOUSEEVENTF_ABSOLUTE | MOUSEEVENTF_LEFTUP; r4 = fn(target, (DWORD_PTR)&hp, 26);
            say("OP win32u NtUserCallHwndParam(SendHardwareInput) ret kbd %lu/%lu mouse %lu/%lu (see target log: WM_KEYDOWN 0x57 / WM_LBUTTONDOWN 'W' sent by hardware path)", (unsigned long)r1, (unsigned long)r2, (unsigned long)r3, (unsigned long)r4);
            memset(hi, 0, sizeof(hi)); hi[0].type = INPUT_KEYBOARD; hi[0].ki.wVk = 'V'; hi[0].ki.wScan = 0x2f;
            r1 = fn(NULL, (DWORD_PTR)&hp, 26); hi[0].ki.dwFlags = KEYEVENTF_KEYUP; r2 = fn(NULL, (DWORD_PTR)&hp, 26);
            say("OP win32u SendHardwareInput hwnd=NULL kbd V       ret %lu/%lu (target log WM_KEYDOWN vk=0x56 means: keys reach the input desktop's focus window without naming it)", (unsigned long)r1, (unsigned long)r2);
            pump(300);
            ind("after win32u hardware path");
        }
    }

    /* hooks */
    ind("before hooks");
    h = SetWindowsHookExW(WH_KEYBOARD_LL, ll_kb, GetModuleHandleW(0), 0); stat("SetWindowsHookEx(WH_KEYBOARD_LL)", "", h != 0);
    h = SetWindowsHookExW(WH_MOUSE_LL, ll_ms, GetModuleHandleW(0), 0); stat("SetWindowsHookEx(WH_MOUSE_LL)", "", h != 0);
    SetLastError(0); h = SetWindowsHookExW(WH_GETMESSAGE, h_gm, GetModuleHandleW(0), ttid); stat("SetWindowsHookEx(GETMESSAGE,tid)", "", h != 0);
    SetLastError(0); h = SetWindowsHookExW(WH_CALLWNDPROC, h_cw, GetModuleHandleW(0), ttid); stat("SetWindowsHookEx(CALLWNDPROC,tid)", "", h != 0);
    SetLastError(0); h = SetWindowsHookExW(WH_KEYBOARD, h_kb, GetModuleHandleW(0), ttid); stat("SetWindowsHookEx(KEYBOARD,tid)", "", h != 0);
    SetLastError(0); h = SetWindowsHookExW(WH_GETMESSAGE, hg_gm, GetModuleHandleW(0), 0); stat("SetWindowsHookEx(GETMESSAGE,global)", "", h != 0);
    SetLastError(0); h = SetWindowsHookExW(WH_CALLWNDPROC, hg_cw, GetModuleHandleW(0), 0); stat("SetWindowsHookEx(CALLWNDPROC,global)", "", h != 0);
    SetLastError(0); h = SetWindowsHookExW(WH_JOURNALRECORD, (HOOKPROC)ll_kb, GetModuleHandleW(0), 0); stat("SetWindowsHookEx(JOURNALRECORD)", "", h != 0);

    /* thread input */
    ind("before AttachThreadInput");
    SetLastError(0); ok = AttachThreadInput(GetCurrentThreadId(), ttid, TRUE); stat("AttachThreadInput(me,target)", "", ok);
    if (ok) AttachThreadInput(GetCurrentThreadId(), ttid, FALSE);

    /* focus */
    { HWND fg0 = GetForegroundWindow();
      SetLastError(0); ok = SetForegroundWindow(target); stat("SetForegroundWindow(target)", "", ok);
      say("OP GetForegroundWindow before/after   %p -> %p (target %p)", fg0, GetForegroundWindow(), target);
      SetLastError(0); r = SetFocus(target); say("OP SetFocus(target) (cross-thread)     ret=%p err=%lu", r, GetLastError());
      SetLastError(0); r = SetActiveWindow(target); say("OP SetActiveWindow(target)             ret=%p err=%lu", r, GetLastError());
      SetLastError(0); ok = BringWindowToTop(target); stat("BringWindowToTop(target)", "", ok);
      SetLastError(0); ok = SetWindowPos(target, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE); stat("SetWindowPos(target,TOPMOST)", "", ok);
      SetLastError(0); ok = ShowWindow(target, SW_HIDE); say("OP ShowWindow(target,HIDE)              prev-visible=%d err=%lu", ok, GetLastError());
      ShowWindow(target, SW_SHOW);
      SetLastError(0); ok = DestroyWindow(target); stat("DestroyWindow(target) (other thread)", "", ok);
    }

    /* switch back by this (possibly limited) process */
    di = OpenDesktopA("Default", 0, FALSE, DESKTOP_SWITCHDESKTOP);
    stat("OpenDesktop(Default, SWITCHDESKTOP)", "", di != 0);
    dw = OpenDesktopA("Winlogon", 0, FALSE, DESKTOP_READOBJECTS);
    if (dw)
    {
        input_desktop_name(dn, sizeof(dn)); say("input desktop before: %s", dn);
        SetLastError(0); ok = SwitchDesktop(dw);
        stat("SwitchDesktop(Winlogon, READOBJECTS only)", "(handle without SWITCHDESKTOP right)", ok);
        input_desktop_name(dn, sizeof(dn)); say("input desktop now: %s", dn);
    }
    /* the one that proves that a limited process can take the input back */
    if (di)
    {
        SetLastError(0); ok = SwitchDesktop(di); stat("SwitchDesktop(Default) back", "", ok);
        input_desktop_name(dn, sizeof(dn)); say("input desktop now: %s", dn);
        SwitchDesktop(dd);  /* put it back so the host input phase still hits the secure desktop */
        input_desktop_name(dn, sizeof(dn)); say("input desktop restored to: %s", dn);
    }

    WaitForSingleObject(CreateThread(NULL, 0, td_thread, NULL, 0, NULL), 10000);
    if (!getenv("NOSEC")) sd_tests(tag);
    pump(100);

    /* wait for host-injected input, report hook counters */
    {
        char rn[64], dn2[64];
        snprintf(rn, sizeof(rn), "att.%s.ready", tag); snprintf(dn2, sizeof(dn2), "att.%s.hostdone", tag);
        touch(rn);
        {
            DWORD end = GetTickCount() + 40000; int qseen = 0, moved = 0, zseen = 0; POINT p0, p1; MSG m;
            GetCursorPos(&p0);
            while (!exists(dn2) && (long)(end - GetTickCount()) > 0)
            {
                while (PeekMessageW(&m, 0, 0, 0, PM_REMOVE)) DispatchMessageW(&m);
                if (GetAsyncKeyState('Q') & 0x8001) qseen++;
                if (GetAsyncKeyState('Z') & 0x8001) zseen++;
                GetCursorPos(&p1); if (p1.x != p0.x || p1.y != p0.y) { moved++; p0 = p1; }
                Sleep(2);
            }
            say("SNIFF GetAsyncKeyState(Q) hits=%d (Z %d), GetCursorPos changed %d times, last (%ld,%ld); host pointer ended at (20,20) on the secure desktop", qseen, zseen, moved, p1.x, p1.y);
        }
    }
    ind("at the end");
    say("HOOKCOUNT kb_ll=%ld mouse_ll=%ld getmsg=%ld callwnd=%ld kb=%ld", kb_ll, mouse_ll, hk_getmsg, hk_cwp, hk_kb);
    fclose(out);
    return 0;
}

/* ---------------- switcher ---------------- */
static int run_switcher(void)
{
    char p[MAX_PATH]; HDESK d = OpenDesktopA("Winlogon", 0, FALSE, DESKTOP_SWITCHDESKTOP); BOOL ok;
    out = stdout;
    ok = d ? SwitchDesktop(d) : FALSE;
    path(p, "switcher.info");
    { FILE *f = fopen(p, "w"); fprintf(f, "%d %lu\n", ok, GetLastError()); fclose(f); }
    Sleep(600000);
    return 0;
}

static int run_tmpswitch(void)
{
    char p[MAX_PATH]; HDESK d = CreateDesktopA("TmpSwitch", NULL, NULL, 0, GENERIC_ALL, NULL); BOOL ok = d ? SwitchDesktop(d) : FALSE; FILE *f;
    path(p, "tmpswitch.info"); f = fopen(p, "w"); fprintf(f, "%d %lu\n", ok, GetLastError()); fclose(f);
    Sleep(600000);
    return 0;
}

/* ---------------- main ---------------- */
static HWND defwin;
static LRESULT CALLBACK defproc(HWND h, UINT m, WPARAM w, LPARAM l)
{
    if (m == WM_LBUTTONDOWN || m == WM_KEYDOWN)
    {
        char p[MAX_PATH]; FILE *f; path(p, "defwin.log"); f = fopen(p, "a");
        if (f) { fprintf(f, "DEFWIN msg %#x\n", m); fclose(f); }
    }
    return DefWindowProcW(h, m, w, l);
}

static BOOL start_proc(const char *cmd, const char *desk, HANDLE tok, PROCESS_INFORMATION *pi)
{
    STARTUPINFOA si = {0}; char c[1024]; BOOL ok;
    si.cb = sizeof(si); si.lpDesktop = (char *)desk;
    strcpy(c, cmd);
    if (tok) ok = CreateProcessAsUserA(tok, NULL, c, NULL, NULL, FALSE, 0, NULL, NULL, &si, pi);
    else ok = CreateProcessA(NULL, c, NULL, NULL, FALSE, 0, NULL, NULL, &si, pi);
    return ok;
}

static int run_main(int sys)
{
    char p[MAX_PATH], exe[MAX_PATH], cmd[1024], dn[64];
    HDESK secure, dflt, sec2; SECURITY_ATTRIBUTES sa = {sizeof(sa)}; PSECURITY_DESCRIPTOR sd = NULL;
    PROCESS_INFORMATION tpi, api, lpi, spi; HWND target; DWORD ttid; HWND btn;
    unsigned long long a, b, c; char tdn[64]; FILE *f; BOOL ok;
    WNDCLASSW wc = {0};

    path(p, "main.log"); out = fopen(p, "w");
    GetModuleFileNameA(0, exe, MAX_PATH);
    thread_desktop_name(dn, sizeof(dn));
    say("MAIN thread desktop %s, screen %dx%d", dn, GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN));

    wc.lpfnWndProc = defproc; wc.hInstance = GetModuleHandleW(0); wc.lpszClassName = L"SDDefault";
    wc.hbrBackground = (HBRUSH)(COLOR_HIGHLIGHT + 1);
    RegisterClassW(&wc);
    defwin = CreateWindowExW(0, L"SDDefault", L"DEFAULT-DESKTOP-WINDOW", WS_OVERLAPPEDWINDOW | WS_VISIBLE, 600, 100, 300, 200, 0, 0, wc.hInstance, 0);

    /* descriptor honored? */
    if (sys)
    {
        if (!ConvertStringSecurityDescriptorToSecurityDescriptorA("D:(A;;GA;;;SY)(A;;0x0006;;;BA)", SDDL_REVISION_1, &sd, NULL))
            say("SDDL conversion failed %lu", GetLastError());
        sa.lpSecurityDescriptor = sd;
    }
    secure = CreateDesktopA("Winlogon", NULL, NULL, 0, GENERIC_ALL, sys ? &sa : NULL);
    say("CreateDesktop(Winlogon, sd=%s) -> %p err=%lu", sys ? "SYSTEM only (+BA enumerate)" : "default", secure, GetLastError());
    {
        static const struct { const char *name, *sddl; } d[] = {
            { "SecSys", "D:(A;;GA;;;SY)" }, { "SecAdm", "D:(A;;GA;;;BA)" }, { "SecDenyAll", "D:(D;;GA;;;WD)" } };
        int i;
        for (i = 0; i < 3 && !getenv("NOSEC"); i++)
        {
            PSECURITY_DESCRIPTOR sd3 = NULL; SECURITY_ATTRIBUTES sa3 = {sizeof(sa3)}; HDESK h2;
            ConvertStringSecurityDescriptorToSecurityDescriptorA(d[i].sddl, SDDL_REVISION_1, &sd3, NULL);
            sa3.lpSecurityDescriptor = sd3;
            h2 = CreateDesktopA(d[i].name, NULL, NULL, 0, GENERIC_ALL, &sa3);
            say("CreateDesktop(%s, %s) -> %p err=%lu", d[i].name, d[i].sddl, h2, GetLastError());
        }
    }
    dflt = OpenDesktopA("Default", 0, FALSE, DESKTOP_SWITCHDESKTOP);
    say("OpenDesktop(Default, SWITCH) -> %p", dflt);

    /* descriptor honored by OpenDesktop? */
    {
        HDESK h; DWORD need = 0; BYTE sdbuf[512]; SECURITY_INFORMATION si = DACL_SECURITY_INFORMATION; char *str = NULL;
        h = OpenDesktopA("Winlogon", 0, FALSE, DESKTOP_SWITCHDESKTOP);
        say("SD-TEST OpenDesktop(Winlogon, SWITCHDESKTOP) by creator-identity process: %s err=%lu", h ? "granted" : "DENIED", GetLastError());
        if (h) CloseDesktop(h);
        h = OpenDesktopA("Winlogon", 0, FALSE, DESKTOP_READOBJECTS);
        say("SD-TEST OpenDesktop(Winlogon, READOBJECTS): %s err=%lu", h ? "granted" : "DENIED", GetLastError());
        if (h) CloseDesktop(h);
        h = OpenDesktopA("Winlogon", 0, FALSE, WRITE_DAC | READ_CONTROL);
        say("SD-TEST OpenDesktop(Winlogon, WRITE_DAC|READ_CONTROL): %s err=%lu", h ? "granted" : "DENIED", GetLastError());
        if (h)
        {
            ok = GetUserObjectSecurity(h, &si, sdbuf, sizeof(sdbuf), &need);
            if (ok && ConvertSecurityDescriptorToStringSecurityDescriptorA(sdbuf, SDDL_REVISION_1, DACL_SECURITY_INFORMATION, &str, NULL))
                say("SD-TEST GetUserObjectSecurity DACL: %s", str);
            else say("SD-TEST GetUserObjectSecurity failed ok=%d err=%lu need=%lu", ok, GetLastError(), need);
            CloseDesktop(h);
        }
        /* a deny-all descriptor on a throw-away desktop */
        {
            PSECURITY_DESCRIPTOR sd2 = NULL; SECURITY_ATTRIBUTES sa2 = {sizeof(sa2)};
            ConvertStringSecurityDescriptorToSecurityDescriptorA("D:(D;;GA;;;WD)", SDDL_REVISION_1, &sd2, NULL);
            sa2.lpSecurityDescriptor = sd2;
            sec2 = getenv("NOSEC") ? NULL : CreateDesktopA("DenyAll", NULL, NULL, 0, GENERIC_ALL, &sa2);
            say("SD-TEST CreateDesktop(DenyAll) -> %p err=%lu", sec2, GetLastError());
            h = OpenDesktopA("DenyAll", 0, FALSE, DESKTOP_SWITCHDESKTOP);
            say("SD-TEST OpenDesktop(DenyAll, SWITCHDESKTOP): %s err=%lu", h ? "granted" : "DENIED", GetLastError());
            if (h) CloseDesktop(h);
            h = OpenDesktopA("DenyAll", 0, FALSE, MAXIMUM_ALLOWED);
            say("SD-TEST OpenDesktop(DenyAll, MAXIMUM_ALLOWED): %s err=%lu", h ? "granted" : "DENIED", GetLastError());
            if (h) CloseDesktop(h);
        }
    }

    if (!getenv("NOSEC")) sd_tests("main");

    /* target on the second desktop */
    snprintf(cmd, sizeof(cmd), "\"%s\" target %s", exe, dir);
    ok = start_proc(cmd, "WinSta0\\Winlogon", NULL, &tpi);
    say("CreateProcess(target, lpDesktop=WinSta0\\Winlogon) -> %d err=%lu", ok, GetLastError());
    if (!wait_file("target.info", 30000)) { say("target did not come up"); return 1; }
    path(p, "target.info"); f = fopen(p, "r");
    { unsigned long tp = 0; fscanf(f, "%llu %llu %llu %63s %lu", &a, &b, &c, tdn, &tp); say("target pid %lu", tp); } fclose(f);
    target = (HWND)(ULONG_PTR)a; ttid = (DWORD)b; btn = (HWND)(ULONG_PTR)c;
    say("target hwnd=%p tid=%lu button=%p on desktop %s", target, ttid, btn, tdn);

    sync_point("s1_before_switch");
    input_desktop_name(dn, sizeof(dn)); say("input desktop before: %s", dn);
    SetLastError(0); ok = SwitchDesktop(secure);
    say("SwitchDesktop(Winlogon) by Default process: %s err=%lu", ok ? "ok" : "FAIL", GetLastError());
    input_desktop_name(dn, sizeof(dn)); say("input desktop after: %s", dn);
    pump(500);
    sync_point("s2_after_switch");   /* host snapshot + xdotool at the target */

    /* attackers: full token, then a restricted token */
    snprintf(cmd, sizeof(cmd), "\"%s\" attacker %s %llu %lu %llu full", exe, dir, (unsigned long long)(ULONG_PTR)target, ttid, (unsigned long long)(ULONG_PTR)btn);
    ok = start_proc(cmd, "WinSta0\\Default", NULL, &api);
    say("start attacker full: %d", ok);
    if (!wait_file("att.full.ready", 60000)) say("attacker full not ready");
    sync_point("s3_hostinput_full");   /* host sends xdotool keys/clicks, then continues */
    touch("att.full.hostdone");
    WaitForSingleObject(api.hProcess, 20000);

    say("--- target log after full attacker + host input ---");
    path(p, "target.log"); f = fopen(p, "r"); if (f) { char l[200]; while (fgets(l, sizeof(l), f)) fputs(l, out); fclose(f); }
    say("--- end target log ---");
    fflush(out);

    /* restricted token attacker */
    {
        HANDLE tok, rtok = NULL; BYTE sidbuf[SECURITY_MAX_SID_SIZE]; DWORD sidlen = sizeof(sidbuf); SID_AND_ATTRIBUTES rsid = {0};
        CreateWellKnownSid(WinWorldSid, NULL, sidbuf, &sidlen); rsid.Sid = sidbuf;
        if (OpenProcessToken(GetCurrentProcess(), TOKEN_ALL_ACCESS, &tok))
        {
            if (CreateRestrictedToken(tok, DISABLE_MAX_PRIVILEGE | LUA_TOKEN, 0, NULL, 0, NULL, 1, &rsid, &rtok))
            {
                say("restricted (LUA|DISABLE_MAX_PRIVILEGE) token created");
                input_desktop_name(dn, sizeof(dn));
                /* target gets destroyed by DestroyWindow in 'full' if allowed; recreate if needed */
                if (!IsWindow(target)) { say("target window was destroyed by the full attacker; skipping the restricted run"); }
                else
                {
                    snprintf(cmd, sizeof(cmd), "\"%s\" attacker %s %llu %lu %llu limited", exe, dir, (unsigned long long)(ULONG_PTR)target, ttid, (unsigned long long)(ULONG_PTR)btn);
                    ok = start_proc(cmd, "WinSta0\\Default", NULL, &lpi);
                    say("start attacker limited: %d err=%lu", ok, GetLastError());
                    if (ok) { if (!wait_file("att.limited.ready", 60000)) say("limited not ready"); touch("att.limited.hostdone"); WaitForSingleObject(lpi.hProcess, 20000); }
                }
            }
            else say("CreateRestrictedToken failed %lu", GetLastError());
        }
    }
    input_desktop_name(dn, sizeof(dn)); say("input desktop at end of attackers: %s", dn);
    SwitchDesktop(dflt);

    /* death of the switching process */
    snprintf(cmd, sizeof(cmd), "\"%s\" switcher %s", exe, dir);
    ok = start_proc(cmd, "WinSta0\\Default", NULL, &spi);
    wait_file("switcher.info", 20000);
    input_desktop_name(dn, sizeof(dn)); say("after switcher switched: input desktop %s", dn);
    TerminateProcess(spi.hProcess, 1); WaitForSingleObject(spi.hProcess, 5000);
    pump(500);
    input_desktop_name(dn, sizeof(dn)); say("after the switching process was killed: input desktop %s", dn);
    sync_point("s4_after_death");
    SwitchDesktop(dflt);

    /* flipping by a host event to a Default window: switch, then let host click defwin */
    SwitchDesktop(secure);
    input_desktop_name(dn, sizeof(dn)); say("flip test: input desktop %s before host click on Default window", dn);
    sync_point("s5_flip");
    pump(500);
    input_desktop_name(dn, sizeof(dn)); say("flip test: input desktop %s after host click on Default window", dn);
    SwitchDesktop(dflt);

    /* a pointer motion over empty host space (no Default window of ours under it) */
    SwitchDesktop(secure);
    sync_point("s7_motion");
    pump(500);
    input_desktop_name(dn, sizeof(dn)); say("motion test: input desktop %s after host pointer motion over empty host space", dn);
    SwitchDesktop(secure);
    sync_point("s8_click");
    pump(500);
    input_desktop_name(dn, sizeof(dn)); say("click test: input desktop %s after host click over empty host space", dn);
    SwitchDesktop(dflt);

    /* a process that creates a private desktop, switches to it and is killed: the object dies with its last handle */
    snprintf(cmd, sizeof(cmd), "\"%s\" tmpswitch %s", exe, dir);
    ok = start_proc(cmd, "WinSta0\\Default", NULL, &spi);
    wait_file("tmpswitch.info", 20000);
    input_desktop_name(dn, sizeof(dn)); say("tmp test: after the process created+switched to TmpSwitch: input desktop %s", dn);
    TerminateProcess(spi.hProcess, 1); WaitForSingleObject(spi.hProcess, 5000);
    pump(500);
    input_desktop_name(dn, sizeof(dn)); say("tmp test: after that process was killed (desktop object destroyed): input desktop %s", dn);
    snprintf(cmd, sizeof(cmd), "\"%s\" query %s", exe, dir);
    if (start_proc(cmd, "WinSta0\\Default", NULL, &spi)) WaitForSingleObject(spi.hProcess, 10000);
    { HDESK h2 = OpenDesktopA("TmpSwitch", 0, FALSE, DESKTOP_READOBJECTS); say("tmp test: OpenDesktop(TmpSwitch) after kill: %s err=%lu", h2 ? "still exists" : "gone", GetLastError()); }
    SwitchDesktop(dflt);

    /* closing the last handle of the input desktop */
    SwitchDesktop(secure);
    TerminateProcess(tpi.hProcess, 0);
    pump(300);
    ok = CloseDesktop(secure);
    say("CloseDesktop(Winlogon input desktop, last handle) -> %d err=%lu", ok, GetLastError());
    input_desktop_name(dn, sizeof(dn)); say("input desktop after closing it: %s", dn);
    sync_point("s6_end");
    fclose(out);
    touch("main.done");
    return 0;
}

int main(int argc, char **argv)
{
    if (argc < 2) return 2;
    if (!strcmp(argv[1], "query")) { char dn[64], tn[64], p[MAX_PATH]; FILE *f; input_desktop_name(dn, sizeof(dn)); thread_desktop_name(tn, sizeof(tn));
        if (argc > 2) { snprintf(p, sizeof(p), "%s\\query.out", argv[2]); f = fopen(p, "a"); fprintf(f, "input=%s thread=%s\n", dn, tn); fclose(f); }
        else printf("%s\n", dn);
        return 0; }
    if (argc < 3) return 2;
    strncpy(dir, argv[2], sizeof(dir) - 1);
    if (!strcmp(argv[1], "main")) return run_main(argc > 3 && !strcmp(argv[3], "sys"));
    if (!strcmp(argv[1], "target")) return run_target();
    if (!strcmp(argv[1], "where") && argc >= 4) return run_where(argv[3]);
    if (!strcmp(argv[1], "switcher")) return run_switcher();
    if (!strcmp(argv[1], "tmpswitch")) return run_tmpswitch();
    if (!strcmp(argv[1], "attacker") && argc >= 7)
        return run_attacker((HWND)(ULONG_PTR)strtoull(argv[3], 0, 10), strtoul(argv[4], 0, 10), (HWND)(ULONG_PTR)strtoull(argv[5], 0, 10), argv[6]);
    return 2;
}

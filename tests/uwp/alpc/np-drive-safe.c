/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright 2026 WineExplorer contributors
 *
 * Host-safe Notepad input driver: like np-drive, but it clicks and types only while the foreground window belongs to the
 * Notepad.exe process it found (checked before the click and before every key); otherwise it stops. Prints the focus, active
 * and caret windows (with their class names and threads) after the click and after typing.
 * Usage: np-drive-safe [initial-wait-seconds] [text]
 */
#include <windows.h>
#include <stdio.h>
#include <string.h>

static HWND found;
static DWORD found_pid;

static BOOL CALLBACK enum_proc(HWND h, LPARAM lp)
{
    DWORD pid; char exe[MAX_PATH] = "";
    HANDLE ph;
    RECT r;

    if (!IsWindowVisible(h)) return TRUE;
    GetWindowRect(h, &r);
    if (r.right - r.left < 200 || r.bottom - r.top < 100) return TRUE;
    GetWindowThreadProcessId(h, &pid);
    ph = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (ph) { DWORD n = sizeof(exe); QueryFullProcessImageNameA(ph, 0, exe, &n); CloseHandle(ph); }
    if (strstr(exe, "WindowsApps") && (strstr(exe, "Notepad.exe") || strstr(exe, "notepad.exe")))
    {
        found = h; found_pid = pid;
        printf("window %p pid %lu rect %ld,%ld-%ld,%ld\n", h, pid, r.left, r.top, r.right, r.bottom);
        return FALSE;
    }
    return TRUE;
}

static BOOL notepad_in_front(void)
{
    DWORD pid = 0;
    HWND fg = GetForegroundWindow();
    if (fg) GetWindowThreadProcessId(fg, &pid);
    return fg && pid == found_pid;
}

static void describe(const char *what, HWND h)
{
    char cls[96] = "";
    DWORD tid = 0;
    if (h) { GetClassNameA(h, cls, sizeof(cls)); tid = GetWindowThreadProcessId(h, NULL); }
    printf("  %s %p class %s thread %lu\n", what, h, cls, tid);
}

static void report(const char *when)
{
    GUITHREADINFO gti = { sizeof(gti) };
    GetGUIThreadInfo(0, &gti);
    printf("%s:\n", when);
    describe("foreground", GetForegroundWindow());
    describe("focus", gti.hwndFocus);
    describe("active", gti.hwndActive);
    describe("caret", gti.hwndCaret);
}

int main(int argc, char **argv)
{
    int wait = argc > 1 ? atoi(argv[1]) : 30, i;
    const char *text = argc > 2 ? argv[2] : "hello";
    RECT r;
    INPUT in[2] = {{0}};

    setvbuf(stdout, NULL, _IONBF, 0);
    for (i = 0; i < wait * 2 && !found; i++) { EnumWindows(enum_proc, 0); if (!found) Sleep(500); }
    if (!found) { printf("no packaged Notepad window\n"); return 1; }
    Sleep(2000);
    SetForegroundWindow(found);
    Sleep(500);
    if (!notepad_in_front()) { report("Notepad is not in front; nothing sent"); return 2; }
    GetWindowRect(found, &r);
    SetCursorPos((r.left + r.right) / 2, (r.top + r.bottom) / 2);
    in[0].type = INPUT_MOUSE; in[0].mi.dwFlags = MOUSEEVENTF_LEFTDOWN;
    in[1].type = INPUT_MOUSE; in[1].mi.dwFlags = MOUSEEVENTF_LEFTUP;
    SendInput(1, &in[0], sizeof(INPUT)); Sleep(50); SendInput(1, &in[1], sizeof(INPUT));
    Sleep(1000);
    report("after the click");
    for (; *text; text++)
    {
        INPUT k[2] = {{0}};
        if (!notepad_in_front()) { report("Notepad left the front; typing stopped"); return 3; }
        k[0].type = INPUT_KEYBOARD; k[0].ki.wVk = VkKeyScanA(*text) & 0xff;
        k[1] = k[0]; k[1].ki.dwFlags = KEYEVENTF_KEYUP;
        SendInput(2, k, sizeof(INPUT));
        Sleep(150);
    }
    Sleep(1000);
    report("after typing");
    if (argc > 3 && !strcmp(argv[3], "save"))
    {
        /* Ctrl+S, only while Notepad is in front; then the foreground window after 4 s */
        INPUT k[4] = {{0}};
        char title[128] = "";

        if (!notepad_in_front()) { report("Notepad left the front; Ctrl+S not sent"); return 3; }
        k[0].type = INPUT_KEYBOARD; k[0].ki.wVk = VK_CONTROL;
        k[1].type = INPUT_KEYBOARD; k[1].ki.wVk = 'S';
        k[2] = k[1]; k[2].ki.dwFlags = KEYEVENTF_KEYUP;
        k[3] = k[0]; k[3].ki.dwFlags = KEYEVENTF_KEYUP;
        SendInput(4, k, sizeof(INPUT));
        Sleep(4000);
        GetWindowTextA(GetForegroundWindow(), title, sizeof(title));
        report("after Ctrl+S");
        printf("  foreground title \"%s\"\n", title);
    }
    return 0;
}

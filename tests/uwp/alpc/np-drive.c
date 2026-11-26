/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright 2026 WineExplorer contributors
 *
 * Drives a running Notepad (any WinUI 3 window of the Notepad.exe process): finds its top-level window, brings it to the
 * foreground, clicks into the text area and types a few characters, printing what it did. Used in the lab to see whether
 * input makes Notepad's compositor produce more frames. Usage: np-drive [initial-wait-seconds] [text]
 */
#include <windows.h>
#include <stdio.h>
#include <string.h>

static HWND found;

static BOOL CALLBACK enum_proc(HWND h, LPARAM lp)
{
    DWORD pid; char exe[MAX_PATH] = "", cls[64] = "";
    HANDLE ph;
    RECT r;

    if (!IsWindowVisible(h)) return TRUE;
    GetWindowRect(h, &r);
    if (r.right - r.left < 200 || r.bottom - r.top < 100) return TRUE;
    GetWindowThreadProcessId(h, &pid);
    ph = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (ph) { DWORD n = sizeof(exe); QueryFullProcessImageNameA(ph, 0, exe, &n); CloseHandle(ph); }
    GetClassNameA(h, cls, sizeof(cls));
    if (strstr(exe, "Notepad.exe") || strstr(exe, "notepad.exe")) { found = h; printf("window %p class %s exe %s rect %ld,%ld-%ld,%ld\n", h, cls, exe, r.left, r.top, r.right, r.bottom); return FALSE; }
    return TRUE;
}

static void key(WORD vk)
{
    INPUT in[2] = {{0}};
    in[0].type = INPUT_KEYBOARD; in[0].ki.wVk = vk;
    in[1] = in[0]; in[1].ki.dwFlags = KEYEVENTF_KEYUP;
    SendInput(2, in, sizeof(INPUT));
}

int main(int argc, char **argv)
{
    int wait = argc > 1 ? atoi(argv[1]) : 30, i;
    const char *text = argc > 2 ? argv[2] : "hello";
    RECT r;
    INPUT in[2] = {{0}};
    GUITHREADINFO gti = { sizeof(gti) };

    setvbuf(stdout, NULL, _IONBF, 0);
    for (i = 0; i < wait * 2 && !found; i++) { EnumWindows(enum_proc, 0); if (!found) Sleep(500); }
    if (!found) { printf("no Notepad window\n"); return 1; }
    Sleep(2000);
    SetForegroundWindow(found);
    Sleep(500);
    GetWindowRect(found, &r);
    SetCursorPos((r.left + r.right) / 2, (r.top + r.bottom) / 2);
    in[0].type = INPUT_MOUSE; in[0].mi.dwFlags = MOUSEEVENTF_LEFTDOWN;
    in[1].type = INPUT_MOUSE; in[1].mi.dwFlags = MOUSEEVENTF_LEFTUP;
    SendInput(1, &in[0], sizeof(INPUT)); Sleep(50); SendInput(1, &in[1], sizeof(INPUT));
    Sleep(1000);
    GetGUIThreadInfo(0, &gti);
    printf("foreground %p, focus %p, active %p, caret %p after the click\n", GetForegroundWindow(), gti.hwndFocus, gti.hwndActive, gti.hwndCaret);
    for (; *text; text++) { key(VkKeyScanA(*text) & 0xff); Sleep(150); }
    Sleep(1000);
    GetGUIThreadInfo(0, &gti);
    printf("after typing: focus %p, caret %p\n", gti.hwndFocus, gti.hwndCaret);
    return 0;
}

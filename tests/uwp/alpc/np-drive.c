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
    if (getenv("NP_CTRL"))
    {
        /* NP_CTRL=letters: Ctrl+letter for each, with the foreground window's class and title 4 s after each */
        const char *l;

        for (l = getenv("NP_CTRL"); *l; l++)
        {
            INPUT k[4] = {{0}};
            char cls[96] = "", title[128] = "";
            HWND fg;

            k[0].type = INPUT_KEYBOARD; k[0].ki.wVk = VK_CONTROL;
            k[1].type = INPUT_KEYBOARD; k[1].ki.wVk = (WORD)(*l & ~0x20);
            k[2] = k[1]; k[2].ki.dwFlags = KEYEVENTF_KEYUP;
            k[3] = k[0]; k[3].ki.dwFlags = KEYEVENTF_KEYUP;
            SendInput(4, k, sizeof(INPUT));
            Sleep(4000);
            fg = GetForegroundWindow();
            if (fg) { GetClassNameA(fg, cls, sizeof(cls)); GetWindowTextA(fg, title, sizeof(title)); }
            printf("after Ctrl+%c: foreground %p class %s title \"%s\"; Notepad title \"", *l & ~0x20, fg, cls, title);
            GetWindowTextA(found, title, sizeof(title));
            printf("%s\"\n", title);
        }
    }
    if (getenv("NP_CLICK"))
    {
        /* NP_CLICK="x,y x,y ...": left clicks at these window coordinates, 3 s apart, with the foreground window after each */
        const char *p = getenv("NP_CLICK");
        int x, y, n;

        while (sscanf(p, "%d,%d%n", &x, &y, &n) == 2)
        {
            char cls[96] = "", title[128] = "";
            HWND fg;

            GetWindowRect(found, &r);
            SetCursorPos(r.left + x, r.top + y);
            Sleep(100);
            SendInput(1, &in[0], sizeof(INPUT)); Sleep(50); SendInput(1, &in[1], sizeof(INPUT));
            Sleep(3000);
            fg = GetForegroundWindow();
            if (fg) { GetClassNameA(fg, cls, sizeof(cls)); GetWindowTextA(fg, title, sizeof(title)); }
            printf("after the click at %d,%d: foreground %p class %s title \"%s\"\n", x, y, fg, cls, title);
            p += n;
            while (*p == ' ') p++;
        }
    }
    if (getenv("NP_SAVE"))
    {
        /* Ctrl+S, then report the window in front (a save dialog?) and the top-level windows of the Notepad process */
        INPUT k[4] = {{0}};
        char cls[96], title[128];
        HWND fg;

        k[0].type = INPUT_KEYBOARD; k[0].ki.wVk = VK_CONTROL;
        k[1].type = INPUT_KEYBOARD; k[1].ki.wVk = 'S';
        k[2] = k[1]; k[2].ki.dwFlags = KEYEVENTF_KEYUP;
        k[3] = k[0]; k[3].ki.dwFlags = KEYEVENTF_KEYUP;
        SendInput(4, k, sizeof(INPUT));
        for (i = 0; i < 10; i++)
        {
            DWORD_PTR res = 0;
            LRESULT answered;

            Sleep(2000);
            fg = GetForegroundWindow();
            cls[0] = title[0] = 0;
            if (fg) { GetClassNameA(fg, cls, sizeof(cls)); GetWindowTextA(fg, title, sizeof(title)); }
            /* does the window's thread answer messages? */
            answered = fg ? SendMessageTimeoutA(fg, WM_NULL, 0, 0, SMTO_ABORTIFHUNG, 1000, &res) : 0;
            printf("after Ctrl+S +%d s: foreground %p class %s title \"%s\", answers WM_NULL: %s\n", (i + 1) * 2, fg, cls, title,
                   answered ? "yes" : "no");
            if (i == 3 && getenv("NP_SAVE_ENTER") && !strcmp(cls, "#32770"))
            {
                /* accept the dialog with Enter (the default button, Save) */
                key(VK_RETURN);
                printf("Enter pressed\n");
            }
        }
        GetWindowTextA(found, title, sizeof(title));
        printf("Notepad title now \"%s\"\n", title);
        {
            static const char *const paths[] = {"C:\\users\\debian\\Desktop\\hello.txt", "C:\\users\\debian\\Documents\\hello.txt"};
            WIN32_FILE_ATTRIBUTE_DATA fa;
            unsigned int p;

            for (p = 0; p < 2; p++)
                if (GetFileAttributesExA(paths[p], GetFileExInfoStandard, &fa))
                    printf("saved: %s, %lu bytes\n", paths[p], fa.nFileSizeLow);
        }
    }
    return 0;
}

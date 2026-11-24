/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright 2026 WineExplorer contributors
 *
 * Black-box probe, Windows 11: user32 GetWindowBand and GetWindowBand's companions as the Windows App Runtime uses them. Prints the
 * return value, the band and GetLastError for a plain window, a window of another process (the shell tray), the desktop window, NULL
 * and an invalid handle.
 * Build (mingw): x86_64-w64-mingw32-gcc -O2 -o windowband-probe.exe windowband-probe.c -luser32
 */
#include <windows.h>
#include <stdio.h>

typedef BOOL (WINAPI *get_band_t)(HWND, DWORD *);

int main(void)
{
    HMODULE user32 = LoadLibraryA("user32.dll");
    get_band_t get_band = (get_band_t)GetProcAddress(user32, "GetWindowBand");
    WNDCLASSA wc = {0};
    HWND own, tray = FindWindowA("Shell_TrayWnd", NULL);
    HWND list[6];
    const char *names[6] = { "own overlapped window", "own popup", "shell tray (other process)", "desktop window", "NULL", "invalid 0x12345678" };
    int i;

    wc.lpfnWndProc = DefWindowProcA; wc.hInstance = GetModuleHandleA(NULL); wc.lpszClassName = "probe";
    RegisterClassA(&wc);
    own = CreateWindowA("probe", "probe", WS_OVERLAPPEDWINDOW, 0, 0, 100, 100, NULL, NULL, wc.hInstance, NULL);
    list[0] = own;
    list[1] = CreateWindowA("probe", "popup", WS_POPUP, 0, 0, 50, 50, NULL, NULL, wc.hInstance, NULL);
    list[2] = tray; list[3] = GetDesktopWindow(); list[4] = NULL; list[5] = (HWND)0x12345678;
    printf("GetWindowBand %p\n", (void *)get_band);
    if (!get_band) return 1;
    for (i = 0; i < 6; i++)
    {
        DWORD band = 0xdeadbeef;
        BOOL ret;

        SetLastError(0xdeadbeef);
        ret = get_band(list[i], &band);
        printf("%-30s %p: returns %d, band %#lx, GetLastError %lu\n", names[i], (void *)list[i], ret, (unsigned long)band, GetLastError());
    }
    SetLastError(0xdeadbeef);
    printf("NULL out pointer: returns %d, GetLastError %lu\n", get_band(own, NULL), GetLastError());
    return 0;
}

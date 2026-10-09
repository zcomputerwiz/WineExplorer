/* SPDX-License-Identifier: LGPL-2.1-or-later
 * Exercise the Linux application bridge without opening the Start menu.
 * Build: x86_64-w64-mingw32-gcc -o linux-apps-probe.exe linux-apps-probe.c
 * Run: wine linux-apps-probe.exe [desktop-id]
 */
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>

int main(int argc, char **argv)
{
    HMODULE module = LoadLibraryW(L"wexhost.dll");
    DWORD (WINAPI *list)(char *, DWORD, DWORD *);
    DWORD (WINAPI *launch)(const WCHAR *);
    DWORD err, needed = 0;
    char *buffer;
    WCHAR id[512];

    if (!module) { fprintf(stderr, "LoadLibrary: %lu\n", GetLastError()); return 1; }
    list = (void *)GetProcAddress(module, "WexAppsList");
    launch = (void *)GetProcAddress(module, "WexAppsLaunch");
    if (!list || !launch) { fprintf(stderr, "Application bridge exports missing\n"); return 1; }
    if (argc > 1)
    {
        if (!MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, argv[1], -1, id, 512)) return 1;
        err = launch(id);
        printf("launch: %lu\n", err);
        return err ? 1 : 0;
    }
    if ((err = list(NULL, 0, &needed)) != ERROR_MORE_DATA || !needed)
    {
        fprintf(stderr, "list size: %lu (%lu bytes)\n", err, needed);
        return 1;
    }
    if (!(buffer = malloc(needed))) return 1;
    err = list(buffer, needed, &needed);
    if (err) fprintf(stderr, "list: %lu\n", err);
    else fputs(buffer, stdout);
    free(buffer);
    return err ? 1 : 0;
}

/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Copyright 2026 WineExplorer contributors */
/*
 * IsDeveloperModeEnabled of api-ms-win-appmodel-unlock-l1-1-0 (kernelbase): what it takes and gives.
 * Build (mingw): x86_64-w64-mingw32-gcc -O2 -o devmode-probe.exe devmode-probe.c
 */
#include <windows.h>
#include <stdio.h>

int main(void)
{
    HMODULE m;
    FARPROC f;
    HMODULE kb = LoadLibraryW(L"kernelbase.dll");

    setvbuf(stdout, NULL, _IONBF, 0);
    m = LoadLibraryW(L"api-ms-win-appmodel-unlock-l1-1-0.dll");
    printf("api set module %p, kernelbase %p\n", (void *)m, (void *)kb);
    f = m ? GetProcAddress(m, "IsDeveloperModeEnabled") : NULL;
    printf("IsDeveloperModeEnabled %p; in kernelbase %p\n", (void *)f, kb ? (void *)GetProcAddress(kb, "IsDeveloperModeEnabled") : NULL);
    if (f)
    {
        /* with a pointer to a canary: does it write a BOOL, and what does it return? */
        {
            ULONG_PTR r;
            volatile LONG canary[4] = { 0x11111111, 0x11111111, 0x11111111, 0x11111111 };

            SetLastError(0);
            r = ((ULONG_PTR (WINAPI *)(void *))f)((void *)canary);
            printf("with a pointer: returned %#llx, canary %08lx %08lx, error %lu\n", (unsigned long long)r, canary[0], canary[1], GetLastError());
        }
        {
            ULONG_PTR r = ((ULONG_PTR (WINAPI *)(void))f)();
            printf("without arguments: %#llx\n", (unsigned long long)r);
        }
    }
    return 0;
}

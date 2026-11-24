/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright 2026 WineExplorer contributors
 *
 * Black-box probe: which ordinals of a system DLL resolve with GetProcAddress (dcomp.dll by default), swept from 1 to 4096, and whether a
 * name given on the command line resolves to the address of an ordinal. Only GetProcAddress is used, the file is not read.
 *   dcomp-exports-probe [DLL [NAME ...]]
 * With no names it checks DCompositionCreateSharedVisualHandle, the name that Wine gives to ordinal 1040 (the Wine log shows the Windows App
 * Runtime asking dcomp.dll for that ordinal).
 * Build: x86_64-w64-mingw32-gcc -O2 -o dcomp-exports-probe.exe dcomp-exports-probe.c
 */
#include <stdio.h>
#include <windows.h>

int main(int argc, char **argv)
{
    HMODULE mod = LoadLibraryA(argc > 1 ? argv[1] : "dcomp.dll");
    const char *default_names[] = {"DCompositionCreateSharedVisualHandle"};
    const char **names = argc > 2 ? (const char **)&argv[2] : default_names;
    int nnames = argc > 2 ? argc - 2 : 1;
    void *by_ordinal[4097] = {0};
    unsigned int n, count = 0;
    int i;

    if (!mod) { printf("load failed %lu\n", GetLastError()); return 1; }
    for (n = 1; n <= 4096; n++)
    {
        by_ordinal[n] = (void *)GetProcAddress(mod, MAKEINTRESOURCEA(n));
        if (by_ordinal[n]) count++;
    }
    printf("%u ordinals between 1 and 4096 resolve\n", count);
    for (n = 1; n <= 4096; n++)
        if (by_ordinal[n]) printf("ordinal %u\n", n);

    for (i = 0; i < nnames; i++)
    {
        void *p = (void *)GetProcAddress(mod, names[i]);
        printf("name %s: %p", names[i], p);
        if (p)
            for (n = 1; n <= 4096; n++)
                if (by_ordinal[n] == p) printf(" = ordinal %u", n);
        printf("\n");
    }
    return 0;
}

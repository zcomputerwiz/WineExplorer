/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright 2026 WineExplorer contributors
 *
 * Black-box probe: the exported names and ordinals of a system DLL, read from its export directory in memory
 * (dcomp.dll by default): which exports are named, and which ordinals have no name. dcompi.dll of the Windows App Runtime imports
 * dcomp.dll by ordinal (1040).
 * Build: x86_64-w64-mingw32-gcc -O2 -o dcomp-exports-probe.exe dcomp-exports-probe.c
 */
#include <stdio.h>
#include <windows.h>

int main(int argc, char **argv)
{
    HMODULE mod = LoadLibraryA(argc > 1 ? argv[1] : "dcomp.dll");
    BYTE *base = (BYTE *)mod;
    IMAGE_NT_HEADERS *nt;
    IMAGE_EXPORT_DIRECTORY *exp;
    DWORD *names, *funcs, i, j;
    WORD *ords;

    if (!mod) { printf("load failed %lu\n", GetLastError()); return 1; }
    nt = (IMAGE_NT_HEADERS *)(base + ((IMAGE_DOS_HEADER *)base)->e_lfanew);
    exp = (IMAGE_EXPORT_DIRECTORY *)(base + nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT].VirtualAddress);
    names = (DWORD *)(base + exp->AddressOfNames);
    funcs = (DWORD *)(base + exp->AddressOfFunctions);
    ords = (WORD *)(base + exp->AddressOfNameOrdinals);
    printf("base ordinal %lu, %lu functions, %lu names\n", exp->Base, exp->NumberOfFunctions, exp->NumberOfNames);
    for (i = 0; i < exp->NumberOfFunctions; i++)
    {
        const char *name = NULL;
        if (!funcs[i]) continue;
        for (j = 0; j < exp->NumberOfNames; j++)
            if (ords[j] == i) { name = (const char *)(base + names[j]); break; }
        printf("%lu rva %08lx %s\n", exp->Base + i, funcs[i], name ? name : "(unnamed)");
    }
    return 0;
}

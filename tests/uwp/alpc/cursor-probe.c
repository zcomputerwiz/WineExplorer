/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Copyright 2026 WineExplorer contributors */
/*
 * Which system cursors LoadCursorW(NULL, ...) gives. Notepad of Windows 11 throws when the cursor for IDC_WAIT cannot be loaded.
 * Build (mingw): x86_64-w64-mingw32-gcc -O2 -o cursor-probe.exe cursor-probe.c -luser32
 */
#include <windows.h>
#include <stdio.h>

int main(void)
{
    static const struct { const WCHAR *name; const void *id; } ids[] =
    {
        { L"IDC_ARROW", IDC_ARROW }, { L"IDC_IBEAM", IDC_IBEAM }, { L"IDC_WAIT", IDC_WAIT }, { L"IDC_CROSS", IDC_CROSS },
        { L"IDC_UPARROW", IDC_UPARROW }, { L"IDC_SIZENWSE", IDC_SIZENWSE }, { L"IDC_SIZENESW", IDC_SIZENESW },
        { L"IDC_SIZEWE", IDC_SIZEWE }, { L"IDC_SIZENS", IDC_SIZENS }, { L"IDC_SIZEALL", IDC_SIZEALL },
        { L"IDC_NO", IDC_NO }, { L"IDC_HAND", IDC_HAND }, { L"IDC_APPSTARTING", IDC_APPSTARTING }, { L"IDC_HELP", IDC_HELP },
    };
    unsigned i;

    for (i = 0; i < sizeof(ids) / sizeof(ids[0]); i++)
    {
        HCURSOR c;

        SetLastError(0);
        c = LoadCursorW(NULL, (LPCWSTR)ids[i].id);
        printf("%ls: %p (error %lu)\n", ids[i].name, (void *)c, GetLastError());
    }
    return 0;
}

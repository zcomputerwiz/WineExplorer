/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Copyright 2026 WineExplorer contributors */
/* Lab tool: run a program elevated through ShellExecuteEx "runas" and wait for it. Usage: runas_wait file [params...] */
#include <windows.h>
#include <shellapi.h>
#include <stdio.h>
#include <wchar.h>

int wmain(int argc, wchar_t **argv)
{
    SHELLEXECUTEINFOW sei = {0};
    WCHAR params[4096] = L"";
    DWORD code = 0;
    int i;

    if (argc < 2) { fprintf(stderr, "usage: runas_wait file [params]\n"); return 2; }
    for (i = 2; i < argc; i++)
    {
        if (i > 2) wcscat(params, L" ");
        wcscat(params, argv[i]);
    }
    sei.cbSize = sizeof(sei);
    sei.fMask = SEE_MASK_NOCLOSEPROCESS | SEE_MASK_FLAG_NO_UI | SEE_MASK_NOASYNC;
    sei.lpVerb = L"runas";
    sei.lpFile = argv[1];
    sei.lpParameters = params;
    sei.nShow = SW_HIDE;
    if (!ShellExecuteExW(&sei))
    {
        printf("runas failed: error %lu\n", GetLastError());
        return 3;
    }
    printf("runas started\n");
    if (sei.hProcess)
    {
        WaitForSingleObject(sei.hProcess, 900000);
        GetExitCodeProcess(sei.hProcess, &code);
        printf("exit code %lu\n", code);
    }
    return (int)code;
}

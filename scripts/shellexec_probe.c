/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Copyright 2026 WineExplorer contributors */
/* Call ShellExecuteExW like an installer does and print the result: shellexec_probe.exe FILE [PARAMS [VERB]]
 * Prints the BOOL, GetLastError and hInstApp, then waits for the process and prints its exit code.
 * Build: i686-w64-mingw32-gcc -municode -o shellexec_probe32.exe shellexec_probe.c -lshell32 */
#include <windows.h>
#include <shellapi.h>
#include <stdio.h>

int wmain(int argc, wchar_t **argv)
{
    SHELLEXECUTEINFOW sei = { sizeof(sei) };
    BOOL ret;
    DWORD err, code = 0;
    FILE *log = _wfopen(L"Z:\\tmp\\gsr-out\\probe.txt", L"a");

    if (argc < 2) { fprintf(stderr, "usage: shellexec_probe FILE [PARAMS [VERB]]\n"); return 2; }
    sei.fMask = SEE_MASK_NOCLOSEPROCESS | SEE_MASK_FLAG_NO_UI;
    sei.lpFile = argv[1];
    if (argc > 2) sei.lpParameters = argv[2];
    if (argc > 3 && argv[3][0]) sei.lpVerb = argv[3];
    sei.nShow = SW_HIDE;
    SetLastError(0);
    ret = ShellExecuteExW(&sei);
    err = GetLastError();
    printf("ShellExecuteExW ret=%d lasterror=%lu hInstApp=%p hProcess=%p\n", ret, err, (void *)sei.hInstApp, sei.hProcess);
    if (log) fprintf(log, "ShellExecuteExW ret=%d lasterror=%lu hInstApp=%p hProcess=%p\n", ret, err, (void *)sei.hInstApp, sei.hProcess);
    if (ret && sei.hProcess)
    {
        WaitForSingleObject(sei.hProcess, 60000);
        GetExitCodeProcess(sei.hProcess, &code);
        printf("exit code %lu\n", code);
        if (log) fprintf(log, "exit code %lu\n", code);
        CloseHandle(sei.hProcess);
    }
    if (log) fclose(log);
    return ret ? 0 : 1;
}

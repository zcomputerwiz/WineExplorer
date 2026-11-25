/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright 2026 WineExplorer contributors
 *
 * B109, narrowing: open and close a connection to a pipe of session 0 through the bridge, many times, and say how many
 * connections could be opened. Used with scripts/test_sysrun_server.sh section s0_scmloop (WEX_SCMLOOP_MODE=pipe).
 *
 * Usage: pipe_loop_probe.exe PIPENAME COUNT [hold]     (hold: keep every connection open, to find the cap)
 */
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(int argc, char **argv)
{
    char name[300];
    int count = argc > 2 ? atoi(argv[2]) : 100, i, ok = 0, failed = 0, hold = argc > 3 && !strcmp(argv[3], "hold");
    HANDLE *kept = calloc(count, sizeof(HANDLE));

    snprintf(name, sizeof(name), "%s", argc > 1 ? argv[1] : "\\\\.\\pipe\\WexSession0\\svcctl");
    for (i = 0; i < count; i++)
    {
        HANDLE h = CreateFileA(name, GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);

        if (h == INVALID_HANDLE_VALUE)
        {
            DWORD err = GetLastError();
            if (err == ERROR_PIPE_BUSY) { WaitNamedPipeA(name, 200); i--; if (++failed > 2000) break; continue; }
            printf("open failed at %d (error %lu)\n", i, err);
            failed++;
            break;
        }
        ok++;
        if (hold) kept[i] = h;
        else CloseHandle(h);
        if (i % 100 == 0) { printf("progress %d\n", i); fflush(stdout); }
    }
    printf("pipe_loop opened=%d failed=%d\n", ok, failed);
    if (hold) { Sleep(8000); for (i = 0; i < count; i++) if (kept[i]) CloseHandle(kept[i]); }
    return 0;
}

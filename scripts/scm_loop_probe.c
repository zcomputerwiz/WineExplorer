/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright 2026 WineExplorer contributors
 *
 * B109: calls that a session's services.exe forwards to session 0 (T5.4) fail with 1722 after hours of uptime with many
 * elevated installs. This probe opens and closes the service manager (and a service handle) over and over in a session,
 * asks for the merged enumeration now and then, and says when and how the first call fails.
 *
 * Usage: scm_loop_probe.exe MODE COUNT
 *   open     open the manager, open a service, close both (the common path)
 *   enum     the same, and every 10th round the merged EnumServicesStatusEx
 *   nosvc    open and close the manager only
 *   leakmgr  open the manager and a service, close the SERVICE handle only (the manager is left open: a client that leaks)
 * Prints "progress ROUND" every 100 rounds, "FAIL what round=N error=E" for a failure (it goes on up to 5 failures), and
 * "scm_loop=PASS rounds=N" or "scm_loop=FAIL ...".
 */
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(int argc, char **argv)
{
    const char *mode = argc > 1 ? argv[1] : "open";
    int count = argc > 2 ? atoi(argv[2]) : 1000, i, fails = 0, leaked = 0;
    BYTE buffer[65536];

    for (i = 0; i < count && fails < 5; i++)
    {
        SC_HANDLE m = OpenSCManagerW(NULL, NULL, SC_MANAGER_CONNECT | SC_MANAGER_ENUMERATE_SERVICE), svc;

        if (!m)
        {
            printf("FAIL OpenSCManager round=%d error=%lu\n", i, GetLastError());
            fflush(stdout);
            fails++;
            Sleep(500);
            continue;
        }
        if (strcmp(mode, "nosvc"))
        {
            svc = OpenServiceW(m, L"PlugPlay", SERVICE_QUERY_STATUS);
            if (!svc)
            {
                printf("FAIL OpenService round=%d error=%lu\n", i, GetLastError());
                fails++;
            }
            else
            {
                SERVICE_STATUS st;
                if (!QueryServiceStatus(svc, &st))
                {
                    printf("FAIL QueryServiceStatus round=%d error=%lu\n", i, GetLastError());
                    fails++;
                }
                CloseServiceHandle(svc);
            }
        }
        if (!strcmp(mode, "enum") && i % 10 == 0)
        {
            DWORD needed = 0, returned = 0, resume = 0;

            if (!EnumServicesStatusExW(m, SC_ENUM_PROCESS_INFO, SERVICE_WIN32, SERVICE_STATE_ALL, buffer, sizeof(buffer), &needed,
                                       &returned, &resume, NULL) && GetLastError() != ERROR_MORE_DATA)
            {
                printf("FAIL EnumServicesStatusEx round=%d error=%lu\n", i, GetLastError());
                fails++;
            }
        }
        if (!strcmp(mode, "leakmgr") && i % 2 == 0) leaked++;   /* every second manager is never closed */
        else CloseServiceHandle(m);
        if (i % 100 == 0)
        {
            printf("progress %d\n", i);
            fflush(stdout);
        }
    }
    printf("scm_loop=%s rounds=%d leaked=%d\n", fails ? "FAIL" : "PASS", i, leaked);
    return fails != 0;
}

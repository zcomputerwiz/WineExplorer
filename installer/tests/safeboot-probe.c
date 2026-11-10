/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Copyright 2026 WineExplorer contributors
 *
 * Safe Mode as Windows programs see it (docs/SAFE_MODE_WORKORDER.md B1-B3, B7), for installer/tests/safe-mode.sh:
 * GetSystemMetrics(SM_CLEANBOOT), the volatile SafeBoot\Option key, which of Wine's automatic services run, and
 * whether the SCM refuses to start a service that no SafeBoot list names. Prints one line:
 *
 *   bits=64 cleanboot=1 option=1 shell=0 EventLog=running PlugPlay=running NDIS=stopped nsiproxy=stopped spooler=1084
 *
 * option and shell are "none" when the key or value is absent; spooler is the error of StartService for the print
 * spooler, tried only in Safe Mode (it is in neither list), and "untried" otherwise.
 *
 *   i686-w64-mingw32-gcc -O2 -o safeboot-probe32.exe safeboot-probe.c
 *   x86_64-w64-mingw32-gcc -O2 -o safeboot-probe64.exe safeboot-probe.c
 */
#include <stdio.h>
#include <windows.h>

static const char option_key[] = "System\\CurrentControlSet\\Control\\SafeBoot\\Option";

static void print_value(const char *label, const char *name)
{
    DWORD value, size = sizeof(value);

    if (RegGetValueA(HKEY_LOCAL_MACHINE, option_key, name, RRF_RT_REG_DWORD, NULL, &value, &size))
        printf(" %s=none", label);
    else
        printf(" %s=%lu", label, value);
}

static void print_service(SC_HANDLE scm, const char *name)
{
    SERVICE_STATUS status;
    SC_HANDLE service = OpenServiceA(scm, name, SERVICE_QUERY_STATUS);
    const char *state = "absent";

    if (service)
    {
        if (!QueryServiceStatus(service, &status)) state = "unknown";
        else if (status.dwCurrentState == SERVICE_RUNNING) state = "running";
        else if (status.dwCurrentState == SERVICE_STOPPED) state = "stopped";
        else state = "pending";
        CloseServiceHandle(service);
    }
    printf(" %s=%s", name, state);
}

int main(void)
{
    DWORD option, size = sizeof(option);
    SC_HANDLE scm, service;

    printf("bits=%u cleanboot=%d", (unsigned int)(sizeof(void *) * 8), GetSystemMetrics(SM_CLEANBOOT));
    print_value("option", "OptionValue");
    print_value("shell", "UseAlternateShell");
    if (!(scm = OpenSCManagerA(NULL, NULL, SC_MANAGER_CONNECT)))
    {
        printf(" scm=%lu\n", GetLastError());
        return 1;
    }
    print_service(scm, "EventLog");
    print_service(scm, "PlugPlay");
    print_service(scm, "MountMgr");   /* a listed driver: drivers start through a Winedevice host no list names */
    print_service(scm, "NDIS");
    print_service(scm, "nsiproxy");
    if (RegGetValueA(HKEY_LOCAL_MACHINE, option_key, "OptionValue", RRF_RT_REG_DWORD, NULL, &option, &size) || !option)
        printf(" spooler=untried");
    else if (!(service = OpenServiceA(scm, "Spooler", SERVICE_START)))
        printf(" spooler=open-%lu", GetLastError());
    else
    {
        printf(" spooler=%lu", StartServiceA(service, 0, NULL) ? 0 : GetLastError());
        CloseServiceHandle(service);
    }
    CloseServiceHandle(scm);
    printf("\n");
    fflush(stdout);
    return 0;
}

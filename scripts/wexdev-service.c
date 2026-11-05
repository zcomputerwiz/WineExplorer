/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright 2026 WineExplorer contributors
 * One-shot service adapter. Compile with -municode -mwindows -ladvapi32.
 * Arguments: service-name output-file command-line-file (UTF-16LE).
 */
#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <wchar.h>

static SERVICE_STATUS_HANDLE status_handle;
static SERVICE_STATUS status = { SERVICE_WIN32_OWN_PROCESS, SERVICE_START_PENDING, 0, 0, 0, 0, 3000 };
static HANDLE child;
static wchar_t **args;

static DWORD WINAPI control(DWORD code, DWORD type, void *data, void *context)
{
    (void)type; (void)data; (void)context;
    if ((code == SERVICE_CONTROL_STOP || code == SERVICE_CONTROL_SHUTDOWN) && child)
        TerminateProcess(child, ERROR_CANCELLED);
    return NO_ERROR;
}

static void WINAPI service_main(DWORD argc, wchar_t **argv)
{
    STARTUPINFOW si = {0};
    PROCESS_INFORMATION pi = {0};
    SECURITY_ATTRIBUTES sa = {sizeof(sa), NULL, TRUE};
    HANDLE output, input;
    FILE *file;
    wchar_t *command = NULL, done[MAX_PATH];
    long size;
    DWORD rc = ERROR_INVALID_DATA, written;
    (void)argc; (void)argv;
    status_handle = RegisterServiceCtrlHandlerExW(args[1], control, NULL);
    if (!status_handle) return;
    status.dwCurrentState = SERVICE_RUNNING;
    status.dwControlsAccepted = SERVICE_ACCEPT_STOP | SERVICE_ACCEPT_SHUTDOWN;
    SetServiceStatus(status_handle, &status);
    output = CreateFileW(args[2], GENERIC_WRITE, FILE_SHARE_READ, &sa, CREATE_ALWAYS, 0, NULL);
    input = CreateFileW(L"NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, &sa, OPEN_EXISTING, 0, NULL);
    file = _wfopen(args[3], L"rb");
    if (file && !fseek(file, 0, SEEK_END) && (size = ftell(file)) > 0 && size < 65536 && !(size % 2))
    {
        rewind(file);
        command = calloc((size_t)size + sizeof(wchar_t), 1);
        if (command && fread(command, 1, (size_t)size, file) == (size_t)size && output != INVALID_HANDLE_VALUE)
        {
            si.cb = sizeof(si);
            si.dwFlags = STARTF_USESTDHANDLES;
            si.hStdOutput = si.hStdError = output;
            si.hStdInput = input;
            if (CreateProcessW(NULL, command, NULL, NULL, TRUE, CREATE_NO_WINDOW, NULL, NULL, &si, &pi))
            {
                child = pi.hProcess;
                CloseHandle(pi.hThread);
                WaitForSingleObject(child, INFINITE);
                GetExitCodeProcess(child, &rc);
                child = NULL;
                CloseHandle(pi.hProcess);
            }
            else rc = GetLastError();
        }
    }
    if (file) fclose(file);
    free(command);
    if (input != INVALID_HANDLE_VALUE) CloseHandle(input);
    if (output != INVALID_HANDLE_VALUE) { FlushFileBuffers(output); CloseHandle(output); }
    /* Publish completion only after all child output has been flushed. */
    if (swprintf(done, MAX_PATH, L"%ls.done", args[2]) > 0)
    {
        char result[32];
        int len = snprintf(result, sizeof(result), "%lu\n", (unsigned long)rc);
        HANDLE h = CreateFileW(done, GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_ALWAYS, 0, NULL);
        if (h != INVALID_HANDLE_VALUE)
        {
            WriteFile(h, result, (DWORD)len, &written, NULL);
            CloseHandle(h);
        }
    }
    status.dwCurrentState = SERVICE_STOPPED;
    status.dwControlsAccepted = 0;
    status.dwWin32ExitCode = rc ? ERROR_SERVICE_SPECIFIC_ERROR : NO_ERROR;
    status.dwServiceSpecificExitCode = rc;
    SetServiceStatus(status_handle, &status);
}

int wmain(int argc, wchar_t **argv)
{
    SERVICE_TABLE_ENTRYW table[] = {{NULL, service_main}, {NULL, NULL}};
    if (argc != 4) return 2;
    args = argv;
    table[0].lpServiceName = argv[1];
    return StartServiceCtrlDispatcherW(table) ? 0 : 1;
}

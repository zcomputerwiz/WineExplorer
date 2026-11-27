/*
 * WineExplorer Desktop Environment - probe for session 0 opening a session's process (X3, M3 waits and queries)
 *
 * Copyright 2026 WineExplorer Desktop Environment Project
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

/* Usage:
 *   probe sleeper FILE SECONDS CODE
 *       writes its process id to FILE (a Windows path), sleeps, and exits with CODE
 *   probe open PID ACCESS WAIT_MS
 *       OpenProcess(ACCESS, hex) of PID, then prints:
 *         open=ok | open=error E
 *         basic=PID PPID EXIT       (NtQueryInformationProcess, ProcessBasicInformation; or basic=error STATUS)
 *         image=NT NAME             (ProcessImageFileName) and image32=NAME (QueryFullProcessImageNameW)
 *         session=N                 (ProcessSessionInformation)
 *         times=CREATED EXITED      (ProcessTimes: nonzero creation time, exit time 0 while it runs)
 *         wow64=0|1                 (ProcessWow64Information)
 *         priority=CLASS            (GetPriorityClass)
 *         cmdline=STATUS            (ProcessCommandLineInformation: not served for a remote process)
 *         terminate=ok|error E      (TerminateProcess with code 99: not served)
 *         dupwide=ok|error E        (DuplicateHandle asking PROCESS_ALL_ACCESS)
 *         wait=W                    (WaitForSingleObject, WAIT_MS)
 *         exit=X                    (GetExitCodeProcess, hex)
 *         timesafter=CREATED EXITED (ProcessTimes again: the exit time is set) */

#include <windows.h>
#include <winternl.h>
#include <stdio.h>
#include <stdlib.h>

typedef struct
{
    ULONG SessionId;
} SESSION_INFO;

/* ProcessBasicInformation as ntdll fills it (mingw's winternl.h names the fields Reserved) */
typedef struct
{
    LONG      ExitStatus;
    void     *PebBaseAddress;
    ULONG_PTR AffinityMask;
    LONG      BasePriority;
    ULONG_PTR UniqueProcessId;
    ULONG_PTR InheritedFromUniqueProcessId;
} BASIC_INFO;

static void sleeper(const WCHAR *file, DWORD seconds, DWORD code)
{
    HANDLE h = CreateFileW(file, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, 0, NULL);
    char buf[32];
    DWORD len, written;

    if (h == INVALID_HANDLE_VALUE) ExitProcess(2);
    len = sprintf(buf, "%lu\n", GetCurrentProcessId());
    WriteFile(h, buf, len, &written, NULL);
    CloseHandle(h);
    Sleep(seconds * 1000);
    ExitProcess(code);
}

static void opener(DWORD pid, DWORD access, DWORD wait_ms)
{
    BASIC_INFO pbi;
    struct { LARGE_INTEGER CreateTime, ExitTime, KernelTime, UserTime; } times;
    SESSION_INFO session;
    ULONG_PTR wow64 = 0;
    BYTE image[1024];
    WCHAR image32[MAX_PATH];
    DWORD size, code, status;
    HANDLE h, dup;
    NTSTATUS st;

    if (!(h = OpenProcess(access, FALSE, pid)))
    {
        printf("open=error %lu\n", GetLastError());
        return;
    }
    printf("open=ok\n");
    if (!(st = NtQueryInformationProcess(h, ProcessBasicInformation, &pbi, sizeof(pbi), NULL)))
        printf("basic=%lu %lu %lx\n", (DWORD)pbi.UniqueProcessId, (DWORD)pbi.InheritedFromUniqueProcessId,
               (DWORD)pbi.ExitStatus);
    else printf("basic=error %lx\n", st);
    if (!(st = NtQueryInformationProcess(h, ProcessImageFileName, image, sizeof(image) - sizeof(WCHAR), NULL)))
    {
        UNICODE_STRING *name = (UNICODE_STRING *)image;
        printf("image=%.*ls\n", name->Length / (int)sizeof(WCHAR), name->Buffer);
    }
    else printf("image=error %lx\n", st);
    size = MAX_PATH;
    if (QueryFullProcessImageNameW(h, 0, image32, &size)) printf("image32=%ls\n", image32);
    else printf("image32=error %lu\n", GetLastError());
    if (!(st = NtQueryInformationProcess(h, ProcessSessionInformation, &session, sizeof(session), NULL)))
        printf("session=%lu\n", session.SessionId);
    else printf("session=error %lx\n", st);
    if (!(st = NtQueryInformationProcess(h, ProcessTimes, &times, sizeof(times), NULL)))
        printf("times=%s %s\n", times.CreateTime.QuadPart ? "set" : "0", times.ExitTime.QuadPart ? "set" : "0");
    else printf("times=error %lx\n", st);
    if (!(st = NtQueryInformationProcess(h, ProcessWow64Information, &wow64, sizeof(wow64), NULL)))
        printf("wow64=%d\n", wow64 != 0);
    else printf("wow64=error %lx\n", st);
    printf("priority=%lx\n", GetPriorityClass(h));
    {
        BYTE buf[512];
        st = NtQueryInformationProcess(h, 60 /* ProcessCommandLineInformation */, buf, sizeof(buf), NULL);
        printf("cmdline=%lx\n", st);
    }
    if (TerminateProcess(h, 99)) printf("terminate=ok\n");
    else printf("terminate=error %lu\n", GetLastError());
    if (DuplicateHandle(GetCurrentProcess(), h, GetCurrentProcess(), &dup, PROCESS_ALL_ACCESS, FALSE, 0))
    {
        printf("dupwide=ok\n");
        CloseHandle(dup);
    }
    else printf("dupwide=error %lu\n", GetLastError());
    status = WaitForSingleObject(h, wait_ms);
    printf("wait=%lu\n", status);
    if (GetExitCodeProcess(h, &code)) printf("exit=%lx\n", code);
    else printf("exit=error %lu\n", GetLastError());
    if (!(st = NtQueryInformationProcess(h, ProcessTimes, &times, sizeof(times), NULL)))
        printf("timesafter=%s %s\n", times.CreateTime.QuadPart ? "set" : "0", times.ExitTime.QuadPart ? "set" : "0");
    CloseHandle(h);
}

int wmain(int argc, WCHAR **argv)
{
    setvbuf(stdout, NULL, _IONBF, 0);
    if (argc == 5 && !wcscmp(argv[1], L"sleeper")) sleeper(argv[2], wcstoul(argv[3], NULL, 10), wcstoul(argv[4], NULL, 10));
    else if (argc == 5 && !wcscmp(argv[1], L"open")) opener(wcstoul(argv[2], NULL, 10), wcstoul(argv[3], NULL, 16),
                                                             wcstoul(argv[4], NULL, 10));
    else
    {
        printf("usage: probe sleeper FILE SECONDS CODE | probe open PID ACCESS WAIT_MS\n");
        return 1;
    }
    return 0;
}

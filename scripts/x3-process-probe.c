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
#include <sddl.h>
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

/* X4: the sleeper's buffer, larger than one read piece (60 KiB): a marker, then byte i = (i * 7) & 0xff */
#define MARK_SIZE (200 * 1024)
static const char marker[] = "WEXX4MARKER";

static void sleeper(const WCHAR *file, DWORD seconds, DWORD code)
{
    HANDLE h = CreateFileW(file, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, 0, NULL);
    unsigned char *mark = VirtualAlloc(NULL, MARK_SIZE, MEM_COMMIT, PAGE_READWRITE);
    char buf[64];
    DWORD len, written, i;

    if (h == INVALID_HANDLE_VALUE || !mark) ExitProcess(2);
    for (i = 0; i < MARK_SIZE; i++) mark[i] = (i * 7) & 0xff;
    memcpy(mark, marker, sizeof(marker));
    /* the process id, then (X4) the buffer's address */
    len = sprintf(buf, "%lu\n%Iu\n", GetCurrentProcessId(), (ULONG_PTR)mark);
    WriteFile(h, buf, len, &written, NULL);
    CloseHandle(h);
    Sleep(seconds * 1000);
    ExitProcess(code);
}

/* X4: read the sleeper's buffer (all of it, so several pieces), an unmapped address, and the read without VM_READ;
 * then its token: prints read=, readbad=, readnoright=, token=, sid=, tsession= */
static void readtoken(DWORD pid, ULONG_PTR addr)
{
    unsigned char *buf = malloc(MARK_SIZE), tu[256];
    HANDLE h, h2, token;
    SIZE_T got = 0;
    DWORD i, len, session = 0;
    LPSTR sid = NULL;

    if (!(h = OpenProcess(PROCESS_VM_READ | PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid)))
    {
        printf("open=error %lu\n", GetLastError());
        return;
    }
    printf("open=ok\n");
    if (ReadProcessMemory(h, (void *)addr, buf, MARK_SIZE, &got) && got == MARK_SIZE)
    {
        for (i = sizeof(marker); i < MARK_SIZE && buf[i] == ((i * 7) & 0xff); i++);
        printf("read=%s %Iu\n", !memcmp(buf, marker, sizeof(marker)) && i == MARK_SIZE ? "ok" : "wrong", got);
    }
    else printf("read=error %lu %Iu\n", GetLastError(), got);
    if (ReadProcessMemory(h, (void *)0x10, buf, 16, &got)) printf("readbad=ok\n");
    else printf("readbad=error %lu\n", GetLastError());
    if ((h2 = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid)))
    {
        if (ReadProcessMemory(h2, (void *)addr, buf, 16, &got)) printf("readnoright=ok\n");
        else printf("readnoright=error %lu\n", GetLastError());
        CloseHandle(h2);
    }
    if (OpenProcessToken(h, TOKEN_QUERY, &token))
    {
        printf("token=ok\n");
        if (GetTokenInformation(token, TokenUser, tu, sizeof(tu), &len) &&
            ConvertSidToStringSidA(((TOKEN_USER *)tu)->User.Sid, &sid))
        {
            printf("sid=%s\n", sid);
            LocalFree(sid);
        }
        if (GetTokenInformation(token, TokenSessionId, &session, sizeof(session), &len)) printf("tsession=%lu\n", session);
        CloseHandle(token);
    }
    else printf("token=error %lu\n", GetLastError());
    CloseHandle(h);
    free(buf);
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
    else if (argc == 4 && !wcscmp(argv[1], L"readtoken"))
        readtoken(wcstoul(argv[2], NULL, 10), (ULONG_PTR)_wcstoui64(argv[3], NULL, 10));
    else
    {
        printf("usage: probe sleeper FILE SECONDS CODE | probe open PID ACCESS WAIT_MS | probe readtoken PID ADDR\n");
        return 1;
    }
    return 0;
}

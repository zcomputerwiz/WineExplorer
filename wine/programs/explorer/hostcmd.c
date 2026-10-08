/*
 * WineExplorer Desktop Environment - Host command helpers
 *
 * Copyright 2026 WineExplorer Desktop Environment Project
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301, USA
 */

#include <stdio.h>
#include <string.h>

#include <windows.h>

#include "hostcmd.h"

/* Append one argument to a Windows command line, quoted by the rules CreateProcess/Wine parse back into argv. */
static void append_arg(char *cmdline, size_t size, const char *arg)
{
    size_t len = strlen(cmdline);
    const char *p;
    int backslashes;

    if (len + 3 >= size) return;
    if (len) cmdline[len++] = ' ';
    cmdline[len++] = '"';
    for (p = arg, backslashes = 0; *p && len + 3 < size; p++)
    {
        if (*p == '\\') { backslashes++; cmdline[len++] = *p; continue; }
        if (*p == '"')
        {
            while (backslashes-- > 0 && len + 3 < size) cmdline[len++] = '\\';
            cmdline[len++] = '\\';
        }
        backslashes = 0;
        cmdline[len++] = *p;
    }
    while (backslashes-- > 0 && len + 3 < size) cmdline[len++] = '\\';
    cmdline[len++] = '"';
    cmdline[len] = 0;
}

/* A bare program name is looked up in the usual Unix bin directories. */
static BOOL resolve_program(const char *name, char *out, size_t size)
{
    static const char *const dirs[] = { "/usr/bin/", "/usr/local/bin/", "/bin/", "/usr/sbin/", "/sbin/" };
    size_t i;

    if (strchr(name, '/')) { lstrcpynA(out, name, (int)size); return TRUE; }
    for (i = 0; i < sizeof(dirs) / sizeof(dirs[0]); i++)
    {
        FILE *f;
        snprintf(out, size, "%s%s", dirs[i], name);
        if ((f = fopen(out, "rb"))) { fclose(f); return TRUE; }
    }
    return FALSE;
}

HANDLE hostcmd_spawn(const char *const *argv, HANDLE *out_read)
{
    char cmdline[1024] = "", program[256];
    STARTUPINFOA si = { sizeof(si) };
    PROCESS_INFORMATION pi;
    HANDLE rd = NULL, wr = NULL, nul = INVALID_HANDLE_VALUE;
    SECURITY_ATTRIBUTES sa = { sizeof(sa), NULL, TRUE };
    int i;

    if (!resolve_program(argv[0], program, sizeof(program))) return NULL;
    append_arg(cmdline, sizeof(cmdline), program);
    for (i = 1; argv[i]; i++) append_arg(cmdline, sizeof(cmdline), argv[i]);

    si.dwFlags = STARTF_USESTDHANDLES;
    if (out_read)
    {
        if (!CreatePipe(&rd, &wr, &sa, 0)) return NULL;
        SetHandleInformation(rd, HANDLE_FLAG_INHERIT, 0);
        si.hStdOutput = wr;
    }
    else
    {
        nul = CreateFileA("NUL", GENERIC_WRITE, FILE_SHARE_WRITE, &sa, OPEN_EXISTING, 0, NULL);
        si.hStdOutput = nul;
    }
    si.hStdError = si.hStdOutput;
    si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);

    if (!CreateProcessA(NULL, cmdline, NULL, NULL, TRUE, CREATE_NO_WINDOW, NULL, NULL, &si, &pi))
    {
        if (rd) CloseHandle(rd);
        if (wr) CloseHandle(wr);
        if (nul != INVALID_HANDLE_VALUE) CloseHandle(nul);
        return NULL;
    }
    CloseHandle(pi.hThread);
    if (wr) CloseHandle(wr);
    if (nul != INVALID_HANDLE_VALUE) CloseHandle(nul);
    if (out_read) *out_read = rd;
    return pi.hProcess;
}

void hostcmd_run_async(const char *const *argv)
{
    HANDLE h = hostcmd_spawn(argv, NULL);
    if (h) CloseHandle(h);
}

BOOL hostcmd_capture(const char *const *argv, char *buf, DWORD size, DWORD timeout_ms)
{
    HANDLE rd, proc;
    DWORD total = 0, start = GetTickCount();

    buf[0] = 0;
    if (!(proc = hostcmd_spawn(argv, &rd))) return FALSE;

    for (;;)
    {
        DWORD avail = 0, got;
        BOOL done = (WaitForSingleObject(proc, 0) == WAIT_OBJECT_0);

        while (PeekNamedPipe(rd, NULL, 0, NULL, &avail, NULL) && avail && total < size - 1)
        {
            if (!ReadFile(rd, buf + total, min(avail, size - 1 - total), &got, NULL) || !got) break;
            total += got;
        }
        if (done || total >= size - 1) break;
        if (GetTickCount() - start > timeout_ms) { TerminateProcess(proc, 1); break; }
        Sleep(5);
    }
    buf[total] = 0;
    CloseHandle(rd);
    CloseHandle(proc);
    return total > 0;
}

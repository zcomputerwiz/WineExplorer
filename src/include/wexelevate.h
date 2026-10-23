/*
 * The elevation broker: request protocol and client (docs/ELEVATION_BROKER_WORKORDER.md).
 *
 * A caller that wants a process started with the full half of its administrator token sends one request to the
 * broker service (wexelevate.exe, a LocalSystem service) over the named pipe \\.\pipe\wexelevate and gets the
 * result of the start, with a process handle for the new process if it asked for one. The broker never reads the
 * identity of the caller from the request: the caller's process is the client of the pipe, as the server recorded
 * it, and the broker takes its token from the server.
 *
 * Request (one pipe message): struct wexelevate_request, then the WCHARs of the image path, the command line,
 * the current directory and the environment block (the lengths in the header count WCHARs, with no terminator
 * for the three strings; the block has its own terminators). The command line is the one a program gets
 * from GetCommandLineW, byte for byte. Reply: struct wexelevate_reply.
 *
 * Errors: ERROR_NOT_SUPPORTED (no system role, a standard user, or a caller that is elevated already),
 * ERROR_NOT_READY (the broker has no way to ask for consent), ERROR_CANCELLED (consent was refused),
 * ERROR_FILE_NOT_FOUND or ERROR_PATH_NOT_FOUND (no such image), ERROR_FILE_CORRUPT (the image changed after the
 * request was frozen), ERROR_BUSY (too many requests), ERROR_TIMEOUT, ERROR_INVALID_PARAMETER, or the error of
 * CreateProcessAsUser.
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

#ifndef __WINE_WEXELEVATE_H
#define __WINE_WEXELEVATE_H

#include <windows.h>
#include <winsvc.h>

#define WEXELEVATE_PIPE_NAME      L"\\\\.\\pipe\\wexelevate"
#define WEXELEVATE_SERVICE        L"wexelevate"

#define WEXELEVATE_MAGIC          0x4c455857  /* "WXEL" */
#define WEXELEVATE_VERSION        1
#define WEXELEVATE_MAX_REQUEST    0x100000    /* bytes of one request */
#define WEXELEVATE_MAX_PATH_CHARS 32767
#define WEXELEVATE_MAX_ENV_CHARS  0x20000

#define WEXELEVATE_WANT_HANDLE    0x1  /* duplicate a process handle into the caller */
#define WEXELEVATE_ENV_INHERIT    0x2  /* the request carries the environment block; else the target user's default */
#define WEXELEVATE_FLAGS_MASK     0x3

struct wexelevate_request
{
    DWORD magic;
    DWORD version;
    DWORD flags;
    DWORD show;           /* SW_* for the first window */
    DWORD advisory_pid;   /* ignored: the broker attributes the request to the process that opened the pipe */
    DWORD image_len;      /* WCHARs */
    DWORD cmdline_len;
    DWORD cwd_len;
    DWORD env_len;        /* WCHARs of the block, terminators included */
};

struct wexelevate_reply
{
    DWORD magic;
    DWORD error;          /* Win32 error, 0 on success */
    DWORD pid;
    DWORD tid;
    ULONGLONG handle;     /* in the caller's process, 0 unless WEXELEVATE_WANT_HANDLE */
};

#ifndef WEXELEVATE_NO_CLIENT

/* The broker's pipe is trusted only if the process at its other end is the process the service control manager
 * says is the running service: another program of the user could have created the pipe first. */
static inline DWORD wexelevate_connect( HANDLE *pipe )
{
    SC_HANDLE scm, svc = NULL;
    SERVICE_STATUS_PROCESS status;
    DWORD err = ERROR_SUCCESS, size, i, server_pid;
    HANDLE file = INVALID_HANDLE_VALUE;

    *pipe = INVALID_HANDLE_VALUE;
    if (!(scm = OpenSCManagerW( NULL, NULL, SC_MANAGER_CONNECT ))) return GetLastError();
    if (!(svc = OpenServiceW( scm, WEXELEVATE_SERVICE, SERVICE_QUERY_STATUS | SERVICE_START )))
    {
        err = GetLastError();
        CloseServiceHandle( scm );
        return err;
    }
    for (i = 0; i < 60; i++)
    {
        if (!QueryServiceStatusEx( svc, SC_STATUS_PROCESS_INFO, (BYTE *)&status, sizeof(status), &size ))
        {
            err = GetLastError();
            break;
        }
        if (status.dwCurrentState == SERVICE_RUNNING) break;
        if (status.dwCurrentState == SERVICE_STOPPED && !StartServiceW( svc, 0, NULL ) &&
            GetLastError() != ERROR_SERVICE_ALREADY_RUNNING)
        {
            err = GetLastError();
            break;
        }
        Sleep( 500 );
    }
    if (!err && status.dwCurrentState != SERVICE_RUNNING) err = ERROR_SERVICE_NOT_ACTIVE;
    CloseServiceHandle( svc );
    CloseServiceHandle( scm );
    if (err) return err;

    for (i = 0; i < 20; i++)
    {
        file = CreateFileW( WEXELEVATE_PIPE_NAME, GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING,
                            SECURITY_SQOS_PRESENT | SECURITY_IDENTIFICATION, NULL );
        if (file != INVALID_HANDLE_VALUE) break;
        err = GetLastError();
        if (err == ERROR_PIPE_BUSY) WaitNamedPipeW( WEXELEVATE_PIPE_NAME, 2000 );
        else if (err == ERROR_FILE_NOT_FOUND) Sleep( 250 );
        else return err;
    }
    if (file == INVALID_HANDLE_VALUE) return err;
    if (!GetNamedPipeServerProcessId( file, &server_pid ) || server_pid != status.dwProcessId)
    {
        CloseHandle( file );
        return ERROR_ACCESS_DENIED;
    }
    *pipe = file;
    return ERROR_SUCCESS;
}

/* Ask the broker to start image with the command line cmdline (the string the program will see) elevated.
 * env is a double-NUL-terminated block of env_len WCHARs, or NULL for the default environment of the target user.
 * With WEXELEVATE_WANT_HANDLE, *process gets a handle with SYNCHRONIZE and PROCESS_QUERY_LIMITED_INFORMATION. */
static inline DWORD wexelevate_run( const WCHAR *image, const WCHAR *cmdline, const WCHAR *cwd, const WCHAR *env,
                                    DWORD env_len, DWORD show, DWORD flags, HANDLE *process, DWORD *pid )
{
    struct wexelevate_request *req;
    struct wexelevate_reply reply;
    DWORD image_len = lstrlenW( image ), cmdline_len = cmdline ? lstrlenW( cmdline ) : 0;
    DWORD cwd_len = cwd ? lstrlenW( cwd ) : 0, size, done, err;
    WCHAR *data;
    HANDLE pipe;

    if (process) *process = NULL;
    if (pid) *pid = 0;
    if (!env) env_len = 0;
    flags &= WEXELEVATE_FLAGS_MASK;
    if (env) flags |= WEXELEVATE_ENV_INHERIT; else flags &= ~WEXELEVATE_ENV_INHERIT;
    if (!image_len || image_len > WEXELEVATE_MAX_PATH_CHARS || cmdline_len > WEXELEVATE_MAX_PATH_CHARS ||
        cwd_len > WEXELEVATE_MAX_PATH_CHARS || env_len > WEXELEVATE_MAX_ENV_CHARS) return ERROR_INVALID_PARAMETER;
    size = sizeof(*req) + (image_len + cmdline_len + cwd_len + env_len) * sizeof(WCHAR);
    if (!(req = HeapAlloc( GetProcessHeap(), HEAP_ZERO_MEMORY, size ))) return ERROR_NOT_ENOUGH_MEMORY;
    req->magic = WEXELEVATE_MAGIC;
    req->version = WEXELEVATE_VERSION;
    req->flags = flags;
    req->show = show;
    req->image_len = image_len;
    req->cmdline_len = cmdline_len;
    req->cwd_len = cwd_len;
    req->env_len = env_len;
    data = (WCHAR *)(req + 1);
    CopyMemory( data, image, image_len * sizeof(WCHAR) ); data += image_len;
    if (cmdline_len) CopyMemory( data, cmdline, cmdline_len * sizeof(WCHAR) );
    data += cmdline_len;
    if (cwd_len) CopyMemory( data, cwd, cwd_len * sizeof(WCHAR) );
    data += cwd_len;
    if (env_len) CopyMemory( data, env, env_len * sizeof(WCHAR) );

    if ((err = wexelevate_connect( &pipe )))
    {
        HeapFree( GetProcessHeap(), 0, req );
        return err;
    }
    err = ERROR_SUCCESS;
    if (!WriteFile( pipe, req, size, &done, NULL ) || done != size) err = GetLastError() ? GetLastError() : ERROR_WRITE_FAULT;
    else if (!ReadFile( pipe, &reply, sizeof(reply), &done, NULL ) || done != sizeof(reply) || reply.magic != WEXELEVATE_MAGIC)
        err = GetLastError() ? GetLastError() : ERROR_INVALID_DATA;
    else
    {
        err = reply.error;
        if (!err)
        {
            if (pid) *pid = reply.pid;
            if (process) *process = (HANDLE)(ULONG_PTR)reply.handle;
            else if (reply.handle) CloseHandle( (HANDLE)(ULONG_PTR)reply.handle );
        }
    }
    CloseHandle( pipe );
    HeapFree( GetProcessHeap(), 0, req );
    return err;
}

#endif  /* WEXELEVATE_NO_CLIENT */

#endif  /* __WINE_WEXELEVATE_H */

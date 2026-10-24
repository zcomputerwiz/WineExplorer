/*
 * The elevation broker, the AppInfo role (docs/ELEVATION_BOUNDARY_PLAN.md D3, docs/ELEVATION_BROKER_WORKORDER.md).
 *
 * A LocalSystem service (so it holds SeTcbPrivilege through the system role) that starts processes with the
 * full half of an administrator's token on behalf of a caller. It listens on \\.\pipe\wexelevate; every connection
 * is one independent request (wexelevate.h). It
 *  - takes the caller's identity from the server (the process that opened the pipe), never from the request;
 *  - freezes the request: opens the image, records its volume, file index, size, time and SHA-256, and checks all of
 *    them again at the launch;
 *  - holds a Linux read lease on the image from before the hash until the elevated process exits (wexhost's
 *    WexLease*): a writer who opens the file makes the pending request fail with ERROR_FILE_CORRUPT, or ends the
 *    running elevated process (only that process) and then goes on;
 *  - gives the request a random single-use id, which is destroyed when the request ends, fails or the caller exits;
 *  - asks for approval (the shipping broker has no way to ask yet and fails closed; a build with
 *    WEXELEVATE_TEST_APPROVAL calls a stub program, which exists in the test harness only);
 *  - gets the linked full token of the caller's token as a primary token (it can, as the system role), creates
 *    the process with the caller as its parent and duplicates the process handle into the caller.
 * A standard user, or a caller that is elevated already, gets ERROR_NOT_SUPPORTED. If the service does not run
 * as SYSTEM (no launcher: the prefix is user-writable) every request gets ERROR_NOT_SUPPORTED.
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

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <winsvc.h>
#include <sddl.h>
#include <userenv.h>
#include <bcrypt.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

#include "wexelevate.h"

#ifndef NT_SUCCESS
#define NT_SUCCESS(s) ((LONG)(s) >= 0)
#endif

#define MAX_REQUESTS      32   /* pending at once */
#define MAX_PER_CALLER    4
#define READ_TIMEOUT      10000
#define APPROVAL_TIMEOUT  120000

#ifdef WEXELEVATE_TEST_APPROVAL
/* Test harness only. The stub gets the request id and the image path, and exits with 0 to approve. */
#define TEST_STUB L"C:\\windows\\system32\\wexelevate_stub.exe"
#endif

struct request
{
    ULONGLONG id;
    HANDLE pipe;
    DWORD caller_pid;
    HANDLE caller;        /* process handle of the caller */
    HANDLE caller_token;  /* its primary token */
    DWORD flags, show;
    WCHAR *image, *cmdline, *cwd, *env;
    DWORD env_len;
    HANDLE image_file;    /* held open with FILE_SHARE_READ from the freeze to the launch */
    UINT64 lease;         /* read lease on the image (0: none needed), see lease_* below */
    HANDLE lease_event;   /* set when the lease broke */
    BY_HANDLE_FILE_INFORMATION info;
    BYTE hash[32];
    int live;             /* in the table: the id can be used once */
};

static SERVICE_STATUS_HANDLE status_handle;
static HANDLE stop_event;
static BOOL system_role;  /* running as SYSTEM with SeTcbPrivilege enabled */
static CRITICAL_SECTION table_cs;
static struct request *table[MAX_REQUESTS];
static LONG active_threads;

/* ---- the read lease on the image (wexhost's Unix side) ---------------------------------------------------------- */

static DWORD (WINAPI *pLeaseTake)( HANDLE, HANDLE, UINT64 * );
static DWORD (WINAPI *pLeaseArm)( UINT64, DWORD );
static DWORD (WINAPI *pLeaseRelease)( UINT64, BOOL * );

static BOOL load_wexhost(void)
{
    WCHAR path[MAX_PATH + 16];
    HMODULE mod;
    UINT len = GetSystemDirectoryW( path, MAX_PATH );

    if (!len || len >= MAX_PATH) return FALSE;
    lstrcpyW( path + len, L"\\wexhost.dll" );
    if (!(mod = LoadLibraryW( path ))) return FALSE;
    pLeaseTake = (void *)GetProcAddress( mod, "WexLeaseTake" );
    pLeaseArm = (void *)GetProcAddress( mod, "WexLeaseArm" );
    pLeaseRelease = (void *)GetProcAddress( mod, "WexLeaseRelease" );
    return pLeaseTake && pLeaseArm && pLeaseRelease;
}

/* the lease lives as long as the elevated process: this thread gives it up when the process has exited */
struct guard { UINT64 lease; HANDLE event, process; };

static DWORD WINAPI guard_thread( void *param )
{
    struct guard *g = param;

    WaitForSingleObject( g->process, INFINITE );
    pLeaseRelease( g->lease, NULL );
    CloseHandle( g->event );
    CloseHandle( g->process );
    free( g );
    return 0;
}

/* the guard takes over the lease and the process handle; on failure the caller still owns them */
static BOOL start_guard( struct request *req, HANDLE process )
{
    struct guard *g = malloc( sizeof(*g) );
    HANDLE thread;

    if (!g) return FALSE;
    g->lease = req->lease;
    g->event = req->lease_event;
    g->process = process;
    if (!(thread = CreateThread( NULL, 0, guard_thread, g, 0, NULL )))
    {
        free( g );
        return FALSE;
    }
    CloseHandle( thread );
    req->lease = 0;
    req->lease_event = NULL;
    return TRUE;
}

/* ---- the table of requests ----------------------------------------------------------------------------------- */

static ULONGLONG random_id(void)
{
    ULONGLONG id = 0;

    while (!id)
    {
        if (!NT_SUCCESS(BCryptGenRandom( NULL, (UCHAR *)&id, sizeof(id), BCRYPT_USE_SYSTEM_PREFERRED_RNG )))
            id = ((ULONGLONG)GetTickCount() << 32) ^ GetCurrentThreadId() ^ (ULONGLONG)(ULONG_PTR)&id;
    }
    return id;
}

/* put a request into the table; fails with ERROR_BUSY at the limits */
static DWORD table_add( struct request *req )
{
    DWORD err = ERROR_BUSY, i, same = 0;

    EnterCriticalSection( &table_cs );
    for (i = 0; i < MAX_REQUESTS; i++) if (table[i] && table[i]->caller_pid == req->caller_pid) same++;
    if (same < MAX_PER_CALLER)
    {
        for (i = 0; i < MAX_REQUESTS; i++)
        {
            if (table[i]) continue;
            table[i] = req;
            req->id = random_id();
            req->live = 1;
            err = ERROR_SUCCESS;
            break;
        }
    }
    LeaveCriticalSection( &table_cs );
    return err;
}

/* the single use of an id: it is found once, and gone afterwards */
static BOOL table_consume( struct request *req )
{
    BOOL found = FALSE;
    DWORD i;

    EnterCriticalSection( &table_cs );
    for (i = 0; i < MAX_REQUESTS; i++)
    {
        if (table[i] != req || !req->live || table[i]->id != req->id) continue;
        found = TRUE;
        req->live = 0;
        table[i] = NULL;
        break;
    }
    LeaveCriticalSection( &table_cs );
    return found;
}

static void table_remove( struct request *req )
{
    DWORD i;

    EnterCriticalSection( &table_cs );
    for (i = 0; i < MAX_REQUESTS; i++) if (table[i] == req) table[i] = NULL;
    req->live = 0;
    req->id = 0;
    LeaveCriticalSection( &table_cs );
}

static void free_request( struct request *req )
{
    table_remove( req );
    if (req->lease) pLeaseRelease( req->lease, NULL );
    if (req->lease_event) CloseHandle( req->lease_event );
    if (req->image_file != INVALID_HANDLE_VALUE) CloseHandle( req->image_file );
    if (req->caller_token) CloseHandle( req->caller_token );
    if (req->caller) CloseHandle( req->caller );
    free( req->image );
    free( req->cmdline );
    free( req->cwd );
    free( req->env );
    free( req );
}

/* ---- pipe I/O with a timeout --------------------------------------------------------------------------------- */

static DWORD pipe_io( HANDLE pipe, void *buf, DWORD size, BOOL write, DWORD timeout, DWORD *done )
{
    OVERLAPPED ov = { 0 };
    HANDLE events[2];
    DWORD err = ERROR_SUCCESS, ret;

    *done = 0;
    if (!(ov.hEvent = CreateEventW( NULL, TRUE, FALSE, NULL ))) return GetLastError();
    if (!(write ? WriteFile( pipe, buf, size, NULL, &ov ) : ReadFile( pipe, buf, size, NULL, &ov )))
    {
        err = GetLastError();
        if (err == ERROR_IO_PENDING)
        {
            events[0] = ov.hEvent;
            events[1] = stop_event;
            ret = WaitForMultipleObjects( 2, events, FALSE, timeout );
            if (ret != WAIT_OBJECT_0)
            {
                CancelIo( pipe );
                GetOverlappedResult( pipe, &ov, done, TRUE );
                CloseHandle( ov.hEvent );
                *done = 0;
                return ret == WAIT_TIMEOUT ? ERROR_TIMEOUT : ERROR_OPERATION_ABORTED;
            }
            err = ERROR_SUCCESS;
        }
        else
        {
            CloseHandle( ov.hEvent );
            return err;
        }
    }
    if (!GetOverlappedResult( pipe, &ov, done, TRUE )) err = GetLastError();
    CloseHandle( ov.hEvent );
    return err;
}

/* ---- the caller ---------------------------------------------------------------------------------------------- */

static BOOL token_user_sid( HANDLE token, BYTE *buf, DWORD size )
{
    DWORD len;
    return GetTokenInformation( token, TokenUser, buf, size, &len );
}

static BOOL token_is_system_with_tcb(void)
{
    BYTE user[sizeof(TOKEN_USER) + SECURITY_MAX_SID_SIZE], privs[1024];
    PSID system_sid = NULL;
    HANDLE token;
    DWORD len, i;
    BOOL ret = FALSE;

    if (!OpenProcessToken( GetCurrentProcess(), TOKEN_QUERY, &token )) return FALSE;
    if (ConvertStringSidToSidW( L"S-1-5-18", &system_sid ))
    {
        if (token_user_sid( token, user, sizeof(user) ) && EqualSid( ((TOKEN_USER *)user)->User.Sid, system_sid ) &&
            GetTokenInformation( token, TokenPrivileges, privs, sizeof(privs), &len ))
        {
            TOKEN_PRIVILEGES *p = (TOKEN_PRIVILEGES *)privs;
            LUID tcb;

            if (LookupPrivilegeValueW( NULL, L"SeTcbPrivilege", &tcb ))
                for (i = 0; i < p->PrivilegeCount; i++)
                    if (p->Privileges[i].Luid.LowPart == tcb.LowPart && p->Privileges[i].Luid.HighPart == tcb.HighPart &&
                        (p->Privileges[i].Attributes & SE_PRIVILEGE_ENABLED)) ret = TRUE;
        }
        LocalFree( system_sid );
    }
    CloseHandle( token );
    return ret;
}

/* Who is the caller: the process that opened the pipe, as the server recorded it (the request carries no
 * identity), with the token of that process. The user the pipe reports for the client thread must be the user of
 * the process. A caller that is a standard user or elevated already is not served here. */
static DWORD identify_caller( struct request *req )
{
    BYTE pipe_user[sizeof(TOKEN_USER) + SECURITY_MAX_SID_SIZE], proc_user[sizeof(TOKEN_USER) + SECURITY_MAX_SID_SIZE];
    HANDLE thread_token = NULL;
    DWORD pid = 0, pid2 = 0, elevation, len;

    if (!GetNamedPipeClientProcessId( req->pipe, &pid ) || !pid) return ERROR_ACCESS_DENIED;
    if (!ImpersonateNamedPipeClient( req->pipe )) return ERROR_ACCESS_DENIED;
    if (!OpenThreadToken( GetCurrentThread(), TOKEN_QUERY, TRUE, &thread_token ))
    {
        RevertToSelf();
        return ERROR_ACCESS_DENIED;
    }
    RevertToSelf();
    if (!token_user_sid( thread_token, pipe_user, sizeof(pipe_user) ))
    {
        CloseHandle( thread_token );
        return ERROR_ACCESS_DENIED;
    }
    CloseHandle( thread_token );

    req->caller_pid = pid;
    if (!(req->caller = OpenProcess( PROCESS_QUERY_INFORMATION | PROCESS_QUERY_LIMITED_INFORMATION | PROCESS_DUP_HANDLE |
                                     PROCESS_CREATE_PROCESS | SYNCHRONIZE, FALSE, pid ))) return ERROR_ACCESS_DENIED;
    if (!OpenProcessToken( req->caller, TOKEN_QUERY | TOKEN_DUPLICATE, &req->caller_token )) return ERROR_ACCESS_DENIED;
    if (!token_user_sid( req->caller_token, proc_user, sizeof(proc_user) ) ||
        !EqualSid( ((TOKEN_USER *)pipe_user)->User.Sid, ((TOKEN_USER *)proc_user)->User.Sid )) return ERROR_ACCESS_DENIED;
    /* the pid must still be the client's, and the process must not have gone away meanwhile */
    if (!GetNamedPipeClientProcessId( req->pipe, &pid2 ) || pid2 != pid || WaitForSingleObject( req->caller, 0 ) != WAIT_TIMEOUT)
        return ERROR_ACCESS_DENIED;
    if (!GetTokenInformation( req->caller_token, TokenElevationType, &elevation, sizeof(elevation), &len )) return ERROR_ACCESS_DENIED;
    /* only the limited half of a split administrator token has something to elevate to */
    return elevation == TokenElevationTypeLimited ? ERROR_SUCCESS : ERROR_NOT_SUPPORTED;
}

/* ---- parsing and freezing ------------------------------------------------------------------------------------ */

static WCHAR *copy_string( const WCHAR *src, DWORD len )
{
    WCHAR *s = malloc( (len + 1) * sizeof(WCHAR) );

    if (!s) return NULL;
    memcpy( s, src, len * sizeof(WCHAR) );
    s[len] = 0;
    return s;
}

static BOOL has_nul( const WCHAR *s, DWORD len )
{
    DWORD i;
    for (i = 0; i < len; i++) if (!s[i]) return TRUE;
    return FALSE;
}

static DWORD parse_request( struct request *req, const BYTE *buf, DWORD size )
{
    const struct wexelevate_request *hdr = (const void *)buf;
    const WCHAR *data;
    ULONGLONG total;

    if (size < sizeof(*hdr) || hdr->magic != WEXELEVATE_MAGIC || hdr->version != WEXELEVATE_VERSION) return ERROR_INVALID_PARAMETER;
    if (hdr->flags & ~WEXELEVATE_FLAGS_MASK) return ERROR_INVALID_PARAMETER;
    if (!hdr->image_len || hdr->image_len > WEXELEVATE_MAX_PATH_CHARS || hdr->cmdline_len > WEXELEVATE_MAX_PATH_CHARS ||
        hdr->cwd_len > WEXELEVATE_MAX_PATH_CHARS || hdr->env_len > WEXELEVATE_MAX_ENV_CHARS) return ERROR_INVALID_PARAMETER;
    total = (ULONGLONG)hdr->image_len + hdr->cmdline_len + hdr->cwd_len + hdr->env_len;
    if (size != sizeof(*hdr) + total * sizeof(WCHAR)) return ERROR_INVALID_PARAMETER;
    if (!!(hdr->flags & WEXELEVATE_ENV_INHERIT) != !!hdr->env_len) return ERROR_INVALID_PARAMETER;

    data = (const WCHAR *)(hdr + 1);
    if (has_nul( data, hdr->image_len )) return ERROR_INVALID_PARAMETER;
    if (has_nul( data + hdr->image_len, hdr->cmdline_len )) return ERROR_INVALID_PARAMETER;
    if (has_nul( data + hdr->image_len + hdr->cmdline_len, hdr->cwd_len )) return ERROR_INVALID_PARAMETER;
    if (hdr->env_len)
    {
        const WCHAR *env = data + hdr->image_len + hdr->cmdline_len + hdr->cwd_len;
        if (hdr->env_len < 2 || env[hdr->env_len - 1] || env[hdr->env_len - 2]) return ERROR_INVALID_PARAMETER;
    }
    /* an absolute path with a drive letter, not relative to anything the broker has */
    if (hdr->image_len < 4 || !((data[0] >= 'A' && data[0] <= 'Z') || (data[0] >= 'a' && data[0] <= 'z')) ||
        data[1] != ':' || (data[2] != '\\' && data[2] != '/')) return ERROR_INVALID_PARAMETER;

    req->flags = hdr->flags;
    req->show = hdr->show;
    req->env_len = hdr->env_len;
    req->image = copy_string( data, hdr->image_len );
    req->cmdline = copy_string( data + hdr->image_len, hdr->cmdline_len );
    req->cwd = hdr->cwd_len ? copy_string( data + hdr->image_len + hdr->cmdline_len, hdr->cwd_len ) : NULL;
    if (hdr->env_len)
    {
        req->env = malloc( hdr->env_len * sizeof(WCHAR) );
        if (req->env) memcpy( req->env, data + hdr->image_len + hdr->cmdline_len + hdr->cwd_len, hdr->env_len * sizeof(WCHAR) );
    }
    if (!req->image || !req->cmdline || (hdr->cwd_len && !req->cwd) || (hdr->env_len && !req->env))
        return ERROR_NOT_ENOUGH_MEMORY;
    return ERROR_SUCCESS;
}

static DWORD hash_file( HANDLE file, BYTE *out )
{
    BCRYPT_ALG_HANDLE alg = NULL;
    BCRYPT_HASH_HANDLE hash = NULL;
    BYTE *buf;
    DWORD got, err = ERROR_SUCCESS;
    LARGE_INTEGER zero = { 0 };

    if (!(buf = malloc( 65536 ))) return ERROR_NOT_ENOUGH_MEMORY;
    if (!SetFilePointerEx( file, zero, NULL, FILE_BEGIN ) ||
        !NT_SUCCESS(BCryptOpenAlgorithmProvider( &alg, BCRYPT_SHA256_ALGORITHM, NULL, 0 )) ||
        !NT_SUCCESS(BCryptCreateHash( alg, &hash, NULL, 0, NULL, 0, 0 ))) err = ERROR_GEN_FAILURE;
    while (!err)
    {
        if (!ReadFile( file, buf, 65536, &got, NULL )) err = GetLastError();
        else if (!got) break;
        else if (!NT_SUCCESS(BCryptHashData( hash, buf, got, 0 ))) err = ERROR_GEN_FAILURE;
    }
    if (!err && !NT_SUCCESS(BCryptFinishHash( hash, out, 32, 0 ))) err = ERROR_GEN_FAILURE;
    if (hash) BCryptDestroyHash( hash );
    if (alg) BCryptCloseAlgorithmProvider( alg, 0 );
    free( buf );
    return err;
}

static BOOL same_file_info( const BY_HANDLE_FILE_INFORMATION *a, const BY_HANDLE_FILE_INFORMATION *b )
{
    return a->dwVolumeSerialNumber == b->dwVolumeSerialNumber && a->nFileIndexHigh == b->nFileIndexHigh &&
           a->nFileIndexLow == b->nFileIndexLow && a->nFileSizeHigh == b->nFileSizeHigh &&
           a->nFileSizeLow == b->nFileSizeLow && a->ftLastWriteTime.dwLowDateTime == b->ftLastWriteTime.dwLowDateTime &&
           a->ftLastWriteTime.dwHighDateTime == b->ftLastWriteTime.dwHighDateTime;
}

/* open the image by its path (no write or delete sharing for the others), record what it is */
static DWORD freeze_image( struct request *req )
{
    DWORD err;

    req->image_file = CreateFileW( req->image, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING,
                                   FILE_ATTRIBUTE_NORMAL, NULL );
    if (req->image_file == INVALID_HANDLE_VALUE) return GetLastError();
    if (GetFileType( req->image_file ) != FILE_TYPE_DISK || !GetFileInformationByHandle( req->image_file, &req->info ) ||
        (req->info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) return ERROR_FILE_NOT_FOUND;
    /* the lease first, then the hash: from here on the bytes cannot change without the broker noticing */
    if (!pLeaseTake) return ERROR_FILE_SYSTEM_LIMITATION;
    if (!(req->lease_event = CreateEventW( NULL, TRUE, FALSE, NULL ))) return GetLastError();
    if ((err = pLeaseTake( req->image_file, req->lease_event, &req->lease ))) return err;
    if ((err = hash_file( req->image_file, req->hash ))) return err;
    return ERROR_SUCCESS;
}

/* at the launch: the path leads to the file that was frozen, and it has not changed */
static DWORD verify_image( struct request *req )
{
    BY_HANDLE_FILE_INFORMATION now, held;
    BYTE hash[32];
    HANDLE file;
    DWORD err;

    file = CreateFileW( req->image, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_DELETE, NULL, OPEN_EXISTING,
                        FILE_ATTRIBUTE_NORMAL, NULL );
    if (file == INVALID_HANDLE_VALUE) return ERROR_FILE_CORRUPT;
    if (!GetFileInformationByHandle( file, &now ) || !GetFileInformationByHandle( req->image_file, &held ) ||
        !same_file_info( &now, &req->info ) || !same_file_info( &held, &req->info ))
    {
        CloseHandle( file );
        return ERROR_FILE_CORRUPT;
    }
    err = hash_file( file, hash );
    CloseHandle( file );
    if (err) return err;
    return memcmp( hash, req->hash, sizeof(hash) ) ? ERROR_FILE_CORRUPT : ERROR_SUCCESS;
}

/* ---- approval ------------------------------------------------------------------------------------------------ */

#ifdef WEXELEVATE_TEST_APPROVAL
static DWORD approve( struct request *req )
{
    WCHAR cmd[2 * MAX_PATH + 64];
    STARTUPINFOW si = { sizeof(si) };
    PROCESS_INFORMATION pi;
    HANDLE handles[4];
    DWORD ret, code, n;

    if (wcschr( req->image, '"' )) return ERROR_INVALID_PARAMETER;
    if (lstrlenW( req->image ) > MAX_PATH) return ERROR_FILENAME_EXCED_RANGE;
    wsprintfW( cmd, L"\"%s\" %08lx%08lx \"%s\"", TEST_STUB, (DWORD)(req->id >> 32), (DWORD)req->id, req->image );
    if (!CreateProcessW( TEST_STUB, cmd, NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi )) return GetLastError();
    CloseHandle( pi.hThread );
    handles[0] = pi.hProcess;
    handles[1] = req->caller;   /* the request is destroyed when the caller exits */
    handles[2] = stop_event;
    handles[3] = req->lease_event;   /* a writer opened the image */
    n = 4;
    ret = WaitForMultipleObjects( n, handles, FALSE, APPROVAL_TIMEOUT );
    if (ret != WAIT_OBJECT_0)
    {
        TerminateProcess( pi.hProcess, 1 );
        CloseHandle( pi.hProcess );
        return ret == WAIT_TIMEOUT ? ERROR_TIMEOUT : ret == WAIT_OBJECT_0 + 3 ? ERROR_FILE_CORRUPT : ERROR_CANCELLED;
    }
    code = 1;
    GetExitCodeProcess( pi.hProcess, &code );
    CloseHandle( pi.hProcess );
    return code == 0 ? ERROR_SUCCESS : ERROR_CANCELLED;
}
#else
/* No consent exists yet (docs/ELEVATION_BOUNDARY_PLAN.md steps 5 and 6): fail closed. */
static DWORD approve( struct request *req )
{
    (void)req;
    return ERROR_NOT_READY;
}
#endif

/* ---- the launch ---------------------------------------------------------------------------------------------- */

static DWORD launch( struct request *req, DWORD *pid, DWORD *tid, ULONGLONG *handle )
{
    STARTUPINFOEXW si = { { sizeof(si) } };
    PROCESS_INFORMATION pi = { 0 };
    SIZE_T attr_size = 0;
    HANDLE linked = NULL, remote = NULL;
    WCHAR *cmdline = NULL;
    void *default_env = NULL;
    void *env;
    DWORD err = ERROR_SUCCESS, len, type;

    if (!GetTokenInformation( req->caller_token, TokenLinkedToken, &linked, sizeof(linked), &len ))
    {
        err = GetLastError();
        return err == ERROR_NO_SUCH_LOGON_SESSION ? ERROR_NOT_SUPPORTED : err;
    }
    /* the broker needs a usable primary token of the full half; an identification token is no use */
    if (!GetTokenInformation( linked, TokenType, &type, sizeof(type), &len ) || type != TokenPrimary)
    {
        CloseHandle( linked );
        return ERROR_NOT_SUPPORTED;
    }
    if (!GetTokenInformation( linked, TokenElevationType, &type, sizeof(type), &len ) || type != TokenElevationTypeFull)
    {
        CloseHandle( linked );
        return ERROR_NOT_SUPPORTED;
    }

    if (req->flags & WEXELEVATE_ENV_INHERIT) env = req->env;
    else if (!CreateEnvironmentBlock( &default_env, linked, FALSE ))
    {
        err = GetLastError();
        CloseHandle( linked );
        return err ? err : ERROR_ENVVAR_NOT_FOUND;
    }
    else env = default_env;

    InitializeProcThreadAttributeList( NULL, 1, 0, &attr_size );
    if (!(si.lpAttributeList = malloc( attr_size )) || !InitializeProcThreadAttributeList( si.lpAttributeList, 1, 0, &attr_size ))
    {
        err = ERROR_NOT_ENOUGH_MEMORY;
        goto done;
    }
    if (!UpdateProcThreadAttribute( si.lpAttributeList, 0, PROC_THREAD_ATTRIBUTE_PARENT_PROCESS, &req->caller,
                                    sizeof(HANDLE), NULL, NULL ))
    {
        err = GetLastError();
        goto done;
    }
    si.StartupInfo.dwFlags = STARTF_USESHOWWINDOW;
    si.StartupInfo.wShowWindow = (WORD)req->show;
    /* CreateProcess may write to the command line */
    cmdline = malloc( (lstrlenW( req->cmdline ) + 1) * sizeof(WCHAR) );
    if (!cmdline)
    {
        err = ERROR_NOT_ENOUGH_MEMORY;
        goto done;
    }
    lstrcpyW( cmdline, req->cmdline );

    if (!CreateProcessAsUserW( linked, req->image, cmdline, NULL, NULL, FALSE,
                               EXTENDED_STARTUPINFO_PRESENT | CREATE_UNICODE_ENVIRONMENT | CREATE_NEW_CONSOLE,
                               env, req->cwd, &si.StartupInfo, &pi ))
    {
        err = GetLastError();
        goto done;
    }
    *pid = pi.dwProcessId;
    *tid = pi.dwThreadId;
    *handle = 0;
    if (req->flags & WEXELEVATE_WANT_HANDLE)
    {
        /* the caller can wait for the process and read its exit code, and nothing else */
        if (!DuplicateHandle( GetCurrentProcess(), pi.hProcess, req->caller, &remote, SYNCHRONIZE | PROCESS_QUERY_LIMITED_INFORMATION,
                              FALSE, 0 )) err = GetLastError();
        else *handle = (ULONGLONG)(ULONG_PTR)remote;
    }
    if (req->lease)
    {
        /* name the process to the lease: if a writer has opened the image meanwhile, it ends here */
        DWORD arm = pLeaseArm( req->lease, pi.dwProcessId );
        if (arm || !start_guard( req, pi.hProcess ))
        {
            err = arm ? arm : ERROR_NOT_ENOUGH_MEMORY;
            if (remote) DuplicateHandle( req->caller, remote, NULL, NULL, 0, FALSE, DUPLICATE_CLOSE_SOURCE );
            *handle = 0;
            TerminateProcess( pi.hProcess, 1 );
            CloseHandle( pi.hProcess );
            CloseHandle( pi.hThread );
            goto done;
        }
        pi.hProcess = NULL;   /* the guard owns it now */
    }
    CloseHandle( pi.hThread );
    if (pi.hProcess) CloseHandle( pi.hProcess );
done:
    if (si.lpAttributeList)
    {
        DeleteProcThreadAttributeList( si.lpAttributeList );
        free( si.lpAttributeList );
    }
    if (default_env) DestroyEnvironmentBlock( default_env );
    free( cmdline );
    CloseHandle( linked );
    return err;
}

/* ---- one request --------------------------------------------------------------------------------------------- */

static DWORD handle_request( struct request *req, BYTE *buf, DWORD size, struct wexelevate_reply *reply )
{
    DWORD err;

    if (!system_role) return ERROR_NOT_SUPPORTED;
    if ((err = identify_caller( req ))) return err;
    if ((err = parse_request( req, buf, size ))) return err;
    if ((err = table_add( req ))) return err;
    if ((err = freeze_image( req ))) return err;
    if ((err = approve( req ))) return err;
    /* from here on the request is used: once */
    if (!table_consume( req )) return ERROR_CANCELLED;
    if (WaitForSingleObject( req->caller, 0 ) != WAIT_TIMEOUT) return ERROR_CANCELLED;
    if ((err = verify_image( req ))) return err;
    if (req->lease && ((err = pLeaseArm( req->lease, 0 )) || WaitForSingleObject( req->lease_event, 0 ) != WAIT_TIMEOUT))
        return err ? err : ERROR_FILE_CORRUPT;
    return launch( req, &reply->pid, &reply->tid, &reply->handle );
}

static DWORD WINAPI client_thread( void *param )
{
    struct request *req = param;
    struct wexelevate_reply reply = { WEXELEVATE_MAGIC };
    DWORD size = 0, done;
    BYTE *buf;

    if (!(buf = malloc( WEXELEVATE_MAX_REQUEST + 1 ))) reply.error = ERROR_NOT_ENOUGH_MEMORY;
    else
    {
        reply.error = pipe_io( req->pipe, buf, WEXELEVATE_MAX_REQUEST + 1, FALSE, READ_TIMEOUT, &size );
        if (reply.error == ERROR_MORE_DATA) reply.error = ERROR_INVALID_PARAMETER;
        if (!reply.error) reply.error = handle_request( req, buf, size, &reply );
    }
    if (reply.error == ERROR_OPERATION_ABORTED || reply.error == ERROR_BROKEN_PIPE) goto done;
    pipe_io( req->pipe, &reply, sizeof(reply), TRUE, READ_TIMEOUT, &done );
    FlushFileBuffers( req->pipe );
done:
    if (reply.error && reply.handle) CloseHandle( (HANDLE)(ULONG_PTR)reply.handle );
    DisconnectNamedPipe( req->pipe );
    CloseHandle( req->pipe );
    req->pipe = INVALID_HANDLE_VALUE;
    free( buf );
    free_request( req );
    InterlockedDecrement( &active_threads );
    return 0;
}

/* ---- the pipe server ----------------------------------------------------------------------------------------- */

static HANDLE create_instance( BOOL first )
{
    SECURITY_ATTRIBUTES sa = { sizeof(sa) };
    HANDLE pipe;

    /* SYSTEM and administrators everything, authenticated users read and write (connect) */
    if (!ConvertStringSecurityDescriptorToSecurityDescriptorW( L"D:(A;;GA;;;SY)(A;;GA;;;BA)(A;;GRGW;;;AU)", SDDL_REVISION_1,
                                                              &sa.lpSecurityDescriptor, NULL )) return INVALID_HANDLE_VALUE;
    pipe = CreateNamedPipeW( WEXELEVATE_PIPE_NAME, PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED |
                             (first ? FILE_FLAG_FIRST_PIPE_INSTANCE : 0),
                             PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE | PIPE_WAIT | PIPE_REJECT_REMOTE_CLIENTS,
                             PIPE_UNLIMITED_INSTANCES, 4096, WEXELEVATE_MAX_REQUEST, 0, &sa );
    LocalFree( sa.lpSecurityDescriptor );
    return pipe;
}

static void report( DWORD state, DWORD error )
{
    SERVICE_STATUS status = { SERVICE_WIN32_OWN_PROCESS, state, state == SERVICE_RUNNING ? SERVICE_ACCEPT_STOP : 0, error };

    status.dwWaitHint = state == SERVICE_START_PENDING || state == SERVICE_STOP_PENDING ? 10000 : 0;
    if (status_handle) SetServiceStatus( status_handle, &status );
}

static DWORD WINAPI handler( DWORD ctrl, DWORD event_type, void *event_data, void *context )
{
    if (ctrl == SERVICE_CONTROL_STOP || ctrl == SERVICE_CONTROL_SHUTDOWN)
    {
        report( SERVICE_STOP_PENDING, 0 );
        SetEvent( stop_event );
        return NO_ERROR;
    }
    return ctrl == SERVICE_CONTROL_INTERROGATE ? NO_ERROR : ERROR_CALL_NOT_IMPLEMENTED;
}

static void serve(void)
{
    HANDLE pipe = create_instance( TRUE );
    OVERLAPPED ov = { 0 };
    HANDLE events[2];
    HANDLE thread;
    struct request *req;

    /* another program created the pipe first: do not serve (and do not take its place) */
    if (pipe == INVALID_HANDLE_VALUE)
    {
        report( SERVICE_STOPPED, GetLastError() ? GetLastError() : ERROR_ACCESS_DENIED );
        return;
    }
    report( SERVICE_RUNNING, 0 );
    ov.hEvent = CreateEventW( NULL, TRUE, FALSE, NULL );
    events[0] = stop_event;
    events[1] = ov.hEvent;
    for (;;)
    {
        DWORD err = ERROR_SUCCESS, ret;

        ResetEvent( ov.hEvent );
        if (!ConnectNamedPipe( pipe, &ov ))
        {
            err = GetLastError();
            if (err == ERROR_IO_PENDING)
            {
                ret = WaitForMultipleObjects( 2, events, FALSE, INFINITE );
                if (ret == WAIT_OBJECT_0)
                {
                    CancelIo( pipe );
                    break;
                }
                err = ERROR_SUCCESS;
            }
            else if (err == ERROR_PIPE_CONNECTED) err = ERROR_SUCCESS;
        }
        if (!err)
        {
            if (InterlockedIncrement( &active_threads ) > 2 * MAX_REQUESTS || !(req = calloc( 1, sizeof(*req) )))
            {
                InterlockedDecrement( &active_threads );
                DisconnectNamedPipe( pipe );
            }
            else
            {
                req->pipe = pipe;
                req->image_file = INVALID_HANDLE_VALUE;
                if (!(thread = CreateThread( NULL, 0, client_thread, req, 0, NULL )))
                {
                    InterlockedDecrement( &active_threads );
                    free( req );
                    DisconnectNamedPipe( pipe );
                    CloseHandle( pipe );
                }
                else CloseHandle( thread );
                pipe = INVALID_HANDLE_VALUE;
            }
        }
        else DisconnectNamedPipe( pipe );
        if (pipe == INVALID_HANDLE_VALUE && (pipe = create_instance( FALSE )) == INVALID_HANDLE_VALUE)
        {
            Sleep( 1000 );   /* out of resources: try again */
            if (WaitForSingleObject( stop_event, 0 ) == WAIT_OBJECT_0) break;
            pipe = create_instance( FALSE );
            if (pipe == INVALID_HANDLE_VALUE) break;
        }
    }
    CloseHandle( ov.hEvent );
    if (pipe != INVALID_HANDLE_VALUE) CloseHandle( pipe );
    SetEvent( stop_event );
    while (active_threads) Sleep( 50 );
}

static void WINAPI service_main( DWORD argc, WCHAR **argv )
{
    status_handle = RegisterServiceCtrlHandlerExW( WEXELEVATE_SERVICE, handler, NULL );
    if (!status_handle) return;
    report( SERVICE_START_PENDING, 0 );
    InitializeCriticalSection( &table_cs );
    stop_event = CreateEventW( NULL, TRUE, FALSE, NULL );
    system_role = token_is_system_with_tcb();
    if (!load_wexhost()) pLeaseTake = NULL;   /* no lease possible: every request then fails closed */
    serve();
    report( SERVICE_STOPPED, 0 );
}

int wmain( int argc, WCHAR **argv )
{
    static WCHAR name[] = WEXELEVATE_SERVICE;
    SERVICE_TABLE_ENTRYW table_entries[] = { { name, service_main }, { NULL, NULL } };

    StartServiceCtrlDispatcherW( table_entries );
    return 0;
}

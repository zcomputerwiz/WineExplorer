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
 *  - asks for approval: click consent (ConsentPromptBehaviorAdmin 2 or 5, 5 when absent) in the consent window,
 *    which runs on the secure desktop WinSta0\Winlogon (PromptOnSecureDesktop, default 1) that this service makes
 *    with a SYSTEM-only descriptor; any other policy fails closed. A build with WEXELEVATE_TEST_APPROVAL calls a
 *    stub program for a request with no policy value, which exists in the test harness only;
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

#ifdef WEXELEVATE_TEST_APPROVAL
#include <stdio.h>
#include "e1s.h"
#include "e1s6_protocol.h"
#define E1S_AUDIT(site) fprintf( stderr, "E1S6_DENY broker site=%s\n", site )
#else
#define E1S_AUDIT(site) do {} while (0)
#endif

#define MAX_REQUESTS      32   /* pending at once */
#define MAX_PER_CALLER    4
#define READ_TIMEOUT      10000
#define APPROVAL_TIMEOUT  120000

#ifdef WEXELEVATE_TEST_APPROVAL
/* Test harness only. The stub gets the request id and the image path, and exits with 0 to approve. */
#define TEST_STUB L"C:\\windows\\system32\\wexelevate_stub.exe"
/* Test harness only: the consent program built with a hook that lets the harness answer without input events
 * (a copy of consent.exe under another name, because Wine loads a system32 program that has a builtin twin from
 * the builtin directory), and a shorter timeout for the timeout probe. */
#define CONSENT_EXE L"consent_test.exe"
#else
#define CONSENT_EXE L"consent.exe"
#endif

struct request
{
    ULONGLONG id;
    HANDLE pipe;
    DWORD caller_pid;
    HANDLE caller;        /* process handle of the caller */
    HANDLE caller_token;  /* its primary token */
    TOKEN_STATISTICS caller_stats; /* frozen H2/S3 security version and caller logon */
    LARGE_INTEGER caller_start;
    DWORD caller_session, caller_sid_len;
    BYTE caller_sid[SECURITY_MAX_SID_SIZE];
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

/* the secure desktop (docs/ELEVATION_SECURE_DESKTOP_WORKORDER.md): WinSta0\Winlogon, made by this service with a
 * descriptor that admits SYSTEM only; the input returns to the user's Default desktop (looked up again each time: a
 * desktop that Wine closed and made again is another object) */
static HDESK secure_desktop, saved_input;
static CRITICAL_SECTION secure_cs;
static LONG secure_users;                      /* consent windows on the secure desktop now */
static BOOL secure_failed;                     /* the desktop could not be made safely: no prompt on it, ever */

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

/* 0 when the generator fails: the request is then denied, never given a predictable id */
static ULONGLONG random_id(void)
{
    ULONGLONG id = 0;

    if (!NT_SUCCESS(BCryptGenRandom( NULL, (UCHAR *)&id, sizeof(id), BCRYPT_USE_SYSTEM_PREFERRED_RNG ))) return 0;
    return id;
}

/* put a request into the table; fails with ERROR_BUSY at the limits, and with ERROR_GEN_FAILURE when no random id
 * can be generated */
static DWORD table_add( struct request *req )
{
    DWORD err = ERROR_BUSY, i, j, tries, same = 0;

    EnterCriticalSection( &table_cs );
    for (i = 0; i < MAX_REQUESTS; i++) if (table[i] && table[i]->caller_pid == req->caller_pid) same++;
    if (same < MAX_PER_CALLER)
    {
        for (i = 0; i < MAX_REQUESTS; i++)
        {
            if (table[i]) continue;
            /* A correlation id is not authority, but it must still be unique
             * among live requests. Bound retries; RNG failure always denies. */
            for (tries = 0; tries < MAX_REQUESTS; ++tries)
            {
                req->id = random_id();
                if (!req->id) break;
                for (j = 0; j < MAX_REQUESTS; ++j) if (table[j] && table[j]->id == req->id) break;
                if (j == MAX_REQUESTS) break;
            }
            if (!req->id || tries == MAX_REQUESTS)
            {
                err = ERROR_GEN_FAILURE;
                break;
            }
            table[i] = req;
            req->live = 1;
            err = ERROR_SUCCESS;
            break;
        }
    }
    LeaveCriticalSection( &table_cs );
    return err;
}

/* Single use; retain the occupied slot/id until cleanup so a consumed request
 * still in launch preparation cannot collide with a newly admitted request. */
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
    if (!(ov.hEvent = CreateEventW( NULL, TRUE, FALSE, NULL )))
    { err = GetLastError(); E1S_AUDIT( "pipe-create-event" ); return err; }
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
            E1S_AUDIT( "pipe-start-io" );
            CloseHandle( ov.hEvent );
            return err;
        }
    }
    if (!GetOverlappedResult( pipe, &ov, done, TRUE ))
    { err = GetLastError(); E1S_AUDIT( "pipe-io-result" ); }
    CloseHandle( ov.hEvent );
    return err;
}

/* ---- the caller ---------------------------------------------------------------------------------------------- */

static BOOL token_user_sid( HANDLE token, BYTE *buf, DWORD size )
{
    DWORD len;
    ULONG_PTR offset;
    SID *sid;

    if (!GetTokenInformation( token, TokenUser, buf, size, &len ) || len > size || len < sizeof(TOKEN_USER))
        return FALSE;
    sid = ((TOKEN_USER *)buf)->User.Sid;
    if ((ULONG_PTR)sid < (ULONG_PTR)buf) return FALSE;
    offset = (ULONG_PTR)sid - (ULONG_PTR)buf;
    if (offset > len || len - offset < FIELD_OFFSET( SID, SubAuthority )) return FALSE;
    if (sid->SubAuthorityCount > SID_MAX_SUB_AUTHORITIES ||
        len - offset - FIELD_OFFSET( SID, SubAuthority ) < sid->SubAuthorityCount * sizeof(DWORD)) return FALSE;
    return IsValidSid( sid );
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

/* Private native-layout ABI from wine/include/winternl.h, mirrored here so the lab broker can also be built
 * against the external SDK. Keep field order/types and constants in sync with WEX_PIPE_CALLER_BINDING. */
#define PIPE_BINDING_VERSION      1
#define PIPE_WRITER_VALID         0x0001
#define PIPE_WRITER_TOKEN_CURRENT 0x0004
struct pipe_caller_binding
{
    ULONG Size;
    ULONG Version;
    HANDLE Process;
    HANDLE Token;
    HANDLE WriterProcess;
    HANDLE WriterToken;
    ULONG Pid;
    LARGE_INTEGER StartTime;
    LUID TokenId;
    LUID ModifiedId;
    LUID WriterTokenId;
    LUID WriterModifiedId;
    BOOL Impersonated;
    BOOL TokenCurrent;
    ULONG WriterFlags;
};

/* E1-S3: compare retained objects and versions at open, consumed write and query.
 * The Ex export and exact capability/version checks make older implementations deny. */
static BOOL token_stats( HANDLE token, TOKEN_STATISTICS *stats )
{
    DWORD len;
    return GetTokenInformation( token, TokenStatistics, stats, sizeof(*stats), &len ) && len == sizeof(*stats);
}

static BOOL same_luid( LUID a, LUID b )
{
    return a.LowPart == b.LowPart && a.HighPart == b.HighPart;
}

/* capture only once, after consuming the one request message. Every later call
 * queries that same retained opener AND consumed writer; never a fresh read. */
static DWORD check_binding( struct request *req, DWORD pid, HANDLE pipe_token, BOOL capture )
{
    struct pipe_caller_binding binding = { .Size = sizeof(binding) };
    TOKEN_STATISTICS primary, pipe;
    BYTE user[sizeof(TOKEN_USER) + SECURITY_MAX_SID_SIZE];
    HANDLE current = NULL, impersonation = NULL;
    DWORD err = ERROR_ACCESS_DENIED, sid1 = 0, sid2 = 0, len;
    HMODULE ntdll = GetModuleHandleW( L"ntdll.dll" ), kb = GetModuleHandleW( L"kernelbase.dll" );
    NTSTATUS (WINAPI *query)( HANDLE, struct pipe_caller_binding * ) =
        ntdll ? (void *)GetProcAddress( ntdll, "NtQueryWexPipeCallerBindingEx" ) : NULL;
    BOOL (WINAPI *compare)( HANDLE, HANDLE ) = kb ? (void *)GetProcAddress( kb, "CompareObjectHandles" ) : NULL;

    /* In particular, a failed RevertToSelf must never reach authorization. */
    if (OpenThreadToken( GetCurrentThread(), TOKEN_QUERY, TRUE, &impersonation ))
    {
        CloseHandle( impersonation );
        E1S_AUDIT( "binding-broker-impersonating" );
        return ERROR_ACCESS_DENIED;
    }
    if (GetLastError() != ERROR_NO_TOKEN) { E1S_AUDIT( "binding-thread-token-query" ); return ERROR_ACCESS_DENIED; }
    if (!query || !compare) { E1S_AUDIT( "binding-missing-export" ); return ERROR_NOT_SUPPORTED; }
    if (!NT_SUCCESS(query( req->pipe, &binding ))) { E1S_AUDIT( "binding-server-query" ); return ERROR_ACCESS_DENIED; }

#define CHECK_BINDING(c, site) do { if (!(c)) { E1S_AUDIT( site ); goto done; } } while (0)
    CHECK_BINDING( binding.Size == sizeof(binding) && binding.Version == PIPE_BINDING_VERSION, "binding-version" );
    CHECK_BINDING( binding.Pid == pid, "binding-pid" );
    CHECK_BINDING( WaitForSingleObject( req->caller, 0 ) == WAIT_TIMEOUT, "binding-caller-live" );
    CHECK_BINDING( !binding.Impersonated, "binding-opener-impersonated" );
    CHECK_BINDING( binding.TokenCurrent, "binding-opener-current" );
    CHECK_BINDING( binding.WriterFlags == (PIPE_WRITER_VALID | PIPE_WRITER_TOKEN_CURRENT), "binding-writer-flags" );
    CHECK_BINDING( compare( binding.Process, req->caller ), "binding-opener-process" );
    CHECK_BINDING( compare( binding.Token, req->caller_token ), "binding-opener-token" );
    CHECK_BINDING( compare( binding.WriterProcess, binding.Process ), "binding-writer-process" );
    CHECK_BINDING( compare( binding.WriterToken, binding.Token ), "binding-writer-token" );
    CHECK_BINDING( OpenProcessToken( req->caller, TOKEN_QUERY, &current ), "binding-open-current" );
    CHECK_BINDING( compare( current, req->caller_token ), "binding-current-object" );
    CHECK_BINDING( same_luid( binding.WriterTokenId, binding.TokenId ), "binding-writer-token-id" );
    CHECK_BINDING( same_luid( binding.WriterModifiedId, binding.ModifiedId ), "binding-writer-modified-id" );
    CHECK_BINDING( token_stats( current, &primary ) && primary.TokenType == TokenPrimary, "binding-primary-statistics" );
    CHECK_BINDING( same_luid( primary.TokenId, binding.TokenId ), "binding-primary-token-id" );
    CHECK_BINDING( same_luid( primary.ModifiedId, binding.ModifiedId ), "binding-primary-modified-id" );
    CHECK_BINDING( primary.AuthenticationId.LowPart || primary.AuthenticationId.HighPart, "binding-authentication-id" );
    CHECK_BINDING( GetTokenInformation( req->caller_token, TokenSessionId, &sid1, sizeof(sid1), &len ) &&
                   len == sizeof(sid1), "binding-session" );
    CHECK_BINDING( token_user_sid( current, user, sizeof(user) ) && IsValidSid( ((TOKEN_USER *)user)->User.Sid ),
                   "binding-user-sid" );
    {
        PSID sid = ((TOKEN_USER *)user)->User.Sid;
        if (pipe_token && (!token_stats( pipe_token, &pipe ) || pipe.TokenType != TokenImpersonation ||
            pipe.ImpersonationLevel < SecurityIdentification || !same_luid( primary.AuthenticationId, pipe.AuthenticationId ) ||
            !GetTokenInformation( pipe_token, TokenSessionId, &sid2, sizeof(sid2), &len ) || len != sizeof(sid2) || sid1 != sid2))
        { E1S_AUDIT( "binding-pipe-token" ); goto done; }
        if (capture)
        {
            req->caller_stats = primary;
            req->caller_start = binding.StartTime;
            req->caller_session = sid1;
            req->caller_sid_len = GetLengthSid( sid );
            if (req->caller_sid_len > sizeof(req->caller_sid) ||
                !CopySid( sizeof(req->caller_sid), req->caller_sid, sid ))
            { E1S_AUDIT( "binding-copy-sid" ); goto done; }
        }
        else if (!same_luid( primary.TokenId, req->caller_stats.TokenId ) ||
                 !same_luid( primary.ModifiedId, req->caller_stats.ModifiedId ) ||
                 !same_luid( primary.AuthenticationId, req->caller_stats.AuthenticationId ) ||
                 sid1 != req->caller_session || binding.StartTime.QuadPart != req->caller_start.QuadPart ||
                 !EqualSid( sid, req->caller_sid )) { E1S_AUDIT( "binding-frozen-identity" ); goto done; }
        err = ERROR_SUCCESS;
    }
done:
#undef CHECK_BINDING
    if (current) CloseHandle( current );
    CloseHandle( binding.Process );
    CloseHandle( binding.Token );
    CloseHandle( binding.WriterProcess );
    CloseHandle( binding.WriterToken );
    return err;
}

/* Who is the caller: the process that opened the pipe, as the server recorded it (the request carries no
 * identity), with the token of that process. The user the pipe reports for the client thread must be the user of
 * the process. A caller that is a standard user or elevated already is not served here. */
static DWORD identify_caller( struct request *req )
{
    BYTE pipe_user[sizeof(TOKEN_USER) + SECURITY_MAX_SID_SIZE], proc_user[sizeof(TOKEN_USER) + SECURITY_MAX_SID_SIZE];
    HANDLE thread_token = NULL;
    DWORD pid = 0, pid2 = 0, elevation, len, err;

    if (!GetNamedPipeClientProcessId( req->pipe, &pid ) || !pid) return ERROR_ACCESS_DENIED;
    if (!ImpersonateNamedPipeClient( req->pipe )) return ERROR_ACCESS_DENIED;
    if (!OpenThreadToken( GetCurrentThread(), TOKEN_QUERY, TRUE, &thread_token ))
    {
        if (!RevertToSelf()) return ERROR_ACCESS_DENIED;
        return ERROR_ACCESS_DENIED;
    }
    if (!RevertToSelf())
    {
        CloseHandle( thread_token );
        return ERROR_ACCESS_DENIED;
    }
    if (!token_user_sid( thread_token, pipe_user, sizeof(pipe_user) ))
    {
        CloseHandle( thread_token );
        return ERROR_ACCESS_DENIED;
    }

    req->caller_pid = pid;
    if (!(req->caller = OpenProcess( PROCESS_QUERY_INFORMATION | PROCESS_QUERY_LIMITED_INFORMATION | PROCESS_DUP_HANDLE |
                                     PROCESS_CREATE_PROCESS | SYNCHRONIZE, FALSE, pid ))) goto denied;
    if (!OpenProcessToken( req->caller, TOKEN_QUERY | TOKEN_DUPLICATE, &req->caller_token )) goto denied;
    if (!token_user_sid( req->caller_token, proc_user, sizeof(proc_user) ) ||
        !EqualSid( ((TOKEN_USER *)pipe_user)->User.Sid, ((TOKEN_USER *)proc_user)->User.Sid )) goto denied;
    /* the server's record of the opener must be this very process and token (E1-H2) */
    if ((err = check_binding( req, pid, thread_token, TRUE )))
    {
        CloseHandle( thread_token );
        return err;
    }
    CloseHandle( thread_token );
    /* the pid must still be the client's, and the process must not have gone away meanwhile */
    if (!GetNamedPipeClientProcessId( req->pipe, &pid2 ) || pid2 != pid || WaitForSingleObject( req->caller, 0 ) != WAIT_TIMEOUT)
        return ERROR_ACCESS_DENIED;
    if (!GetTokenInformation( req->caller_token, TokenElevationType, &elevation, sizeof(elevation), &len ) ||
        len != sizeof(elevation)) return ERROR_ACCESS_DENIED;
    /* only the limited half of a split administrator token has something to elevate to */
    return elevation == TokenElevationTypeLimited ? ERROR_SUCCESS : ERROR_NOT_SUPPORTED;
denied:
    CloseHandle( thread_token );
    return ERROR_ACCESS_DENIED;
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
        if (!ReadFile( file, buf, 65536, &got, NULL ))
        { err = GetLastError(); E1S_AUDIT( "image-hash-read" ); }
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
    if (req->image_file == INVALID_HANDLE_VALUE)
    { err = GetLastError(); E1S_AUDIT( "freeze-open-image" ); return err; }
    if (GetFileType( req->image_file ) != FILE_TYPE_DISK || !GetFileInformationByHandle( req->image_file, &req->info ) ||
        (req->info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY))
    { E1S_AUDIT( "freeze-image-info" ); return ERROR_FILE_NOT_FOUND; }
    /* the lease first, then the hash: from here on the bytes cannot change without the broker noticing */
    if (!pLeaseTake) { E1S_AUDIT( "freeze-lease-export" ); return ERROR_FILE_SYSTEM_LIMITATION; }
    if (!(req->lease_event = CreateEventW( NULL, TRUE, FALSE, NULL )))
    { err = GetLastError(); E1S_AUDIT( "freeze-lease-event" ); return err; }
    if ((err = pLeaseTake( req->image_file, req->lease_event, &req->lease )))
    { E1S_AUDIT( "freeze-lease-take" ); return err; }
    if ((err = hash_file( req->image_file, req->hash )))
    { E1S_AUDIT( "freeze-image-hash" ); return err; }
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

#define POLICY_KEY L"Software\\Microsoft\\Windows\\CurrentVersion\\Policies\\System"

/* The consent policy (writable by administrators only while elevation is enforced). ConsentPromptBehaviorAdmin: 5
 * when absent, as in Windows; 2 and 5 select click consent. Windows' 5 also auto-elevates its own signed programs
 * that ask for it; that is not implemented (a known source of UAC bypasses). 0 (elevate without asking) and any
 * other value select nothing: the broker fails closed, 21. 1 and 3 (credentials) are the polkit work order.
 * Returns 1 for click consent, 0 for none; *absent says whether the value was missing. */
static BOOL click_consent_selected( BOOL *absent )
{
    DWORD value = 0, size = sizeof(value), err;

    err = RegGetValueW( HKEY_LOCAL_MACHINE, POLICY_KEY, L"ConsentPromptBehaviorAdmin", RRF_RT_REG_DWORD, NULL, &value, &size );
    *absent = err == ERROR_FILE_NOT_FOUND;
    if (*absent) return TRUE;
    if (err) return FALSE;   /* a value of another type: not a policy we know */
    return value == 2 || value == 5;
}

/* PromptOnSecureDesktop: 1 when absent. Only a DWORD 0 puts the prompt on the user's desktop. */
static BOOL prompt_on_secure_desktop(void)
{
    DWORD value = 1, size = sizeof(value);

    if (RegGetValueW( HKEY_LOCAL_MACHINE, POLICY_KEY, L"PromptOnSecureDesktop", RRF_RT_REG_DWORD, NULL, &value, &size ))
        return TRUE;
    return value != 0;
}

/* ---- the secure desktop ---------------------------------------------------------------------------------- */

/* Create WinSta0\Winlogon before anything that a user controls can, with a descriptor that names SYSTEM only (not
 * ReactOS' Administrators ACE: its STANDARD_RIGHTS_REQUIRED lets administrators rewrite the descriptor), and a handle
 * that no child inherits. The server reserves the name for the system role; this also fails closed when the desktop
 * existed already or its descriptor reads back as anything else. */
static DWORD check_secure_descriptor( HDESK desk )
{
    SECURITY_DESCRIPTOR *sd;
    SECURITY_INFORMATION info = DACL_SECURITY_INFORMATION;
    DWORD size = 0, err = ERROR_ACCESS_DENIED;
    ACL *dacl;
    BOOL present, defaulted;
    ACCESS_ALLOWED_ACE *ace;

    GetUserObjectSecurity( desk, &info, NULL, 0, &size );
    if (!size || !(sd = malloc( size ))) return ERROR_NOT_ENOUGH_MEMORY;
    if (GetUserObjectSecurity( desk, &info, sd, size, &size ) &&
        GetSecurityDescriptorDacl( sd, &present, &dacl, &defaulted ) && present && dacl &&
        dacl->AceCount == 1 && GetAce( dacl, 0, (void **)&ace ) && ace->Header.AceType == ACCESS_ALLOWED_ACE_TYPE &&
        IsWellKnownSid( (SID *)&ace->SidStart, WinLocalSystemSid ) && (ace->Mask & (GENERIC_ALL | DESKTOP_SWITCHDESKTOP)))
        err = ERROR_SUCCESS;
    free( sd );
    return err;
}

#ifdef WEXELEVATE_TEST_APPROVAL
/* Test harness only: HKLM\Software\WineExplorer\ElevationTest "SecureDesktopTest" = 1 makes this broker create
 * Winlogon with an open descriptor first (the broker must refuse a desktop that existed before it), 2 makes it use an
 * open descriptor itself (it must refuse a descriptor that reads back wider than SYSTEM only). */
static DWORD secure_desktop_test(void)
{
    DWORD value = 0, size = sizeof(value);

    RegGetValueW( HKEY_LOCAL_MACHINE, L"Software\\WineExplorer\\ElevationTest", L"SecureDesktopTest", RRF_RT_REG_DWORD, NULL,
                  &value, &size );
    return value;
}
#endif

static DWORD create_secure_desktop(void)
{
    SECURITY_ATTRIBUTES sa = { sizeof(sa) };
    HWINSTA station, saved = GetProcessWindowStation();
    HDESK existing;
    const WCHAR *sddl = L"D:(A;;GA;;;SY)";
    DWORD err;

    if (!(station = OpenWindowStationW( L"WinSta0", FALSE, WINSTA_CREATEDESKTOP | WINSTA_ENUMDESKTOPS )))
        return GetLastError();
    if (!SetProcessWindowStation( station ))
    {
        err = GetLastError();
        CloseWindowStation( station );
        return err;
    }
    err = ERROR_SUCCESS;
#ifdef WEXELEVATE_TEST_APPROVAL
    if (secure_desktop_test() == 1) CreateDesktopW( L"Winlogon", NULL, NULL, 0, DESKTOP_READOBJECTS, NULL );
    if (secure_desktop_test() == 2) sddl = L"D:(A;;GA;;;SY)(A;;GA;;;WD)";
#endif
    /* an existing Winlogon, whoever made it, is not trusted */
    if ((existing = OpenDesktopW( L"Winlogon", 0, FALSE, DESKTOP_READOBJECTS )))
    {
        CloseDesktop( existing );
        err = ERROR_ALREADY_EXISTS;
    }
    else if (GetLastError() != ERROR_FILE_NOT_FOUND) err = ERROR_ALREADY_EXISTS;
    if (!err && !ConvertStringSecurityDescriptorToSecurityDescriptorW( sddl, SDDL_REVISION_1,
                                                                       &sa.lpSecurityDescriptor, NULL )) err = GetLastError();
    if (!err)
    {
        sa.bInheritHandle = FALSE;
        secure_desktop = CreateDesktopW( L"Winlogon", NULL, NULL, 0, DESKTOP_SWITCHDESKTOP | DESKTOP_READOBJECTS | READ_CONTROL, &sa );
        if (!secure_desktop) err = GetLastError();
        LocalFree( sa.lpSecurityDescriptor );
    }
    if (!err && (err = check_secure_descriptor( secure_desktop )))
    {
        CloseDesktop( secure_desktop );
        secure_desktop = NULL;
    }
    if (err)
    {
        SetProcessWindowStation( saved );
        CloseWindowStation( station );
        if (secure_desktop) CloseDesktop( secure_desktop );
        secure_desktop = NULL;
    }
    /* On success the process stays in WinSta0: SwitchDesktop acts on the process's window station, and only
     * WinSta0 takes input from the host. The elevated programs do not depend on it (they take the caller's desktop). */
    return err;
}

/* switch to the secure desktop for a consent window (the first one switches), and back when the last one is gone */
static DWORD secure_enter(void)
{
    DWORD err = ERROR_SUCCESS;

    EnterCriticalSection( &secure_cs );
    /* The broker is a service, in a window station of its own; WinSta0 exists once a user's program has connected, so
     * the desktop is made at the first prompt, not at the start of the service. A limited process cannot take the name
     * meanwhile: the server reserves it for the system role. Any failure except a missing WinSta0 is final. */
    if (!secure_desktop && !secure_failed)
    {
        DWORD created = create_secure_desktop();

        if (created == ERROR_FILE_NOT_FOUND) err = ERROR_NOT_READY;
        else if (created) secure_failed = TRUE;
    }
    if (!err && !secure_desktop) err = ERROR_NOT_READY;
    if (!err && !secure_users)
    {
        saved_input = OpenInputDesktop( 0, FALSE, DESKTOP_SWITCHDESKTOP );   /* what the input returns to if Default is gone */
        if (!SwitchDesktop( secure_desktop )) err = GetLastError();
        if (err && saved_input) CloseDesktop( saved_input );
    }
    if (!err) secure_users++;
    LeaveCriticalSection( &secure_cs );
    return err;
}

/* the input goes back to Default, or to the desktop that had it if Default cannot be opened */
static void return_input(void)
{
    HDESK desk = OpenDesktopW( L"Default", 0, FALSE, DESKTOP_SWITCHDESKTOP );

    if (desk)
    {
        SwitchDesktop( desk );
        CloseDesktop( desk );
    }
    else if (saved_input) SwitchDesktop( saved_input );
    if (saved_input) CloseDesktop( saved_input );
    saved_input = NULL;
}

static void secure_leave(void)
{
    EnterCriticalSection( &secure_cs );
    if (secure_users && !--secure_users) return_input();
    LeaveCriticalSection( &secure_cs );
}

static DWORD consent_timeout(void)
{
#ifdef WEXELEVATE_TEST_APPROVAL
    DWORD value = 0, size = sizeof(value);

    if (!RegGetValueW( HKEY_LOCAL_MACHINE, L"Software\\WineExplorer\\ElevationTest", L"ConsentTimeoutMs", RRF_RT_REG_DWORD,
                       NULL, &value, &size ) && value) return value;
#endif
    return APPROVAL_TIMEOUT;
}

/* Click consent. The broker starts the consent window itself, as its own child (the SYSTEM token of the service,
 * not the caller's), on the secure desktop (PromptOnSecureDesktop, default 1; 0: the user's desktop), and tells it
 * what it was asked to run: the frozen image path and the command line of the frozen request, never anything the
 * consent program could take from the caller. It exits with 0 only when the user chose Continue, with ERROR_CANCELLED when the user cancelled or closed it. Whatever else
 * happens (it cannot start, crashes, exits with another code) is no approval. */
static DWORD approve_consent( struct request *req, BOOL secure )
{
    STARTUPINFOW si = { sizeof(si) };
    PROCESS_INFORMATION pi;
    WCHAR dir[MAX_PATH], *cmd;
    HANDLE handles[4];
    DWORD ret, code, len;
    size_t cmd_size;

    if (wcschr( req->image, '"' )) return ERROR_INVALID_PARAMETER;
    if (lstrlenW( req->image ) >= MAX_PATH) return ERROR_FILENAME_EXCED_RANGE;
    if (!(len = GetSystemDirectoryW( dir, MAX_PATH )) || len >= MAX_PATH - 32) return ERROR_SERVICE_NOT_ACTIVE;
    cmd_size = (len + lstrlenW( req->image ) + lstrlenW( req->cmdline ) + 160) * sizeof(WCHAR);
    if (cmd_size > 32000 * sizeof(WCHAR)) return ERROR_FILENAME_EXCED_RANGE;
    if (!(cmd = malloc( cmd_size ))) return ERROR_NOT_ENOUGH_MEMORY;
    lstrcpyW( cmd, L"\"" );
    lstrcatW( cmd, dir );
    lstrcatW( cmd, L"\\" CONSENT_EXE L"\" /broker " );
#ifdef WEXELEVATE_TEST_APPROVAL
    wsprintfW( cmd + lstrlenW( cmd ), L"/testid %08lx%08lx ", (DWORD)(req->id >> 32), (DWORD)req->id );
#endif
    if (secure) lstrcatW( cmd, L"/secure " );
    wsprintfW( cmd + lstrlenW( cmd ), L"/ppid %lu ", GetCurrentProcessId() );
    lstrcatW( cmd, L"/image \"" );
    lstrcatW( cmd, req->image );
    lstrcatW( cmd, L"\" /cmd " );
    lstrcatW( cmd, req->cmdline );

    /* the input goes to the secure desktop first; every way out below returns it */
    if (secure && secure_enter())
    {
        free( cmd );
        return ERROR_NOT_READY;
    }
    si.lpDesktop = (WCHAR *)(secure ? L"WinSta0\\Winlogon" : L"WinSta0\\Default");
    if (!CreateProcessW( NULL, cmd, NULL, NULL, FALSE, 0, NULL, dir, &si, &pi ))
    {
        free( cmd );
        if (secure) secure_leave();
        return ERROR_SERVICE_NOT_ACTIVE;
    }
    free( cmd );
    CloseHandle( pi.hThread );
    handles[0] = pi.hProcess;
    handles[1] = req->caller;       /* the request is destroyed when the caller exits */
    handles[2] = stop_event;
    handles[3] = req->lease_event;  /* a writer opened the image */
    ret = WaitForMultipleObjects( 4, handles, FALSE, consent_timeout() );
    if (ret != WAIT_OBJECT_0)
    {
        TerminateProcess( pi.hProcess, 1 );
        WaitForSingleObject( pi.hProcess, 5000 );
        CloseHandle( pi.hProcess );
        if (secure) secure_leave();
        return ret == WAIT_TIMEOUT ? ERROR_TIMEOUT : ret == WAIT_OBJECT_0 + 3 ? ERROR_FILE_CORRUPT : ERROR_CANCELLED;
    }
    code = ERROR_GEN_FAILURE;
    GetExitCodeProcess( pi.hProcess, &code );
    CloseHandle( pi.hProcess );
    if (secure) secure_leave();
    if (code == ERROR_SUCCESS) return ERROR_SUCCESS;
    return code == ERROR_CANCELLED ? ERROR_CANCELLED : ERROR_SERVICE_NOT_ACTIVE;
}

#ifdef WEXELEVATE_TEST_APPROVAL
static DWORD approve_stub( struct request *req )
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
#endif

static DWORD approve( struct request *req )
{
    BOOL absent;

    if (click_consent_selected( &absent ))
    {
#ifdef WEXELEVATE_TEST_APPROVAL
        if (absent) return approve_stub( req );   /* the test build keeps the step 4 stub for a request with no policy */
#endif
        return approve_consent( req, prompt_on_secure_desktop() );
    }
    /* a policy that selects no click consent (0, 1, 3, an unknown value): fail closed */
    return ERROR_NOT_READY;
}

/* ---- the launch ---------------------------------------------------------------------------------------------- */

/* Read NAME from a double-NUL-terminated environment block (a list of NAME=value wide strings), case-insensitively. */
static const WCHAR *env_lookup( const WCHAR *block, const WCHAR *name )
{
    INT len = lstrlenW( name );

    while (*block)
    {
        const WCHAR *eq = wcschr( block, L'=' );
        if (eq && (INT)(eq - block) == len && !_wcsnicmp( block, name, len )) return eq + 1;
        block += lstrlenW( block ) + 1;
    }
    return NULL;
}

/* An absolute path (a drive letter and a colon) with no .. component. */
static BOOL temp_path_ok( const WCHAR *path )
{
    const WCHAR *p;

    if ((path[0] < L'A' || path[0] > L'Z') && (path[0] < L'a' || path[0] > L'z')) return FALSE;
    if (path[1] != L':') return FALSE;
    for (p = path + 2; *p; p++)
    {
        if (*p != L'\\') continue;
        if (p[1] == L'.' && p[2] == L'.' && (p[3] == 0 || p[3] == L'\\')) return FALSE;
    }
    return TRUE;
}

/* Create each missing component of an absolute path from the root down. ERROR_ALREADY_EXISTS is success and any
 * other error is ignored, because the launch must not fail because of this. */
static void ensure_dir_chain( const WCHAR *path )
{
    WCHAR buf[MAX_PATH];
    WCHAR *p;

    if (lstrlenW( path ) >= MAX_PATH) return;
    lstrcpyW( buf, path );
    for (p = buf + 2; *p; p++)
    {
        if (*p != L'\\') continue;
        *p = 0;
        CreateDirectoryW( buf, NULL );
        *p = L'\\';
    }
    CreateDirectoryW( buf, NULL );
}

static DWORD launch( struct request *req, DWORD *pid, DWORD *tid, ULONGLONG *handle )
{
    STARTUPINFOEXW si = { { sizeof(si) } };
    PROCESS_INFORMATION pi = { 0 };
    SIZE_T attr_size = 0;
    HANDLE linked = NULL, remote = NULL;
    WCHAR *cmdline = NULL;
    void *default_env = NULL;
    void *env;
    BYTE user[sizeof(TOKEN_USER) + SECURITY_MAX_SID_SIZE];
    DWORD err = ERROR_SUCCESS, len, type, session;

    if (!GetTokenInformation( req->caller_token, TokenLinkedToken, &linked, sizeof(linked), &len ))
    {
        err = GetLastError();
        return err == ERROR_NO_SUCH_LOGON_SESSION ? ERROR_NOT_SUPPORTED : err ? err : ERROR_ACCESS_DENIED;
    }
    if (len != sizeof(linked) || !linked)
    {
        if (linked) CloseHandle( linked );
        return ERROR_ACCESS_DENIED;
    }
    /* the broker needs a usable primary token of the full half; an identification token is no use */
    if (!GetTokenInformation( linked, TokenType, &type, sizeof(type), &len ) || len != sizeof(type) || type != TokenPrimary)
    {
        CloseHandle( linked );
        return ERROR_NOT_SUPPORTED;
    }
    if (!GetTokenInformation( linked, TokenElevationType, &type, sizeof(type), &len ) ||
        len != sizeof(type) || type != TokenElevationTypeFull)
    {
        CloseHandle( linked );
        return ERROR_NOT_SUPPORTED;
    }
    /* the full token must belong to the caller's own logon (E1-H2); anything else is not the linked token we mean */
    {
        TOKEN_STATISTICS caller_stats, linked_stats;

        if (!token_stats( req->caller_token, &caller_stats ) || !token_stats( linked, &linked_stats ) ||
            !same_luid( caller_stats.AuthenticationId, req->caller_stats.AuthenticationId ) ||
            !same_luid( linked_stats.AuthenticationId, req->caller_stats.AuthenticationId ) ||
            !token_user_sid( linked, user, sizeof(user) ) || !IsValidSid( ((TOKEN_USER *)user)->User.Sid ) ||
            !EqualSid( ((TOKEN_USER *)user)->User.Sid, req->caller_sid ) ||
            !GetTokenInformation( linked, TokenSessionId, &session, sizeof(session), &len ) ||
            len != sizeof(session) || session != req->caller_session)
        {
            CloseHandle( linked );
            return ERROR_ACCESS_DENIED;
        }
    }

    if (req->flags & WEXELEVATE_ENV_INHERIT) env = req->env;
    else if (!CreateEnvironmentBlock( &default_env, linked, FALSE ))
    {
        err = GetLastError();
        CloseHandle( linked );
        return err ? err : ERROR_ENVVAR_NOT_FOUND;
    }
    else env = default_env;

    /* The elevated process runs in the machine root prefix, where the requesting user has no profile directory, so
     * TEMP and TMP point at a directory that does not exist and installers such as msiexec fail (GetTempFileNameW,
     * exit 1627). Create the chain; the launch must not fail because of this. */
    {
        /* Only the block the broker derives from the token is trusted: a caller-supplied block (WEXELEVATE_ENV_INHERIT)
         * must not choose which directories the privileged broker creates. */
        void *token_env = default_env;
        const WCHAR *var;

        if (!token_env && !CreateEnvironmentBlock( &token_env, linked, FALSE )) token_env = NULL;
        if (token_env)
        {
            if ((var = env_lookup( token_env, L"TEMP" )) && temp_path_ok( var )) ensure_dir_chain( var );
            if ((var = env_lookup( token_env, L"TMP" )) && temp_path_ok( var )) ensure_dir_chain( var );
            if (token_env != default_env) DestroyEnvironmentBlock( token_env );
        }
    }

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

    if ((err = check_binding( req, req->caller_pid, NULL, FALSE ))) goto done;
    if (WaitForSingleObject( stop_event, 0 ) != WAIT_TIMEOUT) { err = ERROR_CANCELLED; goto done; }
    /* the target starts suspended: it runs only once the handle, the lease and the guard are in place */
    if (!CreateProcessAsUserW( linked, req->image, cmdline, NULL, NULL, FALSE,
                               EXTENDED_STARTUPINFO_PRESENT | CREATE_UNICODE_ENVIRONMENT | CREATE_NEW_CONSOLE |
                               CREATE_SUSPENDED,
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
                              FALSE, 0 ))
        {
            err = GetLastError();
            goto fail;
        }
        *handle = (ULONGLONG)(ULONG_PTR)remote;
    }
    if (req->lease)
    {
        HANDLE guard_process = NULL;
        /* name the process to the lease: if a writer has opened the image meanwhile, it ends here */
        DWORD arm = pLeaseArm( req->lease, pi.dwProcessId );
        if (arm)
        {
            err = arm;
            goto fail;
        }
        /* Keep our own process handle through the new precommit checks. The
         * guard may observe lease-break death and close its handle at any time. */
        if (!DuplicateHandle( GetCurrentProcess(), pi.hProcess, GetCurrentProcess(), &guard_process,
                              0, FALSE, DUPLICATE_SAME_ACCESS ))
        {
            err = GetLastError();
            goto fail;
        }
        if (!start_guard( req, guard_process ))
        {
            CloseHandle( guard_process );
            err = ERROR_NOT_ENOUGH_MEMORY;
            goto fail;
        }
    }
    /* Last caller-object/version check, after suspended creation and lease setup.
     * Credential launches will additionally require a distinct administrator
     * logon; the linked-token branch above deliberately retains same-logon. */
    EnterCriticalSection( &table_cs );
    err = check_binding( req, req->caller_pid, NULL, FALSE );
    if (!err && WaitForSingleObject( stop_event, 0 ) != WAIT_TIMEOUT) err = ERROR_CANCELLED;
    if (!err && ResumeThread( pi.hThread ) == (DWORD)-1) err = GetLastError();
    LeaveCriticalSection( &table_cs );
    if (err)
    {
        goto fail;
    }
    CloseHandle( pi.hThread );
    CloseHandle( pi.hProcess );
    goto done;
fail:
    /* a rejected launch leaves no running child: end it, wait for it, and close the remote handle in the caller */
    if (remote) DuplicateHandle( req->caller, remote, NULL, NULL, 0, FALSE, DUPLICATE_CLOSE_SOURCE );
    *handle = 0;
    *pid = 0;
    *tid = 0;
    TerminateProcess( pi.hProcess, 1 );
    WaitForSingleObject( pi.hProcess, 5000 );
    CloseHandle( pi.hProcess );
    CloseHandle( pi.hThread );
    goto done;
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

#ifdef WEXELEVATE_TEST_APPROVAL
/* E1-S5 is deliberately unreachable in shipping builds. The native clock,
 * registrations, eligibility, verifier redemption and launch gates do not yet
 * exist. These fixtures exercise broker code, not authentication or execution. */
static UINT64 e1s_luid( LUID id )
{
    return ((UINT64)(DWORD)id.HighPart << 32) | id.LowPart;
}

static void e1s_caller_snapshot( const struct request *req, struct e1s_binding *s )
{
    memset( s, 0, sizeof(*s) );
    s->token_id = e1s_luid( req->caller_stats.TokenId );
    s->modified_id = e1s_luid( req->caller_stats.ModifiedId );
    s->auth_id = e1s_luid( req->caller_stats.AuthenticationId );
    s->start_time = req->caller_start.QuadPart;
    s->session = req->caller_session; s->sid_len = req->caller_sid_len;
    memcpy( s->sid, req->caller_sid, s->sid_len );
}

/* The consumed S3 message is never read again to refresh a stale binding.
 * e1s_advance checks a snapshot; other fixture operations hold table_cs. */
static BOOL e1s_checkpoint( struct request *req, struct e1s_attempt *a, UINT64 now,
                            const struct e1s_context *current )
{
    struct e1s_binding s;
    struct e1s_frozen copy = { 0 };
    BYTE digest[32];
    BOOL ok;
    if (a->state >= E1S_COMMITTED || !now || now >= a->frozen->deadline ||
        (e1s_nonzero( a->evidence_ref, 16 ) && now >= a->evidence_expiry) ||
        !e1s_same_generations( &a->context.gen, &current->gen ) ||
        memcmp( a->context.channel, current->channel, 16 ) ||
        WaitForSingleObject( stop_event, 0 ) != WAIT_TIMEOUT ||
        WaitForSingleObject( req->lease_event, 0 ) != WAIT_TIMEOUT ||
        !PeekNamedPipe( req->pipe, NULL, 0, NULL, NULL, NULL ) ||
        check_binding( req, req->caller_pid, NULL, FALSE )) return FALSE;
    e1s_caller_snapshot( req, &s );
    if (!e1s_binding_digest( current, &s, digest ) || memcmp( digest, a->frozen->binding_digest, 32 ) ||
        !e1s_sha256( "E1S1-REQ", sizeof("E1S1-REQ"), a->frozen->bytes, a->frozen->length, digest ) ||
        memcmp( digest, a->frozen->request_digest, 32 )) return FALSE;
    ok = e1s_freeze( &copy, req->id, now, a->frozen->deadline, current, &s, &req->info, req->hash,
                     req->image, req->cmdline, req->cwd, req->show, req->flags ) &&
         !memcmp( copy.request_digest, a->frozen->request_digest, 32 ) &&
         !memcmp( copy.environment_digest, a->frozen->environment_digest, 32 );
    free( copy.bytes );
    return ok;
}

static BOOL e1s_advance( struct request *req, struct e1s_attempt *a, enum e1s_state next,
                         UINT64 now, const struct e1s_context *current )
{
    struct e1s_attempt snapshot;
    BOOL ok;
    EnterCriticalSection( &table_cs );
    memcpy( &snapshot, a, sizeof(snapshot) );
    LeaveCriticalSection( &table_cs );
    ok = (next != E1S_COMMITTED || !verify_image( req )) && e1s_checkpoint( req, &snapshot, now, current );
    EnterCriticalSection( &table_cs );
    /* Cancellation or any other attempt change invalidates the checked snapshot.
     * Stop and lease break must still be clear immediately before committing. */
    ok = ok && !memcmp( a, &snapshot, sizeof(snapshot) ) &&
         WaitForSingleObject( stop_event, 0 ) == WAIT_TIMEOUT &&
         WaitForSingleObject( req->lease_event, 0 ) == WAIT_TIMEOUT;
    if (ok) ok = e1s_broker_step( a, next );
    if (!ok) e1s_cancel( a );
    LeaveCriticalSection( &table_cs );
    return ok;
}

static BOOL e1s_broker_answer( struct request *req, struct e1s_attempt *a, const struct e1s_answer *r,
                               UINT64 now, const struct e1s_context *current )
{
    BOOL ok;
    EnterCriticalSection( &table_cs );
    ok = e1s_checkpoint( req, a, now, current ) && e1s_answer_matches( a, r, now );
    if (!ok || r->answer_kind == 3) { e1s_cancel( a ); ok = FALSE; }
    else a->consent = TRUE; /* receipt alone cannot move to an authorization state */
    LeaveCriticalSection( &table_cs );
    return ok;
}

/* Binding metadata for the broker's consent challenge. S7 owns rendering and
 * the native conversation channel; credential payloads never enter this API. */
static BOOL e1s_broker_challenge( struct request *req, struct e1s_attempt *a, struct e1s_answer *r,
                                  UINT64 now, const struct e1s_context *current )
{
    BOOL ok;
    EnterCriticalSection( &table_cs );
    ok = a->state == E1S_AUTHENTICATING && e1s_checkpoint( req, a, now, current );
    if (ok)
    {
        memset( r, 0, sizeof(*r) );
        r->kind = 2; r->version = 1; r->length = E1S_ANSWER_LENGTH; r->caps = E1S_CAPS;
        r->id = a->frozen->id; r->attempt = a->number; r->round = a->round; r->index = a->index;
        r->answer_kind = 2;
        memcpy( r->nonce, a->nonce, 32 ); memcpy( r->binding_digest, a->frozen->binding_digest, 32 );
        memcpy( r->request_digest, a->frozen->request_digest, 32 ); memcpy( r->channel, current->channel, 16 );
    }
    else e1s_cancel( a );
    LeaveCriticalSection( &table_cs );
    return ok;
}

static BOOL e1s_broker_identity( struct request *req, struct e1s_attempt *a, const struct e1s_evidence *e,
                                 const BYTE ref[16], UINT64 now, const struct e1s_context *current, UINT64 uid )
{
    BOOL ok;
    EnterCriticalSection( &table_cs );
    ok = e1s_checkpoint( req, a, now, current ) && a->consent && e1s_nonzero( ref, 16 ) &&
         e1s_evidence_matches( a, e, now, &current->gen, uid );
    if (ok)
    {
        memcpy( a->evidence_ref, ref, 16 );
        a->evidence_completed = e->completed; a->evidence_expiry = e->expiry;
        ok = e1s_broker_step( a, E1S_IDENTITY_READY );
    }
    if (!ok) e1s_cancel( a );
    LeaveCriticalSection( &table_cs );
    return ok;
}

/* A real S8 transport must remove its connection-owned evidence atomically and
 * return the verifier's retained bytes. This local object is only a test double. */
struct e1s_test_verifier { struct e1s_evidence record; BYTE ref[16]; BOOL used; };

static BOOL e1s_test_same_object( HANDLE a, HANDLE b )
{
    BOOL (WINAPI *compare)( HANDLE, HANDLE ) =
        (void *)GetProcAddress( GetModuleHandleW( L"kernelbase.dll" ), "CompareObjectHandles" );
    return compare && compare( a, b );
}

static BOOL e1s_broker_consume( struct request *req, struct e1s_attempt *a, struct e1s_test_verifier *v,
                                UINT64 now, const struct e1s_context *current, UINT64 uid )
{
    BOOL ok;
    EnterCriticalSection( &table_cs );
    ok = e1s_checkpoint( req, a, now, current ) && a->state == E1S_IDENTITY_READY &&
         !a->redeemed && table_consume( req );
    if (ok)
    {
        ok = !v->used && !memcmp( v->ref, a->evidence_ref, 16 );
        v->used = TRUE; /* even a mismatching redemption cannot be retried */
        if (ok) ok = e1s_evidence_matches( a, &v->record, now, &current->gen, uid );
        if (ok) { a->redeemed = TRUE; ok = e1s_broker_step( a, E1S_CONSUMED ); }
    }
    if (!ok) e1s_cancel( a );
    LeaveCriticalSection( &table_cs );
    return ok;
}

/* Change the actual caller process primary token in the disposable fixture.
 * The duplicate has the same account/logon/session; only the object changes.
 * Always restore the exact original object before returning a completion marker.
 * No token/right is transferred to the client. ProcessAccessToken is private NT
 * class 9; mirror its two HANDLE fields for the external-SDK lab build. */
static BOOL e1s_test_token_replacement( struct request *req, struct e1s_attempt *a, enum e1s_state next,
                                       UINT64 now, const struct e1s_context *current )
{
    struct { HANDLE Token, Thread; } info = { 0 };
    HANDLE process = NULL, original = NULL, replacement = NULL;
    BOOL ok = FALSE, denied, advanced;
    NTSTATUS (WINAPI *set_process)( HANDLE, ULONG, void *, ULONG ) =
        (void *)GetProcAddress( GetModuleHandleW( L"ntdll.dll" ), "NtSetInformationProcess" );

    if (!set_process || !(process = OpenProcess( PROCESS_SET_INFORMATION, FALSE, req->caller_pid )) ||
        !e1s_test_same_object( process, req->caller ) ||
        !OpenProcessToken( req->caller, TOKEN_QUERY | TOKEN_DUPLICATE | TOKEN_ASSIGN_PRIMARY, &original ) ||
        !e1s_test_same_object( original, req->caller_token ) ||
        !DuplicateTokenEx( original, TOKEN_QUERY | TOKEN_ASSIGN_PRIMARY, NULL, SecurityImpersonation, TokenPrimary,
                           &replacement )) goto done;
    info.Token = replacement;
    if (!NT_SUCCESS(set_process( process, 9, &info, sizeof(info) ))) goto done;
    denied = check_binding( req, req->caller_pid, NULL, FALSE ) == ERROR_ACCESS_DENIED;
    advanced = e1s_advance( req, a, next, now, current );
    info.Token = original;
    if (!NT_SUCCESS(set_process( process, 9, &info, sizeof(info) ))) goto done;
    ok = denied && !advanced && a->state == E1S_CANCELLED &&
         !check_binding( req, req->caller_pid, NULL, FALSE );
done:
    if (replacement) CloseHandle( replacement );
    if (original) CloseHandle( original );
    if (process) CloseHandle( process );
    return ok;
}

static BOOL e1s_test_token_version( struct request *req, struct e1s_attempt *a, UINT64 now,
                                   const struct e1s_context *current )
{
    BYTE saved[8192]; /* bounded lab token, no changed DACL contents */
    TOKEN_STATISTICS before, after;
    HANDLE token;
    DWORD len;
    BOOL ok;
    if (!OpenProcessToken( req->caller, TOKEN_QUERY | TOKEN_ADJUST_DEFAULT, &token )) return FALSE;
    ok = e1s_test_same_object( token, req->caller_token ) && token_stats( token, &before ) &&
         GetTokenInformation( token, TokenDefaultDacl, saved, sizeof(saved), &len ) &&
         SetTokenInformation( token, TokenDefaultDacl, saved, len ) && token_stats( token, &after ) &&
         same_luid( before.TokenId, after.TokenId ) && !same_luid( before.ModifiedId, after.ModifiedId ) &&
         check_binding( req, req->caller_pid, NULL, FALSE ) == ERROR_ACCESS_DENIED &&
         !e1s_advance( req, a, E1S_AUTHENTICATING, now, current ) && a->state == E1S_CANCELLED;
    CloseHandle( token );
    return ok;
}

static DWORD e1s6_probe( struct request *req, DWORD mode, const struct e1s_frozen *f,
                         const BYTE *nonce, struct wexelevate_reply *reply )
{
    DWORD (WINAPI *transport)( HANDLE, DWORD, void *, DWORD, UINT64 * ) =
        (void *)GetProcAddress( GetModuleHandleW( L"wexhost.dll" ), "WexE1s6Transport" );
    BYTE packet[E1S6_REG_SIZE + E1S6_ANSWER_SIZE] = { 0 }, *a = packet + E1S6_REG_SIZE;
    UINT64 channel = 0, old_channel;
    DWORD err = ERROR_INVALID_DATA, result = ERROR_SUCCESS;
    BOOL passed = FALSE;
    if (!transport) { E1S_AUDIT( "probe-transport-export" ); return ERROR_NOT_SUPPORTED; }
#define S6_EXPECT(c, site) do { if (!(c)) { \
    fprintf( stderr, "E1S6_DENY broker site=%s mode=%lu transport_error=%lu fixture_error=%lu\n", \
             site, (unsigned long)mode, (unsigned long)result, (unsigned long)err ); goto done; \
} } while (0)
    result = transport( req->pipe, E1S6_OPEN, packet, E1S6_OPEN_SIZE, &channel );
    S6_EXPECT( !result, "probe-open" );
    memset( a, 0, E1S6_ANSWER_SIZE );
    e1s6_put32(a, 0x10002); e1s6_put32(a + 4, E1S6_ANSWER_SIZE);
    e1s6_put64(a + 16, f->id); e1s6_put32(a + 24, 1); e1s6_put32(a + 28, 1);
    e1s6_put32(a + 36, 2);
    memcpy(a + 40, nonce, 32); memcpy(a + 72, f->binding_digest, 32);
    memcpy(a + 104, f->request_digest, 32); memcpy(a + 136, packet + 88, 16);
    if (mode == E1S_TRANSPORT_CHILD_IMPOSTOR)
    {
        result = transport( req->pipe, E1S6_TEST_CHILD_IMPOSTOR, packet, sizeof(packet), &channel );
        S6_EXPECT( result == ERROR_ACCESS_DENIED && !channel, "probe-child-impostor" );
        S6_EXPECT( !check_binding( req, req->caller_pid, NULL, FALSE ), "probe-impostor-binding" );
        passed = TRUE; err = ERROR_ACCESS_DENIED; goto done;
    }
    result = transport( req->pipe, E1S6_CHILD_START, packet, sizeof(packet), &channel );
    S6_EXPECT( !result, "probe-child-start" );
    old_channel = channel;
    if (mode == E1S_TRANSPORT_STALE) packet[48] ^= 1;
    if (mode == E1S_TRANSPORT_CANCEL) e1s6_put32(a + 36, 3);
    if (mode == E1S_TRANSPORT_OWNER)
    {
        result = transport( req->pipe, E1S6_TEST_WRONG_OWNER, packet, sizeof(packet), &channel );
        S6_EXPECT( !result, "probe-inject-owner" );
    }
    if (mode == E1S_TRANSPORT_LATE)
    {
        result = transport( req->pipe, E1S6_TEST_KILL_CHILD, packet, sizeof(packet), &channel );
        S6_EXPECT( !result, "probe-kill-child" );
    }
    if (mode == E1S_TRANSPORT_DISCONNECT)
    {
        result = transport( req->pipe, E1S6_TEST_DISCONNECT, packet, sizeof(packet), &channel );
        S6_EXPECT( !result && !channel, "probe-disconnect" );
    }
    else
    {
        result = transport( req->pipe, E1S6_EXCHANGE, packet, sizeof(packet), &channel );
        if (mode == E1S_TRANSPORT_ACCEPT || mode == E1S_TRANSPORT_REPLAY)
        {
            S6_EXPECT( !result, "probe-exchange" );
            if (mode == E1S_TRANSPORT_REPLAY)
            {
                result = transport( req->pipe, E1S6_EXCHANGE, packet, sizeof(packet), &channel );
                S6_EXPECT( result == ERROR_ACCESS_DENIED && !channel, "probe-replay" );
            }
        }
        else S6_EXPECT( result == ERROR_ACCESS_DENIED && !channel, "probe-negative-exchange" );
    }
    if (mode != E1S_TRANSPORT_ACCEPT)
    {
        /* A late call using the revoked local channel cannot reopen it. */
        UINT64 stale = old_channel;
        result = transport( req->pipe, E1S6_EXCHANGE, packet, sizeof(packet), &stale );
        S6_EXPECT( result && !stale, "probe-revoked-channel" );
    }
    S6_EXPECT( !check_binding( req, req->caller_pid, NULL, FALSE ), "probe-final-binding" );
    passed = TRUE;
    err = mode == E1S_TRANSPORT_ACCEPT ? ERROR_SUCCESS : ERROR_ACCESS_DENIED;
done:
#undef S6_EXPECT
    if (channel) transport( req->pipe, E1S6_CLOSE, NULL, 0, &channel );
    if (passed) { reply->pid = E1S_PROBE_MAGIC; reply->tid = mode; reply->handle = 0; }
    return err;
}

static DWORD e1s_probe( struct request *req, DWORD mode, struct wexelevate_reply *reply )
{
    struct e1s_context c = { .gen = { 1, 2, 3, 4, 5, 6 }, .host_uid = 1000 };
    struct e1s_binding s, changed;
    struct e1s_frozen f = { 0 };
    struct e1s_attempt a, trial;
    struct e1s_answer answer;
    struct e1s_answer bad;
    struct e1s_test_verifier v = { 0 };
    BYTE nonce[32], account[16] = { 42 }, ref[16] = { 24 };
    UINT64 now = 1000, deadline = 120000001000ULL, uid = 1001, admin;
    DWORD err = ERROR_INVALID_DATA;
    BOOL ok;

    /* Explicitly synthetic endpoint/account identities and host clock. Actual
     * caller/token/SID/session and held image/hash always come from this request. */
    c.prefix[0] = 1; c.host_process[0] = 2; c.opener[0] = c.writer[0] = 3;
    c.desktop[0] = 4; c.channel[0] = 5;
    if (!NT_SUCCESS(BCryptGenRandom( NULL, nonce, sizeof(nonce), BCRYPT_USE_SYSTEM_PREFERRED_RNG )) ||
        !e1s_nonzero( nonce, sizeof(nonce) )) return ERROR_GEN_FAILURE;
    if (check_binding( req, req->caller_pid, NULL, FALSE )) return ERROR_ACCESS_DENIED;
    e1s_caller_snapshot( req, &s );
    changed = s; changed.token_id ^= 1;
    if (e1s_same_binding( &s, &changed )) { E1S_AUDIT( "probe-binding-comparator" ); return ERROR_INVALID_DATA; }
    if (!e1s_freeze( &f, req->id, now, deadline, &c, &s, &req->info, req->hash,
                     req->image, req->cmdline, req->cwd, req->show, req->flags ))
    { E1S_AUDIT( "probe-freeze-record" ); goto done; }
    if (mode >= E1S_TRANSPORT_ACCEPT)
    {
        err = e1s6_probe( req, mode, &f, nonce, reply );
        goto done;
    }
    if (!e1s_begin( &a, &f, &c, 1, nonce, account )) goto done;
    if (!e1s_advance( req, &a, E1S_PROMPTING, now, &c )) goto done;
    if (mode == E1S_TOKEN_REPLACED)
    {
        if (!e1s_test_token_replacement( req, &a, E1S_AUTHENTICATING, now, &c )) goto done;
        goto denied;
    }
    if (mode == E1S_VERSION)
    {
        if (!e1s_test_token_version( req, &a, now, &c )) goto done;
        goto denied;
    }
    if (!e1s_advance( req, &a, E1S_AUTHENTICATING, now, &c )) goto done;
    if (!e1s_broker_challenge( req, &a, &answer, now, &c )) goto done;
    bad = answer;
    switch (mode)
    {
    case E1S_REQUEST_ID: bad.id ^= 1; break;
    case E1S_NONCE: bad.nonce[0] ^= 1; break;
    case E1S_DIGEST: bad.request_digest[0] ^= 1; break;
    case E1S_BINDING: bad.binding_digest[0] ^= 1; break;
    case E1S_SEQUENCE: bad.index++; break;
    default: break;
    }
    if (mode == E1S_REQUEST_ID || mode == E1S_NONCE || mode == E1S_DIGEST ||
        mode == E1S_BINDING || mode == E1S_SEQUENCE)
    {
        trial = a;
        if (e1s_broker_answer( req, &trial, &bad, now, &c ) || trial.state != E1S_CANCELLED) goto done;
    }
    if (!e1s_broker_answer( req, &a, &answer, now, &c )) goto done;
    /* Replaying an otherwise correct answer also fails and cancels its copy. */
    trial = a;
    if (e1s_broker_answer( req, &trial, &answer, now, &c ) || trial.state != E1S_CANCELLED) goto done;
    v.record.kind = 3; v.record.version = 1; v.record.length = E1S_EVIDENCE_LENGTH; v.record.caps = E1S_CAPS;
    v.record.id = f.id; v.record.attempt = a.number; v.record.uid = uid;
    v.record.completed = now; v.record.expiry = deadline - 1; v.record.gen = c.gen;
    v.record.method = v.record.service = 1;
    memcpy( v.record.nonce, a.nonce, 32 ); memcpy( v.record.account, account, 16 );
    memcpy( v.record.request_digest, f.request_digest, 32 ); memcpy( v.record.binding_digest, f.binding_digest, 32 );
    memcpy( v.ref, ref, 16 );
    switch (mode)
    {
    case E1S_REQUEST_ID: v.record.id ^= 1; break;
    case E1S_NONCE: v.record.nonce[0] ^= 1; break;
    case E1S_DIGEST: v.record.request_digest[0] ^= 1; break;
    case E1S_BINDING: v.record.binding_digest[0] ^= 1; break;
    case E1S_SEQUENCE: v.record.attempt++; break;
    case E1S_EXPIRED: v.record.expiry = now; break;
    case E1S_ACCOUNT: v.record.account[0] ^= 1; break;
    case E1S_GENERATION: c.gen.policy++; break;
    default: break;
    }
    ok = e1s_broker_identity( req, &a, &v.record, ref, now, &c, uid );
    if (mode == E1S_REQUEST_ID || mode == E1S_NONCE || mode == E1S_DIGEST || mode == E1S_BINDING ||
        mode == E1S_SEQUENCE || mode == E1S_EXPIRED || mode == E1S_ACCOUNT || mode == E1S_GENERATION)
    {
        if (ok || a.state != E1S_CANCELLED) goto done;
        goto denied;
    }
    if (!ok) goto done;
    if (mode == E1S_EXPIRY_CONSUME || mode == E1S_REDEMPTION)
    {
        /* Different retained expiry at redemption is a mismatch even if both
         * timestamps are still in the future. Expiry itself is exclusive. */
        if (mode == E1S_REDEMPTION) v.record.expiry--;
        else now = v.record.expiry;
        if (e1s_broker_consume( req, &a, &v, now, &c, uid ) || a.state != E1S_CANCELLED) goto done;
        goto denied;
    }
    if (mode == E1S_CANCEL)
    {
        EnterCriticalSection( &table_cs );
        e1s_cancel( &a ); e1s_cancel( &a );
        LeaveCriticalSection( &table_cs );
        if (e1s_broker_consume( req, &a, &v, now, &c, uid ) || a.state != E1S_CANCELLED || v.used) goto done;
        goto denied;
    }
    trial = a; /* preserve identity-ready to also test replay past the table gate */
    if (!e1s_broker_consume( req, &a, &v, now, &c, uid ) || !v.used) goto done;
    if (mode == E1S_REPLAY)
    {
        if (e1s_broker_consume( req, &trial, &v, now, &c, uid ) || trial.state != E1S_CANCELLED) goto done;
        if (e1s_broker_consume( req, &a, &v, now, &c, uid ) || a.state != E1S_CANCELLED) goto done;
        goto denied;
    }
    admin = s.auth_id ^ 1;
    if (!admin) admin = 2;
    if (mode == E1S_LOGON) admin = s.auth_id;
    if (!e1s_admin_logon( s.auth_id, admin, s.session, s.session ))
    {
        e1s_cancel( &a );
        if (mode != E1S_LOGON) goto done;
        goto denied;
    }
    a.admin_auth_id = admin; /* simulated token validation, never an issued token */
    if (!e1s_advance( req, &a, E1S_LAUNCH_PREPARED, now, &c )) goto done;
    if (mode == E1S_TOKEN_COMMIT)
    {
        if (!e1s_test_token_replacement( req, &a, E1S_COMMITTED, now, &c )) goto done;
        goto denied;
    }
    if (mode == E1S_EXPIRY_COMMIT || mode == E1S_MUTATED_REQUEST)
    {
        if (mode == E1S_EXPIRY_COMMIT) now = v.record.expiry;
        else req->show ^= 1;
        ok = e1s_advance( req, &a, E1S_COMMITTED, now, &c );
        if (mode == E1S_MUTATED_REQUEST) req->show ^= 1;
        if (ok || a.state != E1S_CANCELLED) goto done;
        goto denied;
    }
    if (!e1s_advance( req, &a, E1S_COMMITTED, now, &c ) || e1s_cancel( &a ) || a.state != E1S_COMMITTED) goto done;
    err = ERROR_SUCCESS;
    goto passed;
denied:
    err = ERROR_ACCESS_DENIED;
passed:
    /* A lab result marker, never a target PID or authorization capability. */
    reply->pid = E1S_PROBE_MAGIC; reply->tid = mode; reply->handle = 0;
done:
    free( f.bytes );
    SecureZeroMemory( nonce, sizeof(nonce) );
    return err;
}
#endif

static DWORD handle_request( struct request *req, BYTE *buf, DWORD size, struct wexelevate_reply *reply )
{
    DWORD err;
#ifdef WEXELEVATE_TEST_APPROVAL
    DWORD probe = 0;
    /* One message: private lab envelope followed by a normal v1 request. */
    if (size >= 8 && ((DWORD *)buf)[0] == E1S_PROBE_MAGIC)
    {
        probe = ((DWORD *)buf)[1];
        if (!probe || probe >= E1S_CASE_COUNT) return ERROR_INVALID_PARAMETER;
        buf += 8; size -= 8;
    }
#endif

    if (!system_role) return ERROR_NOT_SUPPORTED;
    if ((err = identify_caller( req ))) { E1S_AUDIT( "request-identify" ); return err; }
    if ((err = parse_request( req, buf, size ))) { E1S_AUDIT( "request-parse" ); return err; }
    if ((err = table_add( req ))) { E1S_AUDIT( "request-table" ); return err; }
    if ((err = freeze_image( req ))) { E1S_AUDIT( "request-freeze-image" ); return err; }
#ifdef WEXELEVATE_TEST_APPROVAL
    if (probe) return e1s_probe( req, probe, reply );
#endif
    if ((err = check_binding( req, req->caller_pid, NULL, FALSE ))) return err;
    if ((err = approve( req ))) return err;
    if ((err = check_binding( req, req->caller_pid, NULL, FALSE ))) return err;
    /* from here on the request is used: once */
    if (!table_consume( req )) return ERROR_CANCELLED;
    if (WaitForSingleObject( req->caller, 0 ) != WAIT_TIMEOUT) return ERROR_CANCELLED;
    if ((err = verify_image( req ))) return err;
    if (req->lease && ((err = pLeaseArm( req->lease, 0 )) || WaitForSingleObject( req->lease_event, 0 ) != WAIT_TIMEOUT))
        return err ? err : ERROR_FILE_CORRUPT;
    if (WaitForSingleObject( stop_event, 0 ) != WAIT_TIMEOUT) return ERROR_CANCELLED;
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
    /* the handle is in the caller's table: if the reply does not reach the caller, close it there, and leave the
     * process alone (it was committed) */
    if (pipe_io( req->pipe, &reply, sizeof(reply), TRUE, READ_TIMEOUT, &done ) && !reply.error && reply.handle)
        DuplicateHandle( req->caller, (HANDLE)(ULONG_PTR)reply.handle, NULL, NULL, 0, FALSE, DUPLICATE_CLOSE_SOURCE );
    FlushFileBuffers( req->pipe );
done:
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
        EnterCriticalSection( &table_cs );
        SetEvent( stop_event );
        LeaveCriticalSection( &table_cs );
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
    EnterCriticalSection( &table_cs );
    SetEvent( stop_event );
    LeaveCriticalSection( &table_cs );
    while (active_threads) Sleep( 50 );
}

static void WINAPI service_main( DWORD argc, WCHAR **argv )
{
    InitializeCriticalSection( &table_cs );
    status_handle = RegisterServiceCtrlHandlerExW( WEXELEVATE_SERVICE, handler, NULL );
    if (!status_handle) return;
    report( SERVICE_START_PENDING, 0 );
    stop_event = CreateEventW( NULL, TRUE, FALSE, NULL );
    InitializeCriticalSection( &secure_cs );
    system_role = token_is_system_with_tcb();
    if (!load_wexhost()) pLeaseTake = NULL;   /* no lease possible: every request then fails closed */
    serve();
    if (secure_desktop) return_input();   /* whatever happened: the input goes back */
    report( SERVICE_STOPPED, 0 );
}

int wmain( int argc, WCHAR **argv )
{
    static WCHAR name[] = WEXELEVATE_SERVICE;
    SERVICE_TABLE_ENTRYW table_entries[] = { { name, service_main }, { NULL, NULL } };

    StartServiceCtrlDispatcherW( table_entries );
    return 0;
}

/*
 * wexaccounts: the Windows side of Local Users and Groups (docs/LOCAL_USERS_DESIGN.md, "Draft 3" and the amendments H1
 * to H3)
 *
 * A LocalSystem service of session 0. It listens on the accounts pipes (\\.\pipe\WexSession0\accounts for the processes
 * of its own server, \\.\pipe\accounts for the callers of the sessions that the pipe bridge brings in), speaks the frame
 * of wexaccounts_proto.h with netapi32, and hands each request to the accounts helper through the server (the request
 * accounts_forward, which makes the verdict from the connection's client token and the frame's header itself).
 *
 * It is a thin relay and keeps three things:
 *   - the syntax and the limits of the pipe (the frame, the fields, the operation, one request in flight per
 *     connection, the number of connections): nothing malformed reaches the helper;
 *   - the order of changes: one at a time service-wide, so that the helper's last-administrator rule and its change
 *     cannot race (U6);
 *   - the wait after a change that the account map must show (a created, deleted or renamed user): it asks the helper to
 *     run the sync, then watches the map's generation for about 10 seconds, and answers success with the warning
 *     WEXA_SYNC_PENDING when the map has not shown it (U4). The wait is on the connection's own thread, never on the
 *     service's control thread.
 * It decides no policy and never says who the caller is: the verdict is the server's (H1), the rules are the helper's.
 *
 * Copyright 2026 WineExplorer contributors
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
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <windef.h>
#include <winbase.h>
#include <winsvc.h>
#include <sddl.h>

#include "wexaccounts_proto.h"

#define SERVICE_NAME L"wexaccounts"
#define PIPE_LOCAL   L"\\\\.\\pipe\\WexSession0\\accounts"   /* the processes of this server (session 0's, or every one in a plain prefix) */
#define PIPE_BRIDGED L"\\\\.\\pipe\\accounts"                /* a session's callers, through the bridge (the name the bridge allows) */
#define MAP_PATH     L"Z:\\etc\\wineexplorer\\accounts.map"
#define MAX_CONNECTIONS 128
#define READ_TIMEOUT_MS 30000
#define FORWARD_TIMEOUT_MS 15000
#define SYNC_WAIT_MS 10000
#define SYNC_POLL_MS 250

static SERVICE_STATUS_HANDLE status_handle;
static SERVICE_STATUS service_status = { SERVICE_WIN32_OWN_PROCESS, SERVICE_START_PENDING, 0, NO_ERROR, 0, 0, 0 };
static HANDLE stop_event;
static CRITICAL_SECTION change_lock;
static LONG connections;

#ifdef WEXACCOUNTS_LAB
/* a lab build (scripts/test_accounts_service.*): the helper is a stand-in, the map is a file of the test */
static WCHAR lab_map[MAX_PATH];
static const WCHAR *map_path(void) { return lab_map[0] ? lab_map : MAP_PATH; }
#else
static const WCHAR *map_path(void) { return MAP_PATH; }
#endif

/* ------------------------------------------------------------------------------------------ the helper */

/* One operation to the helper through the server. The server checks the pipe (the reserved name, a bridged connection,
 * an instance made under the reservation), makes the verdict from the client token it recorded for that connection and
 * the caller's unix uid from the link, builds the frame's header (op, request id, flags 0, verdict, caller uid, body
 * length) in front of the body given here, and sends it to the helper; whatever the client wrote in a header is gone by
 * now. Returns FALSE when the helper could not be reached at all (no reply in time, no connection, no server request). */
static BOOL forward( HANDLE pipe, unsigned int op, const BYTE *body, DWORD body_len, BYTE *reply, DWORD *reply_len,
                     unsigned int *status );

#ifdef WEXACCOUNTS_LAB
/* the stand-in: lab_calls counts what reached it, a user named "slow" takes 3 s, "down" is unreachable, "last" is the last
 * administrator; everything else works. Serves the tests of the service: syntax, order, wait, caps. */
static LONG lab_calls, lab_in_change, lab_max_in_change;

static BOOL forward( HANDLE pipe, unsigned int op, const BYTE *body, DWORD body_len, BYTE *reply, DWORD *reply_len,
                     unsigned int *status )
{
    BOOL change = op >= WEXA_CREATE_USER && op != WEXA_START_SYNC;
    const char *name = body_len > 2 ? (const char *)body + 2 : "";
    WORD n = body_len >= 2 ? *(const WORD *)body : 0;

    InterlockedIncrement( &lab_calls );
    if (change)
    {
        LONG now = InterlockedIncrement( &lab_in_change );
        if (now > lab_max_in_change) lab_max_in_change = now;   /* the test reads this: the lock keeps it at 1 */
    }
    *reply_len = 0;
    *status = WEXA_OK;
    if (n == 4 && !memcmp( name, "slow", 4 )) Sleep( 3000 );
    if (n == 4 && !memcmp( name, "down", 4 )) *status = WEXA_UNAVAILABLE;
    if (n == 4 && !memcmp( name, "last", 4 )) *status = WEXA_LAST_ADMIN;
    if (op == WEXA_GET_USER && *status == WEXA_OK)
    {
        /* a record: name, full name "Stand-in", uid 1234, standard, not locked */
        BYTE *p = reply;
        WORD l = n, f = 8;
        DWORD v[3] = { 1234, 0, 0 };

        memcpy( p, &l, 2 ); memcpy( p + 2, name, n ); p += 2 + n;
        memcpy( p, &f, 2 ); memcpy( p + 2, "Stand-in", 8 ); p += 2 + 8;
        memcpy( p, v, 12 ); p += 12;
        *reply_len = (DWORD)(p - reply);
    }
    if (change) InterlockedDecrement( &lab_in_change );
    return *status != WEXA_UNAVAILABLE;
}
#else
static BOOL forward( HANDLE pipe, unsigned int op, const BYTE *body, DWORD body_len, BYTE *reply, DWORD *reply_len,
                     unsigned int *status )
{
    /* the server's request accounts_forward (the L1 session's work, docs/LOCAL_USERS_DESIGN.md "Amendments H1 and H2")
     * does not exist yet: until it does the service answers that the helper is not reachable, which netapi32 maps to
     * RPC_S_SERVER_UNAVAILABLE */
    *reply_len = 0;
    *status = WEXA_UNAVAILABLE;
    return FALSE;
}
#endif

/* ------------------------------------------------------------------------------------------ the map */

/* the generation in the first line of the account map ("WEXACCOUNTS <version> <generation> ..."), or 0 when it cannot be
 * read; the service's account (session 0's) can read the file, nothing else is asked of it */
static unsigned long long map_generation( void )
{
    HANDLE file = CreateFileW( map_path(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL,
                               OPEN_EXISTING, 0, NULL );
    char line[128];
    DWORD got = 0;
    unsigned int version = 0;
    unsigned long long generation = 0;

    if (file == INVALID_HANDLE_VALUE) return 0;
    if (!ReadFile( file, line, sizeof(line) - 1, &got, NULL )) got = 0;
    CloseHandle( file );
    line[got] = 0;
    if (sscanf( line, "WEXACCOUNTS %u %llu", &version, &generation ) != 2) return 0;
    return generation;
}

/* after a change the map must show: the sync is asked for and the generation watched; TRUE when it moved */
static BOOL wait_for_map( HANDLE pipe, unsigned long long before )
{
    BYTE reply[64];
    DWORD reply_len, waited;
    unsigned int status;

    if (!before) return TRUE;   /* no map to watch (no map yet, or not readable): nothing to wait for */
    forward( pipe, WEXA_START_SYNC, NULL, 0, reply, &reply_len, &status );
    for (waited = 0; waited < SYNC_WAIT_MS; waited += SYNC_POLL_MS)
    {
        if (map_generation() > before) return TRUE;
        if (WaitForSingleObject( stop_event, SYNC_POLL_MS ) == WAIT_OBJECT_0) break;
    }
    return map_generation() > before;
}

/* ------------------------------------------------------------------------------------------ the frame */

/* what the body of an operation is made of: S a string (a u16 length of at most 255 and as many bytes, none of them a NUL),
 * U a u32; "?" before a letter makes the rest optional */
static const char *const body_shapes[WEXA_OP_LAST] =
{
    [WEXA_LIST_USERS] = "?U", [WEXA_GET_USER] = "S", [WEXA_ADMIN_MEMBERS] = "?U", [WEXA_CREATE_USER] = "SSUS",
    [WEXA_DELETE_USER] = "S", [WEXA_RENAME_USER] = "SS", [WEXA_SET_FULLNAME] = "SS", [WEXA_SET_LOCKED] = "SU",
    [WEXA_SET_TYPE] = "SU", [WEXA_SET_PASSWORD] = "SS", [WEXA_START_SYNC] = ""
};

static BOOL body_ok( unsigned int op, const BYTE *p, DWORD len )
{
    const char *shape = body_shapes[op];
    const BYTE *end = p + len;
    BOOL optional = FALSE;

    for (; *shape; shape++)
    {
        if (*shape == '?') { optional = TRUE; continue; }
        if (p == end && optional) return TRUE;
        if (*shape == 'U')
        {
            if (end - p < 4) return FALSE;
            p += 4;
        }
        else
        {
            WORD n;
            DWORD i;

            if (end - p < 2) return FALSE;
            memcpy( &n, p, 2 );
            p += 2;
            if (n > WEXA_MAX_STR || end - p < n) return FALSE;
            for (i = 0; i < n; i++) if (!p[i]) return FALSE;
            p += n;
        }
    }
    return p == end;
}

static BOOL send_reply( HANDLE pipe, DWORD request_id, unsigned int status, const BYTE *body, DWORD body_len )
{
    static const struct wexa_reply empty = { WEXA_MAGIC, WEXA_VERSION, 0, 0, 0 };
    BYTE *frame = malloc( sizeof(empty) + body_len );
    struct wexa_reply head = empty;
    DWORD written = 0, size = sizeof(head) + body_len;
    OVERLAPPED ov = {0};
    BOOL ok;

    if (!frame) return FALSE;
    head.status = (WORD)status;
    head.request_id = request_id;
    head.body_len = body_len;
    memcpy( frame, &head, sizeof(head) );
    if (body_len) memcpy( frame + sizeof(head), body, body_len );
    /* the pipe was made for overlapped I/O: every write waits on its own event */
    ov.hEvent = CreateEventW( NULL, TRUE, FALSE, NULL );
    ok = WriteFile( pipe, frame, size, NULL, &ov ) || GetLastError() == ERROR_IO_PENDING;
    if (ok) ok = WaitForSingleObject( ov.hEvent, FORWARD_TIMEOUT_MS ) == WAIT_OBJECT_0 &&
                 GetOverlappedResult( pipe, &ov, &written, FALSE ) && written == size;
    CloseHandle( ov.hEvent );
    free( frame );
    return ok;
}

/* ------------------------------------------------------------------------------------------ a connection */

/* one message from the pipe with the connection's read time out; FALSE ends the connection (closed, too long, late) */
static BOOL read_request( HANDLE pipe, BYTE *buf, DWORD size, DWORD *got )
{
    OVERLAPPED ov = {0};
    HANDLE handles[2];
    DWORD wait;

    ov.hEvent = CreateEventW( NULL, TRUE, FALSE, NULL );
    handles[0] = ov.hEvent;
    handles[1] = stop_event;
    *got = 0;
    if (!ReadFile( pipe, buf, size, NULL, &ov ) && GetLastError() != ERROR_IO_PENDING)
    {
        CloseHandle( ov.hEvent );
        return FALSE;
    }
    wait = WaitForMultipleObjects( 2, handles, FALSE, READ_TIMEOUT_MS );
    if (wait != WAIT_OBJECT_0)
    {
        CancelIoEx( pipe, &ov );
        GetOverlappedResult( pipe, &ov, got, TRUE );
        CloseHandle( ov.hEvent );
        return FALSE;
    }
    if (!GetOverlappedResult( pipe, &ov, got, FALSE ))
    {
        CloseHandle( ov.hEvent );
        return FALSE;   /* ERROR_MORE_DATA: a message over the frame's limit; the connection is closed */
    }
    CloseHandle( ov.hEvent );
    return TRUE;
}

static void serve_request( HANDLE pipe, const struct wexa_request *req, const BYTE *body )
{
    BYTE reply[WEXA_MAX_FRAME];
    DWORD reply_len = 0;
    unsigned int status = WEXA_OK;
    BOOL change = req->op >= WEXA_CREATE_USER, wait_map;
    unsigned long long before = 0;
    BOOL reached;

    wait_map = req->op == WEXA_CREATE_USER || req->op == WEXA_DELETE_USER || req->op == WEXA_RENAME_USER;
    if (change)
    {
        EnterCriticalSection( &change_lock );
        if (wait_map) before = map_generation();
    }
    reached = forward( pipe, req->op, body, req->body_len, reply, &reply_len, &status );
    if (change) LeaveCriticalSection( &change_lock );
    if (!reached || reply_len > sizeof(reply) - sizeof(struct wexa_reply))
    {
        send_reply( pipe, req->request_id, WEXA_UNAVAILABLE, NULL, 0 );
        return;
    }
    if (status == WEXA_OK && wait_map && !wait_for_map( pipe, before )) status = WEXA_SYNC_PENDING;
    send_reply( pipe, req->request_id, status, status == WEXA_OK || status == WEXA_SYNC_PENDING ? reply : NULL,
                status == WEXA_OK || status == WEXA_SYNC_PENDING ? reply_len : 0 );
}

static DWORD WINAPI connection_thread( void *param )
{
    HANDLE pipe = param;
    BYTE *buf = malloc( WEXA_MAX_FRAME + 1 );
    DWORD got;

    while (buf && read_request( pipe, buf, WEXA_MAX_FRAME + 1, &got ))
    {
        struct wexa_request req;

        /* a frame of the wrong size, magic or version, an operation that does not exist or a body that does not fit the
         * operation ends the connection (the helper does the same): nothing malformed goes on to the helper. Whatever
         * the header says about the verdict and the caller is ignored: the server writes its own. */
        if (got < sizeof(req)) break;
        memcpy( &req, buf, sizeof(req) );
        if (req.magic != WEXA_MAGIC || req.version != WEXA_VERSION || got != sizeof(req) + req.body_len) break;
        if (req.op < WEXA_LIST_USERS || req.op >= WEXA_OP_LAST) break;
        if (req.flags) { send_reply( pipe, req.request_id, WEXA_BAD_REQUEST, NULL, 0 ); continue; }
        if (!body_ok( req.op, buf + sizeof(req), req.body_len )) { send_reply( pipe, req.request_id, WEXA_BAD_REQUEST, NULL, 0 ); continue; }
        serve_request( pipe, &req, buf + sizeof(req) );
        SecureZeroMemory( buf, got );   /* a request may carry a password hash */
    }
    if (buf) { SecureZeroMemory( buf, WEXA_MAX_FRAME + 1 ); free( buf ); }
    DisconnectNamedPipe( pipe );
    CloseHandle( pipe );
    InterlockedDecrement( &connections );
    return 0;
}

/* ------------------------------------------------------------------------------------------ the listeners */

struct listener { const WCHAR *name; };

static DWORD WINAPI listener_thread( void *param )
{
    const struct listener *l = param;
    SECURITY_ATTRIBUTES sa = { sizeof(sa), NULL, FALSE };
    /* the system and the administrators in full, everyone may connect and talk; who may change what is the verdict's */
    static const WCHAR sddl[] = L"D:(A;;GA;;;SY)(A;;GA;;;BA)(A;;GRGW;;;WD)";
    DWORD flags = PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED | FILE_FLAG_FIRST_PIPE_INSTANCE;
    OVERLAPPED ov = {0};
    HANDLE handles[2];

    ConvertStringSecurityDescriptorToSecurityDescriptorW( sddl, SDDL_REVISION_1, &sa.lpSecurityDescriptor, NULL );
    ov.hEvent = CreateEventW( NULL, TRUE, FALSE, NULL );
    handles[0] = ov.hEvent;
    handles[1] = stop_event;
    while (WaitForSingleObject( stop_event, 0 ) != WAIT_OBJECT_0)
    {
        HANDLE pipe = CreateNamedPipeW( l->name, flags, PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE | PIPE_WAIT |
                                        PIPE_REJECT_REMOTE_CLIENTS, PIPE_UNLIMITED_INSTANCES, WEXA_MAX_FRAME,
                                        WEXA_MAX_FRAME, 0, &sa );
        DWORD err, wait;

        if (pipe == INVALID_HANDLE_VALUE)
        {
            /* the first instance of the name is made once; a name that is taken (a squatter) is not served */
            if (flags & FILE_FLAG_FIRST_PIPE_INSTANCE) { Sleep( 1000 ); continue; }
            Sleep( 250 );
            continue;
        }
        flags &= ~FILE_FLAG_FIRST_PIPE_INSTANCE;
        ResetEvent( ov.hEvent );
        err = ConnectNamedPipe( pipe, &ov ) ? 0 : GetLastError();
        if (err == ERROR_IO_PENDING)
        {
            wait = WaitForMultipleObjects( 2, handles, FALSE, INFINITE );
            if (wait != WAIT_OBJECT_0) { CancelIoEx( pipe, &ov ); CloseHandle( pipe ); break; }
            err = 0;
        }
        if (err && err != ERROR_PIPE_CONNECTED) { CloseHandle( pipe ); continue; }
        if (InterlockedIncrement( &connections ) > MAX_CONNECTIONS)
        {
            InterlockedDecrement( &connections );
            DisconnectNamedPipe( pipe );
            CloseHandle( pipe );
            continue;
        }
        {
            HANDLE thread = CreateThread( NULL, 0, connection_thread, pipe, 0, NULL );
            if (thread) CloseHandle( thread );
            else { InterlockedDecrement( &connections ); CloseHandle( pipe ); }
        }
    }
    CloseHandle( ov.hEvent );
    LocalFree( sa.lpSecurityDescriptor );
    return 0;
}

static int run_server( void )
{
    static const struct listener listeners[] = { { PIPE_LOCAL }, { PIPE_BRIDGED } };
    HANDLE threads[2];
    unsigned int i;

    for (i = 0; i < 2; i++) threads[i] = CreateThread( NULL, 0, listener_thread, (void *)&listeners[i], 0, NULL );
    WaitForSingleObject( stop_event, INFINITE );
    WaitForMultipleObjects( 2, threads, TRUE, 5000 );
    for (i = 0; i < 2; i++) CloseHandle( threads[i] );
    return 0;
}

/* ------------------------------------------------------------------------------------------ the service */

static void set_state( DWORD state, DWORD err )
{
    service_status.dwCurrentState = state;
    service_status.dwWin32ExitCode = err;
    service_status.dwControlsAccepted = state == SERVICE_RUNNING ? SERVICE_ACCEPT_STOP : 0;
    if (status_handle) SetServiceStatus( status_handle, &service_status );
}

static DWORD WINAPI handler( DWORD control, DWORD type, void *data, void *context )
{
    if (control == SERVICE_CONTROL_STOP || control == SERVICE_CONTROL_SHUTDOWN)
    {
        set_state( SERVICE_STOP_PENDING, NO_ERROR );
        SetEvent( stop_event );
        return NO_ERROR;
    }
    return control == SERVICE_CONTROL_INTERROGATE ? NO_ERROR : ERROR_CALL_NOT_IMPLEMENTED;
}

static void WINAPI service_main( DWORD argc, WCHAR **argv )
{
    status_handle = RegisterServiceCtrlHandlerExW( SERVICE_NAME, handler, NULL );
    if (!status_handle) return;
    set_state( SERVICE_RUNNING, NO_ERROR );
    run_server();
    set_state( SERVICE_STOPPED, NO_ERROR );
}

int __cdecl wmain( int argc, WCHAR **argv )
{
    static const SERVICE_TABLE_ENTRYW table[] = { { (WCHAR *)SERVICE_NAME, service_main }, { NULL, NULL } };

    InitializeCriticalSection( &change_lock );
    stop_event = CreateEventW( NULL, TRUE, FALSE, NULL );
#ifdef WEXACCOUNTS_LAB
    /* wexaccounts.exe --console [MAPFILE]: serve in the foreground until a client sends the stop request (a lab build only) */
    if (argc >= 2 && !wcscmp( argv[1], L"--console" ))
    {
        if (argc >= 3) lstrcpynW( lab_map, argv[2], MAX_PATH );
        return run_server();
    }
#endif
    if (argc == 1 && StartServiceCtrlDispatcherW( table )) return 0;
    fprintf( stderr, "wexaccounts: a service of the system, started by the service manager\n" );
    return 1;
}

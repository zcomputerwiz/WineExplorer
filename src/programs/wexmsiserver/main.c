/*
 * wexmsiserver: Windows Installer's SYSTEM custom action host, started in session 0 (docs/MSI_NOIMPERSONATE_SYSTEM.md,
 * option B with the draft 4 corrections)
 *
 * A LocalSystem service of session 0. The elevated msiexec of a per-machine install opens \\.\pipe\WexSession0\wexmsiserver
 * through the pipe bridge (the pipe is \\.\pipe\wexmsiserver here) when a deferred custom action without impersonation is
 * to run, and sends one request (wine/wex_msisys.h). The service:
 *   - checks the caller on that connection: the server's caller class must be an elevated administrator (the decision
 *     the event log and the accounts service use), the token's elevation must be full, and the request must say the
 *     install is per machine; anything else is refused with ERROR_ACCESS_DENIED, and msi runs the action as the user;
 *   - starts the host, msiexec -WexSysHost <handle> (32 or 64 bit as asked), as its own child: SYSTEM in session 0;
 *   - hands the host that connection, and only it (an inherited duplicate, the one handle the child inherits): the
 *     host's only client is the install that asked;
 *   - writes an event to the System log: the package, the product, the user.
 * The host does the rest (msi's syshost.c): it writes the binaries it runs into a folder of its own and relays the
 * actions' msi calls to the client, which passes on only what a deferred action may do.
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

#include "ntstatus.h"
#define WIN32_NO_STATUS
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <windef.h>
#include <winbase.h>
#include <winsvc.h>
#include <winternl.h>
#include <sddl.h>

#include "wine/wex_caller.h"
#include "wine/wex_eventlog.h"
#include "wine/wex_msisys.h"

#define SERVICE_NAME    L"wexmsiserver"
#define EVENT_SOURCE    WEX_MSISYS_EVENT_SOURCE
#define MAX_CONNECTIONS 64
#define READ_DEADLINE_MS     5000   /* the request must arrive this soon: a silent client cannot hold a slot */
#define MAX_PER_CALLER       4      /* connections of one account at a time */
#define REFUSALS_PER_MINUTE  10     /* refusal events of one account: the System log cannot be flooded */

static SERVICE_STATUS_HANDLE status_handle;
static SERVICE_STATUS service_status = { SERVICE_WIN32_OWN_PROCESS, SERVICE_START_PENDING, 0, NO_ERROR, 0, 0, 0 };
static HANDLE stop_event;
static CRITICAL_SECTION start_lock;  /* one inheritable handle at a time: the host inherits only its own */
static LONG connections;

static BOOL read_full( HANDLE h, void *buffer, DWORD size )
{
    BYTE *p = buffer;
    DWORD got;

    while (size)
    {
        if (!ReadFile( h, p, size, &got, NULL ) || !got) return FALSE;
        p += got;
        size -= got;
    }
    return TRUE;
}

/* ------------------------------------------------------------------------------------------ the event */

static void write_event( WORD type, DWORD id, const WCHAR **strings, int count, const BYTE *sid, DWORD sid_size )
{
    struct wex_evtlog_header hdr = { WEX_EVTLOG_MAGIC, WEX_EVTLOG_VERSION, WEX_EVTLOG_OPEN }, reply;
    struct wex_evtlog_open open = { WEX_EVTLOG_OPEN_SOURCE, sizeof(EVENT_SOURCE) };
    struct wex_evtlog_report report = { type, 0, id, count, 0, 0, sid_size };
    DWORD mode = PIPE_READMODE_MESSAGE, done, size, pos;
    BYTE *msg;
    HANDLE pipe;
    int i;

    pipe = CreateFileW( WEX_EVTLOG_PIPE, GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING,
                        SECURITY_SQOS_PRESENT | SECURITY_IDENTIFICATION, NULL );
    if (pipe == INVALID_HANDLE_VALUE) return;
    SetNamedPipeHandleState( pipe, &mode, NULL, NULL );

    size = sizeof(hdr) + sizeof(open) + sizeof(EVENT_SOURCE);
    if (!(msg = malloc( size ))) goto done;
    hdr.size = size;
    memcpy( msg, &hdr, sizeof(hdr) );
    memcpy( msg + sizeof(hdr), &open, sizeof(open) );
    memcpy( msg + sizeof(hdr) + sizeof(open), EVENT_SOURCE, sizeof(EVENT_SOURCE) );
    if (!WriteFile( pipe, msg, size, &done, NULL ) || !ReadFile( pipe, &reply, sizeof(reply), &done, NULL ) ||
            done < sizeof(reply) || reply.status)
    {
        free( msg );
        goto done;
    }
    free( msg );

    for (i = 0; i < count; i++) report.strings_size += (lstrlenW( strings[i] ) + 1) * sizeof(WCHAR);
    size = sizeof(hdr) + sizeof(report) + sid_size + report.strings_size;
    if (!(msg = calloc( 1, size ))) goto done;
    hdr.op = WEX_EVTLOG_REPORT;
    hdr.size = size;
    hdr.handle = reply.handle;
    memcpy( msg, &hdr, sizeof(hdr) );
    memcpy( msg + sizeof(hdr), &report, sizeof(report) );
    pos = sizeof(hdr) + sizeof(report);
    if (sid_size) memcpy( msg + pos, sid, sid_size );
    pos += sid_size;
    for (i = 0; i < count; i++)
    {
        DWORD len = (lstrlenW( strings[i] ) + 1) * sizeof(WCHAR);
        memcpy( msg + pos, strings[i], len );
        pos += len;
    }
    if (WriteFile( pipe, msg, size, &done, NULL )) ReadFile( pipe, &reply, sizeof(reply), &done, NULL );
    free( msg );

done:
    CloseHandle( pipe );
}

/* ------------------------------------------------------------------------------------------ the gate */

struct caller
{
    BYTE   sid[SECURITY_MAX_SID_SIZE];
    DWORD  sid_size;
    WCHAR  sid_string[192];
    WCHAR  session[16];
};

/* who asks, for the events: the account and the session, whatever the verdict */
static void identify( HANDLE pipe, struct caller *caller )
{
    BYTE user_buffer[sizeof(TOKEN_USER) + SECURITY_MAX_SID_SIZE];
    TOKEN_USER *user = (TOKEN_USER *)user_buffer;
    WCHAR *sid_string;
    HANDLE token;
    ULONG session;
    DWORD size;
    BOOL ok;

    if (GetNamedPipeClientSessionId( pipe, &session ))
        swprintf( caller->session, ARRAY_SIZE(caller->session), L"%lu", session );
    if (!ImpersonateNamedPipeClient( pipe )) return;
    ok = OpenThreadToken( GetCurrentThread(), TOKEN_QUERY, TRUE, &token );
    RevertToSelf();
    if (!ok) return;
    if (GetTokenInformation( token, TokenUser, user, sizeof(user_buffer), &size ) &&
            GetLengthSid( user->User.Sid ) <= sizeof(caller->sid))
    {
        caller->sid_size = GetLengthSid( user->User.Sid );
        memcpy( caller->sid, user->User.Sid, caller->sid_size );
        if (ConvertSidToStringSidW( user->User.Sid, &sid_string ))
        {
            lstrcpynW( caller->sid_string, sid_string, ARRAY_SIZE(caller->sid_string) );
            LocalFree( sid_string );
        }
    }
    CloseHandle( token );
}

/* the gate: the server's verdict on this connection must be an elevated administrator (Administrators enabled, High
 * integrity or more, not a filtered token: a full token or an unfiltered administrator's). Elevation and the
 * membership are the user's server's attestation (T1), as for the event log's and the accounts service's verdicts;
 * the account and the session are session 0's own record of the link. */
static DWORD check_caller( HANDLE pipe )
{
    return wex_caller_class_of_pipe( pipe ) == WEX_CALLER_ELEVATED_ADMIN ? ERROR_SUCCESS : ERROR_ACCESS_DENIED;
}

/* a crashed host leaves its folder; a live one (the service restarted under it) holds host.lock open in its own, so
 * a folder whose lock cannot be deleted is in use and stays */
static void remove_leftovers( void )
{
    WCHAR dir[MAX_PATH], path[MAX_PATH], sub[MAX_PATH];
    WIN32_FIND_DATAW data, file;
    HANDLE find, files;

    if (!GetWindowsDirectoryW( dir, ARRAY_SIZE(dir) )) return;
    swprintf( path, ARRAY_SIZE(path), L"%s\\Installer\\WexSys*", dir );
    if ((find = FindFirstFileW( path, &data )) == INVALID_HANDLE_VALUE) return;
    do
    {
        if (!(data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) continue;
        swprintf( sub, ARRAY_SIZE(sub), L"%s\\Installer\\%s\\host.lock", dir, data.cFileName );
        if (!DeleteFileW( sub ) && GetLastError() == ERROR_SHARING_VIOLATION) continue;  /* a live host's */
        swprintf( sub, ARRAY_SIZE(sub), L"%s\\Installer\\%s\\*", dir, data.cFileName );
        if ((files = FindFirstFileW( sub, &file )) != INVALID_HANDLE_VALUE)
        {
            do
            {
                if (file.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
                swprintf( sub, ARRAY_SIZE(sub), L"%s\\Installer\\%s\\%s", dir, data.cFileName, file.cFileName );
                DeleteFileW( sub );
            } while (FindNextFileW( files, &file ));
            FindClose( files );
        }
        swprintf( sub, ARRAY_SIZE(sub), L"%s\\Installer\\%s", dir, data.cFileName );
        if (RemoveDirectoryW( sub )) fprintf( stderr, "wexmsiserver: removed a crashed host's %ls\n", sub );
    } while (FindNextFileW( find, &data ));
    FindClose( find );
}

/* ------------------------------------------------------------------------------------------ the host */

static DWORD start_host( HANDLE pipe, const struct wex_msisys_request *request, const struct caller *caller,
                         DWORD *pid )
{
    UINT32 arch = request->arch;
    STARTUPINFOEXW si = { { sizeof(si) } };
    PROCESS_INFORMATION pi;
    WCHAR dir[MAX_PATH], path[MAX_PATH + 16], cmd[MAX_PATH + 64];
    HANDLE inherit = NULL;
    SIZE_T size = 0;
    DWORD err = ERROR_SUCCESS;
    BOOL ok;

    if (arch == 64 || !GetSystemWow64DirectoryW( dir, ARRAY_SIZE(dir) ))
    {
        if (!GetSystemDirectoryW( dir, ARRAY_SIZE(dir) )) return GetLastError();
    }
    swprintf( path, ARRAY_SIZE(path), L"%s\\msiexec.exe", dir );

    EnterCriticalSection( &start_lock );
    if (!DuplicateHandle( GetCurrentProcess(), pipe, GetCurrentProcess(), &inherit, 0, TRUE, DUPLICATE_SAME_ACCESS ))
    {
        err = GetLastError();
        goto done;
    }
    InitializeProcThreadAttributeList( NULL, 1, 0, &size );
    if (!(si.lpAttributeList = malloc( size )) || !InitializeProcThreadAttributeList( si.lpAttributeList, 1, 0, &size ) ||
            !UpdateProcThreadAttribute( si.lpAttributeList, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST, &inherit,
                                        sizeof(inherit), NULL, NULL ))
    {
        err = GetLastError() ? GetLastError() : ERROR_OUTOFMEMORY;
        goto done;
    }
    swprintf( cmd, ARRAY_SIZE(cmd), L"\"%s\" -WexSysHost %Ix", path, (ULONG_PTR)inherit );
    /* what the host's events say about the install: ours to set, under start_lock, inherited by the host only */
    SetEnvironmentVariableW( WEX_MSISYS_ENV_USER, caller->sid_string );
    SetEnvironmentVariableW( WEX_MSISYS_ENV_SESSION, caller->session );
    SetEnvironmentVariableW( WEX_MSISYS_ENV_PRODUCT, request->product );
    SetEnvironmentVariableW( WEX_MSISYS_ENV_PACKAGE, request->package );
    ok = CreateProcessW( path, cmd, NULL, NULL, TRUE, CREATE_NO_WINDOW | EXTENDED_STARTUPINFO_PRESENT, NULL, dir,
                         &si.StartupInfo, &pi );
    if (!ok) err = GetLastError();
    SetEnvironmentVariableW( WEX_MSISYS_ENV_USER, NULL );
    SetEnvironmentVariableW( WEX_MSISYS_ENV_SESSION, NULL );
    SetEnvironmentVariableW( WEX_MSISYS_ENV_PRODUCT, NULL );
    SetEnvironmentVariableW( WEX_MSISYS_ENV_PACKAGE, NULL );
    if (ok)
    {
        *pid = pi.dwProcessId;
        CloseHandle( pi.hThread );
        CloseHandle( pi.hProcess );
    }

done:
    if (si.lpAttributeList)
    {
        DeleteProcThreadAttributeList( si.lpAttributeList );
        free( si.lpAttributeList );
    }
    if (inherit) CloseHandle( inherit );
    LeaveCriticalSection( &start_lock );
    return err;
}

/* ------------------------------------------------------------------------------------------ limits per caller */

struct caller_slot
{
    WCHAR sid[192];
    LONG  active;           /* connections now */
    DWORD window;           /* GetTickCount of the current minute's start */
    DWORD refusals;         /* refusal events in it */
};

static struct caller_slot slots[MAX_CONNECTIONS];
static CRITICAL_SECTION slots_lock;

/* a connection of this account, or NULL when it has MAX_PER_CALLER already (or the table is full) */
static struct caller_slot *slot_acquire( const WCHAR *sid )
{
    struct caller_slot *slot = NULL, *free_slot = NULL;
    unsigned int i;

    EnterCriticalSection( &slots_lock );
    for (i = 0; i < ARRAY_SIZE(slots); i++)
    {
        if (slots[i].sid[0] && !wcscmp( slots[i].sid, sid )) { slot = &slots[i]; break; }
        /* free: never used (whatever the uptime: the service starts at boot), or idle for a minute */
        if (!free_slot && !slots[i].active && (!slots[i].sid[0] || GetTickCount() - slots[i].window > 60000))
            free_slot = &slots[i];
    }
    if (!slot && (slot = free_slot))
    {
        lstrcpynW( slot->sid, sid, ARRAY_SIZE(slot->sid) );
        slot->window = GetTickCount();
        slot->refusals = 0;
    }
    if (slot && slot->active >= MAX_PER_CALLER) slot = NULL;
    if (slot) slot->active++;
    LeaveCriticalSection( &slots_lock );
    return slot;
}

static void slot_release( struct caller_slot *slot )
{
    EnterCriticalSection( &slots_lock );
    slot->active--;
    LeaveCriticalSection( &slots_lock );
}

/* whether this account's refusal may be logged now */
static BOOL slot_may_log_refusal( struct caller_slot *slot )
{
    BOOL ret;

    EnterCriticalSection( &slots_lock );
    if (GetTickCount() - slot->window > 60000)
    {
        slot->window = GetTickCount();
        slot->refusals = 0;
    }
    ret = slot->refusals++ < REFUSALS_PER_MINUTE;
    LeaveCriticalSection( &slots_lock );
    return ret;
}

/* the request's deadline: a watchdog cancels the connection thread's read when it has not arrived in time */
struct deadline
{
    HANDLE thread;          /* the connection thread */
    HANDLE done;
};

static DWORD WINAPI deadline_thread( void *param )
{
    struct deadline *d = param;

    if (WaitForSingleObject( d->done, READ_DEADLINE_MS ) == WAIT_TIMEOUT) CancelSynchronousIo( d->thread );
    return 0;
}

static BOOL read_request( HANDLE pipe, struct wex_msisys_request *request )
{
    struct deadline d = { NULL, CreateEventW( NULL, TRUE, FALSE, NULL ) };
    HANDLE watchdog = NULL;
    BOOL ok;

    if (d.done && DuplicateHandle( GetCurrentProcess(), GetCurrentThread(), GetCurrentProcess(), &d.thread, 0, FALSE,
                                   DUPLICATE_SAME_ACCESS ))
        watchdog = CreateThread( NULL, 0, deadline_thread, &d, 0, NULL );
    if (!watchdog)
    {
        if (d.thread) CloseHandle( d.thread );
        if (d.done) CloseHandle( d.done );
        return FALSE;  /* no read without a deadline */
    }
    ok = read_full( pipe, request, sizeof(*request) );
    SetEvent( d.done );
    WaitForSingleObject( watchdog, INFINITE );
    CloseHandle( watchdog );
    CloseHandle( d.thread );
    CloseHandle( d.done );
    return ok;
}

/* ------------------------------------------------------------------------------------------ a connection */

static DWORD WINAPI connection_thread( void *param )
{
    HANDLE pipe = param;
    struct wex_msisys_request request;
    struct caller caller = { { 0 } };
    WCHAR pid_string[16];
    const WCHAR *strings[5];
    const WCHAR *reason = NULL;
    struct caller_slot *slot;
    UINT32 status;
    DWORD done, pid = 0;

    /* who asks, before anything is read: an account has a few connections at a time, and its request a deadline */
    identify( pipe, &caller );
    if (!(slot = slot_acquire( caller.sid_string[0] ? caller.sid_string : L"?" ))) goto end_noslot;
    if (!read_request( pipe, &request ) || request.magic != WEX_MSISYS_MAGIC ||
            request.version != WEX_MSISYS_VERSION || (request.arch != 32 && request.arch != 64))
        goto end;
    request.product[ARRAY_SIZE(request.product) - 1] = 0;
    request.package[ARRAY_SIZE(request.package) - 1] = 0;

    /* the gate: an elevated administrator's per-machine install; every refusal is logged */
    if ((status = check_caller( pipe ))) reason = L"not an elevated administrator";
    else if (!(request.flags & WEX_MSISYS_PER_MACHINE))
    {
        status = ERROR_ACCESS_DENIED;
        reason = L"a per-user install";
    }
    strings[0] = request.package;
    strings[1] = request.product;
    strings[2] = caller.sid_string;
    strings[3] = caller.session;
    if (status)
    {
        strings[4] = reason;
        if (slot_may_log_refusal( slot ))
            write_event( EVENTLOG_WARNING_TYPE, WEX_MSISYS_EVENT_REFUSED, strings, 5,
                         caller.sid_size ? caller.sid : NULL, caller.sid_size );
        WriteFile( pipe, &status, sizeof(status), &done, NULL );
        goto end;
    }

    /* the answer before the host starts: from then on the connection is the host's */
    if (!WriteFile( pipe, &status, sizeof(status), &done, NULL )) goto end;
    if ((status = start_host( pipe, &request, &caller, &pid )))
    {
        fprintf( stderr, "wexmsiserver: cannot start the host: %u\n", status );
        goto end;
    }

    swprintf( pid_string, ARRAY_SIZE(pid_string), L"%lu", pid );
    strings[4] = pid_string;
    write_event( EVENTLOG_INFORMATION_TYPE, WEX_MSISYS_EVENT_HOST, strings, 5, caller.sid, caller.sid_size );
    slot_release( slot );
    CloseHandle( pipe );  /* not disconnected: the host has the connection */
    InterlockedDecrement( &connections );
    return 0;

end:
    slot_release( slot );
end_noslot:
    DisconnectNamedPipe( pipe );
    CloseHandle( pipe );
    InterlockedDecrement( &connections );
    return 0;
}

/* ------------------------------------------------------------------------------------------ the listener */

static DWORD WINAPI listener_thread( void *param )
{
    SECURITY_ATTRIBUTES sa = { sizeof(sa), NULL, FALSE };
    /* any authenticated caller may connect and ask: who gets a host is the gate's, and every refusal is logged (a
     * refusal by this descriptor at the open would leave no trace) */
    static const WCHAR sddl[] = L"D:P(A;;GA;;;SY)(A;;GA;;;BA)(A;;GRGW;;;AU)";
    /* synchronous instances: the host reads and writes its connection synchronously; a stop cancels the wait for a
     * client (CancelSynchronousIo from the control handler) */
    DWORD flags = PIPE_ACCESS_DUPLEX | FILE_FLAG_FIRST_PIPE_INSTANCE;

    ConvertStringSecurityDescriptorToSecurityDescriptorW( sddl, SDDL_REVISION_1, &sa.lpSecurityDescriptor, NULL );
    while (WaitForSingleObject( stop_event, 0 ) != WAIT_OBJECT_0)
    {
        HANDLE pipe = CreateNamedPipeW( WEX_MSISYS_PIPE_LOCAL, flags, PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT |
                                        PIPE_REJECT_REMOTE_CLIENTS, PIPE_UNLIMITED_INSTANCES, 65536, 65536, 0, &sa );
        HANDLE thread;

        if (pipe == INVALID_HANDLE_VALUE)
        {
            /* the first instance of the name is made once; a name that is taken (a squatter) is not served */
            Sleep( flags & FILE_FLAG_FIRST_PIPE_INSTANCE ? 1000 : 250 );
            continue;
        }
        flags &= ~FILE_FLAG_FIRST_PIPE_INSTANCE;
        if (!ConnectNamedPipe( pipe, NULL ) && GetLastError() != ERROR_PIPE_CONNECTED)
        {
            CloseHandle( pipe );
            continue;
        }
        if (InterlockedIncrement( &connections ) > MAX_CONNECTIONS)
        {
            InterlockedDecrement( &connections );
            DisconnectNamedPipe( pipe );
            CloseHandle( pipe );
            continue;
        }
        if ((thread = CreateThread( NULL, 0, connection_thread, pipe, 0, NULL ))) CloseHandle( thread );
        else
        {
            InterlockedDecrement( &connections );
            DisconnectNamedPipe( pipe );
            CloseHandle( pipe );
        }
    }
    LocalFree( sa.lpSecurityDescriptor );
    return 0;
}

static HANDLE listener;

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
        if (listener) CancelSynchronousIo( listener );
        return NO_ERROR;
    }
    return control == SERVICE_CONTROL_INTERROGATE ? NO_ERROR : ERROR_CALL_NOT_IMPLEMENTED;
}

static void WINAPI service_main( DWORD argc, WCHAR **argv )
{
    status_handle = RegisterServiceCtrlHandlerExW( SERVICE_NAME, handler, NULL );
    if (!status_handle) return;
    remove_leftovers();
    if ((listener = CreateThread( NULL, 0, listener_thread, NULL, 0, NULL )))
    {
        set_state( SERVICE_RUNNING, NO_ERROR );
        WaitForSingleObject( stop_event, INFINITE );
        WaitForSingleObject( listener, 5000 );
    }
    set_state( SERVICE_STOPPED, NO_ERROR );
}

int __cdecl wmain( int argc, WCHAR **argv )
{
    static const SERVICE_TABLE_ENTRYW table[] = { { (WCHAR *)SERVICE_NAME, service_main }, { NULL, NULL } };

    InitializeCriticalSection( &start_lock );
    InitializeCriticalSection( &slots_lock );
    stop_event = CreateEventW( NULL, TRUE, FALSE, NULL );
    if (argc == 1 && StartServiceCtrlDispatcherW( table )) return 0;
    fprintf( stderr, "wexmsiserver: a service of the system, started by the service manager\n" );
    return 1;
}

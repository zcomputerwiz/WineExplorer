/*
 * Probe of accounts_forward, the accounts service's way to the root helper (WineExplorer H1)
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
 * along with this program; if not, see <https://www.gnu.org/licenses/>.
 */

/* Stands in for the accounts service (src/programs/wexaccounts) and its clients, so that the server's part can be
 * tested on its own. Built with MinGW, 64 and 32 bit. Wine's own headers do not build with MinGW's, so the service's
 * two requests are made raw, as include/wine/wex_accounts.h makes them: the suite reads their numbers from the Wine
 * tree it built (ACC_FORWARD_REQ, ACC_RESULT_REQ) and checks their layout in include/wine/server_protocol.h first;
 * wine_server_call is taken from ntdll at run time. Each mode prints key=value lines.
 *
 *   squat [imp]        create \\.\pipe\accounts and \\.\pipe\WexSession0\accounts (imp: while impersonating itself);
 *                      squat=, squat_bridged= are Win32 error codes (0: created)
 *   squatfile FILE     squat, the result written to FILE (run as a service), after service_sids=: the token's service
 *                      SIDs (S-1-5-80-...) as SID:attributes, or none
 *   svcserve FILE SECONDS  serve as the service wexaccounts (StartServiceCtrlDispatcher), its output to FILE, after
 *                      service_sids=
 *   sidtype NAME [TYPE]  set a service's SID type, then print sidtype=
 *   svcpath NAME [PATH]  print a service's binary path (svcpath=), then set it to PATH (Wine's sc has no config)
 *   serve SECONDS      the service: serve \\.\pipe\accounts until SECONDS pass or a client sends op 0xffff. Each
 *                      request (u32 op, body) is forwarded as the client's message from offset 4: first with a length
 *                      one byte past its end (bounds), then for real, then again (replay); the client gets back (u32
 *                      NTSTATUS, u32 helper status, u32 bounds NTSTATUS, u32 replay NTSTATUS, reply body). One
 *                      "served=" line per request.
 *   regtime SECONDS    machine registry reads every 100 ms; the slowest, in ms
 *   client OP BODY     a session's client: through \\.\pipe\WexSession0\accounts
 *   local OP BODY      a client in session 0 itself, not bridged: \\.\pipe\accounts
 *   quit               end serve (bridged)
 *   localquit          end serve from session 0 */

#include <windows.h>
#include <winternl.h>
#include <sddl.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if !defined(ACC_FORWARD_REQ) || !defined(ACC_RESULT_REQ)
#error the suite passes the request numbers (ACC_FORWARD_REQ, ACC_RESULT_REQ)
#endif

#define PIPE_PLAIN   L"\\\\.\\pipe\\accounts"
#define PIPE_BRIDGED L"\\\\.\\pipe\\WexSession0\\accounts"
#define OP_QUIT      0xffff
#define MAX_FRAME    65536      /* WEX_ACCOUNTS_MAX_FRAME */
#define CALL_TIMEOUT 15         /* WEX_ACCOUNTS_TIMEOUT */
#ifndef STATUS_BUFFER_OVERFLOW
#define STATUS_BUFFER_OVERFLOW ((NTSTATUS)0x80000005)
#endif

/* wine_server_call's carrier (wine/server.h, __server_request_info), as scripts/e1s10_issue_probe.c has it */
struct raw_call
{
    union { BYTE bytes[88]; ULONGLONG align; } u;
    unsigned int count;
    void *reply;
    struct { const void *ptr; unsigned int size; } data[5];
};
typedef char raw_count_offset[(offsetof(struct raw_call, count) == 88) ? 1 : -1];
typedef char raw_reply_offset[(offsetof(struct raw_call, reply) == (sizeof(void *) == 8 ? 96 : 92)) ? 1 : -1];

static unsigned int (CDECL *p_wine_server_call)( void *req_ptr );

static void put32( BYTE *p, DWORD v ) { memcpy( p, &v, 4 ); }
static DWORD get32( const BYTE *p ) { DWORD v; memcpy( &v, p, 4 ); return v; }

/* wex_accounts_forward of include/wine/wex_accounts.h, with raw requests: accounts_forward {header 12, pipe 12, op 16,
 * offset 20, length 24; reply: call 8}, a wait, accounts_result {call 12; reply: helper_status 8, total 12, body}.
 * The body is len bytes at offset of the client message the service last read whole. */
static NTSTATUS forward( HANDLE pipe, ULONG offset, ULONG len, ULONG op, void *out, ULONG *out_len, USHORT *status )
{
    struct raw_call r;
    LARGE_INTEGER timeout;
    HANDLE call;
    NTSTATUS ret;

    memset( &r, 0, sizeof(r) );
    put32( r.u.bytes, ACC_FORWARD_REQ );
    put32( r.u.bytes + 12, (DWORD)(ULONG_PTR)pipe );
    put32( r.u.bytes + 16, op );
    put32( r.u.bytes + 20, offset );
    put32( r.u.bytes + 24, len );
    if ((ret = p_wine_server_call( &r ))) return ret;
    call = (HANDLE)(ULONG_PTR)get32( r.u.bytes + 8 );
    timeout.QuadPart = -(LONGLONG)(CALL_TIMEOUT + 5) * 10000000;
    NtWaitForSingleObject( call, FALSE, &timeout );
    memset( &r, 0, sizeof(r) );
    put32( r.u.bytes, ACC_RESULT_REQ );
    put32( r.u.bytes + 8, *out_len );
    put32( r.u.bytes + 12, (DWORD)(ULONG_PTR)call );
    r.reply = out;
    ret = p_wine_server_call( &r );
    *status = (USHORT)get32( r.u.bytes + 8 );
    *out_len = ret == STATUS_BUFFER_OVERFLOW ? get32( r.u.bytes + 12 ) : get32( r.u.bytes + 4 );
    NtClose( call );
    return ret;
}

static DWORD squat( const WCHAR *name )
{
    HANDLE pipe = CreateNamedPipeW( name, PIPE_ACCESS_DUPLEX, PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE | PIPE_WAIT,
                                    1, 4096, 4096, 0, NULL );

    if (pipe == INVALID_HANDLE_VALUE) return GetLastError();
    CloseHandle( pipe );
    return 0;
}

static int do_squat( int imp )
{
    if (imp && !ImpersonateSelf( SecurityImpersonation ))
    {
        printf( "impersonate=%lu\n", GetLastError() );
        return 1;
    }
    printf( "squat=%lu\n", squat( PIPE_PLAIN ) );
    printf( "squat_bridged=%lu\n", squat( PIPE_BRIDGED ) );
    if (imp) RevertToSelf();
    return 0;
}

static int do_serve( DWORD seconds )
{
    static char in[MAX_FRAME + 4], out[2 * MAX_FRAME + 16];
    DWORD end = GetTickCount() + seconds * 1000, got, len, op;
    SECURITY_ATTRIBUTES sa = { sizeof(sa), NULL, FALSE };
    OVERLAPPED ov;
    HANDLE pipe, event;
    NTSTATUS status, bounds, replay;
    ULONG reply_len, replay_len;
    USHORT helper, replay_helper;

    if (!(p_wine_server_call = (void *)GetProcAddress( GetModuleHandleW( L"ntdll.dll" ), "wine_server_call" )))
    {
        printf( "serve=no-wine_server_call\n" );
        return 1;
    }
    /* everyone may connect: the service checks its callers through the server's verdict */
    if (!ConvertStringSecurityDescriptorToSecurityDescriptorW( L"D:(A;;GRGW;;;WD)(A;;GA;;;SY)", SDDL_REVISION_1,
                                                               &sa.lpSecurityDescriptor, NULL ))
    {
        printf( "serve=sd %lu\n", GetLastError() );
        return 1;
    }
    pipe = CreateNamedPipeW( PIPE_PLAIN, PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED | FILE_FLAG_FIRST_PIPE_INSTANCE,
                             PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE | PIPE_WAIT, 1, sizeof(out), sizeof(in), 0, &sa );
    if (pipe == INVALID_HANDLE_VALUE)
    {
        printf( "serve=%lu\n", GetLastError() );
        return 1;
    }
    event = CreateEventW( NULL, TRUE, FALSE, NULL );
    printf( "serve=listening\n" );
    fflush( stdout );
    for (;;)
    {
        memset( &ov, 0, sizeof(ov) );
        ov.hEvent = event;
        if (!ConnectNamedPipe( pipe, &ov ))
        {
            DWORD err = GetLastError(), now = GetTickCount();

            if (err == ERROR_IO_PENDING)
            {
                if (WaitForSingleObject( event, (int)(end - now) > 0 ? end - now : 0 ))
                {
                    CancelIo( pipe );
                    printf( "serve=timeout\n" );
                    break;
                }
            }
            else if (err != ERROR_PIPE_CONNECTED)
            {
                printf( "serve=connect %lu\n", err );
                break;
            }
        }
        memset( &ov, 0, sizeof(ov) );
        ov.hEvent = event;
        if (!ReadFile( pipe, in, sizeof(in), &got, &ov ) &&
            (GetLastError() != ERROR_IO_PENDING || !GetOverlappedResult( pipe, &ov, &got, TRUE )))
            got = 0;
        if (got < 4)
        {
            printf( "served=short %lu\n", got );
            DisconnectNamedPipe( pipe );
            continue;
        }
        memcpy( &op, in, 4 );
        if (op == OP_QUIT)
        {
            printf( "serve=quit\n" );
            DisconnectNamedPipe( pipe );
            break;
        }
        /* A3: a body outside the message is refused (and the message stays forwardable); the message is forwarded
         * once; a replay of it is refused */
        reply_len = sizeof(out) - 16;
        bounds = forward( pipe, 4, got - 3, op, out + 16, &reply_len, &helper );
        reply_len = sizeof(out) - 16;
        helper = 0;
        status = forward( pipe, 4, got - 4, op, out + 16, &reply_len, &helper );
        replay_len = MAX_FRAME;
        replay = forward( pipe, 4, got - 4, op, out + 16 + (status ? 0 : reply_len), &replay_len, &replay_helper );
        printf( "served=0x%08lx op=%lu helper=%u reply=%lu bounds=0x%08lx replay=0x%08lx\n", (unsigned long)status, op,
                helper, status ? 0ul : reply_len, (unsigned long)bounds, (unsigned long)replay );
        fflush( stdout );
        if (status) reply_len = 0;
        len = status;
        memcpy( out, &len, 4 );
        len = helper;
        memcpy( out + 4, &len, 4 );
        len = bounds;
        memcpy( out + 8, &len, 4 );
        len = replay;
        memcpy( out + 12, &len, 4 );
        memset( &ov, 0, sizeof(ov) );
        ov.hEvent = event;
        if (!WriteFile( pipe, out, 16 + reply_len, &got, &ov ) && GetLastError() == ERROR_IO_PENDING)
            GetOverlappedResult( pipe, &ov, &got, TRUE );
        FlushFileBuffers( pipe );
        DisconnectNamedPipe( pipe );
    }
    CloseHandle( pipe );
    return 0;
}

static int do_client( const WCHAR *name, DWORD op, const char *body )
{
    static char buf[MAX_FRAME + 16];
    DWORD mode = PIPE_READMODE_MESSAGE, len = strlen( body ), got, ntstatus, helper, bounds, replay, i;
    HANDLE pipe = INVALID_HANDLE_VALUE;

    for (i = 0; i < 100; i++)
    {
        pipe = CreateFileW( name, GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING,
                            SECURITY_SQOS_PRESENT | SECURITY_IDENTIFICATION, NULL );
        if (pipe != INVALID_HANDLE_VALUE) break;
        if (GetLastError() != ERROR_PIPE_BUSY && GetLastError() != ERROR_FILE_NOT_FOUND) break;
        if (!WaitNamedPipeW( name, 200 )) Sleep( 200 );
    }
    if (pipe == INVALID_HANDLE_VALUE)
    {
        printf( "connect=%lu\n", GetLastError() );
        return 1;
    }
    SetNamedPipeHandleState( pipe, &mode, NULL, NULL );
    memcpy( buf, &op, 4 );
    memcpy( buf + 4, body, len );
    if (!WriteFile( pipe, buf, 4 + len, &got, NULL ))
    {
        printf( "write=%lu\n", GetLastError() );
        return 1;
    }
    if (op == OP_QUIT)
    {
        printf( "quit=0\n" );
        return 0;
    }
    if (!ReadFile( pipe, buf, sizeof(buf) - 1, &got, NULL ) || got < 16)
    {
        printf( "read=%lu\n", GetLastError() );
        return 1;
    }
    buf[got] = 0;
    memcpy( &ntstatus, buf, 4 );
    memcpy( &helper, buf + 4, 4 );
    memcpy( &bounds, buf + 8, 4 );
    memcpy( &replay, buf + 12, 4 );
    printf( "status=0x%08lx helper=%lu bounds=0x%08lx replay=0x%08lx len=%lu reply=%s\n", ntstatus, helper, bounds,
            replay, got - 16, buf + 16 );
    CloseHandle( pipe );
    return 0;
}

/* registry reads of the machine's keys (served by session 0) every 100 ms for SECONDS: the slowest one, in ms */
static int do_regtime( DWORD seconds )
{
    DWORD end = GetTickCount() + seconds * 1000, worst = 0, n = 0, t, subkeys;
    HKEY key;
    LONG err;

    while ((int)(end - GetTickCount()) > 0)
    {
        t = GetTickCount();
        if (!(err = RegOpenKeyExW( HKEY_LOCAL_MACHINE, L"Software\\Microsoft\\Windows NT\\CurrentVersion", 0,
                                   KEY_READ, &key )))
        {
            err = RegQueryInfoKeyW( key, NULL, NULL, NULL, &subkeys, NULL, NULL, NULL, NULL, NULL, NULL, NULL );
            RegCloseKey( key );
        }
        if (err)
        {
            printf( "regtime=%ld\n", err );
            return 1;
        }
        t = GetTickCount() - t;
        if (t > worst) worst = t;
        n++;
        Sleep( 100 );
    }
    printf( "regtime=0 reads=%lu worst=%lu\n", n, worst );
    return 0;
}

/* the service SIDs (S-1-5-80-...) among the groups of the process's token, as "SID:attributes", ';' between them */
static void print_service_sids( FILE *f )
{
    static const SID_IDENTIFIER_AUTHORITY nt = { SECURITY_NT_AUTHORITY };
    static BYTE buf[8192];
    TOKEN_GROUPS *groups = (TOKEN_GROUPS *)buf;
    DWORD len, i, n = 0;
    HANDLE token;
    char *str;

    if (!OpenProcessToken( GetCurrentProcess(), TOKEN_QUERY, &token ) ||
        !GetTokenInformation( token, TokenGroups, buf, sizeof(buf), &len ))
    {
        fprintf( f, "service_sids=error %lu\n", GetLastError() );
        return;
    }
    CloseHandle( token );
    fprintf( f, "service_sids=" );
    for (i = 0; i < groups->GroupCount; i++)
    {
        PSID sid = groups->Groups[i].Sid;

        if (*GetSidSubAuthorityCount( sid ) < 1 || memcmp( GetSidIdentifierAuthority( sid ), &nt, sizeof(nt) ) ||
            *GetSidSubAuthority( sid, 0 ) != SECURITY_SERVICE_ID_BASE_RID || !ConvertSidToStringSidA( sid, &str ))
            continue;
        fprintf( f, "%s%s:0x%lx", n++ ? ";" : "", str, groups->Groups[i].Attributes );
        LocalFree( str );
    }
    fprintf( f, "%s\n", n ? "" : "none" );
}

/* squat, its result written to FILE (for a run as a service, whose output goes nowhere) */
static int do_squatfile( const char *file )
{
    FILE *f = fopen( file, "w" );

    if (!f) return 1;
    print_service_sids( f );
    fprintf( f, "squat=%lu\n", squat( PIPE_PLAIN ) );
    fprintf( f, "squat_bridged=%lu\n", squat( PIPE_BRIDGED ) );
    fclose( f );
    return 0;
}

/* svcserve: serve as a real service of the service manager (the reserved pipe is the accounts service's alone, B114) */
static SERVICE_STATUS_HANDLE svc_handle;
static SERVICE_STATUS svc_status;
static DWORD svc_seconds;

static DWORD WINAPI svc_control( DWORD code, DWORD type, void *data, void *context )
{
    if (code == SERVICE_CONTROL_STOP || code == SERVICE_CONTROL_SHUTDOWN)
    {
        svc_status.dwCurrentState = SERVICE_STOP_PENDING;
        SetServiceStatus( svc_handle, &svc_status );
        ExitProcess( 0 );
    }
    return code == SERVICE_CONTROL_INTERROGATE ? NO_ERROR : ERROR_CALL_NOT_IMPLEMENTED;
}

static void WINAPI svc_main( DWORD argc, WCHAR **argv )
{
    svc_handle = RegisterServiceCtrlHandlerExW( L"wexaccounts", svc_control, NULL );
    svc_status.dwServiceType = SERVICE_WIN32_OWN_PROCESS;
    svc_status.dwCurrentState = SERVICE_RUNNING;
    svc_status.dwControlsAccepted = SERVICE_ACCEPT_STOP | SERVICE_ACCEPT_SHUTDOWN;
    SetServiceStatus( svc_handle, &svc_status );
    print_service_sids( stdout );
    do_serve( svc_seconds );
    svc_status.dwCurrentState = SERVICE_STOPPED;
    SetServiceStatus( svc_handle, &svc_status );
}

static int do_svcserve( const char *file, DWORD seconds )
{
    SERVICE_TABLE_ENTRYW table[] = { { (WCHAR *)L"wexaccounts", svc_main }, { NULL, NULL } };

    if (!freopen( file, "w", stdout )) return 1;
    setvbuf( stdout, NULL, _IONBF, 0 );
    svc_seconds = seconds;
    if (!StartServiceCtrlDispatcherW( table )) printf( "dispatcher=%lu\n", GetLastError() );
    return 0;
}

/* sidtype NAME [TYPE]: set a service's SID type (SERVICE_CONFIG_SERVICE_SID_INFO), then print it */
static int do_sidtype( const char *name, const char *type )
{
    SC_HANDLE scm, svc;
    SERVICE_SID_INFO info;
    DWORD needed;

    if (!(scm = OpenSCManagerA( NULL, NULL, SC_MANAGER_CONNECT )) ||
        !(svc = OpenServiceA( scm, name, SERVICE_CHANGE_CONFIG | SERVICE_QUERY_CONFIG )))
    {
        printf( "sidtype=open %lu\n", GetLastError() );
        return 1;
    }
    if (type)
    {
        info.dwServiceSidType = strtoul( type, NULL, 0 );
        if (!ChangeServiceConfig2A( svc, SERVICE_CONFIG_SERVICE_SID_INFO, &info ))
        {
            printf( "sidtype=change %lu\n", GetLastError() );
            return 1;
        }
    }
    info.dwServiceSidType = 0xdeadbeef;
    if (!QueryServiceConfig2A( svc, SERVICE_CONFIG_SERVICE_SID_INFO, (BYTE *)&info, sizeof(info), &needed ))
        printf( "sidtype=query %lu\n", GetLastError() );
    else
        printf( "sidtype=%lu\n", info.dwServiceSidType );
    CloseServiceHandle( svc );
    CloseServiceHandle( scm );
    return 0;
}

/* svcpath NAME [PATH]: print a service's binary path (svcpath=), then set it to PATH */
static int do_svcpath( const char *name, const char *path )
{
    static BYTE buf[8192];
    QUERY_SERVICE_CONFIGA *config = (QUERY_SERVICE_CONFIGA *)buf;
    SC_HANDLE scm, svc;
    DWORD needed;

    if (!(scm = OpenSCManagerA( NULL, NULL, SC_MANAGER_CONNECT )) ||
        !(svc = OpenServiceA( scm, name, SERVICE_CHANGE_CONFIG | SERVICE_QUERY_CONFIG )))
    {
        printf( "svcpath_error=open %lu\n", GetLastError() );
        return 1;
    }
    if (!QueryServiceConfigA( svc, config, sizeof(buf), &needed ))
    {
        printf( "svcpath_error=query %lu\n", GetLastError() );
        return 1;
    }
    printf( "svcpath=%s\n", config->lpBinaryPathName );
    if (path && !ChangeServiceConfigA( svc, SERVICE_NO_CHANGE, SERVICE_NO_CHANGE, SERVICE_NO_CHANGE, path, NULL, NULL,
                                       NULL, NULL, NULL, NULL ))
    {
        printf( "svcpath_error=change %lu\n", GetLastError() );
        return 1;
    }
    CloseServiceHandle( svc );
    CloseServiceHandle( scm );
    return 0;
}

int main( int argc, char **argv )
{
    setvbuf( stdout, NULL, _IONBF, 0 );
    if (argc >= 4 && !strcmp( argv[1], "svcserve" )) return do_svcserve( argv[2], strtoul( argv[3], NULL, 0 ) );
    if (argc >= 3 && !strcmp( argv[1], "sidtype" )) return do_sidtype( argv[2], argc >= 4 ? argv[3] : NULL );
    if (argc >= 3 && !strcmp( argv[1], "svcpath" )) return do_svcpath( argv[2], argc >= 4 ? argv[3] : NULL );
    if (argc >= 2 && !strcmp( argv[1], "squat" )) return do_squat( argc >= 3 && !strcmp( argv[2], "imp" ) );
    if (argc >= 3 && !strcmp( argv[1], "serve" )) return do_serve( strtoul( argv[2], NULL, 0 ) );
    if (argc >= 4 && !strcmp( argv[1], "client" )) return do_client( PIPE_BRIDGED, strtoul( argv[2], NULL, 0 ), argv[3] );
    if (argc >= 4 && !strcmp( argv[1], "local" )) return do_client( PIPE_PLAIN, strtoul( argv[2], NULL, 0 ), argv[3] );
    if (argc >= 2 && !strcmp( argv[1], "quit" )) return do_client( PIPE_BRIDGED, OP_QUIT, "" );
    if (argc >= 2 && !strcmp( argv[1], "localquit" )) return do_client( PIPE_PLAIN, OP_QUIT, "" );
    if (argc >= 3 && !strcmp( argv[1], "regtime" )) return do_regtime( strtoul( argv[2], NULL, 0 ) );
    if (argc >= 3 && !strcmp( argv[1], "squatfile" )) return do_squatfile( argv[2] );
    fprintf( stderr, "usage: accounts-probe squat [imp] | squatfile FILE | serve SECONDS | svcserve FILE SECONDS | "
             "sidtype NAME [TYPE] | svcpath NAME [PATH] | client OP BODY | local OP BODY | quit | localquit | regtime SECONDS\n" );
    return 2;
}

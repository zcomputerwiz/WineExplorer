/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright 2026 WineExplorer contributors
 *
 * What does a server see when it impersonates the client of a named pipe?  One process, two threads: the
 * server creates a pipe, the client opens it with SECURITY_IMPERSONATION and writes a byte, the server calls
 * ImpersonateNamedPipeClient and prints the groups of the thread token and whether it is an Administrator.
 * The same is printed for the process token.  Both should agree when client and server are the same process.
 *
 *   x86_64-w64-mingw32-gcc -municode -o pipe-impersonation-probe.exe pipe-impersonation-probe.c -ladvapi32
 */
#include <windows.h>
#include <sddl.h>
#include <stdio.h>

#define PIPE L"\\\\.\\pipe\\wex-impersonation-probe"

static void print_groups( const char *label, HANDLE token )
{
    static BYTE buffer[8192];
    SECURITY_IMPERSONATION_LEVEL level;
    TOKEN_TYPE type;
    DWORD size, i;
    BYTE admins[SECURITY_MAX_SID_SIZE];
    BOOL member = FALSE;

    printf( "== %s\n", label );
    if (GetTokenInformation( token, TokenType, &type, sizeof(type), &size ))
        printf( "type %u (1 primary, 2 impersonation)\n", type );
    if (type == TokenImpersonation && GetTokenInformation( token, TokenImpersonationLevel, &level, sizeof(level), &size ))
        printf( "impersonation level %u\n", level );
    if (GetTokenInformation( token, TokenUser, buffer, sizeof(buffer), &size ))
    {
        WCHAR *text = NULL;
        ConvertSidToStringSidW( ((TOKEN_USER *)buffer)->User.Sid, &text );
        printf( "user %ls\n", text );
        LocalFree( text );
    }
    if (GetTokenInformation( token, TokenGroups, buffer, sizeof(buffer), &size ))
    {
        TOKEN_GROUPS *groups = (TOKEN_GROUPS *)buffer;
        for (i = 0; i < groups->GroupCount; i++)
        {
            WCHAR *text = NULL;
            ConvertSidToStringSidW( groups->Groups[i].Sid, &text );
            printf( "group %ls attributes 0x%08lx\n", text, groups->Groups[i].Attributes );
            LocalFree( text );
        }
        printf( "%lu groups\n", groups->GroupCount );
    }
    size = sizeof(admins);
    if (CreateWellKnownSid( WinBuiltinAdministratorsSid, NULL, admins, &size ) &&
        CheckTokenMembership( NULL, admins, &member ))
        printf( "CheckTokenMembership(NULL, Administrators): %d\n", member );
}

static DWORD WINAPI client( void *arg )
{
    HANDLE pipe;
    DWORD written;

    Sleep( 200 );
    pipe = CreateFileW( PIPE, GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING,
                        SECURITY_SQOS_PRESENT | SECURITY_IMPERSONATION, NULL );
    if (pipe == INVALID_HANDLE_VALUE)
    {
        printf( "client: open failed, error %lu\n", GetLastError() );
        return 1;
    }
    WriteFile( pipe, "x", 1, &written, NULL );
    Sleep( 1500 );
    CloseHandle( pipe );
    return 0;
}

int wmain( void )
{
    HANDLE pipe, thread, token;
    char byte;
    DWORD read;

    if (OpenProcessToken( GetCurrentProcess(), TOKEN_QUERY, &token ))
    {
        print_groups( "process token", token );
        CloseHandle( token );
    }
    pipe = CreateNamedPipeW( PIPE, PIPE_ACCESS_DUPLEX, PIPE_TYPE_BYTE | PIPE_WAIT, 1, 256, 256, 0, NULL );
    if (pipe == INVALID_HANDLE_VALUE)
    {
        printf( "server: create failed, error %lu\n", GetLastError() );
        return 1;
    }
    thread = CreateThread( NULL, 0, client, NULL, 0, NULL );
    if (!ConnectNamedPipe( pipe, NULL ) && GetLastError() != ERROR_PIPE_CONNECTED)
    {
        printf( "server: connect failed, error %lu\n", GetLastError() );
        return 1;
    }
    ReadFile( pipe, &byte, 1, &read, NULL );
    if (!ImpersonateNamedPipeClient( pipe ))
        printf( "server: ImpersonateNamedPipeClient failed, error %lu\n", GetLastError() );
    else
    {
        if (OpenThreadToken( GetCurrentThread(), TOKEN_QUERY, TRUE, &token ))
        {
            print_groups( "thread token while impersonating the pipe client", token );
            CloseHandle( token );
        }
        else printf( "server: OpenThreadToken failed, error %lu\n", GetLastError() );
        RevertToSelf();
    }
    WaitForSingleObject( thread, 5000 );
    return 0;
}

/*
 * netdom: join, leave and check the domain membership of this computer (docs/DOMAIN_JOIN.md)
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

/*
 * The syntax of Windows' NETDOM for the commands that make sense for one computer joined to a domain with Winbind:
 *
 *   NETDOM JOIN [computer] /Domain:D [/OU:DN] /UserD:USER [/PasswordD:{PASSWORD|*}] [/REBoot]
 *   NETDOM REMOVE [computer] [/Domain:D] [/UserD:USER] [/PasswordD:{PASSWORD|*}] [/Force] [/REBoot]
 *   NETDOM RENAMECOMPUTER [computer] /NewName:NAME /UserD:USER [/PasswordD:{PASSWORD|*}] [/Force] [/REBoot]
 *                                                           the host name and, in a domain, the machine account
 *   NETDOM VERIFY [computer] [/Domain:D]                    the secure channel of the machine account
 *   NETDOM RESET [computer] [/Domain:D]                     the same, with a new machine password first
 *   NETDOM QUERY DC [/Domain:D]                             the domain controllers
 *
 * The work is the netapi32 join functions' (NetJoinDomain, NetUnjoinDomain), which a process of the system group (an
 * elevated administrator) may call, and the secure channel functions of wexjoin. The computer is this one: another
 * computer is refused. /REBoot and /SecurePasswordPrompt are accepted and do nothing: the join takes effect at once, so no
 * restart is asked for (docs/DOMAIN_JOIN.md). A password given as * (or left out when a user is) is asked for without echo.
 * The exit status is 0, or the error code of the failure (87 for a wrong command line), as the batch files that test
 * ERRORLEVEL after NETDOM expect (one tests for 2224, an account that exists): found in forum threads, not documented.
 */

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include <windows.h>
#include <lm.h>

#include "wexconsole.h"
#include "wexhost.h"
#include "wexjoin.h"

#define MAX_TEXT 1024

static void usage(void)
{
    wex_console_write( L"The syntax of this command is:\n\n"
                       L"NETDOM JOIN [computer] /Domain:domain [/OU:ou path] /UserD:user [/PasswordD:{password|*}] [/REBoot]\n"
                       L"NETDOM REMOVE [computer] [/Domain:domain] [/UserD:user] [/PasswordD:{password|*}] [/Force] [/REBoot]\n"
                       L"NETDOM RENAMECOMPUTER [computer] /NewName:name [/UserD:user] [/PasswordD:{password|*}] [/Force] [/REBoot]\n"
                       L"NETDOM VERIFY [computer] [/Domain:domain]\n"
                       L"NETDOM RESET [computer] [/Domain:domain]\n"
                       L"NETDOM QUERY DC [/Domain:domain]\n"
                       L"NETDOM HELP\n\n"
                       L"The computer is this one. A password of * is asked for. No restart is needed or asked for: /REBoot does nothing.\n" );
}

/* the commands that Windows has and this system does not */
static BOOL is_other_command( const WCHAR *command )
{
    static const WCHAR * const others[] = { L"ADD", L"COMPUTERNAME", L"MOVE", L"RESETPWD", L"TRUST", L"TIME", L"RENAME", L"RELOCATE" };
    unsigned int i;

    for (i = 0; i < ARRAY_SIZE(others); i++) if (!_wcsicmp( command, others[i] )) return TRUE;
    return FALSE;
}

struct options
{
    const WCHAR *computer, *domain, *ou, *user, *password, *new_name, *query_type;
    BOOL force, read_only, machine_password, other_user;
};

/* "/Name:value" or "-Name:value": the name (without the mark) and the value, or NULL */
static BOOL split_option( WCHAR *arg, WCHAR **name, WCHAR **value )
{
    if (arg[0] != '/' && arg[0] != '-') return FALSE;
    *name = arg + 1;
    *value = wcschr( arg, ':' );
    if (*value) *(*value)++ = 0;
    return TRUE;
}

static BOOL name_is( const WCHAR *name, const WCHAR *full, const WCHAR *shortest )
{
    return !_wcsicmp( name, full ) || (shortest && !_wcsicmp( name, shortest ));
}

/* 0 for success, else the argument that is not understood */
static const WCHAR *parse_options( int argc, WCHAR **argv, struct options *o, BOOL query )
{
    int i;

    for (i = 2; i < argc; i++)
    {
        WCHAR *name, *value;

        if (!split_option( argv[i], &name, &value ))
        {
            if (query && !o->query_type) o->query_type = argv[i];
            else if (!query && !o->computer) o->computer = argv[i];
            else return argv[i];
            continue;
        }
        if (name_is( name, L"Domain", L"D" )) o->domain = value ? value : L"";
        else if (name_is( name, L"OU", NULL )) o->ou = value ? value : L"";
        else if (name_is( name, L"UserD", L"UD" )) o->user = value ? value : L"";
        else if (name_is( name, L"PasswordD", L"PD" )) o->password = value ? value : L"*";
        else if (name_is( name, L"NewName", NULL )) o->new_name = value ? value : L"";
        else if (name_is( name, L"Force", L"F" )) o->force = TRUE;
        else if (name_is( name, L"ReadOnly", L"RO" ) || name_is( name, L"PasswordM", L"PM" )) { o->read_only = TRUE; o->machine_password = TRUE; }
        else if (name_is( name, L"UserO", L"UO" ) || name_is( name, L"PasswordO", L"PO" ) || name_is( name, L"UserF", L"UF" ) || name_is( name, L"PasswordF", L"PF" ))
            o->other_user = TRUE;
        /* accepted, and nothing to do: no restart is asked for, the server is this computer's engine, verbose adds nothing here */
        else if (name_is( name, L"REBoot", L"REB" ) || name_is( name, L"SecurePasswordPrompt", L"SPP" ) || name_is( name, L"Verify", NULL ) ||
                 name_is( name, L"Verbose", L"V" ) || name_is( name, L"Server", L"S" )) {}
        else return argv[i];
    }
    return NULL;
}

/* a password asked for: without echo at a console, one line from standard input otherwise */
static BOOL ask_password( const WCHAR *prompt, WCHAR *password, DWORD chars )
{
    HANDLE in = GetStdHandle( STD_INPUT_HANDLE );
    DWORD mode, done, n = 0;
    WCHAR c;
    BOOL console = GetConsoleMode( in, &mode );

    wex_console_printf( L"%ls", prompt );
    if (console) SetConsoleMode( in, mode & ~ENABLE_ECHO_INPUT );
    while (n + 1 < chars)
    {
        BOOL ok = console ? ReadConsoleW( in, &c, 1, &done, NULL ) : ReadFile( in, &c, 1, &done, NULL );

        if (!ok || !done) break;
        if (!console) { c &= 0xff; }
        if (c == '\n') break;
        if (c == '\r') { if (console) break; continue; }
        password[n++] = c;
    }
    password[n] = 0;
    if (console) { SetConsoleMode( in, mode ); wex_console_write( L"\n" ); }
    return n > 0;
}

/* the failure: the system's text for the code (as NETDOM prints it), else the engine's; the engine's reason is added when
 * the two differ (Samba's or the engine's own words); then Windows' closing line. The exit status is the error code. */
static int failed( DWORD status )
{
    WCHAR message[MAX_TEXT] = { 0 }, system[400] = { 0 }, *end;

    wex_join_last_message( message, ARRAY_SIZE(message) );
    FormatMessageW( FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, NULL, status, 0, system, ARRAY_SIZE(system), NULL );
    for (end = system + wcslen( system ); end > system && (end[-1] == '\n' || end[-1] == '\r' || end[-1] == ' '); end--) end[-1] = 0;
    if (system[0])
    {
        wex_console_printf( L"%ls\n", system );
        if (message[0] && _wcsicmp( message, system )) wex_console_printf( L"(%ls)\n", message );
    }
    else if (message[0]) wex_console_printf( L"%ls\n", message );
    else wex_console_printf( L"Error %lu.\n", status );
    wex_console_write( L"\nThe command failed to complete successfully.\n" );
    return status ? status : 1;
}

/* a wrong command line, as NETDOM says it */
static int bad_parameters( const WCHAR *what )
{
    if (what && what[0]) wex_console_printf( L"%ls\n", what );
    wex_console_write( L"The parameter is incorrect.\nTry \"NetDom HELP\" for more information.\n\nThe command failed to complete successfully.\n" );
    return ERROR_INVALID_PARAMETER;
}

static int succeeded( void )
{
    WCHAR message[MAX_TEXT], line[MAX_TEXT];
    const WCHAR *p = message;

    wex_join_last_message( message, ARRAY_SIZE(message) );
    /* the engine's message, a line for each item */
    if (wcscmp( message, L"ok" ))
        while (wex_join_item( &p, line, ARRAY_SIZE(line) )) wex_console_printf( L"%ls\n", line );
    wex_console_write( L"\nThe command completed successfully.\n" );
    return 0;
}

/* the computer must be this one */
static BOOL is_this_computer( const WCHAR *computer )
{
    WCHAR name[MAX_COMPUTERNAME_LENGTH + 1];
    DWORD size = ARRAY_SIZE(name);

    if (!computer || !computer[0]) return TRUE;
    while (*computer == '\\') computer++;
    return GetComputerNameW( name, &size ) && !_wcsicmp( computer, name );
}

/* the account and its password: both given, or the user and a password asked for */
static BOOL credentials( struct options *o, WCHAR *password, DWORD chars, BOOL required, int *failure )
{
    if (!o->user || !o->user[0])
    {
        if (required) { wex_console_write( L"The /UserD option is required: this computer has no logged-on domain account to use.\n" ); *failure = bad_parameters( NULL ); }
        return !required;
    }
    if (o->password && wcscmp( o->password, L"*" ) && o->password[0]) { lstrcpynW( password, o->password, chars ); return TRUE; }
    if (!ask_password( L"Type the password associated with the domain user: ", password, chars ))
    {
        *failure = bad_parameters( L"A password is required." );
        return FALSE;
    }
    return TRUE;
}

static int cmd_join( struct options *o )
{
    WCHAR password[256];
    DWORD status;
    int failure = 1;

    if (!o->domain || !o->domain[0]) return bad_parameters( L"The /Domain option is required." );
    if (!is_this_computer( o->computer )) { wex_console_write( L"Joining another computer is not supported: run NETDOM on the computer.\n" ); return failed( ERROR_NOT_SUPPORTED ); }
    if (o->read_only || o->other_user) { wex_console_write( L"The read-only join and the options for another computer's account are not supported.\n" ); return failed( ERROR_NOT_SUPPORTED ); }
    if (!credentials( o, password, ARRAY_SIZE(password), TRUE, &failure )) return failure;
    status = NetJoinDomain( NULL, o->domain, o->ou && o->ou[0] ? o->ou : NULL, o->user, password, WEX_NETSETUP_JOIN_DOMAIN | WEX_NETSETUP_ACCT_CREATE );
    memset( password, 0, sizeof(password) );
    return status ? failed( status ) : succeeded();
}

static int cmd_remove( struct options *o )
{
    WCHAR password[256];
    DWORD status;
    int failure = 1;

    if (!is_this_computer( o->computer )) { wex_console_write( L"Removing another computer is not supported: run NETDOM on the computer.\n" ); return failed( ERROR_NOT_SUPPORTED ); }
    if (o->other_user) { wex_console_write( L"The options for another computer's account are not supported.\n" ); return failed( ERROR_NOT_SUPPORTED ); }
    /* with /Force and no account only this computer's side is undone (the controller may be out of reach) */
    if (!(o->force && (!o->user || !o->user[0])) && !credentials( o, password, ARRAY_SIZE(password), TRUE, &failure )) return failure;
    if (o->user && o->user[0]) status = NetUnjoinDomain( NULL, o->user, password, WEX_NETSETUP_ACCT_DELETE );
    else status = NetUnjoinDomain( NULL, NULL, NULL, 0 );
    memset( password, 0, sizeof(password) );
    return status ? failed( status ) : succeeded();
}

/* the computer's new name: in a domain the engine renames the machine account and the host name together; a computer
 * that is not in a domain only gets the host name */
static int cmd_rename( struct options *o )
{
    WCHAR password[256], *joined = NULL;
    char name[128];
    NETSETUP_JOIN_STATUS type = NetSetupUnknownStatus;
    DWORD status;
    int failure = 1;
    BOOL in_domain;

    if (!o->new_name) return bad_parameters( L"The /NewName option is required." );
    if (!is_this_computer( o->computer )) { wex_console_write( L"Renaming another computer is not supported: run NETDOM on the computer.\n" ); return failed( ERROR_NOT_SUPPORTED ); }
    if (o->other_user) { wex_console_write( L"The options for another computer's account are not supported.\n" ); return failed( ERROR_NOT_SUPPORTED ); }
    NetGetJoinInformation( NULL, &joined, &type );
    if (joined) NetApiBufferFree( joined );
    in_domain = type == NetSetupDomainName;
    if (in_domain && !credentials( o, password, ARRAY_SIZE(password), TRUE, &failure )) return failure;
    if (!WideCharToMultiByte( CP_UTF8, 0, o->new_name, -1, name, sizeof(name), NULL, NULL )) return bad_parameters( L"The new name is not valid." );
    if (in_domain)
    {
        /* the join engine renames the account and the host name together */
        status = NetRenameMachineInDomain( NULL, o->new_name, o->user, password, WEX_NETSETUP_ACCT_CREATE );
        memset( password, 0, sizeof(password) );
        return status ? failed( status ) : succeeded();
    }
    status = WexHostnameSet( NULL, name, FALSE );
    if (status) return failed( status == ERROR_CANCELLED ? ERROR_ACCESS_DENIED : status );
    return succeeded();
}

static int cmd_channel( struct options *o, DWORD op )
{
    DWORD status;

    if (!is_this_computer( o->computer )) { wex_console_write( L"Another computer is not supported: run NETDOM on the computer.\n" ); return failed( ERROR_NOT_SUPPORTED ); }
    status = wex_join_secure_channel( op );
    return status ? failed( status ) : succeeded();
}

static int cmd_query( struct options *o )
{
    DWORD status;
    WCHAR list[MAX_TEXT], dc[256];
    const WCHAR *p = list;

    if (!o->query_type || _wcsicmp( o->query_type, L"DC" ))
    {
        wex_console_write( L"Only NETDOM QUERY DC is supported on this system.\n" );
        return failed( ERROR_NOT_SUPPORTED );
    }
    if ((status = wex_join_secure_channel( WEX_JOIN_OP_DCLIST ))) return failed( status );
    wex_join_last_message( list, ARRAY_SIZE(list) );
    wex_console_write( L"List of domain controllers with accounts in the domain:\n\n" );
    while (wex_join_item( &p, dc, ARRAY_SIZE(dc) )) if (dc[0]) wex_console_printf( L"%ls\n", dc );
    wex_console_write( L"\nThe command completed successfully.\n" );
    return 0;
}

int __cdecl wmain( int argc, WCHAR **argv )
{
    struct options o = {0};
    const WCHAR *command, *bad;

    if (argc < 2 || !_wcsicmp( argv[1], L"HELP" ) || !_wcsicmp( argv[1], L"/?" ) || !_wcsicmp( argv[1], L"/HELP" ))
    {
        usage();
        return argc < 2 ? 1 : 0;
    }
    command = argv[1];
    if (argc == 3 && (!_wcsicmp( argv[2], L"/?" ) || !_wcsicmp( argv[2], L"/HELP" ))) { usage(); return 0; }
    if (is_other_command( command ))
    {
        wex_console_printf( L"NETDOM %ls is not supported on this system.\n", command );
        return failed( ERROR_NOT_SUPPORTED );
    }
    if ((bad = parse_options( argc, argv, &o, !_wcsicmp( command, L"QUERY" ) )))
    {
        wex_console_printf( L"The option %ls is not valid for this command.\n", bad );
        return bad_parameters( NULL );
    }
    if (!_wcsicmp( command, L"JOIN" )) return cmd_join( &o );
    if (!_wcsicmp( command, L"REMOVE" )) return cmd_remove( &o );
    if (!_wcsicmp( command, L"VERIFY" )) return cmd_channel( &o, 1 );
    if (!_wcsicmp( command, L"RESET" )) return cmd_channel( &o, 2 );
    if (!_wcsicmp( command, L"QUERY" )) return cmd_query( &o );
    if (!_wcsicmp( command, L"RENAMECOMPUTER" ))
    {
        return cmd_rename( &o );
    }
    wex_console_printf( L"The command %ls is not valid.\n", command );
    return bad_parameters( NULL );
}

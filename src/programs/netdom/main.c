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
 *   NETDOM RENAMECOMPUTER [computer] /NewName:NAME ...      (not supported yet)
 *   NETDOM VERIFY [computer] [/Domain:D]                    the secure channel of the machine account
 *   NETDOM RESET [computer] [/Domain:D]                     the same, with a new machine password first
 *   NETDOM QUERY DC [/Domain:D]                             the domain controllers
 *
 * The work is the netapi32 join functions' (NetJoinDomain, NetUnjoinDomain), which a process of the system group (an
 * elevated administrator) may call, and the secure channel functions of wexjoin. The computer is this one: another
 * computer is refused. /REBoot and /SecurePasswordPrompt are accepted and do nothing: the join takes effect at once, so no
 * restart is asked for (docs/DOMAIN_JOIN.md). A password given as * (or left out when a user is) is asked for without echo.
 * The exit status is 0 or 1; Windows' own codes for the failures were not checked.
 */

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include <windows.h>
#include <lm.h>

/* the flags of NetJoinDomain and NetUnjoinDomain (lmjoin.h of the Windows SDK) */
#define NETSETUP_JOIN_DOMAIN 0x00000001
#define NETSETUP_ACCT_CREATE 0x00000002
#define NETSETUP_ACCT_DELETE 0x00000004

#define MAX_TEXT 1024

/* Wine's lmjoin.h has only NetGetJoinInformation; these are netapi32's (forwarded to wexjoin.dll) */
NET_API_STATUS NET_API_FUNCTION NetJoinDomain( LPCWSTR server, LPCWSTR domain, LPCWSTR ou, LPCWSTR account, LPCWSTR password, DWORD flags );
NET_API_STATUS NET_API_FUNCTION NetUnjoinDomain( LPCWSTR server, LPCWSTR account, LPCWSTR password, DWORD flags );

static void out( const WCHAR *format, ... )
{
    WCHAR text[MAX_TEXT * 2];
    va_list args;
    DWORD done;
    HANDLE handle = GetStdHandle( STD_OUTPUT_HANDLE );

    va_start( args, format );
    vswprintf( text, ARRAY_SIZE(text), format, args );
    va_end( args );
    if (!WriteConsoleW( handle, text, wcslen( text ), &done, NULL ))
    {
        int len = WideCharToMultiByte( GetConsoleOutputCP(), 0, text, -1, NULL, 0, NULL, NULL );
        char *buf = malloc( len );
        if (!buf) return;
        WideCharToMultiByte( GetConsoleOutputCP(), 0, text, -1, buf, len, NULL, NULL );
        WriteFile( handle, buf, len - 1, &done, NULL );
        free( buf );
    }
}

static void usage(void)
{
    out( L"The syntax of this command is:\n\n"
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
        else if (name_is( name, L"ReadOnly", NULL ) || name_is( name, L"PasswordM", L"PM" )) { o->read_only = TRUE; o->machine_password = TRUE; }
        else if (name_is( name, L"UserO", L"UO" ) || name_is( name, L"PasswordO", L"PO" )) o->other_user = TRUE;
        else if (name_is( name, L"REBoot", L"REB" ) || name_is( name, L"SecurePasswordPrompt", L"SPP" ) || name_is( name, L"Verify", NULL )) {}
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

    out( L"%ls", prompt );
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
    if (console) { SetConsoleMode( in, mode ); out( L"\n" ); }
    return n > 0;
}

typedef DWORD (WINAPI *message_fn)( WCHAR *, DWORD );
typedef DWORD (WINAPI *channel_fn)( DWORD );

/* the message of the last call of wexjoin (the engine's reason, or Samba's), empty when there is none */
static void last_message( WCHAR *text, DWORD chars )
{
    HMODULE module = LoadLibraryW( L"wexjoin.dll" );
    message_fn message = module ? (message_fn)GetProcAddress( module, "WexJoinLastMessage" ) : NULL;

    text[0] = 0;
    if (message) message( text, chars );
}

/* the failure: the engine's words when there are any, else the system's for the code; then Windows' closing line */
static int failed( DWORD status )
{
    WCHAR message[MAX_TEXT] = { 0 }, system[400];

    last_message( message, ARRAY_SIZE(message) );
    if (message[0]) out( L"%ls\n", message );
    else if (FormatMessageW( FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, NULL, status, 0, system, ARRAY_SIZE(system), NULL ))
        out( L"%ls", system );
    else out( L"Error %lu.\n", status );
    out( L"\nThe command failed to complete successfully.\n" );
    return 1;
}

static int succeeded( void )
{
    WCHAR message[MAX_TEXT];

    last_message( message, ARRAY_SIZE(message) );
    if (message[0] && wcscmp( message, L"ok" )) out( L"%ls\n", message );
    out( L"\nThe command completed successfully.\n" );
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
static BOOL credentials( struct options *o, WCHAR *password, DWORD chars, BOOL required )
{
    if (!o->user || !o->user[0])
    {
        if (required) out( L"The /UserD option is required: this computer has no logged-on domain account to use.\n" );
        return !required;
    }
    if (o->password && wcscmp( o->password, L"*" ) && o->password[0]) { lstrcpynW( password, o->password, chars ); return TRUE; }
    if (!ask_password( L"Type the password associated with the domain user: ", password, chars ))
    {
        out( L"A password is required.\n" );
        return FALSE;
    }
    return TRUE;
}

static int cmd_join( struct options *o )
{
    WCHAR password[256];
    DWORD status;

    if (!o->domain || !o->domain[0]) { out( L"The /Domain option is required.\n\n" ); usage(); return 1; }
    if (!is_this_computer( o->computer )) { out( L"Joining another computer is not supported: run NETDOM on the computer.\n" ); return failed( ERROR_NOT_SUPPORTED ); }
    if (o->read_only || o->other_user) { out( L"The read-only join and the options for another computer's account are not supported.\n" ); return failed( ERROR_NOT_SUPPORTED ); }
    if (!credentials( o, password, ARRAY_SIZE(password), TRUE )) return 1;
    status = NetJoinDomain( NULL, o->domain, o->ou && o->ou[0] ? o->ou : NULL, o->user, password, NETSETUP_JOIN_DOMAIN | NETSETUP_ACCT_CREATE );
    memset( password, 0, sizeof(password) );
    return status ? failed( status ) : succeeded();
}

static int cmd_remove( struct options *o )
{
    WCHAR password[256];
    DWORD status;

    if (!is_this_computer( o->computer )) { out( L"Removing another computer is not supported: run NETDOM on the computer.\n" ); return failed( ERROR_NOT_SUPPORTED ); }
    if (o->other_user) { out( L"The options for another computer's account are not supported.\n" ); return failed( ERROR_NOT_SUPPORTED ); }
    /* with /Force and no account only this computer's side is undone (the controller may be out of reach) */
    if (!(o->force && (!o->user || !o->user[0])) && !credentials( o, password, ARRAY_SIZE(password), TRUE )) return 1;
    if (o->user && o->user[0]) status = NetUnjoinDomain( NULL, o->user, password, NETSETUP_ACCT_DELETE );
    else status = NetUnjoinDomain( NULL, NULL, NULL, 0 );
    memset( password, 0, sizeof(password) );
    return status ? failed( status ) : succeeded();
}

static int cmd_channel( struct options *o, DWORD op )
{
    HMODULE module;
    channel_fn channel;
    DWORD status;

    if (!is_this_computer( o->computer )) { out( L"Another computer is not supported: run NETDOM on the computer.\n" ); return failed( ERROR_NOT_SUPPORTED ); }
    if (!(module = LoadLibraryW( L"wexjoin.dll" )) || !(channel = (channel_fn)GetProcAddress( module, "WexJoinSecureChannel" )))
        return failed( ERROR_NOT_SUPPORTED );
    status = channel( op );
    return status ? failed( status ) : succeeded();
}

static int cmd_query( struct options *o )
{
    HMODULE module;
    channel_fn channel;
    DWORD status;
    WCHAR list[MAX_TEXT], *p, *end;

    if (!o->query_type || _wcsicmp( o->query_type, L"DC" ))
    {
        out( L"Only NETDOM QUERY DC is supported on this system.\n" );
        return failed( ERROR_NOT_SUPPORTED );
    }
    if (!(module = LoadLibraryW( L"wexjoin.dll" )) || !(channel = (channel_fn)GetProcAddress( module, "WexJoinSecureChannel" )))
        return failed( ERROR_NOT_SUPPORTED );
    if ((status = channel( 3 ))) return failed( status );
    last_message( list, ARRAY_SIZE(list) );
    out( L"List of domain controllers with accounts in the domain:\n\n" );
    for (p = list; *p; p = end)
    {
        while (*p == ' ') p++;
        if (!*p) break;
        for (end = p; *end && *end != ' '; end++) {}
        out( L"%.*ls\n", (int)(end - p), p );
    }
    out( L"\nThe command completed successfully.\n" );
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
        out( L"NETDOM %ls is not supported on this system.\n", command );
        return failed( ERROR_NOT_SUPPORTED );
    }
    if ((bad = parse_options( argc, argv, &o, !_wcsicmp( command, L"QUERY" ) )))
    {
        out( L"The option %ls is not valid for this command.\n\n", bad );
        usage();
        return 1;
    }
    if (!_wcsicmp( command, L"JOIN" )) return cmd_join( &o );
    if (!_wcsicmp( command, L"REMOVE" )) return cmd_remove( &o );
    if (!_wcsicmp( command, L"VERIFY" )) return cmd_channel( &o, 1 );
    if (!_wcsicmp( command, L"RESET" )) return cmd_channel( &o, 2 );
    if (!_wcsicmp( command, L"QUERY" )) return cmd_query( &o );
    if (!_wcsicmp( command, L"RENAMECOMPUTER" ))
    {
        if (!o.new_name) { out( L"The /NewName option is required.\n\n" ); usage(); return 1; }
        out( L"Renaming the computer in the domain is not supported yet on this system.\n" );
        return failed( ERROR_NOT_SUPPORTED );
    }
    out( L"The command %ls is not valid.\n\n", command );
    usage();
    return 1;
}

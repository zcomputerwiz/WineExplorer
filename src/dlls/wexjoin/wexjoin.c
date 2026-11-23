/*
 * wexjoin: NetJoinDomain, NetUnjoinDomain, NetValidateName and NetRenameMachineInDomain for WineExplorer
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
 * docs/DOMAIN_JOIN.md. The functions of netapi32 (forwarded from wkscli and netapi32) that Windows' Workstation service
 * answers. Here the answer is the join engine's (wex-domain-join.sh serve), reached through its socket by a process of
 * the system group (an elevated administrator's); a process that is not gets ERROR_ACCESS_DENIED, as on Windows a caller
 * who is not an administrator does. The engine's message (Samba's or the engine's own reason) is kept for the caller that
 * wants more than a code (netdom, the dialogs): WexJoinLastMessage.
 */

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <wchar.h>
#include <windef.h>
#include <winbase.h>
#include <winnls.h>
#include <winerror.h>
#include <lmerr.h>

#include "wexjoin_unix.h"
#include "wexjoin.h"

static BOOL unix_ready;
static WCHAR last_message[1024];

BOOL WINAPI DllMain( HINSTANCE inst, DWORD reason, void *reserved )
{
    if (reason == DLL_PROCESS_ATTACH)
    {
        DisableThreadLibraryCalls( inst );
        unix_ready = !__wine_init_unix_call();
    }
    return TRUE;
}

/* the message of the last call, for a tool that shows it (empty when there is none) */
DWORD WINAPI WexJoinLastMessage( WCHAR *buffer, DWORD chars )
{
    if (!buffer || !chars) return ERROR_INVALID_PARAMETER;
    lstrcpynW( buffer, last_message, chars );
    return ERROR_SUCCESS;
}

/* a request under construction: lines of key=value */
struct request
{
    char text[WEX_JOIN_REQUEST_MAX];
    size_t length;
    BOOL bad;
};

/* a field: the value in UTF-8, without a control character, at most 512 bytes (the engine's limit) */
static void add_field( struct request *r, const char *key, const WCHAR *value )
{
    char utf8[1024];
    int n, i;

    if (!value) return;
    n = WideCharToMultiByte( CP_UTF8, 0, value, -1, utf8, sizeof(utf8), NULL, NULL );
    if (!n || n - 1 > 512) r->bad = TRUE;
    for (i = 0; !r->bad && i < n - 1; i++) if ((unsigned char)utf8[i] < 0x20 || utf8[i] == 0x7f) r->bad = TRUE;
    if (!r->bad && r->length + strlen( key ) + n + 2 >= sizeof(r->text)) r->bad = TRUE;
    if (!r->bad) r->length += sprintf( r->text + r->length, "%s=%s\n", key, utf8 );
    SecureZeroMemory( utf8, sizeof(utf8) );   /* a field may be the password */
}

static void add_text( struct request *r, const char *key, const char *value )
{
    if (r->length + strlen( key ) + strlen( value ) + 2 >= sizeof(r->text)) { r->bad = TRUE; return; }
    r->length += sprintf( r->text + r->length, "%s=%s\n", key, value );
}

/* the request to the engine; the engine's status (or the error of reaching it) */
static DWORD ask( struct request *r )
{
    struct request_params params = {0};
    char reply[WEX_JOIN_REPLY_MAX];
    char *line, *end;
    DWORD status = ERROR_INVALID_DATA;

    last_message[0] = 0;
    if (r->bad) return ERROR_INVALID_PARAMETER;
    if (!unix_ready) return ERROR_NOT_SUPPORTED;
    r->text[r->length++] = '\n';          /* the empty line that ends the request */
    params.request = (ULONG_PTR)r->text;
    params.request_length = r->length;
    params.reply = (ULONG_PTR)reply;
    params.reply_size = sizeof(reply);
    WEXJOIN_CALL( join_request, &params );
    SecureZeroMemory( r->text, sizeof(r->text) );    /* the password does not stay in memory */
    if (params.err) return params.err;
    for (line = reply; line && *line; line = end ? end + 1 : NULL)
    {
        end = strchr( line, '\n' );
        if (end) *end = 0;
        if (!strncmp( line, "status=", 7 )) status = strtoul( line + 7, NULL, 10 );
        else if (!strncmp( line, "message=", 8 ))
            MultiByteToWideChar( CP_UTF8, 0, line + 8, -1, last_message, ARRAY_SIZE(last_message) );
        if (!end) break;
    }
    last_message[ARRAY_SIZE(last_message) - 1] = 0;
    return status;
}

/* the server of a call: none, or this computer (by name, with or without the leading backslashes) */
static BOOL is_local( const WCHAR *server )
{
    WCHAR name[MAX_COMPUTERNAME_LENGTH + 1];
    DWORD size = ARRAY_SIZE(name);

    if (!server || !server[0]) return TRUE;
    while (*server == '\\') server++;
    return GetComputerNameW( name, &size ) && !wcsicmp( server, name );
}

/* an account as "DOMAIN\user" or a user principal name: the engine takes the user and the user's own domain (it checks
 * that domain against the realm, and refuses one whose realm it cannot know), or the principal; the password too */
static void add_account( struct request *r, const WCHAR *account, const WCHAR *password )
{
    const WCHAR *slash = account ? wcschr( account, '\\' ) : NULL;
    WCHAR domain[256];

    if (slash && slash > account)
    {
        if (slash - account >= ARRAY_SIZE(domain)) { r->bad = TRUE; return; }
        memcpy( domain, account, (slash - account) * sizeof(WCHAR) );
        domain[slash - account] = 0;
        add_field( r, "userdomain", domain );
    }
    add_field( r, "user", slash ? slash + 1 : account );
    add_field( r, "password", password );
}

/* what the engine does is a join of a domain with an account that can create the computer: the other ways of the API
 * (an unsecured join to an account made beforehand, a read-only controller account, a join with the name queued by
 * SetComputerNameEx) are not offered */
#define SUPPORTED_JOIN (WEX_NETSETUP_JOIN_DOMAIN | WEX_NETSETUP_ACCT_CREATE | WEX_NETSETUP_WIN9X_UPGRADE | \
                        WEX_NETSETUP_DEFER_SPN_SET | WEX_NETSETUP_INSTALL_INVOCATION)

DWORD WINAPI NetJoinDomain( const WCHAR *server, const WCHAR *domain, const WCHAR *ou, const WCHAR *account,
                            const WCHAR *password, DWORD flags )
{
    struct request r = {0};
    WCHAR name[260], *slash;
    char text[16];

    if (!is_local( server )) return ERROR_NOT_SUPPORTED;
    if (!domain || !domain[0]) return ERROR_INVALID_PARAMETER;
    if ((flags & ~WEX_NETSETUP_IGNORE_UNSUPPORTED_FLAGS & ~SUPPORTED_JOIN) && !(flags & WEX_NETSETUP_IGNORE_UNSUPPORTED_FLAGS))
        return ERROR_NOT_SUPPORTED;
    if (!(flags & WEX_NETSETUP_JOIN_DOMAIN)) return ERROR_NOT_SUPPORTED;      /* the join of a workgroup is not offered */
    /* the engine joins with the credentials of an account: "the caller's context" has none to give */
    if (!account || !account[0] || !password) return ERROR_LOGON_FAILURE;
    /* "domain\controller" names a preferred controller: the engine finds its own */
    lstrcpynW( name, domain, ARRAY_SIZE(name) );
    if ((slash = wcschr( name, '\\' ))) *slash = 0;
    add_text( &r, "op", "join" );
    add_field( &r, "domain", name );
    add_account( &r, account, password );
    if (ou && ou[0]) add_field( &r, "ou", ou );
    sprintf( text, "%lu", (unsigned long)flags );
    add_text( &r, "flags", text );
    return ask( &r );
}

DWORD WINAPI NetUnjoinDomain( const WCHAR *server, const WCHAR *account, const WCHAR *password, DWORD flags )
{
    struct request r = {0};
    char text[16];

    if (!is_local( server )) return ERROR_NOT_SUPPORTED;
    if ((flags & ~(WEX_NETSETUP_ACCT_DELETE | WEX_NETSETUP_IGNORE_UNSUPPORTED_FLAGS)) && !(flags & WEX_NETSETUP_IGNORE_UNSUPPORTED_FLAGS))
        return ERROR_INVALID_FLAGS;
    add_text( &r, "op", "leave" );
    /* with no account, only this computer's side is undone (the machine account stays in the domain) */
    if (!account || !account[0]) add_text( &r, "force", "1" );
    else
    {
        if (!password) return ERROR_LOGON_FAILURE;
        add_account( &r, account, password );
    }
    sprintf( text, "%lu", (unsigned long)flags );
    add_text( &r, "flags", text );
    return ask( &r );
}

DWORD WINAPI NetValidateName( const WCHAR *server, const WCHAR *name, const WCHAR *account, const WCHAR *password, DWORD type )
{
    static const char * const types[] = { NULL, "machine", "workgroup", "domain", "nonexistentdomain", "dnsmachine" };
    struct request r = {0};

    if (!is_local( server )) return ERROR_NOT_SUPPORTED;
    if (!name || !name[0] || type == WEX_NETSETUP_NAME_UNKNOWN || type >= ARRAY_SIZE(types)) return ERROR_INVALID_PARAMETER;
    add_text( &r, "op", "validate" );
    add_text( &r, "nametype", types[type] );
    add_field( &r, "name", name );
    return ask( &r );
}

/* The secure channel of the machine account and the controllers (netdom verify, reset and query DC; nltest): the engine asks
 * Samba and Winbind. The answer's message is the text to show (WexJoinLastMessage), for the controllers the names. */
DWORD WINAPI WexJoinSecureChannel( DWORD op )
{
    static const char * const ops[] = { NULL, "verify", "reset", "dclist", "dsgetdc", "scquery" };
    struct request r = {0};

    if (!op || op >= ARRAY_SIZE(ops)) return ERROR_INVALID_PARAMETER;
    add_text( &r, "op", ops[op] );
    return ask( &r );
}

DWORD WINAPI NetRenameMachineInDomain( const WCHAR *server, const WCHAR *new_name, const WCHAR *account, const WCHAR *password,
                                       DWORD flags )
{
    struct request r = {0};

    if (!is_local( server )) return ERROR_NOT_SUPPORTED;
    if ((flags & ~(WEX_NETSETUP_ACCT_CREATE | WEX_NETSETUP_IGNORE_UNSUPPORTED_FLAGS)) && !(flags & WEX_NETSETUP_IGNORE_UNSUPPORTED_FLAGS))
        return ERROR_INVALID_FLAGS;
    if (!new_name || !new_name[0]) return ERROR_INVALID_PARAMETER;
    /* the engine renames the account with the credentials of an account that may: "the caller's context" has none to give */
    if (!account || !account[0] || !password) return ERROR_LOGON_FAILURE;
    /* the engine renames the account, Samba's own name and the host name together */
    add_text( &r, "op", "rename" );
    add_field( &r, "name", new_name );
    add_account( &r, account, password );
    return ask( &r );
}

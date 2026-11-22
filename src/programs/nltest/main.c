/*
 * nltest: the domain controller and secure channel queries of Windows' NLTEST (docs/DOMAIN_JOIN.md)
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
 * The options of NLTEST that make sense for one computer joined to a domain with Winbind, in Windows' syntax:
 *
 *   /DSGETDC:domain     the controller found for the domain (DsGetDcName), with its address, GUID, sites and flags
 *   /DCNAME:domain      the PDC of the domain
 *   /DCLIST:domain      the controllers of the domain
 *   /DSGETSITE          the site of this computer
 *   /QUERY              the state of the netlogon service (here: the secure channel works or not)
 *   /SC_QUERY:domain    the secure channel: the controller it is with, and its state
 *   /SC_VERIFY:domain   the same, checked (the machine account's password against the domain)
 *   /SC_RESET:domain    the same, with a new machine password
 *   /SC_CHANGE_PWD:domain   the machine password changed
 *   /SERVER:computer    this computer only
 *
 * The domain is the one this computer is joined to (the NetBIOS or the DNS name; none means this one). Other options of
 * Windows' NLTEST say that they are not supported here. The work is the join engine's, reached through wexjoin.dll by a
 * process of the system group (an elevated administrator), as for NETDOM; the controller is asked for what it says
 * about itself (a CLDAP ping). Output as NLTEST prints it: the results on standard output, "... failed: Status = N 0xN
 * NAME" on standard error with the exit status 1 (found by running the Windows tool; /DCLIST fails with status 0 there,
 * and here), "The command completed successfully" without a full stop. The flags names follow DS_..._FLAG's bit order.
 * The wording of the lines for /SC_RESET and /SC_CHANGE_PWD, and /DCLIST's lines (names only), are not checked against
 * Windows.
 */

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include <windows.h>
#include <lm.h>

#include "wexconsole.h"

#define MAX_TEXT 1024

static void print( HANDLE handle, const WCHAR *format, va_list args )
{
    WCHAR text[MAX_TEXT * 2];

    vswprintf( text, ARRAY_SIZE(text), format, args );
    wex_console_write_to( handle, text );
}

static void out( const WCHAR *format, ... )
{
    va_list args;
    va_start( args, format );
    print( GetStdHandle( STD_OUTPUT_HANDLE ), format, args );
    va_end( args );
}

static void err( const WCHAR *format, ... )
{
    va_list args;
    va_start( args, format );
    print( GetStdHandle( STD_ERROR_HANDLE ), format, args );
    va_end( args );
}

typedef DWORD (WINAPI *message_fn)( WCHAR *, DWORD );
typedef DWORD (WINAPI *channel_fn)( DWORD );

/* the operations of WexJoinSecureChannel (wexjoin.c) */
#define OP_VERIFY  1
#define OP_RESET   2
#define OP_DCLIST  3
#define OP_DSGETDC 4
#define OP_SCQUERY 5

/* the names of the status codes that NLTEST prints next to a number */
static const struct { DWORD code; const WCHAR *name; } status_names[] =
{
    { 0, L"NERR_Success" }, { 5, L"ERROR_ACCESS_DENIED" }, { 50, L"ERROR_NOT_SUPPORTED" }, { 87, L"ERROR_INVALID_PARAMETER" },
    { 170, L"ERROR_BUSY" }, { 1326, L"ERROR_LOGON_FAILURE" }, { 1355, L"ERROR_NO_SUCH_DOMAIN" }, { 1460, L"ERROR_TIMEOUT" },
    { 1722, L"RPC_S_SERVER_UNAVAILABLE" }, { 1789, L"ERROR_TRUSTED_DOMAIN_FAILURE" }, { 1919, L"ERROR_NO_SITENAME" },
    { 2138, L"NERR_WkstaNotStarted" }, { 2691, L"NERR_SetupAlreadyJoined" }, { 2692, L"NERR_SetupNotJoined" },
};

static const WCHAR *status_name( DWORD code )
{
    unsigned int i;

    for (i = 0; i < ARRAY_SIZE(status_names); i++) if (status_names[i].code == code) return status_names[i].name;
    return L"";
}

/* "Status = 1355 0x54b ERROR_NO_SUCH_DOMAIN" */
static void status_text( WCHAR *text, DWORD chars, DWORD code )
{
    swprintf( text, chars, L"Status = %lu 0x%lx %ls", code, code, status_name( code ) );
}

static int failed( const WCHAR *what, DWORD status )
{
    WCHAR text[200];

    status_text( text, ARRAY_SIZE(text), status );
    err( L"%ls: %ls\n", what, text );
    return 1;
}

static int done(void)
{
    out( L"The command completed successfully\n" );
    return 0;
}

static void usage(void)
{
    err( L"Usage: nltest [/OPTIONS]\n\n"
         L"    /SERVER:<ServerName> - This computer\n"
         L"    /QUERY - Query the netlogon service\n"
         L"    /SC_QUERY:<DomainName> - Query the secure channel for <Domain>\n"
         L"    /SC_RESET:<DomainName> - Reset the secure channel for <Domain>\n"
         L"    /SC_VERIFY:<DomainName> - Verify the secure channel for <Domain>\n"
         L"    /SC_CHANGE_PWD:<DomainName> - Change the secure channel password for <Domain>\n"
         L"    /DCLIST:<DomainName> - Get the list of DCs for <DomainName>\n"
         L"    /DCNAME:<DomainName> - Get the PDC name for <DomainName>\n"
         L"    /DSGETDC:<DomainName> - Find a DC of <DomainName> (flags are accepted and ignored)\n"
         L"    /DSGETSITE - Get the site name of this computer\n\n"
         L"The domain is the one this computer is a member of. Other options of Windows' NLTEST are not supported.\n" );
}

static DWORD channel_call( DWORD op, WCHAR *message, DWORD chars )
{
    HMODULE module = LoadLibraryW( L"wexjoin.dll" );
    channel_fn channel = module ? (channel_fn)GetProcAddress( module, "WexJoinSecureChannel" ) : NULL;
    message_fn last = module ? (message_fn)GetProcAddress( module, "WexJoinLastMessage" ) : NULL;
    DWORD status;

    message[0] = 0;
    if (!channel) return ERROR_NOT_SUPPORTED;
    status = channel( op );
    if (last) last( message, chars );
    return status;
}

/* the value of "name=" in the engine's "a=1||b=2" answer, copied into value */
static BOOL field( const WCHAR *message, const WCHAR *name, WCHAR *value, DWORD chars )
{
    size_t n = wcslen( name );
    const WCHAR *p = message, *end;

    value[0] = 0;
    while (*p)
    {
        end = wcsstr( p, L"||" );
        if (!end) end = p + wcslen( p );
        if (!wcsncmp( p, name, n ) && p[n] == '=')
        {
            size_t len = end - (p + n + 1);
            if (len >= chars) len = chars - 1;
            memcpy( value, p + n + 1, len * sizeof(WCHAR) );
            value[len] = 0;
            return TRUE;
        }
        p = *end ? end + 2 : end;
    }
    return FALSE;
}

/* the domain of this computer, by its NetBIOS and DNS names: the registry that session 0's services keep */
static BOOL our_domain( WCHAR *flat, WCHAR *dns, DWORD chars )
{
    DWORD size;
    BOOL ok;

    size = chars * sizeof(WCHAR);
    ok = !RegGetValueW( HKEY_LOCAL_MACHINE, L"Software\\WineExplorer\\Domain", L"FlatName", RRF_RT_REG_SZ, NULL, flat, &size );
    size = chars * sizeof(WCHAR);
    if (RegGetValueW( HKEY_LOCAL_MACHINE, L"Software\\WineExplorer\\Domain", L"Name", RRF_RT_REG_SZ, NULL, dns, &size )) dns[0] = 0;
    if (!ok) flat[0] = 0;
    return ok;
}

/* the domain named on the command line must be ours (none means ours); false: the answer is "no such domain" */
static BOOL domain_is_ours( const WCHAR *domain )
{
    WCHAR flat[100] = { 0 }, dns[256] = { 0 };

    if (!our_domain( flat, dns, ARRAY_SIZE(flat) )) return FALSE;
    if (!domain || !domain[0]) return TRUE;
    return !_wcsicmp( domain, flat ) || (dns[0] && !_wcsicmp( domain, dns ));
}

static BOOL is_this_computer( const WCHAR *computer )
{
    WCHAR name[MAX_COMPUTERNAME_LENGTH + 1];
    DWORD size = ARRAY_SIZE(name);

    if (!computer || !computer[0]) return TRUE;
    while (*computer == '\\') computer++;
    return GetComputerNameW( name, &size ) && !_wcsicmp( computer, name );
}

/* DsGetDcName's flags, in bit order, as NLTEST names them */
static void flag_names( DWORD flags, WCHAR *text, DWORD chars )
{
    static const struct { DWORD bit; const WCHAR *name; } names[] =
    {
        { 0x1, L"PDC" }, { 0x4, L"GC" }, { 0x10, L"DS" }, { 0x8, L"LDAP" }, { 0x20, L"KDC" }, { 0x40, L"TIMESERV" },
        { 0x80, L"CLOSE_SITE" }, { 0x100, L"WRITABLE" }, { 0x200, L"GOOD_TIMESERV" }, { 0x400, L"NDNC" },
        { 0x800, L"SELECT_SECRET" }, { 0x1000, L"FULL_SECRET" }, { 0x2000, L"WS" }, { 0x4000, L"DS_8" },
        { 0x8000, L"DS_9" }, { 0x10000, L"DS_10" }, { 0x20000, L"KEYLIST" }, { 0x20000000, L"DNS_DC" },
        { 0x40000000, L"DNS_DOMAIN" }, { 0x80000000, L"DNS_FOREST" },
    };
    unsigned int i;

    text[0] = 0;
    for (i = 0; i < ARRAY_SIZE(names); i++)
        if ((flags & names[i].bit) && wcslen( text ) + wcslen( names[i].name ) + 2 < chars)
        {
            wcscat( text, names[i].name );
            wcscat( text, L" " );
        }
}

/* /DSGETDC and the others that start from the controller's own answer */
static int dsgetdc( BOOL show, const WCHAR *domain, WCHAR *dc_out, WCHAR *site_out, WCHAR *domain_out )
{
    WCHAR message[MAX_TEXT], dc[256], addr[100], guid[64], dom[100], forest[256], dcsite[100], oursite[100], flags[32], names[300];
    DWORD status, bits;

    if (!domain_is_ours( domain )) return failed( L"Getting DC name failed", ERROR_NO_SUCH_DOMAIN );
    if ((status = channel_call( OP_DSGETDC, message, ARRAY_SIZE(message) ))) return failed( L"Getting DC name failed", status );
    field( message, L"dc", dc, ARRAY_SIZE(dc) );
    field( message, L"addr", addr, ARRAY_SIZE(addr) );
    field( message, L"guid", guid, ARRAY_SIZE(guid) );
    field( message, L"domain", dom, ARRAY_SIZE(dom) );
    field( message, L"forest", forest, ARRAY_SIZE(forest) );
    field( message, L"dcsite", dcsite, ARRAY_SIZE(dcsite) );
    field( message, L"oursite", oursite, ARRAY_SIZE(oursite) );
    field( message, L"flags", flags, ARRAY_SIZE(flags) );
    bits = wcstoul( flags, NULL, 16 );
    flag_names( bits, names, ARRAY_SIZE(names) );
    if (dc_out) lstrcpynW( dc_out, dc, 256 );
    if (site_out) lstrcpynW( site_out, oursite, 100 );
    if (domain_out) lstrcpynW( domain_out, dom, 100 );
    if (show)
    {
        out( L"           DC: \\\\%ls\n", dc );
        out( L"      Address: \\\\%ls\n", addr );
        out( L"     Dom Guid: %ls\n", guid );
        out( L"     Dom Name: %ls\n", dom );
        out( L"  Forest Name: %ls\n", forest );
        out( L" Dc Site Name: %ls\n", dcsite );
        out( L"Our Site Name: %ls\n", oursite );
        out( L"        Flags: %ls\n", names );
    }
    return -1;
}

/* the secure channel's header: "Flags: 30 HAS_IP  HAS_TIMESERV", the controller, its connection status */
static int sc_show( const WCHAR *domain, DWORD op, BOOL verify_line )
{
    WCHAR message[MAX_TEXT], dc[256], st[32], text[200];
    DWORD status, conn;

    if (!domain_is_ours( domain )) return failed( L"I_NetLogonControl failed", ERROR_NO_SUCH_DOMAIN );
    if (op == OP_RESET || op == OP_VERIFY)
    {
        status = channel_call( op, message, ARRAY_SIZE(message) );
        if (status && op == OP_RESET) return failed( L"I_NetLogonControl failed", status );
        if (status && op == OP_VERIFY)
        {
            /* the controller is still named; the verification is what failed */
            WCHAR q[MAX_TEXT];
            if (channel_call( OP_SCQUERY, q, ARRAY_SIZE(q) ) || !field( q, L"dc", dc, ARRAY_SIZE(dc) )) return failed( L"I_NetLogonControl failed", status );
            out( L"Flags: b0 HAS_IP  HAS_TIMESERV\n" );
            out( L"Trusted DC Name \\\\%ls\n", dc );
            out( L"Trusted DC Connection Status Status = 0 0x0 NERR_Success\n" );
            status_text( text, ARRAY_SIZE(text), status );
            out( L"Trust Verification Status = %ls\n", text + 9 );
            err( L"I_NetLogonControl failed: %ls\n", text );
            return 1;
        }
    }
    status = channel_call( OP_SCQUERY, message, ARRAY_SIZE(message) );
    if (status) return failed( L"I_NetLogonControl failed", status );
    field( message, L"dc", dc, ARRAY_SIZE(dc) );
    field( message, L"status", st, ARRAY_SIZE(st) );
    conn = wcstoul( st, NULL, 10 );
    out( L"Flags: %ls HAS_IP  HAS_TIMESERV\n", verify_line ? L"b0" : L"30" );
    out( L"Trusted DC Name \\\\%ls\n", dc );
    status_text( text, ARRAY_SIZE(text), conn );
    out( L"Trusted DC Connection Status %ls\n", text );
    if (verify_line) out( L"Trust Verification Status = %ls\n", text + 9 );
    return done();
}

static int cmd_dclist( const WCHAR *domain )
{
    WCHAR message[MAX_TEXT], dc[256], dom[100], *p, *end;
    DWORD status;
    int rc;

    if ((rc = dsgetdc( FALSE, domain, dc, NULL, dom )) >= 0) { return 0; }   /* NLTEST's exit status after this failure is 0 */
    if ((status = channel_call( OP_DCLIST, message, ARRAY_SIZE(message) ))) { failed( L"Cannot find DC to get DC list from.", status ); return 0; }
    out( L"Get list of DCs in domain '%ls' from '\\\\%ls'.\n", domain && domain[0] ? domain : dom, dc );
    for (p = message; *p; p = end)
    {
        while (*p == ' ') p++;
        if (!*p) break;
        for (end = p; *end && *end != ' '; end++) {}
        out( L"    %.*ls\n", (int)(end - p), p );
    }
    return done();
}

int __cdecl wmain( int argc, WCHAR **argv )
{
    WCHAR *action = NULL, *action_value = NULL, *server = NULL;
    int i, rc;
    WCHAR dc[256], site[100], dom[100], message[MAX_TEXT], st[32], text[200];

    if (argc < 2) { usage(); return 1; }
    for (i = 1; i < argc; i++)
    {
        WCHAR *arg = argv[i], *value;

        if (arg[0] != '/' && arg[0] != '-') { usage(); return 1; }
        value = wcschr( arg, ':' );
        if (value) *value++ = 0;
        arg++;
        if (!_wcsicmp( arg, L"server" )) { server = value; continue; }
        /* the flags of /DSGETDC and /DNSGETDC change nothing here */
        if (!_wcsicmp( arg, L"force" ) || !_wcsicmp( arg, L"writable" ) || !_wcsicmp( arg, L"pdc" ) || !_wcsicmp( arg, L"gc" ) ||
            !_wcsicmp( arg, L"ds" ) || !_wcsicmp( arg, L"kdc" ) || !_wcsicmp( arg, L"dns" ) || !_wcsicmp( arg, L"netbios" ) ||
            !_wcsicmp( arg, L"ip" ) || !_wcsicmp( arg, L"ldaponly" ) || !_wcsicmp( arg, L"avoidself" ) || !_wcsicmp( arg, L"ret_dns" ) ||
            !_wcsicmp( arg, L"ret_netbios" ) || !_wcsicmp( arg, L"timeserv" ) || !_wcsicmp( arg, L"gtimeserv" ) || !_wcsicmp( arg, L"ws" ) ||
            !_wcsicmp( arg, L"site" ) || !_wcsicmp( arg, L"account" ))
            continue;
        if (action) { usage(); return 1; }
        action = arg;
        action_value = value;
    }
    if (!action) { usage(); return 1; }
    if (!is_this_computer( server ))
    {
        err( L"I_NetLogonControl failed: Status = 50 0x32 ERROR_NOT_SUPPORTED\n" );
        return 1;
    }

    if (!_wcsicmp( action, L"dsgetdc" ) || !_wcsicmp( action, L"dnsgetdc" ))
    {
        rc = dsgetdc( TRUE, action_value, NULL, NULL, NULL );
        return rc < 0 ? done() : rc;
    }
    if (!_wcsicmp( action, L"dcname" ))
    {
        if ((rc = dsgetdc( FALSE, action_value, dc, NULL, dom )) >= 0) return rc;
        out( L"PDC for Domain %ls is \\\\%ls\n", dom, dc );
        return done();
    }
    if (!_wcsicmp( action, L"dsgetsite" ))
    {
        if ((rc = dsgetdc( FALSE, NULL, NULL, site, NULL )) >= 0) return rc;
        out( L"%ls\n", site );
        return done();
    }
    if (!_wcsicmp( action, L"dclist" )) return cmd_dclist( action_value );
    if (!_wcsicmp( action, L"sc_query" )) return sc_show( action_value, OP_SCQUERY, FALSE );
    if (!_wcsicmp( action, L"sc_verify" )) return sc_show( action_value, OP_VERIFY, TRUE );
    if (!_wcsicmp( action, L"sc_reset" ) || !_wcsicmp( action, L"sc_change_pwd" ))
    {
        WCHAR *backslash = action_value ? wcschr( action_value, '\\' ) : NULL;
        if (backslash) *backslash = 0;     /* /SC_RESET:domain\dc: the controller is Winbind's to choose */
        return sc_show( action_value, OP_RESET, FALSE );
    }
    if (!_wcsicmp( action, L"query" ))
    {
        DWORD status = channel_call( OP_SCQUERY, message, ARRAY_SIZE(message) );

        if (status) return failed( L"I_NetLogonControl failed", status );
        field( message, L"status", st, ARRAY_SIZE(st) );
        status_text( text, ARRAY_SIZE(text), wcstoul( st, NULL, 10 ) );
        out( L"Flags: 0\nConnection Status = %ls\n", text + 9 );
        return done();
    }
    err( L"/%ls is not supported on this system.\n", action );
    return 1;
}

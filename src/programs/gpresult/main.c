/*
 * gpresult: the Group Policy that applies to this computer and a user
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
 * docs/GROUP_POLICY.md, "gpresult". XP's command line and the outline of its report:
 *
 *   gpresult [/S system [/U user [/P password]]] [/USER name] [/SCOPE {USER|COMPUTER}] [/V | /Z]
 *
 * The data: the domain join (DsRoleGetPrimaryDomainInformation) and the objects applied for each client-side
 * extension (GetAppliedGPOList, which reads what session 0's policy writer recorded), merged in the order they
 * were applied. Another computer (/S) is not supported. /V and /Z add, for every object, the extensions it was
 * applied for.
 */

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <windows.h>
#include <sddl.h>
#include <dsrole.h>
#include <userenv.h>

#include "wexconsole.h"

static const struct { GUID guid; const WCHAR *name; } extensions[] =
{
    { { 0x35378eac, 0x683f, 0x11d2, { 0xa8, 0x9a, 0x00, 0xc0, 0x4f, 0xbb, 0xcf, 0xa2 } }, L"Administrative Templates" },
    { { 0x827d319e, 0x6eac, 0x11d2, { 0xa4, 0xea, 0x00, 0xc0, 0x4f, 0x79, 0xf8, 0x3a } }, L"Security" },
    { { 0x42b5faae, 0x6536, 0x11d2, { 0xae, 0x5a, 0x00, 0x00, 0xf8, 0x75, 0x71, 0xe3 } }, L"Scripts" },
};

struct applied
{
    WCHAR name[50];        /* the object's GUID */
    WCHAR display[256];
    unsigned int exts;     /* bit per entry of extensions[] */
};

static void usage(void)
{
    wex_console_write( L"GPRESULT [/S system [/U username [/P [password]]]] [/SCOPE scope]\n"
                       L"         [/USER targetusername] [/V | /Z]\n\n"
                       L"Description:\n"
                       L"    Displays the Resultant Set of Policy (RSoP) information for a target user and computer.\n\n"
                       L"Parameter List:\n"
                       L"    /S    system           Specifies the remote system to connect to (not supported).\n"
                       L"    /SCOPE scope           Specifies whether the user or the computer settings are displayed.\n"
                       L"                           Valid values: \"USER\", \"COMPUTER\".\n"
                       L"    /USER targetusername   Specifies the user name for which the RSoP data is displayed.\n"
                       L"    /V                     Specifies that verbose information is displayed.\n"
                       L"    /Z                     Specifies that the super-verbose information is displayed.\n"
                       L"    /?                     Displays this help message.\n" );
}

/* whether the caller may see the data of the user with this SID: the caller's own always, another user's only for an
 * administrator (as the tool of Windows has it) */
static BOOL may_show_user( PSID sid )
{
    BYTE buf[sizeof(TOKEN_USER) + SECURITY_MAX_SID_SIZE], admins[SECURITY_MAX_SID_SIZE];
    DWORD size = sizeof(admins);
    HANDLE token;
    BOOL ok = FALSE, member = FALSE;

    if (OpenProcessToken( GetCurrentProcess(), TOKEN_QUERY, &token ))
    {
        if (GetTokenInformation( token, TokenUser, buf, sizeof(buf), &size ) && EqualSid( ((TOKEN_USER *)buf)->User.Sid, sid ))
            ok = TRUE;
        CloseHandle( token );
    }
    if (ok) return TRUE;
    size = sizeof(admins);
    return CreateWellKnownSid( WinBuiltinAdministratorsSid, NULL, admins, &size ) &&
           CheckTokenMembership( NULL, admins, &member ) && member;
}

/* the objects applied for every known extension, merged in the order applied */
static unsigned int collect( BOOL machine, PSID sid, struct applied *list, unsigned int room )
{
    unsigned int count = 0, e, i;
    GROUP_POLICY_OBJECTW *gpos, *gpo;

    for (e = 0; e < ARRAY_SIZE(extensions); e++)
    {
        GUID guid = extensions[e].guid;
        unsigned int pos = 0;

        if (GetAppliedGPOListW( machine ? GPO_LIST_FLAG_MACHINE : 0, NULL, sid, &guid, &gpos ) || !gpos) continue;
        for (gpo = gpos; gpo; gpo = gpo->pNext)
        {
            for (i = 0; i < count; i++) if (!_wcsicmp( list[i].name, gpo->szGPOName )) break;
            if (i == count)
            {
                if (count == room) break;
                /* a new object goes after the one listed before it for this extension */
                memmove( list + pos + 1, list + pos, (count - pos) * sizeof(*list) );
                memset( list + pos, 0, sizeof(*list) );
                lstrcpynW( list[pos].name, gpo->szGPOName, ARRAY_SIZE(list[pos].name) );
                lstrcpynW( list[pos].display, gpo->lpDisplayName ? gpo->lpDisplayName : L"", ARRAY_SIZE(list[pos].display) );
                i = pos;
                count++;
            }
            list[i].exts |= 1u << e;
            pos = i + 1;
        }
        FreeGPOListW( gpos );
    }
    return count;
}

static void section( const WCHAR *title, BOOL machine, PSID sid, BOOL verbose, const WCHAR *source )
{
    static struct applied list[256];
    unsigned int count, i, e;
    size_t len = wcslen( title );

    wex_console_printf( L"\n%ls\n", title );
    for (i = 0; i < len; i++) wex_console_write( L"-" );
    wex_console_write( L"\n" );
    wex_console_printf( L"    Group Policy was applied from:      %ls\n", source ? source : L"N/A" );
    wex_console_write( L"\n    Applied Group Policy Objects\n    -----------------------------\n" );
    count = collect( machine, sid, list, ARRAY_SIZE(list) );
    if (!count) wex_console_write( L"        N/A\n" );
    for (i = 0; i < count; i++)
    {
        wex_console_printf( L"        %ls\n", list[i].display[0] ? list[i].display : list[i].name );
        if (!verbose) continue;
        wex_console_printf( L"            GPO: %ls\n", list[i].name );
        for (e = 0; e < ARRAY_SIZE(extensions); e++)
            if (list[i].exts & (1u << e)) wex_console_printf( L"            Applied for: %ls\n", extensions[e].name );
    }
}

int __cdecl wmain( int argc, WCHAR **argv )
{
    DSROLE_PRIMARY_DOMAIN_INFO_BASIC *info = NULL;
    WCHAR computer[MAX_COMPUTERNAME_LENGTH + 1], user[256], domain[256], profile[MAX_PATH];
    const WCHAR *target = NULL, *scope = NULL, *dom, *source;
    WCHAR product[128];
    HKEY key;
    BYTE sid_buf[SECURITY_MAX_SID_SIZE];
    PSID sid = (PSID)sid_buf;
    DWORD size, dsize, version;
    SID_NAME_USE use;
    BOOL verbose = FALSE, joined = FALSE;
    SYSTEMTIME now;
    WCHAR date[64], time[64];
    HANDLE token;
    int i;

    for (i = 1; i < argc; i++)
    {
        const WCHAR *arg = argv[i];

        if (arg[0] != '/' && arg[0] != '-') { usage(); return 1; }
        arg++;
        if (!_wcsicmp( arg, L"?" )) { usage(); return 0; }
        if (!_wcsicmp( arg, L"v" ) || !_wcsicmp( arg, L"z" )) verbose = TRUE;
        else if (!_wcsicmp( arg, L"scope" ) && i + 1 < argc) scope = argv[++i];
        else if (!_wcsicmp( arg, L"user" ) && i + 1 < argc) target = argv[++i];
        else if (!_wcsicmp( arg, L"s" ) && i + 1 < argc)
        {
            wex_console_write( L"ERROR: Connecting to another computer is not supported.\n" );
            return 1;
        }
        else { usage(); return 1; }
    }
    if (scope && _wcsicmp( scope, L"user" ) && _wcsicmp( scope, L"computer" )) { usage(); return 1; }

    /* the user: the one named, or the caller */
    if (target)
    {
        size = sizeof(sid_buf);
        dsize = ARRAY_SIZE(domain);
        if (!LookupAccountNameW( NULL, target, sid, &size, domain, &dsize, &use ))
        {
            wex_console_printf( L"ERROR: The user \"%s\" does not have RSoP data.\n", target );
            return 1;
        }
        if (!may_show_user( sid ))
        {
            wex_console_write( L"ERROR: Access denied. You must be an administrator to display the data of another user.\n" );
            return 1;
        }
        lstrcpynW( user, target, ARRAY_SIZE(user) );
    }
    else
    {
        BYTE buf[sizeof(TOKEN_USER) + SECURITY_MAX_SID_SIZE];
        size = ARRAY_SIZE(user);
        GetUserNameW( user, &size );
        if (!OpenProcessToken( GetCurrentProcess(), TOKEN_QUERY, &token ) ||
            !GetTokenInformation( token, TokenUser, buf, sizeof(buf), &size ) ||
            !CopySid( sizeof(sid_buf), sid, ((TOKEN_USER *)buf)->User.Sid ))
        {
            wex_console_printf( L"ERROR: %lu\n", GetLastError() );
            return 1;
        }
        CloseHandle( token );
    }

    size = ARRAY_SIZE(computer);
    GetComputerNameW( computer, &size );
    if (!DsRoleGetPrimaryDomainInformation( NULL, DsRolePrimaryDomainInfoBasic, (BYTE **)&info ) && info)
        joined = info->MachineRole == DsRole_RoleMemberWorkstation || info->MachineRole == DsRole_RoleMemberServer;
    dom = joined && info->DomainNameFlat ? info->DomainNameFlat : computer;
    version = GetVersion();

    GetLocalTime( &now );
    GetDateFormatW( LOCALE_USER_DEFAULT, 0, &now, NULL, date, ARRAY_SIZE(date) );
    GetTimeFormatW( LOCALE_USER_DEFAULT, 0, &now, NULL, time, ARRAY_SIZE(time) );
    wex_console_printf( L"\nGroup Policy Result tool v2.0\n\nCreated On %ls at %ls\n\n\n", date, time );
    wex_console_printf( L"RSOP data for %ls\\%ls on %ls : Logging Mode\n", dom, user, computer );
    wex_console_write( L"------------------------------------------------------------\n\n" );
    product[0] = 0;
    if (!RegOpenKeyExW( HKEY_LOCAL_MACHINE, L"Software\\Microsoft\\Windows NT\\CurrentVersion", 0,
                        KEY_QUERY_VALUE | KEY_WOW64_64KEY, &key ))
    {
        size = sizeof(product) - sizeof(WCHAR);
        if (RegQueryValueExW( key, L"ProductName", NULL, NULL, (BYTE *)product, &size )) size = 0;
        product[size / sizeof(WCHAR)] = 0;
        RegCloseKey( key );
    }
    wex_console_printf( L"OS Type:                     %ls\n", product );
    wex_console_printf( L"OS Configuration:            %ls\n", joined ? L"Member Workstation" : L"Standalone Workstation" );
    wex_console_printf( L"OS Version:                  %u.%u.%u\n", (unsigned int)LOBYTE(LOWORD(version)),
         (unsigned int)HIBYTE(LOWORD(version)), (unsigned int)(version < 0x80000000 ? HIWORD(version) : 0) );
    wex_console_printf( L"Domain Name:                 %ls\n", joined ? info->DomainNameFlat : L"N/A" );
    wex_console_printf( L"Domain Type:                 %ls\n", joined ? L"Windows 2000" : L"<Local Computer>" );
    wex_console_write( L"Site Name:                   N/A\n" );
    wex_console_write( L"Roaming Profile:             \n" );
    size = ARRAY_SIZE(profile);
    if (!target && OpenProcessToken( GetCurrentProcess(), TOKEN_QUERY, &token ))
    {
        if (!GetUserProfileDirectoryW( token, profile, &size )) profile[0] = 0;
        CloseHandle( token );
    }
    else profile[0] = 0;
    wex_console_printf( L"Local Profile:               %ls\n", profile );
    wex_console_write( L"Connected over a slow link?: No\n" );

    /* the domain, not one of its controllers: Samba picks the controller and does not record it */
    source = joined ? info->DomainNameDns : NULL;
    if (!scope || !_wcsicmp( scope, L"computer" )) section( L"COMPUTER SETTINGS", TRUE, NULL, verbose, source );
    if (!scope || !_wcsicmp( scope, L"user" )) section( L"USER SETTINGS", FALSE, sid, verbose, source );
    if (info) DsRoleFreeMemory( info );
    return 0;
}

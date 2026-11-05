/*
 * WineExplorer Desktop Environment - probe for the registry key DACL sequence
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

/* Probe for the registry key DACL modification sequence that Google's updater
 * service (CreateClientStateMedium) performs, which fails under Wine with
 * "AddAllowedAce returned -2147024891" (0x80070005, E_ACCESSDENIED).
 *
 * The sequence (Chromium AddAllowedAce, ATL CDacl/AtlGetDacl/AtlSetDacl style):
 *   1. RegCreateKeyExW(HKLM, L"Software\\WineTest\\Update\\ClientStateMedium",
 *                      0, NULL, 0, KEY_READ|KEY_WRITE|WRITE_DAC|READ_CONTROL, ...)
 *   2. GetSecurityInfo(hkey, SE_REGISTRY_KEY, DACL_SECURITY_INFORMATION, ...)
 *   3. EXPLICIT_ACCESS for Authenticated Users / Users / Everyone with
 *      KEY_ALL_ACCESS and KEY_READ|KEY_WRITE
 *   4. SetEntriesInAclW
 *   5. SetSecurityInfo(hkey, SE_REGISTRY_KEY, DACL_SECURITY_INFORMATION, ..., newDacl)
 *   6. Variants: RegGetKeySecurity/RegSetKeySecurity and
 *      SetNamedSecurityInfoW(L"MACHINE\\Software\\...")
 *
 * Every call's return value and GetLastError are printed.
 *
 * Also prints the current process token (user, groups, privileges) and the
 * owner/group of the key it creates.
 *
 * Usage: reg-acl-probe.exe [keep] [wow64]
 *   keep   do not delete the test key at the end
 *   wow64  open/create the key with KEY_WOW64_32KEY (the 32-bit / Wow6432Node view)
 *
 * Build: x86_64-w64-mingw32-gcc -Wall -O2 reg-acl-probe.c -o reg-acl-probe.exe -ladvapi32 -lole32
 *        i686-w64-mingw32-gcc -Wall -O2 reg-acl-probe.c -o reg-acl-probe.exe -ladvapi32 -lole32
 */

#include <windows.h>
#include <aclapi.h>
#include <sddl.h>
#include <stdio.h>
#include <string.h>

static const WCHAR key_path[]   = L"Software\\WineTest\\Update\\ClientStateMedium";
static const WCHAR named_path[] = L"MACHINE\\Software\\WineTest\\Update\\ClientStateMedium";

/* set to KEY_WOW64_32KEY when the "wow64" option is given, so a 64-bit probe can
 * exercise the 32-bit (Wow6432Node) view the way the x86 updater does */
static DWORD wow64_flag = 0;

static void ret_dword( const char *name, DWORD ret )
{
    printf( "%-46s = %lu (lasterr %lu)\n", name, ret, GetLastError() );
    fflush( stdout );
}

static PSID well_known( WELL_KNOWN_SID_TYPE type )
{
    PSID sid = LocalAlloc( LPTR, SECURITY_MAX_SID_SIZE );
    DWORD size = SECURITY_MAX_SID_SIZE;
    if (!CreateWellKnownSid( type, NULL, sid, &size ))
    {
        LocalFree( sid );
        return NULL;
    }
    return sid;
}

static void print_sid( const char *label, PSID sid )
{
    char *str = NULL;
    if (sid && ConvertSidToStringSidA( sid, &str ))
        printf( "  %-20s %s\n", label, str );
    else
        printf( "  %-20s <null>\n", label );
    LocalFree( str );
    fflush( stdout );
}

/* print the user, groups and privileges of the current process token */
static void print_token_info( void )
{
    HANDLE token;
    DWORD len = 0;
    PTOKEN_USER tu;
    PTOKEN_GROUPS tg;
    PTOKEN_PRIVILEGES tp;
    char *str = NULL;
    DWORD i;

    printf( "-- process token --\n" );
    if (!OpenProcessToken( GetCurrentProcess(),
                           TOKEN_QUERY | TOKEN_READ, &token ))
    {
        printf( "  OpenProcessToken failed: %lu\n", GetLastError() );
        fflush( stdout );
        return;
    }

    if (GetTokenInformation( token, TokenUser, NULL, 0, &len ) ||
        GetLastError() != ERROR_INSUFFICIENT_BUFFER)
        len = 0;
    if (len)
    {
        tu = LocalAlloc( LPTR, len );
        if (tu && GetTokenInformation( token, TokenUser, tu, len, &len ))
        {
            if (ConvertSidToStringSidA( tu->User.Sid, &str ))
                printf( "  user                 %s\n", str );
            else
                printf( "  user                 <convert failed %lu>\n", GetLastError() );
            LocalFree( str ); str = NULL;
        }
        LocalFree( tu );
    }

    if (GetTokenInformation( token, TokenGroups, NULL, 0, &len ) ||
        GetLastError() != ERROR_INSUFFICIENT_BUFFER)
        len = 0;
    if (len)
    {
        tg = LocalAlloc( LPTR, len );
        if (tg && GetTokenInformation( token, TokenGroups, tg, len, &len ))
        {
            printf( "  groups (%lu):\n", tg->GroupCount );
            for (i = 0; i < tg->GroupCount; i++)
            {
                if (ConvertSidToStringSidA( tg->Groups[i].Sid, &str ))
                    printf( "    %-40s attrs=0x%lx\n", str, tg->Groups[i].Attributes );
                else
                    printf( "    <convert failed %lu>\n", GetLastError() );
                LocalFree( str ); str = NULL;
            }
        }
        LocalFree( tg );
    }

    if (GetTokenInformation( token, TokenPrivileges, NULL, 0, &len ) ||
        GetLastError() != ERROR_INSUFFICIENT_BUFFER)
        len = 0;
    if (len)
    {
        tp = LocalAlloc( LPTR, len );
        if (tp && GetTokenInformation( token, TokenPrivileges, tp, len, &len ))
        {
            printf( "  privileges (%lu):\n", tp->PrivilegeCount );
            for (i = 0; i < tp->PrivilegeCount; i++)
            {
                char name[256];
                DWORD nlen = sizeof(name);
                if (LookupPrivilegeNameA( NULL, &tp->Privileges[i].Luid, name, &nlen ))
                    printf( "    %-40s %s\n", name,
                            (tp->Privileges[i].Attributes & SE_PRIVILEGE_ENABLED) ? "enabled" : "disabled" );
                else
                    printf( "    <luid %lu,%lu> %s\n",
                            tp->Privileges[i].Luid.LowPart, tp->Privileges[i].Luid.HighPart,
                            (tp->Privileges[i].Attributes & SE_PRIVILEGE_ENABLED) ? "enabled" : "disabled" );
            }
        }
        LocalFree( tp );
    }

    CloseHandle( token );
    fflush( stdout );
}

/* print the owner (and group) of a registry key */
static void print_key_owner( HKEY key )
{
    PSECURITY_DESCRIPTOR sd = NULL;
    PSID owner = NULL, group = NULL;

    printf( "-- key owner --\n" );
    if (GetSecurityInfo( key, SE_REGISTRY_KEY,
                         OWNER_SECURITY_INFORMATION | GROUP_SECURITY_INFORMATION,
                         &owner, &group, NULL, NULL, &sd ) == ERROR_SUCCESS)
    {
        print_sid( "owner:", owner );
        print_sid( "group:", group );
    }
    else
        printf( "  GetSecurityInfo(OWNER|GROUP) failed: %lu\n", GetLastError() );
    if (sd) LocalFree( sd );
    fflush( stdout );
}

/* create the test key fresh; returns a handle or INVALID_HANDLE_VALUE */
static HKEY create_key( const char *label )
{
    HKEY key;
    DWORD disp;
    LONG res = RegCreateKeyExW( HKEY_LOCAL_MACHINE, key_path, 0, NULL, 0,
                                KEY_READ | KEY_WRITE | WRITE_DAC | READ_CONTROL | wow64_flag,
                                NULL, &key, &disp );
    ret_dword( label, (res == ERROR_SUCCESS) ? 0 : (DWORD)res );
    if (res != ERROR_SUCCESS) return INVALID_HANDLE_VALUE;
    printf( "  %s disposition=%lu\n", label, disp );
    fflush( stdout );
    return key;
}

static void delete_key( void )
{
    LONG res = RegDeleteKeyW( HKEY_LOCAL_MACHINE, key_path );
    ret_dword( "RegDeleteKeyW(ClientStateMedium)", (res == ERROR_SUCCESS) ? 0 : (DWORD)res );
}

int main( int argc, char *argv[] )
{
    int keep = 0, i;
    char user[64] = "?";
    DWORD ulen = sizeof(user);
    PSID au, users, everyone, admin;
    PSID sids[3];
    const char *sid_names[3] = { "Authenticated Users", "Users", "Everyone" };
    ACCESS_MASK masks[2] = { KEY_ALL_ACCESS, KEY_READ | KEY_WRITE };
    const char *mask_names[2] = { "KEY_ALL_ACCESS", "KEY_READ|KEY_WRITE" };
    HKEY key;
    int m;

    for (i = 1; i < argc; i++)
    {
        if (!strcmp( argv[i], "keep" )) keep = 1;
        else if (!strcmp( argv[i], "wow64" )) wow64_flag = KEY_WOW64_32KEY;
    }

    printf( "=== reg-acl-probe (bitness %d, view %s) ===\n", (int)sizeof(void *) * 8,
            wow64_flag ? "KEY_WOW64_32KEY" : "default" );
    GetUserNameA( user, &ulen );
    printf( "user: %s\n", user );
    fflush( stdout );

    print_token_info();

    au       = well_known( WinAuthenticatedUserSid );
    users    = well_known( WinBuiltinUsersSid );
    everyone = well_known( WinWorldSid );
    admin    = well_known( WinBuiltinAdministratorsSid );
    sids[0] = au; sids[1] = users; sids[2] = everyone;
    print_sid( "AuthenticatedUsers:", au );
    print_sid( "Users:", users );
    print_sid( "Everyone:", everyone );
    print_sid( "Administrators:", admin );

    /* ---- phase 1: create the key with READ_CONTROL|WRITE_DAC ---- */
    key = create_key( "RegCreateKeyExW(KEY_READ|KEY_WRITE|WRITE_DAC|READ_CONTROL)" );
    if (key == INVALID_HANDLE_VALUE)
    {
        printf( "cannot create the key; trying to open an existing one\n" );
        if (RegOpenKeyExW( HKEY_LOCAL_MACHINE, key_path, 0,
                           KEY_READ | KEY_WRITE | WRITE_DAC | READ_CONTROL | wow64_flag, &key ))
        {
            printf( "no key to probe; aborting\n" );
            return 1;
        }
    }

    print_key_owner( key );

    /* ---- phase 2: GetSecurityInfo (DACL) ---- */
    {
        PSECURITY_DESCRIPTOR sd = NULL;
        PACL dacl = NULL;
        BOOL present, defaulted;
        DWORD err = GetSecurityInfo( key, SE_REGISTRY_KEY, DACL_SECURITY_INFORMATION,
                                     NULL, NULL, &dacl, NULL, &sd );
        ret_dword( "GetSecurityInfo(DACL)", err );
        if (err == ERROR_SUCCESS)
        {
            GetSecurityDescriptorDacl( sd, &present, &dacl, &defaulted );
            printf( "  dacl present=%d defaulted=%d\n", present, defaulted );
            fflush( stdout );
            LocalFree( sd );
        }
    }

    /* ---- phase 3: the AddAllowedAce sequence for each SID x mask ---- */
    for (i = 0; i < 3; i++)
    {
        for (m = 0; m < 2; m++)
        {
            PSECURITY_DESCRIPTOR sd = NULL;
            PACL old_dacl = NULL, new_dacl = NULL;
            EXPLICIT_ACCESSW ea;
            DWORD err;

            printf( "-- %s + %s --\n", sid_names[i], mask_names[m] );
            err = GetSecurityInfo( key, SE_REGISTRY_KEY, DACL_SECURITY_INFORMATION,
                                   NULL, NULL, &old_dacl, NULL, &sd );
            ret_dword( "  GetSecurityInfo(DACL)", err );
            if (err != ERROR_SUCCESS) { if (sd) LocalFree( sd ); continue; }

            memset( &ea, 0, sizeof(ea) );
            ea.grfAccessPermissions = masks[m];
            ea.grfAccessMode        = SET_ACCESS;
            ea.grfInheritance       = NO_INHERITANCE;
            BuildTrusteeWithSidW( &ea.Trustee, sids[i] );

            err = SetEntriesInAclW( 1, &ea, old_dacl, &new_dacl );
            ret_dword( "  SetEntriesInAclW", err );
            if (err != ERROR_SUCCESS) { LocalFree( sd ); continue; }

            err = SetSecurityInfo( key, SE_REGISTRY_KEY, DACL_SECURITY_INFORMATION,
                                   NULL, NULL, new_dacl, NULL );
            ret_dword( "  SetSecurityInfo(DACL)", err );

            LocalFree( new_dacl );
            LocalFree( sd );
        }
    }

    /* ---- phase 4: RegGetKeySecurity / RegSetKeySecurity ---- */
    {
        PSECURITY_DESCRIPTOR sd = NULL;
        PACL dacl = NULL, new_dacl = NULL;
        EXPLICIT_ACCESSW ea;
        DWORD size = 0, err;

        printf( "-- RegGetKeySecurity / RegSetKeySecurity --\n" );
        err = RegGetKeySecurity( key, DACL_SECURITY_INFORMATION, NULL, &size );
        ret_dword( "  RegGetKeySecurity(size)", err );
        if ((err == ERROR_SUCCESS || err == ERROR_INSUFFICIENT_BUFFER) && size)
        {
            sd = LocalAlloc( LPTR, size );
            err = RegGetKeySecurity( key, DACL_SECURITY_INFORMATION, sd, &size );
            ret_dword( "  RegGetKeySecurity(sd)", err );
            if (err == ERROR_SUCCESS)
            {
                GetSecurityDescriptorDacl( sd, NULL, &dacl, NULL );
                if (!dacl)
                {
                    printf( "  (no dacl in returned sd)\n" );
                    fflush( stdout );
                }
                else
                {
                    memset( &ea, 0, sizeof(ea) );
                    ea.grfAccessPermissions = KEY_ALL_ACCESS;
                    ea.grfAccessMode        = SET_ACCESS;
                    ea.grfInheritance       = NO_INHERITANCE;
                    BuildTrusteeWithSidW( &ea.Trustee, au );
                    err = SetEntriesInAclW( 1, &ea, dacl, &new_dacl );
                    ret_dword( "  SetEntriesInAclW", err );
                    if (err == ERROR_SUCCESS)
                    {
                        err = RegSetKeySecurity( key, DACL_SECURITY_INFORMATION, new_dacl );
                        ret_dword( "  RegSetKeySecurity(DACL)", err );
                        LocalFree( new_dacl );
                    }
                }
            }
            LocalFree( sd );
        }
    }

    /* ---- phase 5: SetNamedSecurityInfoW ---- */
    {
        PSECURITY_DESCRIPTOR sd = NULL;
        PACL dacl = NULL, new_dacl = NULL;
        EXPLICIT_ACCESSW ea;
        DWORD err;

        printf( "-- SetNamedSecurityInfoW(MACHINE\\\\...) --\n" );
        err = GetNamedSecurityInfoW( named_path, SE_REGISTRY_KEY, DACL_SECURITY_INFORMATION,
                                     NULL, NULL, &dacl, NULL, &sd );
        ret_dword( "  GetNamedSecurityInfoW(DACL)", err );
        if (err == ERROR_SUCCESS)
        {
            if (!dacl)
            {
                printf( "  (no dacl in returned sd)\n" );
                fflush( stdout );
            }
            else
            {
                memset( &ea, 0, sizeof(ea) );
                ea.grfAccessPermissions = KEY_ALL_ACCESS;
                ea.grfAccessMode        = SET_ACCESS;
                ea.grfInheritance       = NO_INHERITANCE;
                BuildTrusteeWithSidW( &ea.Trustee, au );
                err = SetEntriesInAclW( 1, &ea, dacl, &new_dacl );
                ret_dword( "  SetEntriesInAclW", err );
                if (err == ERROR_SUCCESS)
                {
                    err = SetNamedSecurityInfoW( (LPWSTR)named_path, SE_REGISTRY_KEY,
                                                 DACL_SECURITY_INFORMATION,
                                                 NULL, NULL, new_dacl, NULL );
                    ret_dword( "  SetNamedSecurityInfoW(DACL)", err );
                    LocalFree( new_dacl );
                }
            }
            LocalFree( sd );
        }
    }

    /* ---- phase 6: cleanup ---- */
    RegCloseKey( key );
    if (!keep) delete_key();
    else printf( "keeping the key\n" );

    LocalFree( au ); LocalFree( users ); LocalFree( everyone ); LocalFree( admin );
    printf( "=== done ===\n" );
    return 0;
}

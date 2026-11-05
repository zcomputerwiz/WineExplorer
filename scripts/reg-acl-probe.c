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
 * Usage: reg-acl-probe.exe [keep]
 *   keep  do not delete the test key at the end
 *
 * Build: x86_64-w64-mingw32-gcc -Wall -O2 reg-acl-probe.c -o reg-acl-probe.exe -ladvapi32
 *        i686-w64-mingw32-gcc -Wall -O2 reg-acl-probe.c -o reg-acl-probe.exe -ladvapi32
 */

#include <windows.h>
#include <aclapi.h>
#include <sddl.h>
#include <stdio.h>
#include <string.h>

static const WCHAR key_path[]   = L"Software\\WineTest\\Update\\ClientStateMedium";
static const WCHAR named_path[] = L"MACHINE\\Software\\WineTest\\Update\\ClientStateMedium";

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

/* create the test key fresh; returns a handle or INVALID_HANDLE_VALUE */
static HKEY create_key( const char *label )
{
    HKEY key;
    DWORD disp;
    LONG res = RegCreateKeyExW( HKEY_LOCAL_MACHINE, key_path, 0, NULL, 0,
                                KEY_READ | KEY_WRITE | WRITE_DAC | READ_CONTROL,
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
    int keep = (argc > 1 && !strcmp( argv[1], "keep" ));
    char user[64] = "?";
    DWORD ulen = sizeof(user);
    PSID au, users, everyone, admin;
    PSID sids[3];
    const char *sid_names[3] = { "Authenticated Users", "Users", "Everyone" };
    ACCESS_MASK masks[2] = { KEY_ALL_ACCESS, KEY_READ | KEY_WRITE };
    const char *mask_names[2] = { "KEY_ALL_ACCESS", "KEY_READ|KEY_WRITE" };
    HKEY key;
    int i, m;

    printf( "=== reg-acl-probe (bitness %d) ===\n", (int)sizeof(void *) * 8 );
    GetUserNameA( user, &ulen );
    printf( "user: %s\n", user );
    fflush( stdout );

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
                           KEY_READ | KEY_WRITE | WRITE_DAC | READ_CONTROL, &key ))
        {
            printf( "no key to probe; aborting\n" );
            return 1;
        }
    }

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

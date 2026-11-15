/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright 2026 WineExplorer contributors
 *
 * Print what the process token is: user, groups with attributes, elevation, integrity level.
 * Run it on Windows (as SYSTEM, for example from a scheduled task) and on Wine and compare.
 *
 *   x86_64-w64-mingw32-gcc -municode -o token-probe.exe token-probe.c -ladvapi32
 */
#include <windows.h>
#include <sddl.h>
#include <stdio.h>

static void print_sid( const char *label, PSID sid, DWORD attributes )
{
    WCHAR *text = NULL;

    ConvertSidToStringSidW( sid, &text );
    printf( "%s %ls attributes 0x%08lx\n", label, text ? text : L"?", attributes );
    LocalFree( text );
}

int wmain( void )
{
    static BYTE buffer[8192];
    TOKEN_ELEVATION_TYPE type = 0;
    TOKEN_ELEVATION elevation = { 0 };
    HANDLE token;
    DWORD size, i;
    BOOL member = FALSE;
    BYTE admins[SECURITY_MAX_SID_SIZE];

    if (!OpenProcessToken( GetCurrentProcess(), TOKEN_QUERY | TOKEN_DUPLICATE, &token ))
    {
        printf( "OpenProcessToken failed, error %lu\n", GetLastError() );
        return 1;
    }
    if (GetTokenInformation( token, TokenUser, buffer, sizeof(buffer), &size ))
        print_sid( "user ", ((TOKEN_USER *)buffer)->User.Sid, ((TOKEN_USER *)buffer)->User.Attributes );
    if (GetTokenInformation( token, TokenOwner, buffer, sizeof(buffer), &size ))
        print_sid( "owner", ((TOKEN_OWNER *)buffer)->Owner, 0 );
    if (GetTokenInformation( token, TokenGroups, buffer, sizeof(buffer), &size ))
    {
        TOKEN_GROUPS *groups = (TOKEN_GROUPS *)buffer;
        for (i = 0; i < groups->GroupCount; i++)
            print_sid( "group", groups->Groups[i].Sid, groups->Groups[i].Attributes );
    }
    if (GetTokenInformation( token, TokenElevationType, &type, sizeof(type), &size ))
        printf( "elevation type %u\n", type );
    if (GetTokenInformation( token, TokenElevation, &elevation, sizeof(elevation), &size ))
        printf( "elevated %lu\n", elevation.TokenIsElevated );
    if (GetTokenInformation( token, TokenIntegrityLevel, buffer, sizeof(buffer), &size ))
        print_sid( "integrity", ((TOKEN_MANDATORY_LABEL *)buffer)->Label.Sid,
                   ((TOKEN_MANDATORY_LABEL *)buffer)->Label.Attributes );
    size = sizeof(admins);
    if (CreateWellKnownSid( WinBuiltinAdministratorsSid, NULL, admins, &size ) &&
        CheckTokenMembership( NULL, admins, &member ))
        printf( "member of Administrators: %d\n", member );
    else printf( "CheckTokenMembership failed, error %lu\n", GetLastError() );
    return 0;
}

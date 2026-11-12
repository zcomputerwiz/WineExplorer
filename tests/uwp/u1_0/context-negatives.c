/*
 * WineExplorer U1.0: package context negative cases (Wine only)
 *
 * Copyright 2026 WineExplorer contributors
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
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

/*
 * Exercises the Wine-private token information class the package context is created and read
 * through (TokenWinePackage, wine/include/winternl.h), bypassing the activation manager and its
 * gate, to check what the server itself refuses:
 *
 *   context-negatives.exe AUMID FULL_NAME APP_EXE OTHER_EXE CORRUPT_AUMID
 *
 * AUMID names a registered application, FULL_NAME its package, APP_EXE the executable of its
 * catalog record, OTHER_EXE any other program, CORRUPT_AUMID an application whose record is
 * malformed. It is meaningless on Windows and exits with 77 there.
 */

#include <windows.h>
#include <stdio.h>
#include <wchar.h>

#define TOKEN_WINE_PACKAGE 1000

typedef LONG NTSTATUS;
NTSTATUS NTAPI NtSetInformationToken( HANDLE, int, void *, ULONG );
NTSTATUS NTAPI NtQueryInformationToken( HANDLE, int, void *, ULONG, ULONG * );

struct package_info
{
    ULONG full_name_len, family_name_len, app_id_len, root_len, executable_len, classes_len;
    WCHAR data[2048];
};

static int failures;

static void check( int ok, const char *name, const char *fmt, ... )
{
    va_list args;

    printf( "%s %s", ok ? "PASS" : "FAIL", name );
    if (!ok)
    {
        failures++;
        printf( " (" );
        va_start( args, fmt );
        vprintf( fmt, args );
        va_end( args );
        printf( ")" );
    }
    printf( "\n" );
}

static HANDLE duplicate_token( TOKEN_TYPE type )
{
    HANDLE process, token = NULL;

    if (!OpenProcessToken( GetCurrentProcess(), TOKEN_DUPLICATE | TOKEN_QUERY, &process )) return NULL;
    DuplicateTokenEx( process, TOKEN_ALL_ACCESS, NULL, SecurityImpersonation, type, &token );
    CloseHandle( process );
    return token;
}

static NTSTATUS set_package( HANDLE token, const WCHAR *aumid )
{
    return NtSetInformationToken( token, TOKEN_WINE_PACKAGE, (void *)aumid, wcslen( aumid ) * sizeof(WCHAR) );
}

/* start a program with the token; returns 1 when a process ran to its own exit */
static int run_with_token( HANDLE token, const WCHAR *exe, const WCHAR *args, DWORD *error, DWORD *exit_code )
{
    WCHAR cmdline[1024];
    STARTUPINFOW si = { sizeof(si) };
    PROCESS_INFORMATION pi;

    swprintf( cmdline, ARRAYSIZE(cmdline), L"\"%ls\" %ls", exe, args );
    *error = 0;
    *exit_code = ~0u;
    if (!CreateProcessAsUserW( token, exe, cmdline, NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi ))
    {
        *error = GetLastError();
        return 0;
    }
    WaitForSingleObject( pi.hProcess, 60000 );
    GetExitCodeProcess( pi.hProcess, exit_code );
    CloseHandle( pi.hThread );
    CloseHandle( pi.hProcess );
    return 1;
}

int wmain( int argc, WCHAR **argv )
{
    struct package_info info;
    WCHAR args[512];
    HANDLE token, process_token;
    DWORD error, exit_code;
    NTSTATUS status;
    ULONG size;
    int ran;

    if (argc != 6) return 2;
    if (!GetProcAddress( GetModuleHandleW( L"ntdll.dll" ), "wine_get_version" )) return 77;

    /* an unpackaged process has no context */
    OpenProcessToken( GetCurrentProcess(), TOKEN_QUERY | TOKEN_ADJUST_DEFAULT, &process_token );
    status = NtQueryInformationToken( process_token, TOKEN_WINE_PACKAGE, &info, sizeof(info), &size );
    check( status == (NTSTATUS)0xc0000225, "unpackaged_no_context", "status %#lx", status );

    /* a process cannot give the token it runs with a context */
    status = set_package( process_token, argv[1] );
    check( status == (NTSTATUS)0xc0000022, "own_process_token_refused", "status %#lx", status );
    status = NtQueryInformationToken( process_token, TOKEN_WINE_PACKAGE, &info, sizeof(info), &size );
    check( status == (NTSTATUS)0xc0000225, "own_process_token_unchanged", "status %#lx", status );
    CloseHandle( process_token );

    /* nor an impersonation token */
    token = duplicate_token( TokenImpersonation );
    status = set_package( token, argv[1] );
    check( status == (NTSTATUS)0xc0000022, "impersonation_token_refused", "status %#lx", status );
    CloseHandle( token );

    /* names that are not in the catalog, or are not names */
    token = duplicate_token( TokenPrimary );
    status = set_package( token, L"WineExplorer.NoSuchPackage_0000000000000!App" );
    check( status == (NTSTATUS)0xc0000225, "unknown_package", "status %#lx", status );
    status = set_package( token, L"..\\..\\system32_0000000000000!App" );
    check( status == (NTSTATUS)0xc000000d, "traversal_name", "status %#lx", status );
    status = set_package( token, L"NoSeparator" );
    check( status == (NTSTATUS)0xc000000d, "malformed_name", "status %#lx", status );
    /* a malformed record is an error, not "no package" */
    status = set_package( token, argv[5] );
    check( status == (NTSTATUS)0xc0000102, "corrupt_record", "status %#lx", status );
    status = NtQueryInformationToken( token, TOKEN_WINE_PACKAGE, &info, sizeof(info), &size );
    check( status == (NTSTATUS)0xc0000225, "failed_attempts_leave_no_context", "status %#lx", status );

    /* the real application: the context comes from the catalog and cannot be replaced */
    status = set_package( token, argv[1] );
    check( !status, "registered_application", "status %#lx", status );
    memset( &info, 0, sizeof(info) );
    status = NtQueryInformationToken( token, TOKEN_WINE_PACKAGE, &info, sizeof(info), &size );
    check( !status && info.full_name_len == wcslen( argv[2] ) * sizeof(WCHAR) &&
           !memcmp( info.data, argv[2], info.full_name_len ), "context_from_catalog", "status %#lx", status );
    status = set_package( token, argv[1] );
    check( status == (NTSTATUS)0xc0000022, "context_is_immutable", "status %#lx", status );

    /* a process started with it from another image does not run */
    ran = run_with_token( token, argv[4], L"/c exit 0", &error, &exit_code );
    check( !ran || exit_code != 0, "other_image_refused", "ran %d, exit code %#lx", ran, exit_code );
    printf( "INFO other_image: ran %d, error %lu, exit code %#lx\n", ran, error, exit_code );

    /* the application's own executable does, and gets the identity (it checks it itself) */
    swprintf( args, ARRAYSIZE(args), L"positive C:\\u1out\\direct-token.txt %ls", argv[2] );
    ran = run_with_token( token, argv[3], args, &error, &exit_code );
    printf( "INFO application_image: ran %d, error %lu, exit code %#lx\n", ran, error, exit_code );
    check( ran && !exit_code, "application_image_keeps_identity", "ran %d, error %lu, exit code %#lx", ran, error,
           exit_code );
    CloseHandle( token );

    return failures ? 1 : 0;
}

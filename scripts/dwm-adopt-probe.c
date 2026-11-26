/*
 * WineExplorer Desktop Environment - probe for who may take over a desktop whose manager died (B120)
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

/* Taking over the desktop window of a desktop whose manager died is for the manager's replacement only: the same user
 * and session, at least its integrity level, on the interactive window station (server/window.c,
 * may_take_over_desktop). Two negative cases, each logs its lines to a file:
 *
 *   dwm-adopt-probe.exe lowil      C:\adopt-lowil.log
 *      Starts a copy of itself ("lowchild") with a duplicate of its token set to Low integrity level. The child waits
 *      for C:\adopt.go, which the suite makes after the shell's explorer was killed and before a new one starts (the
 *      gap), then creates a window of the desktop class "#32769" on the shell's desktop. It must be refused:
 *        lowchild il=1000          the child's integrity level (Low)
 *        lowil DENIED error=5      CreateWindowEx failed with ERROR_ACCESS_DENIED   (or "lowil ADOPTED": a failure)
 *   dwm-adopt-probe.exe service    C:\adopt-service.log
 *      A desktop of a window station that is not the interactive one (a service's: a CreateWindowStation of its own)
 *      whose top window's thread ended is not taken over by a second thread of the same user: "service DENIED error=5".
 * Build: x86_64-w64-mingw32-gcc -municode -mwindows -o dwm-adopt-probe.exe dwm-adopt-probe.c -luser32 -ladvapi32 */

#include <windows.h>
#include <stdio.h>
#include <wchar.h>

static void log_to( const WCHAR *path, const char *fmt, ... ) __attribute__((format(printf, 2, 3)));
static void log_to( const WCHAR *path, const char *fmt, ... )
{
    va_list args;
    FILE *f = _wfopen( path, L"a" );

    if (!f) return;
    va_start( args, fmt );
    vfprintf( f, fmt, args );
    va_end( args );
    fputc( 10, f );
    fclose( f );
}

static HWND create_desktop_class_window( void )
{
    return CreateWindowExW( 0, L"#32769", NULL, WS_POPUP | WS_CLIPSIBLINGS | WS_CLIPCHILDREN, 0, 0, 0, 0, NULL, NULL,
                            GetModuleHandleW( NULL ), NULL );
}

static DWORD integrity_level( HANDLE token )
{
    BYTE buffer[64];
    DWORD size = sizeof(buffer);
    TOKEN_MANDATORY_LABEL *label = (TOKEN_MANDATORY_LABEL *)buffer;

    if (!GetTokenInformation( token, TokenIntegrityLevel, buffer, size, &size )) return 0;
    return *GetSidSubAuthority( label->Label.Sid, *GetSidSubAuthorityCount( label->Label.Sid ) - 1 );
}

static void lowchild( void )
{
    const WCHAR *path = L"C:\\adopt-lowil.log";
    HANDLE token;
    HWND hwnd;
    DWORD i;

    if (OpenProcessToken( GetCurrentProcess(), TOKEN_QUERY, &token ))
    {
        log_to( path, "lowchild il=%lx", integrity_level( token ));
        CloseHandle( token );
    }
    log_to( path, "lowchild ready" );
    for (i = 0; i < 1200 && GetFileAttributesW( L"C:\\adopt.go" ) == INVALID_FILE_ATTRIBUTES; i++) Sleep( 100 );
    SetLastError( 0 );
    hwnd = create_desktop_class_window();
    if (hwnd) log_to( path, "lowil ADOPTED hwnd=%p", hwnd );
    else log_to( path, "lowil DENIED error=%lu", GetLastError() );
}

static void lowil( void )
{
    const WCHAR *path = L"C:\\adopt-lowil.log";
    SID_IDENTIFIER_AUTHORITY authority = { SECURITY_MANDATORY_LABEL_AUTHORITY };
    TOKEN_MANDATORY_LABEL label = {{0}};
    PSID sid = NULL;
    HANDLE token, dup;
    STARTUPINFOW si = { sizeof(si) };
    PROCESS_INFORMATION pi;
    WCHAR exe[MAX_PATH], cmdline[MAX_PATH + 32];

    if (!OpenProcessToken( GetCurrentProcess(), TOKEN_DUPLICATE | TOKEN_QUERY | TOKEN_ADJUST_DEFAULT, &token ) ||
        !DuplicateTokenEx( token, MAXIMUM_ALLOWED, NULL, SecurityImpersonation, TokenPrimary, &dup ) ||
        !AllocateAndInitializeSid( &authority, 1, SECURITY_MANDATORY_LOW_RID, 0, 0, 0, 0, 0, 0, 0, &sid ))
    {
        log_to( path, "lowil FAIL setup error=%lu", GetLastError() );
        return;
    }
    label.Label.Sid = sid;
    label.Label.Attributes = SE_GROUP_INTEGRITY;
    if (!SetTokenInformation( dup, TokenIntegrityLevel, &label, sizeof(label) + GetLengthSid( sid )))
    {
        log_to( path, "lowil FAIL SetTokenInformation error=%lu", GetLastError() );
        return;
    }
    GetModuleFileNameW( NULL, exe, MAX_PATH );
    swprintf( cmdline, MAX_PATH + 32, L"\"%ls\" lowchild", exe );
    if (!CreateProcessAsUserW( dup, exe, cmdline, NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi ))
    {
        log_to( path, "lowil FAIL CreateProcessAsUser error=%lu", GetLastError() );
        return;
    }
    WaitForSingleObject( pi.hProcess, 150000 );
    log_to( path, "lowil parent done" );
}

static DWORD CALLBACK service_manager( void *arg )
{
    HDESK desktop = arg;

    if (!SetThreadDesktop( desktop )) log_to( L"C:\\adopt-service.log", "service FAIL manager SetThreadDesktop error=%lu", GetLastError() );
    if (!create_desktop_class_window()) log_to( L"C:\\adopt-service.log", "service FAIL manager window error=%lu", GetLastError() );
    return 0;   /* the thread ends: the desktop's top window has no owner */
}

static void service( void )
{
    const WCHAR *path = L"C:\\adopt-service.log";
    /* a window station that is not the interactive one (no WSF_VISIBLE), as a service's */
    HWINSTA station = CreateWindowStationW( L"adoptws", 0, WINSTA_ALL_ACCESS, NULL );
    HDESK desktop;
    HANDLE thread;
    HWND hwnd;

    if (!station || !SetProcessWindowStation( station ))
    {
        log_to( path, "service FAIL window station error=%lu", GetLastError() );
        return;
    }
    desktop = CreateDesktopW( L"adoptsvc", NULL, NULL, 0, GENERIC_ALL, NULL );
    if (!desktop)
    {
        log_to( path, "service FAIL CreateDesktop error=%lu", GetLastError() );
        return;
    }
    thread = CreateThread( NULL, 0, service_manager, desktop, 0, NULL );
    WaitForSingleObject( thread, 30000 );
    Sleep( 500 );
    if (!SetThreadDesktop( desktop ))
    {
        log_to( path, "service FAIL SetThreadDesktop error=%lu", GetLastError() );
        return;
    }
    SetLastError( 0 );
    hwnd = create_desktop_class_window();
    if (hwnd) log_to( path, "service ADOPTED hwnd=%p", hwnd );
    else log_to( path, "service DENIED error=%lu", GetLastError() );
}

int WINAPI wWinMain( HINSTANCE inst, HINSTANCE prev, LPWSTR cmdline, int show )
{
    if (cmdline && wcsstr( cmdline, L"lowchild" )) lowchild();
    else if (cmdline && wcsstr( cmdline, L"lowil" )) lowil();
    else if (cmdline && wcsstr( cmdline, L"service" )) service();
    return 0;
}

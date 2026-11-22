/*
 * WineExplorer app installer window (wexappinstall.exe)
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
 * What opening an .appx or .msix package shows, as Windows' App Installer does: the name, publisher, version and source of
 * the package, and Install and Cancel. Install is there only when the signature is trusted (measured on Windows 11: for a
 * package whose publisher is not trusted the button is greyed out and the window says why; there is no way around it in
 * the window). Everything about the package comes from "wexappx query", which reads it as the installer does, and the
 * installing is "wexappx install" without --development; this program parses nothing itself.
 *
 *   wexappinstall PACKAGE
 *
 * For tests, WEXAPPINSTALL_TEST=install or cancel presses that button a second after the window is shown (as consent.exe
 * has its test overrides).
 */

#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <wchar.h>

#define ID_INSTALL 1
#define ID_CANCEL  2
#define ID_TIMER   10
#define ID_TEST    11

#ifndef ARRAY_SIZE
#define ARRAY_SIZE(x) (sizeof(x) / sizeof((x)[0]))
#endif

struct info
{
    WCHAR state[32], name[128], display[256], publisher[512], signer[256], version[64], problem[256], note[512];
};

static struct info info;
static WCHAR package[MAX_PATH];
static HWND main_window, heading, details, source, message, install_button, cancel_button;
static HFONT font, heading_font;
static HANDLE install_process, install_log = INVALID_HANDLE_VALUE;
static WCHAR log_path[MAX_PATH];
static BOOL installing;

static void wexappx_path( WCHAR *path, DWORD size )
{
    WCHAR *slash;

    GetModuleFileNameW( NULL, path, size );
    if ((slash = wcsrchr( path, '\\' ))) slash[1] = 0;
    wcscat( path, L"wexappx.exe" );
}

/* wexappx with these arguments, hidden; the process handle (or NULL), stdout and stderr to log when it is not NULL */
static HANDLE run_wexappx( const WCHAR *args, HANDLE log )
{
    WCHAR exe[MAX_PATH], command[2 * MAX_PATH + 64];
    STARTUPINFOW startup = { sizeof(startup) };
    PROCESS_INFORMATION process;

    wexappx_path( exe, ARRAY_SIZE(exe) );
    swprintf( command, ARRAY_SIZE(command), L"\"%s\" %s", exe, args );
    startup.dwFlags = STARTF_USESHOWWINDOW;
    startup.wShowWindow = SW_HIDE;
    if (log)
    {
        SetHandleInformation( log, HANDLE_FLAG_INHERIT, HANDLE_FLAG_INHERIT );
        startup.dwFlags |= STARTF_USESTDHANDLES;
        startup.hStdOutput = startup.hStdError = log;
        startup.hStdInput = GetStdHandle( STD_INPUT_HANDLE );
    }
    if (!CreateProcessW( exe, command, NULL, NULL, log != NULL, CREATE_NO_WINDOW, NULL, NULL, &startup, &process )) return NULL;
    CloseHandle( process.hThread );
    return process.hProcess;
}

static void set_field( struct info *info, const WCHAR *key, size_t key_len, const WCHAR *value )
{
    struct { const WCHAR *key; WCHAR *dest; size_t size; } fields[] =
    {
        { L"state", info->state, ARRAY_SIZE(info->state) }, { L"name", info->name, ARRAY_SIZE(info->name) },
        { L"display", info->display, ARRAY_SIZE(info->display) }, { L"publisher", info->publisher, ARRAY_SIZE(info->publisher) },
        { L"signer", info->signer, ARRAY_SIZE(info->signer) }, { L"version", info->version, ARRAY_SIZE(info->version) },
        { L"problem", info->problem, ARRAY_SIZE(info->problem) }, { L"note", info->note, ARRAY_SIZE(info->note) },
    };
    unsigned int i;

    for (i = 0; i < ARRAY_SIZE(fields); i++)
        if (wcslen( fields[i].key ) == key_len && !wcsncmp( fields[i].key, key, key_len ) && wcslen( value ) < fields[i].size)
            wcscpy( fields[i].dest, value );
}

/* asks wexappx about the package; FALSE with a message in info.problem when it cannot be read */
static BOOL load_info(void)
{
    WCHAR temp[MAX_PATH], result[MAX_PATH], args[2 * MAX_PATH + 32];
    HANDLE process, file;
    DWORD size, read, code = 1;
    WCHAR *text, *line, *next;

    GetTempPathW( ARRAY_SIZE(temp), temp );
    swprintf( result, ARRAY_SIZE(result), L"%swexappinstall-%lu.txt", temp, GetCurrentProcessId() );
    swprintf( args, ARRAY_SIZE(args), L"query \"%s\" \"%s\"", package, result );
    if (!(process = run_wexappx( args, NULL ))) { wcscpy( info.problem, L"The package installer is not available." ); return FALSE; }
    WaitForSingleObject( process, 60000 );
    GetExitCodeProcess( process, &code );
    CloseHandle( process );
    file = CreateFileW( result, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL );
    if (code || file == INVALID_HANDLE_VALUE)
    {
        wcscpy( info.state, L"inconsistent" );
        wcscpy( info.problem, L"This file could not be read as an app package." );
        if (file != INVALID_HANDLE_VALUE) CloseHandle( file );
        DeleteFileW( result );
        return FALSE;
    }
    size = GetFileSize( file, NULL );
    if (size < 2 || size > 64 * 1024 || !(text = calloc( 1, size + 4 )))
    {
        CloseHandle( file );
        DeleteFileW( result );
        return FALSE;
    }
    ReadFile( file, text, size, &read, NULL );
    CloseHandle( file );
    DeleteFileW( result );
    for (line = text + 1; line && *line; line = next)   /* after the byte order mark */
    {
        WCHAR *equals;

        if ((next = wcschr( line, '\n' ))) *next++ = 0;
        if ((equals = wcschr( line, '=' ))) set_field( &info, line, equals - line, equals + 1 );
    }
    free( text );
    return TRUE;
}

/* the part of a distinguished name after CN=, up to the comma */
static void common_name( const WCHAR *subject, WCHAR *out, size_t size )
{
    const WCHAR *start = wcsstr( subject, L"CN=" ), *end;
    size_t len;

    if (!start) { wcsncpy( out, subject, size - 1 ); out[size - 1] = 0; return; }
    start += 3;
    end = wcschr( start, ',' );
    len = end ? (size_t)(end - start) : wcslen( start );
    if (len >= size) len = size - 1;
    wcsncpy( out, start, len );
    out[len] = 0;
}

/* why the install is not offered; empty when it is */
static const WCHAR *refusal_text(void)
{
    if (!wcscmp( info.state, L"trusted" )) return L"";
    if (!wcscmp( info.state, L"untrusted" ))
        return L"The certificate of the publisher of this package is not trusted on this computer, so the package cannot be "
               L"installed. Ask the publisher for a package signed with a certificate you trust.";
    if (!wcscmp( info.state, L"unsigned" ))
        return L"This package is not signed, so it cannot be installed from here.";
    if (!wcscmp( info.state, L"invalid" ))
        return L"The signature of this package is not valid: the package or its signature was changed after it was signed.";
    if (!wcscmp( info.state, L"mismatch" ))
        return L"This package was signed by someone other than the publisher it names, so it cannot be installed.";
    if (!wcscmp( info.state, L"revoked" ))
        return L"The certificate that signed this package was revoked, so the package cannot be installed.";
    return info.problem[0] ? info.problem : L"This file could not be read as an app package.";
}

static void update_texts(void)
{
    WCHAR title[512], lines[1536], publisher[256], shown[256];
    BOOL known = !wcscmp( info.state, L"trusted" );

    if (!info.name[0]) wcscpy( shown, L"this app" );
    else if (info.display[0] && wcsncmp( info.display, L"ms-resource:", 12 )) wcscpy( shown, info.display );
    else wcscpy( shown, info.name );
    common_name( info.signer[0] ? info.signer : info.publisher, publisher, ARRAY_SIZE(publisher) );
    swprintf( title, ARRAY_SIZE(title), L"Install %s?", shown );
    swprintf( lines, ARRAY_SIZE(lines), L"Publisher: %s\r\nVersion: %s", known ? publisher : L"Unknown",
              info.version[0] ? info.version : L"" );
    SetWindowTextW( heading, title );
    SetWindowTextW( details, lines );
    swprintf( title, ARRAY_SIZE(title), L"Source: %s", package );
    SetWindowTextW( source, title );
    SetWindowTextW( message, refusal_text() );
    ShowWindow( message, refusal_text()[0] ? SW_SHOW : SW_HIDE );
    EnableWindow( install_button, !refusal_text()[0] );
}

static void read_log(WCHAR *text, size_t size)
{
    char raw[2048];
    DWORD read = 0;
    size_t i;
    char *line, *last = NULL;

    text[0] = 0;
    if (install_log == INVALID_HANDLE_VALUE) return;
    SetFilePointer( install_log, 0, NULL, FILE_BEGIN );
    ReadFile( install_log, raw, sizeof(raw) - 1, &read, NULL );
    raw[read] = 0;
    for (line = strtok( raw, "\r\n" ); line; line = strtok( NULL, "\r\n" ))
        if (!strncmp( line, "wexappx: ", 9 )) last = line + 9;
    if (!last) return;
    for (i = 0; last[i] && i < size - 1; i++) text[i] = (unsigned char)last[i];
    text[i] = 0;
}

static void start_install(void)
{
    WCHAR temp[MAX_PATH], args[2 * MAX_PATH + 32];

    installing = TRUE;
    EnableWindow( install_button, FALSE );
    SetWindowTextW( message, L"Installing..." );
    ShowWindow( message, SW_SHOW );
    GetTempPathW( ARRAY_SIZE(temp), temp );
    swprintf( log_path, ARRAY_SIZE(log_path), L"%swexappinstall-%lu.log", temp, GetCurrentProcessId() );
    install_log = CreateFileW( log_path, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, CREATE_ALWAYS,
                               FILE_ATTRIBUTE_TEMPORARY, NULL );
    swprintf( args, ARRAY_SIZE(args), L"install \"%s\"", package );
    install_process = run_wexappx( args, install_log == INVALID_HANDLE_VALUE ? NULL : install_log );
    if (!install_process)
    {
        installing = FALSE;
        SetWindowTextW( message, L"The package installer could not be started." );
        ShowWindow( message, SW_SHOW );
        return;
    }
    SetTimer( main_window, ID_TIMER, 200, NULL );
}

static void finish_install(void)
{
    DWORD code = 1;
    WCHAR text[600], reason[512];

    KillTimer( main_window, ID_TIMER );
    GetExitCodeProcess( install_process, &code );
    CloseHandle( install_process );
    install_process = NULL;
    installing = FALSE;
    read_log( reason, ARRAY_SIZE(reason) );
    if (install_log != INVALID_HANDLE_VALUE) { CloseHandle( install_log ); DeleteFileW( log_path ); }
    if (code) swprintf( text, ARRAY_SIZE(text), L"The app could not be installed. %s", reason );
    else wcscpy( text, L"The app was installed." );
    SetWindowTextW( message, text );
    ShowWindow( message, SW_SHOW );
    SetWindowTextW( cancel_button, L"Close" );
}

static LRESULT CALLBACK window_proc( HWND hwnd, UINT msg, WPARAM wp, LPARAM lp )
{
    switch (msg)
    {
    case WM_CREATE:
    {
        HINSTANCE instance = GetModuleHandleW( NULL );
        const WCHAR *test = _wgetenv( L"WEXAPPINSTALL_TEST" );

        main_window = hwnd;

        heading = CreateWindowW( L"STATIC", L"", WS_CHILD | WS_VISIBLE | SS_LEFT | SS_ENDELLIPSIS, 24, 20, 420, 36, hwnd, NULL, instance, NULL );
        details = CreateWindowW( L"STATIC", L"", WS_CHILD | WS_VISIBLE | SS_LEFT, 24, 66, 420, 44, hwnd, NULL, instance, NULL );
        source = CreateWindowW( L"STATIC", L"", WS_CHILD | WS_VISIBLE | SS_LEFT | SS_PATHELLIPSIS, 24, 112, 572, 22, hwnd, NULL, instance, NULL );
        message = CreateWindowW( L"STATIC", L"", WS_CHILD | WS_VISIBLE | WS_BORDER | SS_LEFT, 24, 150, 572, 100, hwnd, NULL, instance, NULL );
        install_button = CreateWindowW( L"BUTTON", L"Install", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON, 396, 276, 96, 28, hwnd, (HMENU)ID_INSTALL, instance, NULL );
        cancel_button = CreateWindowW( L"BUTTON", L"Cancel", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON, 500, 276, 96, 28, hwnd, (HMENU)ID_CANCEL, instance, NULL );
        font = CreateFontW( -15, 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, L"Segoe UI" );
        heading_font = CreateFontW( -26, 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, L"Segoe UI" );
        SendMessageW( heading, WM_SETFONT, (WPARAM)heading_font, TRUE );
        SendMessageW( details, WM_SETFONT, (WPARAM)font, TRUE );
        SendMessageW( source, WM_SETFONT, (WPARAM)font, TRUE );
        SendMessageW( message, WM_SETFONT, (WPARAM)font, TRUE );
        SendMessageW( install_button, WM_SETFONT, (WPARAM)font, TRUE );
        SendMessageW( cancel_button, WM_SETFONT, (WPARAM)font, TRUE );
        load_info();
        update_texts();
        if (test && (!wcscmp( test, L"install" ) || !wcscmp( test, L"cancel" ))) SetTimer( hwnd, ID_TEST, 1000, NULL );
        return 0;
    }
    case WM_TIMER:
        if (wp == ID_TEST)
        {
            const WCHAR *test = _wgetenv( L"WEXAPPINSTALL_TEST" );

            KillTimer( hwnd, ID_TEST );
            if (test && !wcscmp( test, L"install" ) && IsWindowEnabled( install_button )) PostMessageW( hwnd, WM_COMMAND, ID_INSTALL, 0 );
            else if (test && !wcscmp( test, L"cancel" )) PostMessageW( hwnd, WM_COMMAND, ID_CANCEL, 0 );
        }
        else if (wp == ID_TIMER && installing && install_process && WaitForSingleObject( install_process, 0 ) == WAIT_OBJECT_0)
        {
            finish_install();
            if (_wgetenv( L"WEXAPPINSTALL_TEST" )) SetTimer( hwnd, ID_TEST + 1, 1500, NULL );
        }
        else if (wp == ID_TEST + 1) PostMessageW( hwnd, WM_CLOSE, 0, 0 );
        return 0;
    case WM_COMMAND:
        if (LOWORD(wp) == ID_INSTALL && !installing) start_install();
        else if (LOWORD(wp) == ID_CANCEL && !installing) DestroyWindow( hwnd );
        return 0;
    case WM_CLOSE:
        if (!installing) DestroyWindow( hwnd );
        return 0;
    case WM_DESTROY:
        PostQuitMessage( 0 );
        return 0;
    }
    return DefWindowProcW( hwnd, msg, wp, lp );
}

int WINAPI wWinMain( HINSTANCE instance, HINSTANCE previous, WCHAR *command, int show )
{
    WNDCLASSW wc = { 0 };
    int argc;
    WCHAR **argv = CommandLineToArgvW( GetCommandLineW(), &argc );
    RECT area;
    HWND window;
    MSG msg;

    if (!argv || argc != 2)
    {
        MessageBoxW( NULL, L"Usage: wexappinstall PACKAGE", L"App Installer", MB_OK | MB_ICONERROR );
        return 2;
    }
    wcsncpy( package, argv[1], ARRAY_SIZE(package) - 1 );
    wc.lpfnWndProc = window_proc;
    wc.hInstance = instance;
    wc.hCursor = LoadCursorW( NULL, (LPCWSTR)IDC_ARROW );
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wc.lpszClassName = L"WexAppInstall";
    RegisterClassW( &wc );
    SystemParametersInfoW( SPI_GETWORKAREA, 0, &area, 0 );
    window = CreateWindowW( L"WexAppInstall", L"App Installer", WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX | WS_VISIBLE,
                            (area.right - area.left - 640) / 2, (area.bottom - area.top - 400) / 2, 640, 400, NULL, NULL, instance, NULL );
    if (!window) return 1;
    while (GetMessageW( &msg, NULL, 0, 0 ))
    {
        if (!IsDialogMessageW( window, &msg )) { TranslateMessage( &msg ); DispatchMessageW( &msg ); }
    }
    return 0;
}

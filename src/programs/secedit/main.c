/*
 * secedit: the local security settings of a machine without (or beneath) a domain
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
 * docs/GROUP_POLICY.md, "Local policy". The parts of the security template that apply to WineExplorer are the account
 * lockout policy and the security options in the policy trees (the consent settings); the rest of Windows' template
 * (rights, audit, restricted groups, services, permissions, password policy) belongs to the host and is not read.
 *
 *   secedit /export /cfg FILE         writes those settings as they are now
 *   secedit /configure /cfg FILE      makes FILE the local object's security template
 *                                     (%SystemRoot%\System32\GroupPolicy\Machine\Microsoft\Windows NT\SecEdit\GptTmpl.inf)
 *                                     and refreshes policy (gpupdate) so that it applies; a domain's template that sets
 *                                     the same values is applied after it and wins, as on Windows
 *
 * The local store is in session 0's root: only an administrator's elevated process can write it. /db, /areas, /log
 * and /overwrite are accepted for the command lines of Windows and do not change what is done; /areas other than
 * SECURITYPOLICY are reported as not applicable.
 */

#include <stdio.h>
#include <stdlib.h>
#include <windows.h>

#define ELEVATION_KEY   L"Software\\WineExplorer\\Elevation"
#define SYSTEM_KEY      L"Software\\Microsoft\\Windows\\CurrentVersion\\Policies\\System"
#define LONGEST_MINUTES 99999

static void out( const WCHAR *text )
{
    DWORD done;
    HANDLE handle = GetStdHandle( STD_OUTPUT_HANDLE );

    /* a console gets the text as it is; a pipe or file gets it in the console's code page */
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
    out( L"The SECEDIT command configures and exports the security settings that apply to this computer.\n\n"
         L"SECEDIT /export /cfg <filename> [/areas SECURITYPOLICY] [/log <filename>] [/quiet]\n"
         L"SECEDIT /configure /cfg <filename> [/db <filename>] [/areas SECURITYPOLICY] [/overwrite]\n"
         L"        [/log <filename>] [/quiet]\n\n"
         L"    /export       Writes the account lockout policy and the security options as a template.\n"
         L"    /configure    Makes the template the local security template and applies it.\n"
         L"    /cfg          The template file (an INF file).\n"
         L"    /db, /overwrite, /log, /quiet  Accepted; they do not change what is done here.\n"
         L"    /areas        Only SECURITYPOLICY applies; the other areas are not applicable.\n" );
}

static void error_text( DWORD err )
{
    WCHAR text[600], msg[400];

    if (!FormatMessageW( FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, NULL, err, 0, msg,
                         ARRAY_SIZE(msg), NULL ))
        swprintf( msg, ARRAY_SIZE(msg), L"%lu\n", err );
    swprintf( text, ARRAY_SIZE(text), L"Task is not completed.\n%s", msg );
    out( text );
}

static DWORD read_dword( HKEY root, const WCHAR *key_name, const WCHAR *name, DWORD *value )
{
    DWORD size = sizeof(*value), type;
    HKEY key;
    LONG err;

    if ((err = RegOpenKeyExW( root, key_name, 0, KEY_QUERY_VALUE | KEY_WOW64_64KEY, &key ))) return err;
    err = RegQueryValueExW( key, name, NULL, &type, (BYTE *)value, &size );
    RegCloseKey( key );
    if (!err && type != REG_DWORD) err = ERROR_INVALID_DATA;
    return err;
}

/* the text of the template: UTF-16, so a growing buffer of WCHARs */
struct text
{
    WCHAR *data;
    size_t len, size;
};

static BOOL add_text( struct text *t, const WCHAR *line )
{
    size_t n = wcslen( line );

    if (t->len + n + 1 > t->size)
    {
        WCHAR *p;
        size_t size = (t->size ? t->size * 2 : 1024) + n;
        if (!(p = realloc( t->data, size * sizeof(WCHAR) ))) return FALSE;
        t->data = p;
        t->size = size;
    }
    memcpy( t->data + t->len, line, n * sizeof(WCHAR) );
    t->len += n;
    return TRUE;
}

/* a formatted line: add( t, format, arguments ) */
static WCHAR line_buffer[600];
#define add( t, ... ) (swprintf( line_buffer, ARRAY_SIZE(line_buffer), __VA_ARGS__ ) >= 0 && add_text( (t), line_buffer ))

/* the template, as the writer (wexpolicy security) reads it back */
static BOOL make_template( struct text *t )
{
    static const WCHAR *consent[] = { L"ConsentPromptBehaviorAdmin", L"ConsentPromptBehaviorUser", L"EnableLUA",
                                      L"PromptOnSecureDesktop", L"EnableInstallerDetection" };
    DWORD bad = 0, duration = 30, reset = 30, value;
    BOOL ok;
    int i;

    read_dword( HKEY_LOCAL_MACHINE, ELEVATION_KEY, L"LockoutBadCount", &bad );
    read_dword( HKEY_LOCAL_MACHINE, ELEVATION_KEY, L"LockoutDuration", &duration );
    read_dword( HKEY_LOCAL_MACHINE, ELEVATION_KEY, L"ResetLockoutCount", &reset );
    ok = add( t, L"[Unicode]\r\nUnicode=yes\r\n[System Access]\r\n" ) &&
         add( t, L"LockoutBadCount = %lu\r\n", bad ) &&
         add( t, L"ResetLockoutCount = %lu\r\n", reset );
    /* "forever" is -1 in a template and the longest time (99999 minutes) in the registry */
    if (ok && duration >= LONGEST_MINUTES) ok = add( t, L"LockoutDuration = -1\r\n" );
    else if (ok) ok = add( t, L"LockoutDuration = %lu\r\n", duration );
    if (ok) ok = add( t, L"[Registry Values]\r\n" );
    for (i = 0; ok && i < ARRAY_SIZE(consent); i++)
        if (!read_dword( HKEY_LOCAL_MACHINE, SYSTEM_KEY, consent[i], &value ))
            ok = add( t, L"MACHINE\\%s\\%s=4,%lu\r\n", SYSTEM_KEY, consent[i], value );
    if (ok) ok = add( t, L"[Version]\r\nsignature=\"$CHICAGO$\"\r\nRevision=1\r\n" );
    return ok;
}

/* the file as UTF-16LE with a byte order mark: an ANSI file (Windows accepts one) is converted */
static BOOL write_utf16( const WCHAR *path, const WCHAR *text, size_t len )
{
    DWORD done;
    WCHAR bom = 0xfeff;
    HANDLE file = CreateFileW( path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL );

    if (file == INVALID_HANDLE_VALUE) return FALSE;
    if (!WriteFile( file, &bom, sizeof(bom), &done, NULL ) ||
        !WriteFile( file, text, len * sizeof(WCHAR), &done, NULL ))
    {
        DWORD err = GetLastError();
        CloseHandle( file );
        DeleteFileW( path );
        SetLastError( err );
        return FALSE;
    }
    return CloseHandle( file );
}

/* a template file read as text: UTF-16LE (a byte order mark) or the ANSI code page; at most 1 MB */
static WCHAR *read_template( const WCHAR *path, size_t *len, DWORD *err )
{
    HANDLE file = CreateFileW( path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL );
    DWORD size, done;
    BYTE *bytes;
    WCHAR *text;

    *err = ERROR_SUCCESS;
    if (file == INVALID_HANDLE_VALUE) { *err = GetLastError(); return NULL; }
    size = GetFileSize( file, NULL );
    if (size == INVALID_FILE_SIZE || size > 1024 * 1024 || !size)
    {
        CloseHandle( file );
        *err = ERROR_BAD_FORMAT;
        return NULL;
    }
    if (!(bytes = malloc( size + 2 )))
    {
        CloseHandle( file );
        *err = ERROR_NOT_ENOUGH_MEMORY;
        return NULL;
    }
    if (!ReadFile( file, bytes, size, &done, NULL ) || done != size) { *err = GetLastError(); CloseHandle( file ); free( bytes ); return NULL; }
    CloseHandle( file );
    bytes[size] = bytes[size + 1] = 0;
    if (size >= 2 && bytes[0] == 0xff && bytes[1] == 0xfe)
    {
        size_t n = (size - 2) / sizeof(WCHAR);
        text = malloc( (n + 1) * sizeof(WCHAR) );
        if (text) { memcpy( text, bytes + 2, n * sizeof(WCHAR) ); text[n] = 0; *len = n; }
    }
    else
    {
        int n = MultiByteToWideChar( CP_ACP, 0, (char *)bytes, size, NULL, 0 );
        text = n ? malloc( (n + 1) * sizeof(WCHAR) ) : NULL;
        if (text) { MultiByteToWideChar( CP_ACP, 0, (char *)bytes, size, text, n ); text[n] = 0; *len = n; }
    }
    free( bytes );
    if (!text) *err = ERROR_NOT_ENOUGH_MEMORY;
    return text;
}

/* a template has at least one [section] line, and no NUL inside */
static BOOL looks_like_template( const WCHAR *text, size_t len )
{
    size_t i;

    if (wcslen( text ) != len) return FALSE;
    for (i = 0; i < len; i++)
    {
        if ((i == 0 || text[i - 1] == '\n') && text[i] == '[') return TRUE;
    }
    return FALSE;
}

static DWORD make_directories( WCHAR *path )
{
    WCHAR *p;

    for (p = wcschr( path + 3, '\\' ); p; p = wcschr( p + 1, '\\' ))
    {
        *p = 0;
        if (!CreateDirectoryW( path, NULL ) && GetLastError() != ERROR_ALREADY_EXISTS)
        {
            DWORD err = GetLastError();
            *p = '\\';
            return err;
        }
        *p = '\\';
    }
    return ERROR_SUCCESS;
}

/* run gpupdate for the computer and wait for the refresh to end: the template is applied when this returns */
static DWORD refresh(void)
{
    WCHAR system[MAX_PATH], command[2 * MAX_PATH];
    STARTUPINFOW startup = { sizeof(startup) };
    PROCESS_INFORMATION info;
    DWORD code = 1;

    GetSystemDirectoryW( system, ARRAY_SIZE(system) );
    swprintf( command, ARRAY_SIZE(command), L"\"%s\\gpupdate.exe\" /target:computer /wait:600", system );
    if (!CreateProcessW( NULL, command, NULL, NULL, FALSE, 0, NULL, NULL, &startup, &info )) return GetLastError();
    WaitForSingleObject( info.hProcess, INFINITE );
    GetExitCodeProcess( info.hProcess, &code );
    CloseHandle( info.hThread );
    CloseHandle( info.hProcess );
    return code ? ERROR_TIMEOUT : ERROR_SUCCESS;
}

int __cdecl wmain( int argc, WCHAR **argv )
{
    static const WCHAR local_store[] = L"\\GroupPolicy\\Machine\\Microsoft\\Windows NT\\SecEdit\\GptTmpl.inf";
    BOOL export = FALSE, configure = FALSE, quiet = FALSE;
    const WCHAR *cfg = NULL;
    WCHAR store[MAX_PATH + 80], temp[MAX_PATH + 90];
    DWORD err;
    struct text t = { 0 };
    WCHAR *text;
    size_t len;
    int i;

    for (i = 1; i < argc; i++)
    {
        const WCHAR *arg = argv[i];

        if (arg[0] != '/' && arg[0] != '-') { usage(); return 1; }
        arg++;
        if (!_wcsicmp( arg, L"export" )) export = TRUE;
        else if (!_wcsicmp( arg, L"configure" )) configure = TRUE;
        else if (!_wcsicmp( arg, L"quiet" ) || !_wcsicmp( arg, L"overwrite" )) quiet |= !_wcsicmp( arg, L"quiet" );
        else if (!_wcsicmp( arg, L"cfg" ) && i + 1 < argc) cfg = argv[++i];
        else if ((!_wcsicmp( arg, L"db" ) || !_wcsicmp( arg, L"log" )) && i + 1 < argc) i++;
        else if (!_wcsicmp( arg, L"areas" ) && i + 1 < argc)
        {
            /* only the security policy applies; the other areas are not applicable here */
            for (i++; i < argc && argv[i][0] != '/' && argv[i][0] != '-'; i++)
                if (_wcsicmp( argv[i], L"SECURITYPOLICY" ) && !quiet)
                    out( L"The area is not applicable to this computer and is skipped.\n" );
            i--;
        }
        else { usage(); return 1; }
    }
    if (export == configure || !cfg) { usage(); return 1; }

    if (export)
    {
        if (!make_template( &t )) { error_text( ERROR_NOT_ENOUGH_MEMORY ); return 1; }
        if (!write_utf16( cfg, t.data, t.len )) { error_text( GetLastError() ); free( t.data ); return 1; }
        free( t.data );
        if (!quiet) out( L"The task has completed successfully.\n" );
        return 0;
    }

    /* configure: the file must be a template; it becomes the local object's, under the machine's own root */
    if (!(text = read_template( cfg, &len, &err ))) { error_text( err ); return 1; }
    if (!looks_like_template( text, len ))
    {
        free( text );
        error_text( ERROR_BAD_FORMAT );
        return 1;
    }
    GetSystemDirectoryW( store, MAX_PATH );
    wcscat( store, local_store );
    wcscpy( temp, store );
    wcscat( temp, L".new" );
    if ((err = make_directories( store )) || (!write_utf16( temp, text, len ) && (err = GetLastError())) ||
        (!MoveFileExW( temp, store, MOVEFILE_REPLACE_EXISTING ) && (err = GetLastError())))
    {
        DeleteFileW( temp );
        free( text );
        error_text( err );
        return 1;
    }
    free( text );
    if ((err = refresh()))
    {
        error_text( err );
        return 1;
    }
    if (!quiet) out( L"The task has completed successfully.\n" );
    return 0;
}

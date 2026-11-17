/*
 * wexpolicy: the list of applied Group Policy objects
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
 * docs/GROUP_POLICY.md, "Step 6: the applied list". wexpolicy record /machine|/user:SID LIST replaces the list
 * that userenv's GetAppliedGPOList reads. As on Windows it is kept per client-side extension, in the order the
 * objects were applied:
 *
 *   machine  HKLM\Software\Microsoft\Windows\CurrentVersion\Group Policy\History\{extension}\<n>
 *   user     HKLM\Software\Microsoft\Windows\CurrentVersion\Group Policy\<SID>\History\{extension}\<n>
 *     GPOName (the object's GUID), DisplayName, FileSysPath, DSPath, Link, Extensions (REG_SZ),
 *     Version, GPOLink, Options, lParam (REG_DWORD)
 *
 * Windows reads a user's list from the user's own hive; here it is the copy in the machine registry, which only
 * session 0 writes and which exists while the user is not logged on.
 *
 * LIST is a UTF-8 text file written by wex-gpupdate.sh, one object per line in the order applied, nine fields
 * separated by tabs: GUID (the local object's name is LocalGPO, "Local Group Policy" on Windows), version, link type (GPO_LINK), options, display name, file system path, DS path,
 * link, extensions (as gPCMachineExtensionNames: "[{extension}{tool}...][...]"; an object is listed for each
 * extension that starts a bracket). An empty file empties the list. The whole file is checked before the list
 * is replaced.
 */

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <windows.h>

#include "wexpolicy.h"

#define GP_KEY      L"Software\\Microsoft\\Windows\\CurrentVersion\\Group Policy"
#define MAX_OBJECTS 256
#define FIELDS      9
#define GUID_CHARS  38

struct object
{
    WCHAR *field[FIELDS];
};

static const WCHAR * const string_names[] = { L"GPOName", NULL, NULL, NULL, L"DisplayName", L"FileSysPath", L"DSPath",
                                              L"Link", L"Extensions" };
static const WCHAR * const number_names[] = { NULL, L"Version", L"GPOLink", L"Options" };

/* a well-formed GUID in braces at s (not necessarily the end of the string) */
static BOOL is_guid_at( const WCHAR *s )
{
    static const char shape[] = "{xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx}";
    int i;

    for (i = 0; shape[i]; i++)
    {
        if (shape[i] == 'x' ? !iswxdigit( s[i] ) : s[i] != shape[i]) return FALSE;
    }
    return TRUE;
}

static BOOL is_guid( const WCHAR *s )
{
    return is_guid_at( s ) && !s[GUID_CHARS];
}

/* the name of the local Group Policy object (docs/GROUP_POLICY.md, "Local policy"): it has no GUID; the local objects
 * of the users are LocalGPO- and the SID of their folder */
static BOOL is_object_name( const WCHAR *s )
{
    if (is_guid( s ) || !wcscmp( s, L"LocalGPO" )) return TRUE;
    if (wcsncmp( s, L"LocalGPO-S-1-", 13 ) || wcslen( s ) > 60) return FALSE;
    for (s += 8; *s; s++) if (*s != '-' && *s != 'S' && (*s < '0' || *s > '9')) return FALSE;
    return TRUE;
}

static BOOL is_number( const WCHAR *s, DWORD max )
{
    WCHAR *end;
    unsigned long n;

    if (!*s) return FALSE;
    n = wcstoul( s, &end, 10 );
    return !*end && n <= max;
}

/* the extensions of an object: "[{extension}{tool}...]" groups, or empty; each group starts with a GUID */
static BOOL is_extension_list( const WCHAR *s )
{
    while (*s)
    {
        if (*s++ != '[' || !is_guid_at( s )) return FALSE;
        while (*s && *s != ']')
        {
            if (!is_guid_at( s )) return FALSE;
            s += GUID_CHARS;
        }
        if (*s++ != ']') return FALSE;
    }
    return TRUE;
}

/* the file as lines of fields; the text is changed in place */
static DWORD parse_list( WCHAR *text, struct object *objects, unsigned int *count )
{
    WCHAR *line = text, *next;
    unsigned int n = 0, f;

    while (*line)
    {
        if ((next = wcschr( line, '\n' ))) *next++ = 0;
        else next = line + wcslen( line );
        if (*line && line[wcslen( line ) - 1] == '\r') line[wcslen( line ) - 1] = 0;
        if (*line)
        {
            if (n == MAX_OBJECTS) return ERROR_BAD_FORMAT;
            for (f = 0; f < FIELDS; f++)
            {
                objects[n].field[f] = line;
                if (!(line = wcschr( line, '\t' ))) break;
                *line++ = 0;
            }
            /* exactly nine fields, a GUID (or LocalGPO), three numbers (GPO_LINK is 0 to 4), no field longer than a
             * path */
            if (f != FIELDS - 1 || line) return ERROR_BAD_FORMAT;
            if (!is_object_name( objects[n].field[0] ) || !is_number( objects[n].field[1], 0xffffffff ) ||
                !is_number( objects[n].field[2], 4 ) || !is_number( objects[n].field[3], 0xffffffff ) ||
                !is_extension_list( objects[n].field[8] ))
                return ERROR_BAD_FORMAT;
            for (f = 4; f < FIELDS; f++) if (wcslen( objects[n].field[f] ) > 2048) return ERROR_BAD_FORMAT;
            n++;
        }
        line = next;
    }
    *count = n;
    return ERROR_SUCCESS;
}

static DWORD write_object( HKEY list, unsigned int index, const struct object *o )
{
    WCHAR name[16];
    DWORD err, f, number, zero = 0;
    HKEY key;

    swprintf( name, ARRAY_SIZE(name), L"%u", index );
    if ((err = RegCreateKeyExW( list, name, 0, NULL, 0, KEY_SET_VALUE | KEY_WOW64_64KEY, NULL, &key, NULL ))) return err;
    for (f = 0; f < FIELDS && !err; f++)
    {
        if (f < ARRAY_SIZE(number_names) && number_names[f])
        {
            number = wcstoul( o->field[f], NULL, 10 );
            err = RegSetValueExW( key, number_names[f], 0, REG_DWORD, (const BYTE *)&number, sizeof(number) );
        }
        else
            err = RegSetValueExW( key, string_names[f], 0, REG_SZ, (const BYTE *)o->field[f],
                                  (wcslen( o->field[f] ) + 1) * sizeof(WCHAR) );
    }
    if (!err) err = RegSetValueExW( key, L"lParam", 0, REG_DWORD, (const BYTE *)&zero, sizeof(zero) );
    RegCloseKey( key );
    return err;
}

/* every object that names the extension at ext (a GUID in the list of object o) goes below History\{extension},
 * once per extension: an extension already written for an earlier group of o is not written again */
static DWORD write_history( HKEY history, const struct object *objects, unsigned int count )
{
    WCHAR guid[GUID_CHARS + 1];
    unsigned int i, j, n;
    const WCHAR *s, *t;
    DWORD err = ERROR_SUCCESS;
    HKEY list;

    for (i = 0; i < count && !err; i++)
    {
        for (s = objects[i].field[8]; *s && !err; s++)
        {
            if (*s != '[') continue;
            lstrcpynW( guid, s + 1, ARRAY_SIZE(guid) );
            /* written already: an earlier object, or an earlier group of this one, named it */
            if (!RegOpenKeyExW( history, guid, 0, KEY_READ | KEY_WOW64_64KEY, &list ))
            {
                RegCloseKey( list );
                continue;
            }
            if ((err = RegCreateKeyExW( history, guid, 0, NULL, 0, KEY_ALL_ACCESS | KEY_WOW64_64KEY, NULL, &list, NULL )))
                break;
            /* all objects that name this extension, in order */
            for (j = i, n = 0; j < count && !err; j++)
            {
                for (t = objects[j].field[8]; *t; t++)
                    if (*t == '[' && !_wcsnicmp( t + 1, guid, GUID_CHARS )) break;
                if (*t) err = write_object( list, n++, &objects[j] );
            }
            RegCloseKey( list );
        }
    }
    return err;
}

/* wexpolicy record /machine|/user:SID LIST */
DWORD record_gpo_list( const WCHAR *sid, const WCHAR *path )
{
    static struct object objects[MAX_OBJECTS];
    WCHAR key_name[256], *text = NULL;
    unsigned int count = 0;
    HANDLE file;
    LARGE_INTEGER size;
    DWORD done = 0, err = ERROR_SUCCESS, chars;
    char *bytes = NULL;
    HKEY base, history;

    file = CreateFileW( path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL );
    if (file == INVALID_HANDLE_VALUE) return GetLastError();
    if (!GetFileSizeEx( file, &size )) err = GetLastError();
    else if (size.QuadPart > 1024 * 1024) err = ERROR_FILE_TOO_LARGE;
    else if (!(bytes = malloc( size.QuadPart + 1 ))) err = ERROR_NOT_ENOUGH_MEMORY;
    else if (!ReadFile( file, bytes, size.QuadPart, &done, NULL ) || done != size.QuadPart) err = ERROR_READ_FAULT;
    CloseHandle( file );
    if (!err)
    {
        chars = done ? MultiByteToWideChar( CP_UTF8, MB_ERR_INVALID_CHARS, bytes, done, NULL, 0 ) : 0;
        if (done && !chars) err = ERROR_BAD_FORMAT;
        else if (!(text = malloc( (chars + 1) * sizeof(WCHAR) ))) err = ERROR_NOT_ENOUGH_MEMORY;
        else
        {
            if (chars) MultiByteToWideChar( CP_UTF8, 0, bytes, done, text, chars );
            text[chars] = 0;
            /* no NUL inside the text */
            if (wcslen( text ) != chars) err = ERROR_BAD_FORMAT;
            else err = parse_list( text, objects, &count );
        }
    }
    free( bytes );
    if (err)
    {
        say( "wexpolicy: %ls: error %lu\n", path, err );
        free( text );
        return err;
    }

    swprintf( key_name, ARRAY_SIZE(key_name), sid ? L"%ls\\%ls" : L"%ls", GP_KEY, sid );
    if (!(err = RegCreateKeyExW( HKEY_LOCAL_MACHINE, key_name, 0, NULL, 0, KEY_ALL_ACCESS | KEY_WOW64_64KEY, NULL,
                                 &base, NULL )))
    {
        RegDeleteTreeW( base, L"History" );
        if (!(err = RegCreateKeyExW( base, L"History", 0, NULL, 0, KEY_ALL_ACCESS | KEY_WOW64_64KEY, NULL, &history, NULL )))
        {
            err = write_history( history, objects, count );
            RegCloseKey( history );
        }
        RegCloseKey( base );
    }
    free( text );
    say( "wexpolicy: recorded=%u status=%lu\n", err ? 0 : count, err );
    return err;
}

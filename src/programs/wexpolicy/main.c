/*
 * wexpolicy: writes registry policy (Registry.pol files) into the policy trees
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
 * docs/GROUP_POLICY.md, "the session 0 policy writer". On Windows the registry extension of the Group Policy
 * client, a SYSTEM service, writes the policy trees; here this program does, run in session 0 as LocalSystem
 * (the system tier holds HKLM and every logged-on user's hive there).
 *
 *   wexpolicy apply /machine FILE [FILE...]     HKEY_LOCAL_MACHINE
 *   wexpolicy apply /user:SID FILE [FILE...]    HKEY_USERS\SID, or kept for the user's next logon
 *   wexpolicy clear /machine | /user:SID        remove what the last apply wrote
 *   wexpolicy list  /machine | /user:SID        print what the last apply wrote
 *   wexpolicy                                   started by the service manager: the service "gpsvc"
 *
 * The files are applied in the order given (a later file wins), as the Group Policy client applies the GPOs in
 * its order. Only keys in the two policy trees are written, Software\Policies and
 * Software\Microsoft\Windows\CurrentVersion\Policies; entries for other keys are counted and skipped. What an
 * apply set is kept in an archive, the values with their data (as Windows keeps the last applied policy next to
 * the profile); the next apply first removes exactly those values, so a value that left the policy leaves the
 * registry, and values that were never policy (the local security options under ...\Policies\System) stay.
 *
 * The service. A user's hive file is the user's own, so session 0 empties the two policy trees when it loads
 * the hive at logon. The service sees the hive arrive and writes the user's archive into it again. A volatile
 * key in the policy tree marks a hive that has its policy; it goes away with the hive.
 *
 * Every file is read and checked completely before anything changes. Exit status: 0, or a Win32 error.
 */

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <windows.h>
#include <winsvc.h>
#include <sddl.h>

#include "preg.h"

#define MAX_FILES      64
#define MAX_FILE_SIZE  (32 * 1024 * 1024)

struct file
{
    BYTE  *data;
    DWORD  size;
};

/* a value the policy sets: what the archive holds */
struct item
{
    WCHAR *key, *value;
    DWORD  type, size;
    BYTE  *data;
};

#define SERVICE_NAME  L"gpsvc"
#define MARKER_KEY    L"Software\\Policies\\WineExplorer\\Policy Applied"

static struct item *items;
static size_t item_count, item_room;
static unsigned int count_set, count_deleted, count_skipped;
static SERVICE_STATUS_HANDLE service_handle;
static HANDLE stop_event;

static void say( const char *format, ... )
{
    va_list args;
    va_start( args, format );
    vfprintf( stdout, format, args );
    va_end( args );
    fflush( stdout );
}

/* only LocalSystem writes policy */
static BOOL is_local_system(void)
{
    BYTE buffer[sizeof(TOKEN_USER) + SECURITY_MAX_SID_SIZE];
    PSID system_sid = NULL;
    HANDLE token;
    DWORD len;
    BOOL ret = FALSE;

    if (!OpenProcessToken( GetCurrentProcess(), TOKEN_QUERY, &token )) return FALSE;
    if (GetTokenInformation( token, TokenUser, buffer, sizeof(buffer), &len ) &&
        ConvertStringSidToSidW( L"S-1-5-18", &system_sid ))
    {
        ret = EqualSid( ((TOKEN_USER *)buffer)->User.Sid, system_sid );
        LocalFree( system_sid );
    }
    CloseHandle( token );
    return ret;
}

static DWORD read_file( const WCHAR *path, struct file *file )
{
    HANDLE handle = CreateFileW( path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL );
    LARGE_INTEGER size;
    DWORD err = ERROR_SUCCESS, done;

    file->data = NULL;
    file->size = 0;
    if (handle == INVALID_HANDLE_VALUE) return GetLastError();
    if (!GetFileSizeEx( handle, &size )) err = GetLastError();
    else if (size.QuadPart > MAX_FILE_SIZE) err = ERROR_FILE_TOO_LARGE;
    else if (!(file->data = malloc( size.QuadPart ? size.QuadPart : 1 ))) err = ERROR_NOT_ENOUGH_MEMORY;
    else if (!ReadFile( handle, file->data, size.QuadPart, &done, NULL )) err = GetLastError();
    else if (done != size.QuadPart) err = ERROR_READ_FAULT;
    else file->size = done;
    CloseHandle( handle );
    if (err) { free( file->data ); file->data = NULL; }
    return err;
}

/* the whole file is well formed: nothing is applied from a file with a fault in it */
static DWORD check_file( const struct file *file )
{
    struct preg_entry e;
    size_t off;
    int ret;

    if (preg_begin( file->data, file->size, &off ) != PREG_OK) return ERROR_BAD_FORMAT;
    while ((ret = preg_next( file->data, file->size, &off, &e )) == PREG_OK) {}
    return ret == PREG_END ? ERROR_SUCCESS : ERROR_BAD_FORMAT;
}

/* a name of the file (UTF-16LE bytes, not aligned) as a string; the caller frees it */
static WCHAR *name_string( const BYTE *name, DWORD chars )
{
    WCHAR *str = malloc( ((size_t)chars + 1) * sizeof(WCHAR) );

    if (!str) return NULL;
    memcpy( str, name, (size_t)chars * sizeof(WCHAR) );
    str[chars] = 0;
    return str;
}

static void item_remove( size_t i )
{
    free( items[i].key );
    free( items[i].value );
    free( items[i].data );
    items[i] = items[--item_count];
}

static struct item *item_find( const WCHAR *key, const WCHAR *value )
{
    size_t i;

    for (i = 0; i < item_count; i++)
        if (!_wcsicmp( items[i].key, key ) && !_wcsicmp( items[i].value, value )) return &items[i];
    return NULL;
}

/* the policy sets the value: a later file replaces what an earlier one set */
static BOOL item_set( const WCHAR *key, const WCHAR *value, DWORD type, const BYTE *data, DWORD size )
{
    struct item *item = item_find( key, value ), new_item;

    if (!(new_item.data = malloc( size ? size : 1 ))) return FALSE;
    if (size) memcpy( new_item.data, data, size );
    new_item.type = type;
    new_item.size = size;
    if (item)
    {
        free( item->data );
        item->data = new_item.data;
        item->type = type;
        item->size = size;
        return TRUE;
    }
    if (item_count == item_room)
    {
        size_t room = item_room ? item_room * 2 : 64;
        struct item *list = realloc( items, room * sizeof(*list) );
        if (!list) { free( new_item.data ); return FALSE; }
        items = list;
        item_room = room;
    }
    new_item.key = _wcsdup( key );
    new_item.value = _wcsdup( value );
    if (!new_item.key || !new_item.value)
    {
        free( new_item.key );
        free( new_item.value );
        free( new_item.data );
        return FALSE;
    }
    items[item_count++] = new_item;
    return TRUE;
}

/* a later file deletes what an earlier one set: one value (value given), or all values of the key */
static void items_remove_values( const WCHAR *key, const WCHAR *value )
{
    size_t i = 0;

    while (i < item_count)
    {
        if (!_wcsicmp( items[i].key, key ) && (!value || !_wcsicmp( items[i].value, value ))) item_remove( i );
        else i++;
    }
}

/* the same for the subkey sub of key and everything below it */
static void items_remove_tree( const WCHAR *key, const WCHAR *sub )
{
    size_t i = 0, len = wcslen( key ) + 1 + wcslen( sub );
    WCHAR *path = malloc( (len + 1) * sizeof(WCHAR) );

    if (!path) return;
    wcscpy( path, key );
    wcscat( path, L"\\" );
    wcscat( path, sub );
    while (i < item_count)
    {
        if (!_wcsnicmp( items[i].key, path, len ) && (!items[i].key[len] || items[i].key[len] == '\\')) item_remove( i );
        else i++;
    }
    free( path );
}

/* the semicolon-separated names in an entry's data (**DeleteValues, **DeleteKeys); calls op for each */
static void for_each_name( const struct preg_entry *e, HKEY key, const WCHAR *key_name,
                           void (*op)( HKEY, const WCHAR *, const WCHAR * ) )
{
    DWORD chars = e->size / sizeof(WCHAR), start, i;
    WCHAR *list = name_string( e->data, chars );

    if (!list) return;
    for (start = i = 0; i <= chars; i++)
    {
        if (i < chars && list[i] && list[i] != ';') continue;
        list[i] = 0;
        if (i > start) op( key, key_name, list + start );
        start = i + 1;
    }
    free( list );
}

/* key is NULL when the user is not logged on: then only the list of what the policy sets changes */
static void delete_value( HKEY key, const WCHAR *key_name, const WCHAR *name )
{
    items_remove_values( key_name, name );
    if (key && !RegDeleteValueW( key, name )) count_deleted++;
}

static void delete_subkey( HKEY key, const WCHAR *key_name, const WCHAR *name )
{
    items_remove_tree( key_name, name );
    if (key && !RegDeleteTreeW( key, name ))
    {
        RegDeleteKeyExW( key, name, KEY_WOW64_64KEY, 0 );
        count_deleted++;
    }
}

static void delete_all_values( HKEY key )
{
    WCHAR name[16384];
    DWORD len;

    for (;;)
    {
        len = ARRAY_SIZE(name);
        if (RegEnumValueW( key, 0, name, &len, NULL, NULL, NULL, NULL )) break;
        if (RegDeleteValueW( key, name )) break;
        count_deleted++;
    }
}

/* One entry of a policy file: into the registry below root, and into the list of what the policy sets.
 * root is NULL when the user is not logged on; then the list alone changes. */
static DWORD apply_entry( HKEY root, const struct preg_entry *e )
{
    enum preg_action action;
    uint32_t name_off = 0;
    WCHAR *key_name, *value_name = NULL;
    HKEY key = NULL;
    DWORD err = ERROR_SUCCESS;

    if (!preg_key_in_policy_tree( e->key, e->key_len ))
    {
        count_skipped++;
        return ERROR_SUCCESS;
    }
    action = preg_classify( e, &name_off );
    if (action == PREG_SECURE_KEY) return ERROR_SUCCESS;
    if (!(key_name = name_string( e->key, e->key_len ))) return ERROR_NOT_ENOUGH_MEMORY;
    if (action == PREG_SET) name_off = 0;

    if (root && (action == PREG_SET || action == PREG_SOFT_SET))
        err = RegCreateKeyExW( root, key_name, 0, NULL, 0, KEY_ALL_ACCESS | KEY_WOW64_64KEY, NULL, &key, NULL );
    else if (root)
    {
        /* nothing to delete in a key that does not exist */
        err = RegOpenKeyExW( root, key_name, 0, KEY_ALL_ACCESS | KEY_WOW64_64KEY, &key );
        if (err == ERROR_FILE_NOT_FOUND) { err = ERROR_SUCCESS; key = NULL; }
    }
    if (err) { free( key_name ); return err; }

    switch (action)
    {
    case PREG_SOFT_SET:
    case PREG_SET:
        if (!(value_name = name_string( e->value + 2 * name_off, e->value_len - name_off )))
        {
            err = ERROR_NOT_ENOUGH_MEMORY;
            break;
        }
        if (action == PREG_SOFT_SET)   /* a value that exists is left alone */
        {
            if (item_find( key_name, value_name )) break;
            if (key && !RegQueryValueExW( key, value_name, NULL, NULL, NULL, NULL )) break;
        }
        if (key) err = RegSetValueExW( key, value_name, 0, e->type, e->data, e->size );
        if (!err)
        {
            count_set++;
            if (!item_set( key_name, value_name, e->type, e->data, e->size )) err = ERROR_NOT_ENOUGH_MEMORY;
        }
        break;
    case PREG_DELETE_VALUE:
        if (!(value_name = name_string( e->value + 2 * name_off, e->value_len - name_off ))) err = ERROR_NOT_ENOUGH_MEMORY;
        else delete_value( key, key_name, value_name );
        break;
    case PREG_DELETE_VALUES:
        for_each_name( e, key, key_name, delete_value );
        break;
    case PREG_DELETE_ALL_VALUES:
        items_remove_values( key_name, NULL );
        if (key) delete_all_values( key );
        break;
    case PREG_DELETE_KEYS:
        for_each_name( e, key, key_name, delete_subkey );
        break;
    default:
        break;
    }
    free( value_name );
    free( key_name );
    if (key) RegCloseKey( key );
    return err;
}

/* Remove a key that has become empty, and its parents while they are empty, but never one of the two policy
 * trees' own roots: other components expect those to exist. */
static void prune_empty( HKEY root, WCHAR *key_name )
{
    for (;;)
    {
        DWORD subkeys = 0, values = 0;
        WCHAR *slash;
        HKEY key;

        if (!_wcsicmp( key_name, L"Software\\Policies" ) ||
            !_wcsicmp( key_name, L"Software\\Microsoft\\Windows\\CurrentVersion\\Policies" )) return;
        if (RegOpenKeyExW( root, key_name, 0, KEY_READ | KEY_WOW64_64KEY, &key )) return;
        RegQueryInfoKeyW( key, NULL, NULL, NULL, &subkeys, NULL, NULL, &values, NULL, NULL, NULL, NULL );
        RegCloseKey( key );
        if (subkeys || values) return;
        if (RegDeleteKeyExW( root, key_name, KEY_WOW64_64KEY, 0 )) return;
        if (!(slash = wcsrchr( key_name, '\\' ))) return;
        *slash = 0;
    }
}

/* remove the values the last apply wrote (the archive names them) */
static DWORD undo_archive( HKEY root, const WCHAR *archive_path, BOOL print )
{
    struct file file;
    struct preg_entry e;
    size_t off;
    DWORD err = read_file( archive_path, &file );

    if (err == ERROR_FILE_NOT_FOUND || err == ERROR_PATH_NOT_FOUND) return ERROR_SUCCESS;
    if (err) return err;
    /* an archive that cannot be read is not followed: better stale values than deleting by a damaged list */
    if (check_file( &file ) || preg_begin( file.data, file.size, &off ) != PREG_OK)
    {
        free( file.data );
        return ERROR_BAD_FORMAT;
    }
    while (preg_next( file.data, file.size, &off, &e ) == PREG_OK)
    {
        WCHAR *key_name, *value_name;
        HKEY key;

        if (!preg_key_in_policy_tree( e.key, e.key_len )) continue;
        key_name = name_string( e.key, e.key_len );
        value_name = name_string( e.value, e.value_len );
        if (key_name && value_name)
        {
            if (print) say( "%ls\\%ls\n", key_name, value_name );
            else if (root && !RegOpenKeyExW( root, key_name, 0, KEY_SET_VALUE | KEY_WOW64_64KEY, &key ))
            {
                if (!RegDeleteValueW( key, value_name )) count_deleted++;
                RegCloseKey( key );
                prune_empty( root, key_name );
            }
        }
        free( key_name );
        free( value_name );
    }
    free( file.data );
    return ERROR_SUCCESS;
}

/* write the values the archive holds into the hive again (the service, at a user's logon) */
static DWORD restore_archive( HKEY root, const WCHAR *archive_path )
{
    struct file file;
    struct preg_entry e;
    size_t off;
    DWORD first_err = ERROR_SUCCESS, err = read_file( archive_path, &file );

    if (err == ERROR_FILE_NOT_FOUND || err == ERROR_PATH_NOT_FOUND) return ERROR_SUCCESS;
    if (err) return err;
    if (check_file( &file ) || preg_begin( file.data, file.size, &off ) != PREG_OK)
    {
        free( file.data );
        return ERROR_BAD_FORMAT;
    }
    while (preg_next( file.data, file.size, &off, &e ) == PREG_OK)
    {
        WCHAR *key_name, *value_name;
        HKEY key;

        if (!preg_key_in_policy_tree( e.key, e.key_len )) continue;
        key_name = name_string( e.key, e.key_len );
        value_name = name_string( e.value, e.value_len );
        if (!key_name || !value_name) err = ERROR_NOT_ENOUGH_MEMORY;
        else if (!(err = RegCreateKeyExW( root, key_name, 0, NULL, 0, KEY_SET_VALUE | KEY_WOW64_64KEY, NULL, &key, NULL )))
        {
            err = RegSetValueExW( key, value_name, 0, e.type, e.data, e.size );
            RegCloseKey( key );
        }
        if (err && !first_err) first_err = err;
        free( key_name );
        free( value_name );
    }
    free( file.data );
    return first_err;
}

/* the list of what the policy sets becomes the archive, which replaces the old one in one step */
static DWORD write_archive( const WCHAR *archive_path )
{
    size_t i, size = PREG_HEADER_SIZE, pos;
    BYTE *data;
    WCHAR temp[MAX_PATH];
    HANDLE handle;
    DWORD done, err = ERROR_SUCCESS;

    for (i = 0; i < item_count; i++)
        size += preg_entry_size( wcslen( items[i].key ), wcslen( items[i].value ), items[i].size );
    if (size > MAX_FILE_SIZE) return ERROR_FILE_TOO_LARGE;
    if (!(data = malloc( size ))) return ERROR_NOT_ENOUGH_MEMORY;
    pos = preg_put_header( data );
    for (i = 0; i < item_count; i++)
        pos += preg_put_entry( data + pos, (const BYTE *)items[i].key, wcslen( items[i].key ),
                               (const BYTE *)items[i].value, wcslen( items[i].value ),
                               items[i].type, items[i].data, items[i].size );

    if (swprintf( temp, ARRAY_SIZE(temp), L"%s.new", archive_path ) < 0) { free( data ); return ERROR_BUFFER_OVERFLOW; }
    handle = CreateFileW( temp, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL );
    if (handle == INVALID_HANDLE_VALUE) { free( data ); return GetLastError(); }
    if (!WriteFile( handle, data, pos, &done, NULL ) || done != pos) err = GetLastError() ? GetLastError() : ERROR_WRITE_FAULT;
    else if (!FlushFileBuffers( handle )) err = GetLastError();
    CloseHandle( handle );
    free( data );
    if (!err && !MoveFileExW( temp, archive_path, MOVEFILE_REPLACE_EXISTING )) err = GetLastError();
    if (err) DeleteFileW( temp );
    return err;
}

/* %SystemRoot%\system32\GroupPolicy\Archive\Machine.pol or User-<SID>.pol; the directory is made */
static DWORD archive_file( const WCHAR *sid, WCHAR *path, DWORD size )
{
    WCHAR dir[MAX_PATH];
    UINT len = GetSystemDirectoryW( dir, ARRAY_SIZE(dir) );

    if (!len || len > ARRAY_SIZE(dir) - 32) return ERROR_BUFFER_OVERFLOW;
    wcscat( dir, L"\\GroupPolicy" );
    if (!CreateDirectoryW( dir, NULL ) && GetLastError() != ERROR_ALREADY_EXISTS) return GetLastError();
    wcscat( dir, L"\\Archive" );
    if (!CreateDirectoryW( dir, NULL ) && GetLastError() != ERROR_ALREADY_EXISTS) return GetLastError();
    if (swprintf( path, size, sid ? L"%s\\User-%s.pol" : L"%s\\Machine.pol", dir, sid ) < 0) return ERROR_BUFFER_OVERFLOW;
    return ERROR_SUCCESS;
}

/* /machine, or /user:SID with a well-formed SID (it becomes a key name and a file name) */
static DWORD parse_target( const WCHAR *arg, const WCHAR **sid, HKEY *root )
{
    PSID parsed;

    *sid = NULL;
    if (!_wcsicmp( arg, L"/machine" ))
    {
        *root = HKEY_LOCAL_MACHINE;
        return ERROR_SUCCESS;
    }
    if (_wcsnicmp( arg, L"/user:", 6 ) || !arg[6]) return ERROR_INVALID_PARAMETER;
    if (!ConvertStringSidToSidW( arg + 6, &parsed )) return ERROR_INVALID_SID;
    LocalFree( parsed );
    if (wcslen( arg + 6 ) > 184) return ERROR_INVALID_SID;
    *sid = arg + 6;
    /* the hive of a user who is logged on; for a user who is not, the archive alone changes and the service
     * writes it into the hive at logon */
    if (RegOpenKeyExW( HKEY_USERS, *sid, 0, KEY_READ | KEY_WOW64_64KEY, root )) *root = NULL;
    return ERROR_SUCCESS;
}

static void set_service_state( DWORD state )
{
    SERVICE_STATUS status;

    memset( &status, 0, sizeof(status) );
    status.dwServiceType = SERVICE_WIN32_OWN_PROCESS;
    status.dwCurrentState = state;
    if (state == SERVICE_RUNNING) status.dwControlsAccepted = SERVICE_ACCEPT_STOP | SERVICE_ACCEPT_SHUTDOWN;
    SetServiceStatus( service_handle, &status );
}

static DWORD WINAPI service_handler( DWORD control, DWORD type, void *data, void *context )
{
    switch (control)
    {
    case SERVICE_CONTROL_STOP:
    case SERVICE_CONTROL_SHUTDOWN:
        set_service_state( SERVICE_STOP_PENDING );
        SetEvent( stop_event );
        return NO_ERROR;
    case SERVICE_CONTROL_INTERROGATE:
        return NO_ERROR;
    default:
        return ERROR_CALL_NOT_IMPLEMENTED;
    }
}

/* every account's hive below HKEY_USERS that has no marker gets its policy and the marker */
static void restore_users(void)
{
    WCHAR name[192], archive_path[MAX_PATH];
    DWORD index, len, created;
    HKEY root, marker;
    PSID sid;

    for (index = 0; ; index++)
    {
        len = ARRAY_SIZE(name);
        if (RegEnumKeyExW( HKEY_USERS, index, name, &len, NULL, NULL, NULL, NULL )) break;
        /* the accounts' hives: not .Default, the service accounts or the _Classes links */
        if (_wcsnicmp( name, L"S-1-5-21-", 9 ) || wcschr( name, '_' )) continue;
        if (!ConvertStringSidToSidW( name, &sid )) continue;
        LocalFree( sid );
        if (RegOpenKeyExW( HKEY_USERS, name, 0, KEY_READ | KEY_WOW64_64KEY, &root )) continue;
        if (!RegCreateKeyExW( root, MARKER_KEY, 0, NULL, REG_OPTION_VOLATILE, KEY_READ | KEY_WOW64_64KEY, NULL,
                              &marker, &created ))
        {
            RegCloseKey( marker );
            if (created == REG_CREATED_NEW_KEY && !archive_file( name, archive_path, ARRAY_SIZE(archive_path) ))
                restore_archive( root, archive_path );
        }
        RegCloseKey( root );
    }
}

static void WINAPI service_main( DWORD argc, WCHAR **argv )
{
    HANDLE handles[2];

    if (!(service_handle = RegisterServiceCtrlHandlerExW( SERVICE_NAME, service_handler, NULL ))) return;
    stop_event = CreateEventW( NULL, TRUE, FALSE, NULL );
    handles[0] = stop_event;
    handles[1] = CreateEventW( NULL, FALSE, FALSE, NULL );
    /* only the system writes policy: in a prefix without the system tier the service has nothing to do */
    if (!stop_event || !handles[1] || !is_local_system())
    {
        set_service_state( SERVICE_STOPPED );
        return;
    }
    set_service_state( SERVICE_RUNNING );
    for (;;)
    {
        /* the notification hurries the next look; the look every two seconds does not depend on it */
        RegNotifyChangeKeyValue( HKEY_USERS, FALSE, REG_NOTIFY_CHANGE_NAME, handles[1], TRUE );
        restore_users();
        if (WaitForMultipleObjects( 2, handles, FALSE, 2000 ) == WAIT_OBJECT_0) break;
    }
    set_service_state( SERVICE_STOPPED );
}

int __cdecl wmain( int argc, WCHAR **argv )
{
    static const SERVICE_TABLE_ENTRYW service_table[] = { { (WCHAR *)SERVICE_NAME, service_main }, { NULL, NULL } };
    struct file files[MAX_FILES];
    WCHAR archive_path[MAX_PATH];
    const WCHAR *sid;
    HKEY root = NULL;
    DWORD err;
    int i, count = argc - 3;
    BOOL apply, clear, list;

    if (argc == 1 && StartServiceCtrlDispatcherW( service_table )) return 0;
    if (argc < 3)
    {
        say( "usage: wexpolicy apply /machine|/user:SID FILE [FILE...]\n"
             "       wexpolicy clear /machine|/user:SID\n"
             "       wexpolicy list  /machine|/user:SID\n" );
        return ERROR_INVALID_PARAMETER;
    }
    apply = !_wcsicmp( argv[1], L"apply" );
    clear = !_wcsicmp( argv[1], L"clear" );
    list = !_wcsicmp( argv[1], L"list" );
    if ((!apply && !clear && !list) || (apply && (count < 1 || count > MAX_FILES)) || (!apply && count))
    {
        say( "wexpolicy: wrong arguments\n" );
        return ERROR_INVALID_PARAMETER;
    }
    if (!is_local_system())
    {
        say( "wexpolicy: only the system writes policy\n" );
        return ERROR_ACCESS_DENIED;
    }
    if ((err = parse_target( argv[2], &sid, &root )))
    {
        say( "wexpolicy: %ls: error %lu\n", argv[2], err );
        return err;
    }
    if ((err = archive_file( sid, archive_path, ARRAY_SIZE(archive_path) )))
    {
        say( "wexpolicy: the archive: error %lu\n", err );
        return err;
    }
    if (list)
    {
        err = undo_archive( root, archive_path, TRUE );
        if (err) say( "wexpolicy: the archive: error %lu\n", err );
        return err;
    }

    /* every file is read and checked before anything changes */
    memset( files, 0, sizeof(files) );
    for (i = 0; i < count; i++)
    {
        if (!(err = read_file( argv[3 + i], &files[i] ))) err = check_file( &files[i] );
        if (err)
        {
            say( "wexpolicy: %ls: error %lu\n", argv[3 + i], err );
            goto done;
        }
    }

    if ((err = undo_archive( root, archive_path, FALSE )))
    {
        say( "wexpolicy: the archive of the last apply cannot be read (error %lu); nothing changed\n", err );
        goto done;
    }
    for (i = 0; i < count && !err; i++)
    {
        struct preg_entry e;
        size_t off;

        preg_begin( files[i].data, files[i].size, &off );
        while (!err && preg_next( files[i].data, files[i].size, &off, &e ) == PREG_OK)
            err = apply_entry( root, &e );
    }
    /* the archive is written even after an error: it names what was written so far */
    {
        DWORD archive_err = write_archive( archive_path );
        if (!err) err = archive_err;
    }
    say( "wexpolicy: set=%u deleted=%u skipped=%u status=%lu%s\n", count_set, count_deleted, count_skipped, err,
         root ? "" : " (the user is not logged on: kept for the next logon)" );

done:
    for (i = 0; i < count; i++) free( files[i].data );
    while (item_count) item_remove( 0 );
    free( items );
    if (root && root != HKEY_LOCAL_MACHINE) RegCloseKey( root );
    return err;
}

/*
 * WineExplorer package registration command (wexappx.exe)
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
 * Per-user, per-prefix installation of an application package (.appx, .msix):
 *
 *   wexappx install --development PACKAGE
 *   wexappx uninstall PACKAGE_FULL_NAME
 *   wexappx list
 *
 * The package files go to %LOCALAPPDATA%\WineExplorer\WindowsApps\PackageFullName and the package is
 * published by a record in the catalog of the prefix (appx-catalog\PackageFullName.wexappx), which the
 * Wine server reads when an application is activated (wine/server/uwp_context.h).
 *
 * What this does not do, and says so: it verifies no signature (hence --development, which is required),
 * resolves no package dependency (a package that declares one is refused), installs nothing for other
 * users or the machine, and handles no bundle, encrypted package or licence.
 */

#define COBJMACROS
#include <windows.h>
#include <appmodel.h>
#include <objbase.h>
#include <initguid.h>
#include <xmllite.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <wchar.h>
#include <zlib.h>

#ifndef ARRAY_SIZE
#define ARRAY_SIZE(x) (sizeof(x) / sizeof((x)[0]))
#endif

#define MAX_ARCHIVE_SIZE   (256u * 1024 * 1024)
#define MAX_ENTRIES        4096
#define MAX_FILE_SIZE      (128u * 1024 * 1024)
#define MAX_TOTAL_SIZE     (512u * 1024 * 1024)
#define MAX_RATIO          200
#define MAX_MANIFEST_SIZE  (1024 * 1024)
#define MAX_REL_PATH       512
#define MAX_RECORD_CHARS   32000
#define MAX_ITEMS          1024

struct entry
{
    WCHAR  name[MAX_REL_PATH + 1];   /* relative path with backslashes */
    DWORD  method, crc, comp_size, size, offset;
};

struct item   /* an application or an activatable class of the manifest */
{
    WCHAR id[256];
    WCHAR path[MAX_REL_PATH + 1];
};

struct manifest
{
    WCHAR        name[PACKAGE_NAME_MAX_LENGTH + 1];
    WCHAR        publisher[PACKAGE_PUBLISHER_MAX_LENGTH + 1];
    WCHAR        resource[PACKAGE_RESOURCEID_MAX_LENGTH + 1];
    WCHAR        version[64];
    WCHAR        arch[32];
    struct item *apps, *classes;
    unsigned int app_count, class_count;
};

static const BYTE *archive;
static DWORD archive_size;
static struct entry *entries;
static unsigned int entry_count;

static int fail( const WCHAR *format, ... )
{
    va_list args;

    fputws( L"wexappx: ", stderr );
    va_start( args, format );
    vfwprintf( stderr, format, args );
    va_end( args );
    fputws( L"\n", stderr );
    return 1;
}

static WORD get_word( const BYTE *p ) { return p[0] | (p[1] << 8); }
static DWORD get_dword( const BYTE *p ) { return p[0] | (p[1] << 8) | (p[2] << 16) | ((DWORD)p[3] << 24); }

/* the compatibility gate: exactly the DWORD 1 */
static BOOL uwp_compatibility_enabled(void)
{
    DWORD value = 0, type = REG_NONE, size = sizeof(value);

    if (RegGetValueW( HKEY_CURRENT_USER, L"Software\\WineExplorer\\Shell", L"UwpCompatibility",
                      RRF_RT_REG_DWORD, &type, &value, &size ))
        return FALSE;
    return type == REG_DWORD && size == sizeof(value) && value == 1;
}

/* A relative path as the catalog accepts it: printable ASCII components separated by single backslashes,
 * none of them "." or "..", none ending in a dot or a space, no drive, stream or wildcard syntax. */
static BOOL valid_relative_path( const WCHAR *path )
{
    size_t i, start = 0, len = wcslen( path );

    if (!len || len > MAX_REL_PATH) return FALSE;
    for (i = 0; i <= len; i++)
    {
        WCHAR ch = i < len ? path[i] : '\\';

        if (ch == '\\')
        {
            size_t n = i - start;

            if (!n) return FALSE;
            if (path[start] == '.' && (n == 1 || (n == 2 && path[start + 1] == '.'))) return FALSE;
            if (path[i - 1] == '.' || path[i - 1] == ' ') return FALSE;
            start = i + 1;
        }
        else if (ch < 0x20 || ch > 0x7e || ch == '/' || ch == ':' || ch == '"' || ch == '<' || ch == '>' ||
                 ch == '|' || ch == '?' || ch == '*')
            return FALSE;
    }
    return TRUE;
}

static BOOL valid_id( const WCHAR *id, BOOL class_id )
{
    size_t i, len = wcslen( id );

    if (!len || len > (class_id ? 255 : 64)) return FALSE;
    for (i = 0; i < len; i++)
    {
        WCHAR ch = id[i];
        if ((ch >= '0' && ch <= '9') || (ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z') || ch == '.') continue;
        if (class_id && (ch == '_' || ch == '-')) continue;
        return FALSE;
    }
    return TRUE;
}

/* an archive member name: UTF-8 restricted to ASCII, percent-encoded where the packaging rules require it */
static BOOL convert_entry_name( const BYTE *raw, unsigned int len, WCHAR *name )
{
    unsigned int i, pos = 0;

    for (i = 0; i < len; i++)
    {
        WCHAR ch = raw[i];

        if (ch == '%')
        {
            unsigned int j, value = 0;

            if (i + 2 >= len) return FALSE;
            for (j = 1; j <= 2; j++)
            {
                BYTE digit = raw[i + j];
                if (digit >= '0' && digit <= '9') value = value * 16 + digit - '0';
                else if (digit >= 'a' && digit <= 'f') value = value * 16 + digit - 'a' + 10;
                else if (digit >= 'A' && digit <= 'F') value = value * 16 + digit - 'A' + 10;
                else return FALSE;
            }
            ch = value;
            i += 2;
            if (ch == '/' || ch == '\\') return FALSE;   /* an encoded separator is not a separator */
        }
        else if (ch == '/') ch = '\\';
        else if (ch == '\\') return FALSE;
        if (pos >= MAX_REL_PATH) return FALSE;
        name[pos++] = ch;
    }
    name[pos] = 0;
    return valid_relative_path( name );
}

/* read the central directory; every member is checked before anything is written */
static int read_directory(void)
{
    const BYTE *end, *dir;
    ULONGLONG total = 0;
    DWORD pos, dir_size, dir_offset;
    unsigned int i, j, count;

    if (archive_size < 22) return fail( L"not a package archive" );
    /* the end record, searched backwards over a possible comment */
    for (pos = archive_size - 22; ; pos--)
    {
        if (get_dword( archive + pos ) == 0x06054b50 && pos + 22 + get_word( archive + pos + 20 ) == archive_size) break;
        if (!pos || archive_size - pos > 22 + 65535) return fail( L"not a package archive" );
    }
    end = archive + pos;
    if (get_word( end + 4 ) || get_word( end + 6 ) || get_word( end + 8 ) != get_word( end + 10 ))
        return fail( L"multi-volume archives are not supported" );
    count = get_word( end + 10 );
    dir_size = get_dword( end + 12 );
    dir_offset = get_dword( end + 16 );
    if (count == 0xffff || dir_size == 0xffffffff || dir_offset == 0xffffffff)
        return fail( L"64-bit archives are not supported" );
    if (!count || count > MAX_ENTRIES) return fail( L"the archive has %u members (at most %u are accepted)", count, MAX_ENTRIES );
    if (dir_offset > pos || dir_size > pos - dir_offset) return fail( L"damaged archive directory" );
    if (!(entries = calloc( count, sizeof(*entries) ))) return fail( L"out of memory" );

    dir = archive + dir_offset;
    for (i = 0; i < count; i++)
    {
        struct entry *entry = &entries[entry_count];
        unsigned int name_len, extra_len, comment_len, flags;

        if (dir_size < 46 || get_dword( dir ) != 0x02014b50) return fail( L"damaged archive directory" );
        flags = get_word( dir + 8 );
        entry->method = get_word( dir + 10 );
        entry->crc = get_dword( dir + 16 );
        entry->comp_size = get_dword( dir + 20 );
        entry->size = get_dword( dir + 24 );
        name_len = get_word( dir + 28 );
        extra_len = get_word( dir + 30 );
        comment_len = get_word( dir + 32 );
        entry->offset = get_dword( dir + 42 );
        if (dir_size - 46 < name_len + extra_len + comment_len) return fail( L"damaged archive directory" );

        if (flags & 0x2041) return fail( L"encrypted or patched archive members are not supported" );
        if (entry->comp_size == 0xffffffff || entry->size == 0xffffffff || entry->offset == 0xffffffff)
            return fail( L"64-bit archives are not supported" );
        if (name_len && dir[46 + name_len - 1] == '/')
        {
            if (entry->size) return fail( L"damaged archive directory" );   /* a directory entry */
        }
        else
        {
            if (!convert_entry_name( dir + 46, name_len, entry->name ))
                return fail( L"unsupported member name in the archive (member %u)", i );
            if (entry->method != 0 && entry->method != 8)
                return fail( L"unsupported compression of %s", entry->name );
            if (entry->method == 0 && entry->comp_size != entry->size) return fail( L"damaged member %s", entry->name );
            if (entry->size > MAX_FILE_SIZE) return fail( L"%s is too large", entry->name );
            if (entry->size / MAX_RATIO > entry->comp_size) return fail( L"%s expands too much", entry->name );
            if ((total += entry->size) > MAX_TOTAL_SIZE) return fail( L"the package is too large" );
            if (entry->offset > dir_offset || dir_offset - entry->offset < 30) return fail( L"damaged member %s", entry->name );
            /* names are compared without case: one file per name, and no file where a directory is needed */
            for (j = 0; j < entry_count; j++)
            {
                size_t a = wcslen( entries[j].name ), b = wcslen( entry->name );

                if (!wcsicmp( entries[j].name, entry->name )) return fail( L"%s is in the archive twice", entry->name );
                if (a < b && entry->name[a] == '\\' && !wcsnicmp( entries[j].name, entry->name, a ))
                    return fail( L"%s is both a file and a directory", entries[j].name );
                if (b < a && entries[j].name[b] == '\\' && !wcsnicmp( entries[j].name, entry->name, b ))
                    return fail( L"%s is both a file and a directory", entry->name );
            }
            entry_count++;
        }
        dir += 46 + name_len + extra_len + comment_len;
        dir_size -= 46 + name_len + extra_len + comment_len;
    }
    return 0;
}

static struct entry *find_entry( const WCHAR *name )
{
    unsigned int i;

    for (i = 0; i < entry_count; i++) if (!wcsicmp( entries[i].name, name )) return &entries[i];
    return NULL;
}

/* Wine's zlib has no default allocator */
static void *zlib_alloc( void *opaque, uInt items, uInt size )
{
    return calloc( items, size );
}

static void zlib_free( void *opaque, void *ptr )
{
    free( ptr );
}

/* the data of a member, checked against its recorded size and checksum; NULL with a message */
static BYTE *read_entry( const struct entry *entry )
{
    const BYTE *local = archive + entry->offset, *data;
    DWORD data_offset;
    BYTE *out;

    if (get_dword( local ) != 0x04034b50 || (get_word( local + 6 ) & 0x2041) || get_word( local + 8 ) != entry->method)
        goto damaged;
    data_offset = entry->offset + 30 + get_word( local + 26 ) + get_word( local + 28 );
    if (data_offset > archive_size || archive_size - data_offset < entry->comp_size) goto damaged;
    data = archive + data_offset;

    if (!(out = malloc( entry->size + 1 )))
    {
        fail( L"out of memory" );
        return NULL;
    }
    if (entry->method == 0) memcpy( out, data, entry->size );
    else
    {
        z_stream stream;
        int ret;

        memset( &stream, 0, sizeof(stream) );
        stream.zalloc = zlib_alloc;
        stream.zfree = zlib_free;
        if (inflateInit2( &stream, -MAX_WBITS ) != Z_OK) goto failed;
        stream.next_in = (Bytef *)data;
        stream.avail_in = entry->comp_size;
        stream.next_out = out;
        stream.avail_out = entry->size + 1;   /* one more: a longer stream must not pass */
        ret = inflate( &stream, Z_FINISH );
        inflateEnd( &stream );
        if (ret != Z_STREAM_END || stream.total_out != entry->size || stream.avail_in) goto failed;
    }
    if (crc32( 0, out, entry->size ) != entry->crc) goto failed;
    return out;

failed:
    free( out );
damaged:
    fail( L"damaged member %s", entry->name );
    return NULL;
}

static struct item *add_item( struct item **items, unsigned int *count )
{
    if (*count >= MAX_ITEMS) return NULL;
    if (!*items && !(*items = calloc( MAX_ITEMS, sizeof(**items) ))) return NULL;
    return &(*items)[(*count)++];
}

static BOOL get_attribute( IXmlReader *reader, const WCHAR *name, WCHAR *value, unsigned int size )
{
    const WCHAR *str;
    UINT len;
    BOOL ret = FALSE;

    value[0] = 0;
    if (IXmlReader_MoveToAttributeByName( reader, name, NULL ) == S_OK &&
        IXmlReader_GetValue( reader, &str, &len ) == S_OK && len < size)
    {
        memcpy( value, str, (len + 1) * sizeof(WCHAR) );
        ret = TRUE;
    }
    IXmlReader_MoveToElement( reader );
    return ret;
}

static int parse_manifest( const BYTE *data, DWORD size, struct manifest *manifest )
{
    IXmlReader *reader = NULL;
    IStream *stream = NULL;
    XmlNodeType type;
    WCHAR server_path[MAX_REL_PATH + 1] = L"", category[128], value[256];
    BOOL in_server = FALSE, in_path = FALSE, have_identity = FALSE;
    unsigned int first_class = 0, i;
    const WCHAR *name, *text;
    HGLOBAL global;
    HRESULT hr;
    int ret = 1;

    if (!(global = GlobalAlloc( GMEM_MOVEABLE, size ))) return fail( L"out of memory" );
    memcpy( GlobalLock( global ), data, size );
    GlobalUnlock( global );
    if (FAILED(CreateStreamOnHGlobal( global, TRUE, &stream )))
    {
        GlobalFree( global );
        return fail( L"out of memory" );
    }
    if (FAILED(CreateXmlReader( &IID_IXmlReader, (void **)&reader, NULL )) ||
        FAILED(IXmlReader_SetProperty( reader, XmlReaderProperty_DtdProcessing, DtdProcessing_Prohibit )) ||
        FAILED(IXmlReader_SetProperty( reader, XmlReaderProperty_MaxElementDepth, 32 )) ||
        FAILED(IXmlReader_SetInput( reader, (IUnknown *)stream )))
    {
        fail( L"cannot read the manifest" );
        goto done;
    }

    while ((hr = IXmlReader_Read( reader, &type )) == S_OK)
    {
        if (type == XmlNodeType_Element)
        {
            BOOL empty = IXmlReader_IsEmptyElement( reader );

            if (FAILED(IXmlReader_GetLocalName( reader, &name, NULL ))) break;
            if (!wcscmp( name, L"Identity" ))
            {
                if (have_identity ||
                    !get_attribute( reader, L"Name", manifest->name, ARRAY_SIZE(manifest->name) ) ||
                    !get_attribute( reader, L"Publisher", manifest->publisher, ARRAY_SIZE(manifest->publisher) ) ||
                    !get_attribute( reader, L"Version", manifest->version, ARRAY_SIZE(manifest->version) ))
                {
                    fail( L"the manifest has no usable package identity" );
                    goto done;
                }
                if (!get_attribute( reader, L"ProcessorArchitecture", manifest->arch, ARRAY_SIZE(manifest->arch) ))
                    wcscpy( manifest->arch, L"neutral" );
                get_attribute( reader, L"ResourceId", manifest->resource, ARRAY_SIZE(manifest->resource) );
                have_identity = TRUE;
            }
            else if (!wcscmp( name, L"PackageDependency" ) || !wcscmp( name, L"MainPackageDependency" ))
            {
                get_attribute( reader, L"Name", value, ARRAY_SIZE(value) );
                fail( L"the package depends on the package %s; package dependencies are not supported", value );
                goto done;
            }
            else if (!wcscmp( name, L"Application" ))
            {
                struct item *app = add_item( &manifest->apps, &manifest->app_count );

                if (!app || !get_attribute( reader, L"Id", app->id, ARRAY_SIZE(app->id) ) ||
                    !valid_id( app->id, FALSE ) ||
                    !get_attribute( reader, L"Executable", app->path, ARRAY_SIZE(app->path) ) ||
                    !valid_relative_path( app->path ))
                {
                    fail( L"unsupported application declaration in the manifest" );
                    goto done;
                }
            }
            else if (!wcscmp( name, L"Extension" ))
            {
                get_attribute( reader, L"Category", category, ARRAY_SIZE(category) );
            }
            else if (!wcscmp( name, L"InProcessServer" ) && !wcscmp( category, L"windows.activatableClass.inProcessServer" ))
            {
                in_server = !empty;
                server_path[0] = 0;
                first_class = manifest->class_count;
            }
            else if (in_server && !wcscmp( name, L"Path" )) in_path = !empty;
            else if (in_server && !wcscmp( name, L"ActivatableClass" ))
            {
                struct item *class = add_item( &manifest->classes, &manifest->class_count );

                if (!class || !get_attribute( reader, L"ActivatableClassId", class->id, ARRAY_SIZE(class->id) ) ||
                    !valid_id( class->id, TRUE ))
                {
                    fail( L"unsupported activatable class declaration in the manifest" );
                    goto done;
                }
            }
        }
        else if (type == XmlNodeType_Text && in_path)
        {
            UINT len;

            if (FAILED(IXmlReader_GetValue( reader, &text, &len )) || len > MAX_REL_PATH || server_path[0])
            {
                fail( L"unsupported in-process server path in the manifest" );
                goto done;
            }
            wcscpy( server_path, text );
        }
        else if (type == XmlNodeType_EndElement)
        {
            if (FAILED(IXmlReader_GetLocalName( reader, &name, NULL ))) break;
            if (!wcscmp( name, L"Path" )) in_path = FALSE;
            else if (!wcscmp( name, L"Extension" )) category[0] = 0;
            else if (in_server && !wcscmp( name, L"InProcessServer" ))
            {
                if (!valid_relative_path( server_path ))
                {
                    fail( L"unsupported in-process server path in the manifest" );
                    goto done;
                }
                for (i = first_class; i < manifest->class_count; i++) wcscpy( manifest->classes[i].path, server_path );
                in_server = FALSE;
            }
        }
    }
    if (hr != S_FALSE) fail( L"the manifest is not well-formed (%#lx)", hr );
    else if (!have_identity) fail( L"the manifest has no package identity" );
    else if (!manifest->app_count) fail( L"the manifest declares no application" );
    else ret = 0;

done:
    if (reader) IXmlReader_Release( reader );
    IStream_Release( stream );
    return ret;
}

/* the package full name of the manifest's identity; the publisher id is derived by kernelbase */
static int get_full_name( const struct manifest *manifest, WCHAR full_name[PACKAGE_FULL_NAME_MAX_LENGTH + 1] )
{
    unsigned int parts[4];
    UINT32 len = PACKAGE_FULL_NAME_MAX_LENGTH + 1;
    PACKAGE_ID id;
    WCHAR tail;
    LONG err;

    memset( &id, 0, sizeof(id) );
    if (swscanf( manifest->version, L"%u.%u.%u.%u%c", &parts[0], &parts[1], &parts[2], &parts[3], &tail ) != 4 ||
        parts[0] > 0xffff || parts[1] > 0xffff || parts[2] > 0xffff || parts[3] > 0xffff)
        return fail( L"unsupported package version %s", manifest->version );
    id.version.Major = parts[0];
    id.version.Minor = parts[1];
    id.version.Build = parts[2];
    id.version.Revision = parts[3];
    if (!wcscmp( manifest->arch, L"x86" )) id.processorArchitecture = PROCESSOR_ARCHITECTURE_INTEL;
    else if (!wcscmp( manifest->arch, L"x64" )) id.processorArchitecture = PROCESSOR_ARCHITECTURE_AMD64;
    else if (!wcscmp( manifest->arch, L"neutral" )) id.processorArchitecture = PROCESSOR_ARCHITECTURE_NEUTRAL;
    else return fail( L"unsupported processor architecture %s", manifest->arch );
    id.name = (WCHAR *)manifest->name;
    id.publisher = (WCHAR *)manifest->publisher;
    id.resourceId = manifest->resource[0] ? (WCHAR *)manifest->resource : NULL;
    if ((err = PackageFullNameFromId( &id, &len, full_name )))
        return fail( L"the package identity is not valid (error %ld)", err );
    return 0;
}

/* the catalog directory of the prefix, with a trailing backslash */
static BOOL get_catalog_dir( WCHAR *path, unsigned int size )
{
    const WCHAR *config = _wgetenv( L"WINECONFIGDIR" );

    if (!config || wcsncmp( config, L"\\??\\", 4 ) || wcslen( config ) + 16 > size) return FALSE;
    swprintf( path, size, L"\\\\?\\%s\\appx-catalog\\", config + 4 );
    return TRUE;
}

/* the per-user package directory, with a trailing backslash; it has to be on drive C: */
static BOOL get_store_dir( WCHAR *path, unsigned int size )
{
    DWORD len = GetEnvironmentVariableW( L"LOCALAPPDATA", path, size );

    if (!len || len + 32 > size) return FALSE;
    if ((path[0] != 'C' && path[0] != 'c') || path[1] != ':' || path[2] != '\\') return FALSE;
    wcscat( path, L"\\WineExplorer" );
    CreateDirectoryW( path, NULL );
    wcscat( path, L"\\WindowsApps" );
    CreateDirectoryW( path, NULL );
    wcscat( path, L"\\" );
    return GetFileAttributesW( path ) != INVALID_FILE_ATTRIBUTES;
}

/* remove a directory tree we created; links are removed, not followed */
static void remove_tree( const WCHAR *dir )
{
    WCHAR path[MAX_PATH * 2];
    WIN32_FIND_DATAW data;
    HANDLE find;

    if (swprintf( path, ARRAY_SIZE(path), L"%s\\*", dir ) < 0) return;
    if ((find = FindFirstFileW( path, &data )) != INVALID_HANDLE_VALUE)
    {
        do
        {
            if (!wcscmp( data.cFileName, L"." ) || !wcscmp( data.cFileName, L".." )) continue;
            if (swprintf( path, ARRAY_SIZE(path), L"%s\\%s", dir, data.cFileName ) < 0) continue;
            if (data.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT)
            {
                if (!RemoveDirectoryW( path )) DeleteFileW( path );
            }
            else if (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) remove_tree( path );
            else
            {
                SetFileAttributesW( path, FILE_ATTRIBUTE_NORMAL );
                DeleteFileW( path );
            }
        } while (FindNextFileW( find, &data ));
        FindClose( find );
    }
    RemoveDirectoryW( dir );
}

/* write one member below the staging directory, creating its directories */
static int extract_entry( const WCHAR *staging, const struct entry *entry )
{
    WCHAR path[MAX_PATH * 2], *p;
    DWORD written;
    HANDLE file;
    BYTE *data;
    int ret = 0;

    if (swprintf( path, ARRAY_SIZE(path), L"%s\\%s", staging, entry->name ) < 0 || wcslen( path ) >= MAX_PATH)
        return fail( L"the path of %s is too long", entry->name );
    for (p = path + wcslen( staging ) + 1; (p = wcschr( p, '\\' )); p++)
    {
        *p = 0;
        if (!CreateDirectoryW( path, NULL ) && GetLastError() != ERROR_ALREADY_EXISTS)
            return fail( L"cannot create %s (error %lu)", path, GetLastError() );
        *p = '\\';
    }
    if (!(data = read_entry( entry ))) return 1;
    file = CreateFileW( path, GENERIC_WRITE, 0, NULL, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, NULL );
    if (file == INVALID_HANDLE_VALUE) ret = fail( L"cannot create %s (error %lu)", path, GetLastError() );
    else
    {
        if (!WriteFile( file, data, entry->size, &written, NULL ) || written != entry->size)
            ret = fail( L"cannot write %s (error %lu)", path, GetLastError() );
        CloseHandle( file );
    }
    free( data );
    return ret;
}

static BOOL append_record( WCHAR *record, const WCHAR *format, ... )
{
    size_t len = wcslen( record );
    va_list args;
    int ret;

    va_start( args, format );
    ret = vswprintf( record + len, MAX_RECORD_CHARS - len, format, args );
    va_end( args );
    return ret >= 0;
}

static int install( const WCHAR *package )
{
    WCHAR full_name[PACKAGE_FULL_NAME_MAX_LENGTH + 1], store[MAX_PATH], catalog[MAX_PATH];
    WCHAR staging[MAX_PATH], target[MAX_PATH], record_path[MAX_PATH], temp_path[MAX_PATH], *record = NULL;
    struct manifest manifest;
    struct entry *entry;
    LARGE_INTEGER size;
    HANDLE file, mapping;
    BYTE *data;
    DWORD written;
    unsigned int i;
    int ret = 1;

    memset( &manifest, 0, sizeof(manifest) );
    file = CreateFileW( package, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL );
    if (file == INVALID_HANDLE_VALUE) return fail( L"cannot open %s (error %lu)", package, GetLastError() );
    if (!GetFileSizeEx( file, &size ) || !size.QuadPart || size.QuadPart > MAX_ARCHIVE_SIZE)
    {
        CloseHandle( file );
        return fail( L"%s is empty or too large", package );
    }
    archive_size = size.QuadPart;
    mapping = CreateFileMappingW( file, NULL, PAGE_READONLY, 0, 0, NULL );
    CloseHandle( file );
    if (!mapping || !(archive = MapViewOfFile( mapping, FILE_MAP_READ, 0, 0, 0 )))
        return fail( L"cannot read %s (error %lu)", package, GetLastError() );

    /* everything is checked before anything is written */
    if (read_directory()) return 1;
    if (!(entry = find_entry( L"AppxManifest.xml" ))) return fail( L"the archive has no AppxManifest.xml" );
    if (entry->size > MAX_MANIFEST_SIZE) return fail( L"the manifest is too large" );
    if (!(data = read_entry( entry ))) return 1;
    if (parse_manifest( data, entry->size, &manifest )) return 1;
    free( data );
    if (get_full_name( &manifest, full_name )) return 1;

    if (!get_store_dir( store, ARRAY_SIZE(store) )) return fail( L"no per-user package directory on drive C:" );
    if (!get_catalog_dir( catalog, ARRAY_SIZE(catalog) )) return fail( L"the prefix directory is not known" );
    CreateDirectoryW( catalog, NULL );
    if (swprintf( target, ARRAY_SIZE(target), L"%s%s", store, full_name ) < 0 ||
        swprintf( staging, ARRAY_SIZE(staging), L"%s.staging-%lu-%lu", store, GetCurrentProcessId(), GetTickCount() ) < 0 ||
        swprintf( record_path, ARRAY_SIZE(record_path), L"%s%s.wexappx", catalog, full_name ) < 0 ||
        swprintf( temp_path, ARRAY_SIZE(temp_path), L"%s.%lu.tmp", catalog, GetCurrentProcessId() ) < 0)
        return fail( L"path too long" );
    if (GetFileAttributesW( target ) != INVALID_FILE_ATTRIBUTES || GetFileAttributesW( record_path ) != INVALID_FILE_ATTRIBUTES)
        return fail( L"%s is already installed", full_name );

    /* the record, with the names as they are in the archive */
    if (!(record = calloc( MAX_RECORD_CHARS, sizeof(WCHAR) ))) return fail( L"out of memory" );
    if (!append_record( record, L"WEXAPPX 1\nroot\t%s\n", target )) goto too_long;
    for (i = 0; i < manifest.app_count; i++)
    {
        if (!(entry = find_entry( manifest.apps[i].path )))
        {
            fail( L"the executable %s of application %s is not in the package", manifest.apps[i].path, manifest.apps[i].id );
            goto done;
        }
        if (!append_record( record, L"app\t%s\t%s\n", manifest.apps[i].id, entry->name )) goto too_long;
    }
    for (i = 0; i < manifest.class_count; i++)
    {
        /* a declared server that is not in the package stays declared: activation reports the missing module */
        entry = find_entry( manifest.classes[i].path );
        if (!append_record( record, L"class\t%s\t%s\n", manifest.classes[i].id,
                            entry ? entry->name : manifest.classes[i].path )) goto too_long;
    }
    for (i = 0; record[i]; i++)
    {
        if (record[i] != '\t' && record[i] != '\n' && (record[i] < 0x20 || record[i] > 0x7e))
        {
            fail( L"the package directory or a declared name is not plain ASCII; that is not supported yet" );
            goto done;
        }
    }

    /* stage beside the destination, publish the directory, then the record */
    if (!CreateDirectoryW( staging, NULL ))
    {
        fail( L"cannot create %s (error %lu)", staging, GetLastError() );
        goto done;
    }
    for (i = 0; i < entry_count; i++)
    {
        if (extract_entry( staging, &entries[i] ))
        {
            remove_tree( staging );
            goto done;
        }
    }
    if (!MoveFileW( staging, target ))
    {
        fail( L"cannot publish %s (error %lu)", target, GetLastError() );
        remove_tree( staging );
        goto done;
    }
    file = CreateFileW( temp_path, GENERIC_WRITE, 0, NULL, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, NULL );
    if (file == INVALID_HANDLE_VALUE ||
        !WriteFile( file, record, wcslen( record ) * sizeof(WCHAR), &written, NULL ) ||
        written != wcslen( record ) * sizeof(WCHAR))
    {
        fail( L"cannot write the catalog record (error %lu)", GetLastError() );
        if (file != INVALID_HANDLE_VALUE) CloseHandle( file );
        DeleteFileW( temp_path );
        remove_tree( target );
        goto done;
    }
    CloseHandle( file );
    if (!MoveFileW( temp_path, record_path ))
    {
        fail( L"cannot publish the catalog record (error %lu)", GetLastError() );
        DeleteFileW( temp_path );
        remove_tree( target );
        goto done;
    }

    wprintf( L"Installed %s\n", full_name );
    wprintf( L"The signature of the package was not verified.\n" );
    for (i = 0; i < manifest.app_count; i++)
    {
        WCHAR family[PACKAGE_FAMILY_NAME_MAX_LENGTH + 1];
        const WCHAR *first = wcschr( full_name, '_' ), *last = wcsrchr( full_name, '_' );

        swprintf( family, ARRAY_SIZE(family), L"%.*s%s", (int)(first - full_name), full_name, last );
        wprintf( L"Application: %s!%s\n", family, manifest.apps[i].id );
    }
    ret = 0;
    goto done;

too_long:
    fail( L"the package declares too much" );
done:
    free( record );
    return ret;
}

static BOOL valid_full_name( const WCHAR *name )
{
    unsigned int i, underscores = 0;

    if (!name[0] || name[0] == '.' || wcslen( name ) > PACKAGE_FULL_NAME_MAX_LENGTH) return FALSE;
    for (i = 0; name[i]; i++)
    {
        WCHAR ch = name[i];
        if (ch == '_') underscores++;
        else if (!((ch >= '0' && ch <= '9') || (ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z') || ch == '.' || ch == '-'))
            return FALSE;
    }
    return underscores == 4;
}

static int uninstall( const WCHAR *full_name )
{
    WCHAR store[MAX_PATH], catalog[MAX_PATH], target[MAX_PATH], record_path[MAX_PATH];

    if (!valid_full_name( full_name )) return fail( L"%s is not a package full name", full_name );
    if (!get_store_dir( store, ARRAY_SIZE(store) ) || !get_catalog_dir( catalog, ARRAY_SIZE(catalog) ))
        return fail( L"the package directories are not known" );
    if (swprintf( target, ARRAY_SIZE(target), L"%s%s", store, full_name ) < 0 ||
        swprintf( record_path, ARRAY_SIZE(record_path), L"%s%s.wexappx", catalog, full_name ) < 0)
        return fail( L"path too long" );
    /* the record goes first: the package stops being visible; then only the directory of that name */
    if (!DeleteFileW( record_path ))
    {
        if (GetLastError() == ERROR_FILE_NOT_FOUND || GetLastError() == ERROR_PATH_NOT_FOUND)
            return fail( L"%s is not installed", full_name );
        return fail( L"cannot remove the catalog record (error %lu)", GetLastError() );
    }
    remove_tree( target );
    if (GetFileAttributesW( target ) != INVALID_FILE_ATTRIBUTES)
        return fail( L"%s is no longer registered, but files remain in %s", full_name, target );
    wprintf( L"Removed %s\n", full_name );
    return 0;
}

static int list(void)
{
    WCHAR catalog[MAX_PATH], pattern[MAX_PATH];
    WIN32_FIND_DATAW data;
    HANDLE find;

    if (!get_catalog_dir( catalog, ARRAY_SIZE(catalog) )) return fail( L"the prefix directory is not known" );
    swprintf( pattern, ARRAY_SIZE(pattern), L"%s*.wexappx", catalog );
    if ((find = FindFirstFileW( pattern, &data )) == INVALID_HANDLE_VALUE) return 0;
    do
    {
        size_t len = wcslen( data.cFileName );

        if (len <= 8 || (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) continue;
        data.cFileName[len - 8] = 0;
        if (valid_full_name( data.cFileName )) wprintf( L"%s\n", data.cFileName );
    } while (FindNextFileW( find, &data ));
    FindClose( find );
    return 0;
}

static int usage(void)
{
    fputws( L"Usage:\n"
            L"  wexappx install --development PACKAGE     install an .appx or .msix package for this user\n"
            L"  wexappx uninstall PACKAGE_FULL_NAME       remove an installed package\n"
            L"  wexappx list                              list the installed packages\n"
            L"\n"
            L"--development is required: the signature of the package is not verified.\n", stderr );
    return 2;
}

int __cdecl wmain( int argc, WCHAR **argv )
{
    int ret;

    if (argc < 2) return usage();
    if (!uwp_compatibility_enabled())
        return fail( L"UWP compatibility is not enabled (HKCU\\Software\\WineExplorer\\Shell, UwpCompatibility = 1)" );

    if (!wcscmp( argv[1], L"install" ))
    {
        if (argc != 4 || wcscmp( argv[2], L"--development" )) return usage();
        if (FAILED(CoInitializeEx( NULL, COINIT_MULTITHREADED ))) return fail( L"COM is not available" );
        ret = install( argv[3] );
        CoUninitialize();
        return ret;
    }
    if (!wcscmp( argv[1], L"uninstall" ) && argc == 3) return uninstall( argv[2] );
    if (!wcscmp( argv[1], L"list" ) && argc == 2) return list();
    return usage();
}

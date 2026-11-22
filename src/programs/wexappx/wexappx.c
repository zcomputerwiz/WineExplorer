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
 *   wexappx install [--development] PACKAGE
 *   wexappx uninstall PACKAGE_FULL_NAME
 *   wexappx list
 *
 * The package files go to %LOCALAPPDATA%\WineExplorer\WindowsApps\PackageFullName and the package is
 * published by a record in the catalog of the prefix (appx-catalog\PackageFullName.wexappx), which the
 * Wine server reads when an application is activated (wine/server/uwp_context.h).
 *
 * The signature is checked through WinVerifyTrust (appxsip.dll answers for packages, as AppxSip.dll does on Windows):
 * a package that is not signed, is signed by a publisher this system does not trust or has a signature that is not valid
 * is refused unless --development is given. What this does not do, and says so: it resolves no dependency that is not
 * installed (a package that declares one is refused), asks no consent for a package of this user (the flag is the
 * consent), and handles no bundle, encrypted package or licence.
 *
 * With --all-users the package goes to C:\Program Files\WindowsApps and the machine catalog
 * (%ProgramData%\WineExplorer\appx-catalog), and every user can start it. That needs the full token of an
 * administrator, as provisioning a package does on Windows ("requires elevation", before the file is read): a limited
 * administrator's command is run again through the elevation broker, whose consent window describes the package, and
 * the elevated copy installs only the file the broker froze.
 */

#define COBJMACROS
#include <windows.h>
#include <appmodel.h>
#include <objbase.h>
#include <initguid.h>
#include <xmllite.h>
#include <wincrypt.h>
#include <wintrust.h>
#include <softpub.h>
#include <mssip.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <wchar.h>
#include <zlib.h>

/*
 * The package family functions are looked up at run time and their flags are defined here: a Wine without them
 * (before the package information APIs, WineExplorer UWP U1.2) still builds and installs packages that have no
 * dependencies; a package that has one is then refused with a message.
 */
#ifndef PACKAGE_FILTER_HEAD
#define PACKAGE_FILTER_HEAD        0x00000010
#endif
#ifndef PACKAGE_INFORMATION_BASIC
#define PACKAGE_INFORMATION_BASIC  0x00000000
#endif

typedef LONG (WINAPI *PackageFamilyNameFromId_t)( const PACKAGE_ID *, UINT32 *, WCHAR * );
typedef LONG (WINAPI *FindPackagesByPackageFamily_t)( const WCHAR *, UINT32, UINT32 *, WCHAR **, UINT32 *, WCHAR *, UINT32 * );

static PackageFamilyNameFromId_t pPackageFamilyNameFromId;
static FindPackagesByPackageFamily_t pFindPackagesByPackageFamily;

static BOOL load_package_family_functions(void)
{
    HMODULE module = GetModuleHandleW( L"kernelbase.dll" );

    if (!module) return FALSE;
    pPackageFamilyNameFromId = (PackageFamilyNameFromId_t)GetProcAddress( module, "PackageFamilyNameFromId" );
    pFindPackagesByPackageFamily = (FindPackagesByPackageFamily_t)GetProcAddress( module, "FindPackagesByPackageFamily" );
    return pPackageFamilyNameFromId && pFindPackagesByPackageFamily;
}

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
    WCHAR entry[257];   /* the EntryPoint of an application, if it has one */
};

#define MAX_DEPENDENCIES 16

struct dependency   /* a PackageDependency of the manifest */
{
    WCHAR name[PACKAGE_NAME_MAX_LENGTH + 1];
    WCHAR min_version[64];
    WCHAR publisher[PACKAGE_PUBLISHER_MAX_LENGTH + 1];
};

struct manifest
{
    struct dependency *deps;       /* MAX_DEPENDENCIES, allocated when the manifest has any */
    unsigned int dep_count;
    BOOL         framework;        /* Properties/Framework: a package other packages depend on */
    WCHAR        name[PACKAGE_NAME_MAX_LENGTH + 1];
    WCHAR        publisher[PACKAGE_PUBLISHER_MAX_LENGTH + 1];
    WCHAR        display[256];     /* Properties/DisplayName, as written (it can be an ms-resource: reference) */
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
static ULONGLONG get_qword( const BYTE *p ) { return get_dword( p ) | ((ULONGLONG)get_dword( p + 4 ) << 32); }

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

/* the EntryPoint of an application: a class name, or the full-trust marker */
static BOOL valid_entry_point( const WCHAR *entry )
{
    size_t i, len = wcslen( entry );

    if (!len || len > 256) return FALSE;
    for (i = 0; i < len; i++)
    {
        WCHAR ch = entry[i];
        if (!((ch >= '0' && ch <= '9') || (ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z') || ch == '.' || ch == '_'))
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
    /* with zip64 end records the plain ones may hold 0xffff in every field, the disk numbers too (checked below) */
    if (!(pos >= 20 && get_dword( end - 20 ) == 0x07064b50) &&
        (get_word( end + 4 ) || get_word( end + 6 ) || get_word( end + 8 ) != get_word( end + 10 )))
        return fail( L"multi-volume archives are not supported" );
    count = get_word( end + 10 );
    dir_size = get_dword( end + 12 );
    dir_offset = get_dword( end + 16 );
    if (pos >= 20 && get_dword( end - 20 ) == 0x07064b50)
    {
        /* the zip64 end records that a signing tool writes even for a small package: 0xffff and 0xffffffff above, the
         * values here; only the ones that fit this reader are accepted */
        ULONGLONG record = get_qword( end - 12 );

        if (pos < 76 || record > pos - 76 || get_dword( archive + record ) != 0x06064b50)
            return fail( L"damaged archive directory" );
        if (get_dword( archive + record + 16 ) || get_dword( archive + record + 20 ))
            return fail( L"multi-volume archives are not supported" );
        if (get_qword( archive + record + 24 ) != get_qword( archive + record + 32 ) ||
            get_qword( archive + record + 32 ) > MAX_ENTRIES || get_qword( archive + record + 40 ) >= 0xffffffff ||
            get_qword( archive + record + 48 ) >= 0xffffffff)
            return fail( L"64-bit archives are not supported" );
        count = (unsigned int)get_qword( archive + record + 32 );
        dir_size = (DWORD)get_qword( archive + record + 40 );
        dir_offset = (DWORD)get_qword( archive + record + 48 );
    }
    else if (count == 0xffff || dir_size == 0xffffffff || dir_offset == 0xffffffff)
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
        {
            /* a signing tool's package: the values are in the zip64 extra field (sizes, then the offset), in the order of
             * the fields that hold 0xffffffff; only values that fit this reader are accepted */
            ULONGLONG size = entry->size, csize = entry->comp_size, offset = entry->offset;
            const BYTE *extra = dir + 46 + name_len, *extra_end = extra + extra_len;
            BOOL found = FALSE;

            while (!found && extra_end - extra >= 4)
            {
                unsigned int id = get_word( extra ), len = get_word( extra + 2 );
                const BYTE *field = extra + 4;

                if (len > extra_end - field) break;
                if (id == 1)
                {
                    if (entry->size == 0xffffffff) { if (len < 8) break; size = get_qword( field ); field += 8; len -= 8; }
                    if (entry->comp_size == 0xffffffff) { if (len < 8) break; csize = get_qword( field ); field += 8; len -= 8; }
                    if (entry->offset == 0xffffffff) { if (len < 8) break; offset = get_qword( field ); }
                    found = TRUE;
                }
                else extra += 4 + len;
            }
            if (!found || size >= 0xffffffff || csize >= 0xffffffff || offset >= 0xffffffff)
                return fail( L"64-bit archives are not supported" );
            entry->size = (DWORD)size;
            entry->comp_size = (DWORD)csize;
            entry->offset = (DWORD)offset;
        }
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
    BOOL in_server = FALSE, in_path = FALSE, have_identity = FALSE, in_framework = FALSE, in_display = FALSE;
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
            else if (!wcscmp( name, L"MainPackageDependency" ))
            {
                get_attribute( reader, L"Name", value, ARRAY_SIZE(value) );
                fail( L"the package is an add-on of the package %s; that is not supported", value );
                goto done;
            }
            else if (!wcscmp( name, L"PackageDependency" ))
            {
                struct dependency *dep;

                if (!manifest->deps) manifest->deps = calloc( MAX_DEPENDENCIES, sizeof(*manifest->deps) );
                if (!manifest->deps || manifest->dep_count >= MAX_DEPENDENCIES)
                {
                    fail( L"the manifest has too many package dependencies" );
                    goto done;
                }
                dep = &manifest->deps[manifest->dep_count];
                if (!get_attribute( reader, L"Name", dep->name, ARRAY_SIZE(dep->name) ) ||
                    !get_attribute( reader, L"Publisher", dep->publisher, ARRAY_SIZE(dep->publisher) ))
                {
                    fail( L"unsupported package dependency in the manifest" );
                    goto done;
                }
                if (!get_attribute( reader, L"MinVersion", dep->min_version, ARRAY_SIZE(dep->min_version) ))
                    wcscpy( dep->min_version, L"0.0.0.0" );
                manifest->dep_count++;
            }
            else if (!wcscmp( name, L"Framework" )) in_framework = !empty;
            else if (!wcscmp( name, L"DisplayName" )) in_display = !empty;
            else if (!wcscmp( name, L"Application" ))
            {
                struct item *app = add_item( &manifest->apps, &manifest->app_count );

                if (!app || !get_attribute( reader, L"Id", app->id, ARRAY_SIZE(app->id) ) ||
                    !valid_id( app->id, FALSE ) ||
                    !get_attribute( reader, L"Executable", app->path, ARRAY_SIZE(app->path) ) ||
                    !valid_relative_path( app->path ) ||
                    (get_attribute( reader, L"EntryPoint", app->entry, ARRAY_SIZE(app->entry) ) && !valid_entry_point( app->entry )))
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
        else if (type == XmlNodeType_Text && in_framework)
        {
            UINT len;

            if (FAILED(IXmlReader_GetValue( reader, &text, &len )) || len > 8) text = L"";
            manifest->framework = !wcscmp( text, L"true" ) || !wcscmp( text, L"1" );
        }
        else if (type == XmlNodeType_Text && in_display)
        {
            UINT len;

            /* shown to the person, so only plain printable text of a sensible length; anything else stays empty */
            if (SUCCEEDED(IXmlReader_GetValue( reader, &text, &len )) && len < ARRAY_SIZE(manifest->display))
                wcscpy( manifest->display, text );
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
            else if (!wcscmp( name, L"Framework" )) in_framework = FALSE;
            else if (!wcscmp( name, L"DisplayName" )) in_display = FALSE;
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
    else if (!manifest->app_count && !manifest->framework) fail( L"the manifest declares no application" );
    else if (manifest->framework && manifest->dep_count) fail( L"a framework package with dependencies is not supported" );
    else ret = 0;

done:
    if (reader) IXmlReader_Release( reader );
    IStream_Release( stream );
    return ret;
}

/* "a.b.c.d" as the 64-bit number of PACKAGE_VERSION; ~0 when it is not a version */
static unsigned __int64 version_number( const WCHAR *text )
{
    unsigned __int64 value = 0;
    int part;

    for (part = 0; part < 4; part++)
    {
        WCHAR *end;
        unsigned long number = wcstoul( text, &end, 10 );

        if (end == text || number > 0xffff || (part < 3 && *end != '.') || (part == 3 && *end)) return ~(unsigned __int64)0;
        value = (value << 16) | number;
        text = end + 1;
    }
    return value;
}

/* The installed package a dependency names: the same name and publisher, the architecture of the dependent package
 * (or neutral), a version that is not below the minimum, the highest such version. */
static BOOL resolve_dependency( const struct manifest *manifest, const struct dependency *dep, WCHAR *best, unsigned int size )
{
    PACKAGE_ID id = {0};
    WCHAR family[PACKAGE_FAMILY_NAME_MAX_LENGTH + 1], *text = NULL, **names = NULL;
    UINT32 len = ARRAY_SIZE(family), count = 0, chars = 0, i, arch;
    unsigned __int64 minimum = version_number( dep->min_version ), best_version = 0;
    BOOL found = FALSE;

    if (minimum == ~(unsigned __int64)0) return FALSE;
    id.name = (WCHAR *)dep->name;
    id.publisher = (WCHAR *)dep->publisher;
    if (!load_package_family_functions()) return FALSE;
    if (pPackageFamilyNameFromId( &id, &len, family )) return FALSE;
    arch = !wcscmp( manifest->arch, L"x64" ) ? PROCESSOR_ARCHITECTURE_AMD64 :
           !wcscmp( manifest->arch, L"x86" ) ? PROCESSOR_ARCHITECTURE_INTEL : PROCESSOR_ARCHITECTURE_NEUTRAL;

    pFindPackagesByPackageFamily( family, PACKAGE_FILTER_HEAD | PACKAGE_INFORMATION_BASIC, &count, NULL, &chars, NULL, NULL );
    if (!count) return FALSE;
    names = calloc( count, sizeof(*names) );
    text = malloc( chars * sizeof(WCHAR) );
    if (names && text && !pFindPackagesByPackageFamily( family, PACKAGE_FILTER_HEAD | PACKAGE_INFORMATION_BASIC, &count,
                                                       names, &chars, text, NULL ))
    {
        for (i = 0; i < count; i++)
        {
            BYTE buffer[sizeof(PACKAGE_ID) + sizeof(WCHAR) * (PACKAGE_FULL_NAME_MAX_LENGTH + 32)];
            UINT32 buffer_len = sizeof(buffer);
            PACKAGE_ID *candidate = (PACKAGE_ID *)buffer;

            if (PackageIdFromFullName( names[i], PACKAGE_INFORMATION_BASIC, &buffer_len, buffer )) continue;
            if (candidate->processorArchitecture != arch && candidate->processorArchitecture != PROCESSOR_ARCHITECTURE_NEUTRAL &&
                arch != PROCESSOR_ARCHITECTURE_NEUTRAL)
                continue;
            if (candidate->version.Version < minimum) continue;
            if (!found || candidate->version.Version > best_version)
            {
                if (wcslen( names[i] ) >= size) continue;
                wcscpy( best, names[i] );
                best_version = candidate->version.Version;
                found = TRUE;
            }
        }
    }
    free( names );
    free( text );
    return found;
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

/* the catalog directory, with a trailing backslash: the prefix's (for this user), or with machine the machine's,
 * %ProgramData%\WineExplorer\appx-catalog, which every user's server reads (only an administrator writes there) */
static BOOL get_catalog_dir( WCHAR *path, unsigned int size, BOOL machine )
{
    if (machine)
    {
        DWORD len = GetEnvironmentVariableW( L"ProgramData", path, size );

        if (!len || len + 40 > size || len < 3 || path[1] != ':' || path[2] != '\\') wcscpy( path, L"C:\\ProgramData" );
        wcscat( path, L"\\WineExplorer" );
        CreateDirectoryW( path, NULL );
        wcscat( path, L"\\appx-catalog" );
        CreateDirectoryW( path, NULL );
        wcscat( path, L"\\" );
        return GetFileAttributesW( path ) != INVALID_FILE_ATTRIBUTES;
    }
    else
    {
        const WCHAR *config = _wgetenv( L"WINECONFIGDIR" );

        if (!config || wcsncmp( config, L"\\??\\", 4 ) || wcslen( config ) + 16 > size) return FALSE;
        swprintf( path, size, L"\\\\?\\%s\\appx-catalog\\", config + 4 );
        return TRUE;
    }
}

/* the package directory, with a trailing backslash; it has to be on drive C: (the user's own, or with machine
 * C:\Program Files\WindowsApps, where Windows keeps the packages of every user) */
static BOOL get_store_dir( WCHAR *path, unsigned int size, BOOL machine )
{
    DWORD len = GetEnvironmentVariableW( machine ? L"ProgramFiles" : L"LOCALAPPDATA", path, size );

    if (!len || len + 32 > size) return FALSE;
    if ((path[0] != 'C' && path[0] != 'c') || path[1] != ':' || path[2] != '\\') return FALSE;
    if (!machine)
    {
        wcscat( path, L"\\WineExplorer" );
        CreateDirectoryW( path, NULL );
    }
    wcscat( path, L"\\WindowsApps" );
    CreateDirectoryW( path, NULL );
    wcscat( path, L"\\" );
    return GetFileAttributesW( path ) != INVALID_FILE_ATTRIBUTES;
}

/*
 * A package for every user needs the machine's directories, which only an administrator with the full token writes
 * (Windows: provisioning a package, "requires elevation", before it reads the file). A limited administrator asks the
 * elevation broker to run this same command line once: the consent window describes the package (consent.exe reads the
 * file the broker froze, as it does for msiexec), and the elevated copy installs only that file (WEXELEVATE_DOC_*).
 * Without the broker (a Wine without it, or no consent possible) nothing is installed.
 */
typedef DWORD (WINAPI *WexElevateRun_t)( const WCHAR *, const WCHAR *, const WCHAR *, const WCHAR *, DWORD, DWORD, DWORD,
                                         HANDLE *, DWORD * );

/* Is elevation enforced (HKLM\Software\Wine\Elevation\Helper names the broker's helper)? A Wine without one has no
 * boundary between a limited and a full token: nothing to ask for, and the command goes on as before (msi does the same). */
static BOOL elevation_enforced(void)
{
    WCHAR value[MAX_PATH];
    DWORD type, size = sizeof(value) - sizeof(WCHAR);
    HKEY key;
    LSTATUS status;

    status = RegOpenKeyExW( HKEY_LOCAL_MACHINE, L"Software\\Wine\\Elevation", 0, KEY_READ | KEY_WOW64_64KEY, &key );
    if (status == ERROR_FILE_NOT_FOUND) return FALSE;
    if (status) return TRUE;                       /* unreadable: nothing is assumed */
    value[0] = 0;
    status = RegQueryValueExW( key, L"Helper", NULL, &type, (BYTE *)value, &size );
    RegCloseKey( key );
    if (status == ERROR_FILE_NOT_FOUND) return FALSE;
    return !(status == ERROR_SUCCESS && (type == REG_SZ || type == REG_EXPAND_SZ) && !(size % sizeof(WCHAR)) &&
             (!size || !value[0]));
}

/* the full token, or no enforcement at all: nothing to ask the broker for */
static BOOL token_is_elevated(void)
{
    TOKEN_ELEVATION elevation;
    DWORD size;
    HANDLE token;
    BOOL ret = FALSE;

    if (!elevation_enforced()) return TRUE;
    if (!OpenProcessToken( GetCurrentProcess(), TOKEN_QUERY, &token )) return FALSE;
    if (GetTokenInformation( token, TokenElevation, &elevation, sizeof(elevation), &size )) ret = elevation.TokenIsElevated;
    CloseHandle( token );
    return ret;
}

/* ERROR_SUCCESS with *code the exit code of the elevated copy, or the reason it did not run */
static DWORD run_elevated( DWORD *code )
{
    WCHAR image[MAX_PATH], cwd[MAX_PATH], *env, *p;
    DWORD env_len = 0, err, pid;
    WexElevateRun_t run;
    HMODULE wexhost;
    HANDLE process = NULL;

    if (!GetModuleFileNameW( NULL, image, ARRAY_SIZE(image) ) || !GetCurrentDirectoryW( ARRAY_SIZE(cwd), cwd ))
        return ERROR_INVALID_PARAMETER;
    if (!(wexhost = LoadLibraryExW( L"wexhost.dll", NULL, LOAD_LIBRARY_SEARCH_SYSTEM32 )) ||
        !(run = (WexElevateRun_t)GetProcAddress( wexhost, "WexElevateRun" )))
    {
        if (wexhost) FreeLibrary( wexhost );
        return ERROR_NOT_SUPPORTED;
    }
    if ((env = GetEnvironmentStringsW()))
    {
        for (p = env; *p; p += wcslen( p ) + 1) ;
        env_len = p + 1 - env;
    }
    err = run( image, GetCommandLineW(), cwd, env, env_len, SW_HIDE, 1 /* WEXELEVATE_WANT_HANDLE */, &process, &pid );
    FreeLibrary( wexhost );
    if (env) FreeEnvironmentStringsW( env );
    if (err) return err;
    WaitForSingleObject( process, INFINITE );
    if (!GetExitCodeProcess( process, code )) err = GetLastError();
    CloseHandle( process );
    return err;
}

/* The elevated copy that the broker started for a frozen file installs that file only: the path in WEXELEVATE_DOC_PATH
 * has to be this package, and the bytes held here (the ones that are checked and installed) have to have the SHA-256
 * the broker computed when the user consented. TRUE when the broker froze nothing (a direct run by an elevated token). */
static BOOL package_is_frozen_file( const WCHAR *package, const BYTE *bytes, ULONGLONG length )
{
    WCHAR want[MAX_PATH], full_want[MAX_PATH], full[MAX_PATH], expected[65], hex[65];
    BYTE digest[32];
    HCRYPTPROV prov;
    HCRYPTHASH hash;
    DWORD len, size = sizeof(digest), i;
    ULONGLONG done;
    BOOL ok = FALSE;

    len = GetEnvironmentVariableW( L"WEXELEVATE_DOC_PATH", want, ARRAY_SIZE(want) );
    if (!len && !GetEnvironmentVariableW( L"WEXELEVATE_DOC_SHA256", expected, ARRAY_SIZE(expected) )) return TRUE;
    if (!len || len >= ARRAY_SIZE(want) || GetEnvironmentVariableW( L"WEXELEVATE_DOC_SHA256", expected, ARRAY_SIZE(expected) ) != 64)
        return FALSE;
    len = GetFullPathNameW( want, ARRAY_SIZE(full_want), full_want, NULL );
    if (!len || len >= ARRAY_SIZE(full_want)) return FALSE;
    len = GetFullPathNameW( package, ARRAY_SIZE(full), full, NULL );
    if (!len || len >= ARRAY_SIZE(full) || wcsicmp( full, full_want )) return FALSE;

    if (!CryptAcquireContextW( &prov, NULL, MS_ENH_RSA_AES_PROV_W, PROV_RSA_AES, CRYPT_VERIFYCONTEXT )) return FALSE;
    if (CryptCreateHash( prov, CALG_SHA_256, 0, 0, &hash ))
    {
        for (done = 0, ok = TRUE; done < length && ok; done += 0x100000)
            ok = CryptHashData( hash, bytes + done, (DWORD)(length - done < 0x100000 ? length - done : 0x100000), 0 );
        if (ok && CryptGetHashParam( hash, HP_HASHVAL, digest, &size, 0 ) && size == sizeof(digest))
        {
            for (i = 0; i < size; i++) swprintf( hex + 2 * i, 3, L"%02x", digest[i] );
            ok = !wcsicmp( hex, expected );
        }
        else ok = FALSE;
        CryptDestroyHash( hash );
    }
    CryptReleaseContext( prov, 0 );
    return ok;
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
/*
 * The block map (AppxBlockMap.xml): for every file of the package its size and the SHA-256 of each block of 64 KiB of its
 * content. Windows compares the files with it while it stages a package; so does this. The block map itself is covered by
 * the signature (the AXBM digest), so a file that agrees with it is the file that was signed, whatever the archive held
 * when the signature was checked (KNOWN_BUGS B68: the bytes behind the mapping can change in between).
 */
#define BLOCK_SIZE        65536
#define MAX_BLOCKMAP_SIZE (8u * 1024 * 1024)
#define MAX_BLOCKS        (1u << 17)

struct block_file
{
    WCHAR name[MAX_REL_PATH + 1];
    ULONGLONG size;
    BYTE (*hashes)[32];
    unsigned int blocks;
};

static struct block_file *block_files;
static unsigned int block_file_count, block_total;
static BOOL have_blockmap;

static HCRYPTPROV hash_prov;

static BOOL sha256( const BYTE *data, DWORD len, BYTE out[32] )
{
    HCRYPTHASH hash;
    DWORD size = 32;
    BOOL ok = FALSE;

    if (!hash_prov && !CryptAcquireContextW( &hash_prov, NULL, MS_ENH_RSA_AES_PROV_W, PROV_RSA_AES, CRYPT_VERIFYCONTEXT ))
        return FALSE;
    if (CryptCreateHash( hash_prov, CALG_SHA_256, 0, 0, &hash ))
    {
        ok = CryptHashData( hash, data, len, 0 ) && CryptGetHashParam( hash, HP_HASHVAL, out, &size, 0 ) && size == 32;
        CryptDestroyHash( hash );
    }
    return ok;
}

/* the name of a block map file: backslashes as they are, percent escapes decoded; FALSE for a name that is no path */
static BOOL block_name( const WCHAR *text, WCHAR *name )
{
    unsigned int i, pos = 0;

    for (i = 0; text[i]; i++)
    {
        WCHAR ch = text[i];

        if (ch == '%')
        {
            unsigned int j, value = 0;

            for (j = 1; j <= 2; j++)
            {
                WCHAR digit = text[i + j];
                if (digit >= '0' && digit <= '9') value = value * 16 + digit - '0';
                else if (digit >= 'a' && digit <= 'f') value = value * 16 + digit - 'a' + 10;
                else if (digit >= 'A' && digit <= 'F') value = value * 16 + digit - 'A' + 10;
                else return FALSE;
            }
            ch = value;
            i += 2;
            if (ch == '/' || ch == '\\' || !ch) return FALSE;
        }
        else if (ch == '/') return FALSE;
        if (pos >= MAX_REL_PATH) return FALSE;
        name[pos++] = ch;
    }
    name[pos] = 0;
    return pos != 0;
}

static struct block_file *find_block_file( const WCHAR *name )
{
    unsigned int i;

    for (i = 0; i < block_file_count; i++) if (!wcsicmp( block_files[i].name, name )) return &block_files[i];
    return NULL;
}

/* members that are not in the block map, by their rules (the signature, and the files that describe the package) */
static BOOL is_unlisted_member( const WCHAR *name )
{
    return !wcsicmp( name, L"[Content_Types].xml" ) || !wcsicmp( name, L"AppxBlockMap.xml" ) ||
           !wcsicmp( name, L"AppxSignature.p7x" ) || !wcsicmp( name, L"AppxMetadata\\CodeIntegrity.cat" );
}

/* reads the block map of the package (when it has one); a malformed one is an error */
static int load_blockmap(void)
{
    struct entry *entry = find_entry( L"AppxBlockMap.xml" );
    IXmlReader *reader = NULL;
    IStream *stream = NULL;
    HGLOBAL global;
    BYTE *data = NULL;
    XmlNodeType type;
    struct block_file *current = NULL;
    unsigned int capacity = 0;
    int ret = 1;

    if (!entry)
    {
        /* the signature covers the block map: a signed package without one has been stripped of what would compare its
         * files, and no flag makes that acceptable (an unsigned test package has no map and no signature) */
        if (find_entry( L"AppxSignature.p7x" )) return fail( L"the package is signed but has no block map" );
        return 0;
    }
    have_blockmap = TRUE;
    if (entry->size > MAX_BLOCKMAP_SIZE) return fail( L"the block map is too large" );
    if (!(data = read_entry( entry ))) return 1;
    if (!(global = GlobalAlloc( GMEM_MOVEABLE, entry->size ))) { free( data ); return fail( L"out of memory" ); }
    memcpy( GlobalLock( global ), data, entry->size );
    GlobalUnlock( global );
    free( data );
    if (FAILED(CreateStreamOnHGlobal( global, TRUE, &stream ))) { GlobalFree( global ); return fail( L"out of memory" ); }
    if (FAILED(CreateXmlReader( &IID_IXmlReader, (void **)&reader, NULL )) ||
        FAILED(IXmlReader_SetProperty( reader, XmlReaderProperty_DtdProcessing, DtdProcessing_Prohibit )) ||
        FAILED(IXmlReader_SetProperty( reader, XmlReaderProperty_MaxElementDepth, 8 )) ||
        FAILED(IXmlReader_SetInput( reader, (IUnknown *)stream )))
    {
        fail( L"cannot read the block map" );
        goto done;
    }
    while (IXmlReader_Read( reader, &type ) == S_OK)
    {
        const WCHAR *name;
        WCHAR text[MAX_REL_PATH * 3 + 1], decoded[MAX_REL_PATH + 1];

        if (type != XmlNodeType_Element || FAILED(IXmlReader_GetLocalName( reader, &name, NULL ))) continue;
        if (!wcscmp( name, L"File" ))
        {
            WCHAR size_text[32], *end;

            if (!get_attribute( reader, L"Name", text, ARRAY_SIZE(text) ) || !block_name( text, decoded ) ||
                !get_attribute( reader, L"Size", size_text, ARRAY_SIZE(size_text) ))
            {
                fail( L"the block map has a file without a usable name or size" );
                goto done;
            }
            if (find_block_file( decoded ) || block_file_count >= MAX_ITEMS * 16)
            {
                fail( L"the block map lists a file twice, or too many files" );
                goto done;
            }
            if (block_file_count == capacity)
            {
                void *tmp = realloc( block_files, (capacity = capacity ? capacity * 2 : 64) * sizeof(*block_files) );
                if (!tmp) { fail( L"out of memory" ); goto done; }
                block_files = tmp;
            }
            current = &block_files[block_file_count++];
            memset( current, 0, sizeof(*current) );
            wcscpy( current->name, decoded );
            current->size = _wcstoui64( size_text, &end, 10 );
            if (!size_text[0] || *end)
            {
                fail( L"the block map has a file without a usable size" );
                goto done;
            }
        }
        else if (!wcscmp( name, L"Block" ))
        {
            BYTE hash[32];
            DWORD len = sizeof(hash);
            void *tmp;

            if (!current || !get_attribute( reader, L"Hash", text, ARRAY_SIZE(text) ) ||
                !CryptStringToBinaryW( text, 0, CRYPT_STRING_BASE64, hash, &len, NULL, NULL ) || len != sizeof(hash) ||
                block_total >= MAX_BLOCKS)
            {
                fail( L"the block map has a block that is not usable" );
                goto done;
            }
            if (!(tmp = realloc( current->hashes, (current->blocks + 1) * sizeof(*current->hashes) )))
            {
                fail( L"out of memory" );
                goto done;
            }
            current->hashes = tmp;
            memcpy( current->hashes[current->blocks++], hash, sizeof(hash) );
            block_total++;
        }
    }
    ret = 0;

done:
    if (reader) IXmlReader_Release( reader );
    if (stream) IStream_Release( stream );
    return ret;
}

/* the content of a file of the package is what the block map says: its size and the hash of each block */
static int check_blocks( const struct entry *entry, const BYTE *data )
{
    const struct block_file *file;
    unsigned int i;

    if (!have_blockmap || is_unlisted_member( entry->name )) return 0;
    if (!(file = find_block_file( entry->name ))) return fail( L"%s is not in the block map of the package", entry->name );
    if (file->size != entry->size || file->blocks != (entry->size + BLOCK_SIZE - 1) / BLOCK_SIZE)
        return fail( L"%s does not agree with the block map of the package (size)", entry->name );
    for (i = 0; i < file->blocks; i++)
    {
        BYTE hash[32];
        DWORD len = (DWORD)(entry->size - (ULONGLONG)i * BLOCK_SIZE < BLOCK_SIZE ? entry->size - (ULONGLONG)i * BLOCK_SIZE : BLOCK_SIZE);

        if (!sha256( data + (ULONGLONG)i * BLOCK_SIZE, len, hash ) || memcmp( hash, file->hashes[i], 32 ))
            return fail( L"%s does not agree with the block map of the package (block %u): the package changed", entry->name, i );
    }
    return 0;
}

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
    if (check_blocks( entry, data ))
    {
        free( data );
        return 1;
    }
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

/*
 * The signature of the package is judged by WinVerifyTrust, as on Windows, where the module AppxSip.dll answers for
 * packages (here appxsip.dll). The file is the handle held since the archive was mapped, so the bytes judged are the ones
 * installed. What comes back:
 *   success            signed, the chain ends in a trusted root
 *   CERT_E_UNTRUSTEDROOT  signed, by a publisher this machine does not trust
 *   TRUST_E_NOSIGNATURE   not signed
 *   TRUST_E_BAD_DIGEST, TRUST_E_CERT_SIGNATURE ...  signed, but the package or the signature is not what was signed
 *   TRUST_E_SUBJECT_FORM_UNKNOWN  not a consistent package
 */
static const WCHAR *signature_note = L"The signature of the package was not verified.";

/* revocation of the chain (as the package deployment on Windows asks for it): the answers that mean it could not be
 * found out, not that the certificate is revoked */
#define E_CERT_REVOKED            ((LONG)0x800b010c)
#define E_CERT_REVOCATION_FAILURE ((LONG)0x800b010e)
#define E_CRYPT_REVOKED           ((LONG)0x80092010)
#define E_CRYPT_NO_REVOCATION     ((LONG)0x80092012)
#define E_CRYPT_REVOCATION_OFFLINE ((LONG)0x80092013)

static BOOL revocation_unknown( LONG status )
{
    return status == E_CERT_REVOCATION_FAILURE || status == E_CRYPT_NO_REVOCATION || status == E_CRYPT_REVOCATION_OFFLINE;
}

/* the subject of application packages (appxsip.dll), and the type of the signed data that belongs to it */
static const GUID appx_subject = { 0x0ac5df4b, 0xce07, 0x4de2, { 0xb7, 0x6e, 0x23, 0xc8, 0x39, 0xa0, 0x9f, 0xd1 }};
#define SIPINFO_OBJID "1.3.6.1.4.1.311.2.1.30"

/* two distinguished names are the same name: the same attributes in the same order, with the same text whatever string
 * type holds it (a certificate authority may use printable or UTF-8 strings); the case counts */
static BOOL names_equal( const CERT_NAME_BLOB *a, const CERT_NAME_BLOB *b )
{
    CERT_NAME_INFO *x = NULL, *y = NULL;
    DWORD size, i, j;
    BOOL ret = FALSE;

    if (!CryptDecodeObjectEx( X509_ASN_ENCODING, X509_NAME, a->pbData, a->cbData, CRYPT_DECODE_ALLOC_FLAG, NULL, &x, &size ) ||
        !CryptDecodeObjectEx( X509_ASN_ENCODING, X509_NAME, b->pbData, b->cbData, CRYPT_DECODE_ALLOC_FLAG, NULL, &y, &size ))
        goto done;
    if (!x->cRDN || x->cRDN != y->cRDN) goto done;
    for (i = 0; i < x->cRDN; i++)
    {
        if (x->rgRDN[i].cRDNAttr != y->rgRDN[i].cRDNAttr) goto done;
        for (j = 0; j < x->rgRDN[i].cRDNAttr; j++)
        {
            CERT_RDN_ATTR *p = &x->rgRDN[i].rgRDNAttr[j], *q = &y->rgRDN[i].rgRDNAttr[j];
            WCHAR text_p[512], text_q[512];
            DWORD len_p, len_q;

            if (!p->pszObjId || !q->pszObjId || strcmp( p->pszObjId, q->pszObjId )) goto done;
            len_p = CertRDNValueToStrW( p->dwValueType, &p->Value, text_p, ARRAY_SIZE(text_p) );
            len_q = CertRDNValueToStrW( q->dwValueType, &q->Value, text_q, ARRAY_SIZE(text_q) );
            if (len_p < 2 || len_q < 2 || len_p >= ARRAY_SIZE(text_p) || len_q >= ARRAY_SIZE(text_q) ||
                len_p != len_q || wcscmp( text_p, text_q ))
                goto done;
        }
    }
    ret = TRUE;

done:
    if (x) LocalFree( x );
    if (y) LocalFree( y );
    return ret;
}

/* whether the signer's subject is the publisher the manifest names (a text: it is made a name and the names compared, so a
 * long text cannot match by its beginning, an embedded zero does not cut it short, and spacing and quoting do not matter) */
static BOOL subject_is_publisher( const WCHAR *publisher, CERT_NAME_BLOB *subject )
{
    CERT_NAME_BLOB want = { 0 };
    BOOL ret = FALSE;

    if (!publisher[0] || !subject->cbData || wcslen( publisher ) >= PACKAGE_PUBLISHER_MAX_LENGTH) return FALSE;
    if (!CertStrToNameW( X509_ASN_ENCODING, publisher, CERT_X500_NAME_STR, NULL, NULL, &want.cbData, NULL ) || !want.cbData ||
        !(want.pbData = malloc( want.cbData )))
        return FALSE;
    if (CertStrToNameW( X509_ASN_ENCODING, publisher, CERT_X500_NAME_STR, NULL, want.pbData, &want.cbData, NULL ))
        ret = names_equal( &want, subject );
    free( want.pbData );
    return ret;
}

/* WinVerifyTrust on the package, as a package (the subject is given, not guessed from the file: a signed program with an
 * archive behind it must not pass for a package). *signed_by_publisher is TRUE when there is a signer and it is the
 * publisher; signer is the signer's name for display ("" without one). */
static LONG verify_package( HANDLE file, const WCHAR *path, WCHAR *signer, DWORD signer_len, BOOL revocation,
                            const WCHAR *publisher, BOOL *signed_by_publisher )
{
    GUID action = WINTRUST_ACTION_GENERIC_VERIFY_V2;
    WINTRUST_FILE_INFO info = { sizeof(info) };
    WINTRUST_DATA data = { sizeof(data) };
    LONG status;

    info.pcwszFilePath = path;
    info.hFile = file;
    info.pgKnownSubject = (GUID *)&appx_subject;
    data.dwUIChoice = WTD_UI_NONE;
    data.fdwRevocationChecks = revocation ? WTD_REVOKE_WHOLECHAIN : WTD_REVOKE_NONE;
    data.dwUnionChoice = WTD_CHOICE_FILE;
    data.pFile = &info;
    data.dwStateAction = WTD_STATEACTION_VERIFY;
    data.dwProvFlags = revocation ? WTD_REVOCATION_CHECK_CHAIN : WTD_REVOCATION_CHECK_NONE;
    signer[0] = 0;
    *signed_by_publisher = FALSE;
    status = WinVerifyTrust( INVALID_HANDLE_VALUE, &action, &data );
    if (data.hWVTStateData)
    {
        CRYPT_PROVIDER_DATA *prov = WTHelperProvDataFromStateData( data.hWVTStateData );
        CRYPT_PROVIDER_SGNR *signer_info = prov ? WTHelperGetProvSignerFromChain( prov, 0, FALSE, 0 ) : NULL;
        CRYPT_PROVIDER_CERT *cert = signer_info ? WTHelperGetProvCertFromChain( signer_info, 0 ) : NULL;

        if (cert && cert->pCert)
        {
            if (!CertNameToStrW( X509_ASN_ENCODING, &cert->pCert->pCertInfo->Subject, CERT_X500_NAME_STR, signer, signer_len ))
                signer[0] = 0;
            *signed_by_publisher = subject_is_publisher( publisher, &cert->pCert->pCertInfo->Subject );
        }
        /* what was signed has to be the information of a package, however well the signature itself verifies */
        if ((status == ERROR_SUCCESS || status == CERT_E_UNTRUSTEDROOT) &&
            (!prov || !prov->pPDSip || !prov->pPDSip->psIndirectData || !prov->pPDSip->psIndirectData->Data.pszObjId ||
             strcmp( prov->pPDSip->psIndirectData->Data.pszObjId, SIPINFO_OBJID )))
            status = TRUST_E_SUBJECT_FORM_UNKNOWN;
        data.dwStateAction = WTD_STATEACTION_CLOSE;
        WinVerifyTrust( INVALID_HANDLE_VALUE, &action, &data );
    }
    else if (status == ERROR_SUCCESS || status == CERT_E_UNTRUSTEDROOT)
        status = TRUST_E_SUBJECT_FORM_UNKNOWN;
    return status;
}


/* what the signature comes to, for the install and for the installer window (query) */
enum sig_class { SIG_TRUSTED, SIG_UNTRUSTED, SIG_UNSIGNED, SIG_INVALID, SIG_INCONSISTENT, SIG_MISMATCH, SIG_REVOKED };

static const WCHAR *sig_class_name( enum sig_class sig )
{
    static const WCHAR *names[] = { L"trusted", L"untrusted", L"unsigned", L"invalid", L"inconsistent", L"mismatch", L"revoked" };
    return names[sig];
}

static enum sig_class judge_signature( HANDLE file, const WCHAR *package, const struct manifest *manifest, WCHAR *signer,
                                       DWORD signer_len, WCHAR *note, DWORD note_len, const WCHAR **problem )
{
    BOOL by_publisher;
    LONG status = verify_package( file, package, signer, signer_len, TRUE, manifest->publisher, &by_publisher );
    const WCHAR *revocation = L"";

    if (revocation_unknown( status ))
    {
        /* no revocation information, or nobody to ask: judged without it, and said so */
        revocation = L" Revocation could not be checked.";
        status = verify_package( file, package, signer, signer_len, FALSE, manifest->publisher, &by_publisher );
    }

    if (status == TRUST_E_SUBJECT_FORM_UNKNOWN)
    {
        swprintf( note, note_len, L"Signature: not checked, the members, content types and block map of the package do not agree." );
        *problem = L"the package is not consistent, its signature cannot be checked";
        return SIG_INCONSISTENT;
    }
    if (status == ERROR_SUCCESS || status == CERT_E_UNTRUSTEDROOT)
    {
        if (!by_publisher)
        {
            swprintf( note, note_len, L"Signature: the signer %s is not the publisher %s of the manifest.", signer,
                      manifest->publisher );
            *problem = L"the signer is not the publisher of the package";
            return SIG_MISMATCH;
        }
        if (status == ERROR_SUCCESS)
        {
            swprintf( note, note_len, L"Signature: valid, signed by %s (trusted).%s", signer, revocation );
            return SIG_TRUSTED;
        }
        swprintf( note, note_len, L"Signature: valid, signed by %s, a publisher this system does not trust.%s", signer, revocation );
        *problem = L"the publisher is not trusted";
        return SIG_UNTRUSTED;
    }
    if (status == TRUST_E_NOSIGNATURE)
    {
        swprintf( note, note_len, L"Signature: none, the package is not signed." );
        *problem = L"the package is not signed";
        return SIG_UNSIGNED;
    }
    if (status == E_CERT_REVOKED || status == E_CRYPT_REVOKED)
    {
        swprintf( note, note_len, L"Signature: valid, but the certificate of %s was revoked.", signer );
        *problem = L"the certificate of the publisher was revoked";
        return SIG_REVOKED;
    }
    swprintf( note, note_len, L"Signature: invalid (%08lx), the package or its signature was changed.", (ULONG)status );
    *problem = L"the signature is not valid";
    return SIG_INVALID;
}

static int check_signature( HANDLE file, const WCHAR *package, const struct manifest *manifest, BOOL development )
{
    static WCHAR note[512];
    WCHAR signer[256];
    const WCHAR *problem = NULL;

    judge_signature( file, package, manifest, signer, ARRAY_SIZE(signer), note, ARRAY_SIZE(note), &problem );
    signature_note = note;
    if (problem && !development)
        return fail( L"%s: %s (--development installs it anyway)", package, problem );
    return 0;
}

/* a line of the query file: one line, no control characters (the values come from an untrusted package) */
static void query_line( HANDLE out, const WCHAR *key, const WCHAR *value )
{
    WCHAR line[1024];
    DWORD written, n, i;

    n = swprintf( line, ARRAY_SIZE(line) - 2, L"%s=%.600s", key, value );
    for (i = 0; i < n; i++) if (line[i] < 0x20) line[i] = ' ';
    line[n++] = '\n';
    WriteFile( out, line, n * sizeof(WCHAR), &written, NULL );
}

/*
 * wexappx query PACKAGE RESULT: what the installer window shows, written to the file RESULT as UTF-16 lines key=value
 * (state: trusted, untrusted, unsigned, invalid, inconsistent, mismatch, revoked; name, display, publisher, signer,
 * version, problem, note). Installs nothing and changes nothing but RESULT.
 */
static int query( const WCHAR *package, const WCHAR *result )
{
    WCHAR full_name[PACKAGE_FULL_NAME_MAX_LENGTH + 1], signer[256], note[512];
    const WCHAR *problem = L"";
    struct manifest manifest;
    struct entry *entry;
    LARGE_INTEGER size;
    HANDLE file, mapping, out;
    BYTE *data;
    enum sig_class sig;
    static const BYTE bom[2] = { 0xff, 0xfe };
    DWORD written;

    memset( &manifest, 0, sizeof(manifest) );
    file = CreateFileW( package, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL );
    if (file == INVALID_HANDLE_VALUE) return fail( L"cannot open %s (error %lu)", package, GetLastError() );
    if (!GetFileSizeEx( file, &size ) || !size.QuadPart || size.QuadPart > MAX_ARCHIVE_SIZE)
        return fail( L"%s is empty or too large", package );
    archive_size = size.QuadPart;
    mapping = CreateFileMappingW( file, NULL, PAGE_READONLY, 0, 0, NULL );
    if (!mapping || !(archive = MapViewOfFile( mapping, FILE_MAP_READ, 0, 0, 0 )))
        return fail( L"cannot read %s (error %lu)", package, GetLastError() );
    if (read_directory()) return 1;
    if (!(entry = find_entry( L"AppxManifest.xml" ))) return fail( L"the archive has no AppxManifest.xml" );
    if (entry->size > MAX_MANIFEST_SIZE) return fail( L"the manifest is too large" );
    if (!(data = read_entry( entry ))) return 1;
    if (parse_manifest( data, entry->size, &manifest )) return 1;
    free( data );
    if (get_full_name( &manifest, full_name )) return 1;
    sig = judge_signature( file, package, &manifest, signer, ARRAY_SIZE(signer), note, ARRAY_SIZE(note), &problem );

    out = CreateFileW( result, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL );
    if (out == INVALID_HANDLE_VALUE) return fail( L"cannot write %s (error %lu)", result, GetLastError() );
    WriteFile( out, bom, sizeof(bom), &written, NULL );
    query_line( out, L"state", sig_class_name( sig ) );
    query_line( out, L"name", manifest.name );
    query_line( out, L"display", manifest.display );
    query_line( out, L"publisher", manifest.publisher );
    query_line( out, L"signer", signer );
    query_line( out, L"version", manifest.version );
    query_line( out, L"problem", problem );
    query_line( out, L"note", note );
    CloseHandle( out );
    return 0;
}

static int install( const WCHAR *package, BOOL development, BOOL machine )
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
    WCHAR resolved[MAX_DEPENDENCIES][PACKAGE_FULL_NAME_MAX_LENGTH + 1];
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
    /* the file stays open until the program ends and the signature is checked on this handle. The share mode keeps other
     * Windows programs from writing to it; a program of the host system ignores share modes, so the bytes behind the mapping
     * can still change between the check and the extraction (KNOWN_BUGS B68: the block map's hashes are not yet compared). */
    if (!mapping || !(archive = MapViewOfFile( mapping, FILE_MAP_READ, 0, 0, 0 )))
        return fail( L"cannot read %s (error %lu)", package, GetLastError() );

    /* everything is checked before anything is written */
    if (machine && !package_is_frozen_file( package, archive, archive_size ))
        return fail( L"%s is not the file the elevation broker froze when the consent was given", package );
    if (read_directory()) return 1;
    if (load_blockmap()) return 1;   /* a package without one (an unsigned test package) is not compared */
    if (!(entry = find_entry( L"AppxManifest.xml" ))) return fail( L"the archive has no AppxManifest.xml" );
    if (entry->size > MAX_MANIFEST_SIZE) return fail( L"the manifest is too large" );
    if (!(data = read_entry( entry ))) return 1;
    if (parse_manifest( data, entry->size, &manifest )) return 1;
    free( data );
    if (get_full_name( &manifest, full_name )) return 1;
    if (check_signature( file, package, &manifest, development )) return 1;
    /* every dependency has to be installed (a framework package is installed before the packages that need it) */
    for (i = 0; i < manifest.dep_count; i++)
        if (!resolve_dependency( &manifest, &manifest.deps[i], resolved[i], ARRAY_SIZE(resolved[i]) ))
            return fail( L"the package depends on %s (version %s or later), which is not installed", manifest.deps[i].name,
                         manifest.deps[i].min_version );

    if (!get_store_dir( store, ARRAY_SIZE(store), machine ))
        return fail( machine ? L"no package directory on drive C: (Program Files\\WindowsApps)" : L"no per-user package directory on drive C:" );
    if (!get_catalog_dir( catalog, ARRAY_SIZE(catalog), machine ))
        return fail( machine ? L"the machine catalog is not known" : L"the prefix directory is not known" );
    CreateDirectoryW( catalog, NULL );
    /* a package for every user can use only what every user has: its dependencies are in the machine catalog too */
    for (i = 0; machine && i < manifest.dep_count; i++)
    {
        WCHAR dep_record[MAX_PATH];

        if (swprintf( dep_record, ARRAY_SIZE(dep_record), L"%s%s.wexappx", catalog, resolved[i] ) < 0 ||
            GetFileAttributesW( dep_record ) == INVALID_FILE_ATTRIBUTES)
            return fail( L"the package depends on %s, which is installed for this user only", resolved[i] );
    }
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
    /* the publisher, for the package APIs (kernelbase): only when it is plain printable text */
    for (i = 0; manifest.publisher[i] >= 0x20 && manifest.publisher[i] <= 0x7e; i++) ;
    if (!manifest.publisher[i] && i && !append_record( record, L"publisher\t%s\n", manifest.publisher )) goto too_long;
    if (manifest.framework && !append_record( record, L"framework\t1\n" )) goto too_long;
    for (i = 0; i < manifest.dep_count; i++)
        if (!append_record( record, L"dependency\t%s\n", resolved[i] )) goto too_long;
    for (i = 0; i < manifest.app_count; i++)
    {
        if (!(entry = find_entry( manifest.apps[i].path )))
        {
            fail( L"the executable %s of application %s is not in the package", manifest.apps[i].path, manifest.apps[i].id );
            goto done;
        }
        if (manifest.apps[i].entry[0])
        {
            if (!append_record( record, L"app\t%s\t%s\t%s\n", manifest.apps[i].id, entry->name, manifest.apps[i].entry )) goto too_long;
        }
        else if (!append_record( record, L"app\t%s\t%s\n", manifest.apps[i].id, entry->name )) goto too_long;
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
    wprintf( L"%s\n", signature_note );
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

static int uninstall( const WCHAR *full_name, BOOL machine )
{
    WCHAR store[MAX_PATH], catalog[MAX_PATH], target[MAX_PATH], record_path[MAX_PATH];

    if (!valid_full_name( full_name )) return fail( L"%s is not a package full name", full_name );
    if (!get_store_dir( store, ARRAY_SIZE(store), machine ) || !get_catalog_dir( catalog, ARRAY_SIZE(catalog), machine ))
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

static int list( BOOL machine )
{
    WCHAR catalog[MAX_PATH], pattern[MAX_PATH];
    WIN32_FIND_DATAW data;
    HANDLE find;

    if (!get_catalog_dir( catalog, ARRAY_SIZE(catalog), machine )) return fail( L"the catalog directory is not known" );
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
            L"  wexappx install [--development] [--all-users] PACKAGE\n"
            L"                                            install an .appx or .msix package for this user, or for every user\n"
            L"                                            (an administrator's consent through the elevation broker)\n"
            L"  wexappx query PACKAGE RESULT              write what the installer window shows (key=value, UTF-16)\n"
            L"  wexappx uninstall [--all-users] PACKAGE_FULL_NAME\n"
            L"                                            remove an installed package\n"
            L"  wexappx list [--all-users]                list the installed packages\n"
            L"\n"
            L"The signature of the package is checked: a package that is not signed, whose publisher this system does not\n"
            L"trust, or whose signature is not valid is refused. --development installs it anyway (and says what the\n"
            L"signature is).\n", stderr );
    return 2;
}

int __cdecl wmain( int argc, WCHAR **argv )
{
    int ret;

    if (argc < 2) return usage();
    /* query only reads a package and writes the file it is given: the consent window and the installer window ask it
     * from accounts (session 0's, the elevation broker's child) that have not set the gate */
    if (!wcscmp( argv[1], L"query" ) && argc == 4 && argv[2][0] != '-')
    {
        if (FAILED(CoInitializeEx( NULL, COINIT_MULTITHREADED ))) return fail( L"COM is not available" );
        ret = query( argv[2], argv[3] );
        CoUninitialize();
        return ret;
    }
    if (!uwp_compatibility_enabled())
        return fail( L"UWP compatibility is not enabled (HKCU\\Software\\WineExplorer\\Shell, UwpCompatibility = 1)" );

    if (!wcscmp( argv[1], L"install" ))
    {
        BOOL development = FALSE, machine = FALSE;
        int i;

        /* options, then the package: a package path is never an option */
        for (i = 2; i < argc - 1; i++)
        {
            if (!wcscmp( argv[i], L"--development" ) && !development) development = TRUE;
            else if (!wcscmp( argv[i], L"--all-users" ) && !machine) machine = TRUE;
            else return usage();
        }
        if (argc < 3 || argv[argc - 1][0] == '-') return usage();
        if (machine && !token_is_elevated())
        {
            DWORD code = 1, err = run_elevated( &code );

            if (err == ERROR_CANCELLED) return fail( L"the installation for all users was not allowed" );
            if (err == ERROR_NOT_SUPPORTED)
                return fail( L"installing for all users requires elevation, which is not available here (an administrator, "
                             L"and the elevation broker, are needed)" );
            if (err) return fail( L"the installation for all users could not be started (error %lu)", err );
            return code;
        }
        if (FAILED(CoInitializeEx( NULL, COINIT_MULTITHREADED ))) return fail( L"COM is not available" );
        ret = install( argv[argc - 1], development, machine );
        CoUninitialize();
        return ret;
    }
    if (!wcscmp( argv[1], L"uninstall" ) && (argc == 3 || (argc == 4 && !wcscmp( argv[2], L"--all-users" ))))
    {
        BOOL machine = argc == 4;

        if (machine && !token_is_elevated())
        {
            DWORD code = 1, err = run_elevated( &code );

            if (err == ERROR_CANCELLED) return fail( L"the removal for all users was not allowed" );
            if (err) return fail( L"the removal for all users could not be started (error %lu)", err );
            return code;
        }
        return uninstall( argv[argc - 1], machine );
    }
    if (!wcscmp( argv[1], L"list" ) && (argc == 2 || (argc == 3 && !wcscmp( argv[2], L"--all-users" ))))
        return list( argc == 3 );
    return usage();
}

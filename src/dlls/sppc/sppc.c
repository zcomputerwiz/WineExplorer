/*
 * Software licensing client (sppc.dll): a local license store
 *
 * Copyright 2008 Alistair Leslie-Hughes
 * Copyright 2026 WineExplorer contributors
 *
 * Programs (Office setup first) install their license files and product key here and ask
 * for the licensing state.  The store keeps what was installed and reports the state as it
 * is: a product with a key starts its grace period when the key is installed, and when that
 * has run out it is not licensed.  Nothing here activates a product; activation needs the
 * licensing service to talk to an activation server, which is not implemented.
 *
 * License files are kept as they come in WINDOWS\spp\wine\{file id}.xrm-ms, keys and grace
 * start times in HKLM\Software\Wine\Licensing (64-bit view), so 32- and 64-bit programs see
 * one store.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301, USA
 */

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ntstatus.h"
#include "windef.h"
#include "winbase.h"
#include "winreg.h"
#include "winnls.h"
#include "winternl.h"
#include "bcrypt.h"
#include "wine/debug.h"

#include "slpublic.h"
#include "slerror.h"

WINE_DEFAULT_DEBUG_CHANNEL(slc);

/* public error codes (slerror.h of the SDK) that Wine's header lacks */
#define SL_E_GRACE_TIME_EXPIRED         ((HRESULT)0xc004f009)
#define SL_E_LICENSE_FILE_NOT_INSTALLED ((HRESULT)0xc004f010)
#define SL_E_PKEY_NOT_INSTALLED         ((HRESULT)0xc004f014)
#define SL_E_PRODUCT_SKU_NOT_INSTALLED  ((HRESULT)0xc004f015)
#define SL_E_NOT_SUPPORTED              ((HRESULT)0xc004f016)
#define SL_E_INVALID_PRODUCT_KEY        ((HRESULT)0xc004f050)
#define SL_I_OOB_GRACE_PERIOD           ((HRESULT)0x4004f00c)

typedef enum
{
    SL_ID_APPLICATION,
    SL_ID_PRODUCT_SKU,
    SL_ID_LICENSE_FILE,
    SL_ID_LICENSE,
    SL_ID_PKEY,
    SL_ID_ALL_LICENSES,
    SL_ID_ALL_LICENSE_FILES,
    SL_ID_STORE_TOKEN,
    SL_ID_LAST
} SLIDTYPE;

/* ponytail: the grace period of a volume product before its first activation, a constant;
 * read it from the product's license when a product with another length is met. */
#define GRACE_DAYS 30

#define DPID4_SIZE      0x4f8
#define DPID4_ACT_ID    0x088
#define DPID4_KEY_HASH  0x338
#define DPID4_KEY_TYPE  0x3f8

static const WCHAR store_key[] = L"Software\\Wine\\Licensing";

struct context
{
    BOOL consumed;      /* a right was consumed through this handle */
    SLID app, sku;      /* for these */
    BOOL any_sku;
};

/* one license of a stored license file */
struct license
{
    SLID file, id, app, sku;
};

static void sha256( const BYTE *data, ULONG size, BYTE *digest )
{
    BCRYPT_ALG_HANDLE alg;

    memset( digest, 0, 32 );
    if (BCryptOpenAlgorithmProvider( &alg, BCRYPT_SHA256_ALGORITHM, NULL, 0 )) return;
    BCryptHash( alg, NULL, 0, (UCHAR *)data, size, digest, 32 );
    BCryptCloseAlgorithmProvider( alg, 0 );
}

static void id_from_data( const BYTE *data, ULONG size, SLID *id )
{
    BYTE digest[32];

    sha256( data, size, digest );
    memcpy( id, digest, sizeof(*id) );
}

static void format_id( const SLID *id, WCHAR *text )
{
    swprintf( text, 39, L"{%08lx-%04x-%04x-%02x%02x-%02x%02x%02x%02x%02x%02x}", id->Data1, id->Data2, id->Data3,
              id->Data4[0], id->Data4[1], id->Data4[2], id->Data4[3], id->Data4[4], id->Data4[5], id->Data4[6],
              id->Data4[7] );
}

/* "{xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx}", the braces optional */
static BOOL parse_id( const char *text, SLID *id )
{
    unsigned int d[11];

    if (*text == '{') text++;
    if (sscanf( text, "%8x-%4x-%4x-%2x%2x-%2x%2x%2x%2x%2x%2x", &d[0], &d[1], &d[2], &d[3], &d[4], &d[5], &d[6],
                &d[7], &d[8], &d[9], &d[10] ) != 11) return FALSE;
    id->Data1 = d[0];
    id->Data2 = d[1];
    id->Data3 = d[2];
    for (unsigned int i = 0; i < 8; i++) id->Data4[i] = d[3 + i];
    return TRUE;
}

static BOOL store_dir( WCHAR *path, BOOL create )
{
    UINT len = GetWindowsDirectoryW( path, MAX_PATH - 64 );

    if (!len || len >= MAX_PATH - 64) return FALSE;
    wcscat( path, L"\\spp" );
    if (create) CreateDirectoryW( path, NULL );
    wcscat( path, L"\\wine" );
    if (create) CreateDirectoryW( path, NULL );
    return TRUE;
}

static BOOL license_path( const SLID *file, WCHAR *path, BOOL create )
{
    WCHAR name[40];

    if (!store_dir( path, create )) return FALSE;
    format_id( file, name );
    wcscat( path, L"\\" );
    wcscat( path, name );
    wcscat( path, L".xrm-ms" );
    return TRUE;
}

static char *read_file( const WCHAR *path, DWORD *size )
{
    HANDLE handle = CreateFileW( path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL );
    char *data = NULL;
    DWORD read;

    if (handle == INVALID_HANDLE_VALUE) return NULL;
    *size = GetFileSize( handle, NULL );
    if (*size != INVALID_FILE_SIZE && (data = malloc( *size + 1 )))
    {
        if (ReadFile( handle, data, *size, &read, NULL ) && read == *size) data[*size] = 0;
        else
        {
            free( data );
            data = NULL;
        }
    }
    CloseHandle( handle );
    return data;
}

/* Text of <tm:infoStr name="NAME"> (or another element kind) between start and end. */
/* ponytail: text search in UTF-8 license files, no XML parser; names and prefixes as the files have them. */
static BOOL named_value( const char *start, const char *end, const char *element, const char *name,
                         char *buffer, size_t size )
{
    char pattern[128];
    const char *p, *q;

    snprintf( pattern, sizeof(pattern), "<%s name=\"%s\"", element, name );
    if (!(p = strstr( start, pattern )) || p >= end)
    {
        /* callers write "LicenseType", the files "licenseType" */
        char *first = pattern + strlen( element ) + 8;
        if (*first < 'A' || *first > 'Z') return FALSE;
        *first += 'a' - 'A';
        if (!(p = strstr( start, pattern )) || p >= end) return FALSE;
    }
    if (!(p = strchr( p, '>' )) || p >= end) return FALSE;
    p++;
    if (!(q = strchr( p, '<' )) || q > end || (size_t)(q - p) >= size) return FALSE;
    memcpy( buffer, p, q - p );
    buffer[q - p] = 0;
    return TRUE;
}

/* Call the callback for every license of every stored license file; it returns FALSE to stop. */
typedef BOOL (*license_callback)( const struct license *license, const char *start, const char *end, void *arg );

static void for_each_license( license_callback callback, void *arg )
{
    WCHAR dir[MAX_PATH], path[MAX_PATH];
    WIN32_FIND_DATAW find;
    BOOL more = TRUE;
    HANDLE handle;

    if (!store_dir( dir, FALSE )) return;
    swprintf( path, MAX_PATH, L"%s\\*.xrm-ms", dir );
    if ((handle = FindFirstFileW( path, &find )) == INVALID_HANDLE_VALUE) return;
    do
    {
        struct license license;
        const char *p, *end;
        char name[40], text[64], *data;
        DWORD size;

        WideCharToMultiByte( CP_ACP, 0, find.cFileName, -1, name, sizeof(name), NULL, NULL );
        memset( &license, 0, sizeof(license) );
        if (!parse_id( name, &license.file )) continue;
        swprintf( path, MAX_PATH, L"%s\\%s", dir, find.cFileName );
        if (!(data = read_file( path, &size ))) continue;
        for (p = data; more && (p = strstr( p, "<r:license " )); p = end)
        {
            const char *attr;

            if (!(end = strstr( p, "</r:license>" ))) break;
            memset( &license.id, 0, sizeof(license.id) );
            memset( &license.app, 0, sizeof(license.app) );
            memset( &license.sku, 0, sizeof(license.sku) );
            if ((attr = strstr( p, "licenseId=\"" )) && attr < end) parse_id( attr + 11, &license.id );
            if (named_value( p, end, "tm:infoStr", "applicationId", text, sizeof(text) ))
                parse_id( text, &license.app );
            if (named_value( p, end, "tm:infoStr", "productSkuId", text, sizeof(text) ))
                parse_id( text, &license.sku );
            more = callback( &license, p, end, arg );
        }
        free( data );
    } while (more && FindNextFileW( handle, &find ));
    FindClose( handle );
}

static BOOL is_null_id( const SLID *id )
{
    static const SLID null_id;
    return !memcmp( id, &null_id, sizeof(*id) );
}

/* list of unique ids */
struct id_list
{
    SLID *ids;
    UINT  count, size;
};

static void add_id( struct id_list *list, const SLID *id )
{
    UINT i;

    if (is_null_id( id )) return;
    for (i = 0; i < list->count; i++) if (!memcmp( &list->ids[i], id, sizeof(*id) )) return;
    if (list->count == list->size)
    {
        SLID *ids = realloc( list->ids, (list->size + 32) * sizeof(*ids) );
        if (!ids) return;
        list->ids = ids;
        list->size += 32;
    }
    list->ids[list->count++] = *id;
}

/* Give a list to the caller in memory to release with LocalFree.  As on Windows an empty list
 * is an error and leaves the caller's variables alone. */
static HRESULT return_ids( struct id_list *list, UINT *count, SLID **ids )
{
    SLID *copy;

    if (!list->count) return SL_E_VALUE_NOT_FOUND;
    if (!(copy = LocalAlloc( 0, list->count * sizeof(SLID) )))
    {
        free( list->ids );
        return E_OUTOFMEMORY;
    }
    memcpy( copy, list->ids, list->count * sizeof(SLID) );
    *count = list->count;
    *ids = copy;
    free( list->ids );
    return S_OK;
}

static HRESULT return_value( SLDATATYPE kind, const void *data, UINT size, SLDATATYPE *type, UINT *ret_size,
                             BYTE **value )
{
    if (!ret_size || !value) return E_INVALIDARG;
    if (!(*value = LocalAlloc( 0, size ? size : 1 ))) return E_OUTOFMEMORY;
    memcpy( *value, data, size );
    *ret_size = size;
    if (type) *type = kind;
    return S_OK;
}

static HRESULT return_text( const char *text, SLDATATYPE *type, UINT *size, BYTE **value )
{
    WCHAR buffer[1024];
    int len = MultiByteToWideChar( CP_UTF8, 0, text, -1, buffer, ARRAY_SIZE(buffer) );

    if (!len) return SL_E_VALUE_NOT_FOUND;
    return return_value( SL_DATA_SZ, buffer, len * sizeof(WCHAR), type, size, value );
}

/*
 * Product keys: HKLM\Software\Wine\Licensing\ProductKeys\{key id} with the values Sku (the
 * product's id), DigitalPID2 (the block pidgenx returns), PartialProductKey, Channel.
 * Grace start times: ...\GraceStart, one QWORD (FILETIME) per product id; they stay when a
 * key is removed, so installing a key again does not start a new grace period.
 */
static HKEY open_store_key( const WCHAR *sub, BOOL create )
{
    WCHAR path[128];
    HKEY key;

    swprintf( path, ARRAY_SIZE(path), L"%s\\%s", store_key, sub );
    if (create)
    {
        if (RegCreateKeyExW( HKEY_LOCAL_MACHINE, path, 0, NULL, 0, KEY_ALL_ACCESS | KEY_WOW64_64KEY, NULL, &key,
                             NULL )) return NULL;
    }
    else if (RegOpenKeyExW( HKEY_LOCAL_MACHINE, path, 0, KEY_READ | KEY_WOW64_64KEY, &key )) return NULL;
    return key;
}

static BOOL read_key_value( const SLID *pkey, const WCHAR *name, void *data, DWORD *size )
{
    WCHAR sub[64], id[40];
    BOOL ret;
    HKEY key;

    format_id( pkey, id );
    swprintf( sub, ARRAY_SIZE(sub), L"ProductKeys\\%s", id );
    if (!(key = open_store_key( sub, FALSE ))) return FALSE;
    ret = !RegQueryValueExW( key, name, NULL, NULL, data, size );
    RegCloseKey( key );
    return ret;
}

/* Add the ids of the installed keys, of one product or of all. */
static void list_keys( const SLID *sku, struct id_list *keys, struct id_list *skus )
{
    WCHAR name[40];
    char text[40];
    DWORD index, size;
    HKEY key;

    if (!(key = open_store_key( L"ProductKeys", FALSE ))) return;
    for (index = 0; size = ARRAY_SIZE(name), !RegEnumKeyExW( key, index, name, &size, NULL, NULL, NULL, NULL ); index++)
    {
        SLID id, key_sku;

        WideCharToMultiByte( CP_ACP, 0, name, -1, text, sizeof(text), NULL, NULL );
        size = sizeof(key_sku);
        if (!parse_id( text, &id ) || !read_key_value( &id, L"Sku", &key_sku, &size )) continue;
        if (sku && memcmp( sku, &key_sku, sizeof(*sku) )) continue;
        if (keys) add_id( keys, &id );
        if (skus) add_id( skus, &key_sku );
    }
    RegCloseKey( key );
}

static BOOL sku_has_key( const SLID *sku )
{
    struct id_list keys = { 0 };
    BOOL ret;

    list_keys( sku, &keys, NULL );
    ret = keys.count != 0;
    free( keys.ids );
    return ret;
}

/* The state of one product as it is: no key, in its grace period, or past it. */
static void sku_status( const SLID *sku, SL_LICENSING_STATUS *status )
{
    WCHAR id[40];
    ULONGLONG start = 0, now;
    DWORD size = sizeof(start), now_seconds = 0, start_seconds = 0, minutes;
    FILETIME ft;
    HKEY key;

    memset( status, 0, sizeof(*status) );
    status->SkuId = *sku;
    status->eStatus = SL_LICENSING_STATUS_UNLICENSED;
    status->hrReason = SL_E_PKEY_NOT_INSTALLED;
    if (!sku_has_key( sku )) return;

    format_id( sku, id );
    if ((key = open_store_key( L"GraceStart", FALSE )))
    {
        RegQueryValueExW( key, id, NULL, NULL, (BYTE *)&start, &size );
        RegCloseKey( key );
    }
    GetSystemTimeAsFileTime( &ft );
    now = ((ULONGLONG)ft.dwHighDateTime << 32) | ft.dwLowDateTime;
    /* in seconds since 1970: no 64-bit division in 32-bit code */
    RtlTimeToSecondsSince1970( (LARGE_INTEGER *)&now, &now_seconds );
    RtlTimeToSecondsSince1970( (LARGE_INTEGER *)&start, &start_seconds );
    minutes = start && now_seconds > start_seconds ? (now_seconds - start_seconds) / 60 : 0;
    status->dwTotalGraceDays = GRACE_DAYS;
    if (start && minutes < GRACE_DAYS * 24 * 60)
    {
        status->eStatus = SL_LICENSING_STATUS_IN_GRACE_PERIOD;
        status->dwGraceTime = GRACE_DAYS * 24 * 60 - minutes;
        status->hrReason = SL_I_OOB_GRACE_PERIOD;
    }
    else
    {
        status->eStatus = SL_LICENSING_STATUS_NOTIFICATION;
        status->hrReason = SL_E_GRACE_TIME_EXPIRED;
    }
}

/***********************************************************************
 *      SLOpen (sppc.@)
 */
HRESULT WINAPI SLOpen( HSLC *handle )
{
    TRACE( "(%p)\n", handle );

    if (!handle) return E_INVALIDARG;
    if (!(*handle = calloc( 1, sizeof(struct context) ))) return E_OUTOFMEMORY;
    return S_OK;
}

/***********************************************************************
 *      SLClose (sppc.@)
 */
HRESULT WINAPI SLClose( HSLC handle )
{
    TRACE( "(%p)\n", handle );

    free( handle );
    return S_OK;
}

/***********************************************************************
 *      SLInstallLicense (sppc.@)
 */
HRESULT WINAPI SLInstallLicense( HSLC handle, UINT size, const BYTE *blob, SLID *file_id )
{
    WCHAR path[MAX_PATH];
    HANDLE file;
    DWORD written;
    SLID id;
    BOOL ok;

    TRACE( "(%p %u %p %p)\n", handle, size, blob, file_id );

    if (!size || !blob || !file_id) return E_INVALIDARG;
    id_from_data( blob, size, &id );
    if (!license_path( &id, path, TRUE )) return E_FAIL;
    file = CreateFileW( path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL );
    if (file == INVALID_HANDLE_VALUE) return HRESULT_FROM_WIN32( GetLastError() );
    ok = WriteFile( file, blob, size, &written, NULL ) && written == size;
    CloseHandle( file );
    if (!ok)
    {
        DeleteFileW( path );
        return E_FAIL;
    }
    *file_id = id;
    TRACE( "stored %s\n", debugstr_w(path) );
    return S_OK;
}

/***********************************************************************
 *      SLUninstallLicense (sppc.@)
 */
HRESULT WINAPI SLUninstallLicense( HSLC handle, const SLID *file_id )
{
    WCHAR path[MAX_PATH];

    TRACE( "(%p %s)\n", handle, debugstr_guid(file_id) );

    if (!file_id || !license_path( file_id, path, FALSE )) return E_INVALIDARG;
    return DeleteFileW( path ) ? S_OK : SL_E_LICENSE_FILE_NOT_INSTALLED;
}

/***********************************************************************
 *      SLGetLicenseFileId (sppc.@)
 */
HRESULT WINAPI SLGetLicenseFileId( HSLC handle, UINT size, const BYTE *blob, SLID *file_id )
{
    WCHAR path[MAX_PATH];
    SLID id;

    TRACE( "(%p %u %p %p)\n", handle, size, blob, file_id );

    if (!size || !blob || !file_id) return E_INVALIDARG;
    id_from_data( blob, size, &id );
    if (!license_path( &id, path, FALSE ) || GetFileAttributesW( path ) == INVALID_FILE_ATTRIBUTES)
        return SL_E_LICENSE_FILE_NOT_INSTALLED;
    *file_id = id;
    return S_OK;
}

/***********************************************************************
 *      SLGetLicense (sppc.@)
 */
HRESULT WINAPI SLGetLicense( HSLC handle, const SLID *file_id, UINT *size, BYTE **blob )
{
    WCHAR path[MAX_PATH];
    DWORD len;
    HRESULT hr;
    char *data;

    TRACE( "(%p %s %p %p)\n", handle, debugstr_guid(file_id), size, blob );

    if (!file_id || !size || !blob) return E_INVALIDARG;
    if (!license_path( file_id, path, FALSE ) || !(data = read_file( path, &len )))
        return SL_E_LICENSE_FILE_NOT_INSTALLED;
    hr = return_value( SL_DATA_BINARY, data, len, NULL, size, blob );
    free( data );
    return hr;
}

struct list_query
{
    SLIDTYPE in_type, out_type;
    const SLID *in_id;
    struct id_list list;
};

static BOOL list_callback( const struct license *license, const char *start, const char *end, void *arg )
{
    struct list_query *query = arg;
    const SLID *field;

    switch (query->in_type)
    {
    case SL_ID_APPLICATION:  field = &license->app; break;
    case SL_ID_PRODUCT_SKU:  field = &license->sku; break;
    case SL_ID_LICENSE_FILE: field = &license->file; break;
    case SL_ID_LICENSE:      field = &license->id; break;
    default:                 field = NULL; break;
    }
    if (field && query->in_id && memcmp( field, query->in_id, sizeof(SLID) )) return TRUE;
    switch (query->out_type)
    {
    case SL_ID_APPLICATION:  add_id( &query->list, &license->app ); break;
    case SL_ID_PRODUCT_SKU:  add_id( &query->list, &license->sku ); break;
    case SL_ID_LICENSE_FILE:
    case SL_ID_ALL_LICENSE_FILES: add_id( &query->list, &license->file ); break;
    case SL_ID_LICENSE:
    case SL_ID_ALL_LICENSES: add_id( &query->list, &license->id ); break;
    default: break;
    }
    return TRUE;
}

/***********************************************************************
 *      SLGetSLIDList (sppc.@)
 */
HRESULT WINAPI SLGetSLIDList( HSLC handle, SLIDTYPE in_type, const SLID *in_id, SLIDTYPE out_type, UINT *count,
                              SLID **ids )
{
    struct list_query query = { in_type, out_type, in_id };

    TRACE( "(%p %u %s %u %p %p)\n", handle, in_type, debugstr_guid(in_id), out_type, count, ids );

    if (!count || !ids) return E_INVALIDARG;
    /* the pairs Windows 11 answers (scripts/lab/office/sppc-probe.c); a type to itself lists all */
    if (in_type == out_type && (in_type == SL_ID_APPLICATION || in_type == SL_ID_PRODUCT_SKU ||
                                in_type == SL_ID_LICENSE))
        query.in_id = NULL;
    else if (!((in_type == SL_ID_APPLICATION && (out_type == SL_ID_PRODUCT_SKU || out_type == SL_ID_LICENSE)) ||
               (in_type == SL_ID_PRODUCT_SKU && (out_type == SL_ID_APPLICATION || out_type == SL_ID_LICENSE ||
                                                 out_type == SL_ID_PKEY)) ||
               (in_type == SL_ID_LICENSE_FILE && out_type == SL_ID_LICENSE) ||
               (in_type == SL_ID_LICENSE && out_type == SL_ID_LICENSE_FILE)))
        return SL_E_NOT_SUPPORTED;
    else if (!in_id) return E_INVALIDARG;

    if (out_type == SL_ID_PKEY) list_keys( in_id, &query.list, NULL );
    else for_each_license( list_callback, &query );
    TRACE( "%u ids\n", query.list.count );
    return return_ids( &query.list, count, ids );
}

struct info_query
{
    SLIDTYPE type;
    const SLID *id;
    const char *element, *name;
    char text[1024];
    BOOL found, exists;
};

static BOOL info_callback( const struct license *license, const char *start, const char *end, void *arg )
{
    struct info_query *query = arg;
    const SLID *field = query->type == SL_ID_PRODUCT_SKU ? &license->sku :
                        query->type == SL_ID_APPLICATION ? &license->app : &license->id;

    if (memcmp( field, query->id, sizeof(SLID) )) return TRUE;
    query->exists = TRUE;
    if (!named_value( start, end, query->element, query->name, query->text, sizeof(query->text) )) return TRUE;
    query->found = TRUE;
    return FALSE;
}

/* A named string of the licenses of a product, an application or one license. */
static HRESULT license_info( SLIDTYPE id_type, const SLID *id, const WCHAR *name, SLDATATYPE *type, UINT *size,
                             BYTE **value )
{
    struct info_query query = { id_type, id, "tm:infoStr" };
    char name_a[96];

    if (!id || !name || !size || !value) return E_INVALIDARG;
    WideCharToMultiByte( CP_UTF8, 0, name, -1, name_a, sizeof(name_a), NULL, NULL );
    name_a[sizeof(name_a) - 1] = 0;
    query.name = name_a;
    /* the documented names of the two common values, and the names the files use */
    if (!wcsicmp( name, L"Name" )) query.name = "productName";
    else if (!wcsicmp( name, L"Description" )) query.name = "productDescription";
    for_each_license( info_callback, &query );
    if (query.found) return return_text( query.text, type, size, value );
    if (query.exists) FIXME( "no value %s for %s (id type %u)\n", debugstr_w(name), debugstr_guid(id), id_type );
    return SL_E_VALUE_NOT_FOUND;
}

/***********************************************************************
 *      SLGetProductSkuInformation (sppc.@)
 */
HRESULT WINAPI SLGetProductSkuInformation( HSLC handle, const SLID *sku, const WCHAR *name, SLDATATYPE *type,
                                           UINT *size, BYTE **value )
{
    TRACE( "(%p %s %s %p %p %p)\n", handle, debugstr_guid(sku), debugstr_w(name), type, size, value );
    return license_info( SL_ID_PRODUCT_SKU, sku, name, type, size, value );
}

/***********************************************************************
 *      SLGetLicenseInformation (sppc.@)
 */
HRESULT WINAPI SLGetLicenseInformation( HSLC handle, const SLID *license, const WCHAR *name, SLDATATYPE *type,
                                        UINT *size, BYTE **value )
{
    TRACE( "(%p %s %s %p %p %p)\n", handle, debugstr_guid(license), debugstr_w(name), type, size, value );
    return license_info( SL_ID_LICENSE, license, name, type, size, value );
}

/***********************************************************************
 *      SLGetApplicationInformation (sppc.@)
 */
HRESULT WINAPI SLGetApplicationInformation( HSLC handle, const SLID *app, const WCHAR *name, SLDATATYPE *type,
                                            UINT *size, BYTE **value )
{
    TRACE( "(%p %s %s %p %p %p)\n", handle, debugstr_guid(app), debugstr_w(name), type, size, value );
    return license_info( SL_ID_APPLICATION, app, name, type, size, value );
}

/***********************************************************************
 *      SLGetServiceInformation (sppc.@)
 */
HRESULT WINAPI SLGetServiceInformation( HSLC handle, const WCHAR *name, SLDATATYPE *type, UINT *size, BYTE **value )
{
    TRACE( "(%p %s %p %p %p)\n", handle, debugstr_w(name), type, size, value );

    if (!name || !size || !value) return E_INVALIDARG;
    if (!wcsicmp( name, L"Version" ))
    {
        RTL_OSVERSIONINFOEXW version = { sizeof(version) };
        WCHAR text[48];

        RtlGetVersion( &version );
        swprintf( text, ARRAY_SIZE(text), L"%lu.%lu.%lu.0", version.dwMajorVersion, version.dwMinorVersion,
                  version.dwBuildNumber );
        return return_value( SL_DATA_SZ, text, (wcslen( text ) + 1) * sizeof(WCHAR), type, size, value );
    }
    FIXME( "no service value %s\n", debugstr_w(name) );
    return SL_E_VALUE_NOT_FOUND;
}

static BOOL find_config_callback( const struct license *license, const char *start, const char *end, void *arg )
{
    if (!strstr( start, "name=\"pkeyConfigData\"" )) return TRUE;
    *(SLID *)arg = license->file;
    return FALSE;
}

/* Identify a key with pidgenx.dll and the key configuration among the installed licenses. */
static HRESULT identify_key( const WCHAR *key_text, BYTE *dpid4, SLID *sku, SLID *pkey )
{
    HRESULT (WINAPI *pPidGenX)( const WCHAR *, const WCHAR *, const WCHAR *, const WCHAR *, WCHAR *, BYTE *, BYTE * );
    WCHAR path[MAX_PATH], pid[64], id_text[40];
    char id_a[40];
    BYTE dpid3[0xa4];
    SLID config = { 0 };
    HMODULE module;
    HRESULT hr;

    for_each_license( find_config_callback, &config );
    if (is_null_id( &config ) || !license_path( &config, path, FALSE ))
    {
        WARN( "no key configuration installed\n" );
        return SL_E_INVALID_PRODUCT_KEY;
    }
    if (!(module = LoadLibraryW( L"pidgenx.dll" )) || !(pPidGenX = (void *)GetProcAddress( module, "PidGenX" )))
        return SL_E_INVALID_PRODUCT_KEY;
    memset( dpid3, 0, sizeof(dpid3) );
    memset( dpid4, 0, DPID4_SIZE );
    *(DWORD *)dpid3 = sizeof(dpid3);
    *(DWORD *)dpid4 = DPID4_SIZE;
    /* ponytail: no product code is known here ("00000"); take it from the product's license
     * if a program is found to read it out of the extended product id. */
    hr = pPidGenX( key_text, path, L"00000", NULL, pid, dpid3, dpid4 );
    FreeLibrary( module );
    if (FAILED( hr ))
    {
        WARN( "key not identified, %#lx\n", hr );
        return SL_E_INVALID_PRODUCT_KEY;
    }
    swprintf( id_text, ARRAY_SIZE(id_text), L"%s", (WCHAR *)(dpid4 + DPID4_ACT_ID) );
    WideCharToMultiByte( CP_ACP, 0, id_text, -1, id_a, sizeof(id_a), NULL, NULL );
    if (!parse_id( id_a, sku )) return SL_E_INVALID_PRODUCT_KEY;
    memcpy( pkey, dpid4 + DPID4_KEY_HASH, sizeof(*pkey) );
    return S_OK;
}

/***********************************************************************
 *      SLInstallProofOfPurchase (sppc.@)
 */
HRESULT WINAPI SLInstallProofOfPurchase( HSLC handle, const WCHAR *algorithm, const WCHAR *key_text, UINT size,
                                         BYTE *data, SLID *pkey_id )
{
    WCHAR sub[64], id[40], partial[8] = L"";
    BYTE dpid4[DPID4_SIZE];
    SLID sku, pkey;
    ULONGLONG start;
    DWORD len;
    FILETIME ft;
    HRESULT hr;
    HKEY key;

    TRACE( "(%p %s %p %u %p %p)\n", handle, debugstr_w(algorithm), key_text, size, data, pkey_id );

    if (!key_text || !pkey_id) return E_INVALIDARG;
    if (size) FIXME( "%u bytes of key data ignored\n", size );
    if (FAILED( hr = identify_key( key_text, dpid4, &sku, &pkey ))) return hr;

    format_id( &pkey, id );
    swprintf( sub, ARRAY_SIZE(sub), L"ProductKeys\\%s", id );
    if (!(key = open_store_key( sub, TRUE ))) return E_ACCESSDENIED;
    len = wcslen( key_text );
    if (len >= 5) wcscpy( partial, key_text + len - 5 );
    RegSetValueExW( key, L"Sku", 0, REG_BINARY, (BYTE *)&sku, sizeof(sku) );
    RegSetValueExW( key, L"DigitalPID2", 0, REG_BINARY, dpid4, sizeof(dpid4) );
    RegSetValueExW( key, L"PartialProductKey", 0, REG_SZ, (BYTE *)partial, sizeof(partial) );
    RegSetValueExW( key, L"Channel", 0, REG_SZ, dpid4 + DPID4_KEY_TYPE,
                    (wcslen( (WCHAR *)(dpid4 + DPID4_KEY_TYPE) ) + 1) * sizeof(WCHAR) );
    RegCloseKey( key );

    /* the grace period starts with the product's first key and never starts again */
    if ((key = open_store_key( L"GraceStart", TRUE )))
    {
        format_id( &sku, id );
        len = sizeof(start);
        if (RegQueryValueExW( key, id, NULL, NULL, (BYTE *)&start, &len ))
        {
            GetSystemTimeAsFileTime( &ft );
            start = ((ULONGLONG)ft.dwHighDateTime << 32) | ft.dwLowDateTime;
            RegSetValueExW( key, id, 0, REG_QWORD, (BYTE *)&start, sizeof(start) );
        }
        RegCloseKey( key );
    }
    *pkey_id = pkey;
    TRACE( "key %s for product %s\n", debugstr_guid(&pkey), debugstr_guid(&sku) );
    return S_OK;
}

/***********************************************************************
 *      SLUninstallProofOfPurchase (sppc.@)
 */
HRESULT WINAPI SLUninstallProofOfPurchase( HSLC handle, const SLID *pkey_id )
{
    WCHAR path[128], id[40];

    TRACE( "(%p %s)\n", handle, debugstr_guid(pkey_id) );

    if (!pkey_id) return E_INVALIDARG;
    format_id( pkey_id, id );
    swprintf( path, ARRAY_SIZE(path), L"%s\\ProductKeys\\%s", store_key, id );
    return RegDeleteKeyExW( HKEY_LOCAL_MACHINE, path, KEY_WOW64_64KEY, 0 ) ? SL_E_PKEY_NOT_INSTALLED : S_OK;
}

/***********************************************************************
 *      SLGetPKeyId (sppc.@)
 */
HRESULT WINAPI SLGetPKeyId( HSLC handle, const WCHAR *algorithm, const WCHAR *key_text, UINT size, const BYTE *data,
                            SLID *pkey_id )
{
    BYTE dpid4[DPID4_SIZE];
    SLID sku, pkey;
    DWORD len = sizeof(sku);
    HRESULT hr;

    TRACE( "(%p %s %p %u %p %p)\n", handle, debugstr_w(algorithm), key_text, size, data, pkey_id );

    if (!key_text || !pkey_id) return E_INVALIDARG;
    if (FAILED( hr = identify_key( key_text, dpid4, &sku, &pkey ))) return hr;
    if (!read_key_value( &pkey, L"Sku", &sku, &len )) return SL_E_PKEY_NOT_INSTALLED;
    *pkey_id = pkey;
    return S_OK;
}

/***********************************************************************
 *      SLGetPKeyInformation (sppc.@)
 */
HRESULT WINAPI SLGetPKeyInformation( HSLC handle, const SLID *pkey_id, const WCHAR *name, SLDATATYPE *type,
                                     UINT *size, BYTE **value )
{
    BYTE data[DPID4_SIZE];
    DWORD len = sizeof(data);

    TRACE( "(%p %s %s %p %p %p)\n", handle, debugstr_guid(pkey_id), debugstr_w(name), type, size, value );

    if (!pkey_id || !name) return E_INVALIDARG;
    if (!wcsicmp( name, L"ProductSkuId" )) name = L"Sku";
    if (!read_key_value( pkey_id, name, data, &len ))
    {
        len = sizeof(data);
        if (!read_key_value( pkey_id, L"Sku", data, &len )) return SL_E_NOT_SUPPORTED;  /* as Windows 11 */
        FIXME( "no key value %s\n", debugstr_w(name) );
        return SL_E_VALUE_NOT_FOUND;
    }
    return return_value( !wcsicmp( name, L"PartialProductKey" ) || !wcsicmp( name, L"Channel" ) ? SL_DATA_SZ
                         : SL_DATA_BINARY, data, len, type, size, value );
}

/***********************************************************************
 *      SLGetInstalledProductKeyIds (sppc.@)
 */
HRESULT WINAPI SLGetInstalledProductKeyIds( HSLC handle, const SLID *sku, UINT *count, SLID **ids )
{
    struct id_list list = { 0 };

    TRACE( "(%p %s %p %p)\n", handle, debugstr_guid(sku), count, ids );

    if (!count || !ids) return E_INVALIDARG;
    list_keys( sku, &list, NULL );
    return return_ids( &list, count, ids );
}

/***********************************************************************
 *      SLSetCurrentProductKey (sppc.@)
 */
HRESULT WINAPI SLSetCurrentProductKey( HSLC handle, const SLID *sku, const SLID *pkey_id )
{
    WCHAR id[40];
    SLID key_sku;
    DWORD len = sizeof(key_sku);
    HKEY key;

    TRACE( "(%p %s %s)\n", handle, debugstr_guid(sku), debugstr_guid(pkey_id) );

    if (!sku || !pkey_id) return E_INVALIDARG;
    if (!read_key_value( pkey_id, L"Sku", &key_sku, &len )) return SL_E_PKEY_NOT_INSTALLED;
    if (!(key = open_store_key( L"CurrentKeys", TRUE ))) return E_ACCESSDENIED;
    format_id( sku, id );
    RegSetValueExW( key, id, 0, REG_BINARY, (const BYTE *)pkey_id, sizeof(*pkey_id) );
    RegCloseKey( key );
    return S_OK;
}

struct sku_query
{
    const SLID *app, *sku;
    struct id_list list;
};

static BOOL sku_callback( const struct license *license, const char *start, const char *end, void *arg )
{
    struct sku_query *query = arg;

    if (query->app && memcmp( query->app, &license->app, sizeof(SLID) )) return TRUE;
    if (query->sku && memcmp( query->sku, &license->sku, sizeof(SLID) )) return TRUE;
    add_id( &query->list, &license->sku );
    return TRUE;
}

/***********************************************************************
 *      SLGetLicensingStatusInformation (sppc.@)
 */
HRESULT WINAPI SLGetLicensingStatusInformation( HSLC handle, const SLID *app, const SLID *product, LPCWSTR name,
                                                UINT *count, SL_LICENSING_STATUS **status )
{
    struct sku_query query = { app, product };
    UINT i;

    TRACE( "(%p %s %s %s %p %p)\n", handle, debugstr_guid(app), debugstr_guid(product), debugstr_w(name), count,
           status );

    if (!app || !count || !status) return E_INVALIDARG;
    for_each_license( sku_callback, &query );
    if (!query.list.count)
    {
        free( query.list.ids );
        return SL_E_PRODUCT_SKU_NOT_INSTALLED;
    }
    *count = query.list.count;
    if (!(*status = LocalAlloc( 0, query.list.count * sizeof(**status) )))
    {
        free( query.list.ids );
        return E_OUTOFMEMORY;
    }
    for (i = 0; i < query.list.count; i++)
    {
        sku_status( &query.list.ids[i], &(*status)[i] );
        TRACE( "%s: status %u, reason %#lx, grace %lu minutes\n", debugstr_guid(&query.list.ids[i]),
               (*status)[i].eStatus, (*status)[i].hrReason, (*status)[i].dwGraceTime );
    }
    free( query.list.ids );
    return S_OK;
}

/***********************************************************************
 *      SLConsumeRight (sppc.@)
 *
 * A right is granted for a product that is licensed or in its grace period.
 */
HRESULT WINAPI SLConsumeRight( HSLC handle, const SLID *app, const SLID *product, const WCHAR *right, void *reserved )
{
    struct sku_query query = { app, product };
    struct context *context = handle;
    HRESULT hr = SL_E_RIGHT_NOT_GRANTED;
    UINT i;

    TRACE( "(%p %s %s %s %p)\n", handle, debugstr_guid(app), debugstr_guid(product), debugstr_w(right), reserved );

    if (!context || !app) return E_INVALIDARG;
    for_each_license( sku_callback, &query );
    for (i = 0; i < query.list.count; i++)
    {
        SL_LICENSING_STATUS status;

        sku_status( &query.list.ids[i], &status );
        if (status.eStatus != SL_LICENSING_STATUS_LICENSED && status.eStatus != SL_LICENSING_STATUS_IN_GRACE_PERIOD)
        {
            if (hr == SL_E_RIGHT_NOT_GRANTED && status.hrReason != SL_E_PKEY_NOT_INSTALLED) hr = status.hrReason;
            continue;
        }
        context->consumed = TRUE;
        context->app = *app;
        context->sku = query.list.ids[i];
        hr = S_OK;
        break;
    }
    free( query.list.ids );
    TRACE( "-> %#lx\n", hr );
    return hr;
}

/***********************************************************************
 *      SLGetPolicyInformation (sppc.@)
 *
 * Policies of the product whose right was consumed through this handle.
 */
HRESULT WINAPI SLGetPolicyInformation( HSLC handle, const WCHAR *name, SLDATATYPE *type, UINT *size, BYTE **value )
{
    struct context *context = handle;
    struct info_query query = { SL_ID_PRODUCT_SKU };
    char name_a[128];
    DWORD number;

    TRACE( "(%p %s %p %p %p)\n", handle, debugstr_w(name), type, size, value );

    if (!context || !name || !size || !value) return E_INVALIDARG;
    if (!context->consumed) return SL_E_RIGHT_NOT_GRANTED;  /* as Windows 11 */
    WideCharToMultiByte( CP_UTF8, 0, name, -1, name_a, sizeof(name_a), NULL, NULL );
    name_a[sizeof(name_a) - 1] = 0;
    query.id = &context->sku;
    query.name = name_a;
    query.element = "sl:policyInt";
    for_each_license( info_callback, &query );
    if (query.found)
    {
        number = strtoul( query.text, NULL, 10 );
        return return_value( SL_DATA_DWORD, &number, sizeof(number), type, size, value );
    }
    query.element = "sl:policyStr";
    for_each_license( info_callback, &query );
    if (query.found) return return_text( query.text, type, size, value );
    TRACE( "no policy %s\n", debugstr_w(name) );
    return SL_E_VALUE_NOT_FOUND;
}

/***********************************************************************
 *      SLGetPolicyInformationDWORD (sppc.@)
 */
HRESULT WINAPI SLGetPolicyInformationDWORD( HSLC handle, const WCHAR *name, DWORD *value )
{
    SLDATATYPE type;
    BYTE *data;
    UINT size;
    HRESULT hr;

    if (!value) return E_INVALIDARG;
    if (FAILED( hr = SLGetPolicyInformation( handle, name, &type, &size, &data ))) return hr;
    if (type != SL_DATA_DWORD || size != sizeof(DWORD)) hr = SL_E_DATATYPE_MISMATCHED;
    else *value = *(DWORD *)data;
    LocalFree( data );
    return hr;
}

/***********************************************************************
 *      SLFireEvent (sppc.@)
 */
HRESULT WINAPI SLFireEvent( HSLC handle, const WCHAR *event, const SLID *app )
{
    FIXME( "(%p %s %s) no listeners\n", handle, debugstr_w(event), debugstr_guid(app) );
    return S_OK;
}

/***********************************************************************
 *      SLRegisterEvent (sppc.@)
 */
HRESULT WINAPI SLRegisterEvent( HSLC handle, const WCHAR *event, const SLID *app, HANDLE signal )
{
    FIXME( "(%p %s %s %p) never signaled\n", handle, debugstr_w(event), debugstr_guid(app), signal );
    return S_OK;
}

/***********************************************************************
 *      SLUnregisterEvent (sppc.@)
 */
HRESULT WINAPI SLUnregisterEvent( HSLC handle, const WCHAR *event, const SLID *app, HANDLE signal )
{
    TRACE( "(%p %s %s %p)\n", handle, debugstr_w(event), debugstr_guid(app), signal );
    return S_OK;
}

/***********************************************************************
 *      SLPersistApplicationPolicies (sppc.@)
 *
 * Nothing is saved: SLLoadApplicationPolicies works the policies out again from the
 * installed licenses and the product's state at that time.
 */
HRESULT WINAPI SLPersistApplicationPolicies( const SLID *app, const SLID *product, DWORD flags )
{
    TRACE( "(%s,%s,%lx)\n", wine_dbgstr_guid(app), wine_dbgstr_guid(product), flags );

    if (!app) return E_INVALIDARG;
    return S_OK;
}

/***********************************************************************
 *      SLLoadApplicationPolicies (sppc.@)
 */
HRESULT WINAPI SLLoadApplicationPolicies( const SLID *app, const SLID *product, DWORD flags, HSLC *policies )
{
    HSLC handle;
    HRESULT hr;

    TRACE( "(%s,%s,%lx,%p)\n", wine_dbgstr_guid(app), wine_dbgstr_guid(product), flags, policies );

    if (!app || !policies) return E_INVALIDARG;
    if (FAILED( hr = SLOpen( &handle ))) return hr;
    if (FAILED( hr = SLConsumeRight( handle, app, product, NULL, NULL )))
    {
        SLClose( handle );
        return hr;
    }
    *policies = handle;
    return S_OK;
}

/***********************************************************************
 *      SLGetApplicationPolicy (sppc.@)
 */
HRESULT WINAPI SLGetApplicationPolicy( HSLC policies, const WCHAR *name, SLDATATYPE *type, UINT *size, BYTE **value )
{
    return SLGetPolicyInformation( policies, name, type, size, value );
}

/***********************************************************************
 *      SLUnloadApplicationPolicies (sppc.@)
 */
HRESULT WINAPI SLUnloadApplicationPolicies( HSLC policies, DWORD flags )
{
    TRACE( "(%p,%lx)\n", policies, flags );
    return SLClose( policies );
}

/***********************************************************************
 *      SLSetAuthenticationData (sppc.@)
 *
 * A program gives a challenge here and expects SLGetAuthenticationResult to return an answer
 * that only the licensing service of Windows can make.  This store cannot make it and says so.
 */
HRESULT WINAPI SLSetAuthenticationData( HSLC handle, UINT size, const BYTE *data )
{
    FIXME( "(%p %u %p) no authentication\n", handle, size, data );
    return S_OK;
}

/***********************************************************************
 *      SLGetAuthenticationResult (sppc.@)
 */
HRESULT WINAPI SLGetAuthenticationResult( HSLC handle, UINT *size, BYTE **value )
{
    FIXME( "(%p %p %p) no authentication\n", handle, size, value );
    return E_NOTIMPL;
}

/*
 * Subject interface package (SIP) for application packages (.appx, .msix)
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
 * Windows checks the signature of a package in one module, AppxSip.dll, which wintrust asks like any other signature
 * provider; signtool, the package reader and the deployment service all go through WinVerifyTrust. This is the same
 * arrangement: WinVerifyTrust on a package finds this module through the SIP registry, and everything that wants to know
 * whether a package is signed asks WinVerifyTrust.
 *
 * What a signed package holds (measured on Windows 11, tests/uwp/signature): the member AppxSignature.p7x, the four bytes
 * "PKCX" and then a PKCS #7 SignedData. Its content is the SIP indirect data (the SIP GUID below and the digest
 * algorithm SHA-256) whose digest is "APPX" followed by tagged SHA-256 values:
 *
 *   AXPC  the file records of the archive, from its first byte to the start of the signature's record
 *   AXCD  the central directory without the signature's entry, followed by the end records as they would be without it
 *   AXCT  [Content_Types].xml (the uncompressed member)
 *   AXBM  AppxBlockMap.xml (uncompressed)
 *   AXCI  AppxMetadata/CodeIntegrity.cat (uncompressed), when the package has one
 *
 * Whether a file is a package of this subject at all follows what Windows answers: it must be a zip archive whose members
 * are all declared in [Content_Types].xml and listed in the block map with the size they have, and a signature must be its
 * last record and start with PKCX. Anything else is "not recognised" (TRUST_E_SUBJECT_FORM_UNKNOWN), a package without a
 * signature member is unsigned (TRUST_E_NOSIGNATURE), and a signature whose digests do not fit is TRUST_E_BAD_DIGEST.
 */

#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#define WIN32_NO_STATUS
#include "windef.h"
#include "winbase.h"
#include "winerror.h"
#include "wincrypt.h"
#include "bcrypt.h"
#include "mssip.h"
#include "wintrust.h"
#include "wine/debug.h"
#include <zlib.h>

WINE_DEFAULT_DEBUG_CHANNEL(appxsip);

static const GUID appx_subject = { 0x0ac5df4b, 0xce07, 0x4de2, { 0xb7, 0x6e, 0x23, 0xc8, 0x39, 0xa0, 0x9f, 0xd1 }};

#define MAX_ENTRIES      65535
#define MAX_NAME         512
#define MAX_META_SIZE    (64 << 20)   /* the largest metadata member that is read (manifest, block map, content types) */
#define DIGEST_SIZE      32

struct entry
{
    char      name[MAX_NAME];         /* the name as stored, percent escapes decoded */
    WORD      method;
    ULONGLONG csize, size, offset;    /* offset of the local header */
    DWORD     cd_pos, cd_len;         /* the record in the central directory, from its first byte */
};

struct package
{
    HANDLE         file, mapping;
    const BYTE    *base;
    ULONGLONG      length;
    struct entry  *entries;
    unsigned int   count;
    ULONGLONG      cd_offset, cd_size;
    BOOL           zip64;
    ULONGLONG      z64_record;        /* offset of the zip64 end record when there is one */
    int            signature;         /* index of AppxSignature.p7x, -1 without */
};

static WORD  rd16( const BYTE *p ) { return p[0] | (p[1] << 8); }
static DWORD rd32( const BYTE *p ) { return p[0] | (p[1] << 8) | (p[2] << 16) | ((DWORD)p[3] << 24); }
static ULONGLONG rd64( const BYTE *p ) { return rd32( p ) | ((ULONGLONG)rd32( p + 4 ) << 32); }

static void wr16( BYTE *p, WORD v ) { p[0] = v; p[1] = v >> 8; }
static void wr32( BYTE *p, DWORD v ) { wr16( p, v ); wr16( p + 2, v >> 16 ); }
static void wr64( BYTE *p, ULONGLONG v ) { wr32( p, (DWORD)v ); wr32( p + 4, (DWORD)(v >> 32) ); }

static void package_close( struct package *pkg )
{
    if (pkg->base) UnmapViewOfFile( pkg->base );
    if (pkg->mapping) CloseHandle( pkg->mapping );
    if (pkg->file && pkg->file != INVALID_HANDLE_VALUE) CloseHandle( pkg->file );
    free( pkg->entries );
    memset( pkg, 0, sizeof(*pkg) );
}

/* member names are stored with percent escapes for characters that a URI part name does not allow */
static void unescape_name( const BYTE *raw, unsigned int len, char *out )
{
    unsigned int i, n = 0;

    for (i = 0; i < len && n < MAX_NAME - 1; i++)
    {
        if (raw[i] == '%' && i + 2 < len && isxdigit( raw[i + 1] ) && isxdigit( raw[i + 2] ))
        {
            char hex[3] = { raw[i + 1], raw[i + 2], 0 };
            out[n++] = (char)strtoul( hex, NULL, 16 );
            i += 2;
        }
        else out[n++] = raw[i] == '\\' ? '/' : raw[i];
    }
    out[n] = 0;
}

/* a zip archive, as a signing tool writes one: zip64 end records, members up to the sizes of a package */
static BOOL package_open( struct package *pkg, const WCHAR *name, HANDLE handle )
{
    LARGE_INTEGER size;
    const BYTE *end, *dir;
    ULONGLONG pos, cd_end;
    unsigned int i, count;

    memset( pkg, 0, sizeof(*pkg) );
    pkg->signature = -1;
    if (handle && handle != INVALID_HANDLE_VALUE)
    {
        if (!DuplicateHandle( GetCurrentProcess(), handle, GetCurrentProcess(), &pkg->file, GENERIC_READ, FALSE, 0 )) return FALSE;
    }
    else if (name)
    {
        pkg->file = CreateFileW( name, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_DELETE, NULL, OPEN_EXISTING, 0, NULL );
        if (pkg->file == INVALID_HANDLE_VALUE) { pkg->file = NULL; return FALSE; }
    }
    else { SetLastError( ERROR_INVALID_PARAMETER ); return FALSE; }

    if (!GetFileSizeEx( pkg->file, &size ) || size.QuadPart < 22 || (ULONGLONG)size.QuadPart > (SIZE_T)-1 / 2) goto fail;
    pkg->length = size.QuadPart;
    if (!(pkg->mapping = CreateFileMappingW( pkg->file, NULL, PAGE_READONLY, 0, 0, NULL ))) goto fail;
    if (!(pkg->base = MapViewOfFile( pkg->mapping, FILE_MAP_READ, 0, 0, 0 ))) goto fail;

    /* the end record, searched backwards over a possible comment */
    for (pos = pkg->length - 22; ; pos--)
    {
        if (rd32( pkg->base + pos ) == 0x06054b50 && pos + 22 + rd16( pkg->base + pos + 20 ) == pkg->length) break;
        if (!pos || pkg->length - pos > 22 + 65535) goto fail;
    }
    end = pkg->base + pos;
    count = rd16( end + 10 );
    pkg->cd_size = rd32( end + 12 );
    pkg->cd_offset = rd32( end + 16 );
    if (pos >= 20 && rd32( end - 20 ) == 0x07064b50)
    {
        ULONGLONG record = rd64( end - 12 );

        if (record > pos - 56 || rd32( pkg->base + record ) != 0x06064b50) goto fail;
        pkg->zip64 = TRUE;
        pkg->z64_record = record;
        if (rd64( pkg->base + record + 24 ) > MAX_ENTRIES) goto fail;
        count = (unsigned int)rd64( pkg->base + record + 32 );
        pkg->cd_size = rd64( pkg->base + record + 40 );
        pkg->cd_offset = rd64( pkg->base + record + 48 );
    }
    else if (count == 0xffff || pkg->cd_size == 0xffffffff || pkg->cd_offset == 0xffffffff) goto fail;
    if (!count || count > MAX_ENTRIES || pkg->cd_offset > pos || pkg->cd_size > pos - pkg->cd_offset) goto fail;
    cd_end = pkg->cd_offset + pkg->cd_size;
    if (!(pkg->entries = calloc( count, sizeof(*pkg->entries) ))) goto fail;

    dir = pkg->base + pkg->cd_offset;
    pos = pkg->cd_offset;
    for (i = 0; i < count; i++)
    {
        struct entry *entry = &pkg->entries[i];
        unsigned int nlen, elen, clen;
        const BYTE *extra, *extra_end;
        BOOL need_size, need_csize, need_offset;

        if (cd_end - pos < 46 || rd32( dir ) != 0x02014b50) goto fail;
        nlen = rd16( dir + 28 );
        elen = rd16( dir + 30 );
        clen = rd16( dir + 32 );
        if (cd_end - pos < 46 + nlen + elen + clen) goto fail;
        entry->method = rd16( dir + 10 );
        entry->csize = rd32( dir + 20 );
        entry->size = rd32( dir + 24 );
        entry->offset = rd32( dir + 42 );
        need_size = entry->size == 0xffffffff;
        need_csize = entry->csize == 0xffffffff;
        need_offset = entry->offset == 0xffffffff;
        extra = dir + 46 + nlen;
        extra_end = extra + elen;
        while (extra_end - extra >= 4)
        {
            unsigned int id = rd16( extra ), len = rd16( extra + 2 );
            const BYTE *field = extra + 4;

            if (len > extra_end - field) goto fail;
            if (id == 1)
            {
                if (need_size && len >= 8) { entry->size = rd64( field ); field += 8; len -= 8; need_size = FALSE; }
                if (need_csize && len >= 8) { entry->csize = rd64( field ); field += 8; len -= 8; need_csize = FALSE; }
                if (need_offset && len >= 8) { entry->offset = rd64( field ); need_offset = FALSE; }
            }
            extra += 4 + rd16( extra + 2 );
        }
        if (need_size || need_csize || need_offset) goto fail;
        if (entry->offset > pkg->cd_offset || pkg->cd_offset - entry->offset < 30) goto fail;
        unescape_name( dir + 46, nlen, entry->name );
        entry->cd_pos = (DWORD)(pos - pkg->cd_offset);
        entry->cd_len = 46 + nlen + elen + clen;
        if (!_stricmp( entry->name, "AppxSignature.p7x" )) pkg->signature = i;
        dir += entry->cd_len;
        pos += entry->cd_len;
        pkg->count++;
    }
    return TRUE;

fail:
    TRACE( "not a zip archive (error %lu)\n", GetLastError() );
    if (!GetLastError()) SetLastError( ERROR_BAD_FORMAT );
    package_close( pkg );
    return FALSE;
}

static int find_member( const struct package *pkg, const char *name )
{
    unsigned int i;

    for (i = 0; i < pkg->count; i++)
        if (!_stricmp( pkg->entries[i].name, name )) return i;
    return -1;
}

static void *zlib_alloc( void *opaque, uInt items, uInt size )
{
    return calloc( items, size );
}

static void zlib_free( void *opaque, void *ptr )
{
    free( ptr );
}

/* the uncompressed data of a member, malloc'd with a terminating zero not counted in *size; NULL when damaged */
static BYTE *member_data( const struct package *pkg, int index, SIZE_T *size )
{
    const struct entry *entry = &pkg->entries[index];
    const BYTE *local = pkg->base + entry->offset, *data;
    ULONGLONG start;
    BYTE *out;

    if (entry->size > MAX_META_SIZE || entry->csize > pkg->length) return NULL;
    if (rd32( local ) != 0x04034b50) return NULL;
    start = entry->offset + 30 + rd16( local + 26 ) + rd16( local + 28 );
    if (start > pkg->cd_offset || pkg->cd_offset - start < entry->csize) return NULL;
    data = pkg->base + start;
    if (!(out = malloc( entry->size + 1 ))) return NULL;
    if (entry->method == 0)
    {
        if (entry->csize != entry->size) { free( out ); return NULL; }
        memcpy( out, data, entry->size );
    }
    else if (entry->method == 8)
    {
        z_stream stream;
        int ret;

        memset( &stream, 0, sizeof(stream) );
        stream.zalloc = zlib_alloc;   /* Wine's zlib is built without default allocators */
        stream.zfree = zlib_free;
        if (inflateInit2( &stream, -MAX_WBITS ) != Z_OK) { free( out ); return NULL; }
        stream.next_in = (Bytef *)data;
        stream.avail_in = (uInt)entry->csize;
        stream.next_out = out;
        stream.avail_out = (uInt)entry->size + 1;
        ret = inflate( &stream, Z_FINISH );
        inflateEnd( &stream );
        if (ret != Z_STREAM_END || stream.total_out != entry->size) { free( out ); return NULL; }
    }
    else { free( out ); return NULL; }
    out[entry->size] = 0;
    *size = (SIZE_T)entry->size;
    return out;
}

static BOOL sha256( const BYTE *data, ULONGLONG len, BYTE *out )
{
    BCRYPT_ALG_HANDLE alg = NULL;
    BCRYPT_HASH_HANDLE hash = NULL;
    BOOL ret = FALSE;

    if (BCryptOpenAlgorithmProvider( &alg, BCRYPT_SHA256_ALGORITHM, NULL, 0 )) return FALSE;
    if (!BCryptCreateHash( alg, &hash, NULL, 0, NULL, 0, 0 ))
    {
        /* in pieces: a package can be larger than a DWORD */
        while (len)
        {
            ULONG piece = len > (64 << 20) ? (64 << 20) : (ULONG)len;

            if (BCryptHashData( hash, (UCHAR *)data, piece, 0 )) break;
            data += piece;
            len -= piece;
        }
        ret = !len && !BCryptFinishHash( hash, out, DIGEST_SIZE, 0 );
        BCryptDestroyHash( hash );
    }
    BCryptCloseAlgorithmProvider( alg, 0 );
    return ret;
}

/* an attribute of the element that starts at tag (up to its '>'), whitespace around '=' allowed; copies at most size - 1 */
static BOOL xml_attribute( const char *tag, const char *tag_end, const char *name, char *out, size_t size )
{
    size_t len = strlen( name );
    const char *p = tag;

    while ((p = memchr( p, name[0], tag_end - p )))
    {
        const char *q = p + len;

        if ((p == tag || isspace( (unsigned char)p[-1] )) && q < tag_end && !strncmp( p, name, len ))
        {
            char quote;
            size_t n = 0;

            while (q < tag_end && isspace( (unsigned char)*q )) q++;
            if (q >= tag_end || *q != '=') { p++; continue; }
            q++;
            while (q < tag_end && isspace( (unsigned char)*q )) q++;
            if (q >= tag_end || (*q != '"' && *q != '\'')) { p++; continue; }
            quote = *q++;
            while (q < tag_end && *q != quote && n < size - 1) out[n++] = *q++;
            out[n] = 0;
            return q < tag_end && *q == quote;
        }
        p++;
    }
    return FALSE;
}

/* the next <element ...> of the given name at or after *pos; *tag and *tag_end bound its attributes */
static BOOL xml_next( const char **pos, const char *end, const char *element, const char **tag, const char **tag_end )
{
    size_t len = strlen( element );
    const char *p = *pos;

    while ((p = memchr( p, '<', end - p )))
    {
        if (end - p > len + 1 && !strncmp( p + 1, element, len ) && (isspace( (unsigned char)p[len + 1] ) || p[len + 1] == '/' || p[len + 1] == '>'))
        {
            const char *close = memchr( p, '>', end - p );

            if (!close) return FALSE;
            *tag = p + len + 1;
            *tag_end = close;
            *pos = close + 1;
            return TRUE;
        }
        p++;
    }
    return FALSE;
}

static BOOL is_metadata( const char *name )
{
    return !_stricmp( name, "[Content_Types].xml" ) || !_stricmp( name, "AppxBlockMap.xml" ) || !_stricmp( name, "AppxSignature.p7x" ) ||
           !_stricmp( name, "AppxMetadata/CodeIntegrity.cat" );
}

#define REJECT() do { TRACE( "rejected at line %d\n", __LINE__ ); goto done; } while (0)

/* every member has a content type (an override of its part, or a default for its extension), and every override names a
 * member; the block map lists every other member with the size it has, and nothing else */
static BOOL check_structure( const struct package *pkg )
{
    int types = find_member( pkg, "[Content_Types].xml" ), map = find_member( pkg, "AppxBlockMap.xml" );
    BYTE *types_data = NULL, *map_data = NULL;
    SIZE_T types_size, map_size;
    unsigned int i, listed = 0, files = 0;
    BOOL ret = FALSE;

    if (types < 0 || map < 0 || find_member( pkg, "AppxManifest.xml" ) < 0) return FALSE;
    if (!(types_data = member_data( pkg, types, &types_size )) || !(map_data = member_data( pkg, map, &map_size ))) REJECT();

    for (i = 0; i < pkg->count; i++)
    {
        const struct entry *entry = &pkg->entries[i];
        const char *pos = (char *)types_data, *end = pos + types_size, *tag, *tag_end;
        const char *dot = strrchr( entry->name, '.' );
        BOOL declared = FALSE;
        char value[MAX_NAME];

        if (!_stricmp( entry->name, "[Content_Types].xml" )) continue;
        while (!declared && xml_next( &pos, end, "Override", &tag, &tag_end ))
        {
            if (xml_attribute( tag, tag_end, "PartName", value, sizeof(value) ))
            {
                char decoded[MAX_NAME];
                unescape_name( (BYTE *)value, strlen( value ), decoded );
                declared = !_stricmp( decoded[0] == '/' ? decoded + 1 : decoded, entry->name );
            }
        }
        pos = (char *)types_data;
        while (!declared && dot && xml_next( &pos, end, "Default", &tag, &tag_end ))
            declared = xml_attribute( tag, tag_end, "Extension", value, sizeof(value) ) && !_stricmp( value, dot + 1 );
        if (!declared) REJECT();
        if (!is_metadata( entry->name )) files++;
    }
    {
        const char *pos = (char *)types_data, *end = pos + types_size, *tag, *tag_end;
        char value[MAX_NAME];

        while (xml_next( &pos, end, "Override", &tag, &tag_end ))
        {
            char decoded[MAX_NAME];

            if (!xml_attribute( tag, tag_end, "PartName", value, sizeof(value) )) REJECT();
            unescape_name( (BYTE *)value, strlen( value ), decoded );
            if (find_member( pkg, decoded[0] == '/' ? decoded + 1 : decoded ) < 0) REJECT();
        }
    }
    {
        const char *pos = (char *)map_data, *end = pos + map_size, *tag, *tag_end;
        char name[MAX_NAME], size_text[32], decoded[MAX_NAME];

        while (xml_next( &pos, end, "File", &tag, &tag_end ))
        {
            int index;

            if (!xml_attribute( tag, tag_end, "Name", name, sizeof(name) ) ||
                !xml_attribute( tag, tag_end, "Size", size_text, sizeof(size_text) )) REJECT();
            unescape_name( (BYTE *)name, strlen( name ), decoded );
            if ((index = find_member( pkg, decoded )) < 0 || is_metadata( decoded ) ||
                pkg->entries[index].size != _strtoui64( size_text, NULL, 10)) REJECT();
            listed++;
        }
    }
    ret = listed == files;
    if (!ret) TRACE( "the block map lists %u files, the package has %u\n", listed, files );

done:
    free( types_data );
    free( map_data );
    return ret;
}

/* a package of this subject: the archive, its structure, and a signature (when there is one) that comes last and is a
 * PKCS #7 message in a PKCX envelope */
static BOOL check_package( const struct package *pkg )
{
    unsigned int i;

    if (!check_structure( pkg )) return FALSE;
    if (pkg->signature >= 0)
    {
        const struct entry *sig = &pkg->entries[pkg->signature];
        BYTE *data;
        SIZE_T size;
        BOOL ok;

        for (i = 0; i < pkg->count; i++)
            if (pkg->entries[i].offset > sig->offset) return FALSE;
        if (!(data = member_data( pkg, pkg->signature, &size ))) return FALSE;
        ok = size > 4 && !memcmp( data, "PKCX", 4 ) && data[4] == 0x30;
        free( data );
        return ok;
    }
    return TRUE;
}

static BOOL open_subject( struct package *pkg, SIP_SUBJECTINFO *subject )
{
    if (!subject || (!subject->pwsFileName && (!subject->hFile || subject->hFile == INVALID_HANDLE_VALUE)))
    {
        SetLastError( ERROR_INVALID_PARAMETER );
        return FALSE;
    }
    if (!package_open( pkg, subject->pwsFileName, subject->hFile ))
    {
        SetLastError( TRUST_E_SUBJECT_FORM_UNKNOWN );
        return FALSE;
    }
    if (!check_package( pkg ))
    {
        package_close( pkg );
        SetLastError( TRUST_E_SUBJECT_FORM_UNKNOWN );
        return FALSE;
    }
    return TRUE;
}

/***********************************************************************
 *              DllRegisterServer (APPXSIP.@)
 */
HRESULT WINAPI DllRegisterServer(void)
{
    static WCHAR dll[] = L"APPXSIP.DLL";
    static WCHAR get[] = L"AppxSipGetSignedDataMsg";
    static WCHAR put[] = L"AppxSipPutSignedDataMsg";
    static WCHAR create[] = L"AppxSipCreateIndirectData";
    static WCHAR verify[] = L"AppxSipVerifyIndirectData";
    static WCHAR remove[] = L"AppxSipRemoveSignedDataMsg";
    static WCHAR supported[] = L"AppxSipIsFileSupportedName";
    SIP_ADD_NEWPROVIDER prov;

    memset( &prov, 0, sizeof(prov) );
    prov.cbStruct = sizeof(prov);
    prov.pwszDLLFileName = dll;
    prov.pgSubject = (GUID *)&appx_subject;
    prov.pwszGetFuncName = get;
    prov.pwszPutFuncName = put;
    prov.pwszCreateFuncName = create;
    prov.pwszVerifyFuncName = verify;
    prov.pwszRemoveFuncName = remove;
    prov.pwszIsFunctionNameFmt2 = supported;
    return CryptSIPAddProvider( &prov ) ? S_OK : S_FALSE;
}

/***********************************************************************
 *              DllUnregisterServer (APPXSIP.@)
 */
HRESULT WINAPI DllUnregisterServer(void)
{
    CryptSIPRemoveProvider( (GUID *)&appx_subject );
    return S_OK;
}

/***********************************************************************
 *              AppxSipIsFileSupportedName (APPXSIP.@)
 */
BOOL WINAPI AppxSipIsFileSupportedName( WCHAR *name, GUID *subject )
{
    struct package pkg;
    BOOL ret;

    TRACE( "(%s, %p)\n", debugstr_w( name ), subject );

    if (!name || !subject) return FALSE;
    if (!package_open( &pkg, name, NULL )) return FALSE;
    if ((ret = check_package( &pkg ))) *subject = appx_subject;
    package_close( &pkg );
    return ret;
}

/***********************************************************************
 *              AppxSipGetSignedDataMsg (APPXSIP.@)
 */
BOOL WINAPI AppxSipGetSignedDataMsg( SIP_SUBJECTINFO *subject, DWORD *encoding, DWORD index, DWORD *size, BYTE *buffer )
{
    struct package pkg;
    BYTE *data;
    SIZE_T len;
    BOOL ret = FALSE;

    TRACE( "(%p %p %ld %p %p)\n", subject, encoding, index, size, buffer );

    if (!size || index) { SetLastError( index ? ERROR_NO_MORE_ITEMS : ERROR_INVALID_PARAMETER ); return FALSE; }
    if (!open_subject( &pkg, subject )) return FALSE;
    if (pkg.signature < 0) SetLastError( TRUST_E_NOSIGNATURE );
    else if (!(data = member_data( &pkg, pkg.signature, &len ))) SetLastError( TRUST_E_SUBJECT_FORM_UNKNOWN );
    else
    {
        len -= 4;
        if (!buffer) { *size = len; ret = TRUE; }
        else if (*size < len) { *size = len; SetLastError( ERROR_INSUFFICIENT_BUFFER ); }
        else
        {
            memcpy( buffer, data + 4, len );
            *size = len;
            if (encoding) *encoding = X509_ASN_ENCODING | PKCS_7_ASN_ENCODING;
            ret = TRUE;
        }
        free( data );
    }
    package_close( &pkg );
    return ret;
}

/***********************************************************************
 *              AppxSipPutSignedDataMsg (APPXSIP.@)
 */
BOOL WINAPI AppxSipPutSignedDataMsg( SIP_SUBJECTINFO *subject, DWORD encoding, DWORD *index, DWORD size, BYTE *data )
{
    FIXME( "signing a package is not supported\n" );
    SetLastError( ERROR_CALL_NOT_IMPLEMENTED );
    return FALSE;
}

/***********************************************************************
 *              AppxSipRemoveSignedDataMsg (APPXSIP.@)
 */
BOOL WINAPI AppxSipRemoveSignedDataMsg( SIP_SUBJECTINFO *subject, DWORD index )
{
    FIXME( "removing a package signature is not supported\n" );
    SetLastError( ERROR_CALL_NOT_IMPLEMENTED );
    return FALSE;
}

/***********************************************************************
 *              AppxSipCreateIndirectData (APPXSIP.@)
 */
BOOL WINAPI AppxSipCreateIndirectData( SIP_SUBJECTINFO *subject, DWORD *size, SIP_INDIRECT_DATA *indirect )
{
    FIXME( "signing a package is not supported\n" );
    SetLastError( ERROR_CALL_NOT_IMPLEMENTED );
    return FALSE;
}

/* the digest the signature should hold for this package: APPX and the tagged values; *len is set */
static BOOL package_digest( const struct package *pkg, BOOL with_catalog, BYTE *out, DWORD *len )
{
    static const char tags[4][4] = { { 'A','X','P','C' }, { 'A','X','C','D' }, { 'A','X','C','T' }, { 'A','X','B','M' } };
    static const char *members[] = { NULL, NULL, "[Content_Types].xml", "AppxBlockMap.xml" };
    const struct entry *sig = &pkg->entries[pkg->signature];
    BYTE *dir = NULL, *data;
    SIZE_T dir_len = 0, size;
    unsigned int i, n = 0;
    BYTE *p = out;
    BOOL ret = FALSE;

    memcpy( p, "APPX", 4 );
    p += 4;

    for (i = 0; i < 4 + (with_catalog ? 1 : 0); i++)
    {
        BYTE value[DIGEST_SIZE];

        if (i == 0)
        {
            if (!sha256( pkg->base, sig->offset, value )) goto done;
        }
        else if (i == 1)
        {
            /* the central directory without the signature's entry and the end records as they would then be */
            BYTE tail[56 + 20 + 22], *t = tail;
            ULONGLONG off = sig->offset;

            if (!(dir = malloc( pkg->cd_size + sizeof(tail) ))) goto done;
            for (n = 0; n < pkg->count; n++)
            {
                if ((int)n == pkg->signature) continue;
                memcpy( dir + dir_len, pkg->base + pkg->cd_offset + pkg->entries[n].cd_pos, pkg->entries[n].cd_len );
                dir_len += pkg->entries[n].cd_len;
            }
            n = pkg->count - 1;
            if (pkg->zip64)
            {
                memcpy( t, pkg->base + pkg->z64_record, 56 );
                wr64( t + 24, n );
                wr64( t + 32, n );
                wr64( t + 40, dir_len );
                wr64( t + 48, off );
                t += 56;
                wr32( t, 0x07064b50 ); wr32( t + 4, 0 ); wr64( t + 8, off + dir_len ); wr32( t + 16, 1 );
                t += 20;
                wr32( t, 0x06054b50 ); wr16( t + 4, 0 ); wr16( t + 6, 0 ); wr16( t + 8, 0xffff ); wr16( t + 10, 0xffff );
                wr32( t + 12, 0xffffffff ); wr32( t + 16, 0xffffffff ); wr16( t + 20, 0 );
                t += 22;
            }
            else
            {
                wr32( t, 0x06054b50 ); wr16( t + 4, 0 ); wr16( t + 6, 0 ); wr16( t + 8, n ); wr16( t + 10, n );
                wr32( t + 12, (DWORD)dir_len ); wr32( t + 16, (DWORD)off ); wr16( t + 20, 0 );
                t += 22;
            }
            memcpy( dir + dir_len, tail, t - tail );
            if (!sha256( dir, dir_len + (t - tail), value )) goto done;
        }
        else
        {
            int index = find_member( pkg, i == 4 ? "AppxMetadata/CodeIntegrity.cat" : members[i] );

            if (index < 0 || !(data = member_data( pkg, index, &size ))) goto done;
            if (!sha256( data, size, value )) { free( data ); goto done; }
            free( data );
        }
        memcpy( p, i < 4 ? tags[i] : "AXCI", 4 );
        memcpy( p + 4, value, DIGEST_SIZE );
        p += 4 + DIGEST_SIZE;
    }
    *len = p - out;
    ret = TRUE;

done:
    free( dir );
    return ret;
}

/***********************************************************************
 *              AppxSipVerifyIndirectData (APPXSIP.@)
 */
BOOL WINAPI AppxSipVerifyIndirectData( SIP_SUBJECTINFO *subject, SIP_INDIRECT_DATA *indirect )
{
    BYTE expected[4 + 5 * (4 + DIGEST_SIZE)];
    DWORD len;
    struct package pkg;
    BOOL ret = FALSE, with_catalog;

    TRACE( "(%p %p)\n", subject, indirect );

    if (!subject || !indirect || !indirect->DigestAlgorithm.pszObjId || !indirect->Digest.pbData)
    {
        SetLastError( ERROR_INVALID_PARAMETER );
        return FALSE;
    }
    if (strcmp( indirect->DigestAlgorithm.pszObjId, szOID_NIST_sha256 ))
    {
        SetLastError( NTE_BAD_ALGID );
        return FALSE;
    }
    if (!open_subject( &pkg, subject )) return FALSE;
    if (pkg.signature < 0) { SetLastError( TRUST_E_NOSIGNATURE ); goto done; }
    /* a package with a code integrity catalog has its value in the digest, and the other way round */
    with_catalog = find_member( &pkg, "AppxMetadata/CodeIntegrity.cat" ) >= 0;
    if (!package_digest( &pkg, with_catalog, expected, &len ))
    {
        SetLastError( TRUST_E_SUBJECT_FORM_UNKNOWN );
        goto done;
    }
    if (len != indirect->Digest.cbData || memcmp( expected, indirect->Digest.pbData, len ))
    {
        SetLastError( TRUST_E_BAD_DIGEST );
        goto done;
    }
    ret = TRUE;

done:
    package_close( &pkg );
    return ret;
}

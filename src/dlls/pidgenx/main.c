/*
 * Product key identification (pidgenx.dll)
 *
 * Copyright 2026 WineExplorer contributors
 *
 * Office setup (2013 and later, the MSI editions) loads system32\pidgenx.dll to identify
 * its product key: it decodes the key, looks its group up in the product's own key
 * configuration file and returns the product ids.  Only keys of the 2009 format (25
 * characters with one 'N') are handled.  Nothing here activates or licenses a product:
 * the key's secret part is not verified, only its checksum and its group and range.
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
#include <time.h>

#include "windef.h"
#include "winbase.h"
#include "winnls.h"
#include "winternl.h"
#include "wincrypt.h"
#include "bcrypt.h"

#include "wine/debug.h"

WINE_DEFAULT_DEBUG_CHANNEL(pidgenx);

/* What the native DLL returns for a key that is malformed or not in the configuration. */
#define PIDGENX_E_INVALID_KEY ((HRESULT)0x8a010101)

/* Offsets in the two binary product id blocks.  Layout and contents were found by calling the
 * DLL an Office 2016 medium carries and comparing the output (scripts/lab/office/pidgenx2-probe.c). */
#define DPID3_SIZE      0xa4
#define DPID3_PID       0x08    /* char[24] */
#define DPID3_GROUP     0x20    /* DWORD */
#define DPID3_SKU       0x24    /* char[16] */
#define DPID3_KEY       0x34    /* BYTE[16] */
#define DPID3_TIME      0x48    /* DWORD, seconds since 1970 */
#define DPID3_RANDOM    0x4c    /* DWORD */
#define DPID3_LICENSE   0x50    /* DWORD */
#define DPID3_CRC       0xa0    /* DWORD over all before it */
#define DPID4_SIZE      0x4f8
#define DPID4_EXT_PID   0x008   /* WCHAR[64] */
#define DPID4_ACT_ID    0x088   /* WCHAR[64] */
#define DPID4_EDITION   0x118   /* WCHAR[260] */
#define DPID4_KEY       0x328   /* BYTE[16] */
#define DPID4_KEY_HASH  0x338   /* SHA-256 of the key bytes */
#define DPID4_HASH      0x358   /* SHA-256 of the block with this field zero */
#define DPID4_PART      0x378   /* WCHAR[64] */
#define DPID4_KEY_TYPE  0x3f8   /* WCHAR[64] */
#define DPID4_EULA      0x478   /* WCHAR[64] */

/* The fifth argument of PidGenX2, as Office 2016 setup passes it (32-bit). */
struct pidgenx2_info
{
    DWORD  size;            /* 0x14 */
    DWORD  unknown1;        /* 0 */
    DWORD *groups;          /* key groups of the product */
    DWORD  group_count;     /* E_INVALIDARG when 0 */
    DWORD  unknown2;        /* not written by the native DLL */
};

struct key_info
{
    BYTE  raw[16];      /* the key as a little endian number */
    DWORD group;        /* bits 0-19 */
    DWORD serial;       /* bits 20-49 */
};

struct config_info
{
    char act_id[40];
    char edition[64];
    char key_type[64];
    char part[64];
    char eula[64];
};

static DWORD key_bits( const BYTE *raw, unsigned int first, unsigned int count )
{
    DWORD value = 0;
    unsigned int i;

    for (i = 0; i < count; i++)
        if (raw[(first + i) / 8] & (1 << ((first + i) % 8))) value |= 1u << i;
    return value;
}

static DWORD key_crc( const BYTE *data, unsigned int len )
{
    DWORD crc = ~0u;
    unsigned int i, bit;

    for (i = 0; i < len; i++)
    {
        crc ^= (DWORD)data[i] << 24;
        for (bit = 0; bit < 8; bit++) crc = (crc & 0x80000000) ? (crc << 1) ^ 0x04c11db7 : crc << 1;
    }
    return crc;
}

/* Decode a key of the 2009 format.  The position of the 'N' is the first base 24 digit. */
static BOOL decode_key( const WCHAR *text, struct key_info *info )
{
    static const char alphabet[] = "BCDFGHJKMPQRTVWXY2346789";
    BYTE digits[25], check[16];
    unsigned int count = 0, i, j, n_pos = ~0u;
    DWORD stored;

    for (; *text; text++)
    {
        WCHAR ch = *text;
        const char *p;

        if (ch == '-') continue;
        if (ch >= 'a' && ch <= 'z') ch -= 'a' - 'A';
        if (count == 25) return FALSE;
        if (ch == 'N')
        {
            if (n_pos != ~0u) return FALSE;
            n_pos = count;
            digits[count++] = 0;
            continue;
        }
        if (ch > 0x7f || !(p = strchr( alphabet, (char)ch ))) return FALSE;
        digits[count++] = p - alphabet;
    }
    if (count != 25 || n_pos == ~0u) return FALSE;

    memset( info->raw, 0, sizeof(info->raw) );
    info->raw[0] = n_pos;
    for (i = 0; i < 25; i++)
    {
        unsigned int carry;

        if (i == n_pos) continue;
        carry = digits[i];
        for (j = 0; j < sizeof(info->raw); j++)
        {
            carry += info->raw[j] * 24;
            info->raw[j] = carry;
            carry >>= 8;
        }
        if (carry) return FALSE;
    }

    /* bits 103-112 are the checksum of the number with those bits clear */
    memcpy( check, info->raw, sizeof(check) );
    check[12] &= 0x7f;
    check[13] = 0;
    check[14] &= 0xfe;
    stored = key_bits( info->raw, 103, 10 );
    if ((~key_crc( check, sizeof(check) ) & 0x3ff) != stored) return FALSE;

    info->group = key_bits( info->raw, 0, 20 );
    info->serial = key_bits( info->raw, 20, 30 );
    return TRUE;
}

/* Copy the text of the first <pkc:NAME> element between start and end. */
/* ponytail: plain text search with the "pkc" prefix the files use, no XML parser; use xmllite if a
 * key configuration file ever declares another prefix. */
static BOOL element_text( const char *start, const char *end, const char *name, char *buffer, size_t size )
{
    char tag[48];
    const char *p, *q;

    snprintf( tag, sizeof(tag), "<pkc:%s>", name );
    if (!(p = strstr( start, tag )) || p >= end) return FALSE;
    p += strlen( tag );
    if (!(q = strchr( p, '<' )) || q > end || (size_t)(q - p) >= size) return FALSE;
    memcpy( buffer, p, q - p );
    buffer[q - p] = 0;
    return TRUE;
}

/* Find the configuration of the key's group and the key range that holds its serial number. */
static BOOL find_config( const char *xml, const struct key_info *key, struct config_info *config )
{
    const char *p, *end;
    char text[64];

    for (p = xml; (p = strstr( p, "<pkc:Configuration>" )); p = end)
    {
        if (!(end = strstr( p, "</pkc:Configuration>" ))) return FALSE;
        if (!element_text( p, end, "RefGroupId", text, sizeof(text) ) || strtoul( text, NULL, 10 ) != key->group)
            continue;
        if (!element_text( p, end, "ActConfigId", config->act_id, sizeof(config->act_id) ) ||
            !element_text( p, end, "EditionId", config->edition, sizeof(config->edition) ) ||
            !element_text( p, end, "ProductKeyType", config->key_type, sizeof(config->key_type) ))
            return FALSE;
        break;
    }
    if (!p) return FALSE;

    for (p = xml; (p = strstr( p, "<pkc:KeyRange>" )); p = end)
    {
        DWORD first, last;

        if (!(end = strstr( p, "</pkc:KeyRange>" ))) return FALSE;
        if (!element_text( p, end, "RefActConfigId", text, sizeof(text) ) || stricmp( text, config->act_id ))
            continue;
        if (!element_text( p, end, "IsValid", text, sizeof(text) ) || stricmp( text, "true" )) continue;
        if (!element_text( p, end, "Start", text, sizeof(text) )) continue;
        first = strtoul( text, NULL, 10 );
        if (!element_text( p, end, "End", text, sizeof(text) )) continue;
        last = strtoul( text, NULL, 10 );
        if (key->serial < first || key->serial > last) continue;
        if (!element_text( p, end, "PartNumber", config->part, sizeof(config->part) )) config->part[0] = 0;
        if (!element_text( p, end, "EulaType", config->eula, sizeof(config->eula) )) config->eula[0] = 0;
        return TRUE;
    }
    return FALSE;
}

/* Read the key configuration out of its signed license file: base64 in the "pkeyConfigData" element. */
static char *load_config( const WCHAR *path )
{
    static const char marker[] = "name=\"pkeyConfigData\">";
    char *file = NULL, *xml = NULL, *p, *q;
    DWORD size, read, len;
    HANDLE handle;

    handle = CreateFileW( path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL );
    if (handle == INVALID_HANDLE_VALUE) return NULL;
    size = GetFileSize( handle, NULL );
    if (size != INVALID_FILE_SIZE && (file = malloc( size + 1 )) && ReadFile( handle, file, size, &read, NULL ) &&
        read == size)
    {
        file[size] = 0;
        if ((p = strstr( file, marker )) && (q = strchr( p += strlen( marker ), '<' )) &&
            CryptStringToBinaryA( p, q - p, CRYPT_STRING_BASE64, NULL, &len, NULL, NULL ) &&
            (xml = malloc( len + 1 )))
        {
            if (CryptStringToBinaryA( p, q - p, CRYPT_STRING_BASE64, (BYTE *)xml, &len, NULL, NULL )) xml[len] = 0;
            else
            {
                free( xml );
                xml = NULL;
            }
        }
    }
    CloseHandle( handle );
    free( file );
    return xml;
}

static void put_text( BYTE *block, unsigned int offset, unsigned int chars, const char *text )
{
    MultiByteToWideChar( CP_ACP, 0, text, -1, (WCHAR *)(block + offset), chars );
    ((WCHAR *)(block + offset))[chars - 1] = 0;
}

static void sha256( const BYTE *data, ULONG size, BYTE *digest )
{
    BCRYPT_ALG_HANDLE alg;

    if (BCryptOpenAlgorithmProvider( &alg, BCRYPT_SHA256_ALGORITHM, NULL, 0 )) return;
    BCryptHash( alg, NULL, 0, (UCHAR *)data, size, digest, 32 );
    BCryptCloseAlgorithmProvider( alg, 0 );
}

/* pid receives the product id as text (24 characters), dpid3 and dpid4 the binary product ids;
 * the caller stores their sizes in their first DWORD. */
static HRESULT generate_ids( const WCHAR *key_text, const WCHAR *config_path, const WCHAR *mpc,
                             WCHAR *pid, BYTE *dpid3, BYTE *dpid4 )
{
    struct config_info config;
    struct key_info key;
    OSVERSIONINFOW version = { sizeof(version) };
    char digits[16], pid_text[24], ext_pid[64], act_id[40], mpc_text[16];
    static const unsigned short month_start[12] = { 0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334 };
    SYSTEMTIME now;
    unsigned int i, day;
    BYTE stored[16];
    char *xml;

    if (!key_text || !config_path || !pid || !dpid3 || !dpid4) return E_INVALIDARG;
    if (!decode_key( key_text, &key ))
    {
        WARN( "not a valid key of the 2009 format\n" );
        return PIDGENX_E_INVALID_KEY;
    }
    if (!(xml = load_config( config_path )))
    {
        WARN( "no key configuration in %s\n", debugstr_w(config_path) );
        return HRESULT_FROM_WIN32( ERROR_FILE_NOT_FOUND );
    }
    if (!find_config( xml, &key, &config ))
    {
        WARN( "group %lu serial %lu is not in %s\n", key.group, key.serial, debugstr_w(config_path) );
        free( xml );
        return PIDGENX_E_INVALID_KEY;
    }
    free( xml );
    TRACE( "group %lu, edition %s, type %s\n", key.group, debugstr_a(config.edition), debugstr_a(config.key_type) );

    /* product id: group (6 digits) and serial (9 digits) in groups of five, then an installation part */
    snprintf( digits, sizeof(digits), "%06lu%09lu", key.group, key.serial );
    snprintf( pid_text, sizeof(pid_text), "%.5s-%.5s-%.5s-AA%03lu", digits, digits + 5, digits + 10,
              GetTickCount() % 1000 );

    GetSystemTime( &now );
    day = month_start[now.wMonth - 1] + now.wDay;
    if (now.wMonth > 2 && !(now.wYear % 4) && (now.wYear % 100 || !(now.wYear % 400))) day++;
    GetVersionExW( &version );
    WideCharToMultiByte( CP_ACP, 0, mpc ? mpc : L"", -1, mpc_text, sizeof(mpc_text), NULL, NULL );
    mpc_text[sizeof(mpc_text) - 1] = 0;
    snprintf( ext_pid, sizeof(ext_pid), "%s-%05lu-%03lu-%06lu-03-%lu-%lu.0000-%03u%04u", mpc_text, key.group,
              key.serial / 1000000, key.serial % 1000000, (DWORD)GetSystemDefaultLCID(),
              version.dwBuildNumber, day, now.wYear );

    /* activation id without braces, lower case */
    for (i = 0; config.act_id[i + 1] && config.act_id[i + 1] != '}' && i < sizeof(act_id) - 1; i++)
    {
        char ch = config.act_id[i + 1];
        act_id[i] = (ch >= 'A' && ch <= 'Z') ? ch + 'a' - 'A' : ch;
    }
    act_id[i] = 0;

    memcpy( stored, key.raw, sizeof(stored) );
    stored[14] |= 0x08;  /* set in the stored copy of a key of this format */

    MultiByteToWideChar( CP_ACP, 0, pid_text, -1, pid, 24 );
    if (*(DWORD *)dpid3 >= DPID3_SIZE)
    {
        memset( dpid3 + 4, 0, DPID3_SIZE - 4 );
        *(DWORD *)dpid3 = DPID3_SIZE;
        *(WORD *)(dpid3 + 4) = 3;
        lstrcpynA( (char *)dpid3 + DPID3_PID, pid_text, 24 );
        *(DWORD *)(dpid3 + DPID3_GROUP) = key.group;
        lstrcpynA( (char *)dpid3 + DPID3_SKU, config.part, 16 );
        memcpy( dpid3 + DPID3_KEY, stored, sizeof(stored) );
        *(DWORD *)(dpid3 + DPID3_TIME) = (DWORD)time( NULL );
        *(DWORD *)(dpid3 + DPID3_RANDOM) = GetTickCount() * 2654435761u;
        /* ponytail: 3 is what a volume key gives; other key types were not compared and get 0 */
        if (!strncmp( config.key_type, "Volume", 6 )) *(DWORD *)(dpid3 + DPID3_LICENSE) = 3;
        *(DWORD *)(dpid3 + DPID3_CRC) = ~RtlComputeCrc32( 0, dpid3, DPID3_CRC );
    }
    if (*(DWORD *)dpid4 >= DPID4_SIZE)
    {
        memset( dpid4 + 4, 0, DPID4_SIZE - 4 );
        *(DWORD *)dpid4 = DPID4_SIZE;
        *(DWORD *)(dpid4 + 4) = 4;
        put_text( dpid4, DPID4_EXT_PID, 64, ext_pid );
        put_text( dpid4, DPID4_ACT_ID, 64, act_id );
        put_text( dpid4, DPID4_EDITION, 260, config.edition );
        memcpy( dpid4 + DPID4_KEY, stored, sizeof(stored) );
        put_text( dpid4, DPID4_PART, 64, config.part );
        put_text( dpid4, DPID4_KEY_TYPE, 64, config.key_type );
        put_text( dpid4, DPID4_EULA, 64, config.eula );
        sha256( stored, sizeof(stored), dpid4 + DPID4_KEY_HASH );
        sha256( dpid4, DPID4_SIZE, dpid4 + DPID4_HASH );
    }
    return S_OK;
}

/***********************************************************************
 *      PidGenX (pidgenx.@)
 */
HRESULT WINAPI PidGenX( const WCHAR *key, const WCHAR *config_path, const WCHAR *mpc, const WCHAR *oem_id,
                        WCHAR *pid, BYTE *dpid3, BYTE *dpid4 )
{
    TRACE( "key %p, config %s, mpc %s, oem %s, %p, %p, %p\n", key, debugstr_w(config_path),
           debugstr_w(mpc), debugstr_w(oem_id), pid, dpid3, dpid4 );

    return generate_ids( key, config_path, mpc, pid, dpid3, dpid4 );
}

/***********************************************************************
 *      PidGenX2 (pidgenx.@)
 *
 * Office 2016 setup calls this one.  The native DLL accepts a key whose group is not in the
 * list, so the list is only checked for presence.
 */
HRESULT WINAPI PidGenX2( const WCHAR *key, const WCHAR *config_path, const WCHAR *mpc, const WCHAR *oem_id,
                         struct pidgenx2_info *info, WCHAR *pid, BYTE *dpid3, BYTE *dpid4 )
{
    TRACE( "key %p, config %s, mpc %s, oem %s, info %p, %p, %p, %p\n", key, debugstr_w(config_path),
           debugstr_w(mpc), debugstr_w(oem_id), info, pid, dpid3, dpid4 );

    if (!info || info->size < sizeof(*info) || !info->groups || !info->group_count) return E_INVALIDARG;
    return generate_ids( key, config_path, mpc, pid, dpid3, dpid4 );
}
